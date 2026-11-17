#pragma once

#include "ColourScheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace pedalboard3::ui
{

// One mutually exclusive choice, drawn as a joined bar (VR-U4).
//
// It replaces the browser's filter pills. Detached pills are a web pattern and
// they read as several independent switches; a DAW draws a single control with
// one lit segment, which is what "exactly one of these" looks like. The
// behaviour is the same either way -- this is about the drawing saying what the
// control already does.
//
// Segments are as wide as their text, because the options are words of very
// different lengths ("All" against "Distortion") and equal widths would either
// crop the long ones or strand the short ones in whitespace. They wrap onto
// further rows when the panel is too narrow, each row joined on its own.
class SegmentedControl : public juce::Component
{
public:
    SegmentedControl();

    // Replaces the options. The selection is kept when it is still offered;
    // otherwise the first option is selected. Does not call onChange: this is
    // the list changing underneath, not someone choosing.
    void setOptions(juce::StringArray options, const juce::String& selected);

    juce::String getSelected() const { return selected_; }

    // Called when someone picks a different segment. Never called for a click
    // on the segment already selected.
    std::function<void(juce::String)> onChange;

    // The height the options need at this width, for the panel's layout pass.
    int heightForWidth(int width) const;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    struct Segment
    {
        juce::String text;
        juce::Rectangle<int> bounds;
        bool firstInRow{false};
        bool lastInRow{false};
    };

    std::vector<Segment> layOut(int width) const;

    juce::StringArray options_;
    juce::String selected_;
    std::vector<Segment> segments_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SegmentedControl)
};

} // namespace pedalboard3::ui
