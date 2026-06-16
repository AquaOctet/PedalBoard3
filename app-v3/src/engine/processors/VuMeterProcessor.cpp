#include "VuMeterProcessor.h"

#include <cmath>

namespace pedalboard3::engine::processors
{

namespace
{
// Advance a peak envelope across one channel: hold/raise to the sample peak
// (instant attack), otherwise decay linearly, floored at 0. A non-finite sample
// never raises the level (fabs(NaN) >= level is false), so NaN cannot latch the
// meter into NaN — it just lets the level decay.
float meterChannel(const float* d, int numSamples, float level, float decayPerSample) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        const float a = std::fabs(d[n]);
        if (a >= level)
        {
            level = a;
        }
        else if (level > 0.0f)
        {
            level -= decayPerSample;
            if (level < 0.0f)
                level = 0.0f;
        }
    }
    return level;
}
} // namespace

VuMeterProcessor::VuMeterProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void VuMeterProcessor::prepareToPlay(double, int)
{
    levelL_ = 0.0f;
    levelR_ = 0.0f;
    publishedL_.store(0.0f, std::memory_order_release);
    publishedR_.store(0.0f, std::memory_order_release);
}

bool VuMeterProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() &&
           !layouts.getMainInputChannelSet().isDisabled();
}

void VuMeterProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals nodn;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels > 0)
        levelL_ = meterChannel(buffer.getReadPointer(0), numSamples, levelL_, kDecayPerSample);
    if (numChannels > 1)
        levelR_ = meterChannel(buffer.getReadPointer(1), numSamples, levelR_, kDecayPerSample);

    publishedL_.store(levelL_, std::memory_order_release);
    publishedR_.store(levelR_, std::memory_order_release);
    // The buffer is intentionally NOT written — this is a pure passthrough meter.
}

} // namespace pedalboard3::engine::processors
