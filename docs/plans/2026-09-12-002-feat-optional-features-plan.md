---
title: Optional features — Tempo, Metronome, Looper, MIDI and OSC
type: feat
status: completed
date: 2026-09-12
topic: optional-features
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: the user's requests and decisions of 2026-09-12 (Key Decisions); the UI refresh conventions of plan 004 (#32)
execution: code
---

# Optional features — Tempo, Metronome, Looper, MIDI and OSC - Plan

## Goal Capsule

- **Objective:** Make five features optional, so a board shows only what it uses:
  - Tempo / BPM (the transport), the Metronome, the Looper, MIDI and OSC
  - each is switched on globally in its own Settings section, then per board
  - what's off disappears from the UI and stops
  - the clunky transport strip becomes one compact pill with a tempo popover
- **Product authority:** the user's decisions of 2026-09-12 (Key Decisions).
- **Tracking:** milestone "UI Refresh (pre-beta)", alongside #32.
- **Out of scope:**
  - a metronome click with no Metronome node (the ♩ button drives a Metronome node on the board)
  - per-patch switches (user decision: per board)
  - MIDI Output (1.x)
  - changing what the processors themselves do
- **Stop conditions:** stop and surface if switching a feature off would change or lose anything saved in a board, or if MIDI can reach anything by a path the MIDI gate doesn't cover.

---

## Key Decisions

- **KD1. Five optional features.**
  - **Tempo / BPM:** the transport pill, the Space and Home keys, the transport app commands and MMC.
  - **Metronome:** the Metronome processor and the ♩ button.
  - **Looper:** the Looper processor.
  - **MIDI:** all MIDI input.
  - **OSC:** the OSC server and OSC mappings.
- **KD2. Defaults** (user decision). Tempo, Metronome and Looper start **off**; **MIDI on, OSC off**.
  - An existing install gets the same defaults.
  - For MIDI that matches today.
  - For Tempo and OSC, a board that uses them triggers the question in KD5.
- **KD3. Two levels** (user decision). A feature is **available** when it's enabled in Settings **and** on for the board.
  - **Global:** stored in Preferences as `feature.<name>`.
  - **Board:** stored in the .pdl as attributes on `<BANK>` (`tempo="0"` and so on) and shared by every patch. schemaMinor 7.
  - A board without the attributes (every file today) has every feature on. So enabling a feature globally makes it available on every board until that board turns it off.
  - A board's switch only shows, and only matters, while the feature is on globally.
- **KD4. Per-board switches live in two places** (user decision):
  - **The patch button's right-click menu:** a "This board uses" section, one checkable item per globally enabled feature.
  - **The bottom right of the Mappings panel:** a strip of chips. It sits on the panel's header row when the panel is collapsed.
  - Toggling is a board edit, like renaming a patch, that marks the board unsaved. Bank edits aren't on the undo stack, which covers canvas edits only.
- **KD5. Ask when a board opens** (user decision).
  - **When:** on every path that loads a board (Open, Open Recent, drop, relaunch restore, the Default board), if the board uses features that are off in Settings.
  - **The prompt:** one question lists them all, e.g. "This board uses the Looper and OSC. Turn them on?" with **Turn On** and **Not Now**.
  - **Turn On** enables them globally. **Not Now** leaves them off for now: the board isn't changed and saves as it was.
  - **What "uses" means:**
    - Tempo: a patch with a tempo or time signature other than 120 BPM in 4/4
    - Metronome or Looper: a node of that type
    - MIDI: MIDI mappings
    - OSC: OSC mappings
- **KD6. What "off" does.** Nothing saved in a board ever changes.
  - **Tempo:** the pill hides. Space, Home, the transport app commands and MMC are ignored. Plugins still see the patch's tempo, with the transport stopped.
  - **Metronome and Looper:**
    - They leave Add Plugin and the plugin browser.
    - An existing node's card says "Off in Settings" or "Off for this board".
    - Its processor passes its input through unchanged. That's a runtime flag, not the saved bypass.
  - **MIDI** (user decision: all MIDI):
    - `control::MidiInputManager` unregisters every device callback. That stops mappings, Program Change, MMC and MIDI learn.
    - It's the only place that opens MIDI devices today (`addMidiInputDeviceCallback`), so this is all MIDI. Any future path for notes to plugins sits behind the same gate.
  - **OSC:** `control::OscServer` unbinds, so no port stays open, and OSC learn is unavailable.
