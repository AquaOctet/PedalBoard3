# Pedalboard 3 — Parity Review vs Pedalboard2
Created: 2026-06-16

> **A June 2026 snapshot, kept for the record.** Much of what it lists as missing
> (transport, multi-patch banks, live MIDI/OSC input, undo) has shipped since, in
> milestones M1 to M6. For where things stand now, see
> `docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md` and the GitLab
> milestones.

Status snapshot of Pedalboard 3 against the original Pedalboard2 feature set and the master
rewrite plan acceptance criteria (docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md).
Produced by a 4-agent research pass over `source code/src/` (PB2), `app-v3/src/` (PB3), and the master plan.

# PART 1 — PARITY REVIEW

## Where we are, in one paragraph

Pedalboard 3 today is a **single-patch, effects-only, in-process VST3 effect rack** with a genuinely strong audio engine underneath it. The engine (atomic graph-swap with all-notes-off flush, NaN/Inf master guard, per-node watchdog, out-of-process plugin scanner) is the most complete area. Above it sits a working but minimal app: add/remove/reorder/bypass effect plugins on a read-only signal canvas, open the plugin's editor, save/load a v3 `.pdl`, import a Pedalboard2 `.pdl` (topology + names only), and auto-restore the last patch with a crash-loop guard. A **complete MIDI/OSC control layer and two internal processors (Level, VU) are written and unit-tested but not wired into the running app** — they ship in the binary as dead weight. There is no transport, no multi-patch banks, no live MIDI/OSC I/O, no internal-node serialization, no undo, no theming, no signing/auto-update.

## Status legend

- **Done** — wired and user-reachable in the running app.
- **Built-but-inert** — compiled into the shipping binary and unit-tested, but no app code path constructs/calls it.
- **Partial** — a real but incomplete slice of the v2 feature exists and is reachable.
- **Missing** — no implementation.
- **Needs-hardware** — logic may exist, but acceptance can only be confirmed on QA hardware / real plugins.

---

## Parity table — every Pedalboard2 feature vs Pedalboard3

### A. App shell, window & lifecycle

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| JUCE single-window app, named window, version | **Done** | `App.cpp`, `MainWindow.{h,cpp}` |
| Window position/size persistence | **Missing** | No `WindowState` key; `Preferences` stores only device/known-list/active-patch |
| Command-line / file-association `.pdl` open | **Missing** | App restore only via Preferences; Open is File-menu only |
| Close-to-tray + save-and-quit prompt | **Missing** | No tray; no `saveIfNeededAndUserAgrees` analogue |
| macOS Mac main menu | **Missing** | No menu-bar model |
| Process priority = High | **Missing** | Not set |
| Settings storage (XML PropertiesFile) | **Done** | `Preferences.{h,cpp}` (ApplicationProperties) |
| App-data folder / SVG-from-memory helpers | **Partial** | Colour tokens exist (`ColourScheme.h`); no SVG asset loader |

### B. Signal graph / audio engine

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| `FilterGraph` over `AudioProcessorGraph` (add/remove nodes+conns) | **Done** | `AudioGraphRuntime.{h,cpp}`, `PreparedGraph.{h,cpp}`, `Session.{h,cpp}` |
| Default graph IO nodes (audio in/out, MIDI in/out) | **Done** | `PreparedGraph.cpp` adds 4 `AudioGraphIOProcessor` endpoints |
| Internal plugin format exposing built-in node types | **Missing (by design)** | Plan rejects synthetic `PluginDescription`s; uses explicit `kind` (not yet built — see PART 2) |
| Per-plugin bypass wrapper, click-free ramp | **Done** | `BypassableInstance.{h,cpp}` ~20ms ramped bypass |
| Per-plugin MIDI channel filter | **Built-but-inert** | `BypassableInstance::setMidiChannel` — no UI, no live MIDI to drive it |
| MIDI-over-OSC injection into plugin | **Built-but-inert** | `OscMapping::midiFromOsc` logic exists; no socket |
| Audio device management + state save | **Done** | `AudioEngine.{h,cpp}` (ADM, saved `audioDeviceState`) |
| Audio settings dialog (1–16 in/out, SR/buffer) | **Done** | `TransportBar` → `AudioDeviceSelectorComponent` |
| MIDI input devices attached to graph | **Partial** | Devices *selectable* in Audio Settings; **nothing consumes input** (no `MidiInputCallback`) |
| AudioPlayHead (bpm, 4/4, isPlaying) to plugins | **Missing** | No playhead; no transport source |
| Audio singletons (format managers, thumbnail cache) | **Partial** | `PluginHost` wraps format manager; no AudioFormatManager/ThumbnailCache (no file processors) |
| CPU-usage meter (10 Hz poll) | **Built-but-inert** | `BypassableInstance::consumeCpuStats` produced; StatusBar shows em-dash; no Timer polls it |

