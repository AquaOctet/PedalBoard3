#pragma once

#include "MeterMath.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

// The top bar's CPU and MEM meters (UI refresh U2, #22; memory is #118): two
// stacked bars for the two resources a plugin host runs out of.
//
//   CPU  the audio callback's load, from AudioDeviceManager::getCpuUsage, at
//        10 Hz. A tick at 70%, teal below it, amber from 70%, red from 90%,
//        and a peak marker held for about a second. The tooltip adds the
//        device's dropout (xrun) count where the driver reports one. With no
//        device the bar is empty and the number reads a dash.
//
//   MEM  this process's resident set as a share of the machine's RAM, at
//        1 Hz. It belongs beside the CPU load because the two failures look
//        identical from the stage -- the sound breaks up -- and only one of
//        them is fixed by freezing a track. A board that has been switched
//        twenty times, or one sampler with a big library, walks this number
//        up where the CPU load stays flat, and a host that is paging is a
//        host that drops out.
//
// The resident set rather than the virtual size: a sampler that memory-maps
// ten gigabytes reserves all of it and touches almost none, so the virtual
// figure would be alarming and wrong. See platform/ProcessMemory.h.
//
// Memory is polled at a tenth of the CPU rate. It moves on the scale of
// loading a plugin, not of a buffer, and asking the OS sixty times a second
// for a number that changes once a minute is work for nothing.
class SystemMeters : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit SystemMeters(juce::AudioDeviceManager* deviceManager);
    ~SystemMeters() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void refreshMemory();
    void refreshTooltip();

    juce::AudioDeviceManager* deviceManager_;

    float shown_{0.0f};
    meter::PeakHold peak_;
    bool hasDevice_{false};

    // Memory, refreshed every kMemoryEveryTicks timer callbacks.
    int ticksUntilMemory_{0};
    std::uint64_t memoryBytes_{0};
    std::uint64_t totalBytes_{0};
    float memoryFraction_{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SystemMeters)
};

} // namespace pedalboard3::ui
