#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

// The top bar's round-trip latency, beside the CPU meter (#90): lucide's
// heartbeat icon and a figure in milliseconds, which is what the demo shows
// there and the number a player actually cares about on stage.
//
// It is buffer / rate, doubled for the input and output passes — the same
// arithmetic the demo does, and the same one the Audio Devices dialog shows
// per buffer size. With no device it reads a dash.
//
// Driven by the AudioDeviceManager's change messages, not a timer: the only
// things that move this number are a device, rate or buffer change, and each
// of those is a change message.
class LatencyReadout : public juce::Component, public juce::SettableTooltipClient, private juce::ChangeListener
{
public:
    explicit LatencyReadout(juce::AudioDeviceManager* deviceManager);
    ~LatencyReadout() override;

    void paint(juce::Graphics&) override;

    // The width the current text needs, so the bar can lay it out tight.
    int preferredWidth() const noexcept { return preferredWidth_; }

    // The readout changed width, so the owner should lay out again.
    std::function<void()> onLayoutChanged;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();

    juce::AudioDeviceManager* deviceManager_;
    juce::String text_{"—"};
    bool hasDevice_{false};
    int preferredWidth_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LatencyReadout)
};

} // namespace pedalboard3::ui