### C. Plugin formats

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| Default JUCE host formats (VST/VST3/AU) | **Done** | `PluginHost` format registration |
| LADSPA / NAP formats | **N/A** | v2 had them disabled; PB3 targets VST3/AU/(LV2/CLAP) |
| CLAP hosting | **Partial** | `PB3_ENABLE_CLAP` CMake plumbing; **disabled in windows-ci**; not validated (AC-NEW-5) |
| Plugin scanning UI + dead-man's-pedal + list-saved-on-change | **Done** | `juce::PluginListComponent` in TransportBar; `KnownProblemPlugins`; out-of-process `PluginScanner` + helper |
| Drag-and-drop plugin file → scan+add | **Missing** | No DnD target |

### D. Internal processors (built-in nodes)

| Pedalboard2 node | PB3 status | Grounding |
|---|---|---|
| Level (gain) | **Built-but-inert** | `LevelProcessor.{h,cpp}` — never inserted into any graph |
| VU Meter | **Built-but-inert** | `VuMeterProcessor.{h,cpp}` — levels published, no UI consumer |
| Output Toggle | **Missing** | — |
| File Player | **Missing** | — |
| Audio Recorder | **Missing** | — |
| Metronome | **Missing** | — |
| Looper | **Missing** | — |
| MIDI Interceptor (hidden) | **Missing** | Control layer is inert; no interceptor node |
| OSC Input (dummy node) | **Missing** | — |
| (PB3-only) TestTone | **Done** | `TestToneProcessor`; Test Tone toggle in TransportBar |
| (PB3-only) OutputSanitiser (NaN guard) | **Done** | hardcoded final pass in `AudioGraphRuntime::processBlock` |

### E. Plugin canvas & node UI

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| Scrollable 2D canvas, double-click to add | **Partial** | `SignalCanvas` is a **read-only vertical list** in a Viewport; Add is a toolbar button, not canvas double-click |
| Add plugin via menu from KnownPluginList | **Done** | `TransportBar::openAddPluginMenu` from `getKnownEffects` |
| Drag-drop sound file → File Player node | **Missing** | No DnD, no FilePlayer |
| Node box (drag, rename, edit, mappings, bypass, delete, pins) | **Partial** | `PluginCard`: bypass/edit/move-up-down/remove + drawn ports. **No rename, no mappings button; cards not draggable; ports non-interactive** |
| Bezier connections, selectable/deletable, shift-drag connect-all | **Partial** | Cables **rendered** from `patch.connections`; **no click-to-connect, no shift-drag, no deletion** (linear chain only) |
| MIDI-enable-per-node | **Missing** | — |
| Plugin editor window (+ always-on-top, fixed-size, PresetBar) | **Partial** | `EditorWindow` hosts the editor; no always-on-top/fixed-size/PresetBar |
| Ignore-pin-names option | **Missing** | — |

