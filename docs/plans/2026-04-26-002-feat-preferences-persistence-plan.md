---
title: Persist audio-device choice + scanned plugin list across launches
type: feat
status: active
date: 2026-04-26
---

# Persist audio-device choice + scanned plugin list across launches

## Overview

Phase 1.C.2 of the rewrite plan: every time a developer or QA tester
launches Pedalboard 3 today, they get the platform-default audio device
(not the one they last picked) and an empty `KnownPluginList` (so the
Plugin List dialog forces a re-scan, which on the user's Mac dev box
re-triggers the dead-man's-pedal dance for any buggy plugins until
Phase 1.C.4 sandbox scanning lands). This slice introduces a small
`session::Preferences` subsystem backed by `juce::ApplicationProperties`
so the AudioDeviceManager and KnownPluginList survive across runs.

## Problem Statement / Motivation

- **Audio device resets every launch.** `AudioEngine::initialise()` calls
  `juce::AudioDeviceManager::initialiseWithDefaultDevices(2, 2)` and
  ignores any prior choice. On macOS this means the default output device
  every time; on Linux the first ALSA device JUCE finds.
- **Plugin list is empty every launch.** `PluginHost` constructs a fresh
  `KnownPluginList` and never reads from disk. The Plugin List dialog
  shows zero plugins until the user clicks Scan, which on populated dev
  machines is the same dangerous in-process scan that crashed once
  already (origin: Phase 1.C.6 macOS smoke-test on 2026-04-26).
- **Plugin blacklist resets every launch.** The dead-man's-pedal mechanism
  in `juce::PluginListComponent` uses `KnownPluginList::addToBlacklist()`
  in memory only (`juce_PluginListComponent.cpp:443`). Without persistence,
  every relaunch re-pristines the blacklist and a buggy plugin re-crashes
  the next first-scan. With persistence, the blacklist sticks.

Phase 1.D (patch-switch state machine) needs both a stable device choice
(so the engine has known SR/blocksize for prepared graphs) and a populated
plugin list (so the user can double-click to instantiate). 1.C.2 unblocks
1.D.

## Proposed Solution

Introduce `pedalboard3::session::Preferences`, a thin wrapper around
`juce::ApplicationProperties`. Wire it through `AppContext` so the engine,
plugin host, and dialog components all read/write through one source of
truth. JUCE's `AudioDeviceSelectorComponent` and `PluginListComponent`
both accept a `juce::PropertiesFile*` constructor argument and handle
persistence internally — we just have to give them the right pointer
instead of `nullptr`.

The same `PropertiesFile` carries:

- `audioDeviceState` — XML element produced by
  `AudioDeviceManager::createStateXml()`, fed back to
  `AudioDeviceManager::initialise(..., preferredXml=)` on startup.
- `pluginList` — XML element produced by `KnownPluginList::createXml()`,
  fed back to `KnownPluginList::recreateFromXml()` on startup.
- Plugin blacklist — managed automatically by `KnownPluginList` once it
  knows about the `PropertiesFile` (set via `setCustomScanner` /
  blacklisting calls flow into the same XML).

### Where the file lives

`juce::ApplicationProperties` defaults to OS-conventional locations:

| OS      | Path                                                    |
|---------|---------------------------------------------------------|
| macOS   | `~/Library/Application Support/Pedalboard 3/Preferences.settings` |
| Linux   | `~/.config/Pedalboard 3/Preferences.settings`           |
| Windows | `%APPDATA%\Pedalboard 3\Preferences.settings`           |

Same parent directory as the existing `crashed_plugins.xml` (good — single
folder for all per-user state).

### API shape

```cpp
// app-v3/src/session/Preferences.h
namespace pedalboard3::session
{
class Preferences
{
public:
    Preferences();   // Opens the PropertiesFile at the OS-conventional path.
    ~Preferences();

    // Hand to JUCE dialog components that take a PropertiesFile*.
    juce::PropertiesFile* getCommonSettings() noexcept;

    // Load the saved AudioDeviceManager state (returns nullptr if first run).
    std::unique_ptr<juce::XmlElement> getAudioDeviceState() const;
    void setAudioDeviceState(const juce::XmlElement& state);

    // Load/save the scanned KnownPluginList. Blacklist travels with it.
    std::unique_ptr<juce::XmlElement> getKnownPluginList() const;
    void setKnownPluginList(const juce::XmlElement& xml);

    // Force a write to disk (called on shutdown for safety).
    void flush();

private:
    juce::ApplicationProperties props_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Preferences)
};
} // namespace
```

### Wiring in App and AppContext

