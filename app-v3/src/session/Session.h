#pragma once

#include "NodeId.h"
#include "Patch.h"
#include "PreparedGraph.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace pedalboard3::engine
{
class AudioEngine;
class PluginHost;
} // namespace pedalboard3::engine

namespace pedalboard3::session
{

// The patch-switch state machine specified by master rewrite plan
// §4.1.1, generalised in Phase 1.D.3 from a single-plugin slot to an
// N-plugin chain. Owns the active Patch (specification) and the
// per-NodeId observer pointers into the live graph (realisation).
//
// Two mutation paths:
//
// 1. **Whole-patch swap** via `replacePatch(Patch)` / `clearPatch()`.
//    Worker thread builds the new graph from scratch; message thread
//    commits via engine::AudioGraphRuntime::swapPreparedGraph (1.D.2's
//    audio-thread-side all-notes-off + atomic handshake). Existing
//    plugin instances are destroyed — state preservation across
//    replacePatch is Phase 2's concern (Node::Ptr reparenting requires
//    a JUCE patch).
//
// 2. **Incremental mutation** via `addPlugin`, `removeNode`,
//    `moveNode`, `setNodeBypass`, `setNodeCustomName`. Preserves
//    existing live plugin instances. addPlugin runs on the worker
//    thread (instantiation is the slow part); the rest are synchronous
//    on the message thread. JUCE's AudioProcessorGraph::* mutation
//    APIs handle audio-thread visibility (see deepening notes on
//    UpdateKind::async + sync-on-last-call).
//
// State machine: Idle / Building / Swapping. Synchronous ops keep
// state == Idle but bump stateVersion_ so polling harnesses see the
// change.
//
// Pointer stability: getHostedPlugin(NodeId) returns a stable pointer
// across moveNode + setNodeBypass + setNodeCustomName. Invalidated by
// removeNode, clearPatch, replacePatch. Re-fetch after those.
//
// Lifetime invariants (preserved from 1.D.2):
//   - ~Session cancels any pending AsyncUpdater callback BEFORE joining
//     the worker thread, BEFORE engine::AudioEngine teardown.
//   - The captured BuildFn must outlive the Session.
//
// Not RT-safe. Public API is message-thread only with the noted
// exceptions for the polling helpers.
class Session : public juce::ChangeBroadcaster, private juce::AudioProcessorListener
{
public:
    enum class State
    {
        Idle,
        Building,
        Swapping
    };

    struct Status
    {
        State state{State::Idle};
        int nodeCount{0};
        juce::String lastError;
        std::uint64_t version{0};
    };

    struct NodeSnapshot
    {
        NodeId id;
        juce::String name;
        bool bypassed{false};
        juce::String customName;
        int midiChannel{0}; // 0 = omni, 1..16 (PatchNode::midiChannel)

        // What the card says (UI refresh U7).
        bool builtIn{false};       // an internal node (NodeKind::Internal)
        juce::String internalType; // its typeId, for built-ins
        juce::String category;     // PluginDescription::category, for plugins
        juce::String format;       // PluginDescription::pluginFormatName
        juce::String vendor;       // PluginDescription::manufacturerName

        // Canvas position (NaN = unpositioned → SignalCanvas auto-lays-out).
        float x{std::numeric_limits<float>::quiet_NaN()};
        float y{std::numeric_limits<float>::quiet_NaN()};
        bool hasPosition() const noexcept { return !std::isnan(x) && !std::isnan(y); }
    };

    // The seam Unit 3 calls out: the function the worker thread invokes
    // to build a PreparedGraph from a Patch (used by replacePatch).
    // Production wraps session::buildPreparedGraph bound to a PluginHost;
    // tests inject fast fakes.
    using BuildFn =
        std::function<PreparedGraph(const Patch& patch, double sampleRate, int blockSize, juce::String& errorOut)>;

    // Production constructor — captures host_ for both the whole-patch
    // BuildFn closure AND the per-plugin createInstance path used by
    // addPlugin.
    Session(engine::AudioEngine& engine, engine::PluginHost& host);

    // Test / integration-harness constructor (public — not friend-only).
    // BuildFn covers the replacePatch path; addPlugin still uses host_
    // (so tests that exercise addPlugin must pass a real PluginHost,
    // even if scanning is empty — the createInstance call surfaces an
    // error rather than crashing).
    Session(engine::AudioEngine& engine, engine::PluginHost& host, BuildFn buildFn);

    ~Session() override;

    // ---- Whole-patch swap (worker + atomic graph swap) ---------------------

