#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pedalboard3::engine::processors
{

// Internal "Level" node — a stereo gain trim with one managed AudioParameterFloat
// "level" (0..1, default 0.5). DSP: out = in * level * 2 (unity at 0.5, +6 dB at
// 1.0), matching Pedalboard2's LevelProcessor. The parameter is host-automatable,
// so this is the first internal node the MIDI/OSC control surface can drive.
//
// RT-safe: processBlock allocates/locks nothing, hoists a single parameter read,
// and opens with juce::ScopedNoDenormals.
class LevelProcessor : public juce::AudioProcessor
{
public:
    LevelProcessor();
    ~LevelProcessor() override = default;

    // AudioProcessor ---------------------------------------------------------
    const juce::String getName() const override { return "Level"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // State persistence is deferred — internal-node serialization lands with the
    // add-path increment (PatchNode is currently plugin-only).
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    juce::AudioParameterFloat* levelParam_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelProcessor)
};

} // namespace pedalboard3::engine::processors
