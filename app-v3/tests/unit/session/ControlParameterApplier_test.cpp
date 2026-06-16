// Tests for session::ControlParameterApplier — the bridge that turns a control
// update (MidiTarget + value) into a real parameter set on the resolved live
// processor. Headless: a NodeResolver returns a TestParamProcessor, no device.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "session/ControlParameterApplier.h"
#include "support/TestParamProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

using namespace pedalboard3;
using Catch::Approx;

namespace
{

control::MidiTarget paramTarget(session::NodeId node, int paramIndex)
{
    control::MidiTarget t;
    t.kind = control::MidiTargetKind::PluginParameter;
    t.node = node;
    t.parameterIndex = paramIndex;
    return t;
}

} // namespace

TEST_CASE("ControlParameterApplier: sets the parameter on the resolved node", "[session][control][applier]")
{
    testing::TestParamProcessor proc;
    REQUIRE(proc.getParameters()[0]->getValue() == Approx(1.0f)); // default

    session::ControlParameterApplier applier;
    const session::NodeId node = 7;
    applier.setResolver([&](session::NodeId id) -> juce::AudioProcessor* { return id == node ? &proc : nullptr; });

    REQUIRE(applier.apply(paramTarget(node, 0), 0.5f));
    REQUIRE(proc.getParameters()[0]->getValue() == Approx(0.5f));
}

TEST_CASE("ControlParameterApplier: the applied value actually drives the processor", "[session][control][applier]")
{
    testing::TestParamProcessor proc;
    proc.prepareToPlay(48000.0, 16);

    session::ControlParameterApplier applier;
    applier.setResolver([&](session::NodeId) -> juce::AudioProcessor* { return &proc; });

    REQUIRE(applier.apply(paramTarget(1, 0), 0.25f));

    juce::AudioBuffer<float> buf(2, 16);
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < 16; ++n)
            buf.setSample(ch, n, 1.0f);
    juce::MidiBuffer midi;
    proc.processBlock(buf, midi);

    // Gain param of 0.25 scales the 1.0 input down to 0.25 end to end.
    REQUIRE(buf.getSample(0, 0) == Approx(0.25f));
    REQUIRE(buf.getSample(1, 15) == Approx(0.25f));
}

TEST_CASE("ControlParameterApplier: value is clamped to 0..1", "[session][control][applier]")
{
    testing::TestParamProcessor proc;
    session::ControlParameterApplier applier;
    applier.setResolver([&](session::NodeId) -> juce::AudioProcessor* { return &proc; });

    REQUIRE(applier.apply(paramTarget(1, 0), 5.0f));
    REQUIRE(proc.getParameters()[0]->getValue() == Approx(1.0f));

    REQUIRE(applier.apply(paramTarget(1, 0), -2.0f));
    REQUIRE(proc.getParameters()[0]->getValue() == Approx(0.0f));
}

TEST_CASE("ControlParameterApplier: rejects bad targets and lookups", "[session][control][applier]")
{
    testing::TestParamProcessor proc;
    session::ControlParameterApplier applier;

    // No resolver installed yet.
    REQUIRE_FALSE(applier.apply(paramTarget(1, 0), 0.5f));

    applier.setResolver([&](session::NodeId id) -> juce::AudioProcessor* { return id == 1 ? &proc : nullptr; });

    // Unknown node.
    REQUIRE_FALSE(applier.apply(paramTarget(99, 0), 0.5f));

    // Out-of-range parameter index.
    REQUIRE_FALSE(applier.apply(paramTarget(1, 5), 0.5f));

    // Non-parameter target kind.
    auto patch = paramTarget(1, 0);
    patch.kind = control::MidiTargetKind::PatchSelect;
    REQUIRE_FALSE(applier.apply(patch, 0.5f));

    // The processor parameter was never touched by any of the rejected calls.
    REQUIRE(proc.getParameters()[0]->getValue() == Approx(1.0f));
}
