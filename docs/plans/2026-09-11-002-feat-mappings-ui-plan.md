---
title: Mappings UI and App Commands (M4) - Plan
type: feat
status: completed
date: 2026-09-11
topic: mappings-ui
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md
execution: code
---

# Mappings UI and App Commands (M4) - Plan

## Goal Capsule

- **Objective:** A performer manages every MIDI and OSC mapping of a plugin in one dialog (add, delete, target parameter, source, range, invert, latch, learn, MIDI channel filter), and binds app commands (at minimum next and previous patch) to keys, MIDI and OSC triggers that survive restarts.
- **Product authority:** roadmap M4 (R12, R13) in `docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md`, plus the completeness checklist's R8 (per-plugin MIDI channel filter, folded into M4) in `docs/plans/2026-07-05-001-feat-1-0-completeness-checklist-plan.md`. Pedalboard2's `MappingsDialog`, `MidiMappingManager`, `OscMappingManager` and `ApplicationMappingsEditor` (`source code/src/`) are the behavioural reference; the parity review (`docs/reviews/2026-06-16-pedalboard2-parity-review.md`, sections H to K) lists them as missing.
- **Execution profile:** headless-testable core (U1 to U3) and two dialogs verified by hand on a desktop build (U4, U5).
- **Stop conditions:** stop and surface if an app command would need to run on the MIDI thread, if a mapping edit would need a whole-patch swap, or if the key-mapping persistence would collide with JUCE's own per-component key handling (the canvas's Delete key).

---

## Product Contract

### Summary

The control layer can route, latch, scale and learn, but a performer can only create mappings one at a time through the card's Learn button (M2), with full range and no way to edit or delete them, no OSC authoring at all, no MIDI channel filter, and no app-level commands. M4 adds the per-plugin Mappings dialog, a MIDI channel filter per node, app-level MIDI/OSC mappings stored in Preferences, keyboard shortcuts through JUCE's command manager, and one App Mappings window to edit them.

### Problem Frame

Verified in the code on 2026-09-11:

- `Session` exposes `addMidiMapping` only. There is no edit, delete, or OSC mapping API. Mappings live in `Patch::midiMappings` / `oscMappings` and persist in the `.pdl`.
- `MidiTargetKind::AppCommand` with a `commandId` exists in the model and persists, but nothing dispatches it. `PatchSwitcher` (M3 U4) receives AppCommand targets on ControlHub's unhandled sink and drops them.
- A mapped control reaches the sinks as `(target, value)` only. A button's release and a Program Change 0 both arrive as value 0, so a non-idempotent command (next patch) cannot be told apart from a release. `PatchSelect` got away with this because selecting the same index twice is a no-op.
- `BypassableInstance::setMidiChannel` filters MIDI per node, but `PatchNode` has no channel field and no UI sets it.
- There is no `ApplicationCommandManager`; the only key handling is the canvas's Delete-a-cable.

### Key Decisions

- KD1. **The card's Learn button becomes a Map button that opens the per-plugin dialog; learn moves into the dialog.** Pedalboard2 had one mappings entry point per node. Keeping both would crowd the card, and learn without the ability to edit the result is the gap this milestone closes.
- KD2. **App commands for 1.0 are `patch.next` and `patch.previous`.** R13's minimum. Transport and file commands from Pedalboard2 come with M6 (transport) or stay mouse-only. The command table is a small registry so M6 adds entries without new plumbing.
- KD3. **Default keys: Page Down for next patch, Page Up for previous.** Bluetooth page-turner foot pedals, the cheapest hands-free controller a performer owns, send exactly these. Arrow keys stay free for the canvas.
- KD4. **App commands fire on a press edge.** Each routed update carries a `press` flag: Control Change value at or above 64, note-on, every Program Change, OSC value at or above 0.5. A command runs only on `press`. Pitch Bend is not offered as a command trigger.
- KD5. **App-level MIDI/OSC mappings live in Preferences, not the patch.** They must work across every patch in the bank. ControlWiring feeds the hub the active patch's mappings plus the app mappings.
- KD6. **Mapping edits go through one Session call per node that replaces that node's mappings** (live, bumps the mappings revision). The dialog edits live, with no Apply button, and the checklist's shallow undo (R12) later sees one operation per edit.
- KD7. **The MIDI channel filter is per node, persisted as a `<NODE>` attribute (schemaMinor 5), default 0 = omni.** Old files load unchanged.