### F. Patches & `.pdl` format

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| `.pdl` file (multi-patch root + optional DEVICESETUP) | **Partial** | `PdlSerializer` writes **single** `<PATCH>` under `<PEDALBOARD>`; no multi-patch, no device block |
| Per-patch XML (graph + state + mappings + usernames) | **Partial** | Topology + per-node plugin state + customName; **no mappings, no tempo** (PdlSerializer header defers them) |
| Multi-patch banks (combo, add/rename/next/prev/duplicate) | **Missing** | Single active patch only; no `PatchRegistry` |
| Patch Organiser dialog | **Missing** | — |
| Default patch (Save-As-Default / Reset, autoload `default.pdl`) | **Partial** | Last active patch auto-restored from Preferences; **no explicit Save-As-Default / `default.pdl`** (AC-FP-4) |
| Drag-drop `.pdl` → open | **Missing** | File>Open only (AC-FP-7) |
| Recent files list | **Missing** | — |
| Store audio settings inside `.pdl` (toggle) | **Missing** | Device state lives only in Preferences (AC-FP-5) |
| v2 `.pdl` import | **Partial** | `PdlV2Importer` imports topology+names+connections; **drops mappings, tempo, plugin state** (AC-FS-2) |
| Atomic save | **Partial** | temp-file + rename; **no fsync/dir-fsync** (AC-FS-6) |

### G. Transport, tempo, tap-tempo, metronome sync

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| MainTransport singleton (global Play/Pause/RTZ broadcast) | **Missing** | No transport subsystem at all |
| Play/Pause & RTZ buttons | **Missing** | "TransportBar" is misnamed — it is a File/Add/Settings toolbar with no transport |
| Tempo editor → playhead | **Missing** | — |
| Tap tempo (callout, keyboard/MIDI/OSC, 4-tap avg) | **Missing** | AC-TR-2 |
| Per-processor "Sync to Main Transport" | **Missing** | No syncable processors |

