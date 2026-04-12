#include "TestToneProcessor.h"

#include <cmath>

namespace pedalboard3::engine::processors
{

TestToneProcessor::TestToneProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input",  juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void TestToneProcessor::prepareToPlay(double sampleRate, int /*blockSize*/)
{
    sampleRate_ = sampleRate;
    phase_ = 0.0;
    gain_.reset(sampleRate, kEnableRampSeconds);
    gain_.setCurrentAndTargetValue(enabled_.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
}

bool TestToneProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
        && !layouts.getMainInputChannelSet().isDisabled();
}

void TestToneProcessor::setEnabled(bool shouldBeEnabled) noexcept
{
    enabled_.store(shouldBeEnabled, std::memory_order_relaxed);
}

void TestToneProcessor::setFrequency(float hz) noexcept
{
    frequency_.store(juce::jlimit(20.0f, 20000.0f, hz), std::memory_order_relaxed);
}

void TestToneProcessor::setAmplitude(float amplitude) noexcept
{
    amplitude_.store(juce::jlimit(0.0f, 1.0f, amplitude), std::memory_order_relaxed);
}

void TestToneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals nodn;

    gain_.setTargetValue(enabled_.load(std::memory_order_relaxed) ? 1.0f : 0.0f);

    const float    amp       = amplitude_.load(std::memory_order_relaxed);
    const double   freq      = static_cast<double>(frequency_.load(std::memory_order_relaxed));
    const double   phaseStep = (sampleRate_ > 0.0)
                               ? juce::MathConstants<double>::twoPi * freq / sampleRate_
                               : 0.0;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int n = 0; n < numSamples; ++n)
    {
        const float gate = gain_.getNextValue();
        const float sample = static_cast<float>(std::sin(phase_)) * amp * gate;

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer(ch)[n] = sample;

        phase_ += phaseStep;
        if (phase_ >= juce::MathConstants<double>::twoPi)
            phase_ -= juce::MathConstants<double>::twoPi;
    }
}

} // namespace pedalboard3::engine::processors
