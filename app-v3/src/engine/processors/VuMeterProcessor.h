#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace pedalboard3::engine::processors
{

// Internal "VU Meter" node — a stereo PASSTHROUGH that measures the per-channel
// peak envelope (instant attack, linear decay) and publishes it via atomics for a
// message-thread meter UI. Audio is never altered.
//
// Parity notes vs Pedalboard2's VuMeterProcessor:
//   - v2 was 2-in / 0-out; v3 is stereo-in / stereo-out passthrough, so it drops
//     into the uniform stereo bus convention without a 2->0 special case and can
//     meter mid-chain. Intentional, not a parity bug.
//   - The envelope holds at the peak while the signal stays at/above it (>=),
//     then decays; v2 used strict > (which decays even under a constant peak).
//     Hold-at-peak is the conventional meter behaviour and makes the level
//     deterministic; the decay rate (1e-5/sample) matches v2.
//
// RT-safe: no alloc/lock/IO; the envelope lives in audio-thread-owned members and
// is published with a single release-store atomic per block.
class VuMeterProcessor : public juce::AudioProcessor
{
public:
    VuMeterProcessor();
    ~VuMeterProcessor() override = default;

    // AudioProcessor ---------------------------------------------------------
    const juce::String getName() const override { return "VU Meter"; }

    void prepareToPlay(double, int) override;
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

    // Observability (message-thread, lock-free).
    float getLeftLevel() const noexcept { return publishedL_.load(std::memory_order_acquire); }
    float getRightLevel() const noexcept { return publishedR_.load(std::memory_order_acquire); }

private:
    static constexpr float kDecayPerSample = 0.00001f; // Pedalboard2 parity

    float levelL_{0.0f}; // audio-thread-owned envelope state
    float levelR_{0.0f};
    std::atomic<float> publishedL_{0.0f};
    std::atomic<float> publishedR_{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VuMeterProcessor)
};

} // namespace pedalboard3::engine::processors