### H. MIDI mapping & control

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| MidiMappingManager (CC → param, app-command mappings) | **Built-but-inert** | `control/MidiMapping`, `MidiRouter`, `MidiControlSurface`, `ControlHub` — fully tested, never constructed by app |
| MidiMapping (CC#, channel, latch/toggle, lower/upper, inverted) | **Built-but-inert** | `MidiRouter` LatchState, `applyRange`/`scaleToRange` (AC-IO-5/8) |
| MIDI learn | **Built-but-inert** | `MidiControlSurface` arm/cancel/capture (AC-IO-4) |
| MIDI Program Change → patch switch | **Missing** | `MidiTargetKind::PatchSelect` reserved-only; no patch index (AC-PS-3) |
| MMC → transport | **Missing** | No transport (AC-TR-*) |
| Cross-thread MIDI dispatch (audio→message FIFO) | **Missing** | No live MIDI path exists to dispatch |
| **Live MIDI device input** | **Missing** | No `addMidiInputDeviceCallback`/`MidiInputCallback` anywhere (AC-IO-2) |

### I. OSC mapping & control

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| OscMappingManager (OSC → param, app cmd, MIDI-over-OSC) | **Built-but-inert** | `control/OscMapping`, `OscRouter`; `midiFromOsc`, `isOscMulticastAddress` (AC-IO-6/7) |
| OSC networking (UDP listener, port, multicast) | **Missing** | No `OSCReceiver`/socket bind anywhere (AC-IO-3) |
| Custom OSC lib / bundle handling | **Partial** | JUCE OSC types presumed; bundle dispatch logic not wired |

### J. Per-plugin mappings dialog (MIDI+OSC)

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| MappingsDialog (add/delete MIDI+OSC, target, CC, latch, bounds, learn, MIDI-over-OSC override) | **Missing** | No mapping UI; cards have no mappings button |
| MappingSlider (two-tick, inverted ranges) | **Missing** | — |

### K. Application command mappings (key/MIDI/OSC → app commands)

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| ApplicationCommandManager + full command set | **Missing** | No command manager |
| Key mappings persisted (`AppMappings.xml`) | **Missing** | AC-FP-8 (Home key) |
| ApplicationMappingsEditor tree UI | **Missing** | — |
| App-level MIDI/OSC mappings persisted | **Missing** | Preferences has no mapping keys |

### L. Plugin presets (user preset system)

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| PresetBar (own programs + user `.fxp`, import/save) | **Missing** | EditorWindow has no PresetBar |
| PresetManager (`.fxp` under appdata) | **Missing** | — |
| UserPresetWindow (cross-plugin preset tree) | **Missing** | — |
| Per-node program restore | **Partial** | Per-node plugin **state blob** captured/restored (`getActivePatchWithState`); no program/preset concept |

### M. Preferences / options

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| OSC port + multicast | **Missing** | — |
| Visible I/O node toggles | **Missing** | — |
| Auto-open mappings on connect | **Missing** | — |
| Loop next/prev patch | **Missing** | — |
| Plugin windows always-on-top / fixed-size | **Missing** | — |
| Ignore pin names | **Missing** | — |
| MIDI PC switches patches / MMC transport | **Missing** | — |
| Tray / start-in-tray | **Missing** | — |
| Save audio settings in `.pdl` | **Missing** | — |
| (PB3) crash-loop restore guard | **Done** | `PreferencesPersister` restore-in-flight marker |

### N. Theming, tray, logging, shared UI

| Pedalboard2 feature | PB3 status | Grounding |
|---|---|---|
| ColourScheme singleton + load/save `.colourscheme` | **Partial** | `ColourScheme.h` has hardcoded tokens; **no import/save**, no editor (AC-FP-1) |
| ColourSchemeEditor | **Missing** | — |
| Custom LookAndFeel | **Done** | `Pb3LookAndFeel.{h,cpp}` installed in `App::initialise` |
| System tray / menu bar | **Missing** | — |
| LogFile + LogDisplay (event log) | **Missing** | `diagnostics/` is an **empty directory**; no EventLog |
| WaveformDisplay | **Missing** | No file processors |
| AboutPage | **Missing** | — |
| Documentation launcher | **Missing** | — |

### O. PB3-only / new (plan) features

| Plan feature | PB3 status | Grounding |
|---|---|---|
| MIDI Output node (AC-NEW-1) | **Missing** | — |
| Auto PDC + per-patch opt-out (AC-NEW-2) | **Missing** | No LatencyCompensator |
| Undo/redo 50-step (AC-NEW-3) | **Missing** | — |
| OSC Monitor window (AC-NEW-4) | **Missing** | — |
| Per-node CPU in graph UI (AC-NEW-6) | **Partial** | Metering collected; not surfaced |
| NaN/Inf master guard (AC-SE-4) | **Done** | `OutputSanitiser` final stage |
| Out-of-process scanner + crash blacklist | **Done** | `PluginScanner` + helper + `KnownProblemPlugins` |
| SEH/signal crash guard around processBlock (AC-PL-1) | **Missing** | Only the timing watchdog exists; hard SIGSEGV still kills host |

---

## Phases 0–6 progress summary

- **Phase 0 — Infrastructure: PARTIAL (substantial).** CMake + `CMakePresets.json` (linux-dev/release/rtsan, windows-dev/ci, macos-universal), `.gitlab-ci.yml` (lint→build→test→release on 3 platforms), AGENTS.md, build wrappers. Missing: signing/notarization stubs not exercised, no runner/VM provisioning, hello-world signed artifact unmet.
- **Phase 1 — Audio engine core: PARTIAL (strongest area).** Atomic graph swap + all-notes-off, `BypassableInstance` (ramped bypass, MIDI filter, CPU metering, watchdog), `PluginHost` (VST3/AU/LV2 reg), out-of-process scanner + helper, `OutputSanitiser`, `AudioEngine`. Missing: SEH/signal crash guard, validated CLAP, LatencyCompensator, golden-render/pluginval/rtsan/soak CI gates, graph-wired internal processors, patch-swap benchmark.
- **Phase 2 — Session/persistence: PARTIAL.** `Session` state machine (Idle/Building/Swapping), worker-thread build + incremental mutation, `Patch`/`PdlSerializer` (v3 R/W), v2 read-import (topology+names), Preferences auto-restore + crash-loop guard. Missing: PatchRegistry/multi-patch, `.pdl` hardening (XXE/limits/atomic-fsync/identity-strict), forward-compat preservation, lossy-import modal, `default.pdl`, window-layout persistence.
- **Phase 3 — UI: PARTIAL (basic).** `MainWindow` + read-only `SignalCanvas` (bezier cables, cards, move/bypass/remove/edit), toolbar TransportBar, `Pb3LookAndFeel`. Missing: drag-to-connect, lasso, keyboard nav, real transport/PatchBar/CpuMeter/EventLog, undo, most dialogs, tray, colour-scheme import, card rename.
- **Phase 4 — Control surface: PARTIAL (logic-complete, fully unwired).** Entire MIDI/OSC mapping/routing/learn/latch/invert + MIDI-over-OSC + multicast-validate + `ControlHub` + `ControlParameterApplier`, all unit-tested headless. Missing the entire device/socket integration: no MIDI input callback, no OSC bind, `ControlHub` not in `AppContext`, no mapping UI, no command dispatcher.
- **Phase 5 — Remaining processors + polish: MISSING (~5%).** Only `LevelProcessor` + `VuMeterProcessor` landed, and neither is graph-wired or metered. No FilePlayer, Recorder, Looper, Metronome, OutputToggle, MidiOut, PDC, drag-drop, colour-scheme import.
- **Phase 6 — Release hardening/distribution: UNTOUCHED.** No MSI/DMG/AppImage, no signing/notarization, no Sparkle/WinSparkle/AppImageUpdate, no CrashReporter, no entitlements, no AV submission. CI release jobs upload RelWithDebInfo artifacts only.

---

## Top remaining gaps to be a full replacement (shortlist)

Ordered by how much they block "PB3 can replace PB2 for a live user." Each is grounded in the inventories above.

1. **Live MIDI/OSC I/O.** The control layer is 100% built and tested but inert — there is no MIDI input callback and no OSC socket anywhere in `app-v3/src`. Wiring `ControlHub` into `AppContext`/`App` plus a device/socket layer is the single highest-leverage gap (unlocks AC-IO-2/3, mapping, learn, latch, PC→patch). This is the literal definition of the product ("MIDI/OSC mapping of every parameter").
2. **Transport subsystem.** No MainTransport, play/stop/RTZ, tempo, tap-tempo, or playhead. Blocks all of Phase-5 synced processors and any tempo-aware plugin (AC-TR-*).
3. **Multi-patch banks + Patch Organiser + PC-switch.** PB3 is single-patch; PB2's defining live feature is rapidly switchable patches with a bank UI. Needs `PatchRegistry` and `.pdl` multi-patch (AC-PS-2/3, AC-FS-2).
4. **Internal processors actually in the chain** (File Player, Recorder, Looper, Metronome, Output Toggle, MIDI Out) — and even the two that exist (Level/VU) are inert. Requires the internal-node add-path (see PART 2). This is most of v2's per-node feature surface.
5. **Interactive canvas** (drag-to-connect, free node placement, arbitrary routing). PB3's canvas is a read-only linear chain; PB2's identity is a free 2D drag-to-connect graph. Needs `PatchNode` positions and click/shift-drag connect.
6. **Per-plugin & app mappings UI + persistence.** MappingsDialog, ApplicationMappingsEditor, key mappings, and `.pdl`/Preferences mapping persistence — none exist; the data models do.
7. **Crash isolation (SEH/signal guard).** Only the timing watchdog exists; a hard plugin SIGSEGV still kills the host — disqualifying for on-stage use (AC-PL-1).
8. **Presets, theming, tray, event log, drag-drop, About/docs** — the whole "polish/UX" tier (Phases 3/5 tail).
9. **`.pdl` durability + security hardening** (fsync chain, XXE-safe parse, size/identity limits, forward-compat preservation) — AC-FS-6/7/9, AC-SE-5..8.
10. **Release/distribution (Phase 6)** — signing, notarization, packaging, auto-update — entirely untouched; gates actual shipping.

---
