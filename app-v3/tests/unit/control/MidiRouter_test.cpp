// Tests for the Phase 4 MidiRouter — MIDI message -> parameter-update intents,
// including latch (momentary->toggle) state handling. Headless.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "control/MidiRouter.h"

#include <juce_audio_basics/juce_audio_basics.h>

using namespace pedalboard3::control;
using Catch::Approx;

namespace
{

MidiMapping ccToParam(int channel, int cc, pedalboard3::session::NodeId node, int paramIndex)
{
    MidiMapping m;
    m.source.kind = MidiSourceKind::ControlChange;
    m.source.channel = channel;
    m.source.controller = cc;
    m.target.kind = MidiTargetKind::PluginParameter;
    m.target.node = node;
    m.target.parameterIndex = paramIndex;
    return m;
}

} // namespace

TEST_CASE("MidiRouter: continuous CC drives a parameter value", "[control][router]")
{
    MidiRouter r;
    r.setMappings({ccToParam(1, 7, 5, 2)});

    auto out = r.process(juce::MidiMessage::controllerEvent(1, 7, 127));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].target.node == 5);
    REQUIRE(out[0].target.parameterIndex == 2);
    REQUIRE(out[0].value == Approx(1.0f));

    out = r.process(juce::MidiMessage::controllerEvent(1, 7, 0));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].value == Approx(0.0f));
}

TEST_CASE("MidiRouter: channel filter drops non-matching messages", "[control][router]")
{
    MidiRouter r;
    r.setMappings({ccToParam(1, 7, 5, 0)});

    REQUIRE(r.process(juce::MidiMessage::controllerEvent(2, 7, 100)).empty());
    REQUIRE(r.process(juce::MidiMessage::controllerEvent(1, 8, 100)).empty());
}

TEST_CASE("MidiRouter: one CC fans out to several mappings", "[control][router]")
{
    MidiRouter r;
    r.setMappings({ccToParam(1, 7, 5, 0), ccToParam(1, 7, 6, 1)});

    auto out = r.process(juce::MidiMessage::controllerEvent(1, 7, 64));
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].target.node == 5);
    REQUIRE(out[1].target.node == 6);
}

TEST_CASE("MidiRouter: latch CC toggles on the press edge only", "[control][router]")
{
    MidiRouter r;
    auto m = ccToParam(1, 64, 9, 0); // sustain-pedal-style CC
    m.latch = true;
    r.setMappings({m});

    // First press (>=64) toggles ON -> rangeMax (1.0).
    auto out = r.process(juce::MidiMessage::controllerEvent(1, 64, 127));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].value == Approx(1.0f));

    // A second "held" press without an intervening release does NOT re-toggle.
    REQUIRE(r.process(juce::MidiMessage::controllerEvent(1, 64, 127)).empty());

    // Release (<64) produces no output, just arms the next press.
    REQUIRE(r.process(juce::MidiMessage::controllerEvent(1, 64, 0)).empty());

    // Next press toggles back OFF -> rangeMin (0.0).
    out = r.process(juce::MidiMessage::controllerEvent(1, 64, 127));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].value == Approx(0.0f));
}

TEST_CASE("MidiRouter: latch honours inverted range", "[control][router]")
{
    MidiRouter r;
    auto m = ccToParam(1, 64, 9, 0);
    m.latch = true;
    m.inverted = true; // ON should now yield rangeMin side
    r.setMappings({m});

    auto out = r.process(juce::MidiMessage::controllerEvent(1, 64, 127));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].value == Approx(0.0f)); // inverted ON
}

TEST_CASE("MidiRouter: Note latch toggles per note-on, ignores note-off", "[control][router]")
{
    MidiRouter r;
    MidiMapping m;
    m.source.kind = MidiSourceKind::Note;
    m.source.channel = 1;
    m.source.controller = 60;
    m.target.node = 3;
    m.latch = true;
    r.setMappings({m});

    auto out = r.process(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].value == Approx(1.0f));

    // Note-off arms but emits nothing.
    REQUIRE(r.process(juce::MidiMessage::noteOff(1, 60)).empty());

    out = r.process(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].value == Approx(0.0f));
}

TEST_CASE("MidiRouter: reset and setMappings clear latch state", "[control][router]")
{
    MidiRouter r;
    auto m = ccToParam(1, 64, 9, 0);
    m.latch = true;
    r.setMappings({m});

    // Toggle ON.
    REQUIRE(r.process(juce::MidiMessage::controllerEvent(1, 64, 127))[0].value == Approx(1.0f));

    // reset() returns the toggle to OFF and disarms the button, so the next
    // press toggles ON again (not OFF).
    r.reset();
    auto out = r.process(juce::MidiMessage::controllerEvent(1, 64, 127));
    REQUIRE(out[0].value == Approx(1.0f));

    // setMappings likewise resets latch state.
    r.setMappings({m});
    out = r.process(juce::MidiMessage::controllerEvent(1, 64, 127));
    REQUIRE(out[0].value == Approx(1.0f));
}

TEST_CASE("MidiRouter: press follows the button edge for CC and Note; every Program Change is a press",
          "[control][router]")
{
    MidiRouter r;
    MidiMapping note;
    note.source.kind = MidiSourceKind::Note;
    note.source.channel = 1;
    note.source.controller = 60;
    MidiMapping pc;
    pc.source.kind = MidiSourceKind::ProgramChange;
    pc.source.channel = 0;
    r.setMappings({ccToParam(1, 20, 1, 0), note, pc});

    REQUIRE(r.process(juce::MidiMessage::controllerEvent(1, 20, 127))[0].press);
    REQUIRE(r.process(juce::MidiMessage::controllerEvent(1, 20, 64))[0].press);
    REQUIRE_FALSE(r.process(juce::MidiMessage::controllerEvent(1, 20, 63))[0].press);
    REQUIRE_FALSE(r.process(juce::MidiMessage::controllerEvent(1, 20, 0))[0].press);

    REQUIRE(r.process(juce::MidiMessage::noteOn(1, 60, 0.8f))[0].press);
    const auto off = r.process(juce::MidiMessage::noteOff(1, 60));
    REQUIRE(off.size() == 1);
    REQUIRE_FALSE(off[0].press);

    const auto pc0 = r.process(juce::MidiMessage::programChange(1, 0));
    REQUIRE(pc0.size() == 1);
    REQUIRE(pc0[0].press); // a Program Change 0 is a press even though its value is 0
    REQUIRE(pc0[0].value == Approx(0.0f));
}

TEST_CASE("MidiRouter: a latch toggle is always a press; the release emits nothing", "[control][router]")
{
    MidiRouter r;
    auto m = ccToParam(1, 64, 1, 0);
    m.latch = true;
    r.setMappings({m});

    const auto on = r.process(juce::MidiMessage::controllerEvent(1, 64, 127));
    REQUIRE(on.size() == 1);
    REQUIRE(on[0].press);
    REQUIRE(r.process(juce::MidiMessage::controllerEvent(1, 64, 0)).empty());

    const auto offToggle = r.process(juce::MidiMessage::controllerEvent(1, 64, 127));
    REQUIRE(offToggle.size() == 1);
    REQUIRE(offToggle[0].press);
    REQUIRE(offToggle[0].value == Approx(0.0f));
}
