#pragma once

#include "PreparedGraph.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace pedalboard3::engine
{
class AudioEngine;
class PluginHost;
} // namespace pedalboard3::engine

namespace pedalboard3::session
{

class PluginSlot;

// The patch-switch state machine specified by the master rewrite plan
// §4.1.1, scoped to Phase 1.D.2's single-plugin slot. Wraps the
// synchronous PluginSlot primitive in a worker-thread + latest-value
// register that lets a UI thread dispatch loads without blocking.
//
// States:
//   Idle      — no work in flight; mailbox empty.
//   Building  — worker thread is calling buildPreparedGraph; mailbox may
//               carry a newer request that supersedes this build.
//   Swapping  — build complete, message thread is committing the swap
//               into engine::AudioEngine. Bounded by ~one block of audio.
//
// Mailbox semantics are "latest-value register" / "overwriting single-slot
// queue": a new queueLoad while Building overwrites the slot; the in-flight
// build's result is dropped on commit and the worker is re-armed for the
// newer request. Same-plugin reload triggers a full rebuild.
//
// Lifetime invariants:
//   - Session::~Session cancels any pending AsyncUpdater callback BEFORE
//     joining the worker thread, BEFORE PluginSlot/AudioEngine teardown.
//   - The captured BuildFn must outlive the Session (the production
//     ctor's lambda captures PluginHost by reference; App owns both).
//
// Phase 2 will fold Session into the master plan §4.2 form (owning
// PatchRegistry, current-patch pointer, dirty flag) — today this is the
// state-machine slice only, sized for a single PluginSlot.
//
// Not RT-safe. Public API is message-thread only with the noted
// exceptions for the polling helpers (getState, getStateVersion,
// waitForIdle).
class Session : public juce::ChangeBroadcaster
{
public:
    enum class State
    {
        Idle,
        Building,
        Swapping
    };

    // Coherent snapshot of every Session-visible field — populated under
    // statusMutex_ so a polling harness sees no torn reads.
    struct Status
    {
        State state{State::Idle};
        juce::String loadedName;
        juce::String lastError;
        std::optional<juce::PluginDescription> activeDescription;
        std::uint64_t version{0};
    };

    // The seam Unit 3 calls out: the function the worker thread invokes
    // to build a PreparedGraph. Production wraps session::buildPreparedGraph
    // bound to a PluginHost; tests inject fast fakes.
    using BuildFn = std::function<PreparedGraph(
        const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& errorOut)>;

    // Production constructor — builds a BuildFn that calls
    // session::buildPreparedGraph against `host`.
    Session(PluginSlot& slot, engine::AudioEngine& engine, engine::PluginHost& host);

    // Test / integration-harness constructor. Public, not friend-only,
    // so out-of-process integration harnesses can use it.
    Session(PluginSlot& slot, engine::AudioEngine& engine, BuildFn buildFn);

    ~Session() override;

    // Latest-value register entry point. std::nullopt requests an unload.
    // Coalesces with any pending request — the second call wins. May be
    // called from any thread; signals the worker.
    void queueLoad(std::optional<juce::PluginDescription> desc);

    // Convenience overloads (msg-thread).
    void queueLoad(const juce::PluginDescription& desc);
    void queueUnload();

    // Lock-free read.
    State getState() const noexcept { return state_.load(std::memory_order_acquire); }

    // Monotonic counter; increments on every state transition. Polling
    // harnesses (CLI tests, future MCP wrapper) read this to detect
    // change without owning a juce::MessageManager.
    std::uint64_t getStateVersion() const noexcept { return stateVersion_.load(std::memory_order_acquire); }

    // Coherent snapshot under statusMutex_.
    Status getStatus() const;

    // Forwarders / convenience views.
    juce::String getLoadedName() const;
    juce::String getLastError() const;
    bool isLoaded() const noexcept;
    std::optional<juce::PluginDescription> getActiveDescription() const;

    // Block until state == Idle or timeout. Returns true on Idle, false on
    // timeout. Safe to call off the message thread (does not pump the
    // event loop). Used by integration harnesses.
    bool waitForIdle(int timeoutMs);

private:
    enum class RequestKind
    {
        Load,
        Unload
    };

    struct Request
    {
        RequestKind kind{RequestKind::Load};
        juce::PluginDescription desc;
    };

    struct BuildResult
    {
        std::optional<juce::PluginDescription> desc;
        PreparedGraph prepared;
        juce::String errorOut;
        bool isUnload{false};
    };

    class Worker;
    class ResultUpdater;

    // Worker thread → message thread hand-off via AsyncUpdater. Replaces
    // MessageManager::callAsync(λ-capturing-this) which has a residual
    // race with Session destruction (the pending lambda would fire on a
    // dead `this`). AsyncUpdater coalesces and supports cancelPendingUpdate.
    void onBuildResult(); // message thread

    // Atomic + change-message + idle-event publish.
    void publishStateChange(State newState);

    // If the mailbox holds a request and we are Idle, transition to
    // Building and wake the worker.
    void rearmWorkerIfMailboxNonEmpty();

    PluginSlot& slot_;
    engine::AudioEngine& engine_;
    BuildFn buildFn_;

    // Latest-value-register mailbox. Mutex-guarded — UI + worker threads
    // only. Audio thread never touches it.
    mutable std::mutex mailboxMutex_;
    std::optional<Request> mailbox_;

    // Worker → message-thread result slot.
    mutable std::mutex resultMutex_;
    std::optional<BuildResult> result_;

    // State.
    std::atomic<State> state_{State::Idle};
    std::atomic<std::uint64_t> stateVersion_{0};

    // Accompaniment for getStatus() — guarded together with state to
    // give callers a coherent snapshot.
    mutable std::mutex statusMutex_;
    juce::String lastError_;
    std::optional<juce::PluginDescription> activeDescription_;

    juce::WaitableEvent idleEvent_{true /*manual reset*/};

    std::unique_ptr<Worker> worker_;
    std::unique_ptr<ResultUpdater> resultUpdater_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Session)
};

} // namespace pedalboard3::session
