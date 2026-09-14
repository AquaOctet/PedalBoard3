---
title: "Phase 1.D.1: load a single plugin into the audio graph"
type: feat
status: completed
date: 2026-04-26
---

# Phase 1.D.1: load a single plugin into the audio graph

## Overview

Persistence (Phase 1.C.2) closed the "plugin list survives launches" gap, but the
host still doesn't actually do anything with plugins — you can scan them, you
can blacklist them, you can't *use* them. This slice adds the minimum
end-to-end path: pick a plugin from the known list → instantiate via
`PluginHost::createInstance` → wrap in `BypassableInstance` → build a graph
→ swap it in via `AudioEngine::loadGraph`. After this slice the app is
functionally a single-plugin host: input → bypassable plugin → output, with
bypass + plugin-editor window.

This slice does **not** add the patch-switch state machine (queued switches,
worker thread, coalescing) — that is Phase 1.D.2 and depends on the load
path existing first. It also does not add multi-plugin chains; that is
Phase 1.D.3.

## Problem Statement / Motivation

After Phase 1.C.6 (Plugin List dialog) and 1.C.2 (persistence), the user
experience on macOS is: launch → scan → see your plugins → close dialog →
nothing happens. The plugin list is metadata-only as JUCE ships
`PluginListComponent`. To actually *host* a plugin we need code that:

- Looks up a `PluginDescription` by name or index.
- Calls `PluginHost::createInstance(desc, sr, bs, error)`.
- Wraps the returned `juce::AudioPluginInstance` in `BypassableInstance`
  (we built the wrapper in Phase 1.A; watchdog + ramped bypass + MIDI
  filter are already covered).
- Builds a tiny `juce::AudioProcessorGraph` with three nodes: audio-input
  → bypass-instance → audio-output.
- Calls `AudioEngine::loadGraph(...)` to swap it in atomically.

Plus a thin UI surface so a human can drive it: pick a plugin, hit Load,
see the name, hit Clear, see the editor.

## Proposed Solution

Introduce `pedalboard3::session::PluginSlot` — a message-thread-only object
that owns "the currently-loaded plugin" and the graph that hosts it.
Construction is cheap; nothing happens until `load()` is called. `load()`
synchronously builds the graph and swaps it in. `unload()` swaps a null
graph in. It is a `juce::ChangeBroadcaster` so the UI can observe changes
without owning the object.

Rationale for putting it in `session/` rather than `engine/`: the
slot is user-facing state ("which plugin is loaded right now"), not a
real-time primitive. Phase 1.D.2 grows this into a `session::Session` with
a `PatchRegistry` and the actual switch state machine; Phase 1.D.3 grows
it into a chain. Keeping it under `session/` from day one means future
slices extend rather than rename.

### API shape

```cpp
// app-v3/src/session/PluginSlot.h
namespace pedalboard3::session
{
class PluginSlot : public juce::ChangeBroadcaster
{
public:
    PluginSlot(engine::AudioEngine& engine, engine::PluginHost& host);
    ~PluginSlot();

    // Synchronously instantiate `desc`, wrap, build a graph, swap it in.
    // Returns false + fills `errorOut` if the plugin failed to load
    // (most common: VST3 SDK can't find the binary, plugin's ctor threw,
    // bus layout incompatible). Idempotent — replaces any prior load.
    bool load(const juce::PluginDescription& desc, juce::String& errorOut);

    // Swap a null graph in; the engine goes silent. Idempotent.
    void unload();

    // What's currently loaded? Empty when nothing is loaded.
    juce::String getLoadedName() const;
    bool isLoaded() const noexcept { return current_ != nullptr; }

    // Bypass control on the wrapper. No-op when nothing is loaded.
    void setBypassed(bool shouldBypass);
    bool isBypassed() const;

    // For the "Edit Plugin" window — returns the underlying plugin so the
    // UI can call `createEditor()`. Lifetime of the returned pointer
    // tracks the loaded plugin; nullptr when nothing is loaded.
    juce::AudioPluginInstance* getHostedPlugin() const;

private:
    engine::AudioEngine& engine_;
    engine::PluginHost&  host_;

    // Owned for the lifetime of the load. The graph also owns it as a
    // node, so we keep a non-owning observer pointer for control calls
    // (bypass, getLatency).
    engine::BypassableInstance* bypassed_{nullptr};
    juce::AudioPluginInstance*  hostedPlugin_{nullptr};
    std::unique_ptr<juce::AudioProcessorGraph> current_;

    juce::String loadedName_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSlot)
};
}
```

### Graph topology

```
[ AudioGraphIOProcessor: audioInputNode ]
       0 → 0
       1 → 1
              [ BypassableInstance(plugin) ]
                     0 → 0
                     1 → 1
                            [ AudioGraphIOProcessor: audioOutputNode ]
```

