#include "Session.h"

#include "ControlParameterApplier.h"
#include "engine/AudioEngine.h"
#include "engine/AudioGraphRuntime.h"
#include "engine/BypassableInstance.h"
#include "engine/PluginHost.h"

#include <algorithm>
#include <utility>

namespace pedalboard3::session
{

namespace
{

constexpr double kFallbackSampleRate = 48000.0;
constexpr int kFallbackBlockSize = 512;

using AudioGraph = juce::AudioProcessorGraph;
using UpdateKind = AudioGraph::UpdateKind;

struct DeviceConfig
{
    double sampleRate;
    int blockSize;
};

DeviceConfig deriveConfig(engine::AudioEngine& eng)
{
    if (auto* dm = eng.getDeviceManager())
    {
        if (auto* dev = dm->getCurrentAudioDevice())
        {
            const auto sr = dev->getCurrentSampleRate();
            const auto bs = dev->getCurrentBufferSizeSamples();
            return {sr > 0.0 ? sr : kFallbackSampleRate, bs > 0 ? bs : kFallbackBlockSize};
        }
    }
    return {kFallbackSampleRate, kFallbackBlockSize};
}

} // namespace

// -------- Result updater ---------------------------------------------------

class Session::ResultUpdater : public juce::AsyncUpdater
{
public:
    explicit ResultUpdater(Session& session) : session_(session) {}

    void handleAsyncUpdate() override { session_.onBuildResult(); }

private:
    Session& session_;
};

// -------- Worker thread ----------------------------------------------------

class Session::Worker : public juce::Thread
{
public:
    explicit Worker(Session& session) : juce::Thread("PB3 Patch Build"), session_(session) {}

