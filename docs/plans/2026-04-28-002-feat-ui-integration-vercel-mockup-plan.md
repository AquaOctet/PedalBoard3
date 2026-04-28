---
title: "feat: UI integration with Vercel mockup design (Phase 1.D.3 Unit 6 → Phase 3 staging)"
type: feat
status: active
date: 2026-04-28
origin: docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md
---

# feat: UI integration with the Vercel mockup design

## Enhancement Summary

**Deepened on:** 2026-04-28
**Sections enhanced:** 9 (Critical Deltas, Visual Style, Technical Approach, Implementation Units, Acceptance Criteria, Risk Analysis, System-Wide Impact, Performance, Documentation Plan)
**Research agents used:** framework-docs-researcher (JUCE 8), repo-research-analyst (existing patterns), best-practices-researcher (DAW UI conventions), architecture-strategist, code-simplicity-reviewer, performance-oracle, pattern-recognition-specialist, spec-flow-analyzer.

### Key improvements

1. **Add-plugin entry-point regression caught** — current `RootComponent` ComboBox is the only `Session::addPlugin` caller; MainWindow rewrite drops it without replacement. New AC + Unit-6 task added.
2. **Listener strategy upgraded from rebuild to NodeId-diff** — visual state for cards (bypass toggles, reorder, name changes) survives without flicker.
3. **EditorWindow lifetime is caller-managed, synchronously before any Session mutator that destroys instances** — matches the existing pattern at `MainWindow.cpp:288, 300`. Listener-side cleanup is too late: `Session::removeNode` (lines 305–307) and `Session::commitLoadPatch` (lines 595–598) destroy `BypassableInstance`s synchronously *before* `bumpVersionAndNotify` fires the async change message, so by the time the listener runs, the editor's `AudioProcessor` reference is already dangling.
4. **JUCE 8 LookAndFeel override list expanded** — V4 ColourScheme + 12 additional override targets needed to keep `PluginListComponent`, `AudioDeviceSelectorComponent`, scrollbars, sliders, popup menus, table headers, and toggle buttons on-theme.
5. **Cable rendering moved off the paint hot path** — `computeCables` runs in `layoutCards`, cached as two `juce::Path` members (audio + MIDI), one `strokePath` per material instead of N.
6. **Convention compliance fixed** — namespace is `pedalboard3::ui` (not `pb3::ui`), file naming drops underscores (`Pb3LookAndFeel.h`), token identifiers settle on camelCase.
7. **CMake plumbing made explicit** — every new `.cpp` lands in both `add_library(pb3_app_core …)` AND `pb3_apply_strict_warnings(pb3_app_core …)`. Tests link `pb3_app_core` only if UI-instantiating.
8. **Mockup-fidelity gaps documented** — Master Mute, MIDI/OSC indicators, plugin-type accent borders, bypass-cable dashed style. Each is now either explicitly in scope or explicitly deferred.

### New considerations discovered

- `Session::commitLoadPatch` and `removeNode` destroy `BypassableInstance`s **synchronously inline** (Session.cpp:295–310, 595–598), then fire `sendChangeMessage` (async). The listener cannot safely close editors after the fact — the AudioProcessor they reference is already gone. **Mitigation:** caller-side close-before-call, matching `MainWindow.cpp:288, 300, 132`.
- `Session::getActivePatch()` is a heavyweight copy under `statusMutex_`; canvas reads `getNodeSnapshots()` for paint-driving and only touches `getActivePatch()` for connection-driven cable derivation.
- `pb3_smoke_tests` does not currently link `pb3_app_core` or `juce_gui_extra` (intentional: tests stay headless). UI-instantiating tests need a separate target or an `OfflineRender`-style headless fixture.
- `getNodeIdsInOrder()` returns an empty vector for an empty patch (Session.cpp:374–382). The `graphIO` sentinel is a *caller-side projection* (`MainWindow.cpp:222–223`), not a sentinel returned from the API. The canvas reads `getNodeSnapshots()` (also empty for empty) and renders the Input → Output endpoint cards directly.
- `juce::Font(Typeface::Ptr, height)` constructor is being deprecated in favour of `juce::FontOptions` in JUCE 8; the older form still compiles but new code should use `FontOptions`.
- Mockup uses **single cable per stereo connection** (Bitwig-style), not two parallel L/R cables.

---

## Critical Deltas From Deepen Research

These are the must-fix items the deepen pass surfaced. Each has been folded back into the relevant section below; this list is the consolidated audit trail.

### Must fix before Unit 6 (blocks chain growth)

- **C1 — Add-plugin entry point.** MainWindow rewrite removes `RootComponent`, which owns the only existing `Session::addPlugin` call site (the Load button + plugin chooser ComboBox). Without a replacement, the user cannot grow the chain after Unit 6 ships. **Fix:** TransportBar gains a `[+ Add Plugin]` button that opens a `juce::PopupMenu` populated from `ctx_.pluginHost.getKnownPluginList().getTypes()`. Disabled while `Session::getState() != Idle`. Tracked as **AC-UI-N**.

- **C2 — `addPlugin` failure surfacing.** Failed Add increments `lastError_` and broadcasts a change, but `getNodeSnapshots()` returns the same vector → no card appears, no error visible. **Fix:** StatusBar reads `Session::getLastError()` after every state→Idle transition; clears on next successful op. Tracked as **AC-UI-O**.

- **C3 — `addPlugin` queue overflow silent.** `Session::addPlugin` drops requests once `addQueue_.size() >= kAddQueueMax (16)`. Same fix as C2 closes it (overflow writes `lastError_`).

### Must fix before Unit 5 ships (correctness)

- **D1 — Listener strategy: diff, don't rebuild.** Naive teardown on every change message destroys all `PluginCard`s on a single bypass toggle, causing flicker. **Fix:** `changeListenerCallback` builds `oldIds` and `newIds` sets keyed by `NodeId` (stable across `moveNode`/`setNodeBypass`). Adds new cards, removes departed cards, calls `setSnapshot` on persisting cards, reorders the vector. *(Note: this diff handles **cards** only — see D1b for the editor-window lifecycle, which is caller-managed not listener-managed.)*

- **D1b — EditorWindow lifecycle: caller-managed, synchronous, before the Session mutator.** Session destroys plugin instances synchronously inline (`removeNode` at Session.cpp:305–310; `commitLoadPatch` at 595–598), and only *then* fires the async change message. By the time the listener runs, the editor's `AudioProcessor` is dangling. **Fix:** every UI handler that triggers a Session mutator that may destroy nodes must `editors_.erase(id)` (or `editors_.clear()`) **first, synchronously**, then call the Session method. Concretely:
  - `PluginCard::onRemove` → `editors_.erase(id); session.removeNode(id);`
  - Test Tone toggle → `editors_.clear(); session.clearPatch(); …`
  - Future patches-sidebar load → `editors_.clear(); session.replacePatch(p);`
  - `addPlugin`, `moveNode`, `setNodeBypass`, `setNodeCustomName` — do **not** destroy nodes; no preemptive close needed. (This matches the existing pattern at `MainWindow.cpp:288, 300, 132`.)

- **D2 — Cable derivation off the paint path.** Plan's pseudocode calls `computeCables()` inside `paint()`, which acquires `statusMutex_` and allocates per frame. **Fix:** `rebuildCables()` runs from `layoutCards()`; results cached as two `juce::Path` members (`audioCablesPath_`, `midiCablesPath_`); `paint()` makes two `strokePath` calls regardless of N.

- **D3 — `computeCables` reads `patch.connections`, not chain adjacency.** Hardcoding `cards_[i] → cards_[i+1]` works for linear chains but Phase 3 must rip it out for arbitrary topology. Reading the existing `activePatch_.connections` (already maintained by `recomputeLinearConnections` in Session) gives the same v0 visual with zero Phase 3 rewrite. Maps NodeId→cardIndex once per layout, derives cables from connection records.

### Must fix in Unit 1 (theming completeness)

- **E1 — LookAndFeel override list incomplete.** AC-UI-A ("no JUCE-default-grey remains") fails with the plan's 8-method override list. **Fix:** add `drawLabel`, `drawScrollbar` + scrollbar geometry methods, `drawLinearSlider*` + slider colour/text-box methods, `drawTextEditorOutline`/`fillTextEditorBackground`, `drawComboBoxTextWhenNothingSelected`/`getComboBoxFont`, `drawPopupMenuBackgroundWithOptions`/`drawPopupMenuItemWithOptions`/`getPopupMenuFont`, `drawCallOutBoxBackground`, `drawTreeviewPlusMinusBox`, `createDocumentWindowButton`/`positionDocumentWindowButtons`, `drawTableHeaderBackground`/`drawTableHeaderColumn`, `drawToggleButton`/`drawTickBox`. **Cheaper alternative**: drive most defaults via `LookAndFeel_V4::ColourScheme` + per-class `setColour(ColourId, …)` mapping; override `paint*` only where bespoke shape is needed.

- **E2 — Hardcoded `dialogBackgroundColour` in MainWindow.** `MainWindow.cpp:347, 390` hardcode `0x2A2A2A` for Audio Settings + Plugin List dialogs, bypassing `LookAndFeel_PB3`. Fix on extraction: pull from `tokens::bgPopover`.

