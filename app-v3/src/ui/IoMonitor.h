#pragma once

#include "MeterMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::engine
{
class AudioEngine;
} // namespace pedalboard3::engine

namespace pedalboard3::ui
{

// The top bar's IN and OUT meters (#117), next to the MIDI and OSC lights:
// two stacked level bars for the audio coming in and the audio going out.
//
// They sit with the activity lights because they answer the same kind of
// question those do -- "is anything getting through" -- and because that is
// the corner of the bar a player glances at when nothing is happening and
// they want to know whose fault it is. MIDI and OSC each had a light; audio,
// the one signal the box exists for, had none.
//
// Two meters rather than one because they fail apart, and which one is dead
// is the whole diagnosis:
//
//   IN dead, OUT dead     nothing is arriving -- cable, input device, or the
//                         wrong channel pair.
//   IN live, OUT dead     the signal is here and the board is eating it --
//                         a gain at zero, a gate shut, a crashed plugin, or
//                         the master mute.
//   IN dead, OUT live     a synth, the file player, the test tone.
//
// The levels come from the engine's master stage, not from nodes: IN is read
// before the graph runs, so an empty or bypassed board cannot make a live
// input look dead, and OUT is read after the mute, so it shows what is
// actually leaving rather than what the graph produced.
//
// Ballistics: instant rise, smooth fall, and a peak marker held for about a
// second. Easing the rise would lie about transients, and a transient is
// exactly what you are watching for. Red from -3 dBFS so clipping needs no
// number, and the scale is linear in dB -- an amplitude-linear meter spends
// nearly all its travel in the top 6 dB and shows nothing at all for a quiet
// but perfectly good signal.
class IoMonitor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit IoMonitor(engine::AudioEngine& engine);
    ~IoMonitor() override;

    void paint(juce::Graphics&) override;

    // What the bar asks for when there is room: the caption column plus a
    // track long enough to read at a glance. The two stacked tracks are the
    // reason this is wider than one meter would need -- a 36 px track is a
    // dash, not a level.
    static constexpr int kPreferredWidth = 88;

private:
    void timerCallback() override;

    // One metering point's state. Two of them, read and drawn identically.
    struct Channel
    {
        float shown{0.0f};
        meter::PeakHold peak;
    };

    void advance(Channel& channel, float level, double nowMs);

    engine::AudioEngine& engine_;
    Channel in_;
    Channel out_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IoMonitor)
};

} // namespace pedalboard3::ui
