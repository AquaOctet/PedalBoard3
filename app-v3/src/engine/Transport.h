#pragma once

#include "TransportMath.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>

namespace pedalboard3::engine
{

// The app's transport (M6 U1, plan KD1): play, pause, stop and Return to
// Zero, a tempo and a time signature, reported to every hosted plugin as its
// play head. While playing, the position advances with each audio block, in
// samples and in quarter notes. Paused, it holds; stopped, it's back at 0.
//
// Threads:
//   - Requests (play, pause, stop, returnToZero, setTempo, setTimeSignature)
//     come from the message thread and only store atomics.
//   - AudioGraphRuntime calls beginBlock and endBlock around each block on
//     the audio thread. A Return to Zero lands at the next block boundary.
//   - getPosition answers plugins during the block from the snapshot
//     beginBlock took, so every plugin in a block sees the same position.
//   - The getters are relaxed reads, for the UI.
class Transport final : public juce::AudioPlayHead
{
public:
    Transport() = default;

    void play() noexcept;
    void pause() noexcept; // holds the position
    void togglePlayPause() noexcept;
    void stop() noexcept;         // pauses and returns to zero
    void returnToZero() noexcept; // keeps playing if it was

    void setTempo(double bpm) noexcept;                             // clamped to 20-300 BPM
    void setTimeSignature(int numerator, int denominator) noexcept; // ignored unless valid

    bool isPlaying() const noexcept { return playing_.load(std::memory_order_relaxed); }
    double getTempo() const noexcept { return bpm_.load(std::memory_order_relaxed); }
    int getNumerator() const noexcept { return timeSignature_.load(std::memory_order_relaxed) >> 8; }
    int getDenominator() const noexcept { return timeSignature_.load(std::memory_order_relaxed) & 0xff; }
    double getPpqPosition() const noexcept { return ppq_.load(std::memory_order_relaxed); }
    std::int64_t getSamplePosition() const noexcept { return samples_.load(std::memory_order_relaxed); }

    // Audio thread, around each block.
    void beginBlock(double sampleRate) noexcept;
    void endBlock(int numSamples) noexcept;

    // juce::AudioPlayHead: the current block's position.
    juce::Optional<PositionInfo> getPosition() const override;

private:
    static constexpr int packTimeSignature(int numerator, int denominator) noexcept
    {
        return (numerator << 8) | denominator;
    }

    std::atomic<bool> playing_{false};
    std::atomic<bool> returnToZeroPending_{false};
    std::atomic<double> bpm_{transport::kDefaultTempo};
    std::atomic<int> timeSignature_{packTimeSignature(4, 4)}; // one atomic, so it's never read torn
    std::atomic<std::int64_t> samples_{0};
    std::atomic<double> ppq_{0.0};

    // The current block's snapshot. Audio thread only.
    struct Block
    {
        double sampleRate{44100.0};
        bool playing{false};
        double bpm{transport::kDefaultTempo};
        int numerator{4};
        int denominator{4};
        std::int64_t samples{0};
        double ppq{0.0};
    };
    Block block_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Transport)
};

} // namespace pedalboard3::engine
