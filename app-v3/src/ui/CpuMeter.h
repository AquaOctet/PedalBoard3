#pragma once

#include "MeterMath.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

// The top bar's CPU meter (UI refresh U2, #22): the audio callback's load as
// a bar with a tick at 70%, teal below it, amber from 70%, red from 90%, a
// peak marker held for about a second, and the percentage. The tooltip adds
// the device's dropout (xrun) count where the driver reports one. Reads
// AudioDeviceManager::getCpuUsage on a 10 Hz message-thread timer; with no
// device the bar is empty and the number reads a dash.
class CpuMeter : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit CpuMeter(juce::AudioDeviceManager* deviceManager);
    ~CpuMeter() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::AudioDeviceManager* deviceManager_;
    float shown_{0.0f};
    meter::PeakHold peak_;
    bool hasDevice_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CpuMeter)
};

} // namespace pedalboard3::ui