### Requirements

- R1. A routed update carries a press flag (KD4); ControlHub delivers it to the unhandled-target sink.
- R2. An AppCommand mapping for `patch.next` or `patch.previous` steps the bank once per press, from MIDI or OSC.
- R3. `Session` can replace the plugin-parameter MIDI and OSC mappings targeting one node in a single call; the hub sees the change immediately.
- R4. Each node has a MIDI channel filter (omni or 1 to 16) that persists in the `.pdl`, applies when the patch is built, and changes live.
- R5. App-level MIDI and OSC mappings persist in Preferences, restore on launch, and route alongside the active patch's mappings.
- R6. A per-plugin Mappings dialog lists, adds, edits and deletes the node's MIDI mappings (source kind, channel, number, target parameter, range min/max, invert, latch, learn) and OSC mappings (address, argument index, target parameter, range, invert), and sets the node's MIDI channel filter.
- R7. `patch.next` and `patch.previous` are bound to keys through JUCE's command manager, with the defaults of KD3; custom bindings persist in Preferences.
- R8. An App Mappings window edits key bindings and the app-level MIDI/OSC mappings (command, source, learn, delete).

### Scope Boundaries

- Not in M4: Pedalboard2's MIDI-over-OSC override per mapping, the two-tick MappingSlider widget (plain min/max fields instead), auto-open-mappings-on-connect (deferred to 1.x by the checklist), an OSC monitor window, transport commands (M6).
- Mappings that target a removed node stay in the patch as harmless no-ops (existing behaviour); the dialog shows only the current node's mappings.

---

## Key Technical Decisions

- **KTD1 — `ParameterUpdate` gains `bool press{true}`.** `MidiRouter` sets it from the existing `readPress` logic (Up means false; Down and Pulse mean true; a latch toggle is always a press). `OscRouter` sets it from the raw argument (at or above 0.5). `MidiControlSurface`'s sink and `ControlHub`'s unhandled sink take the whole `ParameterUpdate` instead of `(target, value)`. The default of `true` keeps every existing aggregate initialisation compiling.
- **KTD2 — `session::AppCommands` is the registry:** a static table of `{commandId, display name, int JUCE command id}` plus `bool invoke(BankDocument&, const juce::String& commandId)`. `PatchSwitcher` calls it for AppCommand targets with `press == true`; the UI's `ApplicationCommandTarget::perform` calls it for key presses. One implementation per command.
- **KTD3 — `Session::setNodeMappings(NodeId, std::vector<MidiMapping>, std::vector<OscMapping>)`** removes every PluginParameter mapping whose `target.node` is the node, appends the given ones (targets forced to that node), bumps `mappingsRevision_`, and notifies. Other nodes' mappings and PatchSelect/AppCommand mappings in the patch are untouched.
- **KTD4 — `PatchNode::midiChannel`** (int, 0 to 16) is written as `midiChannel="n"` only when non-zero, applied to the `BypassableInstance` in `buildPreparedGraph` and in the add-plugin commit, and changed live by `Session::setNodeMidiChannel` under the same identity guard as `setNodeBypass`.
- **KTD5 — `session::AppMappings`** is a `ChangeBroadcaster` holding app-level MIDI and OSC mapping vectors, serialised with the same `<MIDI_MAP>` / `<OSC_MAP>` element shape the patch uses (the helpers in `Patch.cpp` move behind a small public API). Preferences stores it under `appMappings`; `ControlWiring` listens to it and feeds `patch ∪ app` to the hub.
- **KTD6 — Keys use `juce::ApplicationCommandManager` owned by `MainWindow`.** The root component is the `ApplicationCommandTarget`; `getKeyMappings()` is added as a key listener on the window; `KeyPressMappingSet::createXml(true)` (only differences from the defaults) persists in Preferences under `keyMappings`. The App Mappings window embeds `juce::KeyMappingEditorComponent` for the key half.

---

## Implementation Units

### U1. Press-edge triggers and app-command dispatch

