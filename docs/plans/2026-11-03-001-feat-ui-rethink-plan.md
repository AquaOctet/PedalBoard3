---
title: The visual rethink — stop looking generated
type: feat
status: draft
date: 2026-11-03
topic: ui-rethink
artifact_contract: ce-unified-plan/v1
artifact_readiness: draft
product_contract_source: the user's report of 2026-11-03 ("told the UI looks AI generated, so we want to figure that out and make it better") and their choice of a full visual rethink over a density pass
execution: code
---

# The visual rethink — stop looking generated - Plan

## Goal Capsule

- **Objective:** The main window stops reading as a generated web dashboard and starts reading as live-performance software.
- **The report:** nobody has sent a crash log. The feedback the app is getting is that the UI looks AI generated.
- **The cause, and it is not the colours.** #105 already re-pointed every token in `ColourScheme.h` to an Ableton Live 12 theme, and those are a real DAW's colours. What is still generated is the *layout*, and the code says so in `ColourScheme.h:18`:

  > `vercel-ui-demo/` is still the source for **layout** — where things sit, how much room they take, which of them are icons.

  That demo is a v0 generation. The port was faithful, which was the right call for getting built; the cost is that it imported v0's compositional habits wholesale.
- **Product authority:** this plan replaces `vercel-ui-demo/` as the layout authority. The demo stays a reference for *what exists* — which controls, which affordances — and stops being the reference for size, spacing, density and emphasis. KD1 says what replaces it.
- **Tracking:** M7 or a milestone of its own (see Open decisions). Marker units `VR-U<N>`, labels `vr-u<N>`, so they do not collide with plan 004's `u<N>` or plan 2026-09-13-002's `uc-u<N>`.
- **Out of scope:** the colour tokens (settled by #105), dialogs and menus (settled by plan 2026-09-13-002), canvas zoom (still deferred, plan 004 KD9), any behaviour change. This plan moves, resizes and restyles; it does not change what a control does.
- **Stop conditions:** stop and surface if a unit cannot be done without changing behaviour, or if a density change would make a target too small for its use (KD2).

---

## The evidence

Measured off `build/ui-snapshots/window-board.png` and `window-empty.png` at 1280x800, rendered from `eb38137`.

| Tell | Measurement | Where it lives |
|---|---|---|
| Half-empty | The empty board holds two 85 px port boxes in a 780x510 canvas. The mappings panel is 170 px tall carrying one sentence. The bank list has roughly 400 px of nothing under six rows. | `SignalCanvas.cpp`, `BoardMappingsPanel.cpp`, `BankSidebar.cpp` |
| Twice too loose | Bank rows are 40 px carrying 11 px text (`BankSidebar.cpp:23`). Plugin rows are 34 px (`PluginBrowser.cpp:25`). Ableton's browser rows are about 19 px. At 1280x800 the window shows 6 patches and 20 plugins where a DAW shows two to three times as many. | `BankSidebar.cpp`, `PluginBrowser.cpp` |
| Repetition as decoration | Twenty identical accent stripes run down the plugin list. Every row has one, so none of them says anything. | `PluginBrowser.cpp` |
| Caps-header reflex | Six all-caps grey micro-headers on one screen: BANK, PLUGINS, BUILT-IN, RECENTLY USED, ALL EFFECTS, MAPPINGS. | `BankSidebar.cpp`, `PluginBrowser.cpp`, `BoardMappingsPanel.cpp` |
| Everything is the same card | One corner radius, one 1 px border, one flat fill, one elevation — the search field, the filter chips, the node cards and the three panels all draw alike. | `Pb3LookAndFeel.cpp`, `PluginCard.cpp` |
| The subject is the least-defined region | The canvas is the point of the app and is the emptiest, flattest area on screen. Nothing directs the eye to it. | `SignalCanvas.cpp`, `BodyLayout.h` |
| Web chrome in a DAW | Pill filter chips (All / Built-in / Other) at 22 px, and three identical hamburger glyphs standing for three unrelated menus. | `PluginBrowser.cpp`, `BankSidebar.cpp`, `BoardMappingsPanel.cpp` |

---

## Key Decisions

- **KD1. The reference becomes live-performance software, not a web app.** Where the demo and a DAW disagree about size, spacing or emphasis, the DAW wins. The named references are Ableton Live 12 (already the colour source, so the two agree), Gig Performer and Cantabile — the two programs that do what this app does. A unit that changes a measurement says which reference it took it from.

- **KD2. Two densities, because there are two uses — and this is the spine of the rethink.** The flaw in "just make it denser" is that this app is used in two incompatible ways:
  - **Building a board:** at a desk, with a mouse, wanting to see as much as possible at once. Dense, information-rich, small targets are fine.
  - **Playing it:** on a stage, with a laptop at arm's length on a stand, glancing down between bars. Big targets, high contrast, almost no text.

  A v0 generation is neither: it is uniformly medium-density, which is what looks good in a screenshot and serves no actual user. Picking one density and applying it everywhere trades one wrong answer for another. So: the build surfaces get dense (U2, U4), and the things you touch while playing — the patch list, the transport, the mute — keep or gain size. This is the one decision that needs the maintainer's nod before U2 starts, because it is the difference between a restyle and a product change. See Open decisions.

- **KD3. The canvas is the subject.** It gets depth the chrome does not: recessed against raised panels, its own backdrop treatment, and cables that read as cables. Everything else becomes subordinate. Today every region is a flat rectangle of equal weight, which is why the eye has nowhere to land.

- **KD4. Density is one header, not fifteen constants.** Each component currently owns its own numbers (`BankSidebar.cpp:23`, `PluginBrowser.cpp:19-25`, `BoardMappingsPanel.cpp:17-18`, `StatusBar.cpp:13-17`, `TransportBar.cpp:40-59`, `ChromeMetrics.h`). U1 gathers them into one spacing and row-height scale so a density decision is a single edit and can be reverted in one. No unit after U1 hard-codes a measurement.

- **KD5. Every unit shows before and after.** The snapshot tool renders 34 surfaces (`build.ps1 snapshots`). Each MR attaches the images for what it changed, as plan 2026-09-13-002 KD6 established. Pixel-exact CI gating stays out, because font rasterisation differs across platforms.

- **KD6. Nothing in this plan changes behaviour.** No control gains or loses a function, no shortcut moves, no file format changes. That keeps the whole plan reviewable by eye against the snapshots, and keeps it off the critical path of #102 and the beta.

---

## Unit Index

| Unit | Title | Depends on |
|---|---|---|
| U1 | One metrics header | none |
| U2 | Density: the lists and the panels | U1 |
| U3 | The canvas becomes the subject | U1 |
| U4 | Cut the generated chrome | U1 |
| U5 | Empty states that are not voids | U1 |
| U6 | The before-and-after pass | U2, U3, U4, U5 |

---

### U1. One metrics header

- **Goal:** One place defines spacing, row heights and the elevation ramp, so KD2's density decision is a single edit (KD4).
- **Requirements:** KD4
- **Dependencies:** none
- **Files:**
  - `app-v3/src/ui/Metrics.h` (create): a spacing ramp, a row-height scale with a name per role (`rowDense` for browser and mapping rows, `rowTouch` for anything hit while playing), the corner radii, the border widths, and an elevation ramp naming which `ColourScheme.h` background each surface level uses. `ChromeMetrics.h`'s four constants move in and it is deleted, with its comment about the corner icon lines kept — it is still the reason those numbers agree.
  - `BankSidebar.cpp`, `PluginBrowser.cpp`, `BoardMappingsPanel.cpp`, `StatusBar.cpp`, `TransportBar.cpp`, `PluginCard.cpp`, `SignalCanvas.cpp`, `BodyLayout.h`: their local constants re-point at the header. Values are unchanged in this unit.
  - `app-v3/tests/unit/ui/BodyLayout_test.cpp`: follows any renamed constant.
- **Test scenarios:** the ramp is monotonic (each step larger than the last) and the elevation ramp's backgrounds get lighter by level, so a later edit cannot silently invert the layering.
- **Verification:** all 34 snapshots are byte-identical to the pre-change set. U1 moves numbers; it does not change any.

### U2. Density: the lists and the panels

- **Goal:** The build surfaces show roughly twice what they show today, at DAW density (KD1, KD2).
- **Requirements:** KD1, KD2
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/ui/BankSidebar.cpp`: the patch row drops from 40 px toward `rowTouch` — this list is touched while playing, so it is the one list that stays large (KD2). What it loses is padding around the list, not row height.
  - `app-v3/src/ui/PluginBrowser.cpp`: rows 34 px to `rowDense`; `kPad` 12 to the ramp's smaller step; the 44 px footer hint shrinks or moves to a tooltip; section gaps tighten.
  - `app-v3/src/ui/BoardMappingsPanel.cpp`: rows 28 px to `rowDense`, gap 2 to the ramp.
  - `app-v3/src/ui/BodyLayout.h`: `kDefaultMetrics` panel widths re-checked against the new content widths; `foldBelow` re-checked, since denser panels fold later.
- **Test scenarios:** `BodyLayout_test.cpp` covers the new widths at the fold boundary; a row height below the minimum legible size fails a static assert.
- **Verification:** `window-board`, `window-empty`, `window-wide`, `window-narrow` and `window-mappings-rows` show the increased count. Record plugins-visible and patches-visible before and after in the MR.

### U3. The canvas becomes the subject

- **Goal:** The eye lands on the canvas, and the nodes on it read as devices rather than web cards (KD3).
- **Requirements:** KD3
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/ui/SignalCanvas.cpp`: the canvas is recessed against the panels — a darker backdrop, an inner shadow at its edges, and a faint grid or texture so it reads as a surface things sit *on* rather than a blank div. Cables become beziers with a slack that depends on the horizontal run, instead of straight segments.
  - `app-v3/src/ui/PluginCard.cpp`: the node stops being a uniform rounded card. It gains a raised edge against the recessed canvas, its header separates from its body by value rather than only by a hairline, and the corner radius drops so it reads as hardware. The left accent stripe stays here, where it distinguishes one node from another — unlike in the browser list, where U4 removes it.
  - `app-v3/src/ui/Pb3LookAndFeel.cpp`: the elevation ramp from U1 is applied, so a raised surface and a recessed one are drawn by one pair of helpers rather than per-component.
- **Test scenarios:** the bezier path is covered headless — a cable between two ports starts and ends on them, and a zero-length run does not produce a degenerate curve.
- **Verification:** `window-board`, `window-empty`, `window-selection`, `window-muted`, `icons-card`.

### U4. Cut the generated chrome

- **Goal:** The decoration that encodes nothing goes, and the affordances that all look alike stop looking alike (KD1).
- **Requirements:** KD1
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/ui/PluginBrowser.cpp`: the per-row accent stripe is removed — twenty identical marks that distinguish nothing. Format (`Built-in`, `VST3`) stops being a right-aligned caption on every row and becomes something that only shows where it differs from the section it is in. The All / Built-in / Other pill chips become a segmented control, which is what a DAW uses for a mutually exclusive filter.
  - `app-v3/src/ui/BankSidebar.cpp`, `PluginBrowser.cpp`, `BoardMappingsPanel.cpp`: the three identical hamburgers become three different glyphs, each naming its own menu. `Icons.h` gains whatever is missing.
  - All-caps section headers: BANK and MAPPINGS go, since the panel they head is unambiguous. BUILT-IN, RECENTLY USED and ALL EFFECTS stay — they separate genuinely different lists — but drop to a quieter weight so six shouting labels become three murmuring ones.
- **Test scenarios:** the segmented control's selection round-trips through the browser model, and no filter state is lost in the swap.
- **Verification:** `window-board`, `window-empty`, `icons-bar`, `window-panels-collapsed`.

### U5. Empty states that are not voids

- **Goal:** An empty region either earns its space or gives it up (KD1).
- **Requirements:** KD1
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/ui/SignalCanvas.cpp`: an empty board currently floats two small port boxes in a large void. Input and Output get placed with the space between them treated as the chain that is missing — a dashed run between the ports, and the hint sitting on it — so the empty state shows the shape of what goes there.
  - `app-v3/src/ui/BoardMappingsPanel.cpp`: the empty panel's 170 px of nothing shrinks to its hint; #134 already made the table fill its viewport, so this is the panel height following suit.
  - `app-v3/src/ui/BankSidebar.cpp`: the list's trailing space takes the New / Duplicate buttons up with it rather than pinning them to the window's bottom edge, so a six-patch bank does not show 400 px of nothing above them.
- **Test scenarios:** the empty-canvas hint is laid out from the port positions, so it stays between them at every window width in `BodyLayout_test.cpp`'s range.
- **Verification:** `window-empty`, `window-mappings-collapsed`, `window-narrow`.

### U6. The before-and-after pass

- **Goal:** The whole change is judged as one thing, on the surfaces, not unit by unit (KD5).
- **Requirements:** KD5
- **Dependencies:** U2, U3, U4, U5
- **Files:**
  - `app-v3/src/tools/pb3-ui-snapshots.cpp`: a surface for a *full* bank — twenty-odd patches — since every snapshot today shows six, which is exactly the count that hides a density problem.
  - `docs/screenshots/*.png`: regenerated. They were last rebuilt at `29582d2` and predate !131, !132, `8c87c4b`, !135 and all of this plan.
  - `docs/status/`: a short note on what the rethink changed and why the demo is no longer the layout authority.
- **Test scenarios:** none; this unit is evidence.
- **Verification:** the full 34-surface set before and after, side by side, in the MR. The maintainer's eye is the gate.

---

## Open decisions

1. **KD2 needs a yes or no before U2 starts.** Treating the patch list and transport as stage controls and everything else as desk controls is a product position, not a styling choice. If the answer is no, U2 collapses to a uniform density pass and the patch list shrinks with everything else.
2. **Milestone.** This is not beta-blocking — it is a look problem, not a defect — so it reads as M7-after-the-beta or a milestone of its own. Filing it anywhere needs the maintainer's say-so.
3. **How far U3 goes.** "Nodes read as hardware" has a wide range, from a raised edge and a tighter radius at one end to drawn faceplates at the other. The unit as written is at the restrained end.
