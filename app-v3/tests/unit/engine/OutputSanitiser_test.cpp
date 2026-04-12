// Tests for pedalboard3::engine::OutputSanitiser — the final-stage guard
// that prevents NaN / Inf / |x|>1.5 from reaching the audio device.

#include <catch2/catch_test_macros.hpp>

#include "engine/processors/OutputSanitiser.h"
#include "support/OfflineRender.h"

#include <cmath>
#include <limits>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{
// JUCE AudioProcessors are non-copyable and non-movable, so every test
// constructs the sanitiser in place and calls this helper to prepare it.
void prepare(engine::processors::OutputSanitiser& s, int channels = 2, int blockSize = kDefaultBlockSize)
{
    s.setPlayConfigDetails(channels, channels, kDefaultSampleRate, blockSize);
    s.prepareToPlay(kDefaultSampleRate, blockSize);
}
} // namespace

TEST_CASE("OutputSanitiser: clean audio passes through unchanged", "[engine][sanitiser]")
{
    engine::processors::OutputSanitiser sanitiser;
    prepare(sanitiser);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillSine(buf, /*freq*/ 440.0);
    const float peakBefore = peakAbs(buf);

    renderBlocks(sanitiser, buf);

    REQUIRE(isFiniteEverywhere(buf));
    REQUIRE(peakAbs(buf) == peakBefore);
    REQUIRE_FALSE(sanitiser.consumeBadSampleFlag());
}

TEST_CASE("OutputSanitiser: silence stays silent", "[engine][sanitiser]")
{
    engine::processors::OutputSanitiser sanitiser;
    prepare(sanitiser);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    renderBlocks(sanitiser, buf);

    REQUIRE(isSilent(buf));
    REQUIRE_FALSE(sanitiser.consumeBadSampleFlag());
}

TEST_CASE("OutputSanitiser: NaN is replaced with zero and bad flag is set", "[engine][sanitiser]")
{
    engine::processors::OutputSanitiser sanitiser;
    prepare(sanitiser);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, std::numeric_limits<float>::quiet_NaN());

    renderBlocks(sanitiser, buf);

    REQUIRE(isSilent(buf));
    REQUIRE(sanitiser.consumeBadSampleFlag());

    // Flag is one-shot: after consume it should clear.
    REQUIRE_FALSE(sanitiser.consumeBadSampleFlag());
}

TEST_CASE("OutputSanitiser: positive infinity is replaced with zero", "[engine][sanitiser]")
{
    engine::processors::OutputSanitiser sanitiser;
    prepare(sanitiser);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, std::numeric_limits<float>::infinity());

    renderBlocks(sanitiser, buf);

    REQUIRE(isSilent(buf));
    REQUIRE(sanitiser.consumeBadSampleFlag());
}

TEST_CASE("OutputSanitiser: negative infinity is replaced with zero", "[engine][sanitiser]")
{
    engine::processors::OutputSanitiser sanitiser;
    prepare(sanitiser);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, -std::numeric_limits<float>::infinity());

    renderBlocks(sanitiser, buf);

    REQUIRE(isSilent(buf));
    REQUIRE(sanitiser.consumeBadSampleFlag());
}

TEST_CASE("OutputSanitiser: samples with |x| > 1.5 are zeroed", "[engine][sanitiser]")
{
    engine::processors::OutputSanitiser sanitiser;
    prepare(sanitiser);

    // Signal amplitude 2.0 — exceeds the ±1.5 ceiling everywhere above 0.75 sin.
    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 2.0f);

    renderBlocks(sanitiser, buf);

    REQUIRE(isSilent(buf));
    REQUIRE(sanitiser.consumeBadSampleFlag());
}

TEST_CASE("OutputSanitiser: samples just inside the ceiling pass through", "[engine][sanitiser]")
{
    engine::processors::OutputSanitiser sanitiser;
    prepare(sanitiser);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 1.49f);

    renderBlocks(sanitiser, buf);

    REQUIRE(peakAbs(buf) > 0.0f);
    REQUIRE_FALSE(sanitiser.consumeBadSampleFlag());
}

TEST_CASE("OutputSanitiser: mixed good and bad samples — bad zeroed, good preserved", "[engine][sanitiser]")
{
    engine::processors::OutputSanitiser sanitiser;
    prepare(sanitiser);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillSine(buf, 220.0);

    // Poison specific sample indices with NaN on channel 0 and Inf on channel 1.
    buf.getWritePointer(0)[10] = std::numeric_limits<float>::quiet_NaN();
    buf.getWritePointer(0)[200] = 3.14f; // exceeds 1.5 — sorry, Pi
    buf.getWritePointer(1)[42] = std::numeric_limits<float>::infinity();
    buf.getWritePointer(1)[77] = -2.0f;

    renderBlocks(sanitiser, buf);

    // Poisoned samples are now zero.
    REQUIRE(buf.getReadPointer(0)[10] == 0.0f);
    REQUIRE(buf.getReadPointer(0)[200] == 0.0f);
    REQUIRE(buf.getReadPointer(1)[42] == 0.0f);
    REQUIRE(buf.getReadPointer(1)[77] == 0.0f);

    // Other samples are still finite (the sine didn't get flattened).
    REQUIRE(isFiniteEverywhere(buf));
    REQUIRE(peakAbs(buf) > 0.0f);

    REQUIRE(sanitiser.consumeBadSampleFlag());
}
