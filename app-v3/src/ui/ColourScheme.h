#pragma once

#include <juce_graphics/juce_graphics.h>

namespace pedalboard3::ui::tokens
{

// Pedalboard 3 colour tokens — mapped from the Vercel design mockup at
// vercel-ui-demo/app/globals.css (oklch source values), pre-converted to
// sRGB hex. Values are approximate visually but stable and addressable by
// name from any UI code; Phase 3 canvas pulls from the same source.
//
// The conversion was done once by hand — adding new tokens means computing
// the sRGB approximation once and committing it as a literal here. There
// is intentionally no runtime oklch→sRGB conversion: tokens are constants.
//
// Naming convention: dotted CSS names from the mockup table (e.g.
// `--background`) translate to flat camelCase (`bgWindow`). The dotted
// form is documentation only — these are the canonical C++ identifiers.
//
// Phase 1.D.3 → Phase 3 staging: this header defines the core dialog/
// button/text tokens. Canvas-specific cable/port tokens land alongside
// SignalCanvas in Unit 5. Master-plan §4.2.D 19-token list is preserved
// in spirit; surface grows as new UI components need it.

// Backgrounds ---------------------------------------------------------------

inline const juce::Colour bgWindow{0xff1a1b22};    //  oklch(0.12 0.01 250)
inline const juce::Colour bgCard{0xff232530};      //  oklch(0.16 0.01 250)
inline const juce::Colour bgPopover{0xff1f2128};   //  oklch(0.14 0.01 250)
inline const juce::Colour bgSecondary{0xff2e303b}; //  oklch(0.22 0.01 250)
inline const juce::Colour bgMuted{0xff2a2c37};     //  oklch(0.20 0.01 250)
inline const juce::Colour bgSidebar{0xff16171e};   //  oklch(0.10 0.01 250)

// Accents -------------------------------------------------------------------

inline const juce::Colour accentPrimary{0xff1fcc9a};     // oklch(0.72 0.19 160) teal
inline const juce::Colour accentDestructive{0xffd14040}; // oklch(0.60 0.20 25)  red
inline const juce::Colour accentWarning{0xffd4b040};     // oklch(0.75 0.18 80)  amber
inline const juce::Colour accentInfo{0xff5cb0d6};        // oklch(0.65 0.15 200) cyan

// Foreground / text ---------------------------------------------------------

inline const juce::Colour fgPrimary{0xfff2f2f2}; // oklch(0.95 0 0)
inline const juce::Colour fgMuted{0xff8b8b8b};   // oklch(0.60 0 0)
inline const juce::Colour fgInverse{0xff1a1b22}; // = bgWindow, used on accent fills

// Borders / dividers --------------------------------------------------------

inline const juce::Colour borderDefault{0xff3e404b}; // oklch(0.28 0.01 250)
inline const juce::Colour borderMuted{0xff2e303b};   // = bgSecondary

// Status-dot colours for StatusBar (Unit 6 consumer) ------------------------

inline const juce::Colour statusIdle = accentPrimary;      // green/teal — running
inline const juce::Colour statusBuilding = accentWarning;  // amber — async build in flight
inline const juce::Colour statusSwapping = accentInfo;     // cyan — graph swap in progress
inline const juce::Colour statusError = accentDestructive; // red — lastError surfaced

} // namespace pedalboard3::ui::tokens
