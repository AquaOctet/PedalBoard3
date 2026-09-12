// Tests for session::mmc (session/Mmc.h): which MIDI Machine Control commands
// drive the transport (M6 U3, plan KD5).

#include <catch2/catch_test_macros.hpp>

#include "session/Mmc.h"

using namespace pedalboard3::session;
using juce::MidiMessage;

TEST_CASE("Mmc: Play, Deferred Play, Stop, Pause and Rewind map to transport commands", "[session][mmc]")
{
    REQUIRE(mmc::commandOf(MidiMessage::midiMachineControlCommand(MidiMessage::mmc_play)) == mmc::Command::Play);
    REQUIRE(mmc::commandOf(MidiMessage::midiMachineControlCommand(MidiMessage::mmc_deferredplay)) ==
            mmc::Command::Play);
    REQUIRE(mmc::commandOf(MidiMessage::midiMachineControlCommand(MidiMessage::mmc_stop)) == mmc::Command::Stop);
    REQUIRE(mmc::commandOf(MidiMessage::midiMachineControlCommand(MidiMessage::mmc_pause)) == mmc::Command::Pause);
    REQUIRE(mmc::commandOf(MidiMessage::midiMachineControlCommand(MidiMessage::mmc_rewind)) ==
            mmc::Command::ReturnToZero);
}

TEST_CASE("Mmc: other MMC commands and other messages are ignored", "[session][mmc]")
{
    REQUIRE(mmc::commandOf(MidiMessage::midiMachineControlCommand(MidiMessage::mmc_fastforward)) == mmc::Command::None);
    REQUIRE(mmc::commandOf(MidiMessage::midiMachineControlCommand(MidiMessage::mmc_recordStart)) == mmc::Command::None);
    REQUIRE(mmc::commandOf(MidiMessage::noteOn(1, 60, 1.0f)) == mmc::Command::None);
    REQUIRE(mmc::commandOf(MidiMessage::controllerEvent(1, 7, 100)) == mmc::Command::None);

    const juce::uint8 identityRequest[] = {0x7E, 0x7F, 0x06, 0x01}; // a SysEx that isn't MMC
    REQUIRE(mmc::commandOf(MidiMessage::createSysExMessage(identityRequest, 4)) == mmc::Command::None);
}
