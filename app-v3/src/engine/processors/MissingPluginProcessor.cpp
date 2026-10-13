#include "MissingPluginProcessor.h"

namespace pedalboard3::engine::processors
{

MissingPluginProcessor::MissingPluginProcessor(juce::PluginDescription identity, juce::MemoryBlock state)
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      identity_(std::move(identity)), state_(std::move(state))
{
}

bool MissingPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // In place: whatever comes in goes out.
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() &&
           !layouts.getMainInputChannelSet().isDisabled();
}

void MissingPluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    destData.append(state_.getData(), state_.getSize());
}

void MissingPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        state_.reset();
    else
        state_.replaceAll(data, static_cast<size_t>(sizeInBytes));
}

} // namespace pedalboard3::engine::processors