- **E3 — `App::shutdown` must `setDefaultLookAndFeel(nullptr)` before the LookAndFeel singleton dies**, otherwise `juce::Desktop` holds a dangling pointer.

### Must fix in Unit 2 (font path)

- **F1 — JUCE 8 font construction.** `juce::Font(Typeface::Ptr, height)` is being deprecated. Use `juce::Font(juce::FontOptions(typeface).withHeight(14.0f))`.

- **F2 — Cache `Typeface::Ptr` once.** `Typeface::createSystemTypefaceFor` parses TTF on every call; load once in `LookAndFeel_PB3` ctor, return refcounted `Ptr` from `Fonts::createInter()` thereafter.

- **F3 — Resources path.** Plan says top-level `Resources/Fonts/`; codebase has no `Resources/` dir. Place at `app-v3/resources/fonts/` to keep app-scoped, matching `app-v3/` convention.

### Convention violations (catch in code review, all units)

- **N1 — Namespace.** Use `pedalboard3::ui::*` (not `pb3::ui::*`). `pb3_*` is the CMake target prefix only.
- **N2 — File names.** Drop underscores; rename `LookAndFeel_PB3.{h,cpp}` → `Pb3LookAndFeel.{h,cpp}`. Codebase precedent: `AudioGraphRuntime`, `BypassableInstance`.
- **N3 — Token identifiers.** Pick one form and align: `pedalboard3::ui::tokens::bgWindow` (camelCase). The `bg.window` form is documentation of the source CSS variable, not a C++ identifier.
- **N4 — Constants.** Drop `k`-prefix on file-scope constants (`cardWidth = 170.0f`) or scope as private statics on `SignalCanvas`. `kAddQueueMax` (private static class member) is the codebase precedent for the `k`-prefix.
- **N5 — `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR`** on every owning class: `SignalCanvas`, `PluginCard`, `TransportBar`, `StatusBar`, `Pb3LookAndFeel`, `EditorWindow`.
- **N6 — Header doc-blocks.** Codebase precedent runs 15–60 lines (see `Session.h`). Drop the "≤30 lines" cap in the Documentation Plan.

### Mockup-fidelity gaps (decide-and-document)

- **G1 — Master Mute.** Mockup TransportBar has a Master Mute button. Plan's TransportBar doesn't. **Decision needed:** add to AC-UI-N or move to Non-Goals. Recommendation: defer (no engine API for master-mute today; would need an output-stage gain stage).
- **G2 — Card button always-visible vs hover-reveal.** Mockup hides Remove behind hover (`opacity-0 group-hover:opacity-100`); plan keeps all five buttons always-visible for stage readability. Document as intentional plan-vs-mockup divergence (live-performance framing).
- **G3 — Plugin-type accent border.** Mockup shows a left-edge colour band per `effect`/`instrument`/`utility`. JUCE `PluginDescription` has no plugin-type field; `juce::PluginDescription::isInstrument` is the only signal. **Decision:** ship a binary `instrument` (warm) vs `effect` (teal) accent; "utility" tier waits for a richer Patch metadata field.
- **G4 — Bypass-cable visual.** Mockup dashes + dims cables when source is bypassed. Plan's `paint` doesn't read bypass state. **Fix in Unit 5:** `Cable` struct carries `sourceBypassed` flag; `paint` toggles `PathStrokeType::dashed` + reduced opacity when set.
- **G5 — Cable thickness 2.0f vs DAW convention 2.5–3.0f.** Bump to 2.5f for stage readability.
- **G6 — Stereo cables: one or two?** Mockup looks single-cable (Bitwig style). Plan's pseudocode implies two parallel cables (L + R). **Decide:** single cable, thicker, with a subtle dual-line motif (matches mockup + reduces visual noise at scale).
- **G7 — MIDI cable colour collides with input-port `--primary` teal.** Pick a distinct hue for MIDI (amber `--chart-3` recommended).

### Documented but acknowledged limitations

- **L1 — DPI/Retina:** bezier `strokeWidth` and port radii in pixels. JUCE's transform handles scaling; constants are fine but should pass through `g.getInternalContext().getPhysicalPixelScaleFactor()` for crispness.
- **L2 — Keyboard navigation, Tab order, Esc-to-close-EditorWindow:** add Esc-close (1-line `KeyListener`); defer Tab-order to Phase 3.
- **L3 — VoiceOver / `juce::AccessibilityHandler`:** Phase 3.

---

## Overview

The user has prototyped the eventual Pedalboard 3 UI in `vercel-ui-demo/`
(Next.js 16 + React 19 + Tailwind v4 + shadcn/ui). The mockup is
*comprehensive* — it covers TransportBar (top), PatchesPanel (left
sidebar), SignalFlowCanvas (center, drag-to-connect), MidiMappingPanel
(bottom), PluginBrowser (right sidebar), StatusBar (bottom). That's the
full master-plan §4.2.D vision in one design.

This plan is **a decision document first, an implementation plan
second**. The question on the table: *how do we sequence the JUCE-side
UI port given the existing master-plan phasing puts canvas in Phase 3
and MIDI/OSC mappings in Phase 4?*

The current state (after 1.D.3 Units 1–5 partial):

- Multi-plugin chain works in the engine.
- A transitional single-row UI drives `Session::addPlugin` /
  `Session::clearPatch` / first-node `setNodeBypass`.
- The user can build a chain but **can't visualise it** — that's their
  primary pain ("can't tell what audio is going through what").

This plan resolves the "now or later" question on the mockup and writes
the implementation units for whichever path is chosen.

## Problem Statement

### What we know

1. **Engine side is ahead of UI side.** Phase 1.D.3 engine work is done
   (Patch + Session multi-node API + buildPreparedGraph + Preferences
   round-trip + PluginSlot retired). Tests green on Linux + macOS CI.
2. **The transitional UI ships only enough to drive Session.** Single
   row → first-node operations only. No chain visualisation.
3. **The Vercel mockup is the design target.** It maps cleanly to JUCE
   (PluginNode card → custom Component, SignalFlowCanvas → Component
   with `paint`+`mouseDown`/`Drag`/`Up`, PatchesPanel/PluginBrowser →
   `juce::ListBox`, MidiMappingPanel → tabs).
4. **Master plan §4.3 schedules the canvas in Phase 3 (4–6 weeks).**
   That estimate predates the mockup. Having a concrete design target
   reduces UX-design risk; the *implementation* effort is largely
   unchanged.
5. **Some sidebar/panel features depend on Phase 2 / Phase 4
   backends.** PatchesPanel needs PatchRegistry (Phase 2). MIDI panel
   needs MidiRouter + MidiMapping (Phase 4). Without those, those
   panels show mock data or are read-only.

### What the user actually needs (from this iteration)

- **Visual signal flow** — see at a glance what plugins are in the
  chain and how audio routes through them. Solves "can't tell what's
  going through what."
- **Per-node controls** — bypass / edit / remove / reorder a specific
  plugin (currently only first node).
- **Optionally, the new aesthetic** — dark blue-gray + teal accent
  matching the mockup so the app stops looking like a 1990s
  control-panel.

The user explicitly does *not* need (right now):
- Drag-to-connect arbitrary topology (linear chain is enough).
- PatchesPanel save/load (no PatchRegistry yet).
- MIDI mapping UI (no MidiRouter yet).
- Plugin browser sidebar (existing PluginListComponent dialog is fine).

## Decision Matrix — Five Paths

> Sized in working-days for one engineer at the project's current
> iterate-on-CI cadence (push, watch pipeline, fix forward). Test +
> CI roundtrip already amortised.

### Path α — minimal UX fix (no aesthetic change)

Finish Phase 1.D.3 Unit 6 *exactly as originally planned*: vertical
list of rows, one per node, each row carrying Bypass + Edit + Move↑ +
Move↓ + Remove. No visual style change; UI stays JUCE-default-grey.

**Effort:** 3–5 days.
**User-visible win:** chain is visible row-by-row; user can act on any
plugin.
**Cost:** UI looks nothing like the mockup. Phase 3 will rip out the
row-list and replace it with the canvas; this work is throwaway except
for the per-node-NodeId routing patterns that survive.
**Phase 3 leftover:** entire visual port (LookAndFeel, transport bar,
canvas with bezier cables, sidebars).

### Path β — visual style only (no canvas)

Land Path α as-is for chain UI. Separately port the **visual style**
from the mockup: a `pb3::ui::ColourScheme` with the oklch tokens, a
`pb3::ui::LookAndFeel_PB3` subclass of `juce::LookAndFeel_V4`, font
choice (Inter as Geist substitute — Geist license requires
distribution attention), button shape conventions. Apply to every
existing dialog (Audio Settings, Plugin List).

**Effort:** 5–7 days total (α + 2–3 days style work).
**User-visible win:** the app looks like the mockup. Chain is still a
boring vertical list, but it's a *correctly-coloured* boring vertical
list.
**Cost:** still no canvas. User's visual-flow pain is half-solved (rows
in the right colours).
**Phase 3 leftover:** canvas + sidebars + transport-bar metering +
status bar.

### Path γ — visual style + read-only signal canvas (recommended)