    void run() override
    {
        while (!threadShouldExit())
        {
            wait(-1);
            if (threadShouldExit())
                return;

            // Drain LoadPatch first (it supersedes any pending Adds).
            std::optional<LoadPatchRequest> loadReq;
            std::optional<AddPluginRequest> addReq;
            {
                std::lock_guard<std::mutex> lk(session_.queueMutex_);
                if (session_.pendingLoadPatch_.has_value())
                {
                    loadReq = std::move(session_.pendingLoadPatch_);
                    session_.pendingLoadPatch_.reset();
                    // Drop pending adds — the load supersedes them.
                    session_.addQueue_.clear();
                }
                else if (!session_.addQueue_.empty())
                {
                    addReq = std::move(session_.addQueue_.front());
                    session_.addQueue_.pop_front();
                }
            }

            const auto cfg = deriveConfig(session_.engine_);

            if (loadReq.has_value())
            {
                LoadPatchResult lr;
                lr.patch = loadReq->patch;
                lr.prepared = session_.buildFn_(loadReq->patch, cfg.sampleRate, cfg.blockSize, lr.errorOut);
                {
                    std::lock_guard<std::mutex> lk(session_.resultMutex_);
                    session_.results_.push_back(std::move(lr));
                }
            }
            else if (addReq.has_value())
            {
                AddPluginResult ar;
                ar.kind = addReq->kind;
                ar.desc = addReq->desc;
                ar.internalType = addReq->internalType;

                // Direct host factory — bypasses BuildFn so we get a single
                // processor back, not a graph. Internal nodes go through the
                // internal factory; plugins through createInstance.
                std::unique_ptr<juce::AudioProcessor> proc;
                if (addReq->kind == NodeKind::Internal)
                    proc = session_.host_.createInternalInstance(
                        addReq->internalType, cfg.sampleRate, cfg.blockSize, ar.errorOut);
                else
                    proc = session_.host_.createInstance(addReq->desc, cfg.sampleRate, cfg.blockSize, ar.errorOut);

                if (proc != nullptr)
                {
                    ar.hosted = proc.get();
                    ar.wrapped = std::make_unique<engine::BypassableInstance>(std::move(proc));
                    // Pre-warm: prepare the wrapper at the runtime's SR/block
                    // so JUCE's AudioProcessorGraph::addNode (called on the
                    // message thread) doesn't need a slow first-block init
                    // to fire there.
                    ar.wrapped->setRateAndBufferSizeDetails(cfg.sampleRate, cfg.blockSize);
                    ar.wrapped->prepareToPlay(cfg.sampleRate, cfg.blockSize);
                }

                {
                    std::lock_guard<std::mutex> lk(session_.resultMutex_);
                    session_.results_.push_back(std::move(ar));
                }
            }
            else
            {
                continue;
            }

            if (session_.resultUpdater_ != nullptr)
                session_.resultUpdater_->triggerAsyncUpdate();
        }
    }

private:
    Session& session_;
};

// -------- Session ----------------------------------------------------------

Session::Session(engine::AudioEngine& engine, engine::PluginHost& host)
    : Session(engine,
              host,
              [&host](const Patch& p, double sr, int bs, juce::String& err)
              { return buildPreparedGraph(host, p, sr, bs, err); })
{
}

Session::Session(engine::AudioEngine& engine, engine::PluginHost& host, BuildFn buildFn)
    : engine_(engine), host_(host), buildFn_(std::move(buildFn))
{
    jassert(buildFn_);
    resultUpdater_ = std::make_unique<ResultUpdater>(*this);
    worker_ = std::make_unique<Worker>(*this);
    worker_->startThread();
    idleEvent_.signal(); // start Idle

    // Synchronous initial empty graph: builds a 4-IO-node passthrough
    // (audioIn → audioOut, midiIn → midiOut) and swaps it into the
    // engine BEFORE the ctor returns. Without this, the very first
    // addPlugin() call lands in commitAddPlugin with liveGraph_ == nullptr
    // — there's no graph to mutate. Building inline avoids an async race
    // with the user's first Load click.
    //
    // The build is fast (no plugin instantiation, just 4 IO processors
    // + 3 connections + prepareToPlay). State stays Idle, version stays
    // 0 — this is initialisation, not a transition users observe.
    juce::String warmupErr;
    const auto cfg = deriveConfig(engine_);
    auto initial = buildFn_(Patch{}, cfg.sampleRate, cfg.blockSize, warmupErr);
    if (initial.isValid())
    {
        auto previous = engine_.loadGraph(std::move(initial.graph));
        (void)previous;
        liveGraph_ = currentRuntimeGraph();
        // activePatch_ + nodes_ stay empty — no plugin nodes in the
        // passthrough graph.
    }
}

Session::~Session()
{
    if (resultUpdater_ != nullptr)
        resultUpdater_->cancelPendingUpdate();

    if (worker_ != nullptr)
    {
        worker_->signalThreadShouldExit();
        worker_->notify();
        // Join, never force-kill: stopThread with a finite timeout falls back
        // to killThread (pthread_cancel / TerminateThread), which corrupts
        // process state if the worker is inside a slow plugin createInstance /
        // prepareToPlay or holding resultMutex_. A hung plugin blocking quit
        // is the lesser evil (crash-hunt #7).
        worker_->stopThread(-1);
    }

    {
        // Destroy any undelivered results here, on the message thread —
        // plugin instances must never destruct on the worker.
        std::lock_guard<std::mutex> lk(resultMutex_);
        results_.clear();
    }

    // The node processors outlive Session (the engine owns the graph and is
    // torn down after it): stop listening while they are known alive.
    std::lock_guard<std::mutex> lk(statusMutex_);
    stopListeningToLiveNodes();
}

// ---- Public API: whole-patch swap -----------------------------------------

void Session::replacePatch(Patch p)
{
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        pendingLoadPatch_ = LoadPatchRequest{std::move(p)};
        addQueue_.clear();
    }

    if (state_.load(std::memory_order_relaxed) == State::Idle)
        publishStateChange(State::Building);

    if (worker_ != nullptr)
        worker_->notify();
}

void Session::clearPatch()
{
    replacePatch(Patch{});
}

// ---- Public API: incremental ops ------------------------------------------

void Session::enqueueAddRequest(AddPluginRequest req)
{
    bool dropped = false;
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        if (addQueue_.size() >= kAddQueueMax)
            dropped = true;
        else
            addQueue_.push_back(std::move(req));
    }

    if (dropped)
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        lastError_ = "addQueue full — request dropped";
        bumpVersionAndNotify();
        return;
    }

    if (state_.load(std::memory_order_relaxed) == State::Idle)
        publishStateChange(State::Building);

    if (worker_ != nullptr)
        worker_->notify();
}

void Session::addPlugin(juce::PluginDescription desc)
{
    AddPluginRequest req;
    req.desc = std::move(desc);
    enqueueAddRequest(std::move(req));
}

void Session::addInternalNode(juce::String internalType)
{
    AddPluginRequest req;
    req.kind = NodeKind::Internal;
    req.internalType = std::move(internalType);
    enqueueAddRequest(std::move(req));
}

