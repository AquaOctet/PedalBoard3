#pragma once

// A tiny AudioProcessor used as a test double for anything that wraps an
// inner processor (BypassableInstance, AudioGraphRuntime). It multiplies
// every sample by `gain`. Public `processBlockCallCount` lets tests assert
// whether processBlock fired (e.g. to prove a bypass path didn't call it).

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace pedalboard3::testing
{

class TestGainProcessor : public juce::AudioProcessor
{
public:
    explicit TestGainProcessor(float g = 2.0f)
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("In", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
          gain(g)
    {
    }

    const juce::String getName() const override { return "TestGain"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() &&
               !layouts.getMainInputChannelSet().isDisabled();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        processBlockCallCount.fetch_add(1, std::memory_order_relaxed);
        lastMidiEventCount.store(midi.getNumEvents(), std::memory_order_relaxed);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                buffer.getWritePointer(ch)[n] *= gain;
    }

    bool acceptsMidi() const override { return true; }
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

    float gain;
    std::atomic<int> processBlockCallCount{0};
    std::atomic<int> lastMidiEventCount{0};
};

} // namespace pedalboard3::testing
