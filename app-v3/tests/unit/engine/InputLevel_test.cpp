// Tests for pedalboard3::engine::InputLevel -- what is arriving at the audio
// input, measured at the master stage so an empty or silent board cannot make
// a live input look dead (#117).

#include <catch2/catch_test_macros.hpp>

#include "engine/InputLevel.h"

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

TEST_CASE("InputLevel: starts at zero and reads the block's peak", "[engine][input-level]")
{
    engine::InputLevel level;
    REQUIRE(level.peek() == 0.0f);

    level.capture(blockWith(0.4f));
    REQUIRE(level.peek() == 0.4f);
}

TEST_CASE("InputLevel: reads the loudest channel, not the first", "[engine][input-level]")
{
    engine::InputLevel level;
    auto b = blockWith(0.1f);
    b.setSample(1, 7, 0.9f);
    level.capture(b);
    REQUIRE(level.peek() == 0.9f);
}

TEST_CASE("InputLevel: magnitude, so a negative peak still registers", "[engine][input-level]")
{
    engine::InputLevel level;
    level.capture(blockWith(-0.6f));
    REQUIRE(level.peek() == 0.6f);
}

TEST_CASE("InputLevel: holds the peak across blocks until it is read", "[engine][input-level]")
{
    // The meter polls at 30 Hz and the audio thread runs far faster than
    // that, so most blocks are never looked at individually. Holding the
    // maximum is what stops a transient between two reads from being
    // invisible -- which, for a meter whose job is "did my guitar make it
    // into the machine", is the case that matters.
    engine::InputLevel level;
    level.capture(blockWith(0.2f));
    level.capture(blockWith(0.8f));
    level.capture(blockWith(0.1f));

    REQUIRE(level.readAndClear() == 0.8f);
}

TEST_CASE("InputLevel: reading clears, so the meter falls when the input stops", "[engine][input-level]")
{
    engine::InputLevel level;
    level.capture(blockWith(0.8f));
    REQUIRE(level.readAndClear() == 0.8f);

    // Nothing captured since: the next read is silence, not the old peak.
    REQUIRE(level.readAndClear() == 0.0f);

    level.capture(blockWith(0.3f));
    REQUIRE(level.readAndClear() == 0.3f);
}
