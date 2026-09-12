#include "Transport.h"

#include <cmath>

namespace pedalboard3::engine
{

void Transport::play() noexcept
{
    playing_.store(true, std::memory_order_relaxed);
}

void Transport::pause() noexcept
{
    playing_.store(false, std::memory_order_relaxed);
}

void Transport::togglePlayPause() noexcept
{
    // Requests come from the message thread only, so load-then-store is fine.
    playing_.store(!playing_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void Transport::stop() noexcept
{
    playing_.store(false, std::memory_order_relaxed);
    returnToZeroPending_.store(true, std::memory_order_release);
}

void Transport::returnToZero() noexcept
{
    returnToZeroPending_.store(true, std::memory_order_release);
}

void Transport::setTempo(double bpm) noexcept
{
    if (std::isfinite(bpm))
        bpm_.store(transport::clampTempo(bpm), std::memory_order_relaxed);
}

void Transport::setTimeSignature(int numerator, int denominator) noexcept
{
    if (transport::isValidTimeSignature(numerator, denominator))
        timeSignature_.store(packTimeSignature(numerator, denominator), std::memory_order_relaxed);
}

void Transport::beginBlock(double sampleRate) noexcept
{
    if (returnToZeroPending_.exchange(false, std::memory_order_acq_rel))
    {
        samples_.store(0, std::memory_order_relaxed);
        ppq_.store(0.0, std::memory_order_relaxed);
    }

    const int timeSignature = timeSignature_.load(std::memory_order_relaxed);
    if (sampleRate > 0.0)
        block_.sampleRate = sampleRate;
    block_.playing = playing_.load(std::memory_order_relaxed);
    block_.bpm = bpm_.load(std::memory_order_relaxed);
    block_.numerator = timeSignature >> 8;
    block_.denominator = timeSignature & 0xff;
    block_.samples = samples_.load(std::memory_order_relaxed);
    block_.ppq = ppq_.load(std::memory_order_relaxed);
}

void Transport::endBlock(int numSamples) noexcept
{
    if (!block_.playing || numSamples <= 0)
        return;
    // A Return to Zero asked for during this block is applied by the next
    // beginBlock, after this store.
    samples_.store(block_.samples + numSamples, std::memory_order_relaxed);
    ppq_.store(transport::advancePpq(block_.ppq, numSamples, block_.sampleRate, block_.bpm), std::memory_order_relaxed);
}

juce::Optional<juce::AudioPlayHead::PositionInfo> Transport::getPosition() const
{
    PositionInfo info;
    TimeSignature signature;
    signature.numerator = block_.numerator;
    signature.denominator = block_.denominator;

    info.setBpm(block_.bpm);
    info.setTimeSignature(signature);
    info.setTimeInSamples(block_.samples);
    info.setTimeInSeconds(static_cast<double>(block_.samples) / block_.sampleRate);
    info.setPpqPosition(block_.ppq);
    info.setPpqPositionOfLastBarStart(transport::barStartPpq(block_.ppq, block_.numerator, block_.denominator));
    info.setBarCount(transport::barsBefore(block_.ppq, block_.numerator, block_.denominator));
    info.setIsPlaying(block_.playing);
    info.setIsRecording(false);
    info.setIsLooping(false);
    return info;
}

} // namespace pedalboard3::engine
