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
class Session : public juce::ChangeBroadcaster
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
    void addPlugin(juce::PluginDescription);

    // Append a built-in (internal) node to the active patch's chain, by its
    // typeId (e.g. "LevelProcessor"). Same worker pipeline as addPlugin — the
    // worker calls PluginHost::createInternalInstance instead of createInstance.
    void addInternalNode(juce::String internalType);

    // Synchronous: take effect before the call returns.
    void removeNode(NodeId);
    void moveNode(NodeId, int newIndex);
    void setNodeBypass(NodeId, bool);
    void setNodeCustomName(NodeId, juce::String);

    // ---- Free-routing edits (interactive canvas, message thread) -----------

    // Create a user-authored connection. Returns false (patch unchanged) if the
    // edge is invalid: cross-signal (audio<->MIDI), a duplicate, a self-loop, or
    // would make the graph fail validatePatch (cycle / dangling / bad channel).
    bool addConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel);
    void removeConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel);

    // Update a node's canvas position (no graph impact; persisted on save).
    void setNodePosition(NodeId, float x, float y);

    // ---- Snapshot accessors (message thread) -------------------------------

    Patch getActivePatch() const; // heavyweight copy

    // Like getActivePatch(), but also captures each live plugin's current
    // state blob (getStateInformation) into PatchNode.state — the snapshot
    // used when saving to a .pdl so a later load restores knob settings.
    // Message-thread only; calls getStateInformation under the status lock.
    Patch getActivePatchWithState() const;

    std::vector<NodeId> getNodeIdsInOrder() const; // cheap polling
    std::vector<NodeSnapshot> getNodeSnapshots() const;

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

    // Worker → message-thread result slot (variant covers both kinds).
    mutable std::mutex resultMutex_;
    Result result_;

    // Active patch + live graph observers. activePatch_ is the
    // canonical spec; nodes_ map keys must equal activePatch_.nodes ids
    // after every successful mutation.
    mutable std::mutex statusMutex_; // protects activePatch_, nodes_, lastError_
    Patch activePatch_;
    std::unordered_map<NodeId, PreparedGraph::NodeRefs> nodes_;
    juce::AudioProcessorGraph* liveGraph_{nullptr}; // non-owning
    juce::String lastError_;

    std::atomic<State> state_{State::Idle};
    std::atomic<std::uint64_t> stateVersion_{0};
    juce::WaitableEvent idleEvent_{true /*manual reset*/};

    std::unique_ptr<Worker> worker_;
    std::unique_ptr<ResultUpdater> resultUpdater_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Session)
};

} // namespace pedalboard3::session
