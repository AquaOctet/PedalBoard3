---
title: "feat: named patch save/load to .pdl (Phase 2 MVP, with plugin state)"
type: feat
status: completed
date: 2026-06-07
origin: docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md
---

# ✨ Named patch save/load to `.pdl` (Phase 2 MVP)

## Overview

The app already builds and runs a multi-plugin effect chain and auto-restores
the single active patch on launch (via `Preferences` XML). It **cannot** save a
named pedalboard to a file, and — critically — **neither save path preserves
plugin settings** (`PatchNode` has no state blob; `replacePatch`'s own comment
defers this to "Phase 2"). This unit makes the app genuinely *workable*: a user
builds a board, saves it as `My Board.pdl`, quits, reopens it later, and gets
the chain **with every plugin's knob settings intact**.

## Scope

**IN (this unit):**
- Add a plugin **state blob** to the patch model; capture on save, restore on load.
- v3 `.pdl` file format: `<PEDALBOARD schemaMajor="3" schemaMinor="0">` wrapping
  the existing node/connection representation + per-node `<STATE>` (master plan §4.5).
- `PdlSerializer`: durable write (temp + atomic rename) and read with a
  `schemaMajor` gate (==3 load; >3 → friendly error; missing/garbage → error).
- UI: **Save…** and **Open…** buttons in `TransportBar` using async `FileChooser`
  (no modal loops — `JUCE_MODAL_LOOPS_PERMITTED` is off), `.pdl` filter.
- Bonus: route the existing `Preferences` auto-restore through the same
  serializer so launch-restore *also* preserves plugin state.

**OUT (deferred — file follow-ups):**
- v2 `.pdl` import, `PluginIdentity` remapper (master plan §4.2.B.1, Phase 2 later unit).
- MIDI/OSC `<MAPPINGS>` + `<APPLICATION_MAPPINGS>` (no control layer yet — Phase 4).
- `<PREFERENCES>`-in-pdl, multi-patch `PatchRegistry`, node `x/y` positions.
- Preserve-unknown-elements round-trip (AC-FS-7) — read the major gate only for now.
- Full fsync/dir-fsync `AtomicFileWriter` shared primitive (use a temp+rename MVP;
  follow-up to harden to the master-plan §A.5 durable-write contract).

## Technical Approach

### Data model — `app-v3/src/session/Patch.h`
- `PatchNode` gains `juce::MemoryBlock state;` (plugin `getStateInformation` blob;
  empty ⇒ nothing to apply).

### Serialization — `app-v3/src/session/Patch.{h,cpp}` (new free functions)
- Extract `patchToXml(const Patch&)` / `patchFromXml(const juce::XmlElement&)` into
  `Patch.cpp` (tag `<PATCH>`), emitting `<STATE encoding="base64">` per node when
  non-empty. Refactor `Preferences.cpp`'s file-local `serialisePatch`/`deserialisePatch`
  to delegate (accept the legacy `ACTIVE_PATCH` tag on read so existing settings
  still load). Centralising means launch-restore gains state preservation for free.

### Restore on build — `app-v3/src/session/PreparedGraph.cpp:70`
- After `host.createInstance(patchNode.desc, …)`, if `patchNode.state.getSize() > 0`,
  `plugin->setStateInformation(patchNode.state.getData(), (int) state.getSize())`
  before `prepareToPlay`. This is the load-time restore hook (used by `replacePatch`).

### Capture on save — `app-v3/src/session/Session.{h,cpp}`
- `Patch getActivePatchWithState() const;` — copy `activePatch_`, and for each plugin
  node call `getHostedPlugin(id)->getStateInformation(node.state)` under `statusMutex_`.
  Message-thread only (matches the rest of the public API).

### `.pdl` file I/O — `app-v3/src/session/PdlSerializer.{h,cpp}` (new)
- `juce::Result writePatch(const Patch&, const juce::File&)` — wrap `patchToXml` in
  `<PEDALBOARD schemaMajor="3" schemaMinor="0" appVersion="3.0.0">`, write to a
  `juce::TemporaryFile`, then `overwriteTargetFileWithTemporary()` (atomic rename).