    // Atomic replacement. Coalesces with any pending replacePatch +
    // discards any pending addPlugin requests (the new patch supersedes).
    // May be called from any thread.
    void replacePatch(Patch);

    // Equivalent to replacePatch(Patch{}).
    void clearPatch();

    // ---- Incremental mutation (preserve existing instances) ----------------

    // Append a plugin to the active patch's chain. Worker instantiates
    // off the message thread; message thread commits incrementally
    // (no whole-graph rebuild). Errors surface via lastError + a change
    // message; the active patch is unchanged on failure.
    //
    // `position`: where the node's card goes on the canvas (model coordinates,
    // ui/CanvasLayout.h), e.g. a drop from the plugin browser (UI refresh U5).
    // Without one the canvas gives it an auto slot.
    void addPlugin(juce::PluginDescription, std::optional<juce::Point<float>> position = std::nullopt);

    // Append a built-in (internal) node to the active patch's chain, by its
    // typeId (e.g. "LevelProcessor"). Same worker pipeline as addPlugin — the
    // worker calls PluginHost::createInternalInstance instead of createInstance.
    void addInternalNode(juce::String internalType, std::optional<juce::Point<float>> position = std::nullopt);

    // Synchronous: take effect before the call returns.
    void removeNode(NodeId);
    void moveNode(NodeId, int newIndex);
    void setNodeBypass(NodeId, bool);
    void setNodeCustomName(NodeId, juce::String);

    // ---- Undo support (finish pass U7, EditHistory) ------------------------

    // Put a removed node back exactly: instantiated through the add worker
    // (plugin or built-in, with its state blob, bypass and MIDI channel) and
    // committed under node.id at `index` in the node list, with its name and
    // position, and the patch's connections replaced by `connections`. Fails
    // (lastError, patch unchanged) if the id is taken or the connections don't
    // validate with the node back in.
    void restoreNode(PatchNode node, int index, std::vector<PatchConnection> connections);

    // Replace the whole connection set (Idle only). Returns false, changing
    // nothing, if the set doesn't validate.
    bool setConnections(std::vector<PatchConnection> connections);

    // One node of the active patch with its live plugin state captured (only
    // that node is quiesced; see getActivePatchWithState), or nullopt.
    std::optional<PatchNode> getNodeWithState(NodeId) const;

    // Every add that committed (addPlugin / addInternalNode, or restoreNode
    // landing), numbered from 1, with the connections just before it. Session
    // keeps the most recent kCommittedAddLog.
    struct CommittedAdd
    {
        std::uint64_t sequence{0};
        NodeId id{};
        bool restored{false};
        std::vector<PatchConnection> connectionsBefore;
    };
    static constexpr std::size_t kCommittedAddLog = 32;
    std::vector<CommittedAdd> getCommittedAddsSince(std::uint64_t sequence) const;
    std::uint64_t getLastCommittedAddSequence() const;

    // Append a MIDI mapping to the active patch (MIDI learn's persist step).
    // Bumps the mappings revision (not just stateVersion_) so ControlWiring
    // re-feeds ControlHub with the new mapping live immediately.
    void addMidiMapping(control::MidiMapping);

    // The plugin-parameter mappings (MIDI and OSC) that target one node: what
    // the per-plugin Mappings dialog edits (roadmap M4 U2/U4).
    struct NodeMappings
    {
        std::vector<control::MidiMapping> midi;
        std::vector<control::OscMapping> osc;
    };
    NodeMappings getNodeMappings(NodeId) const;

    // Replace every PluginParameter mapping that targets `node` with the given
    // ones; their targets are forced to that node as plugin-parameter targets.
    // Other nodes' mappings and PatchSelect / AppCommand mappings are left
    // alone. Bumps the mappings revision so ControlWiring re-feeds the hub
    // live. Returns false, changing nothing, if the node is not in the active
    // patch.
    bool setNodeMappings(NodeId node, std::vector<control::MidiMapping> midi, std::vector<control::OscMapping> osc);

    // Per-node MIDI channel filter (0 = omni, 1..16; clamped): applied to the
    // node's live wrapper and recorded in the active patch so it persists.
    // Same guards as setNodeBypass (Idle, committed graph).
    void setNodeMidiChannel(NodeId, int channel1To16Or0);

    // ---- Free-routing edits (interactive canvas, message thread) -----------

