#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui::meter
{

// One line of the top bar's meter stacks: a caption, a track with a fill, an
// optional held-peak marker and warning tick, and an optional number.
//
// There are four of these now -- IN and OUT in one stack, CPU and MEM in the
// other (#117, #118) -- and they have to read as one instrument rather than
// four widgets that happen to be adjacent. Pulling the drawing out here is
// what keeps the track height, the corner radius, the peak marker and the
// baseline identical across both stacks without either component knowing
// about the other.
//
// Geometry is caller-supplied so a narrow stack can give its whole width to
// the track and a wide one can afford a number column.
struct Row
{
    juce::String caption;
    float fill{0.0f}; // 0..1
    float peak{0.0f}; // 0..1; 0 draws no marker
    float tick{0.0f}; // 0..1; 0 draws no tick
    juce::Colour bar;
    juce::String number;     // empty draws no number column
    juce::Colour numberText; // ignored when `number` is empty
    bool active{true};       // false greys the caption and leaves the track empty
};

// The height one row wants. Two of them plus the gap is what a stack asks of
// the bar.
inline constexpr int kRowHeight = 16;
inline constexpr int kRowGap = 4;
inline constexpr int kStackHeight = kRowHeight * 2 + kRowGap;

void drawRow(juce::Graphics& g, juce::Rectangle<int> area, int captionWidth, int numberWidth, const Row& row);

} // namespace pedalboard3::ui::meter