```cpp
// AppContext gains a Preferences& field next to engine + pluginHost.
struct AppContext { engine::AudioEngine& audioEngine; engine::PluginHost& pluginHost; session::Preferences& preferences; };

// App::initialise order:
//   prefs → engine (passes prefs' device-state XML on init) → pluginHost
//   (loads plugin-list XML, sets common-settings ptr) → context.emplace(...)
//   → mainWindow
// App::shutdown order: mainWindow → context → pluginHost (save list) → engine
//   (save device state) → prefs (flush) → engine.reset → prefs.reset
```

`MainWindow::openAudioSettings()` and `openPluginList()` get
`ctx_.preferences.getCommonSettings()` instead of `nullptr` — JUCE's
selector & list components then auto-save user changes.

### Listener-driven incremental save

To avoid losing work on hard crash:
- `AudioDeviceManager::addChangeListener(...)` → on change, call
  `prefs.setAudioDeviceState(*deviceManager.createStateXml())`.
- `KnownPluginList::addChangeListener(...)` → on change, call
  `prefs.setKnownPluginList(*list.createXml())`.

`ApplicationProperties` flushes to disk every ~3 s (default) and on
clean shutdown. Hard-crash worst case: lose ≤3 s of device/list edits.

## Technical Considerations

- **Threading:** `Preferences` is message-thread-only. Both
  `AudioDeviceManager` change events and `KnownPluginList` change events
  fire on the message thread, so the listener wiring is safe with no
  locking.
- **Backwards compat:** First run (no file) — every getter returns
  empty/nullptr; subsystems fall back to defaults. Same code path as
  today.
- **CLAP (Phase 1.F):** When CLAP arrives, the `KnownPluginList` will
  carry CLAP descriptions alongside VST3/AU/LV2. No schema change — the
  XML format is opaque per-format.
- **JUCE asks for "common" vs "user" settings:** `ApplicationProperties`
  splits into machine-wide vs per-user. We use per-user only —
  `getCommonSettings()` returns the user file. Naming chosen to match
  JUCE's API; the file location is per-user OS conventional path.

## System-Wide Impact

- **Interaction graph:** `Preferences` ctor → opens file (first-run creates).
  `AudioEngine::initialise(prefXml)` → `AudioDeviceManager::initialise(..., prefXml)`.
  `PluginHost::loadKnownList(xml)` → `KnownPluginList::recreateFromXml`.
  Listeners fire on every device change / list edit → write back to prefs.
  `App::shutdown` → `prefs.flush()` then teardown.
- **Error propagation:** `ApplicationProperties::saveIfNeeded()` returns
  `bool` — log on failure, don't throw. First-run "no file" is normal,
  not an error.
- **State lifecycle risks:** Order of teardown matters — must flush prefs
  BEFORE destroying engine/pluginHost (they own change listeners that
  will deregister during destruction).
- **API surface parity:** None — purely additive subsystem.
- **Integration test scenarios:**
  1. First run with no Preferences file → defaults applied, no crash.
  2. Second run after device change → restored device matches what was selected.
  3. Second run after a plugin scan → plugin list shows the same entries without rescanning.
  4. Second run after a crash blacklisted a plugin → plugin remains blacklisted (user verifies via dialog UI showing the disabled row).
  5. Concurrent device + plugin-list changes → both persist independently.

## Acceptance Criteria

### Functional
- [ ] `pedalboard3::session::Preferences` exists, opens the OS-conventional `Pedalboard 3/Preferences.settings` file at construction.
- [ ] `AudioEngine::initialise(...)` accepts an optional saved-state XML; uses it via `AudioDeviceManager::initialise(..., preferredXml=)`.
- [ ] `PluginHost` exposes `loadKnownList(const juce::XmlElement&)` / `saveKnownList(juce::XmlElement&)` (or a method that returns the XML); App orchestrates persistence via `Preferences`.
- [ ] `MainWindow::openAudioSettings` and `openPluginList` pass the live `PropertiesFile*` so JUCE's standard dialogs auto-save edits.
- [ ] `App::shutdown` calls `Preferences::flush()` before subsystems are destroyed.
- [ ] AppContext exposes `Preferences&` alongside `audioEngine` and `pluginHost`.

### Quality
- [ ] Unit tests (Catch2) covering: file is created on first construction; XML round-trip survives write+read; setting an XML and reconstructing returns the same content.
- [ ] No regression in the 47-test suite (pb3_smoke_tests still 47/47 with test 47 SKIP on populated machines).
- [ ] No new lint failures, no per-file format violations on the new code.

### Manual on macOS
- [ ] Pick a non-default output device → Quit → Relaunch → that device is still selected.
- [ ] Run a successful Plugin List scan → Quit → Relaunch → list still populated, no rescan needed.
- [ ] Crash a scan once (the MKU-style plugin) → Relaunch → that plugin remains blacklisted (no second crash on rescan).

