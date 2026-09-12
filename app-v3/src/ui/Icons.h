#pragma once

#include <juce_graphics/juce_graphics.h>

namespace pedalboard3::ui
{

// The side panels' three-bar toggle (UI refresh U6): on an open panel it
// collapses it, on the panel's strip it opens it. Used by the patch bank and
// the plugin browser.
inline juce::Path panelToggleIcon()
{
    juce::Path bars;
    bars.addRoundedRectangle(0.0f, 0.0f, 14.0f, 2.5f, 1.0f);
    bars.addRoundedRectangle(0.0f, 5.5f, 14.0f, 2.5f, 1.0f);
    bars.addRoundedRectangle(0.0f, 11.0f, 14.0f, 2.5f, 1.0f);
    return bars;
}

// The transport's buttons (M6 U4): play, pause, and return to zero (a bar
// with a triangle pointing back at it).
inline juce::Path playIcon()
{
    juce::Path triangle;
    triangle.addTriangle(0.0f, 0.0f, 0.0f, 14.0f, 12.0f, 7.0f);
    return triangle;
}

inline juce::Path pauseIcon()
{
    juce::Path bars;
    bars.addRoundedRectangle(0.0f, 0.0f, 4.0f, 14.0f, 1.0f);
    bars.addRoundedRectangle(8.0f, 0.0f, 4.0f, 14.0f, 1.0f);
    return bars;
}

inline juce::Path returnToZeroIcon()
{
    juce::Path icon;
    icon.addRoundedRectangle(0.0f, 0.0f, 2.5f, 14.0f, 1.0f);
    icon.addTriangle(14.0f, 0.0f, 14.0f, 14.0f, 3.5f, 7.0f);
    return icon;
}

// The Audio Recorder's Record button (M6 U7): a dot.
inline juce::Path recordIcon()
{
    juce::Path dot;
    dot.addEllipse(0.0f, 0.0f, 14.0f, 14.0f);
    return dot;
}

} // namespace pedalboard3::ui
