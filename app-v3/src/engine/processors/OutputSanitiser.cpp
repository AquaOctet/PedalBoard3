#include "OutputSanitiser.h"

#include <cmath>

namespace pedalboard3::engine::processors
{

OutputSanitiser::OutputSanitiser()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool OutputSanitiser::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto inMain  = layouts.getMainInputChannelSet();
    const auto outMain = layouts.getMainOutputChannelSet();
    return inMain == outMain && !inMain.isDisabled();
}

void OutputSanitiser::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals nodn;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    bool sawBadSample = false;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* d = buffer.getWritePointer(ch);
        for (int n = 0; n < numSamples; ++n)
        {
            const float x = d[n];
            // `std::isfinite` is true iff not NaN AND not ±Inf. One branch
            // per sample; well predicted because it's almost always taken.
            if (!std::isfinite(x) || std::fabs(x) > kHardCeiling)
            {
                d[n] = 0.0f;
                sawBadSample = true;
            }
        }
    }

    if (sawBadSample)
        badSampleFlag_.store(true, std::memory_order_release);
}

bool OutputSanitiser::consumeBadSampleFlag() noexcept
{
    return badSampleFlag_.exchange(false, std::memory_order_acq_rel);
}

} // namespace pedalboard3::engine::processors
