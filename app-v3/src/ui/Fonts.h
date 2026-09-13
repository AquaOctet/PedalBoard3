#pragma once

#include <juce_graphics/juce_graphics.h>

namespace pedalboard3::ui::fonts
{

// The mockup's typefaces, embedded in the app (UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, KD1; all SIL OFL, licences
// in app-v3/resources/fonts/):
//   - Barlow Semi Condensed: the UI's text
//   - Barlow Condensed: uppercase captions and display text
//   - JetBrains Mono: numbers, format tags and readouts
//
// Sizes are point heights, as the mockup's CSS font-size: ui(14) is its 14 px.
// Pb3LookAndFeel maps JUCE's default sans-serif and monospaced fonts onto
// these, so plain juce::FontOptions text follows too.

enum class Weight
{
    Regular,
    Medium,
    SemiBold,
    Bold,
};

// Barlow Semi Condensed.
juce::Font ui(float pointHeight, Weight weight = Weight::Regular);

// Barlow Condensed, letter-spaced as the mockup's captions (0.12 em). The
// caller upper-cases the text.
juce::Font caption(float pointHeight, Weight weight = Weight::SemiBold);

// Barlow Condensed Bold, for display text (the About name).
juce::Font display(float pointHeight);

// JetBrains Mono.
juce::Font mono(float pointHeight, Weight weight = Weight::Regular);

// The embedded typefaces themselves (the LookAndFeel's getTypefaceForFont).
// Barlow Semi Condensed has every weight; Barlow Condensed has Medium and up;
// JetBrains Mono has Regular and Medium. A missing weight gives the nearest.
juce::Typeface::Ptr uiTypeface(Weight weight);
juce::Typeface::Ptr captionTypeface(Weight weight);
juce::Typeface::Ptr monoTypeface(Weight weight);

} // namespace pedalboard3::ui::fonts