- **Goal:** A MIDI or OSC mapping whose target is an app command runs that command once per press.
- **Requirements:** R1, R2
- **Dependencies:** none
- **Files:**
  - `app-v3/src/control/MidiMapping.h` (modify: `ParameterUpdate::press`)
  - `app-v3/src/control/MidiRouter.cpp` (modify: set `press`)
  - `app-v3/src/control/OscRouter.cpp` (modify: set `press`)
  - `app-v3/src/control/MidiControlSurface.h`, `app-v3/src/control/MidiControlSurface.cpp` (modify: sink takes `ParameterUpdate`)
  - `app-v3/src/session/ControlHub.h`, `app-v3/src/session/ControlHub.cpp` (modify: sinks take `ParameterUpdate`)
  - `app-v3/src/session/AppCommands.h`, `app-v3/src/session/AppCommands.cpp` (create: registry + invoke)
  - `app-v3/src/session/PatchSwitcher.cpp` (modify: dispatch AppCommand on press)
  - `app-v3/src/session/CMakeLists.txt` (modify)
  - `app-v3/tests/unit/control/MidiRouter_test.cpp`, `app-v3/tests/unit/control/OscRouter_test.cpp`, `app-v3/tests/unit/control/MidiControlSurface_test.cpp`, `app-v3/tests/unit/session/ControlHub_test.cpp`, `app-v3/tests/unit/session/PatchSwitcher_test.cpp` (modify)
  - `app-v3/tests/unit/session/AppCommands_test.cpp` (create)
  - `app-v3/tests/CMakeLists.txt` (modify)
- **Test scenarios:** CC 127 then CC 0 on a `patch.next` mapping steps once; note-on steps, note-off does not; every Program Change is a press (PC 0 included); OSC 1.0 steps, OSC 0.0 does not; a latch mapping's toggle is a press; `patch.previous` wraps; an unknown commandId is ignored; a PluginParameter mapping still applies on release (press is only consulted for commands).
- **Verification:** ctest green on Linux and Windows.

### U2. Per-node mapping edits and MIDI channel filter

- **Goal:** Session can replace one node's mappings and set its MIDI channel, both live and persisted.
- **Requirements:** R3, R4
- **Dependencies:** none
- **Files:**
  - `app-v3/src/session/Patch.h`, `app-v3/src/session/Patch.cpp` (modify: `PatchNode::midiChannel`, XML attribute)
  - `app-v3/src/session/PdlSerializer.h` (modify: schemaMinor 5 note)
  - `app-v3/src/session/PreparedGraph.cpp` (modify: apply channel at build)
  - `app-v3/src/session/Session.h`, `app-v3/src/session/Session.cpp` (modify: `setNodeMappings`, `setNodeMidiChannel`, channel in `NodeSnapshot`, channel applied on add-plugin commit)
  - `app-v3/tests/unit/session/Patch_test.cpp`, `app-v3/tests/unit/session/PdlSerializer_test.cpp`, `app-v3/tests/unit/session/Session_test.cpp`, `app-v3/tests/unit/session/ControlWiring_test.cpp` (modify)
- **Test scenarios:** `setNodeMappings` replaces only that node's PluginParameter mappings and leaves another node's and PatchSelect mappings alone; forces targets to the node; bumps the mappings revision so ControlWiring re-feeds; `midiChannel` round-trips through `.pdl` and a file without the attribute reads 0; the built graph's BypassableInstance carries the channel; `setNodeMidiChannel` changes it live and in the active patch.
- **Verification:** ctest green; an existing `.pdl` still opens.

### U3. App-level MIDI and OSC mappings

