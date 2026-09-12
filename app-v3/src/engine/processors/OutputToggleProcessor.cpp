#include "OutputToggleProcessor.h"

namespace pedalboard3::engine::processors
{

OutputToggleProcessor::OutputToggleProcessor()
    : ProcessorBase(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true),
                    "OutputToggleProcessor")
{
    addParameter(output_ = new juce::AudioParameterChoice(
                     juce::ParameterID{"output", 1}, "Output", juce::StringArray{"Output 1", "Output 2"}, 0));
    toSecond_.setCurrentAndTargetValue(0.0f);
}

void OutputToggleProcessor::prepareToPlay(double sampleRate, int)
{
    toSecond_.reset(sampleRate, kFadeSeconds);
    toSecond_.setCurrentAndTargetValue(getOutput() == 1 ? 1.0f : 0.0f); // start where the switch is
}

bool OutputToggleProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void OutputToggleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2)
        return;

    toSecond_.setTargetValue(getOutput() == 1 ? 1.0f : 0.0f);
    auto* first = buffer.getWritePointer(0);
    auto* second = buffer.getWritePointer(1);
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        const float mono = 0.5f * (first[n] + second[n]);
        const float share = toSecond_.getNextValue();
        first[n] = mono * (1.0f - share);
        second[n] = mono * share;
    }
}

} // namespace pedalboard3::engine::processors
