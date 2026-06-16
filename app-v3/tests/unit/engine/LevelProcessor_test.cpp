// Offline-render tests for engine::processors::LevelProcessor -- a stereo gain
// trim with one managed "level" parameter (0..1, default 0.5; out = in*level*2).

#include <catch2/catch_test_macros.hpp>

#include "engine/processors/LevelProcessor.h"
#include "support/OfflineRender.h"

#include <juce_audio_processors/juce_audio_processors.h>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{
void prepare(juce::AudioProcessor& p, int channels = 2, int bs = kDefaultBlockSize)
{
    p.setPlayConfigDetails(channels, channels, kDefaultSampleRate, bs);
    p.prepareToPlay(kDefaultSampleRate, bs);
}
} // namespace

TEST_CASE("LevelProcessor: default 0.5 is unity gain", "[engine][level]")
{
    engine::processors::LevelProcessor proc;
    prepare(proc);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.3f);
    renderBlocks(proc, buf);

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int n = 0; n < buf.getNumSamples(); ++n)
            REQUIRE(buf.getReadPointer(ch)[n] == 0.3f);
}

TEST_CASE("LevelProcessor: level 1.0 doubles the signal", "[engine][level]")
{
    engine::processors::LevelProcessor proc;
    prepare(proc);
    proc.getParameters()[0]->setValueNotifyingHost(1.0f);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.25f);
    renderBlocks(proc, buf);

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int n = 0; n < buf.getNumSamples(); ++n)
            REQUIRE(buf.getReadPointer(ch)[n] == 0.5f);
}

TEST_CASE("LevelProcessor: level 0.0 silences", "[engine][level]")
{
    engine::processors::LevelProcessor proc;
    prepare(proc);
    proc.getParameters()[0]->setValueNotifyingHost(0.0f);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillSine(buf, 440.0);
    renderBlocks(proc, buf);

    REQUIRE(isSilent(buf));
}

TEST_CASE("LevelProcessor: unity preserves a sine and stays finite", "[engine][level]")
{
    engine::processors::LevelProcessor proc;
    prepare(proc);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillSine(buf, 440.0, kDefaultSampleRate, 0.5f);
    const float before = peakAbs(buf);
    renderBlocks(proc, buf);

    REQUIRE(peakAbs(buf) == before); // default 0.5 == unity (x1.0 is exact)
    REQUIRE(isFiniteEverywhere(buf));
}

TEST_CASE("LevelProcessor: finite at max gain", "[engine][level]")
{
    engine::processors::LevelProcessor proc;
    prepare(proc);
    proc.getParameters()[0]->setValueNotifyingHost(1.0f);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillSine(buf, 440.0, kDefaultSampleRate, 0.5f);
    renderBlocks(proc, buf);

    REQUIRE(isFiniteEverywhere(buf));
    REQUIRE(peakAbs(buf) <= 1.0001f); // amp 0.5 * 2 ~= 1.0
}

TEST_CASE("LevelProcessor: exposes one automatable parameter the control surface drives", "[engine][level]")
{
    engine::processors::LevelProcessor proc;
    prepare(proc);
    REQUIRE(proc.getParameters().size() == 1);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.4f);
    renderBlocks(proc, buf);
    REQUIRE(buf.getReadPointer(0)[0] == 0.4f); // unity at default 0.5

    // setValueNotifyingHost is exactly what ControlParameterApplier calls.
    proc.getParameters()[0]->setValueNotifyingHost(1.0f);
    fillDC(buf, 0.4f);
    renderBlocks(proc, buf);
    REQUIRE(buf.getReadPointer(0)[0] == 0.8f); // doubled
}
