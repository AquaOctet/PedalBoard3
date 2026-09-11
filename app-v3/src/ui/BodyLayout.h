#pragma once

#include <juce_graphics/juce_graphics.h>

namespace pedalboard3::ui::body
{

// The main window body's geometry (UI refresh U6, plan KD3), free of
// components so the headless tests can cover it: left panel | canvas | right
// panel. Below `foldBelow` px wide, shown panels fold to icon strips; a hidden
// panel takes no room; the canvas gets what is left and never goes negative.

struct Metrics
{
    int leftWidth;
    int rightWidth;
    int stripWidth;
    int foldBelow;
};

inline constexpr Metrics kDefaultMetrics{248, 256, 48, 1200};

struct Layout
{
    juce::Rectangle<int> left; // empty when the panel is hidden
    juce::Rectangle<int> right;
    juce::Rectangle<int> canvas;
    bool folded{false}; // shown panels are icon strips
};

inline Layout layout(juce::Rectangle<int> body, bool leftShown, bool rightShown, const Metrics& m = kDefaultMetrics)
{
    Layout out;
    out.folded = body.getWidth() < m.foldBelow;
    const int leftWidth = leftShown ? (out.folded ? m.stripWidth : m.leftWidth) : 0;
    const int rightWidth = rightShown ? (out.folded ? m.stripWidth : m.rightWidth) : 0;

    auto area = body;
    out.left = area.removeFromLeft(leftWidth); // Rectangle clamps to what is there
    out.right = area.removeFromRight(rightWidth);
    out.canvas = area;
    return out;
}

// Where a folded panel slides open: over the canvas, next to its strip.
inline juce::Rectangle<int> overlayFor(const Layout& l, bool leftSide, const Metrics& m = kDefaultMetrics)
{
    const auto& strip = leftSide ? l.left : l.right;
    const int width = juce::jmin(leftSide ? m.leftWidth : m.rightWidth, l.canvas.getWidth());
    const int x = leftSide ? strip.getRight() : strip.getX() - width;
    return {x, strip.getY(), width, strip.getHeight()};
}

} // namespace pedalboard3::ui::body
