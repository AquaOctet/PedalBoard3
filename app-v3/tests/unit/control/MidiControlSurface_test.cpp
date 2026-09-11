// Tests for the Phase 4 MidiControlSurface — the app-facing seam that routes
// incoming MIDI to a parameter sink and runs the MIDI-learn capture flow.
// Headless: the sink is a recording lambda, no engine or device.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "control/MidiControlSurface.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

using namespace pedalboard3::control;
using Catch::Approx;

namespace
{

struct Applied
{
    MidiTarget target;
    float value{0.0f};
};

MidiMapping ccToParam(int channel, int cc, pedalboard3::session::NodeId node, int paramIndex)
{
    MidiMapping m;
    m.source.kind = MidiSourceKind::ControlChange;
    m.source.channel = channel;
    m.source.controller = cc;
    m.target.node = node;
    m.target.parameterIndex = paramIndex;
    return m;
}

} // namespace

TEST_CASE("MidiControlSurface: routes a mapped CC to the parameter sink", "[control][surface]")
{
    MidiControlSurface s;
    std::vector<Applied> applied;
    s.setParameterSink([&](const ParameterUpdate& u) { applied.push_back({u.target, u.value}); });
    s.setMappings({ccToParam(1, 7, 5, 2)});

    s.handleIncomingMidi(juce::MidiMessage::controllerEvent(1, 7, 127));
    REQUIRE(applied.size() == 1);
    REQUIRE(applied[0].target.node == 5);
    REQUIRE(applied[0].target.parameterIndex == 2);
    REQUIRE(applied[0].value == Approx(1.0f));

    // An unmapped CC produces nothing.
    s.handleIncomingMidi(juce::MidiMessage::controllerEvent(1, 9, 64));
    REQUIRE(applied.size() == 1);
}

TEST_CASE("MidiControlSurface: a null sink drops updates without crashing", "[control][surface]")
{
    MidiControlSurface s;
    s.setMappings({ccToParam(1, 7, 5, 0)});
    // No sink installed.
    REQUIRE_NOTHROW(s.handleIncomingMidi(juce::MidiMessage::controllerEvent(1, 7, 100)));
}

TEST_CASE("MidiControlSurface: learn captures the next message and suppresses routing", "[control][surface]")
{
    MidiControlSurface s;
    std::vector<Applied> applied;
    s.setParameterSink([&](const ParameterUpdate& u) { applied.push_back({u.target, u.value}); });
    s.setMappings({ccToParam(1, 7, 5, 0)}); // a pre-existing mapping

    MidiSource learned;
    int learnCount = 0;
    s.armLearn(
        [&](const MidiSource& src)
        {
            learned = src;
            ++learnCount;
        });
    REQUIRE(s.isLearning());

    // The very message that would normally route (CC7) is instead captured and
    // NOT routed.
    s.handleIncomingMidi(juce::MidiMessage::controllerEvent(1, 7, 100));
    REQUIRE(learnCount == 1);
    REQUIRE_FALSE(s.isLearning()); // auto-disarmed
    REQUIRE(applied.empty());      // suppressed
    REQUIRE(learned.kind == MidiSourceKind::ControlChange);
    REQUIRE(learned.channel == 1);
    REQUIRE(learned.controller == 7);

    // After learn completes, routing resumes normally.
    s.handleIncomingMidi(juce::MidiMessage::controllerEvent(1, 7, 0));
    REQUIRE(applied.size() == 1);
}

TEST_CASE("MidiControlSurface: learn ignores non-learnable messages until a real control", "[control][surface]")
{
    MidiControlSurface s;
    MidiSource learned;
    int learnCount = 0;
    s.armLearn(
        [&](const MidiSource& src)
        {
            learned = src;
            ++learnCount;
        });

    // Note-off is not learnable: still armed, nothing captured.
    s.handleIncomingMidi(juce::MidiMessage::noteOff(3, 64));
    REQUIRE(s.isLearning());
    REQUIRE(learnCount == 0);

    // A note-on captures (the press, on its channel).
    s.handleIncomingMidi(juce::MidiMessage::noteOn(3, 48, (juce::uint8)100));
    REQUIRE(learnCount == 1);
    REQUIRE(learned.kind == MidiSourceKind::Note);
    REQUIRE(learned.channel == 3);
    REQUIRE(learned.controller == 48);
}

TEST_CASE("MidiControlSurface: cancelLearn leaves no stale capture", "[control][surface]")
{
    MidiControlSurface s;
    std::vector<Applied> applied;
    s.setParameterSink([&](const ParameterUpdate& u) { applied.push_back({u.target, u.value}); });
    s.setMappings({ccToParam(1, 7, 5, 0)});

    int learnCount = 0;
    s.armLearn([&](const MidiSource&) { ++learnCount; });
    REQUIRE(s.isLearning());

    s.cancelLearn();
    REQUIRE_FALSE(s.isLearning());

    // The next CC neither completes the cancelled learn nor fires a stale
    // mapping from it — it just routes through the existing mapping (AC-IO-4).
    s.handleIncomingMidi(juce::MidiMessage::controllerEvent(1, 7, 127));
    REQUIRE(learnCount == 0);
    REQUIRE(applied.size() == 1);
    REQUIRE(applied[0].value == Approx(1.0f));
}

TEST_CASE("MidiControlSurface: pitch-bend and program-change are learnable", "[control][surface]")
{
    MidiControlSurface s;

    MidiSource learned;
    s.armLearn([&](const MidiSource& src) { learned = src; });
    s.handleIncomingMidi(juce::MidiMessage::pitchWheel(5, 8192));
    REQUIRE(learned.kind == MidiSourceKind::PitchBend);
    REQUIRE(learned.channel == 5);

    s.armLearn([&](const MidiSource& src) { learned = src; });
    s.handleIncomingMidi(juce::MidiMessage::programChange(9, 3));
    REQUIRE(learned.kind == MidiSourceKind::ProgramChange);
    REQUIRE(learned.channel == 9);
}
