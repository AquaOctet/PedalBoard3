#pragma once

#include "MeterMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::session
{
class ControlHub;
} // namespace pedalboard3::session

namespace pedalboard3::ui
{

// The top bar's MIDI and OSC lights (UI refresh U3): each blinks for about
// 120 ms when a message of its kind arrives, mapped or not. Polls ControlHub's
// activity counters on a 30 Hz timer (plan KD8: activity is counted, not
// pushed).
class ActivityLights : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ActivityLights(session::ControlHub& hub);
    ~ActivityLights() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    session::ControlHub& hub_;
    meter::ActivityBlink midiBlink_;
    meter::ActivityBlink oscBlink_;
    bool midiLit_{false};
    bool oscLit_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActivityLights)
};

} // namespace pedalboard3::ui
