#pragma once

// A minimal AudioProcessor exposing one managed AudioParameterFloat ("gain",
// normalized range 0..1) that scales every sample. Unlike TestGainProcessor
// (whose gain is a plain member), this routes gain through getParameters()[0],
// so it stands in for a hosted plugin in parameter-automation tests
// (ControlParameterApplier).

#include <juce_audio_processors/juce_audio_processors.h>

namespace pedalboard3::testing
{

class TestParamProcessor : public juce::AudioProcessor
{
public:
    TestParamProcessor()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("In", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Out", juce::AudioChannelSet::stereo(), true))
    {
        addParameter(gainParam = new juce::AudioParameterFloat(juce::ParameterID{"gain", 1}, "Gain", 0.0f, 1.0f, 1.0f));
    }

    const juce::String getName() const override { return "TestParam"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() &&
               !layouts.getMainInputChannelSet().isDisabled();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const float g = gainParam->get();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                buffer.getWritePointer(ch)[n] *= g;
    }

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

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioParameterFloat* gainParam = nullptr;
};

} // namespace pedalboard3::testing