## Dependencies & Risks

- **Hard dependency:** `juce::ApplicationProperties` (already linked via `juce_data_structures`, which is a transitive dep of `juce_audio_utils`).
- **Risk: stale prefs after a JUCE version bump.** AudioDeviceManager XML schema is JUCE-internal. If JUCE 8 → 9 changes the schema, our load may silently fail. Mitigation: log on parse failure and fall back to defaults; don't crash. Add a try/parse-then-default helper in `Preferences::getAudioDeviceState`.
- **Risk: cross-platform path drift.** macOS uses `Library/Application Support/`, Linux uses `.config/`. JUCE handles this via `ApplicationProperties::Options` defaults. Verify on macOS dev box with `~/Library/Application Support/Pedalboard 3/` and on Linux CI by inspecting the artifact.
- **Risk: race between AudioDeviceManager change-listener and shutdown.** If a device change fires on the message thread mid-shutdown, listener writes to prefs after prefs are flushed. Mitigation: deregister listeners FIRST in `App::shutdown` before flushing.
- **Out of scope (Phase 1.C.4):** Persisting the dead-man's-pedal "currently scanning" sentinel into `Preferences` instead of its own XML file. Keep `crashed_plugins.xml` as-is for now; consolidate when sandbox scan lands.
- **Out of scope (Phase 1.D):** Persisting the active patch / graph state. That's the patch-switch state machine.

## Implementation Sketch

### New files
- `app-v3/src/session/Preferences.h` — class declaration above.
- `app-v3/src/session/Preferences.cpp` — opens file with
  `ApplicationProperties::Options{ "Pedalboard 3" }`, exposes the four
  methods.
- `app-v3/src/session/CMakeLists.txt` — `add_library(pb3_session_core STATIC ...)`,
  links `juce::juce_data_structures` PUBLIC + `juce::juce_core` PUBLIC,
  links `pb3_warnings` PRIVATE via `pb3_apply_strict_warnings()`.
- `app-v3/tests/unit/session/Preferences_test.cpp` — 4-5 cases:
  - first-run-no-file → all getters return null
  - set-then-get round-trip for device state
  - set-then-get round-trip for plugin list
  - destroy-then-reconstruct preserves content
  - flush is idempotent

### Edits
- `app-v3/src/AppContext.h` — add `session::Preferences& preferences;` field.
- `app-v3/src/App.h` — add `std::unique_ptr<session::Preferences> preferences;`.
- `app-v3/src/App.cpp` — construct prefs first, pass device state to
  `engine->initialise()`, pass plugin-list xml to
  `pluginHost->loadKnownList()`, register change listeners that write
  back, deregister + flush on shutdown.
- `app-v3/src/engine/AudioEngine.h/.cpp` — `initialise()` overload that
  accepts `const juce::XmlElement* preferredState`. Defer to
  `AudioDeviceManager::initialise(..., preferredXml=state)`.
- `app-v3/src/engine/PluginHost.h/.cpp` — `loadKnownList(const juce::XmlElement&)` and `createKnownListXml()` (or const `getKnownListXml()`).
- `app-v3/src/ui/MainWindow.cpp` — replace the two `nullptr` PropertiesFile* arguments with `ctx_.preferences.getCommonSettings()`.
- `app-v3/src/CMakeLists.txt` — `add_subdirectory(session)`; pb3_app_core PUBLIC-links pb3_session_core; pb3_engine_core PUBLIC-links pb3_session_core (so AudioEngine.cpp can include the session/ header for the XML type alias if needed).
- `app-v3/tests/CMakeLists.txt` — add `unit/session/Preferences_test.cpp` to PB3_TEST_SOURCES; link pb3_session_core.

## Sources & References

- Origin: macOS dev smoke-test on 2026-04-26 — every relaunch re-empties the plugin list (Phase 1.C.6 conversation).
- JUCE `ApplicationProperties`: `_deps/juce-src/modules/juce_data_structures/app_properties/juce_ApplicationProperties.h`.
- `KnownPluginList::createXml` / `recreateFromXml`: `_deps/juce-src/modules/juce_audio_processors/scanning/juce_KnownPluginList.h`.
- `AudioDeviceManager::initialise(...)` accepts a `const XmlElement* preferredDefaultDeviceState`.
- Dead-man's pedal blacklist flow: `_deps/juce-src/modules/juce_audio_processors/scanning/juce_PluginListComponent.cpp:443-444`.
- AGENTS.md `pedalboard3::session::` namespace convention.
- Plan: `docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md` — Phase 1.C.2 row.