void Session::removeNode(NodeId id)
{
    if (id == graphIO)
        return;

    // A live-graph mutation must not run while a rebuild is in flight. During
    // the async window after clearPatch()/replacePatch() — e.g. a Test Tone
    // toggle, which frees the previous chain graph out-of-band — the cached
    // liveGraph_ and the Node* in nodes_ can already be dangling. replacePatch
    // publishes State::Building synchronously before the old graph is freed, so
    // gating on Idle keeps removeNode off the freed memory (use-after-free).
    // Once the rebuild lands, Session is Idle again and nodes_/liveGraph_ are
    // consistent.
    if (getState() != State::Idle)
        return;

    std::lock_guard<std::mutex> lk(statusMutex_);

    // Identity guard (crash-hunt #7): nodes_' Node* live inside the graph
    // Session last committed. If anything outside Session swapped another
    // graph in (Test Tone-style loadGraph), those pointers dangle — refuse
    // the mutation instead of dereferencing freed memory. The next commit
    // re-syncs liveGraph_/nodes_. (When liveGraph_ is null there is nothing
    // to dereference, so the guard is skipped.)
    if (liveGraph_ != nullptr && currentRuntimeGraph() != liveGraph_)
        return;

    auto it = nodes_.find(id);
    if (it == nodes_.end())
        return;

    if (liveGraph_ != nullptr && it->second.hosted != nullptr)
        it->second.hosted->removeListener(this);

    if (liveGraph_ != nullptr && it->second.graphNode != nullptr)
    {
        // removeNode returns a Node::Ptr; let it fall off scope so the
        // BypassableInstance + plugin destruct on this (message) thread.
        auto removed = liveGraph_->removeNode(it->second.graphNode->nodeID, UpdateKind::async);
        (void)removed;
    }
    nodes_.erase(it);

    auto idx = activePatch_.indexOfNode(id);
    if (idx >= 0)
        activePatch_.nodes.erase(activePatch_.nodes.begin() + idx);

    pruneAndBridgeConnections(id);
    applyConnectionsToLiveGraph();

    bumpVersionAndNotify();
}

void Session::moveNode(NodeId id, int newIndex)
{
    // Reorders the node list only. Under the free-routing model, topology is
    // defined by `connections` (by NodeId), not list order, so a reorder leaves
    // the live graph untouched — no rewire. Kept for any order-sensitive views;
    // the canvas positions nodes by x/y, not order. Idle-gated for consistency.
    if (getState() != State::Idle)
        return;

    std::lock_guard<std::mutex> lk(statusMutex_);

    const auto curIdx = activePatch_.indexOfNode(id);
    if (curIdx < 0)
        return;
    const int n = static_cast<int>(activePatch_.nodes.size());
    const int target = juce::jlimit(0, n - 1, newIndex);
    if (target == curIdx)
        return;

    auto node = activePatch_.nodes[static_cast<size_t>(curIdx)];
    activePatch_.nodes.erase(activePatch_.nodes.begin() + curIdx);
    activePatch_.nodes.insert(activePatch_.nodes.begin() + target, std::move(node));

    bumpVersionAndNotify();
}

void Session::setNodeBypass(NodeId id, bool shouldBypass)
{
    // Same dangling-graph guard as removeNode: the cached BypassableInstance*
    // lives inside the live graph's node, which can be freed during a rebuild
    // (Test Tone toggle). Only touch it while Idle.
    if (getState() != State::Idle)
        return;

    std::lock_guard<std::mutex> lk(statusMutex_);

    // Identity guard (crash-hunt #7): the BypassableInstance* lives inside
    // the committed graph; refuse if an out-of-band swap made it dangle.
    if (auto* current = currentRuntimeGraph(); current == nullptr || current != liveGraph_)
        return;

    auto it = nodes_.find(id);
    if (it == nodes_.end() || it->second.bypass == nullptr)
        return;
    it->second.bypass->setBypass(shouldBypass);
    if (auto* pn = activePatch_.findNode(id))
        pn->bypass = shouldBypass;
    bumpVersionAndNotify();
}

void Session::setNodeCustomName(NodeId id, juce::String name)
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    if (auto* pn = activePatch_.findNode(id))
    {
        pn->customName = std::move(name);
        bumpVersionAndNotify();
    }
}

void Session::addMidiMapping(control::MidiMapping mapping)
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    activePatch_.midiMappings.push_back(std::move(mapping));
    mappingsRevision_.fetch_add(1, std::memory_order_release);
    bumpVersionAndNotify();
}

namespace
{
bool isParameterMappingOf(const control::MidiTarget& target, NodeId node) noexcept
{
    return target.kind == control::MidiTargetKind::PluginParameter && target.node == node;
}
} // namespace

