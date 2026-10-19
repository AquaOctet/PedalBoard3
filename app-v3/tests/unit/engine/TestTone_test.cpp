// Tests for pedalboard3::engine::TestTone -- the master-stage test tone
// (#111).
//
// The property that matters most here is not the sine. It is that the tone
// is a stage the output passes through, so it can be switched on and off
// without anything else in the system being touched. The design it replaced
// switched the tone on by clearing the patch and loading a tone-only graph,
// which is why the bug report was "the whole chain vanishes".
//
// So: silence when off, a clean 440 Hz at the stated amplitude when on, a
// crossfade at both edges rather than a jump, and the incoming signal
// replaced rather than mixed with.

#include <catch2/catch_test_macros.hpp>

#include "engine/TestTone.h"

#include <cmath>
#include <vector>

using namespace pedalboard3;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

juce::AudioBuffer<float> filledBlock(float value, int channels = 2, int samples = kBlock)
{
    juce::AudioBuffer<float> b(channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::fill(b.getWritePointer(ch), value, samples);
    return b;
}

// Run `blocks` blocks of constant input through the tone and return every
// sample of channel 0 in order.
std::vector<float> run(engine::TestTone& tone, int blocks, float input = 0.0f)
{
    std::vector<float> out;
    for (int i = 0; i < blocks; ++i)
    {
        auto b = filledBlock(input);
        tone.process(b);
        const auto* d = b.getReadPointer(0);
        out.insert(out.end(), d, d + kBlock);
    }
    return out;
}

float peakOf(const std::vector<float>& v)
{
    float p = 0.0f;
    for (const float s : v)
        p = juce::jmax(p, std::abs(s));
    return p;
}

// The largest step between neighbouring samples. A switch that jumps rather
// than fades shows up here as a step close to the full amplitude.
float largestStep(const std::vector<float>& v)
{
    float worst = 0.0f;
    for (size_t i = 1; i < v.size(); ++i)
        worst = juce::jmax(worst, std::abs(v[i] - v[i - 1]));
    return worst;
}

} // namespace

TEST_CASE("TestTone: off and settled passes the buffer through untouched", "[engine][test-tone]")
{
    engine::TestTone tone;
    tone.prepare(kSampleRate);

    auto b = filledBlock(0.5f);
    tone.process(b);

    for (int n = 0; n < kBlock; ++n)
        REQUIRE(b.getSample(0, n) == 0.5f);
    REQUIRE(tone.getCurrentMix() == 0.0f);
}

TEST_CASE("TestTone: enabled and settled, the output is the tone at its stated amplitude", "[engine][test-tone]")
{
    engine::TestTone tone;
    tone.prepare(kSampleRate);
    tone.setEnabled(true);

    run(tone, 4); // past the 15 ms crossfade
    const auto settled = run(tone, 4);

    // Within a sample or two of full amplitude: a 440 Hz sine at 48k has
    // plenty of samples near its peak in 4 blocks.
    REQUIRE(peakOf(settled) > engine::TestTone::kAmplitude * 0.99f);
    REQUIRE(peakOf(settled) <= engine::TestTone::kAmplitude + 1.0e-6f);
    REQUIRE(tone.getCurrentMix() == 1.0f);
}

TEST_CASE("TestTone: the tone replaces the signal rather than mixing with it", "[engine][test-tone]")
{
    // This is the audible contract. A test tone you have to pick out from
    // whatever the board is already doing is not a test tone -- and if it
    // summed, a loud board would hide it entirely, which is exactly the
    // situation you reach for a test tone in.
    engine::TestTone tone;
    tone.prepare(kSampleRate);
    tone.setEnabled(true);

    run(tone, 4, /*input*/ 1.0f);
    const auto settled = run(tone, 4, /*input*/ 1.0f);

    // Full-scale DC in. If it were summing, everything would sit around 1.0.
    REQUIRE(peakOf(settled) <= engine::TestTone::kAmplitude + 1.0e-6f);

    // And the tone is still a tone: it goes negative, which DC + tone at
    // this amplitude never would.
    bool sawNegative = false;
    for (const float s : settled)
        sawNegative = sawNegative || s < -0.1f;
    REQUIRE(sawNegative);
}

TEST_CASE("TestTone: switching on and off crossfades instead of jumping", "[engine][test-tone]")
{
    engine::TestTone tone;
    tone.prepare(kSampleRate);

    // Full-scale DC in, so a jump at either edge is a step of ~1.0 and
    // impossible to miss.
    const float toneStep = engine::TestTone::kAmplitude * juce::MathConstants<float>::twoPi *
                           engine::TestTone::kFrequencyHz / static_cast<float>(kSampleRate);

    tone.setEnabled(true);
    const auto fadeIn = run(tone, 4, 1.0f);
    tone.setEnabled(false);
    const auto fadeOut = run(tone, 4, 1.0f);

    // The only legitimate sample-to-sample movement is the sine's own slope
    // plus one step of the fade. Anything near the 1.0 discontinuity a jump
    // would produce is a click.
    const float budget =
        toneStep + 1.0f / (static_cast<float>(kSampleRate) * engine::TestTone::kRampMs / 1000.0f) + 0.01f;
    REQUIRE(largestStep(fadeIn) < budget);
    REQUIRE(largestStep(fadeOut) < budget);

    // And it really did come back to the input.
    REQUIRE(fadeOut.back() == 1.0f);
    REQUIRE(tone.getCurrentMix() == 0.0f);
}

TEST_CASE("TestTone: every channel gets the same tone", "[engine][test-tone]")
{
    engine::TestTone tone;
    tone.prepare(kSampleRate);
    tone.setEnabled(true);

    for (int i = 0; i < 4; ++i)
    {
        auto b = filledBlock(0.0f, /*channels*/ 4);
        tone.process(b);
    }

    auto b = filledBlock(0.0f, /*channels*/ 4);
    tone.process(b);
    for (int n = 0; n < kBlock; ++n)
        for (int ch = 1; ch < 4; ++ch)
            REQUIRE(b.getSample(ch, n) == b.getSample(0, n));
}

TEST_CASE("TestTone: without prepare it stays silent rather than emitting DC", "[engine][test-tone]")
{
    // prepare() sets the phase step. Left at zero, sin(0) is 0 for every
    // sample, so an unprepared tone fades a buffer of silence towards a
    // buffer of silence -- it must not, for instance, hold the first sample
    // as DC.
    engine::TestTone tone;
    tone.setEnabled(true);
    const auto out = run(tone, 4, 0.0f);
    REQUIRE(peakOf(out) == 0.0f);
}
