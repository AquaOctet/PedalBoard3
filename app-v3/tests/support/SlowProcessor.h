#pragma once

// A test AudioProcessor that sleeps for a configurable number of nanoseconds
// each processBlock. Used to drive BypassableInstance's block-budget
// watchdog with deterministic timing. Also records call count so tests can
// assert that the wrapper stopped invoking the hosted plugin after it was
// marked dead.
//
// NEVER use this in production. Sleep on the audio thread is exactly what
// the watchdog is supposed to catch.

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace pedalboard3::testing
{

class SlowProcessor : public juce::AudioProcessor
{
public:
    SlowProcessor()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("In", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Out", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "SlowProc"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() &&
               !layouts.getMainInputChannelSet().isDisabled();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        processBlockCallCount.fetch_add(1, std::memory_order_relaxed);

        // Write a known DC value so tests can distinguish the plugin's output
        // from the silence a dead-flagged BypassableInstance would produce.
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                buffer.getWritePointer(ch)[n] = kSentinelSample;

        const auto sleepNs = sleepNanos.load(std::memory_order_relaxed);
        if (sleepNs > 0)
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleepNs));
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

    // Configurable delay per processBlock call. Tests bump this above the
    // wrapper's budget to induce overruns.
    std::atomic<std::uint64_t> sleepNanos{0};

    std::atomic<int> processBlockCallCount{0};

    static constexpr float kSentinelSample = 0.5f;
};

} // namespace pedalboard3::testing