Session::NodeMappings Session::getNodeMappings(NodeId id) const
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    NodeMappings out;
    for (const auto& m : activePatch_.midiMappings)
        if (isParameterMappingOf(m.target, id))
            out.midi.push_back(m);
    for (const auto& m : activePatch_.oscMappings)
        if (isParameterMappingOf(m.target, id))
            out.osc.push_back(m);
    return out;
}

bool Session::setNodeMappings(NodeId id, std::vector<control::MidiMapping> midi, std::vector<control::OscMapping> osc)
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    if (activePatch_.findNode(id) == nullptr)
        return false;

    std::erase_if(activePatch_.midiMappings,
                  [id](const control::MidiMapping& m) { return isParameterMappingOf(m.target, id); });
    std::erase_if(activePatch_.oscMappings,
                  [id](const control::OscMapping& m) { return isParameterMappingOf(m.target, id); });

    for (auto& m : midi)
    {
        m.target.kind = control::MidiTargetKind::PluginParameter;
        m.target.node = id;
        activePatch_.midiMappings.push_back(std::move(m));
    }
    for (auto& m : osc)
    {
        m.target.kind = control::MidiTargetKind::PluginParameter;
        m.target.node = id;
        activePatch_.oscMappings.push_back(std::move(m));
    }

    mappingsRevision_.fetch_add(1, std::memory_order_release);
    bumpVersionAndNotify();
    return true;
}

void Session::setNodeMidiChannel(NodeId id, int channel1To16Or0)
{
    const int channel = juce::jlimit(0, 16, channel1To16Or0);

    // Same guards as setNodeBypass: the cached BypassableInstance* lives inside
    // the committed graph, which a rebuild or an out-of-band swap can free.
    if (getState() != State::Idle)
        return;

    std::lock_guard<std::mutex> lk(statusMutex_);
    if (auto* current = currentRuntimeGraph(); current == nullptr || current != liveGraph_)
        return;

    auto it = nodes_.find(id);
    if (it == nodes_.end() || it->second.bypass == nullptr)
        return;
    it->second.bypass->setMidiChannel(channel);
    if (auto* pn = activePatch_.findNode(id))
        pn->midiChannel = channel;
    bumpVersionAndNotify();
}

// ---- Public API: snapshot accessors ---------------------------------------

Patch Session::getActivePatch() const
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    return activePatch_;
}

namespace
{
// In-flight-block barrier: wait until the audio thread has completed two more
// processBlocks, so any block that was already inside a node when the caller
// flipped its suspend flag has drained. Returns quickly (bounded ~100 ms) when
// no device is running — the counter simply never advances.
void waitForAudioQuiescence(engine::AudioGraphRuntime* runtime)
{
    if (runtime == nullptr)
        return;
    const auto start = runtime->getGenerationAckedByAudio();
    const auto deadlineMs = juce::Time::getMillisecondCounterHiRes() + 100.0;
    while (runtime->getGenerationAckedByAudio() < start + 2 && juce::Time::getMillisecondCounterHiRes() < deadlineMs)
        juce::Thread::sleep(1);
}
} // namespace

Patch Session::getActivePatchWithState() const
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    Patch snapshot = activePatch_;

    // Quiesce the audio path before reading plugin state (crash-hunt #7):
    // getStateInformation on a live instance races the audio thread's
    // processBlock. Suspend each wrapper — the graph's render op checks
    // isSuspended() and skips the node — then wait out any in-flight block
    // before touching state. Suspended nodes go briefly silent during save.
    std::vector<engine::BypassableInstance*> suspended;
    suspended.reserve(nodes_.size());
    for (const auto& [id, refs] : nodes_)
    {
        if (refs.bypass != nullptr && !refs.bypass->isSuspended())
        {
            refs.bypass->suspendProcessing(true);
            suspended.push_back(refs.bypass);
        }
    }
    // RAII restore: every suspended wrapper is un-suspended on the way out,
    // even if a plugin's getStateInformation throws. Leaving a node suspended
    // would silence it for the rest of the session.
    const juce::ScopeGuard unsuspendAll{[&suspended]
                                        {
                                            for (auto* wrapper : suspended)
                                                wrapper->suspendProcessing(false);
                                        }};

    if (!suspended.empty())
        waitForAudioQuiescence(engine_.getRuntime());

    // Capture each live plugin's current state into the snapshot so the
    // saved .pdl restores knob settings on the next load. nodes_ keys are
    // kept in sync with activePatch_.nodes ids by every mutation, so a
    // missing entry (e.g. mid-build) just leaves that node's state empty.
    for (auto& node : snapshot.nodes)
    {
        auto it = nodes_.find(node.id);
        if (it != nodes_.end() && it->second.hosted != nullptr)
        {
            // Clear any blob carried over from a loaded .pdl first —
            // getStateInformation appends via MemoryOutputStream in many
            // plugins, which would otherwise concatenate onto stale bytes.
            node.state.setSize(0);
            it->second.hosted->getStateInformation(node.state);
        }
    }

    return snapshot;
}

