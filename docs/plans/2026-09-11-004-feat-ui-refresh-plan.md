---
title: UI Refresh (stage layout) - Plan
type: feat
status: completed
date: 2026-09-11
topic: ui-refresh
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: issue #32 design pass (mockup https://claude.ai/code/artifact/0cfe3e05-a5b3-4fe7-bc98-f41e5117e6c2, private)
execution: code
---

# UI Refresh (stage layout) - Plan

## Goal Capsule

- **Objective:** Rebuild the main window's layout for the stage, without changing the palette. That covers:
  - plugin actions together on the left of the top bar
  - a CPU meter and MIDI/OSC activity lights
  - the patch bank in a left sidebar
  - a searchable plugin browser in a right sidebar
  - sidebars that fold to icon strips on small windows
  - redesigned plugin cards with inline rename
  - a board-wide mappings panel
  - an About window
- **Product authority:** the #32 design pass and its mockup (above), with the decisions the user made on 2026-09-11 (Key Decisions). Colours stay exactly as `app-v3/src/ui/ColourScheme.h` defines them.
- **Tracking:** milestone "UI Refresh (pre-beta)". Units whose requirement is already an issue close it directly:
  - #22, the CPU meter: U2
  - #23, rename on the card: U8
  - #24, the About window without license text: U11

  #32 is the design issue and closes when this plan lands. #22-#24 sit on "1.0 Finish Pass (pre-beta)" today; move them to this milestone when the units are filed. The other units get new issues through `milestone-from-plan`.
