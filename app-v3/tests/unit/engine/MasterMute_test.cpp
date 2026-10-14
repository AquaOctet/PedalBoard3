// Tests for pedalboard3::engine::MasterMute -- the master gain stage the
// output always passes through (#106). What matters is that it reaches
// silence, comes back, and never gets there in one sample.

#include <catch2/catch_test_macros.hpp>

#include "engine/MasterMute.h"

#include <cmath>

using namespace pedalboard3;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

// A block of full-scale DC, which makes every gain change visible in the
// sample values and makes a discontinuity impossible to miss.
juce::AudioBuffer<float> dcBlock(int channels = 2, int samples = kBlock, float value = 1.0f)
{
    juce::AudioBuffer<float> b(channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::fill(b.getWritePointer(ch), value, samples);
    return b;
}

// Run blocks until the gain settles at `target`, or give up. Returns the
// number of blocks it took, and every sample seen along the way.
struct RunResult
{
    int blocks{0};
    std::vector<float> samples; // channel 0, in order
    bool settled{false};
};

RunResult runUntilSettled(engine::MasterMute& mute, float target, int maxBlocks = 64)
{
    RunResult r;
    for (int i = 0; i < maxBlocks; ++i)
    {
        auto b = dcBlock();
        mute.process(b);
        const auto* d = b.getReadPointer(0);
        r.samples.insert(r.samples.end(), d, d + b.getNumSamples());
        ++r.blocks;
        if (std::fabs(mute.getCurrentGain() - target) < 1.0e-6f)
        {
            r.settled = true;
            break;
        }
    }
    return r;
}

// The largest jump between consecutive samples. On full-scale DC this is
// exactly the gain step, so it is the thing that would be a click.
float largestStep(const std::vector<float>& xs)
{
    float worst = 0.0f;
    for (std::size_t i = 1; i < xs.size(); ++i)
        worst = std::max(worst, std::fabs(xs[i] - xs[i - 1]));
    return worst;
}
} // namespace

TEST_CASE("MasterMute: passes audio through untouched when it is not muted", "[engine][mute]")
{
    engine::MasterMute mute;
    mute.prepare(kSampleRate);

    auto b = dcBlock();
    mute.process(b);

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int n = 0; n < b.getNumSamples(); ++n)
            REQUIRE(b.getSample(ch, n) == 1.0f);
}

TEST_CASE("MasterMute: reaches exact silence, and comes back to exactly unity", "[engine][mute]")
{
    engine::MasterMute mute;
    mute.prepare(kSampleRate);

    mute.setMuted(true);
    const auto down = runUntilSettled(mute, 0.0f);
    REQUIRE(down.settled);
    REQUIRE(mute.getCurrentGain() == 0.0f);

    // Settled shut, every sample of every later block is exactly zero -- not
    // merely small. Anything else is audible leakage on a muted rig.
    auto shut = dcBlock();
    mute.process(shut);
    for (int ch = 0; ch < shut.getNumChannels(); ++ch)
        for (int n = 0; n < shut.getNumSamples(); ++n)
            REQUIRE(shut.getSample(ch, n) == 0.0f);

    mute.setMuted(false);
    const auto up = runUntilSettled(mute, 1.0f);
    REQUIRE(up.settled);
    REQUIRE(mute.getCurrentGain() == 1.0f);

    auto open = dcBlock();
    mute.process(open);
    for (int ch = 0; ch < open.getNumChannels(); ++ch)
        for (int n = 0; n < open.getNumSamples(); ++n)
            REQUIRE(open.getSample(ch, n) == 1.0f);
}

TEST_CASE("MasterMute: the fade is the length it says it is", "[engine][mute]")
{
    engine::MasterMute mute;
    mute.prepare(kSampleRate);

    mute.setMuted(true);
    const auto down = runUntilSettled(mute, 0.0f);
    REQUIRE(down.settled);

    const double expected = kSampleRate * static_cast<double>(engine::MasterMute::kRampMs) / 1000.0;
    // It settles within one block of the nominal ramp, since it can only
    // finish on a block boundary.
    REQUIRE(static_cast<double>(down.samples.size()) >= expected);
    REQUIRE(static_cast<double>(down.samples.size()) < expected + kBlock);
}

TEST_CASE("MasterMute: no block contains a discontinuity at the switch", "[engine][mute]")
{
    engine::MasterMute mute;
    mute.prepare(kSampleRate);

    // One step of the ramp, with a little slack for float error. On
    // full-scale DC the sample-to-sample delta IS the gain step, so this is
    // the click test: a jump of 1.0 would be the un-ramped version.
    const float step = 1.0f / static_cast<float>(kSampleRate * engine::MasterMute::kRampMs / 1000.0);

    mute.setMuted(true);
    const auto down = runUntilSettled(mute, 0.0f);
    REQUIRE(down.settled);
    REQUIRE(largestStep(down.samples) <= step * 1.01f);

    mute.setMuted(false);
    const auto up = runUntilSettled(mute, 1.0f);
    REQUIRE(up.settled);
    REQUIRE(largestStep(up.samples) <= step * 1.01f);
}

TEST_CASE("MasterMute: a switch mid-fade turns round from where it got to", "[engine][mute]")
{
    engine::MasterMute mute;
    mute.prepare(kSampleRate);

    // Half a block in, so the fade is under way but nowhere near done.
    mute.setMuted(true);
    auto b = dcBlock();
    mute.process(b);
    const float midGain = mute.getCurrentGain();
    REQUIRE(midGain > 0.0f);
    REQUIRE(midGain < 1.0f);

    mute.setMuted(false);
    auto back = dcBlock();
    mute.process(back);
    REQUIRE(mute.getCurrentGain() > midGain); // turned round rather than jumped

    // And still no discontinuity across the turn.
    const float step = 1.0f / static_cast<float>(kSampleRate * engine::MasterMute::kRampMs / 1000.0);
    std::vector<float> xs(back.getReadPointer(0), back.getReadPointer(0) + back.getNumSamples());
    REQUIRE(largestStep(xs) <= step * 1.01f);
}

TEST_CASE("MasterMute: every channel gets the same ramp", "[engine][mute]")
{
    engine::MasterMute mute;
    mute.prepare(kSampleRate);
    mute.setMuted(true);

    auto b = dcBlock(/*channels*/ 4);
    mute.process(b);

    for (int n = 0; n < b.getNumSamples(); ++n)
        for (int ch = 1; ch < b.getNumChannels(); ++ch)
            REQUIRE(b.getSample(ch, n) == b.getSample(0, n));
}

TEST_CASE("MasterMute: an unprepared stage still mutes rather than passing audio", "[engine][mute]")
{
    // prepare() has not been called, so the step is the fallback 1.0: the
    // fade is one sample. That is a click, but silence beats a stuck-open
    // mute, and it only happens with no audio device.
    engine::MasterMute mute;
    mute.setMuted(true);

    auto b = dcBlock();
    mute.process(b);
    REQUIRE(mute.getCurrentGain() == 0.0f);
    REQUIRE(b.getSample(0, b.getNumSamples() - 1) == 0.0f);
}