Replace the chain row-list with a **canvas-style component**: cards
laid out left-to-right (auto-positioned, since the chain is linear),
input/output ports drawn on the cards, bezier cables drawn between
ports. Cards have per-card Bypass / Edit / Move↑ / Move↓ / Remove
buttons (same controls as α, mounted on the card instead of in a row).
**Read-only at first**: not draggable, ports not click-to-connect.
Adopt the visual style from β at the same time. Add a **thin
TransportBar** at the top (existing Audio Settings / Test Tone /
Plugin List buttons restyled into the new bar) and a **thin
StatusBar** at the bottom (chain length + Session state).

**Effort:** 10–12 days (~2–2.5 weeks).
**User-visible win:** the app looks like the mockup *and* the chain is
visualised with cables. The user's "can't tell what's going through
what" pain is fully solved.
**Cost:** still no PatchesPanel, no PluginBrowser sidebar, no MIDI
mapping panel. The canvas is read-only — Phase 3 grows it into the
full editor.
**Phase 3 leftover:** drag-to-rearrange + port-click connections +
lasso-select + zoom + the three sidebars/panels.

### Path δ — full Phase 3 UI port now

Port the entire mockup to JUCE. PatchesPanel shows a "No saved patches
yet — patch save/load ships in Phase 2" placeholder; MidiMappingPanel
shows a "MIDI mapping ships in Phase 4" placeholder; PluginBrowser is
real (we already have a KnownPluginList). Drag-to-rearrange,
port-click connections, lasso-select, zoom all included. AudioSettings
dialog restyled.

**Effort:** 25–35 days (~5–7 weeks). Matches master-plan §4.3
Phase-3 estimate.
**User-visible win:** the entire mockup, fully interactive (where the
backend supports it).
**Cost:** ships before Phase 2 / Phase 4 backends. Two of the four
panels are placeholders. The shipping cadence dies — this is one
~6-week slice instead of the project's current
push-fix-push-fix rhythm.
**Phase 3 leftover:** none. Phase 4 gates the MIDI panel becoming live.

### Path ε — Vercel demo as Tauri/Electron front-end (rejected)

Keep the JUCE app as a headless audio back-end and ship the UI as the
Next.js demo wired through Tauri or Electron. **Rejected** —
explicitly considered and rejected by the master plan §"Alternative
Approaches" (Alt A1, Tauri + Rust + clack-host). The rejection
reasoning still applies: JUCE provides hardened VST3/AU/CLAP/LV2
*hosting*, the entire reason this codebase exists; switching to
Tauri+IPC means rebuilding the format layer that JUCE gives us for
free. Master plan §1 commits to JUCE/CMake; Phase 6 release pipeline
(installers, signing, auto-update) is built around it.

## Recommended Path: γ

Path γ is the right choice. The reasoning:

1. **It solves the user's stated pain in full.** Bezier cables on a
   left-to-right card layout *is* the visual-flow win. Cards-with-text-
   arrows (Path α) doesn't get there.
2. **It adopts the mockup's aesthetic without committing to its full
   surface area.** Three of the mockup's six panels (PatchesPanel,
   MidiMappingPanel, PluginBrowser-sidebar) need backends that don't
   exist yet. Shipping placeholder UI for them (Path δ) creates fake
   expectations.
3. **The canvas component is reusable.** Path γ's read-only canvas IS
   the foundation Phase 3 grows into the full editor — drag handlers
   bolt onto the same `paint` + hit-test code; port-click connection
   logic plugs into the existing card port-rendering.
4. **It preserves the project's iterate-on-CI cadence.** 2–2.5 weeks
   is several iterate-cycles (commit, push, watch CI, fix forward),
   not one big-bang slice.
5. **It's symmetric with the engine work.** Phase 1.D.3 engine
   supports arbitrary topology; the mockup's canvas can grow into a
   real graph editor without engine changes. Path γ delivers the
   linear-chain projection of that, which matches the engine's
   current UI exposure.

The two rejected options (α, β) are too cheap to be satisfying; the
two heavy options (δ, ε) commit to scope ahead of the engine's
readiness or against the project's architectural direction.