    // Create a user-authored connection. Returns false (patch unchanged) if the
    // edge is invalid: cross-signal (audio<->MIDI), a duplicate, a self-loop, or
    // would make the graph fail validatePatch (cycle / dangling / bad channel).
    bool addConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel);
    void removeConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel);

    // Update a node's canvas position (no graph impact; persisted on save).
    void setNodePosition(NodeId, float x, float y);

    // Seed canvas positions for nodes that don't have one yet, as a single
    // batch (one change message). SignalCanvas calls this with its first
    // auto-layout so every card immediately owns a real, persisted position —
    // otherwise a later drag of one card reflows the remaining auto-slotted
    // cards underneath it. Nodes that already have a position, and unknown
    // ids, are left untouched; a no-op batch sends no change message.
    struct NodePositionSeed
    {
        NodeId id;
        float x;
        float y;
    };
    void seedNodePositions(const std::vector<NodePositionSeed>& seeds);

    // ---- Snapshot accessors (message thread) -------------------------------

    Patch getActivePatch() const; // heavyweight copy

    // Like getActivePatch(), but also captures each live plugin's current
    // state blob (getStateInformation) into PatchNode.state — the snapshot
    // used when saving to a .pdl so a later load restores knob settings.
    // Message-thread only; calls getStateInformation under the status lock.
    Patch getActivePatchWithState() const;

    std::vector<NodeId> getNodeIdsInOrder() const; // cheap polling
    std::vector<NodeSnapshot> getNodeSnapshots() const;

    // Monotonic count of committed whole-patch swaps (replacePatch /
    // clearPatch landing). Incremental mutations don't bump it. UI uses this
    // to detect "everything was replaced": NodeIds can be reused across a
    // swap for different plugins, so per-id reconciliation alone would leave
    // a stale editor pointing at a destroyed processor (crash-hunt #7).
    std::uint64_t getPatchGeneration() const noexcept { return patchGeneration_.load(std::memory_order_acquire); }

    // Monotonic count of changes to the active patch's MIDI/OSC mapping
    // tables specifically — bumped on every whole-patch load (mappings are
    // wholesale replaced) and on any incremental mapping mutation (e.g.
    // MIDI learn appending one mapping). A finer-grained sibling of
    // getPatchGeneration(): a node-position drag or bypass toggle bumps
    // stateVersion_ but never this, so a listener that only cares about
    // mapping content (ControlWiring) doesn't re-feed on unrelated churn.
    std::uint64_t getMappingsRevision() const noexcept { return mappingsRevision_.load(std::memory_order_acquire); }

    // Broadcasts (coalesced) when a live plugin parameter was edited by hand:
    // a change made on the message thread outside a control input
    // (ControlParameterApplier), or the end of an edit gesture in the
    // plugin's editor. A mapped MIDI / OSC input and worker-side state loads
    // don't count (finish pass KD1). Separate from Session's own broadcast so
    // a knob drag doesn't wake every Session listener.
    juce::ChangeBroadcaster& parameterEdits() noexcept { return parameterEdits_; }

    bool hasNode(NodeId) const noexcept;
    // The live processor for a node, or nullptr. juce::AudioProcessor (not
    // AudioPluginInstance) so it covers internal nodes too; callers wanting
    // plugin-only APIs must dynamic_cast.
    juce::AudioProcessor* getHostedPlugin(NodeId) const noexcept;
    bool isNodeBypassed(NodeId) const noexcept;
    int getNodeCount() const noexcept;

    // ---- Polling primitives (lock-free / wait-event) -----------------------

    State getState() const noexcept { return state_.load(std::memory_order_acquire); }
    std::uint64_t getStateVersion() const noexcept { return stateVersion_.load(std::memory_order_acquire); }
    Status getStatus() const;
    juce::String getLastError() const;

    bool waitForIdle(int timeoutMs);
    void dispatchPendingResult();

