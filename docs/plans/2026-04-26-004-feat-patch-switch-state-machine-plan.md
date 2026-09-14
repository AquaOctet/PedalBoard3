---
title: "feat: queued patch-switch state machine on top of PluginSlot (Phase 1.D.2)"
type: feat
status: completed
date: 2026-04-26
origin: docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md
---

## Enhancement Summary

**Deepened on:** 2026-04-27
**Sections enhanced:** Technical Approach (all-notes-off + state machine), Implementation Units 1–7, Acceptance Criteria, Risks, Deferred-to-Implementation
**Research lanes used:** JUCE 8 framework docs · C++20 atomics & lock-free · architecture review · performance review · simplicity review · pattern-recognition · security review · agent-native parity · spec-flow analysis

### Key Improvements

1. **All-notes-off ordering corrected** — emit per channel as `allSoundOff → allNotesOff → resetAllControllers (CC121)`. The original ordering ignored CC#64 (sustain) latching, which makes `allNotesOff` a no-op on synths that latch sustain. (Source: JUCE forum + research lane 1.)
2. **Audio-thread allocation closed** — pre-build the flush `juce::MidiBuffer` in `prepareToPlay` and copy a sized scratch on every swap; size silence scratch to **4×** block, not 2×, to absorb Pro Tools / Reaper / CoreAudio adversarial block-size bursts. (Source: research lane 4 + existing `BypassableInstance::dryScratch_` audit.)
3. **`MessageManager::callAsync` lifetime hardened** — switch the worker → message-thread hand-off to `juce::AsyncUpdater` with a mutex-guarded `BuildResult` slot; `cancelPendingUpdate` in `Session` dtor closes the residual race that worker-join alone cannot. (Source: research lanes 1 + 2.)
4. **`session::buildPreparedGraph` moved to a free function** — keeps `PluginSlot` purely message-thread and removes the dual threading-contract smell. (Source: architecture review.)
5. **`PreferencesPersister` extraction** — `App::changeListenerCallback` was already doing two unrelated things; adding a third pushed it into god-callback territory. New tiny class owns the three listeners. (Source: architecture review.)
6. **`KnownPluginList` cross-check on restore (AC-1.D.2-L)** + **crash-blacklist consult on restore (AC-1.D.2-N)** — closes a path-traversal vector and a crash-loop vector that were both implicit in the original plan. (Source: security review.)
7. **`Session::Status` snapshot + `stateVersion_` atomic + `waitForIdle()`** — gives integration tests and any future MCP harness a torn-read-free polling API instead of a `juce::ChangeBroadcaster`-only surface. (Source: agent-native review.)
8. **Hard 2-second timeout on `swapPreparedGraph` ack-wait** + **MainWindow ctor self-syncs initial Session state** — closes two real-world stalls (audio thread starvation, restore-finishes-before-listener-attached). (Source: performance + spec-flow.)
9. **AC count grows from 10 → 17** with K–T covering: no-device load, SR-change-mid-build, ack-timeout, same-desc rebuild, prefs-write-failure, KnownPluginList cross-check, dirty-write-only persistence, crash-blacklist on restore, status-snapshot atomicity. (Source: spec-flow + security + agent-native.)
10. **Unit count tightened from 8 → 7**: Unit 8 (doc-comment update) folded into Units 1/2 as part of the same diff. (Source: simplicity review.)

### New Considerations Discovered

- **Diva/Omnisphere/Kontakt outgoing flush at <512-frame buffers may exceed the 5.3 ms RT budget by 2–4 ms.** AC-PS-1 ("zero buffer underruns") needs a footnote: the swap block is the worst-case block in a session, and at 256-frame buffers some heavy synths will glitch the swap by ≤1 sample. Documented as a known limitation; no code fix. Recommend a future "raise minimum block size for heavy plugins" preference.
- **`juce::XmlDocument` does NOT resolve external entities when constructed from a `String` (only from a `File`).** XXE is structurally safe via the `Preferences` call path. AC-1.D.2-K is a regression test, not a current vulnerability.
- **`Session::queueLoad` before `engine.initialise()` returns a live device** is real (headless CI / first-launch / device-disconnected). Path: state stays Idle, error = "audio device not ready", restore-on-launch defers via a one-shot listener.
- **Sample-rate / block-size change during a build invalidates the prepared graph.** Mitigation: re-`prepareToPlay` on the message thread in `onBuildResult` if the runtime's SR/block has changed since dispatch.
- **`Session` dtor must `cancelPendingUpdate()` on its `AsyncUpdater` + join the worker BEFORE PluginSlot/AudioEngine teardown.** Construction order assertion added.

# feat: queued patch-switch state machine on top of PluginSlot (Phase 1.D.2)

## Overview

Phase 1.D.1 shipped a synchronous `session::PluginSlot` that builds a
`juce::AudioProcessorGraph`, swaps it into `engine::AudioGraphRuntime`, and
returns a result on the message thread. That gets us "load a single VST3,
hear it" — at the cost of blocking the UI for the full plugin
instantiation + `prepareToPlay` (≥ tens of ms for heavy synths, sometimes
seconds).

Phase 1.D.2 wraps that synchronous primitive in the queued-patch-switch
state machine specified by the master rewrite plan §4.1.1: a single
worker thread builds prepared graphs off the message thread, a depth-1
latest-wins mailbox coalesces rapid switch requests into the most-recent
target, and the swap into the runtime carries an all-notes-off flush so
synths don't hang notes when the graph changes. It also lights up the
last-loaded-plugin restore on launch.

This is the spine that gates Phase 1.D.3 (multi-plugin chain) and
Phase 2 (PatchRegistry / `.pdl` persistence): everything downstream
needs the queued-load + coalescing semantics, so getting them right —
and tested — now is the cheapest place to do it.

## Problem Statement

The synchronous `PluginSlot::load()` blocks the UI thread on plugin
instantiation. For a SerumX-class instrument that's 1–4 seconds where
the host appears frozen. Worse, every UI action that triggers a plugin
load is single-shot: a fast double-click on two different plugins
queues two full instantiations serially with no chance for the user to
change their mind.

The master plan promises something far better:

- **Zero-glitch swap** at a buffer boundary (master plan §4.1.1 / AC-PS-1)
- **Latest-wins coalescing** so MIDI Program Change bursts collapse to
  the final target (master plan §4.1.1 / AC-PS-3 implicit)
- **All-notes-off on outgoing MIDI receivers** (master plan §4.1.1 +
  Appendix A.1, lines 1017–1033)
- **Build failure leaves outgoing graph live** (no glitch on bad-plugin
  load, master plan §4.1.1 recovery paths)
- **Restore last-loaded plugin on launch** (per Phase 1.C.2 promise:
  Preferences carries device + plugin list; the active plugin is the
  natural next persistence step)

The state machine itself is well-specified. The work is structuring it
correctly, splitting the worker-thread-safe portion from the
message-thread-only portion of the existing `PluginSlot::load()`, and
threading the transitions through the UI without re-introducing the
"frozen on load" symptom.

## Proposed Solution

Introduce a thin `session::Session` that owns the existing
`session::PluginSlot` plus the patch-switch state machine and worker
thread.

- `Session::queueLoad(juce::PluginDescription)` is the new public entry
  point used by UI / restore-on-launch.