> **The remainder of this plan describes the implementation of Path γ.**
> If the user prefers α, β, or δ, the relevant implementation units
> from this plan adapt with documented deltas (see "Adaptation Notes
> by Path" at the bottom of the plan). The user's choice is the
> single decision required before implementation begins.

## Proposed Solution (Path γ)

Build the JUCE UI as a **three-zone vertical layout**:

```
┌────────────────────────────────────────────────────────────────────┐
│ TransportBar — Audio Settings · Test Tone · Plugin List · Status   │
├────────────────────────────────────────────────────────────────────┤
│                                                                    │
│   SignalCanvas (read-only):                                        │
│                                                                    │
│   ╭─[Input]─╮     ╭─[Compressor]─╮     ╭─[Reverb]──╮  ╭─[Output]─╮ │
│   │   ●─ ─ ─ ─ ─ ─●              ●─ ─ ─●           ●──●          │ │
│   │   ●─ ─ ─ ─ ─ ─●  bypass edit ●─ ─ ─●  bypass…  ●──●          │ │
│   ╰────────╯     ╰─────[× ↑ ↓ ]──╯     ╰───[× ↑ ↓]─╯  ╰─────────╯ │
│                                                                    │
│                                                                    │
├────────────────────────────────────────────────────────────────────┤
│ StatusBar — Chain length: 2 · State: Idle · Audio: 48k / 256       │
└────────────────────────────────────────────────────────────────────┘
```

Cards auto-layout left-to-right starting from a virtual "Input"
endpoint card on the left, ending at a virtual "Output" endpoint card
on the right, with the user's plugin nodes in between. Cables are
quadratic-bezier curves between port circles. Cards expose per-node
controls (bypass / edit / move / remove) inline, not in a hover menu —
visible at a glance for live performance.

Plugin browser stays as the existing `juce::PluginListComponent`
dialog (opened via the TransportBar button). Audio settings stays as
the existing `juce::AudioDeviceSelectorComponent` dialog. Both get
restyled by `LookAndFeel_PB3` automatically.

## Technical Approach

### Architecture

**New `ui/` files** (all in `app-v3/src/ui/`):

- `ColourScheme.h` / `.cpp` — token definitions matching mockup oklch
  values, converted to `juce::Colour` (sRGB). 19 v2-parity tokens
  preserved per master plan §4.2.D + extras for the new aesthetic.
- `LookAndFeel_PB3.h` / `.cpp` — subclass of
  `juce::LookAndFeel_V4`. Overrides button drawing, label drawing,
  background fill, slider styling, dialog title-bar drawing. Drives
  ColourScheme.
- `TransportBar.h` / `.cpp` — small `juce::Component`. Buttons:
  Audio Settings, Test Tone, Plugin List. Right-aligned text:
  current device name + sample-rate + buffer-size. (CPU/latency
  meters from the mockup wait for `MetricsRegistry` — Phase 1.B+;
  show "—" until then.)
- `StatusBar.h` / `.cpp` — small `juce::Component`. Left-aligned:
  "Chain: N plugin(s)" + Session state ("Idle" / "Loading…" /
  "Swapping…"). Right-aligned: build version string.
- `SignalCanvas.h` / `.cpp` — the read-only canvas. Renders cards +
  cables. Listens to `Session` change-messages to re-layout. Calls
  `Session::removeNode` / `moveNode` / `setNodeBypass` from card
  controls. Opens an `EditorWindow` per node on Edit click.
- `PluginCard.h` / `.cpp` — one card per `PatchNode`. Owns its own
  bounds, paint, mouse handlers (clicks on the per-card buttons —
  not yet drag-to-rearrange). Delegates to `SignalCanvas` for
  global ops (move, remove, edit-window-open).
- `EditorWindow.h` / `.cpp` — extracted from current `MainWindow.cpp`
  inner class. Per-NodeId, multiple can be open at once, each
  closes independently.

**Modified files:**

- `MainWindow.h` / `.cpp` — strips the inline `RootComponent` +
  `EditorWindow` mess. Becomes a thin `juce::DocumentWindow` shell
  hosting a `SignalCanvasView` (TransportBar + SignalCanvas +
  StatusBar in a vertical FlexBox).
- `App.cpp` — sets `juce::LookAndFeel::setDefaultLookAndFeel(...)`
  to a singleton `LookAndFeel_PB3` early in `initialise`.

**Unchanged:**

- All of `engine/`, `session/` (no engine changes).
- `Preferences`, `PreferencesPersister`.
- All test files; canvas adds new UI-level tests.

### Visual style (from `vercel-ui-demo/app/globals.css`)

Direct token map (mockup oklch → `juce::Colour::fromFloatRGBA` after
oklch→sRGB conversion done at compile time):

| Token (mockup)                  | JUCE-side name              | sRGB approx |
|---------------------------------|-----------------------------|-------------|
| `--background`     `oklch(0.12 0.01 250)` | `bg.window`         | #1A1B22 |
| `--card`           `oklch(0.16 0.01 250)` | `bg.card`           | #232530 |
| `--popover`        `oklch(0.14 0.01 250)` | `bg.popover`        | #1F2128 |
| `--primary`        `oklch(0.72 0.19 160)` | `accent.primary`    | #1FCC9A (teal) |
| `--secondary`      `oklch(0.22 0.01 250)` | `bg.secondary`      | #2E303B |
| `--muted-foreground` `oklch(0.60 0 0)`    | `fg.muted`          | #8B8B8B |
| `--foreground`     `oklch(0.95 0 0)`      | `fg.primary`        | #F2F2F2 |
| `--border`         `oklch(0.28 0.01 250)` | `border.default`    | #3E404B |
| `--destructive`    `oklch(0.60 0.20 25)`  | `accent.destructive` | #D14040 |
| `--chart-3`        `oklch(0.75 0.18 80)`  | `accent.warning`    | #D4B040 (output ports) |
| `--sidebar`        `oklch(0.10 0.01 250)` | `bg.sidebar`        | #16171E |

(oklch → sRGB conversion done once via a small Python script committed
under `scripts/colour-mapping.py` so future token additions are
mechanical.)

Font: **Inter** as the closest-license-clean Geist substitute. Geist
ships under SIL Open Font License 1.1 — redistributable but
fontconfig fragility on Linux is a known issue; Inter is broadly
available and visually adjacent. JUCE bundles via
`juce::Typeface::createSystemTypefaceFor` from a binary blob in the
binary-data subdir (`Resources/Fonts/Inter-*.ttf`).

### SignalCanvas rendering

Layout algorithm (linear chain auto-layout):

```
constexpr float kCardWidth = 170.0f;
constexpr float kCardHeight = 140.0f;
constexpr float kCardSpacing = 100.0f;   // gap for cable
constexpr float kCanvasMarginY = 48.0f;
constexpr float kCanvasMarginX = 24.0f;

void SignalCanvas::layoutCards() {
    auto snaps = ctx_.session.getNodeSnapshots();
    const auto centerY = getHeight() / 2.0f;
    float x = kCanvasMarginX;

    // Virtual input endpoint card (just renders Output ports).
    inputCard_->setBounds(x, centerY - kCardHeight / 2,
                          kCardWidth, kCardHeight);
    x += kCardWidth + kCardSpacing;

    cards_.resize(snaps.size());
    for (size_t i = 0; i < snaps.size(); ++i) {
        if (!cards_[i]) cards_[i] = std::make_unique<PluginCard>(...);
        cards_[i]->setBounds(x, centerY - kCardHeight / 2,
                             kCardWidth, kCardHeight);
        cards_[i]->setSnapshot(snaps[i]);
        x += kCardWidth + kCardSpacing;
    }

    outputCard_->setBounds(x, centerY - kCardHeight / 2,
                           kCardWidth, kCardHeight);

    // Canvas auto-grows horizontally if needed (parent handles
    // horizontal scroll via juce::Viewport).
    setSize((int)(x + kCardWidth + kCanvasMarginX), getHeight());

    repaint();
}
```

Cables drawn in `paint`:

```cpp
void SignalCanvas::paint(juce::Graphics& g) {
    g.fillAll(LookAndFeelTokens::bgWindow());

    // Background grid (subtle, matches mockup's signal-flow-canvas)
    drawGrid(g);

    // Bezier cables between cards.
    for (const auto& cable : computeCables()) {
        juce::Path path;
        path.startNewSubPath(cable.fromX, cable.fromY);
        const float cx = (cable.fromX + cable.toX) / 2.0f;
        path.cubicTo(cx, cable.fromY, cx, cable.toY,
                     cable.toX, cable.toY);
        g.setColour(cable.isMidi ? tokens::midiCable
                                 : tokens::audioCable);
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }
}
```

`computeCables` derives cable endpoints from `session.getActivePatch()`
via `PatchConnection` records: stereo-L + stereo-R audio cables
between adjacent cards, plus a MIDI cable in a different colour.

Read-only contract: ports are drawn but `mouseDown` on a port is a
no-op for now. Phase 3 wires them to start/end a connection drag.

### Per-card controls

```
┌──────────────────────────────────┐ <- card 170×140
│ ≡  Compressor       ⚙  ⏻  ✕   │ <- title row: drag-grip (disabled),
│                                  │    name, edit, bypass, remove
│ ● In L           Out L ●         │ <- ports row 1
│ ● In R           Out R ●         │ <- ports row 2
│ ● MIDI           MIDI ●          │ <- ports row 3
│                                  │
│           [↑]  [↓]                │ <- move buttons row
└──────────────────────────────────┘
```

Card draws its own background (rounded rect, accent border on the
left edge by plugin type), ports as small filled circles.
- Bypass (⏻) → `Session::setNodeBypass(id, !current)`.
- Edit (⚙) → opens an `EditorWindow` keyed by NodeId (multiple
  windows can be open at once; map cleaned on `removeNode` /
  `replacePatch`).
- Remove (✕) → `Session::removeNode(id)`.
- Move ↑ / ↓ → `Session::moveNode(id, currentIndex ± 1)`.

When `Session` fires `sendChangeMessage()`, the canvas's
`changeListenerCallback` re-reads `getNodeSnapshots()` and either
rebuilds card vector (count changed) or updates existing cards in
place (reorder / bypass-toggle).

### TransportBar + StatusBar

`TransportBar` (height 48):
- Left: Pedalboard 3 logo + name (text only — no asset for v0).
- Centre: device-info pill (current driver / sample rate / buffer
  size, queried from `engine_.getDeviceManager()`).
- Right: Audio Settings · Test Tone · Plugin List buttons.
- CPU + latency meters: placeholder dashes for now (Phase 1.B+
  brings the metrics registry).

`StatusBar` (height 32):
- Left: "Chain: N plugin(s)" + Session state with a small status
  dot (● Idle / ● Loading… / ● Swapping…).
- Right: build version string + "v3.0.0-pre".

### EditorWindow extraction

Pull `MainWindow.cpp`'s inner `EditorWindow` class (currently inside
`RootComponent`'s private section at lines **399–425**, an
unnamed-namespace inner class) out into its own translation unit.
Same shape — same constructor signature
(`title, juce::AudioProcessorEditor*, std::function<void()> onClose`).
After the rewrite, `MainWindow` no longer knows about plugin editors
at all; `SignalCanvas` owns the
`std::unordered_map<NodeId, std::unique_ptr<EditorWindow>>` and is
responsible for closing entries synchronously before any
`Session::removeNode` / `clearPatch` / `replacePatch` call (per D1b
and AC-UI-P).

### Implementation Units

> 5 units (Unit 3 folded into Unit 5 per the simplicity review),
> sized to land in 2–2.5 working weeks. Phase A (Units 1–2) are
> independent; Phase B (Units 4–6) depend on A. Unit numbering
> preserved for cross-reference with the original 6-unit draft;
> Unit 3 below is retained as a fold-marker only.

#### Unit 1 — `ColourScheme` + `Pb3LookAndFeel`

- **Files:** `app-v3/src/ui/ColourScheme.{h,cpp}`,
  `app-v3/src/ui/Pb3LookAndFeel.{h,cpp}`, `App.cpp`
  (`setDefaultLookAndFeel` in `initialise`,
  `setDefaultLookAndFeel(nullptr)` in `shutdown`),
  `app-v3/src/CMakeLists.txt` (add `.cpp` to both target sources
  and `pb3_apply_strict_warnings`). *(`scripts/colour-mapping.py`
  removed per simplicity review — hex constants committed
  directly with comment showing source oklch tuple.)*
