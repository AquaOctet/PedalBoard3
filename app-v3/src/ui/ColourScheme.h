#pragma once

#include <juce_graphics/juce_graphics.h>

namespace pedalboard3::ui::tokens
{

// Pedalboard 3 colour tokens — a DAW's palette, not a web app's (#105).
//
// The values come from the Ableton Live 12 theme
// `custom-configs/colorful/ableton/cyberpunk.ask` (236 entries, by Poelyte):
// deep violet-navy surfaces, cyan-green for what is playing, hot pink for what
// is chosen, and a cool blue-grey text ramp. Each token below names the
// Ableton role it was taken from, so the mapping can be checked against the
// theme file rather than taken on trust.
//
// `vercel-ui-demo/` is still the source for *layout* — where things sit, how
// much room they take, which of them are icons. It is no longer the source for
// colour. The token names and the light-to-dark ordering of each ramp are
// unchanged from that mapping, so re-pointing them re-themes the whole app:
// every colour in `app-v3/src/` comes from a name here, bar three
// `Colours::black` drop shadows, which are correct in any theme.
//
// Naming convention: flat camelCase. Adding a token means picking the Ableton
// role it belongs to and committing the literal — there is no runtime theme
// loading, and these are constants.

// Backgrounds ---------------------------------------------------------------
// Darkest to lightest. The order matters more than any single value: panels
// read as layered because each sits a step above the one behind it.

inline const juce::Colour bgTitleBar{0xff0c0b14};  // ControlContrastFrame — dialog title bars
inline const juce::Colour bgSidebar{0xff141322};   // SurfaceArea — sidebars, the status bar
inline const juce::Colour bgWindow{0xff1c1a30};    // Desktop — the canvas backdrop
inline const juce::Colour bgPopover{0xff1e1c34};   // TakeLaneTrackNotHighlighted — menus, popovers
inline const juce::Colour bgMuted{0xff221f3a};     // BrowserBar — search fields, muted fills
inline const juce::Colour bgCard{0xff262242};      // DetailViewBackground — plugin cards, the top bar
inline const juce::Colour bgSecondary{0xff2f2b4f}; // SurfaceHighlight — buttons, chips

// Accents -------------------------------------------------------------------
// Ableton splits "playing" from "chosen", and so do we: accentPrimary is the
// live/active/running colour (a lit toggle, a running chain, a signal cable),
// accentSelected is the picked-out one (a selection frame, the current row).

inline const juce::Colour accentPrimary{0xff43fdd5};     // ChosenPlay — cyan-green
inline const juce::Colour accentSelected{0xffff428e};    // ChosenDefault / SelectionFrame — hot pink
inline const juce::Colour accentDestructive{0xffff1767}; // ChosenRecord — red
inline const juce::Colour accentWarning{0xffffb000};     // LearnKey — amber
inline const juce::Colour accentInfo{0xff8ce1e7};        // LoopColor — pale cyan

// Foreground / text ---------------------------------------------------------

inline const juce::Colour fgPrimary{0xfff4f1fa}; // SelectionForeground — body text
inline const juce::Colour fgMuted{0xff94b4c4};   // PianoWhiteKey — secondary text
inline const juce::Colour fgInverse{0xff100f1a}; // ControlOnForeground — text on an accent fill

// The finer shades (UI chrome plan 2026-09-13-002, U3 on). Ableton uses fewer
// text colours than the mockup did, so several of these now share a value;
// they are kept apart because they mark different roles, and a later tweak to
// one should not drag the others with it.

inline const juce::Colour fgMenu{0xffb4dae9};          // ControlForeground — menu items
inline const juce::Colour fgSubtle{0xff5a6470};        // RangeDisabled — headers, accessories, disabled items
inline const juce::Colour fgTitle{0xfff4f1fa};         // SelectionForeground — dialog titles
inline const juce::Colour fgSecondaryText{0xffb4dae9}; // RetroDisplayTitle — alert messages
inline const juce::Colour fgField{0xffb4dae9};         // ControlOffForeground — text in fields

// Borders / dividers --------------------------------------------------------

inline const juce::Colour borderDefault{0xff38335c}; // ViewControlOff
inline const juce::Colour borderMuted{0xff2f2b4f};   // SurfaceHighlight — = bgSecondary, as before

// Status-dot colours for StatusBar -------------------------------------------

inline const juce::Colour statusIdle = accentPrimary;      // cyan-green — running
inline const juce::Colour statusBuilding = accentWarning;  // amber — async build in flight
inline const juce::Colour statusSwapping = accentInfo;     // pale cyan — graph swap in progress
inline const juce::Colour statusError = accentDestructive; // red — lastError surfaced

} // namespace pedalboard3::ui::tokens
