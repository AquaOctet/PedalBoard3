---
title: UI chrome — menus, dialogs, alerts and Audio Settings
type: feat
date: 2026-09-13
topic: ui-chrome
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: the issue #32 design pass and its mockup (https://claude.ai/code/artifact/0cfe3e05-a5b3-4fe7-bc98-f41e5117e6c2, private), vercel-ui-demo/components/audio-settings-dialog.tsx for Audio Settings, and the user's decisions of 2026-09-13 (Key Decisions)
execution: code
---

# UI chrome — menus, dialogs, alerts and Audio Settings - Plan

## Goal Capsule

- **Objective:** Everything that opens over the main window looks like the #32 mockup: menus and submenus, dialogs, alerts, dropdowns, tabs, tables, and the Audio Settings window.
  - The UI refresh (plan 2026-09-11-004) restyled the panels, cards, bank, browser and top bar.
  - Everything that pops up is still JUCE's stock LookAndFeel_V4 drawing with the palette swapped in. The user flagged it on 2026-09-13: "some of the menus, dialogs, and sub menus are still dated or look wrong… The Audio Hardware device doesn't look nice either."
- **Product authority:**
  - The #32 mockup wins where it and the older Vercel prototype disagree.
  - Colours stay exactly as `app-v3/src/ui/ColourScheme.h` defines them. New tokens only name greys the mockup already uses.
- **Tracking:** milestone "UI Refresh (pre-beta)" (iid 4). Issues use marker units `UC-U<N>` (labels `uc-u<N>`), so they don't collide with plan 004's `u<N>`.
- **Out of scope:**
  - canvas zoom, which stays deferred (plan 004 KD9)
  - a custom main-window title bar (KD2)
  - device gain controls (KD3)
  - native file choosers, which stay OS dialogs
  - plugin editor windows, which keep the native frame, since third-party UIs size themselves
- **Stop conditions:** stop and surface if a unit needs a colour the palette doesn't have, or if it would change behaviour (what a menu item does, what a dialog saves) rather than appearance.

---

## Key Decisions

- **KD1. Embed the mockup's fonts** (user decision, 2026-09-13).
  - The fonts, all SIL OFL:
    - Barlow Semi Condensed 400/500/600/700 for the UI
    - Barlow Condensed 500/600/700 for uppercase captions and display
    - JetBrains Mono 400/500 for numbers, tags and readouts
  - They're embedded with `juce_add_binary_data`, and their licence files sit beside them.
  - One `ui/Fonts.h` gives `ui`, `caption` and `mono` fonts. The ad-hoc `monoFont()` helpers and bare `FontOptions(size)` calls move onto it.
- **KD2. Custom title bars on dialogs, native on the main window** (user decision).
  - Dialogs and alerts get the mockup frame:
    - a 28 px `#0c0d11` title bar
    - the title left-aligned at 13 px
    - a single ✕
    - drag to move
    - the JUCE drop shadow
  - The main window keeps the OS title bar, so snapping and the taskbar behave normally.
- **KD3. Audio Settings is rebuilt** on `juce::AudioDeviceManager` (user decision).
  - It follows the prototype's layout, restyled to the mockup: driver, output device, input device, active channels as chips, sample rate, buffer size, and a latency readout.
  - Kept: Test Tone and ASIO's Control Panel button.
  - Left out: the prototype's input and output gain sliders (the engine has no device gain) and its "Exclusive mode" switch (JUCE offers WASAPI exclusive as a driver type, so it's already in the Driver list).
  - Changes apply immediately, like the Mappings dialog; the window has Close, not Apply and Cancel.
- **KD4. Alert buttons: right-aligned, main action last** (user decision). For example, "Unsaved changes" reads Don't Save · Cancel · Save.
  - The main action is a filled `--sec` button, the others are ghost buttons, and a destructive action has red text.
  - Enter runs the main action and Esc cancels.
  - No icon, as in the mockup.
- **KD5. The stock JUCE tables and editors are restyled, not rebuilt:** `PluginListComponent` and `KeyMappingEditorComponent`. They get the LookAndFeel's table headers, rows, tabs, buttons and menus, so they match without forking JUCE components.
- **KD6. Every unit shows its surfaces before and after.**
  - U1's `pb3-ui-snapshots` tool renders each menu and dialog to PNG through the real LookAndFeel.
  - Each MR attaches the images for what it changed.
  - There are no UI snapshot tests today, and the unit tests don't compile `ui/*.cpp`. The images are evidence for review, not a pixel-exact CI gate, because font rasterisation differs across platforms.

---

## Mockup spec (the target)