private:
    enum class RequestKind
    {
        LoadPatch,
        AddPlugin
    };

    struct LoadPatchRequest
    {
        Patch patch;
    };

    struct AddPluginRequest
    {
        juce::PluginDescription desc; // used when kind == Plugin
        NodeKind kind{NodeKind::Plugin};
        juce::String internalType; // used when kind == Internal
        // restoreNode: the node to put back (kind, desc and internalType above
        // are copied from it), where it goes and the wiring to restore.
        std::optional<PatchNode> restore;
        int restoreIndex{-1};
        std::vector<PatchConnection> restoreConnections;
        std::optional<juce::Point<float>> position; // a positioned add (UI refresh U5)
    };

    struct LoadPatchResult
    {
        Patch patch;
        PreparedGraph prepared;
        juce::String errorOut;
    };

    struct AddPluginResult
    {
        juce::PluginDescription desc; // used when kind == Plugin
        NodeKind kind{NodeKind::Plugin};
        juce::String internalType; // used when kind == Internal
        std::unique_ptr<engine::BypassableInstance> wrapped;
        juce::AudioProcessor* hosted{nullptr};
        juce::String errorOut;
        std::optional<PatchNode> restore; // carried over from the request
        int restoreIndex{-1};
        std::vector<PatchConnection> restoreConnections;
        std::optional<juce::Point<float>> position;
    };

    using Result = std::variant<std::monostate, LoadPatchResult, AddPluginResult>;

    class Worker;
    class ResultUpdater;

    // Worker → message thread hand-off via AsyncUpdater (lifetime-safe).
    void onBuildResult(); // message thread

    void publishStateChange(State newState);
    void bumpVersionAndNotify();
    void rearmWorkerIfQueueNonEmpty();

    void commitLoadPatch(LoadPatchResult&&);
    void commitAddPlugin(AddPluginResult&&);

    // Shared enqueue path for addPlugin / addInternalNode (bounded FIFO + drop
    // handling + Idle->Building transition).
    void enqueueAddRequest(AddPluginRequest);

    // Helpers used during incremental mutation on the message thread. Connections
    // are user-authored (free graph), so adds/removes mutate them surgically
    // rather than recomputing a linear chain.
    void autoConnectNewNode(NodeId newId);          // splice a new node before the graph output
    void pruneAndBridgeConnections(NodeId removed); // drop a node's edges, bridge its neighbours
    void applyConnectionsToLiveGraph();

    // The runtime's currently-live graph pointer, or nullptr. The identity
    // guards (crash-hunt #7) compare this against the cached liveGraph_ to
    // reject a graph swapped in outside Session (Test Tone) before touching
    // cached Node* that belong only to Session's own committed graph.
    juce::AudioProcessorGraph* currentRuntimeGraph() const noexcept;

    // Hand-edit detection: Session listens to every live node's hosted
    // processor (see parameterEdits()).
    void audioProcessorParameterChanged(juce::AudioProcessor*, int parameterIndex, float newValue) override;
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int parameterIndex) override;
    void listenTo(const PreparedGraph::NodeRefs&);
    // Stop listening to every node in nodes_, but only while their processors
    // are known alive: the graph Session committed is still the runtime's
    // (crash-hunt #7 identity guard). Requires statusMutex_.
    void stopListeningToLiveNodes();

    static int stereoChannel(int ch) noexcept { return ch; }

    engine::AudioEngine& engine_;
    engine::PluginHost& host_;
    BuildFn buildFn_;

    // Mailbox: latest-wins for LoadPatch (most disruptive).
    // FIFO for AddPlugin (each is independent), bounded at kAddQueueMax.
    static constexpr size_t kAddQueueMax = 16;
    mutable std::mutex queueMutex_;
    std::optional<LoadPatchRequest> pendingLoadPatch_;
    std::deque<AddPluginRequest> addQueue_;

    // Worker → message-thread result hand-off. A queue, not a single slot:
    // the worker can finish a second build before onBuildResult consumes the
    // first (notify() is sticky), and overwriting a slot would destroy a
    // live plugin instance on the worker thread and silently lose an add.
    // onBuildResult drains the whole queue on the message thread.
    mutable std::mutex resultMutex_;
    std::deque<Result> results_;

    // Active patch + live graph observers. activePatch_ is the
    // canonical spec; nodes_ map keys must equal activePatch_.nodes ids
    // after every successful mutation.
    mutable std::mutex statusMutex_; // protects activePatch_, nodes_, lastError_
    Patch activePatch_;
    std::unordered_map<NodeId, PreparedGraph::NodeRefs> nodes_;
    juce::AudioProcessorGraph* liveGraph_{nullptr}; // non-owning
    juce::String lastError_;
    std::deque<CommittedAdd> committedAdds_; // the last kCommittedAddLog, oldest first
    std::uint64_t addSequence_{0};

    std::atomic<State> state_{State::Idle};
    std::atomic<std::uint64_t> stateVersion_{0};
    std::atomic<std::uint64_t> patchGeneration_{0};
    std::atomic<std::uint64_t> mappingsRevision_{0};
    juce::WaitableEvent idleEvent_{true /*manual reset*/};
    juce::ChangeBroadcaster parameterEdits_;

    std::unique_ptr<Worker> worker_;
    std::unique_ptr<ResultUpdater> resultUpdater_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Session)
};

} // namespace pedalboard3::session