- `struct LoadResult { std::unique_ptr<Patch> patch; juce::String error; };`
  `LoadResult readPatch(const juce::File&)` — parse XML, require root `<PEDALBOARD>`,
  gate on `schemaMajor` (==3 ok; >3 → "saved by a newer version"; else → error),
  pull the `<PATCH>` child, `patchFromXml`, then `validatePatch`.

### UI — `app-v3/src/ui/TransportBar.{h,cpp}`
- Two buttons mirroring the existing `addButton_`/`audioButton_` pattern. A
  `std::unique_ptr<juce::FileChooser> fileChooser_` member (must outlive the async cb).
- **Save…**: `session.getActivePatchWithState()` → `FileChooser` (save, `*.pdl`,
  default dir = user Documents/Pedalboard 3) → `PdlSerializer::writePatch` →
  result alert.
- **Open…**: `FileChooser` (open, `*.pdl`) → `PdlSerializer::readPatch` → on success,
  **close all editors first** (mirror `onTestToneToggled`'s caller-managed editor
  teardown), then `session.replacePatch(std::move(*patch))`; on error, warning alert.

## Acceptance Criteria

- [ ] `PatchNode.state` added; `buildPreparedGraph` applies it after `createInstance`.
- [ ] `getActivePatchWithState()` captures live plugin state on the message thread.
- [ ] `patchToXml`/`patchFromXml` round-trip nodes, connections, **and** state;
      `Preferences` delegates and still reads legacy `ACTIVE_PATCH` files.
- [ ] `PdlSerializer` writes atomically and reads with a `schemaMajor` gate.
- [ ] TransportBar **Save…**/**Open…** work via async `FileChooser`; Open closes
      editors before `replacePatch`.
- [ ] Unit tests: patch XML round-trip incl. state blob; `.pdl` write→read round-trip;
      schema-major-too-new rejected; legacy `ACTIVE_PATCH` still loads.
- [ ] `clang-format` clean; CI green (Linux + Windows unit + 3 builds + lint).

## Test Scenarios (`app-v3/tests/unit/session/`)
- `PdlSerializer_test.cpp`: round-trip a 2-node patch with non-empty state blobs;
  assert nodes/conns/state survive. Write to a temp `.pdl`, read back, compare.
  Feed a `schemaMajor="4"` doc → expect error. Feed junk → expect error.
- `Patch_test.cpp` (extend) / `Preferences_test.cpp`: `patchFromXml` accepts a
  legacy `<ACTIVE_PATCH>` element; state blob round-trips through base64.

## System-Wide Impact
- **Interaction graph:** Open → FileChooser cb → close editors (SignalCanvas) →
  `Session::replacePatch` → worker `buildPreparedGraph` (now applies state) →
  message-thread `swapPreparedGraph` → ChangeBroadcaster → SignalCanvas rebuilds
  cards → `PreferencesPersister` writes the new active patch (with state).
- **Threading:** `getActivePatchWithState` calls `getStateInformation` on the
  message thread without suspending audio — standard host practice; note as a
  known limitation (a follow-up could suspend processing for large states).
- **State lifecycle:** atomic temp+rename means a crash mid-save never corrupts an
  existing `.pdl`; a failed load leaves the current patch untouched (replacePatch
  only called on success).
- **Editor lifecycle:** Open must close `EditorWindow`s before `replacePatch`
  destroys instances — identical hazard to Test Tone; reuse that path.

## Risks
- **Plugin `setStateInformation` during build** — some plugins are picky; wrap in
  the existing per-plugin error handling so one bad blob fails that node, not the load.
- **`getStateInformation` on the message thread** — accepted for MVP (see Threading).
- **Legacy settings tag** — covered by accepting `ACTIVE_PATCH` on read.

## Sources
- Origin: master plan §4.5 (schema), §4.2.B (`PdlSerializer`, `AtomicFileWriter`),
  §4.3 Phase 2 — see `docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md`.
- Existing serialization to reuse: `app-v3/src/session/Preferences.cpp:129-209`.
- Build/instantiate hook: `app-v3/src/session/PreparedGraph.cpp:44,70,140`.
- Live plugin access: `Session::getHostedPlugin` (`Session.h:146`),
  `BypassableInstance::get/setStateInformation` (`BypassableInstance.h:63-64`).
- Editor-close precedent: `TransportBar::onTestToneToggled` (`TransportBar.cpp:423`).
