---
title: Multi-Patch Banks and Program Change Switching (M3) - Plan
type: feat
date: 2026-09-11
topic: multi-patch-banks
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md
execution: code
---

# Multi-Patch Banks and Program Change Switching (M3) - Plan

## Goal Capsule

- **Objective:** A `.pdl` holds a bank of patches instead of exactly one; the performer adds, renames, duplicates, removes, and steps between them in the app, and a MIDI Program Change switches the live patch through the engine's existing atomic graph swap.
- **Product authority:** roadmap M3 (R10, R11) in `docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md`. The completeness checklist (`2026-07-05-001`) reserves current-file tracking, quick save, and the unsaved-changes prompt (its R11/R13) for the finish pass; this plan lays the `PatchBank` they will hang off but does not build them.
- **Execution profile:** headless-testable core (bank model, serializer, switch logic) plus a thin UI strip. Desktop verification with a real controller for R11 per the on-hardware matrix.
- **Stop conditions:** stop and surface if switching patches would require `Session` to preserve plugin instances across a swap (it does not today and that is Phase-2-deferred work); if the schema bump would break loading a schemaMinor 3 single-patch file; or if the Program Change route needs an audio-thread-side change.

---

## Product Contract

### Summary

Today `Session` owns one `Patch`, a `.pdl` wraps one `<PATCH>`, and the only way to change the whole board is Open. Pedalboard2 users perform from a bank: several patches in one file, stepped with next/previous or jumped to with a Program Change. M3 adds a `PatchBank` (an ordered list of patches with one active index) as the app-level document, extends the `.pdl` schema to carry the bank, adds a small patch strip to the transport bar, and routes MIDI Program Change plus the already-modelled `PatchSelect` mapping target into a bank switch that goes through `Session::replacePatch`.

### Problem Frame

Verified 2026-09-11 in the code:

- `session::Patch` is a single topology + mappings + name. `Session` holds `activePatch_` and swaps it whole via `replacePatch(Patch)`, which destroys live instances and rebuilds on the worker thread with an atomic handoff (the "atomic graph swap" R11 names).
- `pdl::writePatch` / `readPatch` serialise exactly one `<PATCH>` under `<PEDALBOARD schemaMajor=3 schemaMinor=3>`.
- The v2 importer reads only the first `<Patch>` of a `Pedalboard2PatchFile` and warns that the rest were dropped.
- `control::MidiTargetKind::PatchSelect` (with `patchIndex`) exists in the mapping model and persists, but `ControlHub` routes it to an unhandled-target sink that `ControlWiring` installs as a no-op.
- `control::MidiSourceKind::ProgramChange` is matched and normalised (number/127) by `MidiRouter`; nothing consumes it at app level.
- There is no notion of "current file" in `Session`, `Preferences`, or the UI; Save is always Save As.

### Key Decisions

- KD1. **`PatchBank` is a new session-level document object; `Session` stays single-patch.** `Session`'s contract (one active patch, one live graph, incremental edits) is stable and heavily tested. A bank is a list of `Patch` values plus an active index; switching means capturing the outgoing patch's state (`getActivePatchWithState`), storing it in the bank, and calling `replacePatch` with the incoming one. No change to the swap machinery, no instance preservation.
- KD2. **Schema bump to minor 4: `<PEDALBOARD>` gains a `<BANK activeIndex=N>` holding N `<PATCH>` children; a bare `<PATCH>` child remains legal and reads as a one-patch bank.** Old files load unchanged; new files written with one patch still write the bank form so readers stay uniform. `readPatch` keeps working for callers that only want the active patch.
- KD3. **Program Change selects by bank index, on every channel, as the default rule; `PatchSelect` mappings are the explicit override.** Pedalboard2 behaviour is "PC n → patch n". Both routes end in one `PatchSwitcher::select(index)`; a `PatchSelect` mapping fires first and, when one matches, the default rule is suppressed for that message so a mapped PC cannot double-switch.
- KD4. **The patch strip lives in the transport bar, not a side panel.** The roadmap's deferred side-panel question is answered minimally: a `[◀] Patch 2/5: Name [▶]` strip plus a menu (Add, Duplicate, Rename, Remove) is enough for R10 and keeps the canvas untouched. A fuller bank list can come with M4's dialogs.
- KD5. **Switching is debounced to the last request while a swap is in flight.** `Session::replacePatch` already coalesces latest-wins; the switcher mirrors that at the bank level (stores the outgoing patch state before each request, tracks the target index) so a fast PC sweep lands on the final program without racing the worker.
- KD6. **The v2 importer imports every `<Patch>` into the bank.** The "only the first was imported" warning goes away; the roadmap's R9 already covers v2 mappings inside each patch.