- **Out of scope:**
  - canvas zoom and Fit (U12, deferred; see KD9)
  - the macOS menu bar (#25)
  - recent files (#30) and drag-and-drop open (#31)
  - MiniMeters-style metering (#45)
  - any license text (#47)
- **Stop conditions:** stop and surface if an activity signal needs an allocation or a lock on the audio or MIDI-input thread, or if dropping a plugin at a position needs a Session API beyond an optional position on the add request.

---

## Key Decisions

- **KD1. Structure, not colour.** Every colour comes from the existing tokens; the refresh changes layout and density. New components style through `Pb3LookAndFeel` and `tokens::`, never literals.
- **KD2. Top bar: plugin actions on the left.**
  - The left cluster is `[File ▾] [Settings ▾] [Plugins ▾] [+]` (Settings joined it on 2026-09-11, KD10).
  - `+` is a square icon button (tooltip "Add plugin") that opens the add list directly: scanned effects first, then built-ins.
  - The Plugins menu puts the same list first as "Add Plugin ▸", then Plugin List… and Rescan Plugins.
  - The right end is for watching the rig: the MIDI/OSC lights and the CPU meter. The OSC port field, Settings and Test Tone have left it (KD10).
  - The plugin browser sidebar stays on the right.
- **KD3. Small windows fold, not hide.** Below about 1200 px wide, both sidebars fold to 48 px icon strips. Clicking a strip icon slides its panel open over the canvas. The left strip keeps the previous / current / next patch one click away. A three-bar toggle at each panel's outer top corner (top-left under File for the bank, top-right for the plugin browser) collapses it to its strip or opens it again at any window size, and File › Patch Bank and Plugins › Plugin Browser do the same; there are no status-bar panel toggles (user decision, 2026-09-11).
- **KD4. Mappings: both views.**
  - The per-plugin dialog (M4) stays as the close-up: learn, ranges and the plugin's MIDI channel filter.
  - A board-wide panel docks under the canvas and lists every mapping on the board, with the selected card's rows highlighted, not filtered.
  - Both edit through `Session::setNodeMappings`.
- **KD5. Patch changes are immediate.** Picking a patch switches to it; there is no queued-patch marker or patch queue.
- **KD6. About shows no license text** until the license is chosen (#47). Its "Source code" button stays hidden until the open-source release, because the repository is private (user decision, 2026-09-11).
- **KD7. The canvas centres the board** with Input and Output next to the chain (merged in !60). `ui/CanvasLayout.h` stays the single geometry source, so a card-size change is a metrics change.
- **KD8. Activity is counted, not pushed.** MIDI/OSC and per-plugin activity are counters the UI polls on a timer. ControlHub runs on the message thread, so plain counters suffice there. Anything read across threads is a relaxed atomic. Nothing allocates or locks off the message thread.
- **KD9. Canvas zoom and Fit are deferred.** They're new behaviour (a scale on CanvasLayout, scaled hit-testing and cables), not a layout change, so they wait for 1.x (user confirmed, 2026-09-11). The mockup's zoom control is not built.
- **KD10. Settings is a menu on the left, and under File.** It opens three dialogs: Audio Devices (the device and its channels, plus the Test Tone toggle), MIDI Settings (which inputs are enabled) and OSC Settings (the port and multicast group); a ticked Test Tone item sits at the foot of the menu. Test Tone leaves the main screen. These are set once per rig and don't need top-bar space (user decisions, 2026-09-11).

---

## Implementation Units

Pure models (layout, rows, zones) live header-only under `app-v3/src/ui/`, as `CanvasLayout.h` does, so the headless test target can include them without linking `pb3_app_core`.

### U1. Top bar: plugin actions on the left

- **Goal:** File, Plugins and `+` sit together on the left. `+` and Plugins ▸ Add Plugin open the same add list.
- **Requirements:** KD2
- **Dependencies:** none. Shipped in !62 (`feat/ui-refresh-transport`).
- **Files:**
  - `app-v3/src/ui/TransportBar.h`, `app-v3/src/ui/TransportBar.cpp`: the left cluster order, a `+` icon button, the Plugins menu with an "Add Plugin" submenu, and one `makeAddPluginMenu()` shared by both
- **Approach:**
  - One function builds the add menu: effects from `PluginHost::getKnownEffects`, then a Built-in submenu from `getInternalNodeTypes`. Each item carries its own action, so both entry points share it without id bookkeeping. Adds go through `EditHistory`.
  - `+` keeps Add Plugin's enablement (Session Idle, Test Tone off). The submenu is disabled under the same conditions (`canAddPlugin()`).
  - The outline styling of Audio Settings and Test Tone moves to U2, which reworks the top bar's right end anyway.
- **Test scenarios:** UI by hand.
- **Verification:** at 1280×800 and 960×540, the left cluster reads File, Plugins, +. Both paths add a plugin, and undo removes it.

### U2. CPU meter in the top bar

- **Goal:** A CPU meter with a 70% tick: teal below, amber from 70%, red from 90%, with a numeric readout. Settings (U10) and Test Tone become outline buttons.
- **Requirements:** #22
- **Dependencies:** U1 (top-bar layout)
- **Files:**
  - `app-v3/src/ui/MeterMath.h` (create): zone for a load, and exponential smoothing, as pure functions
  - `app-v3/src/ui/CpuMeter.h`, `app-v3/src/ui/CpuMeter.cpp` (create): a component with a ~10 Hz timer reading `AudioDeviceManager::getCpuUsage()`
  - `app-v3/src/ui/TransportBar.cpp`: place it
  - `app-v3/src/ui/Pb3LookAndFeel.h`, `app-v3/src/ui/Pb3LookAndFeel.cpp`: an outline ("ghost") button style, set per button through a component property
  - `app-v3/tests/unit/ui/MeterMath_test.cpp` (create), `app-v3/tests/CMakeLists.txt`
- **Approach:**
  - Smooth the 0..1 load so the bar doesn't flicker, and hold a peak for about 1 s so a spike is seen.
  - The tooltip gives the audio device's xrun count (`AudioIODevice::getXRunCount`) where the device reports one. That's the "overload warning" in #22.
  - With no device, the meter reads "—" in the muted colour.
- **Test scenarios:**
  - the zone boundaries at 0.69 / 0.70 / 0.89 / 0.90
  - smoothing converges and never overshoots
  - the peak hold decays after its hold time
- **Verification:** ctest green. By hand, a heavy plugin pushes the bar into amber.

### U3. MIDI and OSC activity lights, and per-plugin activity

- **Goal:** The MIDI and OSC lights blink when input arrives. Each card can tell when a mapped control just moved it; U7 uses that.
- **Requirements:** mockup top bar and card states; KD8
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/session/ControlHub.h`, `app-v3/src/session/ControlHub.cpp`:
    - counters bumped in `handleMidi` / `handleOsc`
    - a per-`NodeId` counter bumped when `applyUpdate` sets a parameter
    - read accessors
  - `app-v3/src/ui/ActivityLights.h`, `app-v3/src/ui/ActivityLights.cpp` (create): a timer at ~30 Hz; a light stays lit ~120 ms after its counter changes
  - `app-v3/tests/unit/session/ControlHub_test.cpp`
- **Approach:**
  - ControlHub is the message-thread entry point both input edges already marshal into, so the counters are plain members read on the same thread. If an edge ever calls from another thread, they become relaxed atomics.
  - The per-node map is cleared on a mapping re-feed, so a whole-patch swap never shows a stale id.
- **Test scenarios:**
  - MIDI and OSC counters advance on input, including input that matches no mapping
  - the node counter advances only when a parameter is actually set
  - the node counter clears on re-feed
- **Verification:** ctest green. By hand, the lights blink with a MIDI controller and an OSC sender.

### U4. Patch bank sidebar

- **Goal:** A 248 px left sidebar showing the whole bank:
  - one row per patch: program-change number, name and node count
  - the active row highlighted
  - click to switch
  - New and Duplicate
- **Requirements:** mockup main window; KD5
- **Dependencies:** none
- **Files:**
  - `app-v3/src/ui/BankRows.h` (create): a pure row model from a `PatchBank` (PC number = index, name, node count, active)
  - `app-v3/src/ui/BankSidebar.h`, `app-v3/src/ui/BankSidebar.cpp` (create)
  - `app-v3/tests/unit/ui/BankRows_test.cpp` (create), `app-v3/tests/CMakeLists.txt`
- **Approach:**
  - The sidebar listens to BankDocument. Clicking a row calls `select`, a double-click renames through the existing rename flow, and a context menu offers Rename / Remove.
  - The header shows the board name (`boardDisplayName` of the current file).
  - The patch strip stays in the top bar.
  - The active patch's node count reads live from Session, because the slot is a cache until switching away.
- **Test scenarios:**
  - rows for a three-patch bank
  - the active flag follows `activeIndex`
  - an empty bank yields one "Untitled" row
- **Verification:** ctest green. By hand, click-switching matches Program Change switching.

### U5. Plugin browser sidebar

- **Goal:** A 256 px right sidebar:
  - a search box and category chips
  - groups: Built-in, Recently used, All effects
  - double-click adds at the end of the chain
  - dragging onto the canvas adds the plugin at the drop point
- **Requirements:** mockup main window; KD2
- **Dependencies:** U6 (panel slot)
- **Files:**
  - `app-v3/src/ui/BrowserModel.h` (create): pure filtering and grouping (query, chip, known effects, internal types, recents)
  - `app-v3/src/ui/PluginBrowser.h`, `app-v3/src/ui/PluginBrowser.cpp` (create)
  - `app-v3/src/session/Preferences.h`, `app-v3/src/session/Preferences.cpp`: a `recentPlugins` key holding the last 8 identifiers
  - `app-v3/src/session/Session.h`, `app-v3/src/session/Session.cpp`, `app-v3/src/session/EditHistory.h`, `app-v3/src/session/EditHistory.cpp`: an optional canvas position on an add request, committed as the node's position
  - `app-v3/src/ui/SignalCanvas.h`, `app-v3/src/ui/SignalCanvas.cpp`: `DragAndDropTarget`; converts the drop point to model coordinates (drop minus view offset)
  - `app-v3/src/ui/MainWindow.cpp`: the root component becomes a `DragAndDropContainer`
  - `app-v3/tests/unit/ui/BrowserModel_test.cpp` (create), `app-v3/tests/unit/session/Preferences_test.cpp`, `app-v3/tests/unit/session/EditHistory_test.cpp`
- **Approach:**
  - Chips come from `PluginDescription::category`, plus Built-in.
  - Rows show name and format.
  - Instruments stay out, matching `getKnownEffects`.
  - An add from the browser records the plugin in recents.
  - A drop at a position is still a normal add for undo. The committed add carries the position, so no second edit is recorded.
- **Test scenarios:**
  - search matches name and vendor, case-insensitively
  - a chip filters by category
  - recents are ordered, de-duplicated and capped at 8
  - an add with a position commits at that position, and undo removes it
- **Verification:** ctest green. By hand, drag three plugins onto chosen spots, then double-click one to add it at the end.

### U6. Panels: sidebar frame, icon strips and status-bar toggles

- **Goal:** Lay out the main window body as left panel | canvas | right panel.
  - Below about 1200 px, both panels fold to 48 px icon strips; a strip click slides its panel over the canvas.
  - A three-bar toggle at each panel's outer top corner collapses it to its strip and opens it again, at any window size; File › Patch Bank and Plugins › Plugin Browser do the same. The choice persists.
- **Requirements:** KD3
- **Dependencies:** U4
- **Files:**
  - `app-v3/src/ui/BodyLayout.h` (create): pure rectangles for the window width and the panels' open / folded / hidden state
  - `app-v3/src/ui/IconRail.h`, `app-v3/src/ui/IconRail.cpp` (create): the left strip (patches icon, previous / current / next PC) and the right strip (browser, mappings)
  - `app-v3/src/ui/MainWindow.cpp`: RootComponent layout through `BodyLayout`; the viewport takes the centre
  - `app-v3/src/ui/TransportBar.h`, `app-v3/src/ui/TransportBar.cpp`: File › Patch Bank
  - `app-v3/src/session/Preferences.h`, `app-v3/src/session/Preferences.cpp`: a `panels` key
  - `app-v3/tests/unit/ui/BodyLayout_test.cpp` (create), `app-v3/tests/unit/session/Preferences_test.cpp`
- **Approach:**
  - The canvas re-centres through its existing `parentSizeChanged`, so the board stays centred as panels open and fold.
  - A slid-open panel overlays the canvas and closes on Escape or a click outside.
  - The right slot is empty until U5 lands.
- **Test scenarios:**
  - at 1280 wide, both panels are open
  - at 960, both fold to 48
  - a hidden panel yields no rectangle
  - the canvas always gets the remainder, never a negative width
- **Verification:** ctest green. By hand, drag the window from 1280 to 960 and back.

### U7. Plugin card redesign

- **Goal:** The mockup's 170×92 card:
  - a header row with a grip, the name, and BYP / M / ED
  - Remove shown only on hover
  - the body line: category, then format · vendor, or the mapping count
  - a bypassed look: dimmed, struck-through name, amber BYP, and dashed outgoing cables
  - a selected state
  - a bottom strip that lights cyan when a mapped control moves the plugin
  - a VU Meter card that shows its live meter
- **Requirements:** mockup card states
- **Dependencies:** U3 (activity)
- **Files:**
  - `app-v3/src/ui/PluginCard.h`, `app-v3/src/ui/PluginCard.cpp`
  - `app-v3/src/ui/SignalCanvas.h`, `app-v3/src/ui/SignalCanvas.cpp`:
    - card metrics (height 92) through `canvas::Metrics`
    - dashed strokes for a bypassed source
    - click selection, cleared by clicking empty canvas
  - `app-v3/src/engine/processors/VuMeterProcessor.h`: read access to its levels, if not already exposed as atomics
- **Approach:**
  - Ports move with the new height (`getPortCentre`).
  - Selection is canvas state. U9 reads it.
  - The VU card polls the processor's atomic levels at ~30 Hz, and only while visible.
- **Test scenarios:**
  - `CanvasLayout` cases with the new metrics
  - the rest by hand
- **Verification:** by hand, each mockup state: normal, hover, bypassed, VU, MIDI activity and selected.

### U8. Rename a node on its card

- **Goal:** Double-click a card's name to edit it in place. Enter keeps the name, Escape cancels, and an empty name reverts to the plugin's own.
- **Requirements:** #23
- **Dependencies:** U7
- **Files:**
  - `app-v3/src/ui/PluginCard.h`, `app-v3/src/ui/PluginCard.cpp`: an in-place `TextEditor`
  - `app-v3/src/ui/SignalCanvas.cpp`: commit through `EditHistory::setNodeCustomName`
- **Approach:**
  - Renames are undoable. EditHistory already records them.
  - While editing, the card's key presses stay local, so Ctrl+Z inside the text field edits the text, not the board.
- **Test scenarios:** EditHistory's rename round-trip already covers the model. The rest is by hand.
- **Verification:** by hand, rename, undo and redo. Enter, Escape and empty behave as described.

### U9. Board-wide mappings panel

- **Goal:** A panel docked under the canvas (about 196 px) that lists every MIDI and OSC mapping on the board:
  - MIDI and OSC tabs with counts
  - columns for Plugin, Source, Controls, Range, Latch/Invert, and delete
  - the selected card's rows highlighted
  - "+ Mapping" for the selected card
- **Requirements:** KD4
- **Dependencies:** U6 (toggle), U7 (selection)
- **Files:**
  - `app-v3/src/ui/MappingRows.h` (create): pure rows from a `Patch`, sorted by the node's order in the chain, with a "selected" flag
  - `app-v3/src/ui/MappingsPanel.h`, `app-v3/src/ui/MappingsPanel.cpp` (create): reuses the row editors from `MappingsDialog` where they fit
  - `app-v3/src/ui/MainWindow.cpp`: dock it below the viewport
  - `app-v3/tests/unit/ui/MappingRows_test.cpp` (create)
- **Approach:**
  - Editing a row rewrites that node's mappings through `Session::setNodeMappings`, the same path as the dialog.
  - MIDI learn stays in the per-plugin dialog. The panel's source cell has a "Learn…" button that opens the dialog on that plugin.
  - With no card selected, "+ Mapping" is disabled and its tooltip says why.
- **Test scenarios:**
  - rows for two plugins with MIDI and OSC mappings
  - the selected flag
  - the order follows the chain
  - a mapping to a removed node is excluded
- **Verification:** ctest green. By hand, edit a range in the panel and it changes in the dialog too.

### U10. Settings dialog: audio device and OSC

- **Goal:** Audio Settings becomes a Settings menu (KD10) with Audio Devices, MIDI Settings and OSC Settings dialogs and the Test Tone switch; the OSC port and Test Tone leave the top bar.
- **Requirements:** KD10
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/ui/SettingsWindow.h`, `app-v3/src/ui/SettingsWindow.cpp` (create): the device selector above an OSC section
  - `app-v3/src/ui/TransportBar.h`, `app-v3/src/ui/TransportBar.cpp`: the Settings button opens it; the OSC label, field and `applyOscPortChange` move out
- **Approach:**
  - The OSC fields keep today's behaviour: Enter or leaving the field applies; the port persists through Preferences and rebinds the live listener (`OscServer::rebind`); a bind failure shows inline on the field, not in a blocking dialog.
  - The multicast address gets the same treatment, validated with `isOscMulticastAddress`.
- **Test scenarios:** port validation, persistence and rebind are covered by the existing OscServer and Preferences tests. The dialog by hand.
- **Verification:** by hand, change the port in Settings, and an OSC sender on the new port moves a mapped control. An in-use port shows the error inline.

The status bar keeps what it shows today (state dot, chain count, error pill, device readout, version). Panel toggles live on the panels and in the File menu (KD3).

### U11. About window

- **Goal:** File > About opens a small window: "Pedalboard 3", the version, "Based on Pedalboard2 by Niall Moody. Built with JUCE.", and Close. There's no license text.
- **Requirements:** #24; KD6
- **Dependencies:** U1 (File menu)
- **Files:**
  - `app-v3/src/ui/AboutWindow.h`, `app-v3/src/ui/AboutWindow.cpp` (create)
  - `app-v3/src/ui/TransportBar.cpp`: File > About
- **Approach:**
  - The version comes from `JUCE_APPLICATION_VERSION_STRING`.
  - On macOS, File > About stands in until the menu bar (#25).
  - No "Source code" button until the open-source release; the repository is private (KD6).
- **Test scenarios:** UI by hand.
- **Verification:** by hand, the window opens once, closes, and shows the build's version.

### U12. Canvas zoom and Fit (deferred)

- **Goal:** Zoom −/+ and Fit in the canvas corner.
- **Status:** deferred per KD9. It's listed so it isn't lost. It would add a scale to `canvas::Layout`, scale the cards, cables and hit tests, and persist the zoom per window.

---

## Verification Contract

- ctest green on Linux CI and inside `build:windows` for U2, U3, U4, U5, U6 and U9. Their pure models are covered by unit tests.
- Desktop pass at 1280×800 and 960×540 against the mockup, for every unit.
- Real-time safety:
  - no new work on the audio callback
  - the only counters touched off the message thread are relaxed atomics
  - the CPU meter reads `getCpuUsage` from a message-thread timer

## Risks

- **Drag-and-drop from the browser (U5).** JUCE drag-and-drop needs the root component as the container and the canvas as the target. A drop has to turn into a positioned add without recording a second undo step, so the position rides on the add request.
- **Layout at 960×540 (U1, U6).** The top bar's left and right clusters plus the patch strip must fit in 960 px. The patch name is the part that gives way, as it does today. Test at the minimum size in every MR.
- **Card height change (U7).** Cards drop from 140 to 92 px. Saved positions stay valid, since `CanvasLayout` centres the group, but port positions and cable anchors move.
- **Activity counters (U3, U7).** If a future input edge calls ControlHub off the message thread, the counters must become atomics (KD8).

## Delivery

MRs through `ship-mr`, in this order:

1. **Top bar:** U1. Merged.
2. **Top bar right end, Settings and About:** U2, U3, U10, U11. Closes #22 and #24.
3. **Sidebar frame and bank:** U4, U6.
4. **Plugin browser:** U5, isolated because of the drag-and-drop risk.
5. **Cards:** U7, U8. Closes #23.
6. **Mappings panel:** U9.

U12 stays deferred.

## Resolved Questions (2026-09-11)

1. The OSC port field moves into the Settings dialog (KD10, U10).
2. The About window's "Source code" button stays hidden until the open-source release (KD6, U11).
3. Canvas zoom and Fit are deferred to 1.x (KD9, U12).