std::vector<NodeId> Session::getNodeIdsInOrder() const
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    std::vector<NodeId> result;
    result.reserve(activePatch_.nodes.size());
    for (const auto& n : activePatch_.nodes)
        result.push_back(n.id);
    return result;
}

std::vector<Session::NodeSnapshot> Session::getNodeSnapshots() const
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    std::vector<NodeSnapshot> snaps;
    snaps.reserve(activePatch_.nodes.size());
    for (const auto& n : activePatch_.nodes)
    {
        NodeSnapshot s;
        s.id = n.id;
        s.name = n.desc.name;
        if (n.kind == NodeKind::Internal)
        {
            s.name = n.internalType; // fallback if the type isn't in the table
            for (const auto& t : host_.getInternalNodeTypes())
                if (t.typeId == n.internalType)
                    s.name = t.displayName;
        }
        s.bypassed = n.bypass;
        s.customName = n.customName;
        s.midiChannel = n.midiChannel;
        s.x = n.x;
        s.y = n.y;
        snaps.push_back(std::move(s));
    }
    return snaps;
}

bool Session::hasNode(NodeId id) const noexcept
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    return nodes_.find(id) != nodes_.end();
}

juce::AudioProcessor* Session::getHostedPlugin(NodeId id) const noexcept
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    auto it = nodes_.find(id);
    return it != nodes_.end() ? it->second.hosted : nullptr;
}

bool Session::isNodeBypassed(NodeId id) const noexcept
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    auto it = nodes_.find(id);
    return it != nodes_.end() && it->second.bypass != nullptr && it->second.bypass->isBypassed();
}

int Session::getNodeCount() const noexcept
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    return static_cast<int>(activePatch_.nodes.size());
}

// ---- Public API: status / polling -----------------------------------------

Session::Status Session::getStatus() const
{
    Status s;
    s.state = state_.load(std::memory_order_acquire);
    s.version = stateVersion_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        s.nodeCount = static_cast<int>(activePatch_.nodes.size());
        s.lastError = lastError_;
    }
    return s;
}

juce::String Session::getLastError() const
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    return lastError_;
}

bool Session::waitForIdle(int timeoutMs)
{
    if (state_.load(std::memory_order_acquire) == State::Idle)
        return true;
    return idleEvent_.wait(timeoutMs);
}

void Session::dispatchPendingResult()
{
    onBuildResult();
}

// ---- Private helpers ------------------------------------------------------

void Session::publishStateChange(State newState)
{
    state_.store(newState, std::memory_order_release);
    stateVersion_.fetch_add(1, std::memory_order_release);

    if (newState == State::Idle)
        idleEvent_.signal();
    else
        idleEvent_.reset();

    sendChangeMessage();
}

void Session::bumpVersionAndNotify()
{
    stateVersion_.fetch_add(1, std::memory_order_release);
    sendChangeMessage();
}

void Session::rearmWorkerIfQueueNonEmpty()
{
    bool more = false;
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        more = pendingLoadPatch_.has_value() || !addQueue_.empty();
    }
    if (more)
    {
        publishStateChange(State::Building);
        if (worker_ != nullptr)
            worker_->notify();
    }
}

namespace
{
// The channels a node is wired on by the auto-connect convenience: stereo audio
// (0, 1) plus the MIDI pin. Mirrors linearChainConnections' threading.
constexpr int kAutoChannels[] = {0, 1, AudioGraph::midiChannelIndex};

bool connectionExists(const std::vector<PatchConnection>& v, const PatchConnection& c)
{
    return std::find(v.begin(), v.end(), c) != v.end();
}
} // namespace