### Requirements

- R1. A `PatchBank` type holds an ordered list of `Patch` and an active index, with add, duplicate, rename, remove, next, previous, and select operations; removing the last patch leaves one empty "Untitled" patch; indices wrap on next/previous.
- R2. `.pdl` schemaMinor 4 round-trips a bank; schemaMinor ≤3 single-patch files load as a one-patch bank with active index 0; a bank with one patch still loads through `readPatch`.
- R3. The app owns one `PatchBank` document; Open loads the whole bank and activates its active index; Save As writes the whole bank with the live patch's captured state in its slot.
- R4. Switching the active patch captures the outgoing patch (with plugin state) into the bank, then swaps the incoming one through `Session::replacePatch`, closing editors first as Open does today.
- R5. MIDI Program Change n (any channel) selects bank index n when n is in range; out-of-range is ignored. A `PatchSelect` mapping selects its `patchIndex` and suppresses the default rule for that message.
- R6. The transport bar shows the active patch's position and name and offers previous/next plus a menu with Add, Duplicate, Rename, Remove.
- R7. The auto-restore path (`Preferences` active patch) restores the bank, not just one patch.
- R8. Importing a Pedalboard2 file yields one bank patch per v2 `<Patch>`.

### Scope Boundaries

- Not in M3: current-file tracking, quick Save, unsaved-changes prompt, window-title identity (finish pass, checklist R11/R13); app-command mappings for next/previous patch via MIDI/OSC/keys (M4 R13, although the switcher exposes `next()`/`previous()` for it); a bank list panel; per-patch audio settings; PB2's "loop next/prev" option.
- Plugin instances are destroyed and rebuilt on every switch (existing `replacePatch` semantics). Instance preservation across a switch stays deferred.

---

## Key Technical Decisions

- **KTD1 — `PatchBank` is a value type in `session/`, owned by a new `session::BankDocument` that the app constructs next to `Session`.** `BankDocument` is the one object that knows both the bank and the `Session`: it performs select/next/previous by capture-then-replace and broadcasts changes (`juce::ChangeBroadcaster`) for the UI. It is the anchor the finish pass will extend with file identity and dirty tracking.
- **KTD2 — Serializer API grows `writeBank`/`readBank`; `writePatch`/`readPatch` stay as thin wrappers.** `readPatch` returns the active patch of whatever it reads, so `App::restoreActivePatchIfAny` and tests keep compiling while callers migrate.
- **KTD3 — `PatchSwitcher` is a small message-thread object fed from two hooks: `ControlHub`'s unhandled-target sink (for `PatchSelect`) and a raw-MIDI tap from `MidiInputManager`'s drain (for the default Program Change rule).** `ControlWiring` currently installs the no-op sink; it will forward `PatchSelect` to the switcher instead. The raw tap sees each message before the hub processes it, and the switcher records "this message already switched" so the default rule skips a message a mapping consumed.
- **KTD4 — Capture-before-swap uses `Session::getActivePatchWithState()` on the message thread, exactly as Save does.** That call reads plugin state under the status lock and is the only sanctioned way to snapshot knob positions.

---

## Implementation Units

### U1. PatchBank model

- **Goal:** A tested `PatchBank` value type with the bank operations of R1.
- **Requirements:** R1
- **Dependencies:** none
- **Files:**
  - `app-v3/src/session/PatchBank.h` (create)
  - `app-v3/src/session/PatchBank.cpp` (create)
  - `app-v3/src/session/CMakeLists.txt` (add)
  - `app-v3/tests/unit/session/PatchBank_test.cpp` (create)