- **KD7. A Settings section per feature** (user decision).
  - **The Settings menu becomes:** Audio Devices…, MIDI Settings…, OSC Settings…, Tempo / BPM Settings…, Metronome Settings…, Looper Settings…, a separator, then Test Tone.
  - **Each feature window** opens with its Enable switch; the rest is greyed out while it's off.
  - **Tempo / BPM:**
    - Enable
    - "MIDI Machine Control drives the transport" (moves here from MIDI Settings)
    - the tempo and time signature for new patches
  - **Metronome:** Enable; "Show the ♩ click button"; the level for new Metronomes.
  - **Looper:** Enable; the folder new Loopers save loops into.
  - **MIDI:** Enable at the top, then the device list.
  - **OSC:** Enable at the top, then the port and multicast address.
- **KD8. The transport pill:** `( ▶  ⏮ │ 120 BPM ▾ │ 3.2 │ ♩ )` replaces the six loose controls.
  - **"120 BPM ▾"** opens a popover:
    - −/+ around a field you can drag or type in
    - a big Tap button
    - time-signature chips (2/4, 3/4, 4/4, 5/4, 6/8, 7/8, 12/8, Other…)
    - "Saved with this patch"
  - **♩** shows when the Metronome is available and the patch has a Metronome. It toggles that Metronome's Play; when the Metronome is synced, it arms it with the transport.
  - **In a narrow window** the position hides first, then ♩.
- **KD9. Tidier cards.**
  - **Metronome:** larger beat dots, plus a readout such as "4/4 · synced" or "3/4 · 96 BPM".
  - **Looper:** a REC / PLAY / — status chip beside its buttons.

---

## Implementation Units

### U1. Feature model: global switches, board switches, and "uses"

- **Goal:** The data, and the one question everything asks: is this feature available now, and does this board use it?
- **Requirements:** KD1, KD2, KD3, KD5
- **Dependencies:** none
- **Files:**
  - `app-v3/src/session/Features.h`, `app-v3/src/session/Features.cpp` (create): the `Feature` enum, names, defaults and `boardUses(const PatchBank&, Feature)`
  - `app-v3/src/session/FeatureGate.h`, `app-v3/src/session/FeatureGate.cpp` (create): the global and board switches combined into `isAvailable(Feature)`, broadcasting a change when either changes
  - `app-v3/src/session/Preferences.h`, `app-v3/src/session/Preferences.cpp`: `get/setFeatureEnabled` with the KD2 defaults
  - `app-v3/src/session/PatchBank.h`, `app-v3/src/session/PatchBank.cpp`: the board's switches as `<BANK>` attributes, on when absent
  - `app-v3/src/session/PdlSerializer.h`: schemaMinor 7
  - `app-v3/src/session/BankDocument.*`: `setBoardFeature`, which marks the board unsaved (part of its content signature)
  - `app-v3/src/AppContext.h`, `app-v3/src/App.*`: own the FeatureGate
  - `app-v3/tests/unit/session/Features_test.cpp`, `app-v3/tests/unit/session/FeatureGate_test.cpp` (create)
- **Approach:** Pure logic on the message thread; no UI.
- **Test scenarios:**
  - the defaults on a fresh Preferences
  - available only when on globally and on for the board
  - a board without the attributes has everything on
  - the switches round-trip through the .pdl
  - `boardUses` for each feature
  - a board toggle marks the board unsaved, and switching it back clears that
- **Verification:** ctest green.

### U2. Settings sections, and the MIDI and OSC gates