void Session::autoConnectNewNode(NodeId newId)
{
    auto& conns = activePatch_.connections;

    // Sources currently feeding the graph output, per channel.
    std::vector<PatchConnection> toOut;
    for (const auto& c : conns)
        if (c.dst == graphIO)
            toOut.push_back(c);

    if (toOut.empty())
    {
        // Empty graph (or nothing reaches the output): wire input -> new -> output.
        for (int ch : kAutoChannels)
        {
            conns.push_back({graphIO, ch, newId, ch});
            conns.push_back({newId, ch, graphIO, ch});
        }
        return;
    }

    // Splice the new node in front of the output: drop each X -> out edge, add
    // X -> new, then add new -> out once per channel that fed the output. This
    // appends the node to the end of the signal flow while preserving every
    // other user-authored edge.
    conns.erase(std::remove_if(conns.begin(), conns.end(), [](const PatchConnection& c) { return c.dst == graphIO; }),
                conns.end());

    for (const auto& c : toOut)
    {
        PatchConnection intoNew{c.src, c.srcChannel, newId, c.dstChannel};
        if (!connectionExists(conns, intoNew))
            conns.push_back(intoNew);
    }
    for (int ch : kAutoChannels)
    {
        const bool fedThisChannel =
            std::any_of(toOut.begin(), toOut.end(), [ch](const PatchConnection& c) { return c.dstChannel == ch; });
        if (fedThisChannel)
            conns.push_back({newId, ch, graphIO, ch});
    }
}

void Session::pruneAndBridgeConnections(NodeId removed)
{
    auto& conns = activePatch_.connections;

    // Capture the removed node's in/out edges before dropping them, so we can
    // bridge its neighbours (remove a middle node -> its source feeds its sink).
    std::vector<PatchConnection> inEdges, outEdges;
    for (const auto& c : conns)
    {
        if (c.dst == removed)
            inEdges.push_back(c);
        if (c.src == removed)
            outEdges.push_back(c);
    }

    conns.erase(std::remove_if(conns.begin(),
                               conns.end(),
                               [removed](const PatchConnection& c) { return c.src == removed || c.dst == removed; }),
                conns.end());

    // Bridge across the removed node, matching its pin channel (in.dst == out.src).
    for (const auto& in : inEdges)
        for (const auto& out : outEdges)
        {
            if (in.dstChannel != out.srcChannel)
                continue;
            PatchConnection bridge{in.src, in.srcChannel, out.dst, out.dstChannel};
            if (bridge.src != bridge.dst && !connectionExists(conns, bridge))
                conns.push_back(bridge);
        }
}

bool Session::addConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel)
{
    if (getState() != State::Idle)
        return false;

    // Cross-signal (audio<->MIDI) connections are never valid.
    const bool srcMidi = (srcChannel == AudioGraph::midiChannelIndex);
    const bool dstMidi = (dstChannel == AudioGraph::midiChannelIndex);
    if (srcMidi != dstMidi)
        return false;

    std::lock_guard<std::mutex> lk(statusMutex_);

    const PatchConnection c{src, srcChannel, dst, dstChannel};
    if (connectionExists(activePatch_.connections, c))
        return false;

    // Trial-validate: rejects self-loops on real nodes, cycles, dangling
    // endpoints, and negative channels (graphIO -> graphIO passthrough is fine).
    Patch trial = activePatch_;
    trial.connections.push_back(c);
    if (validatePatch(trial) != PatchValidation::Ok)
        return false;

    activePatch_.connections.push_back(c);
    applyConnectionsToLiveGraph();
    bumpVersionAndNotify();
    return true;
}

void Session::removeConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel)
{
    if (getState() != State::Idle)
        return;

    std::lock_guard<std::mutex> lk(statusMutex_);

    const PatchConnection c{src, srcChannel, dst, dstChannel};
    auto& v = activePatch_.connections;
    const auto it = std::remove(v.begin(), v.end(), c);
    if (it == v.end())
        return; // nothing matched

    v.erase(it, v.end());
    applyConnectionsToLiveGraph();
    bumpVersionAndNotify();
}

void Session::setNodePosition(NodeId id, float x, float y)
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    auto* n = activePatch_.findNode(id);
    if (n == nullptr)
        return;
    n->x = x;
    n->y = y;
    bumpVersionAndNotify();
}

void Session::seedNodePositions(const std::vector<NodePositionSeed>& seeds)
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    bool applied = false;
    for (const auto& seed : seeds)
    {
        auto* n = activePatch_.findNode(seed.id);
        if (n == nullptr || n->hasPosition())
            continue; // never overwrite a user-authored (or already-seeded) position
        n->x = seed.x;
        n->y = seed.y;
        applied = true;
    }
    if (applied)
        bumpVersionAndNotify();
}

juce::AudioProcessorGraph* Session::currentRuntimeGraph() const noexcept
{
    auto* runtime = engine_.getRuntime();
    return runtime != nullptr ? runtime->getLiveGraph() : nullptr;
}

