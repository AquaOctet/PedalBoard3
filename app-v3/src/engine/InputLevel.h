#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace pedalboard3::engine
{

// What is arriving from the audio input, measured at the master stage before
// the board sees it.
//
// It is here rather than in a node for the same reason the mute and the tone
// are: a board with nothing on it, or one whose first plugin is silent or
// bypassed or crashed, still has to be able to answer "is my guitar reaching
// this machine". A meter that reads zero because the board is empty tells you
// nothing about the cable.
//
// The audio thread writes the running peak and never clears it; the reader
// takes the value and resets the slot in one exchange. That way the meter
// shows the loudest sample since it last looked rather than whichever block
// happened to be in flight when it looked, so a transient between two 30 Hz
// reads is still seen. A read that races a write loses at most one block.
//
// RT contract: capture() does not allocate or lock.
class InputLevel
{
public:
    // Audio thread. `buffer` holds the device input, before the graph runs.
    void capture(const juce::AudioBuffer<float>& buffer) noexcept
    {
        float blockPeak = 0.0f;
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        for (int ch = 0; ch < numChannels; ++ch)
            blockPeak = juce::jmax(blockPeak, buffer.getMagnitude(ch, 0, numSamples));

        // Only the audio thread writes, so load-then-store needs no CAS; the
        // reader's exchange can drop one block's peak and nothing else.
        if (blockPeak > peak_.load(std::memory_order_relaxed))
            peak_.store(blockPeak, std::memory_order_relaxed);
    }

    // Message thread. The peak since the previous call, and resets.
    float readAndClear() noexcept { return peak_.exchange(0.0f, std::memory_order_relaxed); }

    // Message thread, without resetting. For tests.
    float peek() const noexcept { return peak_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> peak_{0.0f};
};

} // namespace pedalboard3::engine