Two stereo channels in, two out. `setPlayConfigDetails(2, 2, sr, bs)` on
the graph; the graph's `prepareToPlay` cascades into the
BypassableInstance, which calls `prepareToPlay` on the inner plugin and
sets the watchdog budget.

When the plugin doesn't have stereo I/O, JUCE's `setBusesLayout` will
return false. For 1.D.1 we surface that as a load failure (error
message); 1.D.3 will handle mono / multi-out properly.

### Wiring in App + AppContext + MainWindow

- `AppContext` gains `session::PluginSlot& pluginSlot`.
- `App::initialise` constructs the slot AFTER engine + pluginHost (depends
  on both), passes refs into `AppContext.emplace(...)`.
- `App::shutdown` resets it BEFORE engine/pluginHost (which it depends on).
- `MainWindow` adds a row of UI: a `juce::ComboBox` populated with
  plugin names from `pluginHost.getKnownPlugins()`, a "Load" button, a
  "Clear" button, an "Edit" button, a "Bypass" toggle, and a
  "Loaded: <name>" status label. The `ComboBox` is rebuilt when the
  KnownPluginList changes (we already listen via the App's
  ChangeBroadcaster wiring, so MainWindow gets a hook too).
- "Edit" pops a `juce::DialogWindow` containing the plugin's
  `createEditor()`. Manage as `std::unique_ptr<juce::DocumentWindow>` so
  closing it triggers a callback that nulls the unique_ptr.

## Technical Considerations

- **Graph node lifetime.** `juce::AudioProcessorGraph::addNode` takes a
  `std::unique_ptr<juce::AudioProcessor>` and the graph owns it from
  there. We hold a non-owning observer pointer to the BypassableInstance
  for control calls (bypass) — it's safe as long as the graph is still
  alive in `current_`.
- **Sample rate / block size.** Pulled from the live audio device:
  `engine.getDeviceManager()->getCurrentAudioDevice()` returns the active
  device or nullptr; we fall back to 48 k / 512 like `MainWindow`'s
  `buildTestToneGraph()` already does.
- **Plugin editor lifetime.** `createEditor()` returns a `juce::AudioProcessorEditor*`
  the host owns. We wrap it in a `juce::DialogWindow` whose
  `closeButtonPressed()` deletes the dialog (and thus the editor). Avoid
  leaking the editor when the plugin is unloaded by closing the editor
  window first.
- **Restoring last-loaded plugin (Phase 1.D.2 prep).** Out of scope here.
  We won't write the loaded plugin's ID into Preferences yet; that lands
  alongside the patch-switch state machine.
- **Buggy plugin crash on load.** Same hazard as scanning — a plugin's
  ctor can throw or call a pure virtual. Phase 1.C.4 sandbox addresses
  it. For 1.D.1 we accept the risk and document it; the load path
  already returns an error string when JUCE's `createPluginInstance`
  reports failure.

## System-Wide Impact

