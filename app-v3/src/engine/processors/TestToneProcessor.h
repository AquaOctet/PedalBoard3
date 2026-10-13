#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace pedalboard3::engine::processors
{

// A sine generator used as an audible "is the pipeline actually running"
// check. Not a user-facing feature in v3.0; it lives in the dev builds so
// we can smoke-test audio hardware end-to-end without waiting for a plugin
// host layer. Also serves as a handy building block for integration tests.
//
// RT contract: processBlock does not allocate or lock. Enable / frequency
// / amplitude setters are lock-free atomics so they're safe from the UI
// thread. Phase changes are smoothed over ~20 ms by juce::SmoothedValue.
class TestToneProcessor : public juce::AudioProcessor
{
public:
    TestToneProcessor();
    ~TestToneProcessor() override = default;

    // AudioProcessor ---------------------------------------------------------
    const juce::String getName() const override { return "Test Tone"; }

    void prepareToPlay(double sampleRate, int blockSize) override;
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

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // Controls ---------------------------------------------------------------
    void setEnabled(bool shouldBeEnabled) noexcept;

    // 20 Hz – 20 kHz. Values outside this range are clamped.
    void setFrequency(float hz) noexcept;

    // 0.0 – 1.0. Clamped.
    void setAmplitude(float amplitude) noexcept;

private:
    static constexpr double kEnableRampSeconds = 0.020; // 20 ms ramp

    std::atomic<bool> enabled_{false};
    std::atomic<float> frequency_{440.0f};
    std::atomic<float> amplitude_{0.25f};

    double sampleRate_{0.0};
    double phase_{0.0};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestToneProcessor)
};

} // namespace pedalboard3::engine::processors
