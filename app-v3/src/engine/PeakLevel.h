#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace pedalboard3::engine
{

// One metering point: the loudest sample seen since somebody last looked.
//
// AudioGraphRuntime keeps two, at the two places worth watching (#117):
//
//   input   what arrived from the device, taken before the graph runs, so an
//           empty board, a bypassed first plugin or a silent synth cannot
//           make a live input look dead. A meter that reads zero because the
//           board is empty answers the wrong question.
//   output  what is actually leaving, taken after the sanitiser, the test
//           tone and the master mute. Everything that can change what you
//           hear has happened by then, so muted reads as silence and the
//           test tone reads as the tone -- which is the point of having the
//           meter at the end rather than at the graph's last node.
//
// The audio thread writes a running peak and never clears it; the reader
// takes the value and resets the slot in one exchange. That way the meter
// shows the loudest sample since it last looked rather than whichever block
// happened to be in flight when it looked, so a transient between two 30 Hz
// reads is still seen. A read that races a write loses at most one block.
//
// RT contract: capture() does not allocate or lock.
class PeakLevel
{
public:
    // Audio thread.
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
