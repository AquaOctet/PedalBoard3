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

// The speaker cone, on its own. The device badge's speaker and the master
// mute button (#106) both draw it, so it lives here once and the two cannot
// drift apart. Unscaled — its callers scale the finished icon.
inline juce::Path speakerConeIcon()
{
    juce::Path p;
    p.startNewSubPath(2.0f, 9.0f);
    p.lineTo(6.0f, 9.0f);
    p.lineTo(11.0f, 4.0f);
    p.lineTo(11.0f, 20.0f);
    p.lineTo(6.0f, 15.0f);
    p.lineTo(2.0f, 15.0f);
    p.closeSubPath();
    return p;
}

// The two waves that turn the cone into lucide's "volume-2".
inline juce::Path addVolumeWaves(juce::Path p)
{
    p.startNewSubPath(15.5f, 9.0f);
    p.quadraticTo(18.0f, 12.0f, 15.5f, 15.0f);
    p.startNewSubPath(18.5f, 6.5f);
    p.quadraticTo(22.5f, 12.0f, 18.5f, 17.5f);
    return p;
}

// lucide "volume-2": the output device, in the badge.
inline juce::Path speakerIcon()
{
    return scaledToIconBox(addVolumeWaves(speakerConeIcon()));
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

// The top bar's menu icons (#90, "less text, more icons"). The UI demo has no
// File or Plugins menu to copy, so these follow lucide's drawing rules — a
// 24-unit grid, outlines, no fill — and borrow its vocabulary where it has a
// word for the thing: `plug` for plugins, `settings` for settings, `activity`
// for the latency readout.

// lucide "folder": the File menu is New / Open / Save / Save As.
inline juce::Path folderIcon()
{
    juce::Path p;
    p.startNewSubPath(3.0f, 19.0f);
    p.lineTo(3.0f, 5.5f);
    p.lineTo(9.5f, 5.5f);
    p.lineTo(11.5f, 8.0f);
    p.lineTo(21.0f, 8.0f);
    p.lineTo(21.0f, 19.0f);
    p.closeSubPath();
    return scaledToIconBox(p);
}

// lucide "plug": two prongs, a body and a cord.
inline juce::Path plugIcon()
{
    juce::Path p;
    p.addRoundedRectangle(6.0f, 8.0f, 12.0f, 9.0f, 4.5f, 4.5f, false, false, true, true);
    p.startNewSubPath(9.0f, 8.0f);
    p.lineTo(9.0f, 2.0f);
    p.startNewSubPath(15.0f, 8.0f);
    p.lineTo(15.0f, 2.0f);
    p.startNewSubPath(12.0f, 17.0f);
    p.lineTo(12.0f, 22.0f);
    return scaledToIconBox(p);
}

// lucide "settings": a gear. Generated rather than transcribed — the teeth
// are regular, so a loop says what it is and a 40-point path literal does
// not. Four vertices per tooth: up to the tip, along the flat top, back down,
// then round the gap to the next one.
inline juce::Path gearIcon()
{
    constexpr int teeth = 8;
    constexpr float cx = 12.0f, cy = 12.0f, rRoot = 7.4f, rTip = 10.4f;
    const float step = juce::MathConstants<float>::twoPi / static_cast<float>(teeth);

    juce::Path p;
    p.setUsingNonZeroWinding(false);
    for (int i = 0; i < teeth; ++i)
    {
        const float a = static_cast<float>(i) * step;
        const auto at = [&](float frac, float r)
        { return juce::Point<float>(cx + r * std::cos(a + frac * step), cy + r * std::sin(a + frac * step)); };

        const auto root0 = at(0.00f, rRoot);
        if (i == 0)
            p.startNewSubPath(root0);
        else
            p.lineTo(root0);
        p.lineTo(at(0.12f, rTip));
        p.lineTo(at(0.38f, rTip));
        p.lineTo(at(0.50f, rRoot));
    }
    p.closeSubPath();
    p.addEllipse(cx - 3.4f, cy - 3.4f, 6.8f, 6.8f); // the hub, a hole under even-odd
    return scaledToIconBox(p);
}

// lucide "activity": the heartbeat line, which is what the demo puts beside
// its latency readout.
inline juce::Path activityIcon()
{
    juce::Path p;
    p.startNewSubPath(2.0f, 12.0f);
    p.lineTo(6.0f, 12.0f);
    p.lineTo(9.0f, 3.5f);
    p.lineTo(15.0f, 20.5f);
    p.lineTo(18.0f, 12.0f);
    p.lineTo(22.0f, 12.0f);
    return scaledToIconBox(p);
}

// lucide "volume-x": the master mute button when it is on (#106). The cone is
// the same one the unmuted button and the device badge draw; muted swaps the
// two waves for a cross, so the state reads in the shape and not only in the
// colour.
inline juce::Path volumeMutedIcon()
{
    auto p = speakerConeIcon();
    p.startNewSubPath(16.0f, 9.0f);
    p.lineTo(22.0f, 15.0f);
    p.startNewSubPath(22.0f, 9.0f);
    p.lineTo(16.0f, 15.0f);
    return scaledToIconBox(p);
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
