#pragma once

#include "ColourScheme.h"
#include "Icons.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

// A square button that shows a stroked outline icon instead of a label
// (#90). It is a TextButton so that Pb3LookAndFeel draws exactly the same
// background as the text buttons beside it — including the outline-button
// property and the toggled-on wash — and only the label is replaced.
//
// The name passed to the constructor is the accessible name, and it is also
// the obvious tooltip; a caller that wants a longer one calls setTooltip
// afterwards.
class IconButton : public juce::TextButton
{
public:
    IconButton(juce::Path icon, const juce::String& name, float inset = 10.0f)
        : juce::TextButton(name), icon_(std::move(icon)), inset_(inset)
    {
        setTooltip(name);
    }

    // For a button whose icon is its state, like the master mute (#106).
    void setIcon(juce::Path icon)
    {
        icon_ = std::move(icon);
        repaint();
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        getLookAndFeel().drawButtonBackground(
            g,
            *this,
            findColour(getToggleState() ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId),
            shouldDrawButtonAsHighlighted,
            shouldDrawButtonAsDown);

        auto colour =
            findColour(getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        if (!isEnabled())
            colour = colour.withMultipliedAlpha(0.4f);
        g.setColour(colour);
        strokeIcon(g, icon_, getLocalBounds().toFloat().reduced(inset_));
    }

private:
    juce::Path icon_;
    float inset_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IconButton)
};

} // namespace pedalboard3::ui
