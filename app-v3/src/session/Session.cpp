#include "Session.h"

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
constexpr int kStereoChannels = 2;

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

// Recompute a linear-chain `connections` vector from a node ordering.
// Stereo audio + MIDI through every node in order: graphIO → node[0] →
// node[1] → ... → graphIO.
std::vector<PatchConnection> computeLinearConnections(const std::vector<PatchNode>& nodes)
{
    std::vector<PatchConnection> conns;
    if (nodes.empty())
    {
        // Audio passthrough.
        conns.push_back({graphIO, 0, graphIO, 0});
        conns.push_back({graphIO, 1, graphIO, 1});
        // MIDI passthrough.
        conns.push_back({graphIO, AudioGraph::midiChannelIndex, graphIO, AudioGraph::midiChannelIndex});
        return conns;
    }

    // Stereo audio: in → first node, last → out, intermediates form the chain.
    for (int ch = 0; ch < kStereoChannels; ++ch)
    {
        conns.push_back({graphIO, ch, nodes.front().id, ch});
        for (size_t i = 0; i + 1 < nodes.size(); ++i)
            conns.push_back({nodes[i].id, ch, nodes[i + 1].id, ch});
        conns.push_back({nodes.back().id, ch, graphIO, ch});
    }

    // MIDI follows the same shape (master plan §4.5 v2 parity, not broadcast).
    conns.push_back({graphIO, AudioGraph::midiChannelIndex, nodes.front().id, AudioGraph::midiChannelIndex});
    for (size_t i = 0; i + 1 < nodes.size(); ++i)
        conns.push_back({nodes[i].id, AudioGraph::midiChannelIndex, nodes[i + 1].id, AudioGraph::midiChannelIndex});
    conns.push_back({nodes.back().id, AudioGraph::midiChannelIndex, graphIO, AudioGraph::midiChannelIndex});

    return conns;
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
                    session_.result_ = std::move(lr);
                }
            }
            else if (addReq.has_value())
            {
                AddPluginResult ar;
                ar.desc = addReq->desc;

                // Direct host.createInstance — bypasses BuildFn so we
                // get a single AudioPluginInstance back, not a graph.
                auto plugin = session_.host_.createInstance(addReq->desc, cfg.sampleRate, cfg.blockSize, ar.errorOut);
                if (plugin != nullptr)
                {
                    ar.hosted = plugin.get();
                    ar.wrapped = std::make_unique<engine::BypassableInstance>(std::move(plugin));
                    // Pre-warm: prepare the wrapper at the runtime's SR/block
                    // so JUCE's AudioProcessorGraph::addNode (called on the
                    // message thread) doesn't need a slow first-block init
                    // to fire there.
                    ar.wrapped->setRateAndBufferSizeDetails(cfg.sampleRate, cfg.blockSize);
                    ar.wrapped->prepareToPlay(cfg.sampleRate, cfg.blockSize);
                }

                {
                    std::lock_guard<std::mutex> lk(session_.resultMutex_);
                    session_.result_ = std::move(ar);
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
        liveGraph_ = engine_.getRuntime() != nullptr ? engine_.getRuntime()->getLiveGraph() : nullptr;
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
        worker_->stopThread(5000);
    }

    {
        std::lock_guard<std::mutex> lk(resultMutex_);
        result_ = std::monostate{};
    }
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

void Session::addPlugin(juce::PluginDescription desc)
{
    bool dropped = false;
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        if (addQueue_.size() >= kAddQueueMax)
        {
            dropped = true;
        }
        else
        {
            addQueue_.push_back(AddPluginRequest{std::move(desc)});
        }
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

void Session::removeNode(NodeId id)
{
    if (id == graphIO)
        return;

    std::lock_guard<std::mutex> lk(statusMutex_);

    auto it = nodes_.find(id);
    if (it == nodes_.end())
        return;

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

    recomputeLinearConnections();
    applyConnectionsToLiveGraph();

    bumpVersionAndNotify();
}

void Session::moveNode(NodeId id, int newIndex)
{
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

    recomputeLinearConnections();
    applyConnectionsToLiveGraph();

    bumpVersionAndNotify();
}

void Session::setNodeBypass(NodeId id, bool shouldBypass)
{
    std::lock_guard<std::mutex> lk(statusMutex_);
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

// ---- Public API: snapshot accessors ---------------------------------------

Patch Session::getActivePatch() const
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    return activePatch_;
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
        s.bypassed = n.bypass;
        s.customName = n.customName;
        snaps.push_back(std::move(s));
    }
    return snaps;
}

bool Session::hasNode(NodeId id) const noexcept
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    return nodes_.find(id) != nodes_.end();
}

juce::AudioPluginInstance* Session::getHostedPlugin(NodeId id) const noexcept
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

void Session::recomputeLinearConnections()
{
    activePatch_.connections = computeLinearConnections(activePatch_.nodes);
}

void Session::applyConnectionsToLiveGraph()
{
    if (liveGraph_ == nullptr)
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

    auto previous = engine_.loadGraph(std::move(result.prepared.graph));
    (void)previous;

    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        activePatch_ = std::move(result.patch);
        nodes_ = std::move(result.prepared.nodes);
        liveGraph_ = engine_.getRuntime() != nullptr ? engine_.getRuntime()->getLiveGraph() : nullptr;
        lastError_.clear();
    }

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
        pn.desc = std::move(result.desc);
        activePatch_.nodes.push_back(std::move(pn));

        nodes_[newId] = PreparedGraph::NodeRefs{wrapPtr, hostedPtr, graphNode.get()};

        recomputeLinearConnections();
    }

    applyConnectionsToLiveGraph();

    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        lastError_.clear();
    }
    publishStateChange(State::Idle);
}

void Session::onBuildResult()
{
    Result result;
    {
        std::lock_guard<std::mutex> lk(resultMutex_);
        result = std::move(result_);
        result_ = std::monostate{};
    }

    if (std::holds_alternative<std::monostate>(result))
        return;

    // Coalescing check — if a newer LoadPatch is pending, drop this
    // result (RAII destroys any prepared instance/graph here).
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        if (pendingLoadPatch_.has_value())
        {
            if (worker_ != nullptr)
                worker_->notify();
            return; // discard `result` via RAII as we exit
        }
    }

    if (std::holds_alternative<LoadPatchResult>(result))
        commitLoadPatch(std::get<LoadPatchResult>(std::move(result)));
    else if (std::holds_alternative<AddPluginResult>(result))
        commitAddPlugin(std::get<AddPluginResult>(std::move(result)));

    rearmWorkerIfQueueNonEmpty();
}

} // namespace pedalboard3::session
