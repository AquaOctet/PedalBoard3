---
title: Finish Pass Core (file workflow, window memory, undo) - Plan
type: feat
date: 2026-09-11
topic: finish-pass-core
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: docs/plans/2026-07-05-001-feat-1-0-completeness-checklist-plan.md
execution: code
---

# Finish Pass Core (file workflow, window memory, undo) - Plan

## Goal Capsule

- **Objective:** The layout-independent half of the 1.0 finish pass: a bank knows which file it came from and whether it has unsaved changes; File > Save writes back without a picker; the title shows the bank and an unsaved marker; quitting or replacing a modified board asks to save; a default bank serves File > New; the window reopens where it was left; and structural edits can be undone.
- **Product authority:** completeness checklist R1, R11, R12, R13 and R9 (default patch) in `docs/plans/2026-07-05-001-feat-1-0-completeness-checklist-plan.md`. Pedalboard2's `MainPanel.cpp` (FileNew / FileSaveAsDefault / FileResetDefault, `default.pdl` in the app-data folder) is the behavioural reference for the default bank.
- **Tracking:** the requirement-level issues already on milestone "1.0 Finish Pass (pre-beta)" are the tracking issues; units close them directly instead of filing duplicate U-issues: #26 (U1-U3), #27 (U4), #29 (U5), #21 (U6), #28 (U7-U8).
- **Out of scope here (layout-dependent, waits for the UI refresh design, #32):** CPU meter #22, inline rename #23, About #24, macOS menu bar #25. Recent files #30 and drag-and-drop open #31 reuse this plan's file identity and can follow it.
- **Stop conditions:** stop and surface if tracking plugin-parameter edits needs an audio-thread allocation or lock, or if restoring a deleted node needs a whole-patch rebuild.

---

## Key Decisions

- KD1. **"Modified" means anything a save would change except the active patch index and card positions.** Structural and bank edits (patches, names, nodes, bypass, MIDI channel, connections, mappings) count, compared against a signature taken at load/save. Card positions are excluded: the canvas auto-lays-out cards of older files on open, which would mark every freshly opened file modified; positions are still saved. Plugin-parameter edits made on the message thread (a plugin's editor) also count; parameters moved by the performer's own MIDI/OSC mappings do not, or every performance would end in a save prompt. Switching patches does not count.
- KD7. **Auto-restore and explicit files agree.** The app keeps persisting the live bank for crash-safe auto-restore, so the current file path and the modified flag persist with it. A relaunch (or a crash) restores the bank, its file and its unsaved marker. "Don't Save" at quit reverts the persisted session to the last saved content (the file on disk, or the default / empty bank for an untitled board) so discarded edits do not come back at the next launch.
- KD2. **Launch keeps restoring the last bank.** Pedalboard2 loaded `default.pdl` at every startup; Pedalboard 3 already restores the last session, which is what a performer mid-set wants. The default bank is used by File > New and at launch only when there is nothing to restore.
- KD3. **Default bank = `default.pdl` in the Pedalboard 3 app-data folder**, like Pedalboard2: Save as Default writes the current bank there, Reset Default deletes it, New loads it (or one empty patch).
- KD4. **Every "replace the board" path goes through one save-before-discard prompt** (Save / Don't Save / Cancel): quit, Open, New, and any later drag-and-drop or recent-file open.
- KD5. **Undo is per-operation with exact inverses, not snapshots.** Add node, remove node, connect, disconnect, rename. Removing a node captures its full `PatchNode` (with plugin state) and the pre-removal connection set; undo re-creates it under the same `NodeId` (so its mappings reattach) through the existing worker instantiation path and restores those exact connections. History is bounded (32 steps) and cleared on every whole-patch swap (switching patches, loading a bank). Parameter changes stay out (1.x full history).
- KD6. **App commands carry the new shortcuts:** `file.save` (Ctrl/Cmd+S), `edit.undo` (Ctrl/Cmd+Z), `edit.redo` (Ctrl+Y and Ctrl/Cmd+Shift+Z) join `session::AppCommands`, so they are also MIDI/OSC-mappable and the macOS Edit menu (#25) reuses them.

---

## Implementation Units

### U1. Bank file identity and structural modified tracking

- **Goal:** BankDocument knows its current file and whether its content differs from what was last loaded or saved.
- **Requirements:** checklist R13, R11 (shared prerequisite); KD1
- **Dependencies:** none
- **Files:**
  - `app-v3/src/session/BankDocument.h`, `app-v3/src/session/BankDocument.cpp` (modify: current file, modified state, signature)
  - `app-v3/tests/unit/session/BankDocument_test.cpp` (modify)
- **Approach:** `currentFile()`, `loadBank(bank, file)` / `markSaved(file)` set the file and take a content signature (bank structure plus each slot's topology, names, positions, channel filter and mappings; not plugin state, not `activeIndex`). The document listens to Session; on an Idle change whose live patch matches the active slot, it refreshes the active slot's signature part. `isModified()` compares against the saved signature or a sticky parameter-edit flag (U2). Broadcasts when the modified state flips.
- **Test scenarios:** load then no edits: not modified; add/remove node, connect, bypass, rename patch, add patch: modified; switch patches: not modified; save then modified clears; a failed switch does not flip modified.
- **Verification:** ctest green.

### U2. Plugin-parameter edits mark the board modified

- **Goal:** A knob turned in a plugin's own editor makes the board modified; a parameter moved by a mapped MIDI/OSC control does not.
- **Requirements:** KD1
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/session/Session.h`, `app-v3/src/session/Session.cpp` (modify: per-node `juce::AudioProcessorListener`, edit counter)
  - `app-v3/src/session/ControlParameterApplier.cpp` (modify: control-input scope)
  - `app-v3/tests/unit/session/Session_test.cpp`, `app-v3/tests/unit/session/BankDocument_test.cpp` (modify)
- **Approach:** Session attaches a listener to each hosted processor on commit and detaches before destruction; `audioProcessorParameterChanged` (any thread) bumps an atomic counter unless a message-thread "control input" scope is active, which ControlParameterApplier sets around its `setValueNotifyingHost`. `audioProcessorChanged` with non-parameter state changes counts too. BankDocument treats a counter advance since the last save as modified. RT-safe: one relaxed atomic increment.
- **Test scenarios:** setting a parameter directly bumps the counter; applying through ControlHub does not; the counter survives nothing but a save (markSaved resets the baseline).
- **Verification:** ctest green.

### U3. File menu: New, Save, Save As; title with the bank and an unsaved marker

- **Goal:** File > Save writes back to the current file with no picker; the title always shows what is loaded and whether it is saved.
- **Requirements:** checklist R13 (AE8); KD6
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/ui/TransportBar.h`, `app-v3/src/ui/TransportBar.cpp` (modify: File menu New / Open / Save / Save As, set current file on open and save)
  - `app-v3/src/ui/MainWindow.cpp` (modify: title from BankDocument, `file.save` command)
  - `app-v3/src/session/AppCommands.h`, `app-v3/src/session/AppCommands.cpp` (modify: `file.save`)
  - `app-v3/tests/unit/session/AppCommands_test.cpp` (modify)
- **Approach:** Save with a current file writes `writeBank(snapshotForSave(), file)` and `markSaved`; without one it falls through to Save As. The title reads `<name> - Pedalboard 3` with a leading `*` while modified; `<name>` is the file stem or "Untitled". `file.save` is an app command the UI dispatches (not a session action), so the registry entry carries a UI-only flag.
- **Test scenarios:** registry has `file.save` with a unique JUCE id. UI verified by hand.
- **Verification:** AE8 on a desktop build.

### U4. Save-before-discard prompt

- **Goal:** Quit, Open and New ask to save a modified board first.
- **Requirements:** checklist R11 (AE6); KD4
- **Dependencies:** U1, U3
- **Files:**
  - `app-v3/src/ui/SavePrompt.h`, `app-v3/src/ui/SavePrompt.cpp` (create: async Save / Don't Save / Cancel, then continue)
  - `app-v3/src/App.h`, `app-v3/src/App.cpp` (modify: `systemRequestedQuit` asks MainWindow first)
  - `app-v3/src/ui/MainWindow.h`, `app-v3/src/ui/MainWindow.cpp` (modify: quit request)
  - `app-v3/src/ui/TransportBar.cpp` (modify: Open / New go through the prompt)
- **Approach:** one helper `confirmDiscard(ctx, owner, onProceed)`: not modified runs `onProceed` at once; otherwise an owned AlertWindow; Save runs U3's save (Save As when there is no file) and proceeds only if it succeeded; Cancel does nothing. Callbacks guard with SafePointer.
- **Test scenarios:** UI verified by hand.
- **Verification:** AE6 on a desktop build: modify, quit: prompt; Don't Save quits; Cancel stays; Save writes then quits.

### U5. Default bank: Save as Default, Reset Default, New

- **Goal:** File > New starts from the performer's default bank; Save as Default and Reset Default manage it.
- **Requirements:** checklist R9 (default patch), issue #29; KD2, KD3
- **Dependencies:** U1, U4
- **Files:**
  - `app-v3/src/session/Preferences.h`, `app-v3/src/session/Preferences.cpp` (modify: `defaultBankFile()` next to the settings file)
  - `app-v3/src/ui/TransportBar.cpp` (modify: Save as Default, Reset Default; New loads the default)
  - `app-v3/src/App.cpp` (modify: launch falls back to the default when there is nothing to restore)
  - `app-v3/tests/unit/session/Preferences_test.cpp` (modify)
- **Approach:** the file is `default.pdl` beside `Pedalboard 3.settings`. New (behind U4's prompt) loads it with no current file (title "Untitled"), or one empty patch. Save as Default writes the current bank there without changing the current file. Reset Default deletes it after a confirmation.
- **Test scenarios:** `defaultBankFile()` sits in the settings folder; a written default reads back as a bank.
- **Verification:** desktop: save as default, New: that bank loads as Untitled; Reset Default, New: one empty patch; with no last session to restore, launch opens the default.

### U6. Remember window position and size

- **Goal:** The main window reopens where it was left.
- **Requirements:** checklist R1 (AE1)
- **Dependencies:** none
- **Files:**
  - `app-v3/src/ui/MainWindow.cpp` (modify: save on close and on shutdown, restore at construction)
  - `app-v3/src/session/Preferences.h`, `app-v3/src/session/Preferences.cpp` (modify: `windowState` key)
  - `app-v3/tests/unit/session/Preferences_test.cpp` (modify)
- **Approach:** `getWindowStateAsString()` into Preferences when the window closes (MainWindow destructor, before teardown); `restoreWindowStateFromString()` at construction, then check the restored bounds intersect a connected display and centre at the default size when they do not.
- **Test scenarios:** Preferences round-trip of the state string. Window behaviour by hand.
- **Verification:** AE1 on a desktop build; unplug a secondary display between runs: the window comes back on the main one.

### U7. Edit history and exact node restore

- **Goal:** A bounded undo/redo history of structural edits with exact inverses.
- **Requirements:** checklist R12 (AE7); KD5
- **Dependencies:** none
- **Files:**
  - `app-v3/src/session/EditHistory.h`, `app-v3/src/session/EditHistory.cpp` (create)
  - `app-v3/src/session/Session.h`, `app-v3/src/session/Session.cpp` (modify: `restoreNode(PatchNode, connections)` through the add worker, `setConnections`, `getNodeWithState(NodeId)`, the id of the last committed add)
  - `app-v3/src/session/CMakeLists.txt`, `app-v3/tests/CMakeLists.txt` (modify)
  - `app-v3/tests/unit/session/EditHistory_test.cpp` (create)
- **Approach:** EditHistory wraps the five undoable edits (`addPlugin`, `addInternalNode`, `removeNode`, `addConnection`/`removeConnection`, `setNodeCustomName`) and records a command per edit. Remove captures `getNodeWithState(id)` and the connection set first; undo calls `restoreNode`, which instantiates via the add worker (desc or internal type, state blob, bypass, channel, position) and commits under the captured id with the captured connections. An add is recorded when its commit lands (the new id is the one not present before); undoing it removes that node and restores the pre-add connections. The history clears on a patch-generation change. `canUndo`/`canRedo` plus a change broadcast for menus.
- **Test scenarios:** remove then undo restores the node, its id, name, bypass and every original connection (not the bridge); add then undo; connect / disconnect / rename round-trips; redo re-applies each; a new edit clears redo; a patch swap clears both stacks; depth is capped at 32.
- **Verification:** ctest green.

### U8. Undo / redo in the app

- **Goal:** Ctrl+Z / Ctrl+Y (Cmd+Z / Cmd+Shift+Z) undo and redo canvas edits.
- **Requirements:** checklist R12 (AE7); KD6
- **Dependencies:** U7
- **Files:**
  - `app-v3/src/ui/SignalCanvas.cpp` (modify: edits go through EditHistory)
  - `app-v3/src/ui/TransportBar.cpp` (modify: Add Plugin goes through EditHistory)
  - `app-v3/src/ui/MainWindow.cpp` (modify: `edit.undo` / `edit.redo` commands and default keys)
  - `app-v3/src/session/AppCommands.h`, `app-v3/src/session/AppCommands.cpp` (modify)
  - `app-v3/src/AppContext.h`, `app-v3/src/App.h`, `app-v3/src/App.cpp` (modify: own EditHistory)
- **Approach:** App constructs EditHistory after BankDocument; AppContext carries it; the canvas and transport bar call it instead of Session for the five edits. Undo/redo are unavailable while Test Tone is on or Session is building.
- **Test scenarios:** registry entries; UI by hand.
- **Verification:** AE7 on a desktop build.

---

## Verification Contract

- ctest green on Linux CI and inside `build:windows` for U1, U2, U5, U6, U7.
- Desktop pass: AE1, AE6, AE7, AE8, and the default-bank flow.

## Delivery

Three MRs through `ship-mr`: file workflow (U1-U5, closes #26, #27, #29), window memory (U6, closes #21), undo (U7-U8, closes #28).
