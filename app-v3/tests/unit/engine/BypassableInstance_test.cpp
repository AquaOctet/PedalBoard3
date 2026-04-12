// Tests for pedalboard3::engine::BypassableInstance — the ramped-bypass
// wrapper around a hosted plugin. Phase 1.A scope: forward pass, ramped
// bypass in both directions, MIDI channel filter, CPU metering. SEH/signal
// crash isolation and the watchdog timer arrive in Phase 1.B.

#include <catch2/catch_test_macros.hpp>

#include "engine/BypassableInstance.h"
#include "support/OfflineRender.h"
#include "support/TestGainProcessor.h"

#include <memory>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{
std::unique_ptr<engine::BypassableInstance> makeBypassable(float gain = 2.0f, int blockSize = kDefaultBlockSize)
{
    auto inner = std::make_unique<TestGainProcessor>(gain);
    auto* innerRaw = inner.get(); // kept for test inspection
    inner->setPlayConfigDetails(2, 2, kDefaultSampleRate, blockSize);
    auto wrapper = std::make_unique<engine::BypassableInstance>(std::move(inner));
    wrapper->setPlayConfigDetails(2, 2, kDefaultSampleRate, blockSize);
    wrapper->prepareToPlay(kDefaultSampleRate, blockSize);
    (void)innerRaw; // for future extension
    return wrapper;
}
} // namespace

TEST_CASE("BypassableInstance: passes audio through the wrapped processor when not bypassed", "[engine][bypassable]")
{
    auto wrapper = makeBypassable(/*gain*/ 2.0f);
    REQUIRE_FALSE(wrapper->isBypassed());

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.25f);

    renderBlocks(*wrapper, buf);

    // Gain of 2 applied to DC 0.25 → 0.5 everywhere.
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int n = 0; n < buf.getNumSamples(); ++n)
            REQUIRE(buf.getReadPointer(ch)[n] == 0.5f);
}

TEST_CASE("BypassableInstance: steady-state bypass passes dry signal unchanged", "[engine][bypassable]")
{
    auto wrapper = makeBypassable(/*gain*/ 2.0f);
    wrapper->setBypass(true);

    // Run a few blocks to let the ramp settle to 100% dry before measuring.
    auto warm = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(warm, 0.0f);
    for (int i = 0; i < 10; ++i)
        renderBlocks(*wrapper, warm);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.25f);
    renderBlocks(*wrapper, buf);

    // Bypassed wrapper should output dry 0.25 (not the gained 0.5).
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int n = 0; n < buf.getNumSamples(); ++n)
            REQUIRE(buf.getReadPointer(ch)[n] == 0.25f);
}

TEST_CASE("BypassableInstance: toggling bypass ramps instead of clicking", "[engine][bypassable]")
{
    auto wrapper = makeBypassable(/*gain*/ 2.0f);
    REQUIRE_FALSE(wrapper->isBypassed());

    // Let the wet-state ramp settle to fully wet.
    auto warm = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(warm, 0.0f);
    for (int i = 0; i < 10; ++i)
        renderBlocks(*wrapper, warm);

    wrapper->setBypass(true);

    // First post-toggle block: output should be a monotonic ramp between
    // the fully-wet value (0.5) and the fully-dry value (0.25). With a
    // linear 20 ms ramp at 48 kHz, a 256-sample block covers ~5.3 ms —
    // the ramp does not complete in one block, but it DOES move.
    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.25f);
    renderBlocks(*wrapper, buf);

    const float first = buf.getReadPointer(0)[0];
    const float last = buf.getReadPointer(0)[kDefaultBlockSize - 1];

    // Moved toward dry.
    REQUIRE(first > last);
    // First sample is still closer to wet than dry (ramp hasn't completed).
    REQUIRE(first > 0.26f);
    // Last sample is closer to dry than wet.
    REQUIRE(last < 0.49f);
    // No NaNs / no out-of-range.
    REQUIRE(isFiniteEverywhere(buf));
}

TEST_CASE("BypassableInstance: MIDI channel filter — 0 passes everything", "[engine][bypassable][midi]")
{
    auto wrapper = makeBypassable();
    wrapper->setMidiChannel(0); // omni

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(3, 60, (juce::uint8)100), 0);
    midi.addEvent(juce::MidiMessage::noteOn(11, 60, (juce::uint8)100), 0);

    renderBlocks(*wrapper, buf, midi);

    auto* inner = dynamic_cast<TestGainProcessor*>(wrapper->getHostedProcessor());
    REQUIRE(inner != nullptr);
    REQUIRE(inner->lastMidiEventCount.load() == 2);
}

TEST_CASE("BypassableInstance: MIDI channel filter — non-zero drops off-channel events", "[engine][bypassable][midi]")
{
    auto wrapper = makeBypassable();
    wrapper->setMidiChannel(3);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(3, 60, (juce::uint8)100), 0);  // kept
    midi.addEvent(juce::MidiMessage::noteOn(11, 60, (juce::uint8)100), 0); // dropped

    renderBlocks(*wrapper, buf, midi);

    auto* inner = dynamic_cast<TestGainProcessor*>(wrapper->getHostedProcessor());
    REQUIRE(inner != nullptr);
    REQUIRE(inner->lastMidiEventCount.load() == 1);
}

TEST_CASE("BypassableInstance: CPU metering accumulates across blocks", "[engine][bypassable][metering]")
{
    auto wrapper = makeBypassable();

    // Baseline reads zero.
    const auto baseline = wrapper->consumeCpuStats();
    REQUIRE(baseline.blockCount == 0);
    REQUIRE(baseline.elapsedNanos == 0);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillSine(buf, 440.0);
    const int kFrames = kDefaultBlockSize * 4;
    buf.setSize(2, kFrames, true, false, true);

    renderBlocks(*wrapper, buf);
    const auto after = wrapper->consumeCpuStats();

    // Four blocks of 256 samples processed.
    REQUIRE(after.blockCount == 4);
    REQUIRE(after.elapsedNanos > 0);

    // After consume, next read is zeroed.
    const auto cleared = wrapper->consumeCpuStats();
    REQUIRE(cleared.blockCount == 0);
}

TEST_CASE("BypassableInstance: getLatencySamples is 0 when bypassed, child's value otherwise",
          "[engine][bypassable][latency]")
{
    auto wrapper = makeBypassable();
    auto* inner = dynamic_cast<TestGainProcessor*>(wrapper->getHostedProcessor());
    REQUIRE(inner != nullptr);
    inner->setLatencySamples(123);

    wrapper->setBypass(false);
    REQUIRE(wrapper->getLatencySamples() == 123);

    wrapper->setBypass(true);
    REQUIRE(wrapper->getLatencySamples() == 0);
}
