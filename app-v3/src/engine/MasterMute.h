#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace pedalboard3::engine
{

// The master mute (#106): one gain stage the output always passes through,
// whatever is on the board.
//
// It lives in AudioGraphRuntime rather than in the graph, which is what makes
// it a *master* mute and not a node:
//   - it works on a board with nothing on it,
//   - a patch change swaps the graph and leaves it alone, so you stay muted
//     across a patch change,
//   - and it can't be a board edit, because it is not part of a board.
//
// It runs AFTER the OutputSanitiser, not before. Muting is a multiply by
// zero, and `NaN * 0.0f` is NaN — mute a graph that is producing NaN with
// this stage ahead of the sanitiser and the NaN still reaches the device.
// Sanitise, then mute, and silence is exactly 0.
//
// Switching is ramped over kRampMs rather than jumping, because a jump from
// full scale to zero in one sample is a click, and the whole point of a
// panic mute is that it is the thing you reach for when the sound is already
// wrong.
//
// Threading. setMuted comes from the message thread (a button, a MIDI note,
// an OSC message); process() is the audio thread. The only shared state is
// one relaxed atomic bool: the audio thread reads a target and walks towards
// it, so a torn or late read costs at most one block of ramp, and there is
// nothing to order it against.
//
// RT contract: process() does not allocate, lock or branch unpredictably.
// Unmuted and fully faded up — which is almost always — it returns without
// touching the buffer at all.
class MasterMute
{
public:
    // The fade, in milliseconds. Long enough not to click, short enough that
    // "mute" still feels immediate.
    static constexpr float kRampMs = 15.0f;

    // Audio thread setup. Safe to call again on a device change.
    void prepare(double sampleRate) noexcept
    {
        const double samples = sampleRate * static_cast<double>(kRampMs) / 1000.0;
        step_ = samples > 1.0 ? static_cast<float>(1.0 / samples) : 1.0f;
    }

    // Message thread.
    void setMuted(bool shouldMute) noexcept { muted_.store(shouldMute, std::memory_order_relaxed); }
    bool isMuted() const noexcept { return muted_.load(std::memory_order_relaxed); }

    // Audio thread. Call last, after the sanitiser.
    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        const float target = muted_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;

        // Settled open: the common case, and it costs one load and one
        // compare.
        if (gain_ >= 1.0f && target >= 1.0f)
            return;

        // Settled shut: no need to walk a ramp of zeroes over the buffer.
        if (gain_ <= 0.0f && target <= 0.0f)
        {
            buffer.clear();
            return;
        }

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        // Every channel replays the same ramp from the same starting gain,
        // so the walk is per channel and the final value is written back
        // once. Doing it the other way round — samples outside, channels
        // inside — would be one gain per sample but a cache miss per
        // channel.
        float endGain = gain_;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float g = gain_;
            auto* d = buffer.getWritePointer(ch);
            for (int n = 0; n < numSamples; ++n)
            {
                g = g < target ? juce::jmin(target, g + step_) : juce::jmax(target, g - step_);
                d[n] *= g;
            }
            endGain = g;
        }
        gain_ = endGain;
    }

    // Test and diagnostic hook: where the ramp has got to.
    float getCurrentGain() const noexcept { return gain_; }

private:
    std::atomic<bool> muted_{false};
    float gain_{1.0f};
    float step_{1.0f};
};

} // namespace pedalboard3::engine