- **Approach:**
  1. `pedalboard3::ui::tokens` namespace with `inline juce::Colour`
     constants for every entry in the colour table below.
  2. `Pb3LookAndFeel` (subclass of `juce::LookAndFeel_V4`) does
     two things in its ctor: (a) sets a `LookAndFeel_V4::ColourScheme`
     wired to our tokens (`windowBackground`, `widgetBackground`,
     `menuBackground`, `outline`, `defaultText`, `defaultFill`,
     `highlightedText`, `highlightedFill`, `menuText`); (b) calls
     `setColour(ClassName::ColourId, tokens::…)` for each JUCE class
     we surface (`Slider::thumbColourId`, `ScrollBar::thumbColourId`,
     `PluginListComponent::columnHeaderColourId`,
     `ComboBox::backgroundColourId`,
     `PopupMenu::backgroundColourId`, `Label::textColourId`,
     `TextEditor::backgroundColourId`, `ToggleButton::tickColourId`,
     etc.). Most defaults derive from the ColourScheme without
     bespoke `paint*` overrides.
  3. Override `paint*` only where shape changes are needed:
     `drawButtonBackground` (rounded-rect + accent border),
     `drawDocumentWindowTitleBar` (custom title chrome),
     `createDocumentWindowButton`/`positionDocumentWindowButtons`
     (close/min/max glyphs), `drawAlertBox`, `drawTickBox`. Fonts
     return cached `juce::Font(juce::FontOptions(typeface).withHeight(...))`
     by value (Typeface::Ptr loaded once in ctor — see Unit 2).
  4. `App::initialise` instantiates a static `Pb3LookAndFeel` and
     calls `juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_)`.
     `App::shutdown` calls `setDefaultLookAndFeel(nullptr)` *before*
     `mainWindow_.reset()` to avoid `juce::Desktop` holding a
     dangling pointer.
  5. Replace hardcoded `dialogBackgroundColour = juce::Colour::fromRGB(0x2A, 0x2A, 0x2A)`
     at `MainWindow.cpp:347, 390` with `tokens::bgPopover` (or remove
     so the LookAndFeel drives — check JUCE's `DialogWindow` default).
- **Verification:** Linux + macOS smoke. Open every dialog
  (Audio Settings, Plugin List, internal AlertWindows). No
  default-grey visible; no scrollbar/slider/popup-menu chrome
  leaks the V4 default. Cite the JUCE class doc URL
  (https://docs.juce.com/master/classjuce_1_1LookAndFeel__V4.html)
  in the source-file doc-block as a method-list reference.

> **Research insights (Unit 1):**
> - **JUCE 8 reference:** https://docs.juce.com/master/classjuce_1_1LookAndFeel__V4.html — full member list at https://docs.juce.com/master/classjuce_1_1LookAndFeel__V4-members.html.
> - **`LookAndFeel_V4::ColourScheme`** carries 9 named slots that drive most JUCE defaults; setting them once + per-class `setColour(ColourId, …)` for the long tail is materially cheaper than overriding 30 `draw*` methods.
> - **`PluginListComponent` is a `TableListBox`** — needs `drawTableHeaderBackground` + `drawTableHeaderColumn` overrides if we want themed column headers.
> - **`AudioDeviceSelectorComponent` contains sliders** — needs `drawLinearSlider*` and slider colour IDs themed.
> - **`createDocumentWindowButton` / `positionDocumentWindowButtons`** must be overridden if we draw a custom title bar; otherwise the close/min/max buttons retain V4 defaults.
> - **British spelling** (`ColourScheme`, not `ColorScheme`) matches `OutputSanitiser`/`initialise` codebase precedent.

#### Unit 2 — Inter font resource + binary build wiring

- **Files:** `app-v3/resources/fonts/Inter-Regular.ttf`,
  `app-v3/resources/fonts/Inter-Medium.ttf` (Latin subset),
  `app-v3/src/CMakeLists.txt` (`juce_add_binary_data` +
  `target_link_libraries(pb3_app_core PRIVATE pb3_app_resources)`),
  `app-v3/src/ui/Fonts.{h,cpp}`.
- **Approach:** generate a `pb3_app_resources` `BinaryData` target
  via `juce_add_binary_data`. Expose `Fonts::interTypeface()`
  returning a refcounted `juce::Typeface::Ptr` cached in a
  function-local static (load TTF once). `Pb3LookAndFeel` calls
  `juce::Font(juce::FontOptions(Fonts::interTypeface()).withHeight(14.0f))`
  in `getLabelFont`/`getTextEditorFont`/`getPopupMenuFont`.
- **CMake snippet** (verified against JUCE 8 GuiApp example):

```cmake
juce_add_binary_data(pb3_app_resources
    HEADER_NAME "BinaryData.h"
    NAMESPACE pedalboard3::BinaryData
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/fonts/Inter-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/fonts/Inter-Medium.ttf)

target_link_libraries(pb3_app_core PRIVATE pb3_app_resources)
```

- **Verification:** font renders correctly on Linux + macOS smoke.
  Subset + filesize check (Latin-only ~150 KB per weight). License
  attribution in `docs/THIRD_PARTY.md` (new file; format spec:
  one section per dependency with name, version, license SPDX id,
  source URL, and the SIL OFL 1.1 notice text verbatim).

> **Research insights (Unit 2):**
> - **`juce::Font(Typeface::Ptr, height)` is being deprecated in JUCE 8** in favour of `juce::FontOptions`. New code: `juce::Font(juce::FontOptions(typeface).withHeight(14.0f))` — see https://docs.juce.com/master/classjuce_1_1Font.html.
> - **Cache `Typeface::Ptr` once.** `Typeface::createSystemTypefaceFor` parses TTF tables on every call; load once + return refcounted shared `Ptr`.
> - **`juce_add_binary_data` SOURCES are mmap'd** by JUCE's binary-data wrapper; no per-startup copy cost.
> - **`pedalboard3::BinaryData` namespace** keeps fonts off the `juce::*` and `pb3::*` collisions; consistent with codebase namespace nesting.

#### Unit 3 — *(folded into Unit 5)*

Per the simplicity review: standalone `EditorWindow` extraction is
a 30-minute lift-and-shift that only earns its keep once `SignalCanvas`
(Unit 5) owns the `editors_` map. The lift-and-shift now lives inside
Unit 5's prep step. The actual class lives in `RootComponent`'s
private section at `MainWindow.cpp:399–425` (an unnamed-namespace
inner class, not on `MainWindow` directly), with constructor
`EditorWindow(title, editor, onClose)`. Lift verbatim, add
`JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR`, project-style
header doc-block, into `app-v3/src/ui/EditorWindow.{h,cpp}`.

#### Unit 4 — `PluginCard` Component

- **Files:** `ui/PluginCard.{h,cpp}`.
- **Approach:** custom `juce::Component`. `paint` draws card
  background (rounded rect, accent border on left edge by
  plugin-type colour), title text, ports (audio L/R + MIDI),
  per-card buttons. `mouseDown` dispatches to button handlers
  delegated through a callback set (`onBypass`, `onEdit`,
  `onMoveUp`, `onMoveDown`, `onRemove`). NodeId stored as a
  member; updated via `setSnapshot(NodeSnapshot)` on Session
  change.
- **Verification:** unit test asserts card paints correctly with
  a fake snapshot (visual via JUCE OfflineRender if practical,
  else tested via integration in Unit 5).

#### Unit 5 — `SignalCanvas` Component (with `EditorWindow` extraction)

- **Files:** `app-v3/src/ui/SignalCanvas.{h,cpp}`,
  `app-v3/src/ui/EditorWindow.{h,cpp}` (folded from old Unit 3).
- **Approach:** owns:
  - `std::vector<std::unique_ptr<PluginCard>>` cards_ — real chain.
  - `std::unique_ptr<EndpointCard> inputCard_, outputCard_` —
    virtual endpoints.
  - `std::unordered_map<NodeId, std::unique_ptr<EditorWindow>> editors_`.
  - `juce::Path audioCablesPath_, midiCablesPath_` — cached;
    rebuilt only by `rebuildCables()`, called from `layoutCards()`.
  - `std::unordered_map<session::NodeId, size_t> idToIndex_` —
    derived during `layoutCards()`; used by `rebuildCables()` to
    iterate `activePatch_.connections` and look up endpoint
    cards (Phase 3-future-proofing per D3).

- **Listener strategy (NodeId-diff for cards, per D1):**

```cpp
void changeListenerCallback(juce::ChangeBroadcaster*) override {
    auto snaps = ctx_.session.getNodeSnapshots();

    // Build cards_ in snap order, reusing existing PluginCards
    // by NodeId so visual state survives reorder/bypass. Departed
    // NodeIds' cards (and their entry in editors_) are dropped here;
    // editors for those NodeIds were already closed *synchronously
    // by the UI handler that triggered the Session mutator* — see
    // D1b. (The map cleanup here just removes orphaned entries that
    // somehow survived; in normal flow it's a no-op.)
    std::vector<std::unique_ptr<PluginCard>> next;
    next.reserve(snaps.size());
    std::unordered_map<session::NodeId, size_t> oldIndex;
    for (size_t i = 0; i < cards_.size(); ++i)
        oldIndex[cards_[i]->getNodeId()] = i;

    std::unordered_set<session::NodeId> newIds;
    for (const auto& s : snaps) {
        newIds.insert(s.id);
        if (auto it = oldIndex.find(s.id); it != oldIndex.end()) {
            cards_[it->second]->setSnapshot(s);
            next.push_back(std::move(cards_[it->second]));
        } else {
            next.push_back(makeCard(s));
        }
    }
    cards_ = std::move(next);

    // Defensive cleanup: any editors whose NodeId vanished get
    // erased here. In normal flow the UI handler already did this
    // before the Session call (D1b); this only catches restore-
    // on-launch and other code paths that bypassed the UI.
    for (auto it = editors_.begin(); it != editors_.end(); ) {
        if (newIds.count(it->first) == 0) it = editors_.erase(it);
        else ++it;
    }

    // Surface lastError if state just settled to Idle.
    if (ctx_.session.getState() == session::Session::State::Idle)
        statusBar_.setError(ctx_.session.getLastError());

    layoutCards();   // rebuilds cables; calls repaint()
}
```

- **Card-button → Session ordering for destructive ops (per D1b):**

```cpp
// PluginCard delegates Remove via callback; SignalCanvas wires it:
auto onRemove = [this](session::NodeId id) {
    editors_.erase(id);                // synchronous, BEFORE Session
    ctx_.session.removeNode(id);       // destroys instance inline
};

// Test Tone (TransportBar handler in Unit 6) and any future
// replacePatch UI:
auto onTestToneEnable = [this] {
    editors_.clear();                  // close every editor first
    ctx_.session.clearPatch();
    // ... build tone graph ...
};
```

- **Cable derivation (per D2 + D3):**
  - `rebuildCables()` runs in `layoutCards()`, NOT in `paint()`.
  - Reads `ctx_.session.getActivePatch().connections` (one
    heavyweight copy per layout — paid once per change message,
    not once per paint).
  - For each `PatchConnection`, looks up source/dest cards via
    `idToIndex_`, computes port pixel positions, appends
    `cubicTo` segment to `audioCablesPath_` (audio links) or
    `midiCablesPath_` (MIDI links).
  - Single cable per stereo connection (Bitwig-style; mockup
    fidelity per G6); rendered with thicker stroke (2.5f) for
    stage readability (per G5).
  - Cables for nodes whose source is `bypassed=true` use
    `PathStrokeType::dashed` + 0.4 alpha (mockup G4).

- **Paint (hot path):**

```cpp
void paint(juce::Graphics& g) override {
    g.fillAll(tokens::bgWindow);
    drawGrid(g);
    g.setColour(tokens::cableAudio);
    g.strokePath(audioCablesPath_, juce::PathStrokeType(2.5f));
    g.setColour(tokens::cableMidi);
    g.strokePath(midiCablesPath_, juce::PathStrokeType(2.0f,
                 juce::PathStrokeType::curved,
                 juce::PathStrokeType::rounded));
}
```

  Two `strokePath` calls regardless of N. No mutex acquisition.
  No allocation on the paint path.

- **`resized()` override (per AC-UI-T):**

```cpp
void resized() override { layoutCards(); }
```

- **`layoutCards()` empty-patch handling:** `getNodeSnapshots()`
  returns an empty vector when the patch is empty (Session.cpp:374–
  398); `cards_` is empty too. Render only `inputCard_` → cable →
  `outputCard_`. (No `graphIO` sentinel handling needed — that
  sentinel is a caller-side projection in `MainWindow.cpp:222–223`,
  not something the snapshot API returns.)

- **Verification:** macOS smoke — load 3 plugins, see three
  cards + cables. Bypass a card, cable visually dashes. Move↑
  on card 2; cards swap; editor windows survive. Click Test
  Tone with editors open; ASan run confirms no use-after-free.

> **Research insights (Unit 5):**
> - **`Session::getActivePatch()` is heavyweight** under `statusMutex_` (`Session.h:141` doc); call it once per `layoutCards()`, not once per `paint()`.
> - **`Session` extends `juce::ChangeBroadcaster`** and uses `sendChangeMessage` (async). It also destroys `BypassableInstance`s synchronously inside `removeNode`/`commitLoadPatch` *before* firing the change message. Editor-window lifecycle is therefore caller-managed (synchronous, before the Session mutator), not listener-managed — see D1b.
> - **`PluginCard::setSnapshot()` is the cheap path** for bypass toggles and customName changes — no card destruction; just a paint invalidation on that one card.
> - **`Component::setBufferedToImage(true)` on cards** caches the rendered card image; bypass toggle invalidates only the card. Trade-off: extra GPU memory per card (~50 KB at 170×140). Worth it at N≥10.
> - **`juce::Viewport` does not auto-cull children** — they receive `paint()` calls regardless of visibility. JUCE relies on the OS clip rect to skip the actual draw. Acceptable up to ~50 cards; if it bites, check `g.getClipBounds().intersects(...)` before per-cable cubicTo.
> - **Empty patch → empty snapshot vector** (Session.cpp:374–398). No `graphIO` sentinel returned from the snapshot API; the canvas just renders the two endpoint cards when `cards_` is empty.
> - **DAW conventions** (Bitwig Grid, Helix Native, VCV Rack): single cable per stereo connection, distinct hue for MIDI, always-visible bypass for live performance, `Input → Output` endpoint cards on empty state.

#### Unit 6 — `TransportBar` + `StatusBar` + `MainWindow` rewrite

- **Files:** `app-v3/src/ui/TransportBar.{h,cpp}`,
  `app-v3/src/ui/StatusBar.{h,cpp}`,
  `app-v3/src/ui/MainWindow.{h,cpp}` (significant rewrite).
- **Approach:** `TransportBar` (height 48) carries:
  - Left: `[+ Add Plugin]` button (per AC-UI-N — closes the
    biggest spec-flow gap). Click opens `juce::PopupMenu`
    populated from `ctx_.pluginHost.getKnownPluginList().getTypes()`;
    selection calls `ctx_.session.addPlugin(desc)`. Disabled
    while `Session::getState() != Idle`.
  - Centre: device-info label (driver / sample-rate / buffer-size)
    queried from `ctx_.audioEngine.getDeviceManager()`.
  - Right: `[Audio Settings]` · `[Test Tone]` · `[Plugin List]`
    buttons. Handlers lifted from current
    `RootComponent::onAudioSettingsClicked` /
    `onTestToneToggled` / `onPluginListClicked`.
  - CPU/latency placeholders ("CPU —" / "Latency —") until
    Phase 1.B+ MetricsRegistry.

  `StatusBar` (height 32) listens to `Session`:
  - Left: status dot (●) coloured by state — green Idle,
    amber Building, blue Swapping. "Chain: N plugin(s)" +
    state text ("Idle" / "Loading…" / "Swapping…"). Surfaces
    `Session::getLastError()` via a destructive-coloured pill
    on Idle transitions (per AC-UI-O).
  - Right: build version ("v3.0.0-pre").

  `MainWindow` becomes a thin `juce::DocumentWindow`:
  - Owns nothing besides a single `RootComponent` content (a
    vertical `FlexBox` of `TransportBar` (h:48) +
    `juce::Viewport` wrapping `SignalCanvas` (flex:1) +
    `StatusBar` (h:32)).
  - `closeButtonPressed()` → `JUCEApplication::quit()`.
  - No editor-window orchestration logic (canvas owns it).
  - Use `viewport_.setViewedComponent(canvas_.get(), false)` —
    canvas owned by parent, not viewport.

- **Verification:** macOS smoke — full window paints; Add Plugin
  populates; selection adds card; Test Tone toggle still works;
  Audio Settings / Plugin List dialogs open with `Pb3LookAndFeel`
  applied (no default-grey leak). Long chain horizontal-scrolls
  via Viewport.

> **Research insights (Unit 6):**
> - **Spec-flow critical:** without `[+ Add Plugin]` on TransportBar, MainWindow rewrite removes the only chain-growth entry point. The current `RootComponent` ComboBox at `MainWindow.cpp` lines 113–222 is the regression risk — Unit 6 *must* ship the replacement in the same commit.
> - **`juce::DialogWindow::launchAsync`** with `dialogBackgroundColour` parameter currently hardcodes `0x2A2A2A` at `MainWindow.cpp:347, 390`. Replace with `tokens::bgPopover` or remove so `Pb3LookAndFeel` drives.
> - **Master Mute decision (G1):** defer; no engine API today, would require an output-stage gain node.
> - **`MidiActivity` / `OscIndicator` from mockup:** defer to Phase 4 (when `MidiRouter` lands); show as dim placeholder dots until then or omit.
> - **Window state persistence (size/position):** out of scope for v0; document as Phase 3 follow-up.

### Adaptation notes by Path

(Note: post-deepen, Unit 3 was folded into Unit 5. The remaining
units are 1 — ColourScheme + Pb3LookAndFeel; 2 — Inter font;
4 — PluginCard; 5 — SignalCanvas + EditorWindow extraction;
6 — TransportBar + StatusBar + MainWindow rewrite + Add-Plugin
entry point. Five units total.)

If the user picks **α**: drop Units 1, 2, 5 (canvas + visual
style). Keep Unit 4 (`PluginCard`) but mount cards in a vertical
`juce::ListBox` instead of a canvas. Pull `EditorWindow` out as
a quick sub-task of Unit 4 (no longer free with Unit 5). Unit 6
shrinks: `TransportBar` exposes the same Add-Plugin button; no
`SignalCanvas` to wrap; `MainWindow` hosts the row-list directly.
Effort: 3–5 days.

If the user picks **β**: do α + Units 1 + 2 (visual style + Inter
font). Effort: 5–7 days.

If the user picks **δ**: do *all* of γ + add `PatchesPanel`,
`PluginBrowser`, `MidiMappingPanel` + drag-handlers + port-click
connection logic. Effort: 25–35 days. Add Units 7–11 for the
deferred features.

## Alternative Approaches Considered

Already covered as Paths α / β / δ / ε in the Decision Matrix. Path
ε (Tauri+Electron front-end) is the only one rejected for
architectural reasons; the others are valid trade-offs picked
against by phasing/scope.

## System-Wide Impact

### Interaction graph

When the user clicks Add to chain → `Session::addPlugin(desc)` →
worker instantiates → message thread commits → `Session` fires
`sendChangeMessage` → `SignalCanvas::changeListenerCallback`
re-reads `getNodeSnapshots()` → `layoutCards()` rebuilds the card
vector → `repaint()` → next paint draws an additional card + cables.

When the user clicks Remove on card 2 →
`card2->onRemove(NodeId(N))` → SignalCanvas closes
`editors_[N]` synchronously (destructor runs while the underlying
plugin is still alive) → `Session::removeNode(N)` destroys the
plugin instance inline + `bumpVersionAndNotify()` (async) →
listener callback runs on next message tick → diff drops card 2
from `cards_` and re-runs `layoutCards()` → `repaint()`.

### Error / failure propagation

Same as 1.D.3 baseline. UI errors (e.g., editor failed to open)
display via inline status text. No new error paths introduced.

### State lifecycle risks

- **EditorWindow lifecycle vs. node destruction:** `Session::removeNode`
  (Session.cpp:295–319) and `Session::commitLoadPatch`
  (Session.cpp:578–602) destroy `BypassableInstance`s
  **synchronously, inline**, *then* fire `sendChangeMessage`
  (which is async). The listener fires later — by which time
  the editor's `AudioProcessor*` is already a dangling pointer.
  **The correct discipline is caller-side, synchronous, before
  the Session call** — exactly the pattern the existing code
  already uses at `MainWindow.cpp:288, 300, 132`:
  ```cpp
  editorWindow_.reset();        // sync close, on message thread
  ctx_.session.addPlugin(desc); // (or removeNode / clearPatch)
  ```
  In the new canvas, every `PluginCard::onRemove` callback
  closes `editors_[id]` first; every Test Tone / clearPatch /
  replacePatch handler calls `editors_.clear()` first. Listener-
  side cleanup is defensive only (catches preference-restore-on-
  launch and other UI-bypassing paths). Tracked as AC-UI-P with
  ASan integration test.
- **Card layout vs Session async:** Session can transition
  `Idle → Building → Idle` between two paints. Canvas reads
  `getNodeSnapshots()` on every change message; intermediate
  Building state shows the current cards (no flicker). Add and
  Remove buttons are disabled during Building (per AC-UI-S).
- **Add-plugin queue overflow:** `Session::addPlugin` writes
  `lastError_` and drops requests once `addQueue_.size() >=
  kAddQueueMax (16)`. AC-UI-O surfaces this via StatusBar.

### API surface parity

The new UI is a presentation layer over `Session`. No engine API
changes. `SignalCanvas` is the *only* caller of
`Session::moveNode` / `setNodeBypass` / `removeNode` /
`getNodeSnapshots` from the UI side.

### Integration test scenarios

1. **3-plugin chain renders.** macOS smoke: add 3 plugins, see 3
   cards + cables.
2. **Per-card bypass works.** Click Bypass on card 2, audio stays
   audible (BypassableInstance ramp), card visual dims.
3. **Move ↑ on card 3 reorders.** Cards swap positions; cables
   rewire correctly.
4. **Remove on card 1 closes its editor.** Open EditorWindow on
   plugin 1, click Remove, EditorWindow closes without
   use-after-free (ASan).
5. **clearPatch empties canvas.** Click Test Tone (clearPatch +
   tone graph) → only Input + Output endpoint cards remain.

## Acceptance Criteria

### Functional

- [ ] **AC-UI-A** Visual style applied to all dialogs (Audio
  Settings, Plugin List, AlertWindow). No JUCE-default-grey
  remains in user-facing UI.
- [ ] **AC-UI-B** Inter font renders correctly on Linux + macOS.
  License attribution committed.
- [ ] **AC-UI-C** SignalCanvas auto-lays out cards left-to-right
  with bezier cables between adjacent cards.
- [ ] **AC-UI-D** Per-card Bypass / Edit / Move↑ / Move↓ /
  Remove buttons work end-to-end.
- [ ] **AC-UI-E** Multiple `EditorWindow`s can be open at once.
- [ ] **AC-UI-F** TransportBar shows current driver / sample-rate /
  buffer-size; CPU/latency placeholders are clearly marked.
- [ ] **AC-UI-G** StatusBar shows chain length + Session state.
- [ ] **AC-UI-H** Test Tone diagnostic still works; Plugin List
  dialog still works; Audio Settings dialog still works.
- [ ] **AC-UI-I** Chain length 0 (empty / cleared) renders
  Input → Output endpoint cards with a single cable between them.
- [ ] **AC-UI-J** Long chains (≥10 plugins) horizontal-scroll via
  `juce::Viewport`.
- [ ] **AC-UI-N** TransportBar exposes `[+ Add Plugin]` opening a
  `juce::PopupMenu` populated from
  `ctx_.pluginHost.getKnownPluginList().getTypes()`. Disabled
  while `Session::getState() != Idle`. *(Closes C1 — Add-plugin
  entry-point regression.)*
- [ ] **AC-UI-O** StatusBar surfaces `Session::getLastError()`
  after every state→Idle transition; clears on next successful
  op. Covers `addPlugin` failures and queue-overflow drops.
  *(Closes C2 + C3.)*
- [ ] **AC-UI-P** Every UI handler that triggers a Session
  mutator capable of destroying nodes (`removeNode`,
  `clearPatch`, `replacePatch`) closes affected `EditorWindow`s
  **synchronously, on the message thread, before** invoking the
  Session method — matching the existing pattern at
  `MainWindow.cpp:288, 300, 132`. Listener-side cleanup is
  defensive only (catches code paths that bypass the UI, like
  preference-restore-on-launch). ASan integration test loads a
  plugin, opens its EditorWindow, clicks Test Tone, and asserts
  no use-after-free.
- [ ] **AC-UI-Q** Clicking Edit on a node whose `EditorWindow` is
  already open brings the existing window to front (`toFront(true)`
  + `setVisible(true)`); never spawns a duplicate.
- [ ] **AC-UI-R** Move↑ on first card and Move↓ on last card
  render disabled buttons; click is a no-op. Move applied via
  `Session::moveNode(id, currentIndex ± 1)`.
- [ ] **AC-UI-S** While `Session::getState() == Building`, the
  StatusBar dot transitions to amber within 50 ms and shows
  "Loading…"; canvas controls (`Add Plugin`, per-card buttons)
  are disabled until Idle.
- [ ] **AC-UI-T** `SignalCanvas::resized()` re-runs `layoutCards()`;
  horizontal growth via `juce::Viewport`; vertical fits content
  height. No layout-on-paint coupling.

### Non-functional

- [ ] **AC-UI-K** `addPlugin` instantiation runs off the message
  thread; the canvas shows disabled controls + amber StatusBar
  state during the async window. (Reworded from "one event-loop
  tick" — `sendChangeMessage` is async-by-construction; the
  testable property is the absence of UI freeze + the disabled
  state coverage.)
- [ ] **AC-UI-L** No new audio-thread allocations. Listener path
  runs on message thread only; canvas never reads from
  `AudioGraphRuntime::processBlock`.
- [ ] **AC-UI-M** ColourScheme tokens are addressable by name from
  any future UI code (Phase 3 canvas reads the same source).
  *(Combined with AC-UI-A in spirit; left in for the
  forward-compat angle.)*
- [ ] **AC-UI-U** `SignalCanvas::paint` runs in <500µs at chain
  length 30 on the user's MaxBook (`juce::PerformanceCounter`
  smoke). Cables cached as two `juce::Path` members; one
  `strokePath` per material.

### Quality gates

- [ ] All existing 91 unit tests still pass.
- [ ] No new clang-format-18 violations.
- [ ] No new clang-tidy-18 warnings.
- [ ] `pb3_apply_strict_warnings` covers every new `.cpp` file.
- [ ] macOS build job stays green.

## Success Metrics

- **User can see signal flow at a glance.** macOS smoke: load 3
  plugins, look at the canvas, identify which is first / second /
  third without reading text. Bezier cables convey direction.
- **Per-plugin operations are 1 click.** Bypass, Edit, Remove,
  Move all reachable on every card simultaneously (vs current
  single-row UI's first-node-only).
- **Aesthetic matches the mockup.** Side-by-side screenshot of
  the JUCE app + the Vercel demo: same dark blue-gray + teal
  accent + dark cards.

## Dependencies & Prerequisites

- **Phase 1.D.3 Units 1–5** in `main` — engine already supports
  the multi-node API the canvas drives. ✅
- **JUCE 8.0.4** — `juce_add_binary_data` for fonts,
  `juce::FlexBox`, `juce::Viewport`, `juce::LookAndFeel_V4`. ✅
- **Inter font files** — open-licensed, fetched once into
  `Resources/Fonts/`. New dependency.
- **No new third-party libs.**

## Risk Analysis & Mitigation

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Inter font binary bloats the executable | Medium | Low | Subset to Latin glyphs only (~150 KB / weight). |
| oklch → sRGB conversion drift across platforms | Low | Low | Conversion done once in Python script; tokens become hard-coded sRGB constants. |
| `LookAndFeel_PB3` overrides miss a JUCE component we use, leaving JUCE-default-grey leak | Medium | Low | Visual smoke test on every dialog before merge; document override checklist. |
| Long chain (≥20 plugins) triggers horizontal scroll-jank | Low | Low | `juce::Viewport`'s built-in scrolling; cards painted only when in viewport (JUCE's default culling). |
| User wants drag-to-rearrange before Phase 3 | Medium | Medium | Document explicitly: "Move ↑ / ↓ buttons are the linear-chain reorder mechanism in 1.D.3; drag arrives in Phase 3." Card layout uses absolute positions so adding drag later is purely a `mouseDrag` handler attaching to the card. |
| Phase 3 wants a different layout (not auto-left-to-right) | Low | Low | `SignalCanvas::layoutCards` is the single layout authority; Phase 3 replaces it with a position-from-Patch implementation. |
| Geist license complications block redistribution | Avoided | — | Use Inter (SIL OFL 1.1, broadly used) instead. Documented in plan. |
| Visual style port to JUCE LookAndFeel ends up uglier than the mockup | Medium | Medium | Reference the Vercel demo running side-by-side during development; use macOS smoke + screenshot comparison. |
| Add-plugin entry point lost when MainWindow rewrites away `RootComponent` | High (caught) | High | Ship `[+ Add Plugin]` on TransportBar in same Unit 6 commit. AC-UI-N. |
| EditorWindow use-after-free on `removeNode`/`replacePatch` because Session destroys instances synchronously inline before firing the async change message | High (caught) | High | UI handler closes affected editors **synchronously, before** calling the Session mutator (mirrors `MainWindow.cpp:288, 300, 132`). Listener-side cleanup is defensive only. ASan integration test gates the merge. AC-UI-P. |
| `addPlugin` failure or 16-deep queue overflow silent | Medium (caught) | Medium | StatusBar surfaces `Session::getLastError()` on Idle transitions. AC-UI-O. |
| `Pb3LookAndFeel` override list incomplete; default-grey leaks on scrollbars / sliders / table headers / popup menus | Medium | Low | Drive most defaults via `LookAndFeel_V4::ColourScheme` + per-class `setColour(ColourId, …)`; override `paint*` only for bespoke shape. Visual smoke on every dialog before merge. |
| Hardcoded `dialogBackgroundColour = 0x2A2A2A` in `MainWindow.cpp:347, 390` keeps Audio Settings + Plugin List dialogs grey | Medium | Low | Replace both call sites with `tokens::bgPopover` during Unit 6. |
| `App::shutdown` leaves `juce::Desktop` holding a dangling LookAndFeel pointer | Low | High | Call `setDefaultLookAndFeel(nullptr)` before `mainWindow_.reset()` in `App::shutdown`. |
| `juce::Font(Typeface::Ptr, height)` deprecated in JUCE 8 | Low | Low | Use `juce::Font(juce::FontOptions(typeface).withHeight(...))` everywhere. |
| Naive listener tear-down causes flicker on every change message | Medium (caught) | Medium | NodeId-diff strategy reuses `PluginCard`s by NodeId; `setSnapshot` is the cheap visual update path. See Unit 5 pseudocode. (UAF risk is handled separately by D1b — caller-managed editor close.) |
| `computeCables` on the paint hot path acquires `statusMutex_` per frame | Medium (caught) | Medium | `rebuildCables()` runs from `layoutCards`; cached as two `juce::Path` members. AC-UI-U benchmark. |
| Phase 3 forced to rewrite cable derivation because `computeCables` hardcodes adjacency | Medium (caught) | Medium | v0 reads `activePatch_.connections`; same logic carries forward to arbitrary topology with zero rewrite. |
| Headless test target excludes `juce_gui_extra` and `pb3_app_core`; UI tests can't link | Medium | Medium | UI tests use OfflineRender-style headless fixture; or add a separate `pb3_ui_tests` target that does link `pb3_app_core`. Decided in Unit 4/5 implementation. |

## Resource Requirements

- 1 engineer × 2–2.5 weeks (Path γ).
- macOS hardware (user's MaxBook) for end-to-end smoke per unit.
- No CI changes; existing `pb3_app_core` linker config picks up
  new files via the existing apply_strict_warnings + binary_data
  patterns.

## Future Considerations

After Path γ lands, Phase 3 grows the canvas:

1. **Drag-to-rearrange** — `PluginCard::mouseDrag` updates
   `card.bounds`; on `mouseUp`, snap to nearest valid chain
   position and call `Session::moveNode`. ~2-3 days.
2. **Port-click connections** — the engine already supports
   arbitrary topology; UI grows port hit-testing + drag-cable
   preview + `Session::setConnections(std::vector<PatchConnection>)`
   API addition. ~5-7 days. Requires an additive Session API.
3. **Lasso-select + bulk delete** — ~2-3 days.
4. **Zoom controls** — ~2 days.
5. **Patches sidebar** — once Phase 2 lands `PatchRegistry`,
   wire the sidebar to it. ~3-5 days.
6. **Plugin browser sidebar** — currently the existing
   `juce::PluginListComponent` dialog. Could be folded into the
   right sidebar in Phase 3 + add drag-from-sidebar-to-canvas.
   ~3-5 days.
7. **MIDI mapping panel** — once Phase 4 lands `MidiRouter`,
   wire the bottom panel. ~5-7 days.
8. **Grid background + minimap** — visual polish from the
   mockup. ~2 days.

## Documentation Plan

- `app-v3/src/ui/SignalCanvas.h` — class doc-block (≤30 lines)
  describing the read-only canvas + Phase 3 extension hooks.
- `app-v3/src/ui/ColourScheme.h` — token list with master plan
  §4.2.D 19-token mapping + new tokens.
- `docs/THIRD_PARTY.md` (new) — Inter font attribution.
- `docs/architecture.md` (when written) — UI section pointing to
  this plan as the design rationale.

## Resolve Before Planning

**One open decision required from the user:**

- **Path α / β / γ / δ?** Recommendation is γ. Plan adapts
  trivially to α or β by dropping units; δ adds Units 7–11.

If user picks γ, no other questions block.

## Deferred to Implementation

- Exact button glyphs (lucide-react has Power/Settings/X/Grip/etc;
  JUCE doesn't have those out of the box). Use `juce::Drawable` from
  SVG or a local `juce::Path` for each. Pick at implementation.
- Cable colour mapping (audio = neutral grey vs MIDI = teal)
  finalised against the mockup's
  `--chart-3` (output ports, amber) and `--primary` (input ports,
  teal). Picked during Unit 5.
- Card-drag indicator (when Phase 3 lands drag-to-rearrange):
  ghost outline vs slot indicator vs reorder animation. Phase 3
  decision.
- Status-bar refresh cadence: listener-driven (instant) vs Timer
  poll (5 Hz, smooths Session-state spam). Listener-driven for v0;
  Timer if Idle/Building flickers.
- Whether the Test Tone toggle stays in the TransportBar or moves
  to a "Diagnostics" overflow menu post-Phase-3. Defer; for v0 it
  stays in the TransportBar where it currently lives.

## Scope Boundaries (Non-Goals)

- ❌ Drag-to-rearrange cards (Phase 3).
- ❌ Port-click drag-to-connect (Phase 3).
- ❌ Arbitrary topology UI (Phase 3 — engine already supports;
  Phase 3 adds the editor).
- ❌ Lasso-select + bulk operations (Phase 3).
- ❌ Zoom controls (Phase 3).
- ❌ PatchesPanel sidebar (Phase 2 + Phase 3).
- ❌ Plugin browser sidebar (Phase 3 polish; existing dialog stays).
- ❌ MidiMappingPanel (Phase 4).
- ❌ CPU + latency live meters (Phase 1.B+ MetricsRegistry; show
  placeholder dashes).
- ❌ Save/load patch files (Phase 2 PdlSerializer).
- ❌ Tray icon (Phase 5+).
- ❌ Menus (File / Edit / Help) — current diagnostic buttons in
  TransportBar substitute.

## Sources & References

### Origin

- **Origin document:**
  [docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md](2026-04-12-001-feat-pedalboard-3-rewrite-plan.md)
  — key decisions carried forward:
  - §4.2.D UI subsystem list (PluginFieldView, PluginNodeView,
    PluginConnectionView, TransportBar, etc.) — Path γ ships a
    reduced version of these, deferring sidebars/panels to Phase
    3.
  - §"Alternative Approaches" Alt A1 rejection of Tauri/React →
    Path ε rejection here is consistent.
  - §4.3 Phase 3 4–6 week estimate — Path γ's 2–2.5 week effort
    is a *subset* (read-only canvas + visual style only) of that
    Phase-3 estimate.

### Internal references

- `vercel-ui-demo/` — design target. Read once for visual style;
  not a runtime dependency.
- `app-v3/src/session/Session.h` — multi-node API the canvas
  drives.
- `app-v3/src/ui/MainWindow.cpp` — current transitional UI;
  Unit 6 rewrites it.

### External references

- JUCE 8 `juce::LookAndFeel_V4` —
  https://docs.juce.com/master/classLookAndFeel__V4.html
- JUCE 8 `juce::FlexBox` + `juce::Viewport` for layout.
- Inter font — https://rsms.me/inter/ — SIL OFL 1.1.
- Mockup design tokens — `vercel-ui-demo/app/globals.css`.

### Related work

- Phase 1.D.3 plan:
  [docs/plans/2026-04-28-001-feat-multi-plugin-chain-plan.md](2026-04-28-001-feat-multi-plugin-chain-plan.md)
  — engine foundation that this UI presents.

---

## Decision Required (the user reads this and replies)

> **Pick a path: α, β, γ (recommended), δ. Or push back if the
> framing's wrong.**
>
> - **α** — vertical row list, no style change. 3–5 days.
> - **β** — vertical row list + visual style only. 5–7 days.
> - **γ** — read-only signal canvas + visual style + thin
>   transport/status bars. 2–2.5 weeks. **Recommended.**
> - **δ** — full mockup port now. 5–7 weeks. Phase 2/4 panels
>   show "Coming in Phase X" placeholders.
> - **ε** — Tauri/React front-end. **Rejected** by master plan.
>
> Once a path is chosen, the implementation units in this plan
> proceed (or adapt per the "Adaptation Notes by Path" section).