void Session::applyConnectionsToLiveGraph()
{
    // Verify, don't adopt (crash-hunt #7): the graphNode pointers resolved
    // below live inside the graph Session last committed. Adopting whatever
    // graph the runtime currently holds (the old refresh behaviour) would
    // dereference those pointers against a foreign graph after an
    // out-of-band swap (Test Tone). If the runtime's graph isn't ours,
    // leave it alone — the next commit re-syncs.
    auto* current = currentRuntimeGraph();
    if (current == nullptr)
    {
        liveGraph_ = nullptr;
        return;
    }
    if (current != liveGraph_)
        return;

    // Cache the four I/O processor NodeIDs once — they're stable across
    // mutations (AudioGraphIOProcessor nodes added at graph build time).
    AudioGraph::NodeID audioIn, audioOut, midiIn, midiOut;
    for (auto* node : liveGraph_->getNodes())
    {
        if (auto* io = dynamic_cast<AudioGraph::AudioGraphIOProcessor*>(node->getProcessor()))
        {
            using IO = AudioGraph::AudioGraphIOProcessor;
            switch (io->getType())
            {
            case IO::audioInputNode:
                audioIn = node->nodeID;
                break;
            case IO::audioOutputNode:
                audioOut = node->nodeID;
                break;
            case IO::midiInputNode:
                midiIn = node->nodeID;
                break;
            case IO::midiOutputNode:
                midiOut = node->nodeID;
                break;
            }
        }
    }

    // Clear all existing connections, then add the new linear-chain shape.
    auto existing = liveGraph_->getConnections();
    for (const auto& c : existing)
        liveGraph_->removeConnection(c, UpdateKind::async);

    const auto& conns = activePatch_.connections;
    for (size_t i = 0; i < conns.size(); ++i)
    {
        const auto& c = conns[i];
        const bool isLast = (i == conns.size() - 1);
        const bool isMidi = (c.srcChannel == AudioGraph::midiChannelIndex);

        AudioGraph::NodeID resolvedSrc;
        if (c.src == graphIO)
        {
            resolvedSrc = isMidi ? midiIn : audioIn;
        }
        else
        {
            auto it = nodes_.find(c.src);
            if (it == nodes_.end() || it->second.graphNode == nullptr)
                continue;
            resolvedSrc = it->second.graphNode->nodeID;
        }

        AudioGraph::NodeID resolvedDst;
        if (c.dst == graphIO)
        {
            resolvedDst = isMidi ? midiOut : audioOut;
        }
        else
        {
            auto it = nodes_.find(c.dst);
            if (it == nodes_.end() || it->second.graphNode == nullptr)
                continue;
            resolvedDst = it->second.graphNode->nodeID;
        }

        liveGraph_->addConnection({{resolvedSrc, c.srcChannel}, {resolvedDst, c.dstChannel}},
                                  isLast ? UpdateKind::sync : UpdateKind::async);
    }

    if (conns.empty())
        liveGraph_->rebuild();
}

void Session::commitLoadPatch(LoadPatchResult&& result)
{
    publishStateChange(State::Swapping);

    if (!result.prepared.isValid())
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        lastError_ = result.errorOut.isNotEmpty() ? result.errorOut : juce::String("Patch build failed");
        publishStateChange(State::Idle);
        return;
    }

    {
        // Detach from the outgoing processors while they are still alive: the
        // previous graph is destroyed when this function returns.
        std::lock_guard<std::mutex> lk(statusMutex_);
        stopListeningToLiveNodes();
    }

    auto previous = engine_.loadGraph(std::move(result.prepared.graph));
    (void)previous;

    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        activePatch_ = std::move(result.patch);
        nodes_ = std::move(result.prepared.nodes);
        liveGraph_ = currentRuntimeGraph();
        for (const auto& entry : nodes_)
            listenTo(entry.second);
        lastError_.clear();
    }

    patchGeneration_.fetch_add(1, std::memory_order_release);
    // A whole-patch load always replaces the mapping tables wholesale (even
    // when the new patch has none), so this bumps unconditionally alongside
    // patchGeneration_ — a mapping-focused listener (ControlWiring) re-feeds
    // on every load, not just ones that happen to carry mappings.
    mappingsRevision_.fetch_add(1, std::memory_order_release);
    publishStateChange(State::Idle);
}

