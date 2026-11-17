#include "SegmentedControl.h"

#include "Fonts.h"
#include "Metrics.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kHeight = metrics::kRowCompact + 4; // a shade taller than a column heading
constexpr int kTextPad = metrics::kSpaceXl;       // either side of a segment's word
constexpr int kRowGap = metrics::kSpaceSm;
constexpr float kRadius = 4.0f;

juce::Font segmentFont()
{
    return fonts::ui(11.0f, fonts::Weight::Medium);
}
} // namespace

SegmentedControl::SegmentedControl()
{
    setInterceptsMouseClicks(true, false);
}

void SegmentedControl::setOptions(juce::StringArray options, const juce::String& selected)
{
    options_ = std::move(options);
    selected_ = options_.contains(selected) ? selected : (options_.isEmpty() ? juce::String() : options_[0]);
    segments_ = layOut(getWidth());
    repaint();
}

std::vector<SegmentedControl::Segment> SegmentedControl::layOut(int width) const
{
    std::vector<Segment> segments;
    if (options_.isEmpty() || width <= 0)
        return segments;

    const auto font = segmentFont();
    int x = 0;
    int y = 0;
    size_t firstOfRow = 0;

    for (const auto& text : options_)
    {
        const int w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, text)) + 2 * kTextPad;

        if (x > 0 && x + w > width)
        {
            segments[segments.size() - 1].lastInRow = true;
            firstOfRow = segments.size();
            x = 0;
            y += kHeight + kRowGap;
        }

        segments.push_back({text, {x, y, w, kHeight}, segments.size() == firstOfRow, false});
        x += w;
    }

    if (!segments.empty())
        segments.back().lastInRow = true;
    return segments;
}

int SegmentedControl::heightForWidth(int width) const
{
    const auto segments = layOut(width);
    return segments.empty() ? 0 : segments.back().bounds.getBottom();
}

void SegmentedControl::resized()
{
    segments_ = layOut(getWidth());
}

void SegmentedControl::paint(juce::Graphics& g)
{
    const auto font = segmentFont();

    for (const auto& segment : segments_)
    {
        const auto bounds = segment.bounds.toFloat();
        const bool on = segment.text == selected_;

        // Rounded only where the row ends, so the middle segments butt up
        // against their neighbours and the row reads as one control.
        juce::Path shape;
        shape.addRoundedRectangle(bounds.getX(),
                                  bounds.getY(),
                                  bounds.getWidth(),
                                  bounds.getHeight(),
                                  kRadius,
                                  kRadius,
                                  segment.firstInRow,
                                  segment.lastInRow,
                                  segment.firstInRow,
                                  segment.lastInRow);

        g.setColour(on ? tokens::accentPrimary : tokens::bgSecondary);
        g.fillPath(shape);

        if (!segment.lastInRow)
        {
            // The divider between neighbours. It sits on the panel's own
            // background rather than a lighter line, so an unselected run
            // looks like one bar with grooves in it.
            g.setColour(tokens::bgSidebar);
            g.fillRect(bounds.getRight() - 1.0f, bounds.getY(), 1.0f, bounds.getHeight());
        }

        g.setColour(on ? tokens::fgInverse : tokens::fgMuted);
        g.setFont(font);
        g.drawText(segment.text, segment.bounds, juce::Justification::centred, false);
    }
}

void SegmentedControl::mouseDown(const juce::MouseEvent& e)
{
    for (const auto& segment : segments_)
    {
        if (!segment.bounds.contains(e.getPosition()))
            continue;
        if (segment.text == selected_)
            return; // already chosen: a mutually exclusive filter has nothing to toggle

        selected_ = segment.text;
        repaint();
        if (onChange)
            onChange(selected_);
        return;
    }
}

} // namespace pedalboard3::ui