- **Goal:** Each feature has its own Settings window with its Enable switch, and MIDI and OSC really stop when they're off.
- **Requirements:** KD6 (MIDI, OSC), KD7
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/ui/SettingsWindow.*`: an Enable at the top of MIDI and OSC; MMC moves out
  - `app-v3/src/ui/FeatureSettingsWindows.*` (create): Tempo / BPM, Metronome and Looper
  - `app-v3/src/ui/TransportBar.cpp` (`makeSettingsMenu`), `app-v3/src/ui/MainWindow.cpp` (the macOS menu)
  - `app-v3/src/control/MidiInputManager.*`: `setEnabled`, which registers or unregisters every device callback
  - `app-v3/src/App.cpp`: OSC binds only while available and rebinds on a change
  - `app-v3/tests/unit/control/MidiInputManager_test.cpp`, OSC gating tests
- **Approach:** FeatureGate listeners switch MidiInputManager and OscServer. The Settings windows write through FeatureGate.
- **Test scenarios:**
  - MIDI off: no callbacks registered, and the drain delivers nothing
  - MIDI back on: the enabled devices re-register
  - OSC off: the port is closed; on: bound
  - on a fresh Preferences, MIDI is on and OSC off
- **Verification:** ctest green. By hand: with OSC off no port is open, and with MIDI off a mapped footswitch does nothing.

### U3. Per-board switches, and asking when a board opens

- **Goal:** Switch features per board from the patch's right-click menu and the Mappings panel, and ask when a board needs a feature that's off.
- **Requirements:** KD4, KD5
- **Dependencies:** U1, U2
- **Files:**
  - `app-v3/src/ui/TransportBar.cpp`: right-clicking the patch button opens a menu with the "This board uses" section
  - `app-v3/src/ui/BoardMappingsPanel.*`: the chip strip in the bottom right
  - `app-v3/src/ui/FeatureChips.*` (create): the shared chip strip
  - `app-v3/src/ui/FileActions.cpp`, `app-v3/src/App.cpp` (restore), `app-v3/src/session/DefaultBank.cpp`: the question when a board opens
  - `app-v3/tests/unit/session/Features_test.cpp`: the question's feature list, as a pure function
- **Approach:** The question is an async AlertWindow, with no modal loop. It lists only the features the board uses that are off in Settings.
- **Test scenarios:**
  - the question's list for boards using each feature
  - no question when everything used is on
  - "Not Now" leaves the board unmodified
  - the right-click menu shows only globally enabled features
- **Verification:** ctest green. By hand: open a board with a Looper while the Looper is off.

### U4. Gating what's off, and the transport pill

- **Goal:** What's off disappears and stops, and the transport becomes a compact pill with a tempo popover and the ♩ button.
- **Requirements:** KD6, KD8
- **Dependencies:** U1, U2, U3
- **Files:**
  - `app-v3/src/ui/TransportControls.*`: the pill
  - `app-v3/src/ui/TempoPopover.*` (create)
  - `app-v3/src/ui/TransportBar.cpp`: hides the pill; filters the built-ins in Add Plugin
  - `app-v3/src/ui/PluginBrowser.cpp`: filters the built-ins
  - `app-v3/src/ui/MainWindow.cpp`: Space and Home act only while Tempo is available
  - `app-v3/src/session/TransportCommands.*`: ignores commands and MMC while Tempo is unavailable
  - `app-v3/src/engine/processors/ProcessorBase.*`: a runtime pass-through flag, honoured by the Metronome and the Looper
  - `app-v3/src/ui/ProcessorControls.cpp`: cards say "Off"
  - tests next to each
- **Test scenarios:**
  - transport commands are ignored while Tempo is unavailable
  - the Metronome and Looper are filtered from the built-ins
  - a passed-through Metronome adds nothing, and a passed-through Looper passes its input unchanged and records nothing
  - when ♩ shows
  - how the pill folds in a narrow window
- **Verification:** ctest green. By hand:
  - with all three off, the top bar is clean
  - with Tempo on, the pill appears
  - the popover taps and sets the time signature

### U5. Tidier Metronome and Looper cards

- **Goal:** KD9.
- **Requirements:** KD9
- **Dependencies:** U4
- **Files:** `app-v3/src/ui/ProcessorControls.cpp`; `app-v3/src/ui/CardText.h` (the readout text, as testable helpers)
- **Test scenarios:** the readout text for synced and unsynced Metronomes, and the Looper's status chip.
- **Verification:** ctest green; checked by hand.

---

## Verification Contract

- ctest green on Windows, Linux and macOS, with every CI job listed, the allow_failure ones included.
- Nothing saved in a board changes because a feature is off: a round-trip test covers each feature switched off.
- Every MR lists its hands-on checks.

## Risks

- **OSC off by default changes today's behaviour** for boards with OSC mappings. The question on opening (KD5) covers it.
- **A future MIDI path could bypass the gate.** `MidiInputManager` documents that every MIDI input path must sit behind the MIDI gate.
- **Boards with a tempo other than 120 BPM in 4/4 trigger the question** while Tempo is off. That's intended (KD5), but it's the most common case, so the wording must be quick to dismiss.

## MR plan

1. U1: the feature model, plus this plan
2. U2: the Settings sections and the MIDI and OSC gates
3. U3: the per-board switches and the question on opening
4. U4 and U5: the pill, the gates and the cards
