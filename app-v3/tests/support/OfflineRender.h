#pragma once

// Offline render helpers for engine unit tests.
//
// The real-time invariants of the audio engine (no alloc, no lock, atomic
// graph swap) are impossible to assert against hardware: an audio driver
// will happily paper over a 5 ms stall. Every engine test in Phase 1 onward
// uses these helpers to drive processors and graphs in a fully synchronous,
// deterministic, hardware-free way.
//
// Conventions:
//   - Default sample rate: 48 kHz. Default block size: 256 samples.
//   - `renderBlocks(proc, buffer)` drives `proc` block-by-block over `buffer`
//     in place — mimics what an audio thread would do, minus the realtime-
//     priority scheduler. No MIDI unless a MidiBuffer is supplied.
//   - `silence`, `dc`, `sine` are input generators that produce a buffer of
//     the requested size.
//   - `makeTestBuffer` allocates an AudioBuffer with the given channels and
//     frames.
//
// Keep this header minimal and RT-free (allocation is fine in tests).

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

namespace pedalboard3::testing
{

inline constexpr double kDefaultSampleRate = 48000.0;
inline constexpr int    kDefaultBlockSize  = 256;

// Build an empty AudioBuffer<float> with the given shape, zero-initialised.
inline juce::AudioBuffer<float> makeTestBuffer(int numChannels, int numFrames)
{
    juce::AudioBuffer<float> buf(numChannels, numFrames);
    buf.clear();
    return buf;
}

// Fill every sample of `buf` with constant `value`.
inline void fillDC(juce::AudioBuffer<float>& buf, float value)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        auto* d = buf.getWritePointer(ch);
        for (int n = 0; n < buf.getNumSamples(); ++n)
            d[n] = value;
    }
}

// Fill `buf` with a sine of the given frequency. Used as a smoke input.
inline void fillSine(juce::AudioBuffer<float>& buf, double freqHz,
                     double sampleRate = kDefaultSampleRate, float amplitude = 0.5f)
{
    const double phaseInc = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        auto* d = buf.getWritePointer(ch);
        double phase = 0.0;
        for (int n = 0; n < buf.getNumSamples(); ++n, phase += phaseInc)
            d[n] = amplitude * static_cast<float>(std::sin(phase));
    }
}

// Drive `proc` across `buffer` block-by-block, mutating in place.
// `proc` must already have been prepareToPlay'd with the test's sample rate
// and block size. Empty MIDI each block (see midiOverload below for MIDI).
inline void renderBlocks(juce::AudioProcessor& proc, juce::AudioBuffer<float>& buffer,
                         int blockSize = kDefaultBlockSize)
{
    juce::MidiBuffer midi;
    const int numFrames = buffer.getNumSamples();
    const int numCh     = buffer.getNumChannels();

    for (int frame = 0; frame < numFrames; frame += blockSize)
    {
        const int remaining = juce::jmin(blockSize, numFrames - frame);
        juce::AudioBuffer<float> slice(buffer.getArrayOfWritePointers(), numCh, frame, remaining);
        proc.processBlock(slice, midi);
        midi.clear();
    }
}

// Overload that passes a non-empty MIDI buffer on the first block only.
inline void renderBlocks(juce::AudioProcessor& proc,
                         juce::AudioBuffer<float>& buffer,
                         juce::MidiBuffer initialMidi,
                         int blockSize = kDefaultBlockSize)
{
    const int numFrames = buffer.getNumSamples();
    const int numCh     = buffer.getNumChannels();

    for (int frame = 0; frame < numFrames; frame += blockSize)
    {
        const int remaining = juce::jmin(blockSize, numFrames - frame);
        juce::AudioBuffer<float> slice(buffer.getArrayOfWritePointers(), numCh, frame, remaining);
        proc.processBlock(slice, initialMidi);
        initialMidi.clear();
    }
}

// Peak absolute sample across all channels and frames. Useful for asserting
// signal presence / silence / clipping.
inline float peakAbs(const juce::AudioBuffer<float>& buf) noexcept
{
    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const auto* d = buf.getReadPointer(ch);
        for (int n = 0; n < buf.getNumSamples(); ++n)
            peak = juce::jmax(peak, std::abs(d[n]));
    }
    return peak;
}

// True if every sample is exactly zero.
inline bool isSilent(const juce::AudioBuffer<float>& buf) noexcept
{
    return peakAbs(buf) == 0.0f;
}

// True if no sample is NaN or ±Inf.
inline bool isFiniteEverywhere(const juce::AudioBuffer<float>& buf) noexcept
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const auto* d = buf.getReadPointer(ch);
        for (int n = 0; n < buf.getNumSamples(); ++n)
            if (!std::isfinite(d[n]))
                return false;
    }
    return true;
}

} // namespace pedalboard3::testing
