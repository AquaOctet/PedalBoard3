#pragma once

#include "MeterMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::engine
{
class AudioEngine;
} // namespace pedalboard3::engine

namespace pedalboard3::ui
{

// The top bar's audio input monitor (#117), next to the MIDI and OSC lights:
// a mic icon and a short level bar showing what is arriving at the input.
//
// It sits with the activity lights because it answers the same kind of
// question they do -- "is anything coming in" -- and because that is the
// corner of the bar a player glances at when nothing is happening and they
// want to know whose fault it is. MIDI and OSC each had a light there; the
// one input that had none was the audio.
//
// The level is read from the engine's master stage, before the board, so an
// empty board, a bypassed first plugin or a silent synth cannot make a live
// input look dead. The reading is the peak since the last look rather than
// whatever block happened to be in flight, so a transient between two frames
// is still seen.
//
// Ballistics: the bar rises instantly to a new peak and falls smoothly, which
// is what makes a meter readable at a glance, plus a peak marker held for
// about a second (the same PeakHold the CPU meter uses). Red from -3 dBFS, so
// "you are clipping the input" is visible without reading a number.
class InputMonitor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit InputMonitor(engine::AudioEngine& engine);
    ~InputMonitor() override;

    void paint(juce::Graphics&) override;

    // What the bar asks for when there is room for it.
    static constexpr int kPreferredWidth = 74;

private:
    void timerCallback() override;

    engine::AudioEngine& engine_;
    float shown_{0.0f};
    meter::PeakHold peak_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputMonitor)
};

} // namespace pedalboard3::ui
