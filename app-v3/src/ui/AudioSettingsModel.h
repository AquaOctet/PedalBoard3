#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace pedalboard3::ui::audiosettings
{

// The headless part of the Audio Settings panel (UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U6): its latency text,
// channel pairs, and sample-rate and buffer-size choices.

// The dash in a channel pair's label ("1–2"). Built from UTF-8: a non-ASCII
// narrow literal trips juce::String's assertion.
inline juce::String pairDash()
{
    return juce::String::fromUTF8("\xe2\x80\x93");
}

// A latency in milliseconds, rounded to 0.1: "5.3 ms". Empty while the rate
// isn't known.
inline juce::String latencyText(int samples, double sampleRate)
{
    if (sampleRate <= 0.0 || samples < 0)
        return {};
    const double ms = std::round(static_cast<double>(samples) * 1000.0 / sampleRate * 10.0) / 10.0;
    return juce::String(ms, 1) + " ms";
}

// One direction's latency in samples: what the device reports, or its buffer
// when it reports nothing.
inline int directionLatencySamples(int reported, int bufferSize)
{
    return reported > 0 ? reported : bufferSize;
}

// A sample rate as its field shows it: "44.1 kHz", "48 kHz".
inline juce::String rateText(double sampleRate)
{
    const double kHz = sampleRate / 1000.0;
    const bool whole = std::abs(kHz - std::round(kHz)) < 1.0e-6;
    return (whole ? juce::String(juce::roundToInt(kHz)) : juce::String(kHz, 1)) + " kHz";
}

// A buffer size as its field shows it: "256 samples (5.3 ms)" at `sampleRate`.
inline juce::String bufferText(int samples, double sampleRate)
{
    auto text = juce::String(samples) + " samples";
    if (sampleRate > 0.0)
        text << " (" << latencyText(samples, sampleRate) << ")";
    return text;
}

// The buffer sizes to offer: the device's own, as it lists them, and the
// current size too when the device didn't list it.
inline juce::Array<int> bufferChoices(const juce::Array<int>& deviceSizes, int current)
{
    auto choices = deviceSizes;
    if (current > 0 && !choices.contains(current))
    {
        choices.add(current);
        choices.sort();
    }
    return choices;
}

// A chip: one stereo pair of a device's channels, or a lone last channel.
struct ChannelPair
{
    int first{0}; // the first channel's index, from 0
    int count{2}; // 2, or 1 for a lone last channel
    juce::String label;
};

// A device's channels as stereo pairs: "1–2", "3–4", and a lone "5".
inline std::vector<ChannelPair> channelPairs(int channelCount)
{
    std::vector<ChannelPair> pairs;
    for (int first = 0; first < channelCount; first += 2)
    {
        const int count = juce::jmin(2, channelCount - first);
        const auto label =
            count == 2 ? juce::String(first + 1) + pairDash() + juce::String(first + 2) : juce::String(first + 1);
        pairs.push_back({first, count, label});
    }
    return pairs;
}

// Whether any of a pair's channels is on.
inline bool isPairOn(const juce::BigInteger& channels, const ChannelPair& pair)
{
    for (int i = 0; i < pair.count; ++i)
        if (channels[pair.first + i])
            return true;
    return false;
}

// The channels after clicking a pair's chip: all of it off when any of it was
// on, otherwise all of it on.
inline juce::BigInteger togglePair(juce::BigInteger channels, const ChannelPair& pair)
{
    const bool on = !isPairOn(channels, pair);
    for (int i = 0; i < pair.count; ++i)
        channels.setBit(pair.first + i, on);
    return channels;
}

} // namespace pedalboard3::ui::audiosettings
