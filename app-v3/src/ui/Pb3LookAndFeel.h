#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

// Pedalboard 3 LookAndFeel — subclass of juce::LookAndFeel_V4 that
// replaces the JUCE-default-grey aesthetic with the dark blue-gray +
// teal-accent palette from the Vercel design mockup
// (vercel-ui-demo/app/globals.css).
//
// Theming strategy
// ----------------
// JUCE's V4 LookAndFeel reads a `ColourScheme` of 9 named slots (window
// background, widget background, menu background, outline, text, fill,
// highlighted text, highlighted fill, menu text). Setting those once
// drives most JUCE classes' default `paint*` methods to the right
// colours without bespoke overrides.
//
// For the long-tail JUCE classes that ignore the V4 `ColourScheme`
// (or that the mockup wants styled differently — sliders, scrollbars,
// table headers, popup menus, toggle buttons, plugin-list view), we
// register per-class colour IDs via `setColour(ClassName::ColourId,
// tokens::…)` in the constructor.
//
// Bespoke `paint*` overrides are kept to a minimum — enabled only
// where shape changes are needed (rounded button corners, custom
// title-bar chrome). The Unit 1 baseline only overrides
// `drawButtonBackground` for rounded corners + accent border;
// everything else uses V4 defaults driven by colour mappings.
//
// Singleton lifetime
// ------------------
// One instance is owned by `App` and registered via
// `juce::LookAndFeel::setDefaultLookAndFeel(&lnf)` in `App::initialise`.
// `App::shutdown` calls `setDefaultLookAndFeel(nullptr)` BEFORE
// destroying the instance so `juce::Desktop` doesn't hold a dangling
// pointer.
class Pb3LookAndFeel : public juce::LookAndFeel_V4
{
public:
    Pb3LookAndFeel();
    ~Pb3LookAndFeel() override = default;

    // A component property: set it true on a Button to draw it as an outline
    // ("ghost") button, transparent until toggled on (UI refresh U2).
    static constexpr const char* kOutlineButton = "pb3OutlineButton";

    // Bespoke shape overrides — rounded button corners + a thin
    // accent border on default buttons. All colour decisions still
    // route through the V4 ColourScheme + per-class setColour calls
    // configured in the constructor.
    void drawButtonBackground(juce::Graphics&,
                              juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    // Filled circular tick instead of V4's rounded-rect-with-tick.
    // Used by ToggleButtons (the per-card Bypass control etc.).
    void drawTickBox(juce::Graphics&,
                     juce::Component&,
                     float x,
                     float y,
                     float w,
                     float h,
                     bool ticked,
                     bool isEnabled,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    // The mockup's embedded fonts stand in for JUCE's default sans-serif and
    // monospaced fonts (UI chrome plan U2, KD1), so plain juce::FontOptions text
    // and stock JUCE components follow them. Bold is Barlow's SemiBold.
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pb3LookAndFeel)
};

} // namespace pedalboard3::ui
