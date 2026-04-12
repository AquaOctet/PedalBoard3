#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace pedalboard3::engine::processors
{

// Final-stage audio guard. Sits at the master output of the graph and:
//
//   1. Replaces any non-finite sample (NaN, +Inf, -Inf) with 0.
//   2. Replaces any sample with |x| > kHardCeiling with 0.
//
// Both conditions set an atomic flag, polled at 1 Hz by a message-thread
// Timer elsewhere in the app so the Event Log records at most one event
// per second per direction. The flag is one-shot via `consumeBadSampleFlag`.
//
// Contract: RT-safe. No allocation, no locking, no I/O. Scalar
// `std::isfinite` + branch is fast enough at 48 kHz stereo (≈0.25% of one
// core). SSE2 vectorisation is a future micro-optimisation if we ever push
// ≥8 output channels. See plan §4.1 invariant 4 and §Appendix A.2.
class OutputSanitiser : public juce::AudioProcessor
{
public:
    // Hard amplitude ceiling. Samples strictly greater than this (in absolute
    // value) are replaced with 0. +/- 1.0 is nominal full-scale; the 0.5 dB
    // of headroom keeps honest near-full-scale signals from being punished.
    static constexpr float kHardCeiling = 1.5f;

    OutputSanitiser();
    ~OutputSanitiser() override = default;

    // AudioProcessor ---------------------------------------------------------
    const juce::String getName() const override { return "Output Sanitiser"; }

    void prepareToPlay(double /*sr*/, int /*blockSize*/) override {}
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

    // Observability ----------------------------------------------------------

    // Returns true and clears the flag if any bad sample has been observed
    // since the last call. Polled by a message-thread Timer. Lock-free.
    bool consumeBadSampleFlag() noexcept;

private:
    std::atomic<bool> badSampleFlag_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputSanitiser)
};

} // namespace pedalboard3::engine::processors