Summarised from the mockup's CSS. Tokens are `ColourScheme.h` names; the mockup's CSS name is in brackets.

- **Menu and submenu:**
  - **Panel:** `bgPopover` (`--pop` #1f2128), 1 px `borderDefault` (`--line` #3e404b), radius 7, padding 5, shadow, min width 208.
  - **Items:** 30 high, padding 0 10, radius 5, Barlow Semi Condensed 14/400, `#d7d8de`.
  - **Highlight:** `bgSecondary` (`--sec` #2e303b) with `fgPrimary` text. **Not teal.**
  - **Right accessory** (shortcut, format tag "VST3"/"INT"): JetBrains Mono 10, `#62646f`.
  - **Submenu marker:** ▸.
  - **Header:** Barlow Condensed 11, uppercase, tracking 0.12, `#62646f`, padding 8/10/3.
  - **Separator:** 1 px `borderDefault`, inset 6 px.
  - **Disabled:** text `#62646f`.
  - **Ticked:** a teal tick in a left gutter.
  - **The button that opened it:** tinted teal while the menu is open.
- **Dialog:**
  - **Frame:** `bgWindow`, 1 px black border, radius 8.
  - **Title bar:** 28 px `#0c0d11`; title left-aligned, 13 px, `#c7c8cf`; ✕ in `#62646f`.
  - **Body:** padding 14, 10 px between rows.
  - **Section captions:** Barlow Condensed 12/600, uppercase, tracking 0.12.
- **Controls:**
  - **Field** (combo, text): 28 high, radius 5, `bgSecondary`, no border, 12 px `#d6d7dd`; a combo shows " ▾".
  - **Buttons:** 34 high (28 small), radius 6, 14/500. Filled `bgSecondary`, or ghost (transparent with a 1 px `borderDefault`). There's no teal-filled primary.
  - **Toggle on:** `accentPrimary` at 14% alpha with teal text.
  - **Tabs:** uppercase captions with a 2 px teal underline.
  - **Scrollbar:** thin, rounded, `borderDefault`.
- **Alert:** the dialog frame and the title, a 14 px message in `#c9cad1` with names in `fgPrimary` 600, and a right-aligned button row with 8 px gaps.
- **Audio Settings** (the prototype restyled):
  - captions DRIVER, OUTPUT, INPUT and FORMAT
  - 28 px fields
  - channel chips (on: teal-dim with teal text)
  - latency in mono teal
  - the Test Tone toggle
  - a ghost Close button

---

## Implementation Units

### U1. UI snapshots

- **Goal:** One command renders every popup surface to PNG through the app's real LookAndFeel, so the other units can show before and after (KD6).
- **Requirements:** KD6
- **Dependencies:** none
- **Files:**
  - `app-v3/src/tools/pb3-ui-snapshots.cpp` (create): a console app that links the UI sources, installs `Pb3LookAndFeel`, and writes PNGs to a given folder.
    - **Menus:** File, Settings, Plugins, Add Plugin (with its Built-in submenu), Open Recent, and Patch (with section headers), from the top bar's own builders. Each is laid out and drawn with the LookAndFeel's popup-menu methods, as JUCE's menu window does, one item highlighted.
    - **Dialogs**, each as its content: Audio Devices, MIDI, OSC, the three feature settings, About, a Mappings dialog, and Plugin List. U4 adds the frame around them.
    - **Alerts:** Unsaved changes, and a message.
    - Later units add the surfaces they bring in: the bank row menu (U3), App Mappings (U7).
  - `app-v3/src/CMakeLists.txt`: the `pb3-ui-snapshots` target
  - `build.ps1`: a `snapshots` action that builds and runs it into `build/ui-snapshots/`
- **Test scenarios:** the tool writes one non-empty PNG per surface; a missing surface fails it.
- **Verification:** run it on `main` and keep the images as the "before" set.

### U2. The mockup's fonts

- **Goal:** The UI, captions and numbers use the mockup's typefaces everywhere (KD1).
- **Requirements:** KD1
- **Dependencies:** none
- **Files:**
  - `app-v3/resources/fonts/` (create): the nine TTFs plus `OFL.txt` for each family
  - `app-v3/src/CMakeLists.txt`: `juce_add_binary_data(pb3_ui_fonts …)`, linked into the app, the snapshots tool and the tests
  - `app-v3/src/ui/Fonts.h/.cpp` (create): `fonts::ui(size, weight)`, `fonts::caption(size)` (uppercase is the caller's; kerning of about 0.12 is built in) and `fonts::mono(size)`, from the embedded typefaces
  - `app-v3/src/ui/Pb3LookAndFeel.*`: `getTypefaceForFont` makes Barlow Semi Condensed the default sans, so stock JUCE text follows
  - `PluginCard.cpp`, `TempoPopover.cpp`, `TransportControls.cpp`, `ProcessorControls.cpp`, `CardText`, `AboutWindow.cpp`, `StatusBar.cpp`, `BankSidebar.cpp`, `PluginBrowser.cpp`, `BoardMappingsPanel.cpp`: their fonts move onto `ui/Fonts.h`
- **Test scenarios:** each embedded typeface loads with the expected family name, and `mono` has equal digit widths.
- **Verification:** snapshots show Barlow and JetBrains Mono. By hand: the cards, top bar and status bar read as in the mockup.

### U3. Menus and submenus

- **Goal:** Every popup menu and submenu, ComboBox lists included, looks like the mockup's `.menu`.
- **Requirements:** mockup top bar (Plugins menu and the Add Plugin submenu); KD6
- **Dependencies:** U2
- **Files:**
  - `app-v3/src/ui/Pb3LookAndFeel.*`, overriding:
    - `drawPopupMenuBackgroundWithOptions`: rounded 7 with a border and no stripes
    - `getPopupMenuBorderSizeWithOptions`: 5
    - `drawPopupMenuItemWithOptions`: 30 px rows, a radius-5 `bgSecondary` highlight, the teal tick gutter, ▸, and the right-hand accessory
    - `drawPopupMenuSectionHeaderWithOptions`
    - `getIdealPopupMenuItemSizeWithOptions`: 30, separators 9
    - `getPopupMenuFont`
    - `drawPopupMenuUpDownArrowWithOptions`
  - `app-v3/src/ui/ColourScheme.h`: `fgSubtle` #62646f, `fgMenu` #d7d8de, `bgTitleBar` #0c0d11, `fgTitle` #c7c8cf, `fgSecondaryText` #c9cad1, `fgField` #d6d7dd
  - `app-v3/src/ui/TransportBar.cpp`, `BankSidebar.cpp`:
    - menus open with a 208 px minimum width, 6 px below their button
    - Add Plugin items put the format in the accessory slot ("VST3", "INT") rather than "name [format]"
    - the Add Plugin list gets "Effects" and "Built-in" headers, as in the mockup
    - the button that opened a menu stays tinted while the menu is open
- **Test scenarios:** the Add Plugin model gives each item a name and its format tag (headless, on the existing browser model).
- **Verification:** snapshots of every menu against the mockup's Plugins menu. By hand: File, Settings, Plugins, +, Open Recent, the Patch menu, the bank row menu, and a ComboBox list.

### U4. The dialog frame and its controls

- **Goal:** Every dialog uses the mockup's frame and fields, buttons, tabs, sliders and scrollbars (KD2).
- **Requirements:** KD2; mockup Mappings dialog and About
- **Dependencies:** U2
- **Files:**
  - `app-v3/src/ui/DialogFrame.*` (create): a `juce::DocumentWindow` with the JUCE title bar at 28 px, only a close button, drag to move, and the JUCE drop shadow. Resizable dialogs keep a resize border. It replaces every `DialogWindow::LaunchOptions` and ad-hoc `DocumentWindow` for dialogs.
  - `app-v3/src/ui/Pb3LookAndFeel.*`:
    - **Frame:** `drawDocumentWindowTitleBar` and `createDocumentWindowButton` (a ✕ glyph)
    - **Fields:** `drawComboBox`, `positionComboBoxText`, `getComboBoxFont`, `fillTextEditorBackground`, `drawTextEditorOutline`
    - **Buttons and toggles:** `drawButtonText` / `getTextButtonFont`, and `drawToggleButton` (toggles as fields)
    - **Sliders and scrollbars:** `drawLinearSlider` (the value bar), `drawScrollbar`
    - **Tabs:** `drawTabButton`, `drawTabbedButtonBarBackground`
    - **Call-out and labels:** `drawCallOutBoxBackground`, `drawLabel` for captions
  - `SettingsWindow.cpp`, `FeatureSettingsWindows.cpp`, `AboutWindow.cpp`, `AppMappingsWindow.cpp`, `MappingsDialog.cpp`, and `TransportBar.cpp` (Plugin List): they move onto `DialogFrame`, their section labels become captions, and About follows the mockup (display name with a teal "3", mono version).
- **Test scenarios:** `DialogFrame`'s title-bar and close-button layout (headless, on its geometry helper).
- **Verification:** snapshots of every dialog. By hand: each dialog opens, drags, closes on ✕ and Esc, and the resizable ones resize.

### U5. Alerts

- **Goal:** Every message, confirmation and prompt uses one mockup-style alert (KD4).
- **Requirements:** KD4
- **Dependencies:** U4
- **Files:**
  - `app-v3/src/ui/Alert.*` (create), on `DialogFrame`:
    - `alert::show(title, message, buttons, onResult)`, with optional text-entry fields
    - buttons right-aligned with the main action last; filled main, ghost others, red text for destructive
    - Enter runs the main action and Esc cancels
    - no icon
  - Callers move onto it:
    - `FileActions.cpp`: messages, Unsaved changes, Save a copy
    - `MainWindow.cpp`: settings reset
    - `FeaturePrompt.cpp`
    - `TransportBar.cpp`: the crash list, scan summary, scanner missing, Rename Patch, Remove Patch
    - `TransportControls.cpp`: time signature
  - `app-v3/src/ui/Pb3LookAndFeel.*`: `drawAlertBox` (no icon, title left) for the alerts JUCE still raises itself, such as the scan progress window
- **Test scenarios:** the alert's button order and default and cancel roles for two- and three-button alerts (headless, on its layout helper).
- **Verification:** snapshots of Unsaved changes and a message. By hand: Unsaved changes on New, Open and quit; Rename, Remove, and the scan summary.

### U6. Audio Settings, rebuilt

- **Goal:** The Audio Settings window is our own panel in the mockup's style, not JUCE's device selector (KD3).
- **Requirements:** KD3; `vercel-ui-demo/components/audio-settings-dialog.tsx`
- **Dependencies:** U4
- **Files:**
  - `app-v3/src/ui/AudioSettingsPanel.*` (create), on `juce::AudioDeviceManager`:
    - **Driver:** the device types
    - **Output:** device field, channel chips as stereo pairs, and ASIO's "Control Panel…"
    - **Input:** device field, channel chips, and an input level meter
    - **Format:** sample rate and buffer size fields
    - **Latency readout:** output and input in mono teal
    - **Test Tone:** the toggle, sharing the Settings menu's value
    - **Errors:** a failed device open shows inline in red instead of JUCE's alert
    - it follows the device manager's changes
  - `app-v3/src/ui/AudioSettingsModel.h` (create): the headless part: latency text, channel-pair labels, and the buffer and rate choices formatted
  - `app-v3/src/ui/SettingsWindow.cpp`: Audio Devices opens the new panel in a `DialogFrame`
- **Test scenarios:** latency is buffer ÷ rate for each direction, rounded to 0.1 ms; channel pairs are labelled "1–2", "3–4" and a lone "5"; the buffer list keeps the device's own values.
- **Verification:** snapshots of Audio Settings. By hand, on WASAPI and ASIO:
  - switch the driver, device, rate and buffer
  - toggle channels
  - Control Panel on ASIO
  - Test Tone
  - audio keeps running after each change

### U7. Tables, lists and tabs

- **Goal:** Plugin List, the keyboard-shortcut editor and every list and table match the mockup's rows, headers and tabs (KD5).
- **Requirements:** KD5
- **Dependencies:** U4
- **Files:**
  - `app-v3/src/ui/Pb3LookAndFeel.*`: `drawTableHeaderBackground` and `drawTableHeaderColumn` (captions), list and table row colours, `drawKeymapChangeButton`, and `drawTreeviewPlusMinusBox`
  - `app-v3/src/ui/TransportBar.cpp` (Plugin List) and `AppMappingsWindow.cpp`: the row height and header height set to the mockup's
- **Test scenarios:** none headless (LookAndFeel painting).
- **Verification:** snapshots of Plugin List and App Mappings. By hand: sort, remove and blacklist in Plugin List; change a key in App Mappings.

---

## Verification Contract

- ctest green on Windows, Linux and macOS, with every job listed, the allow_failure ones included.
- Each MR attaches the before and after snapshots of the surfaces it changes (KD6).
- A desktop pass at 1280×800 against the mockup for every unit.

## Risks

- **Fonts in stock JUCE components:** `getTypefaceForFont` changes the default sans everywhere, so some stock layouts (the device selector, tables) may clip. U6 and U7 replace or tune the worst of them.
- **Custom title bars:** dialogs lose the OS's own title-bar behaviour (Aero snap, the system menu on Alt+Space). They're dialogs, so moving and closing is what matters.
- **Menu drawing APIs:** the JUCE 8 `…WithOptions` popup-menu methods are the stable override points. The non-`Options` overloads are left alone.

## MR plan

1. U1 + U2: snapshots and fonts; this plan rides along.
2. U3: menus.
3. U4: dialog frame and controls.
4. U5: alerts.
5. U6: Audio Settings.
6. U7: tables, lists and tabs.
