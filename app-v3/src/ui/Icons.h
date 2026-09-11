#pragma once

#include <juce_graphics/juce_graphics.h>

namespace pedalboard3::ui
{

// The patch bank's three-bar toggle (UI refresh U6): the same icon on the open
// sidebar, where it collapses the panel, and on its strip, where it opens it.
inline juce::Path bankIcon()
{
    juce::Path bars;
    bars.addRoundedRectangle(0.0f, 0.0f, 14.0f, 2.5f, 1.0f);
    bars.addRoundedRectangle(0.0f, 5.5f, 14.0f, 2.5f, 1.0f);
    bars.addRoundedRectangle(0.0f, 11.0f, 14.0f, 2.5f, 1.0f);
    return bars;
}

} // namespace pedalboard3::ui
