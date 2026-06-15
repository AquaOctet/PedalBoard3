// Tests for the Phase 4 MIDI mapping model + pure value transform
// (control/MidiMapping). All headless — no device or audio engine.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "control/MidiMapping.h"

#include <juce_audio_basics/juce_audio_basics.h>

using namespace pedalboard3::control;
using Catch::Approx;

TEST_CASE("MidiMapping matches: CC kind/channel/controller", "[control][midi]")
{
    MidiSource src;
    src.kind = MidiSourceKind::ControlChange;
    src.channel = 1;
    src.controller = 7;

    REQUIRE(matches(src, juce::MidiMessage::controllerEvent(1, 7, 100)));
    // Wrong controller number.
    REQUIRE_FALSE(matches(src, juce::MidiMessage::controllerEvent(1, 8, 100)));
    // Wrong channel.
    REQUIRE_FALSE(matches(src, juce::MidiMessage::controllerEvent(2, 7, 100)));
    // Wrong kind.
    REQUIRE_FALSE(matches(src, juce::MidiMessage::noteOn(1, 7, (juce::uint8)100)));
}

TEST_CASE("MidiMapping matches: omni channel accepts any channel", "[control][midi]")
{
    MidiSource src;
    src.kind = MidiSourceKind::ControlChange;
    src.channel = 0; // omni
    src.controller = 7;

    REQUIRE(matches(src, juce::MidiMessage::controllerEvent(1, 7, 0)));
    REQUIRE(matches(src, juce::MidiMessage::controllerEvent(16, 7, 0)));
}

TEST_CASE("MidiMapping matches: Note accepts note-on and note-off", "[control][midi]")
{
    MidiSource src;
    src.kind = MidiSourceKind::Note;
    src.channel = 1;
    src.controller = 60;

    REQUIRE(matches(src, juce::MidiMessage::noteOn(1, 60, (juce::uint8)90)));
    REQUIRE(matches(src, juce::MidiMessage::noteOff(1, 60)));
    REQUIRE_FALSE(matches(src, juce::MidiMessage::noteOn(1, 61, (juce::uint8)90)));
}

TEST_CASE("MidiMapping matches: ProgramChange and PitchBend ignore controller", "[control][midi]")
{
    MidiSource pc;
    pc.kind = MidiSourceKind::ProgramChange;
    pc.channel = 0;
    REQUIRE(matches(pc, juce::MidiMessage::programChange(3, 12)));
    REQUIRE_FALSE(matches(pc, juce::MidiMessage::controllerEvent(3, 0, 0)));

    MidiSource pb;
    pb.kind = MidiSourceKind::PitchBend;
    pb.channel = 2;
    REQUIRE(matches(pb, juce::MidiMessage::pitchWheel(2, 8192)));
    REQUIRE_FALSE(matches(pb, juce::MidiMessage::pitchWheel(3, 8192)));
}

TEST_CASE("MidiMapping rawNormalized: per-kind value extraction", "[control][midi]")
{
    REQUIRE(*rawNormalized(MidiSourceKind::ControlChange, juce::MidiMessage::controllerEvent(1, 7, 127)) ==
            Approx(1.0f));
    REQUIRE(*rawNormalized(MidiSourceKind::ControlChange, juce::MidiMessage::controllerEvent(1, 7, 0)) == Approx(0.0f));

    REQUIRE(*rawNormalized(MidiSourceKind::Note, juce::MidiMessage::noteOn(1, 60, (juce::uint8)127)) == Approx(1.0f));
    // Note-off reads as 0 (released).
    REQUIRE(*rawNormalized(MidiSourceKind::Note, juce::MidiMessage::noteOff(1, 60)) == Approx(0.0f));

    REQUIRE(*rawNormalized(MidiSourceKind::ProgramChange, juce::MidiMessage::programChange(1, 127)) == Approx(1.0f));

    REQUIRE(*rawNormalized(MidiSourceKind::PitchBend, juce::MidiMessage::pitchWheel(1, 16383)) == Approx(1.0f));
    REQUIRE(*rawNormalized(MidiSourceKind::PitchBend, juce::MidiMessage::pitchWheel(1, 0)) == Approx(0.0f));

    // Mismatched message kind -> nullopt.
    REQUIRE_FALSE(
        rawNormalized(MidiSourceKind::ControlChange, juce::MidiMessage::noteOn(1, 60, (juce::uint8)1)).has_value());
}

TEST_CASE("MidiMapping applyRange: identity, inverted, custom and reversed range", "[control][midi]")
{
    MidiMapping m;

    SECTION("default 0..1 is identity")
    {
        REQUIRE(applyRange(m, 0.0f) == Approx(0.0f));
        REQUIRE(applyRange(m, 0.5f) == Approx(0.5f));
        REQUIRE(applyRange(m, 1.0f) == Approx(1.0f));
    }

    SECTION("inverted flips the source")
    {
        m.inverted = true;
        REQUIRE(applyRange(m, 0.0f) == Approx(1.0f));
        REQUIRE(applyRange(m, 1.0f) == Approx(0.0f));
    }

    SECTION("custom range maps linearly")
    {
        m.rangeMin = 0.2f;
        m.rangeMax = 0.8f;
        REQUIRE(applyRange(m, 0.0f) == Approx(0.2f));
        REQUIRE(applyRange(m, 0.5f) == Approx(0.5f));
        REQUIRE(applyRange(m, 1.0f) == Approx(0.8f));
    }

    SECTION("reversed range (min>max) also inverts")
    {
        m.rangeMin = 1.0f;
        m.rangeMax = 0.0f;
        REQUIRE(applyRange(m, 0.0f) == Approx(1.0f));
        REQUIRE(applyRange(m, 1.0f) == Approx(0.0f));
    }

    SECTION("input is clamped to 0..1")
    {
        REQUIRE(applyRange(m, -0.5f) == Approx(0.0f));
        REQUIRE(applyRange(m, 2.0f) == Approx(1.0f));
    }
}
