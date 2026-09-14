---
title: 1.0 Completeness Checklist - Plan
type: feat
status: active
date: 2026-07-05
topic: 1-0-completeness-checklist
artifact_contract: ce-unified-plan/v1
artifact_readiness: requirements-only
product_contract_source: ce-brainstorm
---

# 1.0 Completeness Checklist - Plan

## Goal Capsule

- **Objective:** Resolve the roadmap's R17 catch-all by deciding, item by item, what the v1.0.0 bar actually requires — reframing it from "full Pedalboard2 parity" to **live-performer core plus the finish-polish that makes the app read as shipped**, with the deep-feature tail explicitly moved to 1.x.
- **Product authority:** Amends the 1.0 bar in `docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md` (this is the R17 triage that plan calls for). The gap inventory is `docs/reviews/2026-06-16-pedalboard2-parity-review.md`.
- **Open blockers:** none — this is a scoping decision, not new discovery. Every item below is already cataloged in the parity review.

---

## Product Contract

### Summary

A triaged checklist that turns the roadmap's fuzzy "triage the remaining parity items" (R17) into explicit 1.0 decisions. It narrows the 1.0 bar to a shippable core: the live-performer features already sequenced (M2–M4, M6) plus a small set of finish-polish items a public host looks unfinished without — including a shallow undo/redo for structural edits, since editors are judged on that reflex. Deeper subsystems (full undo history, presets, theming, tray, and more — see Scope Boundaries) move to 1.x by decision, not omission.

### Problem Frame

The June parity review found everything missing versus Pedalboard2, and the roadmap sequenced the big features (M0–M8). What was never decided is the *polish tier*: R17 says "remaining parity items are triaged against acceptance criteria; anything cut is recorded as a scope change" — but no triage happened, so a pile of small, high-visibility gaps sits in a vague bucket where they can slip past a release.

Verified current state (2026-07-05): the status bar shows a state dot, state text, version, and an error pill — no CPU meter and no VU meter (the VU processor publishes levels and `BypassableInstance::consumeCpuStats` produces load, but nothing consumes either). There is no window position/size persistence, no in-app node rename (the `customName` field persists but has no editor), no About window, no macOS menu bar, and no plain "Save" (only "Save As", with no in-app display of which file is currently loaded). These are the tells that make a host look unfinished regardless of how strong the engine is.

The decision this doc records: ship 1.0 when the core works *and* the app has no obvious unfinished tells, rather than waiting for every Pedalboard2 feature. This is consistent with the roadmap's beta-first instinct.

### Requirements

