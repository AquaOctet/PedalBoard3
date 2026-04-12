// Tests for pedalboard3::engine::processors::TestToneProcessor — a sine
// generator used as the first audible signal source when verifying that
// audio flows end-to-end through the engine on real hardware. Because it
// is also a useful building block in later integration tests, we treat it
// as a shippable processor rather than a throwaway.

#include <catch2/catch_test_macros.hpp>

#include "engine/processors/TestToneProcessor.h"
#include "support/OfflineRender.h"

#include <cmath>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{
void prepare(engine::processors::TestToneProcessor& t, int blockSize = kDefaultBlockSize, int channels = 2)
{
    t.setPlayConfigDetails(channels, channels, kDefaultSampleRate, blockSize);
    t.prepareToPlay(kDefaultSampleRate, blockSize);
}
} // namespace

TEST_CASE("TestToneProcessor: disabled + warmed up is silent", "[engine][testtone]")
{
    engine::processors::TestToneProcessor t;
    prepare(t);
    // Default construction is disabled.

    auto buf = makeTestBuffer(2, kDefaultBlockSize * 4);
    renderBlocks(t, buf);

    REQUIRE(isSilent(buf));
}

TEST_CASE("TestToneProcessor: when enabled and ramped up, emits signal near target amplitude", "[engine][testtone]")
{
    engine::processors::TestToneProcessor t;
    prepare(t);
    t.setFrequency(440.0f);
    t.setAmplitude(0.5f);
    t.setEnabled(true);

    // Warm up past the ramp (default ~20 ms ≈ 4 blocks @ 256/48k).
    auto warm = makeTestBuffer(2, kDefaultBlockSize);
    for (int i = 0; i < 8; ++i)
        renderBlocks(t, warm);

    auto buf = makeTestBuffer(2, kDefaultBlockSize * 4);
    renderBlocks(t, buf);

    // Peak should be close to the requested amplitude but never exceed it.
    const float peak = peakAbs(buf);
    REQUIRE(peak > 0.45f);
    REQUIRE(peak <= 0.5f);
    REQUIRE(isFiniteEverywhere(buf));
}

TEST_CASE("TestToneProcessor: enable ramps smoothly without a click", "[engine][testtone]")
{
    engine::processors::TestToneProcessor t;
    prepare(t);
    t.setFrequency(220.0f);
    t.setAmplitude(0.5f);

    // Disabled, warmed up — silent.
    auto warm = makeTestBuffer(2, kDefaultBlockSize);
    for (int i = 0; i < 4; ++i)
        renderBlocks(t, warm);

    t.setEnabled(true);

    // First post-enable block: amplitude should rise over the block and the
    // first sample must be very close to zero (ramped-in, not clicked-in).
    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    renderBlocks(t, buf);

    const float first = std::abs(buf.getReadPointer(0)[0]);
    const float last = std::abs(buf.getReadPointer(0)[kDefaultBlockSize - 1]);

    REQUIRE(first < 0.05f); // essentially silent at the start
    REQUIRE(last > first);  // moved toward full amplitude
    REQUIRE(isFiniteEverywhere(buf));
}

TEST_CASE("TestToneProcessor: disable ramps down without a click", "[engine][testtone]")
{
    engine::processors::TestToneProcessor t;
    prepare(t);
    t.setFrequency(330.0f);
    t.setAmplitude(0.5f);
    t.setEnabled(true);

    auto warm = makeTestBuffer(2, kDefaultBlockSize);
    for (int i = 0; i < 8; ++i)
        renderBlocks(t, warm);

    t.setEnabled(false);

    // First post-disable block: should ramp from loud toward silent.
    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    renderBlocks(t, buf);

    // Mid-block sample is between full amplitude and zero.
    const float mid = std::abs(buf.getReadPointer(0)[kDefaultBlockSize / 2]);
    REQUIRE(mid < 0.5f);

    // Continue rendering long enough for the ~20 ms ramp to complete and
    // then some. Inspect only the *final* block so we're measuring the
    // settled state, not the still-ramping portion.
    auto tail = makeTestBuffer(2, kDefaultBlockSize * 8);
    renderBlocks(t, tail);

    juce::AudioBuffer<float> lastBlock(tail.getArrayOfWritePointers(),
                                       tail.getNumChannels(),
                                       tail.getNumSamples() - kDefaultBlockSize,
                                       kDefaultBlockSize);
    REQUIRE(peakAbs(lastBlock) < 0.001f);
}