- **Goal:** App-level mappings persist in Preferences and route alongside the active patch's mappings.
- **Requirements:** R5
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/session/AppMappings.h`, `app-v3/src/session/AppMappings.cpp` (create)
  - `app-v3/src/session/Patch.h`, `app-v3/src/session/Patch.cpp` (modify: expose the `<MIDI_MAP>` / `<OSC_MAP>` helpers)
  - `app-v3/src/session/Preferences.h`, `app-v3/src/session/Preferences.cpp` (modify: `appMappings` key)
  - `app-v3/src/session/ControlWiring.h`, `app-v3/src/session/ControlWiring.cpp` (modify: feed patch ∪ app, listen to AppMappings)
  - `app-v3/src/AppContext.h`, `app-v3/src/App.h`, `app-v3/src/App.cpp` (modify: construct, restore, persist on change)
  - `app-v3/src/session/CMakeLists.txt`, `app-v3/tests/CMakeLists.txt` (modify)
  - `app-v3/tests/unit/session/AppMappings_test.cpp` (create)
  - `app-v3/tests/unit/session/ControlWiring_test.cpp`, `app-v3/tests/unit/session/Preferences_test.cpp` (modify)
- **Test scenarios:** app mappings round-trip through XML and Preferences; a change to AppMappings re-feeds the hub without a patch swap; a `patch.next` app mapping keeps working after a patch switch (it is not replaced by the new patch's table); patch mappings and app mappings both route in the same message.
- **Verification:** ctest green.

### U4. Per-plugin Mappings dialog

- **Goal:** The performer edits every mapping of a plugin, and its MIDI channel filter, in one place.
- **Requirements:** R6
- **Dependencies:** U2
- **Files:**
  - `app-v3/src/ui/MappingsDialog.h`, `app-v3/src/ui/MappingsDialog.cpp` (create)
  - `app-v3/src/ui/PluginCard.h`, `app-v3/src/ui/PluginCard.cpp` (modify: Learn button becomes Map)
  - `app-v3/src/ui/SignalCanvas.h`, `app-v3/src/ui/SignalCanvas.cpp` (modify: open the dialog; learn moves into it)
  - the app target's source list in `app-v3/src/CMakeLists.txt` (modify)
- **Approach:** a `DialogWindow` per node (one at a time) with two table sections. MIDI rows: source kind (CC / Note / Program Change / Pitch Bend), channel (omni, 1 to 16), number, target parameter (combo of the plugin's parameter names), min, max, invert, latch, Learn, delete. OSC rows: address, argument index, target parameter, min, max, invert, delete. Header: the node's MIDI channel filter combo. Every edit calls `Session::setNodeMappings` (KTD3). Learn arms `ControlHub::armMidiLearn` for that row and writes the captured source into it. The dialog closes when its node is removed or the patch is swapped.
- **Test scenarios:** UI verified by hand; the data path is covered by U2's tests.
- **Verification:** desktop: map a CC to a parameter with a narrowed range and invert, confirm the knob moves within the range; latch a note to a bypass-like parameter; add an OSC mapping and drive it from TouchOSC; set the channel filter and confirm other channels no longer reach the plugin; save, reopen, and see every mapping and the channel restored.

### U5. App Mappings window and keyboard shortcuts

- **Goal:** Next and previous patch are bound to keys, MIDI and OSC from one window, and the bindings survive restarts.
- **Requirements:** R7, R8
- **Dependencies:** U1, U3
- **Files:**
  - `app-v3/src/ui/AppMappingsWindow.h`, `app-v3/src/ui/AppMappingsWindow.cpp` (create)
  - `app-v3/src/ui/MainWindow.h`, `app-v3/src/ui/MainWindow.cpp` (modify: command manager, key listener, command target)
  - `app-v3/src/ui/TransportBar.cpp` (modify: File menu entry "App Mappings...")
  - `app-v3/src/session/Preferences.h`, `app-v3/src/session/Preferences.cpp` (modify: `keyMappings` key)
  - the app target's source list in `app-v3/src/CMakeLists.txt` (modify)
- **Approach:** `MainWindow` owns an `ApplicationCommandManager` registering the AppCommands table (KTD2) with the KD3 default keys; saved key mappings are restored at construction and written back on every change. The window has a key-binding tab (`KeyMappingEditorComponent`) and a MIDI/OSC tab listing app mappings (command, source, learn, delete) edited through AppMappings (U3).
- **Test scenarios:** UI verified by hand; key persistence is a Preferences round-trip covered in U3's style.
- **Verification:** desktop: Page Down / Page Up step the bank with the canvas focused; rebind to another key and restart, the binding holds; map a foot-controller CC to next patch and step through the bank with audio running.

---

## Verification Contract

- Every unit's ctest suite passes on Linux CI and inside `build:windows`.
- A schemaMinor 4 `.pdl` opens unchanged (R4 back-compat).
- Desktop pass for U4 and U5 as listed.

## Definition of Done

- U1 to U5 merged through `ship-mr`, pipelines green.
- Roadmap R12 and R13 and checklist R8 satisfied.
- The M2 per-card Learn affordance is gone, replaced by the dialog's per-row learn.
