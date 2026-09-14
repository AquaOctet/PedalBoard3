#pragma once

#include "AudioSettingsModel.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace pedalboard3::ui
{

// The Audio Devices window's content (UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U6 / KD3): our own panel
// on juce::AudioDeviceManager in place of JUCE's device selector, laid out
// like the prototype (vercel-ui-demo/components/audio-settings-dialog.tsx) in
// the mockup's style:
//   DRIVER     the device types
//   OUTPUT     the device, its channels as stereo-pair chips, and ASIO's
//              Control Panel
//   INPUT      the device, its channel chips, and a level meter
//   FORMAT     sample rate and buffer size, and the latency each way in teal
//   TEST TONE  the switch the Settings menu shares
// Changes apply at once (there's no Apply): each goes straight to the device
// manager, which PreferencesPersister saves. A device that won't open says so
// here in red rather than in an alert. The panel follows the device manager,
// whoever changes it. The prototype's gain sliders and exclusive-mode switch
// are left out: nothing behind them.
//
// Message thread only.
class AudioSettingsPanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    AudioSettingsPanel(juce::AudioDeviceManager& devices, juce::Value& testTone);
    ~AudioSettingsPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override; // the device manager changed
    void timerCallback() override;                                  // the input meter

    // Every field from the device manager as it is now.
    void refresh();
    void rebuildChips(std::vector<std::unique_ptr<juce::TextButton>>& chips,
                      const juce::StringArray& channelNames,
                      const juce::BigInteger& active,
                      bool input);
    void toggleChannels(const audiosettings::ChannelPair& pair, bool input);
    // Hands `setup` to the device manager and shows what went wrong, if anything.
    void apply(const juce::AudioDeviceManager::AudioDeviceSetup& setup);

    juce::AudioDeviceManager& devices_;
    juce::AudioDeviceManager::LevelMeter::Ptr inputLevel_;

    juce::Label driverCaption_;
    juce::Label outputCaption_;
    juce::Label inputCaption_;
    juce::Label formatCaption_;
    juce::Label toneCaption_;
    juce::ComboBox driverBox_;
    juce::ComboBox outputBox_;
    juce::ComboBox inputBox_;
    juce::ComboBox rateBox_;
    juce::ComboBox bufferBox_;
    juce::TextButton controlPanel_{"Control Panel..."};
    std::vector<std::unique_ptr<juce::TextButton>> outputChips_;
    std::vector<std::unique_ptr<juce::TextButton>> inputChips_;
    juce::Label error_;
    juce::ToggleButton toneButton_{"Test Tone"};
    juce::Label toneHint_;
    juce::TextButton closeButton_{"Close"};

    juce::Array<double> rates_;
    juce::Array<int> buffers_;
    juce::String outputLatency_;
    juce::String inputLatency_;
    juce::Rectangle<int> meterArea_;
    juce::Rectangle<int> latencyArea_;
    float shownLevel_{0.0f};
    bool refreshing_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsPanel)
};

} // namespace pedalboard3::ui
