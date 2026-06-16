// Offline-render tests for engine::processors::VuMeterProcessor -- a stereo
// passthrough peak meter (instant attack, 1e-5/sample linear decay) publishing
// per-channel level via atomics. Deterministic, hardware-free.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/processors/VuMeterProcessor.h"
#include "support/OfflineRender.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <limits>

using namespace pedalboard3;
using namespace pedalboard3::testing;
using Catch::Approx;

namespace
{
void prepare(juce::AudioProcessor& p, int channels = 2, int bs = kDefaultBlockSize)
{
    p.setPlayConfigDetails(channels, channels, kDefaultSampleRate, bs);
    p.prepareToPlay(kDefaultSampleRate, bs);
}

void fillChannel(juce::AudioBuffer<float>& buf, int ch, float value)
{
    auto* d = buf.getWritePointer(ch);
    for (int n = 0; n < buf.getNumSamples(); ++n)
        d[n] = value;
}
} // namespace

TEST_CASE("VuMeter: passes audio through unchanged", "[engine][vumeter]")
{
    engine::processors::VuMeterProcessor proc;
    prepare(proc);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillSine(buf, 440.0, kDefaultSampleRate, 0.5f);

    juce::AudioBuffer<float> copy;
    copy.makeCopyOf(buf);

    renderBlocks(proc, buf);

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int n = 0; n < buf.getNumSamples(); ++n)
            REQUIRE(buf.getReadPointer(ch)[n] == copy.getReadPointer(ch)[n]);
}

TEST_CASE("VuMeter: instant attack to the peak", "[engine][vumeter]")
{
    engine::processors::VuMeterProcessor proc;
    prepare(proc);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.7f);
    renderBlocks(proc, buf);

    REQUIRE(proc.getLeftLevel() == Approx(0.7f));
    REQUIRE(proc.getRightLevel() == Approx(0.7f));
}

TEST_CASE("VuMeter: deterministic linear decay", "[engine][vumeter]")
{
    engine::processors::VuMeterProcessor proc;
    prepare(proc);

    auto attack = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(attack, 0.7f);
    renderBlocks(proc, attack);
    REQUIRE(proc.getLeftLevel() == Approx(0.7f));

    auto silence = makeTestBuffer(2, kDefaultBlockSize); // 256 zeros
    renderBlocks(proc, silence);
    // 256 samples * 1e-5/sample decay from 0.7.
    REQUIRE(proc.getLeftLevel() == Approx(0.7f - 256.0f * 0.00001f).margin(1e-4));
}

TEST_CASE("VuMeter: decay floors at zero, never negative", "[engine][vumeter]")
{
    engine::processors::VuMeterProcessor proc;
    prepare(proc);

    auto attack = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(attack, 0.7f);
    renderBlocks(proc, attack);

    // 0.7 / 1e-5 = 70000 samples to reach 0; render well past that.
    auto silence = makeTestBuffer(2, 80000);
    renderBlocks(proc, silence);

    REQUIRE(proc.getLeftLevel() == 0.0f);
    REQUIRE(proc.getRightLevel() == 0.0f);
}

TEST_CASE("VuMeter: left and right meter independently", "[engine][vumeter]")
{
    engine::processors::VuMeterProcessor proc;
    prepare(proc);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillChannel(buf, 0, 0.6f);
    fillChannel(buf, 1, 0.2f);
    renderBlocks(proc, buf);

    REQUIRE(proc.getLeftLevel() == Approx(0.6f));
    REQUIRE(proc.getRightLevel() == Approx(0.2f));
}

TEST_CASE("VuMeter: a NaN sample does not latch the meter into NaN", "[engine][vumeter]")
{
    engine::processors::VuMeterProcessor proc;
    prepare(proc);

    auto dc = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(dc, 0.5f);
    renderBlocks(proc, dc);
    REQUIRE(proc.getLeftLevel() == Approx(0.5f));

    auto nan = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(nan, std::numeric_limits<float>::quiet_NaN());
    renderBlocks(proc, nan);
    REQUIRE(std::isfinite(proc.getLeftLevel())); // decayed, not NaN
}
