#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pedalboard3::engine::processors
{

// The stand-in for a plugin that can't be loaded (M5 U3, plan KD2 and KD4): it
// isn't in the scanned list, it's blocked, or it failed to load or to take its
// saved settings. It passes audio and MIDI straight through, so the rest of the
// chain keeps sounding, and it keeps the plugin's identity and saved settings
// byte for byte, so saving the board writes them back unchanged.
//
// No parameters and no editor. RT-safe: processBlock does nothing, which leaves
// the buffer as it came in.
class MissingPluginProcessor final : public juce::AudioProcessor
{
public:
    MissingPluginProcessor(juce::PluginDescription identity, juce::String reason, juce::MemoryBlock state);

    const juce::String getName() const override { return identity_.name; }
    // Why it's a placeholder, as the card says it.
    const juce::String& getReason() const noexcept { return reason_; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // The saved settings, verbatim: what was loaded is what gets saved.
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    juce::PluginDescription identity_;
    juce::String reason_;
    juce::MemoryBlock state_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MissingPluginProcessor)
};

} // namespace pedalboard3::engine::processors