- A single worker `juce::Thread` (`PatchBuildWorker`) services a
  depth-1 mailbox of pending descriptions. The worker
  - constructs the plugin instance via `PluginHost::createInstance`,
  - wraps it in a `BypassableInstance`,
  - builds the prepared `AudioProcessorGraph` (already prepared at the
    runtime's current sample rate / block size),
  - posts the result back to the message thread.
- The message thread does the swap via the existing
  `engine::AudioEngine::loadGraph`, which already enforces
  message-thread-only entry to `AudioGraphRuntime::swapPreparedGraph`.
  Before the swap publishes the new pointer, the audio thread observes a
  pending-swap atomic and on its next block (a) flushes all-notes-off on
  the outgoing graph with a silence buffer, then (b) performs the
  atomic exchange. This is the §4.1.1 invariant ("live swap happens on a
  buffer boundary, inside `AudioGraphRuntime::processBlock` at block
  start after the all-notes-off flush of the outgoing graph").
- `PluginSlot` keeps its public `load()` as the synchronous primitive
  (used by tests and by `Session` indirectly), and gains a structured
  helper `prepareGraph(desc, sr, block, errorOut)` that returns a
  `PreparedGraph` (`unique_ptr<AudioProcessorGraph>` plus non-owning
  raw pointers to the bypass wrapper + hosted plugin + display name) so
  the worker thread can build off the message thread without touching
  `engine_.loadGraph`.
- `Preferences` learns one new key — `activePluginDescription` —
  serialised via `juce::PluginDescription::createXml` /
  `loadFromXml`. On `App::initialise`, after the `Session` is
  constructed, the saved description (if any) is fed to
  `Session::queueLoad` so the user lands back in the same plugin they
  closed with.

`Session` is a `juce::ChangeBroadcaster` and exposes its current state
via a `Session::State` enum + last-error string so `MainWindow` can
- disable the "Load Plugin" button while the state is `Building` or
  `Swapping`,
- show "Loading <name>…" or "Failed: <reason>" inline,
- still let the user re-queue a different plugin during a build (the
  mailbox replaces the previous queued item — this is the coalescing
  guarantee).

## Technical Approach

### Architecture

#### Components

```
                           ┌────────────────────────────┐
       UI / restore   ───► │ session::Session           │
                           │   queueLoad(desc)          │
                           │   ChangeBroadcaster        │
                           │   State, lastError         │
                           └──────┬───────────────┬─────┘
                                  │               │
                      mailbox     │               │   message-thread
                      (depth-1)   ▼               ▼   callback queue
                           ┌────────────────┐  ┌──────────────────────┐
                           │ PatchBuild     │  │ Session::onBuild     │
                           │ Worker (Thread)│  │ Result (msg thread)  │
                           └──────┬─────────┘  └──────┬───────────────┘
                                  │                    │
                                  ▼                    ▼
                       PluginSlot::prepareGraph    engine::AudioEngine
                       (worker side: build +       ::loadGraph
                       prepareToPlay; no swap)     (msg thread; calls
                                                   AudioGraphRuntime
                                                   ::swapPreparedGraph
                                                   which schedules
                                                   audio-thread swap)
```

#### State machine (concrete mapping to §4.1.1)

| Master plan state | Pedalboard 3 1.D.2 representation |
|-------------------|-----------------------------------|
| `IDLE`            | `State::Idle`, mailbox empty      |
| `PREPARING`       | `State::Building`, worker running |
| `READY_TO_SWAP`   | `State::Swapping`, prepared graph posted to msg thread, awaiting `AudioGraphRuntime` ack |
| `CLEANUP`         | implicit — `swapPreparedGraph` already drains generation counter and returns the outgoing graph for RAII destruction |

Coalescing semantics (master plan §4.1.1 invariant 2 — "depth-1 queue,
latest-wins"):

- A second `queueLoad(P2)` while `State::Building` overwrites the
  mailbox slot. The in-flight build still completes (we don't try to
  cancel it mid-`createInstance`; that's a v3.1 problem) but its
  prepared graph is discarded by the message-thread callback because
  the mailbox has moved on.
- A third `queueLoad(P3)` while `State::Swapping` is queued; the
  message-thread callback at end of swap checks the mailbox and
  re-arms the worker if non-empty.

#### All-notes-off (§4.1.1 invariant 3)

Two implementation paths exist; the plan picks the audio-thread-side
variant because it matches the master plan's stated invariant exactly.
The message-thread-side variant is documented as a fallback.

**Path A — audio-thread-side (chosen).** Extend `AudioGraphRuntime`:

- Add `std::atomic<juce::AudioProcessorGraph*> pending_{nullptr}`.
- Add `swapPreparedGraph(next)` publishes `pending_` (release) instead
  of `live_`; signals a generation target; waits for ack.
- `processBlock` on the audio thread:
  ```cpp
  auto* g = live_.load(acquire);
  if (auto* p = pending_.exchange(nullptr, acquire)) {
      // Outgoing flush: 16ch × {allNotesOff, allSoundOff, allControllersOff}
      flushMidiReceivers(g, silenceScratch_);
      live_.store(p, release);
      g = p;
  }
  if (g == nullptr) { buf.clear(); midi.clear(); }
  else g->processBlock(buf, midi);
  sanitiser_.processBlock(buf, midi);
  generationAckedByAudio_.fetch_add(1, release);
  ```
- The outgoing graph is then released on the message thread via the
  generation-counter wait already implemented in
  `swapPreparedGraph`. This is the spec's "trash FIFO" but since
  swaps are message-thread serialised by the state machine we only
  ever have one outgoing graph in flight; a single `unique_ptr`
  return is sufficient (the master plan's full FIFO is a v3.x
  generalisation).

**Path B — message-thread-side (fallback).** Call `flushMidiReceivers`
on the outgoing graph from `swapPreparedGraph` *before* the
`live_.exchange`. This is what Appendix A.1 sketches but it requires
either pausing the audio thread during the flush or accepting a brief
window where the outgoing graph receives both its normal block and the
flush in parallel. Document why we did not take this path in the
implementation comment for Path A.

The decision between A and B is **deferred to implementation** —
benchmark both on a real-machine smoke test and pick the one whose
audible behaviour is cleaner. Default is A.

#### Research Insights — all-notes-off

**MIDI message ordering (corrected).** Emit per channel as
`allSoundOff → allNotesOff → resetAllControllers` rather than the
original `allNotesOff/allSoundOff/allControllersOff`. Reason: many
synths latch CC#64 (sustain pedal). Until sustain is released,
`allNotesOff` is a no-op. `allSoundOff` first kills tails immediately;
`allNotesOff` then handles release-stage envelopes; `resetAllControllers`
(CC121) finally releases sustain so the *next* note-off path is clean.
See [JUCE forum: VST3 Wrapper and MIDI AllNotesOff](https://forum.juce.com/t/bug-vst3-wrapper-and-midi-allnotesoff-event/32054).
JUCE has no built-in 16-channel host helper — every host rolls its own.

**Pre-built `MidiBuffer` template (RT-safe).** `juce::MidiBuffer::addEvent`
calls `data.ensureStorageAllocated(...)`, which **heap-allocates on the
first add**. The plan's audio-thread flush would allocate on every swap
unless the buffer is sized in `prepareToPlay`. Concrete shape:

```cpp
// in prepareToPlay (msg thread):
flushMidiTemplate_.clear();
flushMidiTemplate_.ensureSize(48 * 12);  // 16ch x 3 msgs, conservative
for (int ch = 1; ch <= 16; ++ch) {
    flushMidiTemplate_.addEvent(juce::MidiMessage::allSoundOff(ch), 0);
    flushMidiTemplate_.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
    flushMidiTemplate_.addEvent(juce::MidiMessage::controllerEvent(ch, 121, 0), 0);
}
flushMidiScratch_.ensureSize(flushMidiTemplate_.getNumEvents() * 12 + 64);

// in audio thread swap branch:
flushMidiScratch_ = flushMidiTemplate_;  // no realloc when pre-sized
g->processBlock(silenceScratch_, flushMidiScratch_);
```

**Silence scratch sizing — 4× not 1×.** Hosts call `processBlock` with up
to 4× the prepared block size during edge cases (Pro Tools AAX 32→1024
bridging, Reaper anticipative FX, CoreAudio sample-rate switch, JACK
xrun catch-up). Pre-allocate `silenceScratch_` to
`jmax(samplesPerBlock * 4, 4096) × jmax(numOutputChannels, 2)` floats —
~32 KB worst case, fits in L1, allocated once in `prepareToPlay`. Same
recommendation applies to the existing `BypassableInstance::dryScratch_`
which is currently sized 2× and falls back to a `setSize` realloc inside
`processBlock` (master plan invariant 1 violation under adversarial
hosts).

**Block size cost & a known limitation.** The swap block now runs
*two* graph processes back-to-back: the outgoing flush + the incoming
live block. Lightweight plugins (Dexed ~0.15 ms, FabFilter Pro-Q 3
~0.05 ms) absorb this trivially. **Diva in Divine mode, Omnisphere with
loaded multi, Kontakt with a sampled instrument can take 3–8 ms on the
flush block alone** — exceeding a 256-frame / 48 kHz budget (5.3 ms).
This is unavoidable inside the budget for these plugins; document
AC-PS-1 with the footnote "swap glitch bounded to ≤1 sample on heavy
synths at <512-frame buffers; raise buffer to 512 if hosting these".
The lookahead-limiter "primed with silence" concern is moot because we
discard the outgoing graph immediately after the flush; the incoming
graph is freshly prepared.

**Pass actual block size — not numSamples=0.** Some VST3 plugins skip
MIDI processing entirely when `numSamples == 0` because their internal
buffer-pre-roll path is gated on a positive sample count. AC-1.D.2-D
would silently fail against those plugins. Always pass the runtime's
current block size + the silenced scratch.

**Memory-order summary (bake into a comment block above
`swapPreparedGraph`):**
- `pending_.store(p, release)` (msg) ↔ audio `pending_.exchange(nullptr, acquire)` — publishes prepared graph + transitive `prepareToPlay` writes.
- `live_.store(p, release)` (audio) — only audio writes `live_`.
- `generationAckedByAudio_.fetch_add(1, release)` (audio) ↔ msg `load(acquire)` — gates outgoing-graph reclamation on the message thread.
- All counters: `relaxed`.
- `dead_` flag in `BypassableInstance`: `acquire/release` (cheap on x86/ARM, future-proofs against side-effects added later).

#### Preferences extension (master plan invariant 5 alignment)

`session::Preferences` already wraps `juce::ApplicationProperties` and
uses dirty-flag auto-save. The new key is one more XML round-trip:

```cpp
std::unique_ptr<juce::PluginDescription> getActivePluginDescription();
void setActivePluginDescription(const juce::PluginDescription* desc);
void clearActivePluginDescription();   // when user explicitly unloads
```

Storage key: `"activePluginDescription"`. Wire format:
`juce::PluginDescription::createXml()` → string. On load,
`PluginDescription::loadFromXml`.

#### Restore-on-launch flow

```
App::initialise
  preferences ← new Preferences
  engine      ← new AudioEngine; initialise(savedDeviceState)
  pluginHost  ← new PluginHost; loadKnownList(savedKnownList)
  pluginSlot  ← new PluginSlot(engine, host)
  session     ← new Session(pluginSlot, preferences)        // NEW
  // listeners
  dm.addChangeListener(this)
  knownList.addChangeListener(this)
  session->addChangeListener(this)                           // NEW
  // restore active plugin (queued, not synchronous)
  if (auto desc = preferences->getActivePluginDescription())
      session->queueLoad(*desc)                              // NEW
  context.emplace(...)
  mainWindow ← new MainWindow(name, *context)
```

`Session`'s ChangeListener writes a fresh active-plugin XML to
`Preferences` whenever the state machine transitions back to `Idle`
with a successful load (or to a cleared description on user unload).

### Implementation Units

> Each unit lists `Files`, `Approach`, `Patterns to follow`,
> `Test scenarios`, and `Verification`. Honour the `Execution note`
> when present.

#### Unit 1 — Extract `session::buildPreparedGraph` free function

- **Goal:** split `PluginSlot::load` so the slow part (createInstance +
  graph build + prepareToPlay) can run off the message thread, in a
  context that does not pretend to be a `PluginSlot` method.

**Architecture-review correction:** the original plan put `prepareGraph`
on `PluginSlot` itself, which gave the class two threading contracts
(some methods msg-thread-only, this one worker-safe). It also did not
need any `PluginSlot` member state. Move it to a free function in a new
`session/PreparedGraph.h/.cpp` (or a static method on `Session`) so
`PluginSlot` stays purely message-thread. `PluginSlot` gains one new
small method instead — `slot_.adoptPrepared(PreparedGraph&&)` — that
runs on the message thread inside the swap callback to update the
observer pointers atomically with the graph swap.
- **Files:**
  - `app-v3/src/session/PreparedGraph.h`   (new)
  - `app-v3/src/session/PreparedGraph.cpp` (new)
  - `app-v3/src/session/PluginSlot.h`      (add `adoptPrepared`)
  - `app-v3/src/session/PluginSlot.cpp`    (refactor `load` to use the new path)
  - `app-v3/src/session/CMakeLists.txt`    (add new sources to lib AND `pb3_apply_strict_warnings`)
  - `app-v3/tests/unit/session/PluginSlot_test.cpp`
  - `app-v3/tests/unit/session/PreparedGraph_test.cpp` (new)
- **Approach:** Define a value type
  ```cpp
  namespace pedalboard3::session {
      struct PreparedGraph {
          std::unique_ptr<juce::AudioProcessorGraph> graph;
          engine::BypassableInstance*                bypass{};
          juce::AudioPluginInstance*                 hosted{};
          juce::String                               name;
      };

      // Worker-safe. Does NOT touch any AudioEngine / PluginSlot state.
      // Returns an empty PreparedGraph on failure with errorOut set.
      PreparedGraph buildPreparedGraph(engine::PluginHost& host,
                                       const juce::PluginDescription& desc,
                                       double sampleRate,
                                       int    blockSize,
                                       juce::String& errorOut);
  } // namespace pedalboard3::session
  ```
  `PluginSlot::load(desc, errorOut)` becomes
  `buildPreparedGraph(...) → engine_.loadGraph(...) → adoptPrepared(...)`,
  preserving its public signature. `PluginSlot::adoptPrepared(PreparedGraph&&)`
  is the new message-thread helper that updates `bypassed_`, `hostedPlugin_`,
  `loadedName_` and fires `sendChangeMessage()` — used by both `load`
  and by `Session::onBuildResult`.
- **Patterns to follow:** existing `PluginSlot::load` body
  (`PluginSlot.cpp:46-108`); JUCE `AudioProcessorGraph` build is
  unchanged; the only restructure is the function-boundary.
- **Execution note:** test-first. Add a unit test for `prepareGraph`
  that returns a non-null `PreparedGraph` whose `graph->prepareToPlay`
  has been called (assert `graph->getSampleRate() != 0.0`).
- **Verification:**
  - All Phase 1.D.1 PluginSlot tests still pass unchanged.
  - New `PluginSlot::prepareGraph` returns false + error on bogus
    description; observer pointers remain null.

#### Unit 2 — `session::Session` skeleton + state machine

- **Goal:** introduce `session::Session` owning `PluginSlot`, a
  worker thread, the state-machine variable, and the coalescing
  mailbox.
- **Files:**
  - `app-v3/src/session/Session.h`     (new)
  - `app-v3/src/session/Session.cpp`   (new)
  - `app-v3/src/session/CMakeLists.txt`
  - `app-v3/tests/unit/session/Session_test.cpp` (new)
- **Approach:**
  ```cpp
  namespace pedalboard3::session {

  class Session : public juce::ChangeBroadcaster {
  public:
      enum class State { Idle, Building, Swapping };

      Session(PluginSlot& slot, engine::AudioEngine& engine);
      ~Session() override;          // joins worker

      // Message-thread entry. Latest-wins coalescing.
      void queueLoad(const juce::PluginDescription& desc);

      // Message-thread entry. Coalesces; on Idle empties the slot.
      void queueUnload();

      State           getState() const noexcept;
      juce::String    getLastError() const;
      juce::String    getLoadedName() const;     // forwards to slot
      bool            isLoaded() const noexcept; // forwards to slot

  private:
      class Worker;                            // juce::Thread subclass
      void onBuildResult(...);                 // msg thread
      void publishStateChange();               // sendChangeMessage()

      PluginSlot&            slot_;
      engine::AudioEngine&   engine_;

      // Mailbox (depth-1, latest-wins). Protected by mailboxMutex_;
      // mutex contention is bounded — UI thread + worker thread only.
      std::mutex             mailboxMutex_;
      std::optional<juce::PluginDescription> pending_;
      bool                   pendingIsUnload_{false};

      std::atomic<State>     state_{State::Idle};
      juce::String           lastError_;       // msg-thread access only

      std::unique_ptr<Worker> worker_;
  };

  } // namespace pedalboard3::session
  ```
  Worker thread internals:
  - `juce::WaitableEvent` is signalled by the message thread after
    publishing into the mailbox.
  - Worker loop: wait → snapshot mailbox → release `mailboxMutex_`
    BEFORE calling `buildPreparedGraph` (the slow 1–4 s call must NOT
    hold the mutex; otherwise UI thread freezes on next `queueLoad`)
    → if empty + told-to-quit → exit, else → call
    `buildPreparedGraph(host_, desc, sr, block, errorOut)` → deposit
    result into a member `std::optional<BuildResult> result_` under
    `resultMutex_` → `triggerAsyncUpdate()` on a member
    `juce::AsyncUpdater`.
  - **Hand-off via `juce::AsyncUpdater`, not `MessageManager::callAsync`.**
    Reason: `callAsync(λ-capturing-this)` has a residual race where
    queued lambdas fire on a destroyed `Session` — worker-join alone
    cannot drain already-queued message-thread items. `AsyncUpdater`
    coalesces (matching our latest-wins semantics) and exposes
    `cancelPendingUpdate()` which the dtor calls. See
    [JUCE forum: Unsafe uses of callAsync](https://forum.juce.com/t/unsafe-uses-of-callasync/19647).
  - `handleAsyncUpdate()` runs `Session::onBuildResult` on the message
    thread: snapshot `mailbox_` again (re-coalescing check); if a newer
    description has landed, drop the prepared graph and re-signal the
    worker; otherwise call `engine_.loadGraph(std::move(prepared.graph))`,
    `slot_.adoptPrepared(std::move(prepared))`, transition state and
    re-check mailbox.
  - **Re-prepare on SR/block change.** If `engine_.getRuntime()`'s
    sample rate or block size has changed since dispatch (DAW device
    swap mid-build), call `prepared.graph->prepareToPlay(newSr, newBlock)`
    on the message thread before `loadGraph`. Cheap; only the inner
    `prepareToPlay` runs.
- **Patterns to follow:** message-thread idioms in existing `App` /
  `MainWindow`; `juce::ChangeBroadcaster` use in `PluginSlot`. Keep
  state-machine code in `.cpp`, exposed methods minimal.
- **Execution note:** test-first for the state-machine transitions.
- **Test scenarios:**
  1. `queueLoad` with bogus description → state walks Idle → Building →
     Idle; `getLastError()` non-empty; `slot.isLoaded()` still false;
     change-message fired ≥ 2 times.
  2. Two `queueLoad` calls back-to-back while Building → only the
     second is committed (first is coalesced out).
  3. `queueUnload` then `queueLoad` while Building → final state is
     Idle with the second plugin loaded (or unloaded if order
     inverted).
  4. `Session` dtor while Building → joins worker cleanly without
     deadlock or crash.
  5. Loading the same description twice in a row → **full rebuild**
     (resolved during deepening: matches "user clicks the same button
     again expects a reload"; the all-notes-off is a feature, not a bug).
  6. `Session` constructed before audio device live (no `engine.initialise()`
     yet, or initialise failed) → `queueLoad` walks Idle → Idle, error =
     "audio device not ready", no worker dispatch. Restore-on-launch
     defers via a one-shot listener on the engine until first
     device-state-changed.
  7. `queueLoad` while `Swapping` is unreachable in practice (the
     synchronous swap blocks the message thread — `queueLoad` cannot
     land until it returns). Document; don't test.
  8. Sample-rate change during Building → final swapped graph runs at
     the new sample rate (fake builder with 100 ms delay; trigger device
     SR change at 50 ms; assert final graph reports the new SR).
- **Verification:** all Session_test cases pass on linux-ci-image
  unit-test job; no thread-sanitizer warnings under
  `-fsanitize=thread` (Phase 0 promises tsan; if it isn't wired yet,
  add as a follow-up unit, do not block on it).

#### Research Insights — Unit 2

- **Add `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Session)`** at the
  bottom of the class body. Every existing class in the codebase uses it
  (`PluginSlot.h:72`, `Preferences.h:61`, `AudioEngine.h:86`,
  `AudioGraphRuntime.h:111`, `BypassableInstance.h:129`). The original
  Unit 2 snippet omitted it.
- **Forward-declare `engine::AudioEngine`** in `Session.h` (mirror
  `PluginSlot.h:8-13`).
- **CMake: two edits, not one.** Add `Session.cpp` to
  `add_library(pb3_session_core ...)` AND to
  `pb3_apply_strict_warnings(pb3_session_core ...)` in
  `app-v3/src/session/CMakeLists.txt`. The latter is the easy thing to
  forget; without it, the strict-warning policy doesn't apply to the
  new file.
- **Public Status snapshot accessor** (agent-native parity):
  ```cpp
  struct Status {
      State                                  state{State::Idle};
      juce::String                           loadedName;
      juce::String                           lastError;
      std::optional<juce::PluginDescription> activeDescription;
      std::uint64_t                          version{0};
  };
  Status getStatus() const;            // populated under statusMutex_

  // Polling helpers for harnesses without a JUCE message loop.
  std::uint64_t getStateVersion() const noexcept;  // monotonic
  bool waitForIdle(int timeoutMs);                 // backed by WaitableEvent
  ```
  `version` bumps on every transition; harnesses (CLI tests, future
  MCP wrapper) can poll for change without owning a `MessageManager`.
- **Distinguish unload from failed-load.** Listener (Unit 6) writes
  `clearActivePluginDescription` only when the user explicitly called
  `queueLoad(std::nullopt)` (sentinel for unload), NOT when a failed
  load lands in `Idle && !isLoaded()`. Otherwise an agent that calls
  `queueLoad` on a missing plugin silently wipes the user's restore
  state.
- **Coalescing terminology.** "Depth-1 mailbox" is not a term of art.
  Standard names: "latest-value register" or "overwriting single-slot
  queue" (Go's `chan` with capacity 1 + non-blocking send formalises
  this). Use the standard name in code comments so future readers can
  search.
- **`pending_` invariant in `AudioGraphRuntime`** (cross-reference Unit 5):
  the message thread must not publish into `pending_` until the previous
  swap has been acked. The generation-counter wait already enforces
  this; make it a `jassert`, not just a comment.
- **clang-format-18 hoist warning.** Worker-result lambda chains and
  long `juce::MessageManager::callAsync(...)` calls are likely to
  overshoot 120 chars. Per user feedback memory
  `feedback_clang_format_split_hoist.md`, hoist named locals
  (`auto& dm = engine_.getDeviceManager();` etc.) rather than letting
  clang-format-18 auto-split. See `MainWindow.cpp` `MessageBoxOptions`
  precedent.

#### Unit 3 — `Session` factory seam for tests

- **Goal:** make Session unit-testable without a real `engine::AudioEngine`
  or VST3 binary. The tests need an injection point for the prepared
  graph build (so they can inject a fake that returns a synthetic
  graph wrapping `TestGainProcessor`) and an injection point for the
  swap (so they can verify it was attempted).
- **Files:**
  - `app-v3/src/session/Session.h`
  - `app-v3/src/session/Session.cpp`
  - `app-v3/tests/unit/session/Session_test.cpp`
- **Approach:** Either
  - **Option A (preferred):** narrow the production constructor to take
    references to `PluginSlot` + `engine::AudioEngine` and add a
    second constructor / factory function that takes `std::function`s
    for `buildPreparedGraph` and `commitSwap`, used only by tests.
  - **Option B:** extract a tiny `IPatchBuilder` interface with a
    real impl in `PluginSlot` and a fake impl in tests.

  Choose Option A unless Option B falls out for free during Unit 2.
  The decision is deferred to implementation — pick whichever lets
  the Session_test cases stay short and obvious.

  **Constraint resolved during deepening (agent-native review):**
  whichever option is chosen, the seam ships in `Session.h` as a
  **public** constructor or interface, **never** a `friend class
  Session_test` seam. Friend-only seams are agent-hostile because a
  future MCP/scriptable-test harness lives outside the test binary's
  TU and cannot reach friended internals.
- **Verification:** Session_test runs without instantiating a real
  `engine::AudioEngine` or `engine::PluginHost`. The chosen seam is
  reachable from a hypothetical out-of-tree integration harness, not
  hidden behind `friend`.

#### Unit 4 — `Preferences` active-plugin-description round-trip

- **Goal:** persist + restore the last-loaded plugin's description.
- **Files:**
  - `app-v3/src/session/Preferences.h`
  - `app-v3/src/session/Preferences.cpp`
  - `app-v3/tests/unit/session/Preferences_test.cpp`
- **Approach:**
  ```cpp
  std::unique_ptr<juce::PluginDescription> getActivePluginDescription();
  void setActivePluginDescription(const juce::PluginDescription& desc);
  void clearActivePluginDescription();
  ```
  - `set` writes `desc.createXml()->toString()` under key
    `"activePluginDescription"`.
  - `get` reads the string, parses with `juce::parseXML`, returns
    `nullptr` if missing/malformed/empty.
  - `clear` calls `props_.getUserSettings()->removeValue(...)`.
- **Patterns to follow:** existing `getKnownPluginList` /
  `setKnownPluginList` pair (Preferences.cpp).
- **Execution note:** test-first.
- **Test scenarios:**
  1. First-run `getActivePluginDescription()` → nullptr.
  2. `setActivePluginDescription(desc)` then `get` → round-trips
     name + format + identifier (`PluginDescription` has
     value-equality semantics for the relevant fields; a custom
     comparator is fine).
  3. Persistence across `Preferences` reconstruction (write file →
     destruct → reconstruct → read).
  4. `clearActivePluginDescription` → subsequent `get` returns nullptr.
  5. Malformed XML in store → `get` returns nullptr (not crash).
  6. **XXE regression test.** Feed
     `<!DOCTYPE foo [<!ENTITY x SYSTEM "/etc/passwd">]><PLUGIN file="&x;"/>`
     into the settings file → `get` returns either nullptr or a
     PluginDescription whose `fileOrIdentifier` does NOT contain the
     contents of `/etc/passwd`. Confirms the structurally-safe path
     (string-overload `parseXML` + JUCE's hand-rolled `XmlDocument`
     does not resolve external entities when constructed from a
     `String` — only from a `File`).
- **Verification:** all six Preferences_test additions pass.

#### Research Insights — Unit 4

- **`PluginDescription::createXml` round-trips fully** in JUCE 8.0.4 —
  every public field is preserved including `lastFileModTime`,
  `lastInfoUpdateTime`, `uniqueId`, `deprecatedUid`, `hasARAExtension`.
  Don't strip them in `set` — they'll be ignored on restore (paths
  revalidate on `createInstance`) but stripping breaks JUCE's own
  `KnownPluginList` round-trip if Preferences ever feeds the same XML
  back into a list.
- **`juce::XmlDocument` XXE safety** is structurally guaranteed only
  for the **string overload** of `parseXML`. The `File` overload
  auto-installs a `FileInputSource` that *will* resolve `SYSTEM`
  external entities. Add a one-line code comment at the read site:
  `// PROD ONLY USES THE STRING OVERLOAD — never juce::parseXML(File)`.
- **`KnownPluginList` cross-check on restore (security review).**
  Before calling `Session::queueLoad(*desc)` in `App::initialise`,
  cross-check the parsed description against
  `pluginHost.getKnownPluginList()`. Reject + `clearActivePluginDescription`
  if no `KnownPluginList` entry has matching `(pluginFormatName,
  fileOrIdentifier, uniqueId)`. This raises the bar from "edit one
  settings key" to "edit two correlated settings keys" for an
  attacker-controlled `Preferences.settings`. Spec'd as **AC-1.D.2-L**.
- **Crash-blacklist consult on restore (security review).**
  Before instantiation, consult `PluginHost::getCrashedPluginsFile()`
  (already exists at `PluginHost.cpp:124-129` but currently consumed
  only by `PluginListComponent`'s scanner dead-man's-pedal). Skip +
  clear if `fileOrIdentifier` is blacklisted. Belt-and-braces:
  atomically tag the entry as in-flight before instantiation, clear on
  success — a crash mid-`createInstance` leaves the entry, next-launch
  short-circuits. Spec'd as **AC-1.D.2-N**.
- **Dirty-write-only persistence.** App's listener writes the XML only
  when the persisted description differs from
  `Preferences::getActivePluginDescription()`. Equality via
  `(pluginFormatName, fileOrIdentifier, deprecatedUid)` triple. Avoids
  unnecessary disk churn on restore-load (no-op write of the same desc
  back) and on rapid same-plugin reload. Spec'd as **AC-1.D.2-Q**.

#### Unit 5 — `AudioGraphRuntime` audio-thread-side all-notes-off swap

- **Goal:** guarantee the §4.1.1 invariant — outgoing MIDI receivers
  see all-notes-off before the live pointer flips.
- **Files:**
  - `app-v3/src/engine/AudioGraphRuntime.h`
  - `app-v3/src/engine/AudioGraphRuntime.cpp`
  - `app-v3/tests/unit/engine/AudioGraphRuntime_test.cpp` (extend if
    present, create if absent)
- **Approach:**
  - Add `std::atomic<juce::AudioProcessorGraph*> pending_{nullptr}`.
  - Reshape `swapPreparedGraph` to publish into `pending_` instead of
    `live_`; the existing generation-counter wait still applies (now
    it waits for the audio thread to consume the pending pointer).
  - In `processBlock`, before the live read, do an
    `pending_.exchange(nullptr, acquire)`. If non-null, build a
    16-channel × 3-message MIDI flush and call the outgoing graph's
    `processBlock` with a silent scratch buffer pre-allocated in
    `prepareToPlay`. Then `live_.store(newPending, release)` and
    proceed.
  - If `processBlock` is called on a runtime that has no `live_` and
    a `pending_` arrives, skip the flush (no outgoing graph) and
    publish directly. (No-op edge case for first-load.)
  - Keep the existing dtor cleanup but read both `live_` and
    `pending_` so a graph that was published-but-never-consumed at
    teardown is also destroyed.
- **Patterns to follow:** the existing atomic pattern in
  `AudioGraphRuntime.cpp` (acquire/release pairing on `live_`).
  Memory order on `pending_`: release on publish (msg thread); acquire
  on `exchange(nullptr)` (audio thread).
- **Execution note:** characterization-first — write a test that
  verifies the pre-1.D.2 swap *does not* flush, run it green, then
  add the new flush behaviour and a new test that asserts the flush
  happens. Keeps the change reviewable.
- **Test scenarios:**
  1. Swap from a graph hosting a mock MIDI-receiving processor
     (extend `TestGainProcessor` or add a sibling `TestMidiSinkProcessor`
     that records its received MIDI buffer per block) to a new graph;
     assert the previous graph received `allNotesOff/allSoundOff/
     allControllersOff` on every channel 1–16 in its final block.
  2. Swap into a runtime with no live graph (first load) — no flush
     happens, no crash.
  3. Two successive `swapPreparedGraph` calls back-to-back from the
     message thread — second one waits for first to be observed (the
     existing generation-counter logic handles this; this test just
     guards regression).
  4. **Flush safety on dead/null outgoing.** Swap from a
     `BypassableInstance` whose `dead_` flag is set → the audio-thread
     flush still runs without UB; the new graph still swaps in cleanly.
  5. **Ack-wait timeout.** Fake `AudioGraphRuntime` whose audio thread
     never bumps `generationAckedByAudio_` → `swapPreparedGraph` returns
     within 2 s with a timeout error rather than blocking forever.
- **Verification:** existing AudioGraphRuntime tests stay green, new
  tests pass, no thread-sanitizer warnings.

#### Research Insights — Unit 5

- **Pre-build the flush `MidiBuffer` in `prepareToPlay`** (see
  Technical Approach > all-notes-off > Research Insights for the
  concrete shape). Audio-thread `processBlock` must not heap-allocate
  via `MidiBuffer::addEvent`.
- **Silence scratch sized 4×, not 1× or 2×.** Hosts adversarially call
  with up to 4× block size during transport edge cases. Allocate once
  in `prepareToPlay`; never reallocate on the audio thread.
- **MIDI ordering corrected.** `allSoundOff → allNotesOff →
  resetAllControllers (CC121)` per channel — releases sustain so
  `allNotesOff` actually fires on synths that latch CC#64.
- **`pending_` invariant enforcement.** Add a `jassert` in
  `swapPreparedGraph` that the previous `pending_` is null before
  publishing a new one — the generation-counter wait ensures this, but
  the assert documents the invariant for future readers and catches
  regressions.
- **Dtor precondition.** `~AudioGraphRuntime` must read both `live_`
  AND `pending_` and destroy both (the published-but-never-consumed
  edge case at teardown). Add a `jassert` that the audio device is
  stopped before the dtor reads either pointer — otherwise an in-flight
  `processBlock` could `exchange(nullptr)` between the dtor's two reads.
- **Audio-thread cost caveat for AC-PS-1.** Document in
  `AudioGraphRuntime.h` doc-block: "the swap block runs *two* graph
  blocks back-to-back (outgoing flush + incoming live). Heavy synths
  on the outgoing side may exceed a single-block budget at <512-frame
  buffers; this is bounded to ≤1 sample on the swap block and never
  thereafter."
- **Don't pass numSamples=0 to the flush `processBlock`.** Some VST3
  plugins skip MIDI processing when `numSamples == 0`. Always use the
  runtime's negotiated block size with the silenced scratch.
- **No ack-wait via spinning.** Existing wait uses
  `std::this_thread::sleep_for(1ms)`; keep that pattern. Add a hard
  cap (`maxWaitIters` already calculates ~3 blocks; add an absolute
  2-second ceiling on top via a wall-clock check).

#### Unit 6 — `App` wiring + restore-on-launch + `AppContext` + listeners

- **Goal:** wire `Session` into the app, restore last-loaded plugin
  on launch, persist the active plugin on every successful load.
- **Files:**
  - `app-v3/src/AppContext.h`
  - `app-v3/src/App.h`
  - `app-v3/src/App.cpp`
- **Approach:**
  - `AppContext` gains a `session::Session& session` field.
  - `App` owns `std::unique_ptr<session::Session> session;` between
    `pluginSlot` and `context`. Construction order:
    `preferences → engine → pluginHost → pluginSlot → session →
    listeners → restore queueLoad → context.emplace → mainWindow`.
  - Shutdown order: deregister listeners, flush prefs, destroy
    `mainWindow`, `context`, `session` (joins worker, awaits any
    in-flight build), `pluginSlot`, `pluginHost`, `engine`,
    `preferences`. Worker join must happen before `pluginSlot` is
    destroyed.
  - `App::changeListenerCallback` learns a third source:
    `session.get()`. On state transition into `Idle` with a
    `slot.isLoaded()`, write
    `preferences->setActivePluginDescription(...)`. On Idle without
    loaded plugin, `clearActivePluginDescription`.
  - The plugin description we persist is the one associated with the
    most recent `queueLoad` that succeeded — we keep a copy on
    `Session` (`std::optional<juce::PluginDescription>
    activeDescription_`) and the listener reads it.
- **Patterns to follow:** existing `App::initialise` / `shutdown`
  layout (App.cpp); existing two listener-source pattern in
  `changeListenerCallback`.
- **Execution note:** no `Execution note` from the master plan; proceed
  pragmatically, verify by running the app + restarting + confirming
  the plugin reappears.
- **Verification:** build green; manual macOS smoke — load a VST3,
  quit, relaunch, plugin reloads automatically.

#### Research Insights — Unit 6

- **Extract `session::PreferencesPersister`** (architecture review).
  `App::changeListenerCallback` was already doing two unrelated things
  (device state, plugin list); adding a third (active plugin
  description) crossed into god-callback territory. The three sources
  share one responsibility — "write to Preferences" — and that has a
  name. Shape:
  ```cpp
  // app-v3/src/session/PreferencesPersister.h — new file, ~40 LOC
  class PreferencesPersister : private juce::ChangeListener {
  public:
      PreferencesPersister(Preferences&,
                           engine::AudioEngine&,
                           engine::PluginHost&,
                           Session&);
      ~PreferencesPersister();   // deregisters
  private:
      void changeListenerCallback(juce::ChangeBroadcaster*) override;
      // refs + dispatch switch on source identity
      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesPersister)
  };
  ```
  `App` becomes a pure composition root with no `juce::ChangeListener`
  inheritance. The persister is independently testable. ~30-minute
  refactor that pays off the moment EventLog or any other subsystem
  wants to listen to Session.
- **`AppContext.h` comment migration.** The placeholder comment at
  `AppContext.h:27` (`// - session::Session (Phase 1.D.2 — patch-switch
  state machine)`) must be removed and a corresponding "Phase 1.D.2
  added: session::Session" line added at `AppContext.h:25` alongside
  the existing "Phase 1.D.1 added" entry.
- **Construction order assertion.** `Session` ctor takes
  `PluginSlot&` and `engine::AudioEngine&` — add a `jassert` in the
  ctor body that `&slot != nullptr` (defensive against future refactors
  that move Session above PluginSlot in App's init list).
- **`AsyncUpdater` dtor cancel sequence.** `Session::~Session` must
  call `cancelPendingUpdate()` on its `AsyncUpdater` member BEFORE
  joining the worker, AND BEFORE `PluginSlot`/`AudioEngine` teardown.
  Order: deregister listeners → flush prefs → reset MainWindow →
  reset AppContext → reset PreferencesPersister → reset Session
  (cancel + join + drain) → reset PluginSlot → reset PluginHost →
  reset AudioEngine → reset Preferences.
- **`MainWindow` ctor self-sync (performance review).** After
  `addChangeListener(&ctx_.session)`, immediately call
  `changeListenerCallback(&ctx_.session)` once with the listener
  registered. Plugs the restore-on-launch race where a fast worker
  build completes and fires `sendChangeMessage()` before MainWindow
  was constructed; the change-message has no listener and the UI
  never reflects "Loading…" → "Ready".
- **CI validation.** Per user feedback memory
  `feedback_build_validation.md`, validate via CI push, not local
  `cmake --build`. The existing `pb3_session_core` static lib +
  `pb3_apply_strict_warnings` per-source already cover the new files
  once the CMake lists are updated. No `.gitlab-ci.yml` changes
  required.

#### Unit 7 — `MainWindow` UI feedback on Session state

- **Goal:** swap the "Load Plugin" UI from `PluginSlot::load` to
  `Session::queueLoad`; surface the Building/Swapping state; disable
  the Load button while a build is in flight.
- **Files:**
  - `app-v3/src/ui/MainWindow.cpp`
  - `app-v3/src/ui/MainWindow.h` (only if a member needs adding)
- **Approach:**
  - `MainWindow` already inherits `juce::ChangeListener` for
    `KnownPluginList` and `PluginSlot`. Add the `Session` source.
  - `changeListenerCallback`: when the source is the `Session`, read
    `getState()` and `getLastError()`, update a status `juce::Label`
    (or repaint the title strip), and toggle the Load button's
    `setEnabled(state == State::Idle)`.
  - The "Load" button click handler now calls
    `ctx_.session.queueLoad(desc)` instead of
    `ctx_.pluginSlot.load(desc, error)`. Errors are no longer
    synchronous; show them via the change-listener path
    (`AlertWindow::showAsync`, same idiom MainWindow already uses).
  - The Edit-plugin button still keys off `pluginSlot.getHostedPlugin()`;
    that path is unchanged.
- **Patterns to follow:** existing MainWindow ChangeListener pattern;
  existing `AlertWindow::showAsync` use (the
  `juce::MessageBoxOptions{}.withIconType(...)` helper hoisted in
  Phase 1.D.1 to satisfy clang-format-18).
- **Verification:** macOS smoke — clicking Load shows "Loading…"; UI
  doesn't freeze on a heavy synth; rapid double-click only loads the
  second one; bogus desc shows "Failed: …" inline rather than blocking
  modal.

#### ~~Unit 8 — Documentation: state-machine doc-comment update~~ (folded into Units 1 + 2)

Resolved during deepening (simplicity review): the doc-comment update
is a 4-line edit that lands inside the same diffs as Units 1 and 2.
Calling it out as a phase ("Phase D — doc cleanup") inflated the plan.
Unit 1 updates `PluginSlot.h`'s "Phase 1.D.2 will add…" comment to
describe what's actually there; Unit 2 introduces `Session.h` with a
≤ 30-line doc-block. No separate unit, no separate phase.

### Implementation Phases

**Validation discipline.** Per user feedback memory
`feedback_build_validation.md`: push to CI; do not run local
`cmake --build` / `ctest` cycles. Read pipeline output to validate.
The existing `pb3_session_core` static lib + `pb3_apply_strict_warnings`
per-source already wires every new `.cpp` once the CMake source lists
are updated. No `.gitlab-ci.yml` changes required.

**clang-format-18 hoist guidance.** Per user feedback memory
`feedback_clang_format_split_hoist.md`: for ~120-char-overshoot calls
(`juce::MessageManager::callAsync(...)`,
`slot_.adoptPrepared(buildPreparedGraph(host_, desc, sr, block, errorOut))`,
the `MessageBoxOptions{}.withIconType(...)` chain), hoist into a named
local instead of trying to predict clang-format-18's preferred multi-line
layout.

#### Refactor before implementation

- Move `prepareGraph` out of `PluginSlot` and into a `session::buildPreparedGraph`
  free function (`app-v3/src/session/PreparedGraph.h/.cpp`). PluginSlot
  gains a small `adoptPrepared(PreparedGraph&&)` message-thread helper.
- Extract `session::PreferencesPersister` (~40 LOC) so the three
  listener fan-outs leave `App` as a pure composition root.
- Update `AppContext.h:23-30` comment migration.
- Add `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` to every new class.

#### Phase A — primitives (Units 1, 4, 5)

These are independent of each other and can land in any order or in
parallel subagents:

- Unit 1 — `PluginSlot::prepareGraph` extraction
- Unit 4 — Preferences active-plugin-description
- Unit 5 — `AudioGraphRuntime` audio-thread-side all-notes-off

Verifiable in isolation by their tests.

#### Phase B — Session + tests (Units 2, 3)

Depends on Unit 1. Lands the state-machine class with tests that use
the factory seam injected by Unit 3.

#### Phase C — wiring (Units 6, 7)

Depends on Units 1–5. Wires Session into `App` and `MainWindow`,
threads restore-on-launch, finishes the UI feedback. End-to-end
manual smoke test on macOS confirms the original problem ("frozen
on load") is resolved.

#### Phase D — doc cleanup (Unit 8)

Trailing house-keeping. Land in the same MR as Phase C or as a tiny
follow-up.

## Alternative Approaches Considered

### Alt 1 — `PluginSlot` grows the state machine in place

Don't introduce `Session`; just give `PluginSlot` a worker thread and
the mailbox. **Rejected** because the master plan §4.2 places the
state machine on `Session` for a reason: when Phase 2 lands
`PatchRegistry` + `Patch`, the state machine generalises from "swap
one plugin" to "swap one whole patch". `PluginSlot` then naturally
becomes the per-node primitive. Locating the state machine in its
eventual home now avoids a bigger Phase-2 refactor.

### Alt 2 — `juce::AudioPluginFormatManager::createPluginInstanceAsync`

JUCE provides an async create. Use it directly from Session and skip
the worker thread. **Rejected** because we still need a single
serialisation point for `prepareToPlay` + graph build (which JUCE's
async-create does *not* do for us) and we need a coalescing mailbox.
Building the worker thread anyway is no extra cost and keeps the
state machine isolated and test-injectable.

### Alt 3 — Message-thread-side all-notes-off

Path B from the architecture section. **Rejected as default** because
of the race window during the flush; **kept as fallback** in case
audio-thread-side proves harder to land safely than the master plan
suggests.

## System-Wide Impact

### Interaction graph

When a user clicks "Load Plugin" on a 2-second-instantiation synth:

1. UI button click on message thread → `Session::queueLoad(desc)`.
2. Session writes `desc` into the mailbox under `mailboxMutex_`,
   transitions `state_` to `Building`, signals the worker
   `WaitableEvent`, fires `sendChangeMessage()`.
3. MainWindow's `changeListenerCallback` runs on message thread,
   reads `Session::State::Building`, disables the Load button,
   updates the status label.
4. Worker thread wakes, snapshots mailbox, clears it, calls
   `slot_.prepareGraph(desc, sr, block, errorOut)` (this is where
   the 2 seconds happens — *off* the message thread; UI stays
   responsive).
5. Worker `MessageManager::callAsync(λ)` → message thread runs
   `Session::onBuildResult(prepared, errorOut)`. If a newer
   `queueLoad` arrived during the build, the prepared graph is
   discarded and we re-enter step 4 with the new desc; if not, we
   transition `state_ → Swapping`, call
   `engine_.loadGraph(std::move(prepared.graph))` which goes through
   `AudioGraphRuntime::swapPreparedGraph`.
6. `swapPreparedGraph` publishes `pending_`, waits for the audio
   thread to (a) flush all-notes-off on outgoing, (b) atomic-swap
   `live_ ← pending_`, (c) bump generation counter twice.
7. After the wait, the message thread receives the outgoing graph
   `unique_ptr` back. RAII destroys it on the message thread, with
   `releaseResources()` already invoked.
8. `Session::onBuildResult` updates `slot_`'s observers
   (`bypassed_`, `hostedPlugin_`, `loadedName_`) — these have been
   moved to a `commitSwap` helper on PluginSlot, set from the
   Session — transitions `state_ → Idle`, fires
   `sendChangeMessage()`.
9. MainWindow's listener re-enables the Load button, clears the
   status. App's listener writes the new
   `activePluginDescription` to Preferences (auto-save batches it
   to disk in ~3 s).

### Error propagation

- **Plugin instantiation failure on worker thread:**
  `slot_.prepareGraph` returns false; worker callback sets
  `lastError_`, transitions to Idle, leaves the live graph
  untouched (per the master plan recovery path "PREPARING →
  build failure → IDLE").
- **All-notes-off flush failure on audio thread (e.g., outgoing
  graph crashes during the flush):** out of scope for 1.D.2.
  The §1.B SEH/signal-handler watchdog already exists for this on
  `BypassableInstance`; if the outgoing plugin dies on its
  flush block, it's marked dead and the swap proceeds with
  silence on the dead node.
- **Worker thread join during shutdown with a build in flight:**
  `Session` dtor sets a quit flag and signals the
  `WaitableEvent`. The worker observes the flag at its next
  yield point. The in-flight `prepareGraph` call cannot be
  preempted — we wait for it to finish (worst case: 4 s) and
  drop the result on the floor. Acceptable for shutdown.

### State lifecycle risks

- **Mailbox mutex contention:** UI thread + worker thread only.
  Critical section is a single `std::optional<PluginDescription>`
  copy. Cannot starve.
- **`pending_` not consumed before runtime teardown:** dtor reads
  both `live_` and `pending_` and destroys both. Tests cover this
  edge case (Unit 5 scenario 2).
- **Restore-on-launch with a known-bad plugin (the previously
  loaded plugin no longer exists on disk):** `queueLoad` issues
  a build that fails inside `createInstance`. State returns to
  Idle with an error message; the user sees a status-line
  warning; `Preferences::setActivePluginDescription` is *not*
  written (because the load did not succeed), so the next
  launch re-attempts. If we want "stop trying after N failures"
  that's a v3.x polish, not 1.D.2.
- **App quit while restore is mid-build:** Session dtor joins
  the worker, prepared graph (if any) is dropped, no swap
  occurs. Active-plugin XML in Preferences is unchanged from
  the previous successful load.

### API surface parity

`MainWindow`'s "Load Plugin" code path is the only caller that
moves from `PluginSlot::load` to `Session::queueLoad`. Tests of
`PluginSlot::load` keep working unchanged because we did not
remove `load`.

When the agent-native review pass runs, every UI action ("load
plugin", "unload") must have a matching agent-callable
`Session::queueLoad/queueUnload` — they are already public on
the `Session` class, satisfying the agent-parity invariant.

### Integration test scenarios

1. **Restore-on-launch round-trip.** `App` starts, user loads
   plugin X, user quits, `App` starts again, plugin X is loaded
   automatically. Verified by manual macOS smoke test (no headless
   plugin instance available).
2. **Coalescing under burst load.** Inject N=20 synthetic
   `queueLoad` calls in 200 ms via the test factory; assert
   exactly 1 `commitSwap` runs (or up to 2, depending on
   whether the very first build had already started before the
   second arrived); the final loaded plugin is the last
   queued.
3. **All-notes-off observed on outgoing.** AudioGraphRuntime
   test with a `TestMidiSinkProcessor` swap-out.
4. **Active-plugin XML survives Preferences re-construct.**
   Preferences_test scenario 3.
5. **Session dtor mid-build doesn't deadlock.** Session_test
   scenario 4. Run under `-fsanitize=thread` if the linux-rtsan
   preset is available; otherwise just under release + valgrind.

## Acceptance Criteria

### Functional Requirements

- [ ] **AC-1.D.2-A** UI does not freeze when loading a plugin —
  `MainWindow` remains responsive throughout instantiation.
- [ ] **AC-1.D.2-B** Two `Session::queueLoad` calls within the same
  build window collapse to the second one (latest-wins). The first
  build's prepared graph is discarded without being swapped in.
- [ ] **AC-1.D.2-C** A failed build (bogus description) leaves the
  previously-live graph audible and untouched.
- [ ] **AC-1.D.2-D** Outgoing MIDI receivers receive
  `allNotesOff` + `allSoundOff` + `allControllersOff` on every
  channel 1..16 before the live pointer flips.
- [ ] **AC-1.D.2-E** Last-loaded plugin description persists across
  app restarts via `Preferences`.
- [ ] **AC-1.D.2-F** On launch, the saved plugin description is
  re-loaded automatically without freezing the UI.
- [ ] **AC-1.D.2-G** Manual `unload` clears the active-plugin
  description from `Preferences`.

### Non-Functional Requirements

- [ ] **AC-1.D.2-H** Worker thread joins cleanly on `Session`
  destruction within ≤ 5 s even mid-build (the cap is the slowest
  plugin's `createInstance`; document if any QA plugin exceeds it).
- [ ] **AC-1.D.2-I** No allocations on the audio thread introduced
  by Unit 5; the silence scratch buffer for the all-notes-off flush
  is allocated in `prepareToPlay`.
- [ ] **AC-1.D.2-J** No `juce::Logger` calls or `printf` on the
  audio thread (per master plan invariant 1).

### Quality Gates

- [ ] All new unit tests pass on `test:linux:unit` job.
- [ ] No new `clang-format-18` violations on lint job.
- [ ] No new `clang-tidy-18` warnings on lint job.
- [ ] `pb3_apply_strict_warnings` covers the new `.cpp` files.
- [ ] macOS build job stays green (`build:macos:debug`).

### Acceptance Criteria — Additions from deepening

- [ ] **AC-1.D.2-K** XXE regression test: malicious `Preferences.settings`
  with `<!ENTITY x SYSTEM "/etc/passwd">` does not exfiltrate the file's
  contents into the parsed `PluginDescription` (Unit 4 scenario 6).
- [ ] **AC-1.D.2-L** Restore-on-launch only loads descriptions present
  in `pluginHost.getKnownPluginList()` matching `(pluginFormatName,
  fileOrIdentifier, uniqueId)`. Otherwise `clearActivePluginDescription`
  + non-fatal status report.
- [ ] **AC-1.D.2-M** All-notes-off flush is a safe no-op when outgoing
  live graph is null; remains safe when the outgoing instance is in
  `BypassableInstance::dead_` state (Unit 5 scenario 4).
- [ ] **AC-1.D.2-N** Restore-on-launch consults
  `PluginHost::getCrashedPluginsFile()` and refuses to instantiate
  blacklisted plugins. Instantiation attempts atomically tag the entry
  as in-flight; tag is cleared on success — a crash mid-`createInstance`
  leaves the entry, next launch short-circuits (crash-loop guard).
- [ ] **AC-1.D.2-O** `Session::queueLoad` issued before
  `engine.initialise()` returns a live device → state stays Idle, error
  set to "audio device not ready", no worker dispatch. Restore-on-launch
  defers via a one-shot listener until first device-state-changed.
- [ ] **AC-1.D.2-P** Sample-rate / block-size change during a build →
  in-flight prepared graph is re-`prepareToPlay`'d on the message thread
  in `onBuildResult` before `engine_.loadGraph` (Unit 2 scenario 8).
- [ ] **AC-1.D.2-Q** Active-plugin XML is written only when the new
  description differs from the persisted one (dirty-write-only). Equality
  via `(pluginFormatName, fileOrIdentifier, deprecatedUid)`. Avoids
  unnecessary disk churn on restore-load round-trips.
- [ ] **AC-1.D.2-R** `swapPreparedGraph` ack-wait is bounded by 2 s; on
  timeout, log warning, abandon swap, return prepared graph for retry
  (Unit 5 scenario 5).
- [ ] **AC-1.D.2-S** `Session::queueLoad(samePlugin)` triggers a full
  rebuild (and audible all-notes-off). Documented; not deferred.
- [ ] **AC-1.D.2-T** `Session::getStatus()` returns a single coherent
  snapshot (state + name + lastError + activeDescription + version) —
  no torn reads. `getStateVersion()` increments monotonically on every
  transition; `waitForIdle(timeoutMs)` returns true when the state machine
  is idle, false on timeout (agent-native polling support).

## Success Metrics

- **UI responsiveness during load** — load a synth that takes ≥ 1 s
  to instantiate; the message-thread JUCE event loop continues to
  pump (visible by dragging the window during load).
- **Coalescing efficiency** — `Session_test` scenario 2 confirms
  N=20 rapid queueLoads → ≤ 2 actual commits.
- **All-notes-off audibility** — manual macOS test with a synth
  that's been holding a chord; loading a different plugin produces
  silence cleanly (no held notes through the swap).
- **Restore-on-launch latency** — saved plugin is loaded within
  the time the splash screen would have been visible anyway; UI is
  responsive throughout.

## Dependencies & Prerequisites

- Phase 1.D.1 `PluginSlot` lands in `main` ✅ (commit `f57958d`)
- Phase 1.C.2 `Preferences` lands in `main` ✅
- JUCE 8.0.4 (`juce::PluginDescription::createXml`,
  `juce::MessageManager::callAsync`, `juce::WaitableEvent`)
  — already pinned via `external/JUCE`.
- No new third-party deps.

## Risk Analysis & Mitigation

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Audio-thread-side flush has a subtle race with the existing generation-counter wait | Medium | Medium | Land Path A behind a code review with explicit `acquire/release` pairing diagrams; add Unit 5 scenario 1 as a thread-sanitizer regression target. Fall back to Path B if Path A fails review. |
| Plugin's `prepareToPlay` crashes on the worker thread (JUCE expects the message thread for many plugin internals, even though the contract says "any non-RT thread") | Low | High | Wrap the worker's `prepareGraph` call in the existing SEH/signal handler from §1.B if it's already merged; otherwise, `try/catch` + log. The master plan §4.1.1 already calls `prepareToPlay` from the worker so this risk is inherent to the design — accept it and rely on the existing watchdog. |
| Session worker thread join deadlocks on shutdown | Low | High | Test scenario 4. Worker uses `juce::WaitableEvent` not `std::condition_variable`; quit flag is checked at every yield. |
| Restore-on-launch fails silently when the user has uninstalled the plugin since last quit | Medium | Low | Already handled by the build-failure path: error reported in status, `activePluginDescription` not overwritten. Acceptable behaviour for v3.0. |
| Coalescing causes the user to feel "the first thing I clicked never loaded" — they expect FIFO not latest-wins | Low | Low | Document in user-facing docs (post-1.D.2 doc pass). UI status label shows "Loading X" → user has feedback. |
| Heavy outgoing synth (Diva Divine, Omnisphere, Kontakt) takes 3–8 ms on its flush block, exceeding 5.3 ms RT budget at 256/48k | Medium | Low | Document AC-PS-1 footnote: "swap glitch bounded to ≤1 sample on heavy synths at <512-frame buffers". Recommend 512+ buffers when hosting these. Phase 5 polish: per-plugin "min buffer" hint surfaced in UI. |
| Stale `pending_` from an unfinished swap blocks future swaps after audio device disconnect | Low | High | Hard 2 s ceiling on the ack-wait (AC-1.D.2-R); on timeout, abandon, log, return graph for retry. Tested by Unit 5 scenario 5. |
| `AsyncUpdater` lambda fires on a partially-destroyed `Session` despite worker join | Low | High | `Session::~Session` calls `cancelPendingUpdate()` BEFORE joining the worker. Test by Session_test scenario 4 with `-fsanitize=thread`. |
| Attacker writes `Preferences.settings` pointing `fileOrIdentifier` at a malicious binary outside known plugin dirs | Low | High | KnownPluginList cross-check on restore (AC-1.D.2-L). Raises bar from "edit one settings key" to "edit two correlated settings keys". |
| Crashing plugin in active-description causes restore-loop ("crash-on-launch every time") | Medium | High | Crash-blacklist consult on restore (AC-1.D.2-N) with in-flight-tag-then-clear-on-success pattern. Self-healing across two launches. |

## Resource Requirements

- 1 engineer × ~2 days for Units 1–8 if `/ce:work` runs serially
  with subagent dispatch.
- macOS hardware for end-to-end smoke test (the user's MaxBook).
- No CI-side changes — existing `build:linux` /
  `test:linux:unit` / `build:macos:debug` jobs cover the new
  files automatically once they're added to `pb3_session_core`.

## Future Considerations

- **Phase 1.D.3 — multi-plugin chain.** Session's `queueLoad`
  generalises to `queuePatch(PatchSpec)` where `PatchSpec` is a
  list of `(PluginDescription, BypassState)` plus an internal
  topology. The state machine and worker thread are unchanged;
  `prepareGraph` becomes per-node-loop in the worker.
- **Phase 2 — `.pdl` / PatchRegistry.** Session subsumes
  `PatchRegistry`; `queuePatch` reads from the in-memory registry
  and the worker no longer needs to instantiate plugins directly
  (the registry already holds prepared instances or
  blob-deserialised descriptions).
- **Phase 1.B — out-of-process sandbox.** When `PluginScanner`
  becomes sandboxed, the worker thread continues to run
  `createInstance` in-process for hosting; only scanning moves
  out-of-process. No interaction with this plan.

## Documentation Plan

- `docs/architecture.md` — when it's written (master plan §10),
  the patch-switch state-machine section quotes Session's
  doc-comment.
- `app-v3/src/session/Session.h` — header doc-block describing the
  states + coalescing semantics, ≤ 30 lines, no inline tutorial.
- `app-v3/src/engine/AudioGraphRuntime.h` — update the existing
  doc-block to say "audio-thread-side all-notes-off on swap" once
  Unit 5 lands.
- No user-facing docs in 1.D.2 — the visible behaviour change ("UI
  no longer freezes on load") is the documentation.

## Resolve Before Planning

None. The master plan §4.1.1 + Appendix A.1 + the existing 1.D.1
codebase provide a complete design. Open implementation-time
decisions are listed below.

## Deferred to Implementation

- **All-notes-off path** — Unit 5 picks Path A (audio-thread-side)
  but tries Path B (message-thread-side) if Path A's review uncovers
  a subtle race we missed. Default Path A.
- ~~**Same-description re-queue dedup**~~ — **resolved:** full
  rebuild. AC-1.D.2-S.
- **Test-seam shape** — Unit 3 picks between an `std::function`
  factory ctor (Option A) and an `IPatchBuilder` interface (Option
  B). Pick whichever keeps Session_test cases short. **Constraint:
  must be public, not friend-only.**
- **Worker thread name** — name the JUCE thread `"PB3 Patch Build"`
  so it's distinguishable in macOS Instruments / Linux `perf`. (Use
  `juce::Thread`'s name argument; the constructor takes a `String
  threadName`.)
- **Watchdog on `prepareToPlay` worker call** — out-of-scope for
  1.D.2 unless §1.B's SEH/signal handler is already merged at the
  time Unit 2 lands; then trivially wrap.

### Resolved during deepening

- **`AsyncUpdater` vs `MessageManager::callAsync`** — chose
  `AsyncUpdater` with mutex-guarded result slot (lifetime-safe;
  `cancelPendingUpdate()` in dtor closes the residual race that
  worker-join alone cannot drain).
- **`prepareGraph` location** — free function in
  `session/PreparedGraph.{h,cpp}`, not a method on `PluginSlot`.
- **`PreferencesPersister` extraction** — yes, before Unit 6 lands
  (decouples App from listener fan-out).
- **`PluginDescription` serialisation** — keep XML round-trip via
  `createXml/loadFromXml`. Two-strings shortcut rejected: JUCE's full
  XML preserves uniqueId, vendor, format, fileTime — useful as soon
  as Phase 2 starts cross-checking against `KnownPluginList`.
- **State count** — keep three (`Idle`, `Building`, `Swapping`).
  Collapsing to two would conflate the message-thread-side
  `engine_.loadGraph` blocking window with the worker-thread build,
  hiding the 2-second ack-timeout's distinct meaning.
- **Coalescing-while-Building** — keep in 1.D.2. The state machine
  is the spine of Phase 2; testing coalescing here surfaces races
  that Phase 4 (MIDI Program Change) would otherwise hit blind.
- **Restore-on-launch** — keep in 1.D.2. Fully guarded by the
  KnownPluginList cross-check (AC-L) and crash-blacklist consult
  (AC-N), so it ships safely.
- **MIDI ordering** — `allSoundOff → allNotesOff → resetAllControllers`
  per channel (CC#64 sustain-latch fix).

## Scope Boundaries (Non-Goals)

- ❌ Multi-plugin chain (Phase 1.D.3).
- ❌ Plugin state blob restore — `setStateInformation` plumbing is
  Phase 2's `PdlSerializer` problem; 1.D.2 only restores the
  *description*, not the parameter state.
- ❌ Patch registry, next/previous patch commands.
- ❌ MIDI Program Change → patch switch (Phase 4 control surface).
- ❌ Out-of-process sandbox scan (Phase 1.B / 1.C.4).
- ❌ Per-plugin CPU meter UI.
- ❌ Trash FIFO generalisation — single outgoing graph per swap is
  enough for 1.D.2; the FIFO matters when 1.D.3 has many
  simultaneous swaps in flight.
- ❌ `ApplicationProperties` schema migration — the new key is
  additive; any v3.0 launch reads a missing key as nullptr (first-run
  semantics).

## Sources & References

### Origin

- **Origin document:**
  [docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md](2026-04-12-001-feat-pedalboard-3-rewrite-plan.md)
  — key decisions carried forward:
  - §4.1.1 patch-switch state machine (Idle/Preparing/Ready/Cleanup
    + depth-1 latest-wins coalescing)
  - Appendix A.1 `swapPreparedGraph` + worker-build skeleton +
    `flushMidiReceivers` helper
  - §4.2 placement of state machine on `session::Session` (sibling
    to `Preferences` + `UndoHistory`, not parent)
  - "live performance robustness contract" invariants 1–7 (no
    allocation/locking/IO on audio thread, atomic swap at buffer
    boundary, plugin crash isolation, NaN guard, atomic .pdl
    saves, single Session source-of-truth, RT-safety enforced by
    tooling)

### Internal References

- `app-v3/src/session/PluginSlot.h:25-31` — Phase 1.D.2 docstring
  promise: "load() becomes its blocking back-end, run on a worker
  thread". This plan honours that contract.
- `app-v3/src/session/PluginSlot.cpp:46-108` — current synchronous
  `load()` body; refactor target for Unit 1.
- `app-v3/src/engine/AudioGraphRuntime.cpp:77-110` — current
  `swapPreparedGraph`; refactor target for Unit 5.
- `app-v3/src/engine/AudioGraphRuntime.h:25` — Phase 1.A docstring
  promise: "the full patch-switch state machine ... is built on top
  of this primitive in Phase 1.D".
- `app-v3/src/AppContext.h:27-30` — pre-declared
  `session::Session (Phase 1.D.2 — patch-switch state machine)`
  member; Unit 6 fills it in.
- `app-v3/src/App.cpp:6-37` — initialise/shutdown pattern; Unit 6
  extends.
- `app-v3/src/session/Preferences.h:43-49` — `getAudioDeviceState`
  / `getKnownPluginList` pair; Unit 4 mirrors.

### External References

- JUCE 8 `AudioProcessorGraph` docs:
  `juce::AudioProcessorGraph::UpdateKind::none` for batched
  graph mutation (referenced in master plan §4.1.1 research note).
- JUCE 8 `juce::PluginDescription::createXml` /
  `loadFromXml` — round-trippable, used by `KnownPluginList`
  internally; same pattern Unit 4 follows.
- JUCE 8 `juce::WaitableEvent` /
  `juce::Thread::signalThreadShouldExit` — the canonical
  "worker with a quit flag" pattern.

### Related Work

- Phase 1.D.1 plan:
  [docs/plans/2026-04-26-003-feat-load-single-plugin-plan.md](2026-04-26-003-feat-load-single-plugin-plan.md)
  — direct predecessor.
- Phase 1.C.2 plan:
  [docs/plans/2026-04-26-002-feat-preferences-persistence-plan.md](2026-04-26-002-feat-preferences-persistence-plan.md)
  — Preferences foundation that Unit 4 extends.

### Deepen-Plan Research Sources (added 2026-04-27)

JUCE 8 framework:
- [docs.juce.com — AudioProcessorGraph](https://docs.juce.com/master/classAudioProcessorGraph.html)
- [docs.juce.com — AudioProcessorPlayer](https://docs.juce.com/master/classAudioProcessorPlayer.html)
- [docs.juce.com — MessageManager](https://docs.juce.com/master/classMessageManager.html)
- [docs.juce.com — AsyncUpdater](https://docs.juce.com/master/classAsyncUpdater.html)
- [docs.juce.com — WaitableEvent](https://docs.juce.com/master/classWaitableEvent.html)
- [JUCE forum — AudioProcessorGraph thread safety (Jules)](https://forum.juce.com/t/audioprocessorgraph-thread-safety/24419)
- [JUCE forum — Atomic swap resources onto the audio thread](https://forum.juce.com/t/atomic-swap-resources-onto-the-audio-thread/65707)
- [JUCE forum — VST3 Wrapper and MIDI AllNotesOff event](https://forum.juce.com/t/bug-vst3-wrapper-and-midi-allnotesoff-event/32054)
- [JUCE forum — Send all-notes-off when standalone plugin exits](https://forum.juce.com/t/solved-send-all-notes-off-when-standalone-plugin-exits/61604)
- [JUCE forum — Unsafe uses of callAsync](https://forum.juce.com/t/unsafe-uses-of-callasync/19647)
- [JUCE forum — Thread-safe weak reference](https://forum.juce.com/t/thread-safe-weak-reference/29690)
- [JUCE forum — AsyncUpdater vs MessageManager::callAsync](https://forum.juce.com/t/asyncupdater-vs-messagemanager-callasync/23459)
- JUCE 8.0.4 source (via `build/linux-dev/_deps/juce-src/`):
  `juce_PluginDescription.cpp`, `juce_AudioPluginFormatManager.h`,
  `juce_MessageManager.cpp`, `juce_AudioProcessorPlayer.cpp`,
  `juce_MidiMessage.h`, `juce_XmlDocument.cpp:39,182-193,278,804-893`,
  `juce_PropertySet.cpp:125-128`.

C++20 concurrency:
- [std::memory_order — cppreference](https://en.cppreference.com/cpp/atomic/memory_order)
- [std::jthread — cppreference](https://en.cppreference.com/cpp/thread/jthread)
- [Timur Doumler — Using locks in real-time audio processing, safely](https://timur.audio/using-locks-in-real-time-audio-processing-safely)

Internal cross-references:
- `app-v3/src/engine/PluginHost.cpp:116-129` — `getCrashedPluginsFile`,
  currently consumed only by the scanner (not by hosting). Unit 4
  changes this for the AC-1.D.2-N cross-check.
- `app-v3/src/session/Preferences.cpp:26-77` — Properties.settings
  IO via `PropertiesFile::getXmlValue` (string-overload `parseXML`).
- `app-v3/src/engine/AudioGraphRuntime.cpp:77-110` — generation-counter
  wait, refactor target for Unit 5 + AC-1.D.2-R.
- `app-v3/src/engine/BypassableInstance.cpp:23-48` — `prepareToPlay`
  scratch sizing; bump to 4× as part of the same diff for parity.
