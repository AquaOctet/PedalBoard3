#include "LevelProcessor.h"

namespace pedalboard3::engine::processors
{

LevelProcessor::LevelProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Explicit continuous NormalisableRange (interval 0) — the (min,max,default)
    // convenience ctor injects a 0.01 step that would snap MIDI/OSC automation.
    addParameter(levelParam_ = new juce::AudioParameterFloat(
                     juce::ParameterID{"level", 1}, "Level", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
}

bool LevelProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() &&
           !layouts.getMainInputChannelSet().isDisabled();
}

void LevelProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals nodn;

    const float gain = levelParam_->get() * 2.0f; // unity at 0.5, +6 dB at 1.0 (v2 parity)
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* d = buffer.getWritePointer(ch);
        for (int n = 0; n < numSamples; ++n)
            d[n] *= gain;
    }
}

} // namespace pedalboard3::engine::processors