void Session::commitAddPlugin(AddPluginResult&& result)
{
    publishStateChange(State::Swapping);

    if (result.wrapped == nullptr)
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        lastError_ = result.errorOut.isNotEmpty() ? result.errorOut : juce::String("addPlugin failed");
        publishStateChange(State::Idle);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(statusMutex_);

        // Refresh liveGraph_ from the runtime's authoritative atomic
        // pointer. The cached field can become stale when anything
        // outside Session calls AudioEngine::loadGraph — e.g. the
        // Test Tone diagnostic in TransportBar swaps a non-chain graph
        // in directly, then `loadGraph(nullptr)` on toggle-off clears
        // it. Either path leaves liveGraph_ pointing at freed memory.
        // Adopting the runtime's graph is only safe while Session holds
        // no committed nodes — with nodes present, their graphNode
        // pointers belong to the old graph and applyConnections below
        // would dereference them against the foreign one (crash-hunt #7).
        auto* current = currentRuntimeGraph();
        if (current != liveGraph_ && !nodes_.empty())
        {
            lastError_ = "addPlugin: live graph changed outside Session";
            publishStateChange(State::Idle);
            return;
        }
        liveGraph_ = current;

        if (liveGraph_ == nullptr)
        {
            lastError_ = "addPlugin: no live graph";
            publishStateChange(State::Idle);
            return;
        }

        auto* hostedPtr = result.hosted;
        auto* wrapPtr = result.wrapped.get();
        auto graphNode = liveGraph_->addNode(std::move(result.wrapped), {}, UpdateKind::async);
        if (graphNode == nullptr)
        {
            lastError_ = "addPlugin: graph addNode failed";
            publishStateChange(State::Idle);
            return;
        }

        const auto newId = activePatch_.nextNodeId();
        PatchNode pn;
        pn.id = newId;
        pn.kind = result.kind;
        if (result.kind == NodeKind::Internal)
            pn.internalType = std::move(result.internalType);
        else
            pn.desc = std::move(result.desc);
        activePatch_.nodes.push_back(std::move(pn));

        nodes_[newId] = PreparedGraph::NodeRefs{wrapPtr, hostedPtr, graphNode.get()};
        listenTo(nodes_[newId]);

        autoConnectNewNode(newId);
    }

    applyConnectionsToLiveGraph();

    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        lastError_.clear();
    }
    publishStateChange(State::Idle);
}

// ---- Hand-edit detection (finish pass U2) ----------------------------------

void Session::listenTo(const PreparedGraph::NodeRefs& refs)
{
    if (refs.hosted != nullptr)
        refs.hosted->addListener(this);
}

void Session::stopListeningToLiveNodes()
{
    // If a graph was swapped in outside Session (Test Tone), the processors in
    // nodes_ were destroyed with the old graph.
    if (liveGraph_ == nullptr || currentRuntimeGraph() != liveGraph_)
        return;
    for (const auto& entry : nodes_)
        if (entry.second.hosted != nullptr)
            entry.second.hosted->removeListener(this);
}

void Session::audioProcessorParameterChanged(juce::AudioProcessor*, int, float)
{
    // Hand edits come from the message thread. Host automation, the audio
    // thread and worker-side state loads call from elsewhere, and a mapped
    // MIDI / OSC input sets parameters inside ControlParameterApplier::apply.
    if (juce::MessageManager::existsAndIsCurrentThread() && !ControlParameterApplier::isApplyingOnThisThread())
        parameterEdits_.sendChangeMessage();
}

void Session::audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int)
{
    // The end of a drag in the plugin's editor, whichever thread the plugin
    // reports it from. Control inputs never send gestures.
    parameterEdits_.sendChangeMessage();
}

void Session::onBuildResult()
{
    // Drain every queued result — the worker may have produced several while
    // the message thread was busy (each is destroyed or committed here, on
    // the message thread, never on the worker).
    for (;;)
    {
        Result result;
        {
            std::lock_guard<std::mutex> lk(resultMutex_);
            if (results_.empty())
                break;
            result = std::move(results_.front());
            results_.pop_front();
        }

        if (std::holds_alternative<std::monostate>(result))
            continue;

        // Coalescing check — if a newer LoadPatch is pending, drop this
        // result (RAII destroys any prepared instance/graph here).
        {
            std::lock_guard<std::mutex> lk(queueMutex_);
            if (pendingLoadPatch_.has_value())
            {
                if (worker_ != nullptr)
                    worker_->notify();
                continue; // discard `result` via RAII; keep draining
            }
        }

        if (std::holds_alternative<LoadPatchResult>(result))
            commitLoadPatch(std::get<LoadPatchResult>(std::move(result)));
        else if (std::holds_alternative<AddPluginResult>(result))
            commitAddPlugin(std::get<AddPluginResult>(std::move(result)));
    }

    rearmWorkerIfQueueNonEmpty();
}

} // namespace pedalboard3::session
