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

// The audio-device badge's icons (#90), drawn as lucide draws them: outlines
// on a 24-unit grid, stroked rather than filled, then scaled to a 14-unit box
// so they sit with 12 pt text. strokeIcon() below is how they are painted.
inline juce::Path scaledToIconBox(juce::Path p, float box = 14.0f)
{
    p.applyTransform(juce::AffineTransform::scale(box / 24.0f));
    return p;
}

// lucide "mic": the capsule, the cradle under it and the stem.
inline juce::Path micIcon()
{
    juce::Path p;
    p.addRoundedRectangle(9.0f, 2.0f, 6.0f, 14.0f, 3.0f);
    p.startNewSubPath(19.0f, 10.0f);
    p.lineTo(19.0f, 12.0f);
    p.addCentredArc(12.0f,
                    12.0f,
                    7.0f,
                    7.0f,
                    0.0f,
                    juce::MathConstants<float>::halfPi,
                    juce::MathConstants<float>::halfPi * 3.0f,
                    false);
    p.lineTo(5.0f, 10.0f);
    p.startNewSubPath(12.0f, 19.0f);
    p.lineTo(12.0f, 22.0f);
    return scaledToIconBox(p);
}

// lucide "volume-2": the cone and two waves.
inline juce::Path speakerIcon()
{
    juce::Path p;
    p.startNewSubPath(2.0f, 9.0f);
    p.lineTo(6.0f, 9.0f);
    p.lineTo(11.0f, 4.0f);
    p.lineTo(11.0f, 20.0f);
    p.lineTo(6.0f, 15.0f);
    p.lineTo(2.0f, 15.0f);
    p.closeSubPath();
    p.startNewSubPath(15.5f, 9.0f);
    p.quadraticTo(18.0f, 12.0f, 15.5f, 15.0f);
    p.startNewSubPath(18.5f, 6.5f);
    p.quadraticTo(22.5f, 12.0f, 18.5f, 17.5f);
    return scaledToIconBox(p);
}

// The status bar's three panel toggles (#90), as lucide's panel-left,
// panel-bottom and panel-right: a frame with one pane picked out. Even-odd
// winding turns the inner rectangle into a hole, and the pane rectangle,
// drawn over that hole, fills solid again.
enum class PanelSide
{
    Left,
    Bottom,
    Right
};

inline juce::Path panelSideIcon(PanelSide side)
{
    juce::Path p;
    p.setUsingNonZeroWinding(false);
    p.addRoundedRectangle(0.0f, 1.0f, 16.0f, 14.0f, 2.5f);
    p.addRoundedRectangle(1.6f, 2.6f, 12.8f, 10.8f, 1.2f);
    switch (side)
    {
    case PanelSide::Left:
        p.addRectangle(1.6f, 2.6f, 4.4f, 10.8f);
        break;
    case PanelSide::Right:
        p.addRectangle(10.0f, 2.6f, 4.4f, 10.8f);
        break;
    case PanelSide::Bottom:
    default:
        p.addRectangle(1.6f, 9.8f, 12.8f, 3.6f);
        break;
    }
    return p;
}

// Stroke an outline icon centred in `area`, the way the badge and the panel
// toggles want it: lucide's 2-unit stroke on its 24 grid, which is 1.17 at our
// 14.
//
// The icon is fitted to the largest square that fits inside `area` — the
// SMALLER of its two sides, not the height. Callers hand this a cell that is
// icon-wide but row-high, so taking the height scaled a 13 px mic up to the
// full 36 px of the top bar and ran it over the text beside it.
inline void strokeIcon(juce::Graphics& g, const juce::Path& icon, juce::Rectangle<float> area, float thickness = 1.17f)
{
    const auto side = juce::jmin(area.getWidth(), area.getHeight());
    auto placed = icon;
    placed.applyTransform(icon.getTransformToScaleToFit(area.withSizeKeepingCentre(side, side), true));
    g.strokePath(placed, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace pedalboard3::ui