Requirement IDs (R1–R13) below are local to this checklist. References to the roadmap's requirements are written `roadmap's R<n>` to avoid confusion with this doc's numbering.

**1.0-blocking — the work this checklist owns**

- R1. Window position and size persist: restored on launch, saved on move/close. No implementation exists today.
- R2. A global CPU-load meter is visible in the status bar, updating live (~10 Hz) from the per-node metering already collected. It surfaces at least a near-overload state (a colour/threshold shift above a defined load) so a performer can react before dropout — a plain readout with no threshold does not meet the bar. Per-node CPU display in the graph is deferred to 1.x.
- R4. A node can be renamed in the app: double-click the card's name label to edit inline; Enter commits, Escape cancels, and an empty name reverts to the plugin's own name. The name persists in the `.pdl` (the `customName` data path already exists; only the editor is missing).
- R5. An About window shows app name, version, the full GPLv3 license text, and attribution to Pedalboard2 / Niall Moody. It is reachable on every 1.0 platform — via the macOS menu bar (R6) and via a File-menu / TransportBar entry on Windows/Linux. This is the in-app affordance only; the distribution-side copyleft obligation is R10.
- R6. The macOS build presents a standard main menu bar (app / File / Edit / Window) wired to existing actions. The Edit menu carries Undo/Redo (R12) — its canonical contents — so it is not a dead menu. Blocking for the macOS release target specifically.
- R10. GPLv3 distribution compliance (owned by M8 / release engineering): every distributed binary ships the full GPLv3 license text, makes Corresponding Source available (repository link or a written offer), and carries modification/date notices. Distinct from R5 — a visible About notice alone does not satisfy copyleft for a binary derived from GPL'd Pedalboard2.
- R11. Unsaved-work protection: quitting, or loading a patch over a modified board, prompts to save first so a live performer cannot silently lose an unsaved board. The parity review lists a "save-and-quit prompt" as Missing; this safety gap sits outside the review's polish tier and is triaged here as blocking. Shares its "is the board modified" tracking with R13 — plan and build together.
- R12. Shallow undo/redo covering structural edits: node add/delete, connect/disconnect, and rename. Scoped to these operations only — not plugin-parameter changes, not the full 50-step history (roadmap AC-NEW-3, which stays in 1.x). This is the decided resolution of the undo/redo question: undo fires on the first accidental delete or misconnection, which is the most common "unfinished tell" in any editor, so it is in 1.0 rather than deferred.
- R13. Quick save writes back to the currently-loaded file without a picker, and the window title (or an equivalent always-visible spot) shows the loaded patch's name plus an unsaved-changes indicator. Today the File menu has only "Save Patch As..." — every save is a fresh file-picker with no in-app way to tell which `.pdl` is currently loaded. Found in the road-to-1.0 gap audit, not the original parity review. R11's save-prompt needs the same "is there a current file, and is it modified" tracking this requires, so plan them together.

**Covered by existing milestones — listed for completeness, not re-scoped here**

- R3. The VU Meter internal node renders a live level meter in its card, consuming the levels its processor already publishes — assigned to M6 / roadmap's R16.
- R8. Per-plugin MIDI channel filter is settable in the app (the engine already supports it via `setMidiChannel`) — folded into the M4 mappings work.

**1.0 nice-to-have — committed dispositions**

- R7. A minimal options surface exposes the OSC listening port (from M2) — the one PB2 option that already exists as a setting and only needs surfacing. The others (plugin-window always-on-top, ignore-pin-names, save-audio-settings-in-`.pdl`) each require net-new backend behavior, not UI-surfacing: `ignore-pin-names` has no pin-name logic in PB3 at all, and `save-audio-in-.pdl` is a `.pdl` format change. They are **deferred to 1.x** unless their backend lands cheaply for another reason. The full settings-modal redesign stays deferred.
- R9. In 1.0 only if it reuses the R1 persistence path with no new UI surface, else 1.x: recent-files list, explicit default patch (Save-as-Default / autoload), and drag-and-drop of a `.pdl` to open. Default disposition if not landed by the 1.0-finish pass: deferred to 1.x.

### Acceptance Examples

- AE1. **Covers R1.** Move and resize the window, quit, relaunch → the window reopens at the same position and size.
- AE2. **Covers R2.** Load a CPU-heavy plugin and play audio → the status-bar CPU meter rises; push load past the defined threshold → the meter shifts to its near-overload state; remove the plugin → it falls and clears. With no audio device, the meter reads zero rather than blank.
- AE3. **Covers R4.** Double-click a node's name, type "My Reverb", press Enter, save, reload → the node shows "My Reverb". Press Escape mid-edit → the name is unchanged.
- AE4. **Covers R5.** Open About from the menu (mac) or File menu (Windows/Linux) → app name, version, the full GPLv3 license text, and Pedalboard2 / Niall Moody attribution are visible.
- AE5. **Covers R10.** Download a distributed build → it is accompanied by the full GPLv3 text and a working path to the Corresponding Source.
- AE6. **Covers R11.** Modify a board, then quit (or open another patch) → a save prompt appears; declining loses the changes by choice, not silently.
- AE7. **Covers R12.** Delete a node, press Undo → the node and its connections return. Connect two ports, press Undo → the cable is removed. Rename a node, press Undo → the old name returns. Redo re-applies each.
- AE8. **Covers R13.** Open a patch, change something, choose Save (not Save As) → the same file is overwritten, no picker appears. The window title shows the patch's name and an unsaved-changes marker while the change is pending, and the marker clears on save.

### Scope Boundaries

**Deferred to 1.x — real subsystems, not finish-polish:**
- User preset system: PresetBar, `.fxp` import/save, cross-plugin preset tree. (Board-level recall is covered in 1.0 by multi-patch banks + Program-Change switching in M3; the deferred piece is per-plugin `.fxp` — a power-user affordance. See Success Criteria for the PresetBar-visibility caveat.)
- Colour-scheme import and editor (theming).
- System tray / start-in-tray.
- Event-log / LogDisplay window (`diagnostics/` stays empty for 1.0).
- Per-node CPU display in the graph UI (the global status-bar meter in R2 covers 1.0).
- Auto-open-mappings-on-connect and loop-next/prev-patch options — deferred to 1.x (may land earlier only if M3/M4 absorbs them trivially).
- The PB2 options without existing backing (plugin-window always-on-top, ignore-pin-names, save-audio-settings-in-`.pdl`) — see R7.
- The full 50-step undo/redo history (roadmap AC-NEW-3). 1.0 ships a shallow undo instead — see R12.

**Outside this checklist's identity:**
- Command-line / OS file-association `.pdl` open — an install/packaging integration, handled with Phase 6 (M8), not app-UI polish.
- Anything already assigned to a milestone (MIDI/OSC control M2, multi-patch + PC-switch M3, mappings UI M4, transport + internal processors M6, signing/packaging M8) is not re-scoped here.

### Success Criteria

- **Reads as shipped:** a first-time user on macOS or Windows sees no obvious "unfinished" tells — the window remembers itself, nodes can be named, meters move (with an overload state), an About window exists, on mac a menu bar (with a working Edit menu) exists, and the currently-loaded patch's identity and save state are always visible (R13).
- **Safe to perform with:** no unrecoverable silent loss of work — a modified board can't vanish to an accidental quit or patch-load (R11), and a structural mistake (wrong node deleted, wrong cable connected) is recoverable via undo (R12). This is a distinct axis from "reads as shipped"; the blocking triage is run against both.
- **R17 closed:** every parity item has a *non-conditional* tier — blocking, nice-to-have with a committed disposition, or deferred — so nothing sits in a "maybe, depending on cost" bucket.
- Deferred subsystems are mostly noticed only on close inspection, with one acknowledged exception a returning Pedalboard2 user will see immediately: the absent PresetBar (M3 patch banks substitute for board-level recall). Undo is no longer in that exception set — R12 covers the structural-edit case that made undo's absence visible.

### Dependencies / Assumptions

- The 1.0-blocking finish pass (R1, R2, R4, R5, R6, R11, R12, R13) is largely independent of M2–M4 and should land as a dedicated milestone **before the public beta**, so the first public impression already reads as shipped. It must not fold into M6 — M6 lands *after* the beta, which would ship the beta without the polish the success criteria require. *(2026-09-13: the beta is now M7, after M6 and hardening; the finish pass shipped before either.)*
- R12 (shallow undo) is the one item in this set genuinely sized like a small subsystem rather than pure UI polish — it needs a command/history model for three operation types, not just a screen. Size it accordingly in finish-pass planning; it is still far smaller than the full 50-step system deferred to 1.x.
- R11 and R13 share a prerequisite — tracking whether the active patch has a known file path and unsaved changes — that doesn't exist in `Session` today. Plan them as one unit of work rather than two.
- R10 (GPLv3 distribution compliance) is owned by M8 / release engineering, not the finish pass — it gates the packaged, distributed build.
- R3 (VU meter UI) and R8 (MIDI channel UI) are already inside M6 and M4 respectively; this doc lists them for completeness and creates no parallel work.
- R6 (macOS menu bar) and the on-app halves of R5/R11 can only be fully verified on desktop builds (macOS/Windows), per the project's on-hardware verification matrix.
- This checklist amends the roadmap's 1.0 bar; the roadmap's R17 should be marked resolved by it.

### Outstanding Questions

**Deferred to planning**

- Whether the minimal options surface (R7, OSC port) reuses the existing Audio Settings dialog area or gets its own small window — an M4-adjacent UI decision.
- The exact CPU near-overload threshold and its visual treatment (R2) — a design detail for the finish-pass plan.