- **Interaction graph:** UI Load button → `PluginSlot::load(desc)` → `PluginHost::createInstance(...)` → wrap in `BypassableInstance` → build graph → `AudioEngine::loadGraph(...)`. The previous graph (TestTone or null) is replaced; the audio thread sees the new graph atomically via `AudioGraphRuntime::swapPreparedGraph`.
- **Error propagation:** `PluginHost::createInstance` returns `(nullptr, errorString)` on failure. PluginSlot bubbles the same string to the caller; MainWindow surfaces it via `juce::AlertWindow::showMessageBoxAsync`.
- **State lifecycle risks:** If the user clicks Load while a Test Tone is playing, the bypass-graph replaces the tone graph cleanly (same `loadGraph()` path the existing tone toggle uses, so there's no new lifecycle risk). Pluging editor windows must be closed before the slot is unloaded; PluginSlot's dtor closes any open editor before destroying the graph.
- **API surface parity:** None — PluginSlot is the only entry point for "load a plugin into the audio graph". Phase 1.D.2 adds a queued/worker-thread variant; the synchronous `load()` here will become its blocking back-end.
- **Integration test scenarios (manual on macOS):**
  1. Pick a known-good VST3 → Load → audio passes through it (verify with Test Tone or a real signal source).
  2. Hit Bypass → audio goes around the plugin (BypassableInstance ramps over ~20 ms — should be glitch-free).
  3. Hit Edit → plugin's GUI opens in a window. Tweak a parameter; audio responds.
  4. Pick a different plugin → Load → first plugin unloads, second loads, no crash. Editor window closes.
  5. Hit Clear → silence (engine has empty graph).
  6. Pick a known-buggy plugin → Load → AlertWindow shows error string; previously-loaded plugin (if any) remains intact.

## Acceptance Criteria

### Functional
- [ ] `pedalboard3::session::PluginSlot` exists, is constructed in App, and added to AppContext.
- [ ] `PluginSlot::load(desc, errorOut)` returns true and populates the audio graph for a working VST3 / AU.
- [ ] `PluginSlot::load` returns false + error string for a bogus / missing plugin; no crash; no graph mutation.
- [ ] `PluginSlot::unload()` clears the graph; engine goes silent.
- [ ] `PluginSlot::setBypassed()` flips the BypassableInstance bypass state.
- [ ] `PluginSlot::getHostedPlugin()` returns a usable pointer the UI can hand to `createEditor()`.
- [ ] MainWindow gains: ComboBox of known plugins, Load button, Clear button, Edit button, Bypass toggle, "Loaded: …" label.
- [ ] ComboBox repopulates when the KnownPluginList changes.

### Quality
- [ ] Catch2 unit tests for PluginSlot covering: construction (no-op until load); failed load (bogus desc); unload-before-load (no-op); bypass control after a successful load using a stub `juce::AudioProcessor` injected via test seam.
- [ ] All 53 existing tests still pass.
- [ ] Linux CI builds + lint cleanly.

### Manual on macOS (sign off after PR)
- [ ] Load a known-good VST3 → audio audible end-to-end.
- [ ] Bypass toggle works without click/pop.
- [ ] Edit opens the plugin GUI; parameter changes are audible.
- [ ] Switching between two plugins doesn't crash.
- [ ] Clear silences the engine.

## Dependencies & Risks

- **Hard dependency:** `engine::BypassableInstance` (Phase 1.A — already shipped). `engine::PluginHost::createInstance` (Phase 1.C.3 — already shipped).
- **Plugin compatibility:** A non-trivial fraction of installed plugins will fail to load on a fresh test machine: missing licence-server connectivity, mismatched format, dynamic library not found. We surface those as load errors. Out of scope to *fix* third-party brokenness.
- **Bus layout mismatch:** Some plugins are mono-only or have unusual bus configs. JUCE's `setPlayConfigDetails(2, 2, …)` will silently coerce many; some will crash on `prepareToPlay`. For 1.D.1 we log + surface the error.
- **Risk: editor window outlives plugin.** A naïve "open editor in a DocumentWindow, do nothing on close" leaks the editor and may use-after-free if the plugin is unloaded. Mitigation: PluginSlot owns a weak handle to the editor window and closes it on `unload()`; the window's close button releases the editor before the dialog dies.
- **Risk: load() hangs the message thread.** Plugin instantiation can take seconds (heavy synth ctors). For 1.D.1 we accept the freeze; Phase 1.D.2 puts it on a worker thread.
- **Out of scope:** Persisting the loaded plugin (Phase 1.D.2). Multi-plugin chains (Phase 1.D.3). Plugin parameters bound to MIDI/OSC (Phase 4). Latency reporting on the device (Phase 1.D.2 also).

## Implementation Sketch

### New files
- `app-v3/src/session/PluginSlot.h` — class declaration above.
- `app-v3/src/session/PluginSlot.cpp` — load/unload/bypass + graph build.
- `app-v3/tests/unit/session/PluginSlot_test.cpp` — 4-5 cases.

### Edits
- `app-v3/src/session/CMakeLists.txt` — add PluginSlot to the lib's source list. Already has correct deps (juce_data_structures + juce_core); adds `juce_audio_processors` PUBLIC for the AudioProcessorGraph.
- `app-v3/src/AppContext.h` — add `session::PluginSlot& pluginSlot;`.
- `app-v3/src/App.h` — add `std::unique_ptr<session::PluginSlot> pluginSlot;`.
- `app-v3/src/App.cpp` — construct after engine + pluginHost, destroy before them.
- `app-v3/src/ui/MainWindow.cpp` — new ComboBox + 4 buttons + label; wire to slot. Listen on `pluginHost.getKnownPluginList()` to repopulate the ComboBox.
- `app-v3/tests/CMakeLists.txt` — add the new test file to PB3_TEST_SOURCES.

## Sources & References

- Origin: macOS 2026-04-26 smoke-test — "Plugin List shows my plugins but I can't actually do anything with them."
- `juce::AudioProcessorGraph::addNode` / `addConnection` — `_deps/juce-src/modules/juce_audio_processors/processors/juce_AudioProcessorGraph.h`.
- `juce::AudioGraphIOProcessor` for input/output nodes — same file.
- `engine::AudioEngine::loadGraph` — `app-v3/src/engine/AudioEngine.cpp:46`.
- `engine::BypassableInstance` — `app-v3/src/engine/BypassableInstance.h`.
- `engine::PluginHost::createInstance` — `app-v3/src/engine/PluginHost.cpp:98`.
- `MainWindow::buildTestToneGraph` (existing reference for graph construction pattern) — `app-v3/src/ui/MainWindow.cpp:26`.
- Master plan §4.2.B (Session subsystem) — `docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md:216`.
