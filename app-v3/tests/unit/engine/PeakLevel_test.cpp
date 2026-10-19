// Tests for pedalboard3::engine::PeakLevel -- one metering point. The
// runtime keeps two, for what arrives at the input and what leaves the
// output (#117).

#include <catch2/catch_test_macros.hpp>

#include "engine/PeakLevel.h"

using namespace pedalboard3;

namespace
{
constexpr int kBlock = 128;

juce::AudioBuffer<float> blockWith(float value, int channels = 2)
{
    juce::AudioBuffer<float> b(channels, kBlock);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::fill(b.getWritePointer(ch), value, kBlock);
    return b;
}
} // namespace

TEST_CASE("PeakLevel: starts at zero and reads the block's peak", "[engine][peak-level]")
{
    engine::PeakLevel level;
    REQUIRE(level.peek() == 0.0f);

    level.capture(blockWith(0.4f));
    REQUIRE(level.peek() == 0.4f);
}

TEST_CASE("PeakLevel: reads the loudest channel, not the first", "[engine][peak-level]")
{
    engine::PeakLevel level;
    auto b = blockWith(0.1f);
    b.setSample(1, 7, 0.9f);
    level.capture(b);
    REQUIRE(level.peek() == 0.9f);
}

TEST_CASE("PeakLevel: magnitude, so a negative peak still registers", "[engine][peak-level]")
{
    engine::PeakLevel level;
    level.capture(blockWith(-0.6f));
    REQUIRE(level.peek() == 0.6f);
}

TEST_CASE("PeakLevel: holds the peak across blocks until it is read", "[engine][peak-level]")
{
    // The meter polls at 30 Hz and the audio thread runs far faster than
    // that, so most blocks are never looked at individually. Holding the
    // maximum is what stops a transient between two reads from being
    // invisible -- which, for a meter whose job is "did my guitar make it
    // into the machine", is the case that matters.
    engine::PeakLevel level;
    level.capture(blockWith(0.2f));
    level.capture(blockWith(0.8f));
    level.capture(blockWith(0.1f));

    REQUIRE(level.readAndClear() == 0.8f);
}

TEST_CASE("PeakLevel: reading clears, so the meter falls when the input stops", "[engine][peak-level]")
{
    engine::PeakLevel level;
    level.capture(blockWith(0.8f));
    REQUIRE(level.readAndClear() == 0.8f);

    // Nothing captured since: the next read is silence, not the old peak.
    REQUIRE(level.readAndClear() == 0.0f);

    level.capture(blockWith(0.3f));
    REQUIRE(level.readAndClear() == 0.3f);
}
