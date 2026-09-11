#pragma once

#include <juce_graphics/juce_graphics.h>

namespace pedalboard3::ui::body
{

// The main window body's geometry (UI refresh U6, plan KD3), free of
// components so the headless tests can cover it: left panel | canvas | right
// panel. A panel is hidden (no room), collapsed to a 48 px icon strip, or open.
// Below `foldBelow` px wide an open panel folds to its strip too. The canvas
// gets what is left and never goes negative.

struct Metrics
{
    int leftWidth;
    int rightWidth;
    int stripWidth;
    int foldBelow;
};

inline constexpr Metrics kDefaultMetrics{248, 256, 48, 1200};

enum class Panel
{
    Hidden,
    Collapsed,
    Open
};

struct Layout
{
    juce::Rectangle<int> left; // empty when the panel is hidden
    juce::Rectangle<int> right;
    juce::Rectangle<int> canvas;
    bool leftFolded{false}; // shown as an icon strip
    bool rightFolded{false};
    bool narrow{false}; // below the fold width
};

inline Layout layout(juce::Rectangle<int> body, Panel left, Panel right, const Metrics& m = kDefaultMetrics)
{
    Layout out;
    out.narrow = body.getWidth() < m.foldBelow;

    const auto widthOf = [&out, &m](Panel panel, int openWidth, bool& folded)
    {
        folded = panel == Panel::Collapsed || (panel == Panel::Open && out.narrow);
        if (panel == Panel::Hidden)
            return 0;
        return folded ? m.stripWidth : openWidth;
    };

    auto area = body;
    out.left = area.removeFromLeft(widthOf(left, m.leftWidth, out.leftFolded)); // Rectangle clamps
    out.right = area.removeFromRight(widthOf(right, m.rightWidth, out.rightFolded));
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
