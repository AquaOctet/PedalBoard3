#pragma once

#include "ProcessorBase.h"

namespace pedalboard3::engine::processors
{

// Output Toggle (M6 U10, plan KD10): Pedalboard2's A/B switch. The input is
// mixed to mono and sent to output 1 (the left output port) or output 2 (the
// right), with a 20 ms crossfade so a switch never clicks. One parameter,
// "Output" (Output 1 / Output 2), which a footswitch can map.
//
// RT-safe: processBlock reads the parameter once and ramps two gains; nothing
// allocates or locks.
class OutputToggleProcessor final : public ProcessorBase
{
public:
    static constexpr double kFadeSeconds = 0.02;

    OutputToggleProcessor();

    const juce::String getName() const override { return "Output Toggle"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // 0 = output 1, 1 = output 2.
    int getOutput() const noexcept { return output_->getIndex(); }

private:
    juce::AudioParameterChoice* output_ = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> toSecond_; // 0 = all to output 1, 1 = all to output 2

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputToggleProcessor)
};

} // namespace pedalboard3::engine::processors
