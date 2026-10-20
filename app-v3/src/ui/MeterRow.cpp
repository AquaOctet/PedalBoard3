#include "MeterRow.h"

#include "ColourScheme.h"
#include "Fonts.h"

namespace pedalboard3::ui::meter
{

namespace
{
constexpr float kTrackHeight = 7.0f;
constexpr float kTrackRadius = 3.0f;
constexpr float kCaptionSize = 10.0f;
constexpr float kNumberSize = 10.0f;
constexpr int kTrackInset = 6; // either side of the track
} // namespace

void drawRow(juce::Graphics& g, juce::Rectangle<int> area, int captionWidth, int numberWidth, const Row& row)
{
    auto caption = area.removeFromLeft(captionWidth);
    g.setFont(fonts::ui(kCaptionSize, fonts::Weight::Medium));
    g.setColour(row.active ? tokens::fgMuted : tokens::fgSubtle);
    g.drawText(row.caption, caption, juce::Justification::centredLeft);

    if (numberWidth > 0)
    {
        auto number = area.removeFromRight(numberWidth);
        if (row.number.isNotEmpty())
        {
            // Mono, so a figure that changes every tick does not shuffle the
            // ones either side of it.
            g.setFont(fonts::mono(kNumberSize));
            g.setColour(row.active ? row.numberText : tokens::fgSubtle);
            g.drawText(row.number, number, juce::Justification::centredRight);
        }
    }

    const auto track =
        area.reduced(kTrackInset, 0)
            .toFloat()
            .withSizeKeepingCentre(static_cast<float>(juce::jmax(0, area.getWidth() - kTrackInset * 2)), kTrackHeight);
    if (track.getWidth() <= 0.0f)
        return;

    g.setColour(tokens::bgMuted);
    g.fillRoundedRectangle(track, kTrackRadius);

    if (!row.active)
        return;

    if (row.fill > 0.0f)
    {
        g.setColour(row.bar);
        g.fillRoundedRectangle(track.withWidth(track.getWidth() * juce::jlimit(0.0f, 1.0f, row.fill)), kTrackRadius);
    }

    if (row.tick > 0.0f)
    {
        const float x = track.getX() + track.getWidth() * juce::jlimit(0.0f, 1.0f, row.tick);
        g.setColour(tokens::borderDefault);
        g.drawVerticalLine(juce::roundToInt(x), track.getY() - 1.0f, track.getBottom() + 1.0f);
    }

    if (row.peak > 0.0f)
    {
        const float x = track.getX() + track.getWidth() * juce::jlimit(0.0f, 1.0f, row.peak);
        g.setColour(tokens::fgMuted);
        g.drawVerticalLine(juce::roundToInt(x), track.getY(), track.getBottom());
    }
}

} // namespace pedalboard3::ui::meter