- **Approach:** `struct PatchBank { std::vector<Patch> patches; int activeIndex{0}; }` with member functions `add(Patch)`, `duplicate(int)`, `rename(int, juce::String)`, `remove(int)`, `select(int)`, `next()`, `previous()`, `active()`, `size()`, `isValidIndex(int)`. `remove` of the last patch resets to one empty patch named "Untitled". Duplicate copies node state blobs and mappings and appends " copy" to the name. `next`/`previous` wrap.
- **Patterns to follow:** `session/Patch.h` (plain struct + free helpers, no JUCE UI types).
- **Test scenarios:** add/duplicate/remove keep `activeIndex` valid; remove-active selects the neighbour; wrap-around; remove-last yields one Untitled; duplicate is a deep copy (mutating the copy leaves the original untouched).
- **Verification:** ctest green on Linux.

### U2. Bank serialisation (.pdl schemaMinor 4) and v2 multi-patch import

- **Goal:** A bank round-trips through `.pdl`; old single-patch files and v2 files load as banks.
- **Requirements:** R2, R8
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/session/PdlSerializer.h`, `app-v3/src/session/PdlSerializer.cpp` (add `writeBank`/`readBank`, bump `kSchemaMinor` to 4, keep `writePatch`/`readPatch` as wrappers)
  - `app-v3/src/session/v2_import/PdlV2Importer.h`, `.cpp` (add `importPedalboard2Bank` returning every patch; keep the single-patch entry point as its first element)
  - `app-v3/tests/unit/session/PdlSerializer_test.cpp`, `app-v3/tests/unit/session/PdlV2Importer_test.cpp` (extend)
- **Approach:** `<PEDALBOARD>` gets a `<BANK activeIndex="n">` child with N `<PATCH>` children produced by `patchToXml`. `readBank` accepts either `<BANK>` or a bare `<PATCH>` (one-patch bank). Every patch is run through `validatePatch`; an invalid patch fails the whole load with its index in the message. `readPatch` = `readBank` then the active patch. The v2 importer loops `<Patch>` siblings and builds a bank; the "multiple patches" warning is removed.
- **Patterns to follow:** existing `writePatch` temp-file + rename atomic write; existing schemaMajor/minor gating and error strings.
- **Test scenarios:** write a 3-patch bank with activeIndex 2, read it back identical; read a hand-written schemaMinor 3 file → one-patch bank, index 0; `readPatch` on a bank file returns the active patch; a bank whose second patch has a dangling connection fails with "patch 2" in the error; v2 file with two `<Patch>` elements imports two bank patches, no warning.
- **Verification:** ctest green; an existing `.pdl` saved by the current build still opens.

### U3. BankDocument and patch switching through Session

- **Goal:** The app owns one bank; selecting a patch captures the outgoing state and swaps the incoming patch through `Session::replacePatch`.
- **Requirements:** R3, R4, R7
- **Dependencies:** U1, U2
- **Files:**
  - `app-v3/src/session/BankDocument.h`, `app-v3/src/session/BankDocument.cpp` (create)
  - `app-v3/src/session/CMakeLists.txt` (add)
  - `app-v3/src/AppContext.h` (add `session::BankDocument& bankDocument`)
  - `app-v3/src/App.h`, `app-v3/src/App.cpp` (construct after `Session`; restore bank on launch)
  - `app-v3/src/session/Preferences.h`, `.cpp` (persist the bank instead of one patch; accept the old single-patch element)
  - `app-v3/src/ui/TransportBar.cpp` (Open/Save go through `BankDocument`; the unsafe-node filtering applies per patch)
  - `app-v3/tests/unit/session/BankDocument_test.cpp` (create)
- **Approach:** `BankDocument` holds `PatchBank` + `Session&`, is a `ChangeBroadcaster`. `select(i)`: if `i == active` no-op; `storeActive()` writes `session.getActivePatchWithState()` into the current slot; set index; `session.replacePatch(copy of patches[i])`; broadcast. `next()/previous()` wrap. `loadBank(PatchBank)` replaces the document and activates its index. `snapshotForSave()` stores the active patch then returns the bank. Coalescing per KD5: a `select` while `Session` is `Building` still stores state (from the last committed patch) and requests the new patch; `replacePatch` latest-wins handles the rest. Editor-close-before-swap stays a UI responsibility: `BankDocument` exposes a `beforeSwap` callback the transport bar binds to `canvas.closeAllEditors()`.
- **Patterns to follow:** `ControlWiring` for a Session-adjacent object with clear thread contract; `TransportBar::openPatch` for the unsafe-node filter and editor-close ordering.
- **Test scenarios:** with the test `BuildFn`, select(1) stores patch 0's state and `Session` ends up hosting patch 1's nodes; select(active) does nothing; next from the last wraps to 0; loadBank activates the stored index; rapid select(1), select(2) lands on 2 with `Session` idle; a change message is broadcast per switch.
- **Verification:** ctest green; on a desktop build, Open a bank file, step patches, Save As, reopen: every patch keeps its knob state.

### U4. PatchSwitcher: Program Change and PatchSelect routing

- **Goal:** A MIDI Program Change or a `PatchSelect` mapping switches the bank.
- **Requirements:** R5
- **Dependencies:** U3
- **Files:**
  - `app-v3/src/session/PatchSwitcher.h`, `app-v3/src/session/PatchSwitcher.cpp` (create)
  - `app-v3/src/session/ControlWiring.h`, `.cpp` (forward `PatchSelect` from the unhandled sink to the switcher instead of the no-op)
  - `app-v3/src/control/MidiInputManager.h`, `.cpp` (add a raw-message tap callback invoked on the drain, before `ControlHub::handleMidi`)
  - `app-v3/src/App.cpp` (construct and wire)
  - `app-v3/tests/unit/session/PatchSwitcher_test.cpp` (create)
- **Approach:** `PatchSwitcher(BankDocument&)` with `onRawMidi(const MidiMessage&)` and `onPatchSelect(int index)`. `onRawMidi` on a Program Change: if `consumedByMapping_` is set for this message sequence, clear it and return; else `select(program)` when in range. `onPatchSelect` selects and marks the current message consumed. Ordering: the tap runs first, so the tap defers the default rule to after the hub call: it records the pending program, the hub runs, then a post-hook applies the default unless a mapping consumed it. Implement as `beginMessage(msg)` / `endMessage()` around the hub call in the drain loop.
- **Patterns to follow:** `ControlWiring`'s sink installation; `MidiInputManager`'s drain timer.
- **Test scenarios:** PC 3 with a 5-patch bank → active 3; PC 9 with 5 patches → unchanged; PC 1 with a `PatchSelect{patchIndex=4}` mapping on PC → active 4, not 1; a non-PC message never switches; switching through the switcher goes through `BankDocument::select` (spy).
- **Verification:** ctest green; desktop: a controller's program buttons step the bank with audio running and no dropout beyond the swap.

### U5. Patch strip in the transport bar

- **Goal:** The performer sees and drives the bank from the UI.
- **Requirements:** R6
- **Dependencies:** U3
- **Files:**
  - `app-v3/src/ui/TransportBar.h`, `app-v3/src/ui/TransportBar.cpp` (add the strip, listen to `BankDocument`)
  - `app-v3/src/ui/MainWindow.cpp` (only if the bar's height or layout needs a tweak)
- **Approach:** `[◀]` `label "2/5 · Name"` `[▶]` plus a `[Patch ▾]` popup with Add (empty patch), Duplicate, Rename (inline `AlertWindow` text prompt), Remove (confirm when the patch has nodes). Previous/next call `BankDocument`. The label updates from the document's change message. Keyboard: none in M3 (M4 maps app commands).
- **Patterns to follow:** existing `openFileMenu` popup and the OSC port control in `TransportBar`.
- **Test scenarios:** UI is verified by hand (headless CI cannot); the underlying operations are covered by U1/U3 tests.
- **Verification:** desktop: add three patches, rename, duplicate, step with the arrows, remove one, Save As, reopen, the strip shows the saved active patch.

---

## Verification Contract

- Every unit's ctest suite passes on Linux CI (`test:linux:unit`).
- A schemaMinor 3 `.pdl` from a current build opens without change (R2 back-compat).
- Desktop pass (macOS or Windows): bank editing, Save/Open round-trip with knob state, PC switching from a hardware controller, `PatchSelect` override.
- No new `apt`/toolchain needs; the factory image is unchanged.

## Definition of Done

- U1–U5 merged to `main` through `ship-mr`, each pipeline green.
- Roadmap R10 and R11 satisfied; the roadmap's "where the patch-bank UI lives" question is closed by KD4.
- The finish pass can attach file identity and dirty tracking to `BankDocument` without reshaping it.
