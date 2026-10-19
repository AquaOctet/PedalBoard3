#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>

namespace pedalboard3::engine
{

// The test tone (#111): a 440 Hz sine at the master stage, used to answer
// "is anything reaching my speakers at all".
//
// It lives in AudioGraphRuntime next to the MasterMute, for the same reason
// and with one more of its own:
//   - it works on a board with nothing on it,
//   - a patch change leaves it alone,
//   - and IT DOES NOT TOUCH THE BOARD.
//
// That last one is the whole point of this class. The first version of the
// tone was a graph: switching it on cleared the patch and loaded a two-node
// graph in its place, and switching it off reloaded the patch from the bank.
// So a diagnostic you reach for when you are unsure whether the audio path
// works would empty your canvas, and on the way back every plugin was
// destroyed and re-instantiated from its state blob. Anything that does not
// survive that round trip -- an iLok title, a single-instance plugin, one
// that is slow to authorise -- came back as a missing-plugin placeholder,
// and the next save wrote "missing" into the bank. "Test Tone breaks the
// chain" was a literal description of the design.
//
// As a master stage there is nothing to break: the graph keeps running
// untouched behind the tone, the canvas keeps its cards, no plugin is
// destroyed, and switching off is a 15 ms fade rather than a patch reload.
//
// The tone REPLACES the output rather than mixing with it -- crossfaded, so
// neither edge clicks. A test tone you have to pick out from whatever the
// board is already doing is not a test tone.
//
// Order in the runtime: after the OutputSanitiser (so a chain producing NaN
// cannot poison the one signal you are using to prove the path is clean) and
// before the MasterMute (so muted means silent, tone or no tone -- the status
// bar promises exactly that).
//
// Threading. setEnabled comes from the message thread; process() is the audio
// thread. The only shared state is one relaxed atomic bool: the audio thread
// reads a target and walks towards it, so a late read costs at most one block
// of fade.
//
// RT contract: process() does not allocate or lock, and returns immediately
// when the tone is off and faded out, which is almost always.
class TestTone
{
public:
    // The crossfade, in milliseconds. Matches MasterMute: long enough not to
    // click, short enough that the switch feels immediate.
    static constexpr float kRampMs = 15.0f;
    static constexpr float kFrequencyHz = 440.0f;
    // -12 dBFS. Loud enough to hear over a noisy stage, quiet enough that it
    // is not a punishment on headphones.
    static constexpr float kAmplitude = 0.25f;

    // Audio thread setup. Safe to call again on a device change.
    void prepare(double sampleRate) noexcept
    {
        const double samples = sampleRate * static_cast<double>(kRampMs) / 1000.0;
        step_ = samples > 1.0 ? static_cast<float>(1.0 / samples) : 1.0f;
        phaseStep_ = sampleRate > 0.0 ? juce::MathConstants<double>::twoPi * kFrequencyHz / sampleRate : 0.0;
    }

    // Message thread.
    void setEnabled(bool shouldPlay) noexcept { enabled_.store(shouldPlay, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

    // Audio thread. Call after the sanitiser, before the mute.
    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        const float target = enabled_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;

        // Off and faded out: the common case, one load and one compare. The
        // phase is parked at zero so the next switch-on starts at a zero
        // crossing -- not that it could click through the fade, but a tone
        // that always begins the same way is easier to recognise.
        if (mix_ <= 0.0f && target <= 0.0f)
        {
            phase_ = 0.0;
            return;
        }

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        // Samples on the outside, channels on the inside -- the opposite of
        // MasterMute. The sine is per sample, not per channel: hoisting the
        // channel loop out would cost one std::sin per channel per sample,
        // and it keeps the tone identical (centred) on every channel.
        double phase = phase_;
        float m = mix_;
        for (int n = 0; n < numSamples; ++n)
        {
            m = m < target ? juce::jmin(target, m + step_) : juce::jmax(target, m - step_);
            const float tone = static_cast<float>(std::sin(phase)) * kAmplitude;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = buffer.getWritePointer(ch);
                d[n] = d[n] * (1.0f - m) + tone * m;
            }

            phase += phaseStep_;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
        }

        phase_ = phase;
        mix_ = m;
    }

    // Test and diagnostic hook: where the crossfade has got to.
    float getCurrentMix() const noexcept { return mix_; }

private:
    std::atomic<bool> enabled_{false};
    double phase_{0.0};
    double phaseStep_{0.0};
    float mix_{0.0f};
    float step_{1.0f};
};

} // namespace pedalboard3::engine
