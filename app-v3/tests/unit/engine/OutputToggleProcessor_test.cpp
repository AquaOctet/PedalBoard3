// Tests for engine::processors::OutputToggleProcessor (M6 U10): the input,
// mixed to mono, goes to output 1 or output 2 with a 20 ms crossfade, and its
// switch round-trips through the node state (ProcessorBase).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/processors/OutputToggleProcessor.h"

using Catch::Approx;
using pedalboard3::engine::processors::OutputToggleProcessor;

namespace
{

juce::AudioParameterChoice& outputParameter(OutputToggleProcessor& p)
{
    for (auto* parameter : p.getParameters())
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter))
            return *choice;
    FAIL("no Output parameter");
    throw std::logic_error("unreachable");
}

// One block of `samples` with left = `left` and right = `right`, through `p`.
juce::AudioBuffer<float> run(OutputToggleProcessor& p, int samples, float left = 1.0f, float right = 1.0f)
{
    juce::AudioBuffer<float> buffer(2, samples);
    for (int n = 0; n < samples; ++n)
    {
        buffer.setSample(0, n, left);
        buffer.setSample(1, n, right);
    }
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
    return buffer;
}

} // namespace

TEST_CASE("OutputToggle: by default everything goes to output 1, mixed to mono", "[engine][processors][output-toggle]")
{
    OutputToggleProcessor p;
    p.prepareToPlay(48000.0, 512);
    const auto out = run(p, 512, 1.0f, 0.0f);
    REQUIRE(out.getSample(0, 0) == Approx(0.5f)); // (1 + 0) / 2
    REQUIRE(out.getSample(0, 511) == Approx(0.5f));
    REQUIRE(out.getSample(1, 511) == Approx(0.0f));
}

TEST_CASE("OutputToggle: switching crossfades over 20 ms, then sends everything to output 2",
          "[engine][processors][output-toggle]")
{
    for (const double rate : {44100.0, 48000.0})
    {
        OutputToggleProcessor p;
        p.prepareToPlay(rate, 4096);
        outputParameter(p) = 1; // Output 2

        const int fade = static_cast<int>(rate * OutputToggleProcessor::kFadeSeconds);
        const auto out = run(p, fade * 2);
        // Halfway through the fade the input is split evenly between the two.
        REQUIRE(out.getSample(0, fade / 2) == Approx(0.5f).margin(0.02));
        REQUIRE(out.getSample(1, fade / 2) == Approx(0.5f).margin(0.02));
        // Once it's done, all of it is on output 2.
        REQUIRE(out.getSample(0, fade + 10) == Approx(0.0f).margin(1e-6));
        REQUIRE(out.getSample(1, fade + 10) == Approx(1.0f));
        // Never louder than the input, so a switch never jumps.
        for (int n = 0; n < out.getNumSamples(); ++n)
            REQUIRE(out.getSample(0, n) + out.getSample(1, n) == Approx(1.0f).margin(1e-5));
    }
}

TEST_CASE("OutputToggle: the switch round-trips through the node state", "[engine][processors][output-toggle]")
{
    OutputToggleProcessor a;
    outputParameter(a) = 1;
    juce::MemoryBlock state;
    a.getStateInformation(state);

    OutputToggleProcessor b;
    REQUIRE(b.getOutput() == 0);
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(b.getOutput() == 1);

    // Another processor's state (or garbage) leaves the default.
    OutputToggleProcessor c;
    const char junk[] = "not a state";
    c.setStateInformation(junk, sizeof(junk));
    REQUIRE(c.getOutput() == 0);
}

TEST_CASE("OutputToggle: it starts where its switch is, without fading in", "[engine][processors][output-toggle]")
{
    OutputToggleProcessor p;
    outputParameter(p) = 1;
    p.prepareToPlay(48000.0, 64);
    const auto out = run(p, 64);
    REQUIRE(out.getSample(1, 0) == Approx(1.0f));
    REQUIRE(out.getSample(0, 0) == Approx(0.0f).margin(1e-6));
}
