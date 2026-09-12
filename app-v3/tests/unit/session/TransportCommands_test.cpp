// Tests for session::TransportCommands (session/TransportCommands.h): the
// transport's app commands, tap tempo into the patch, and MMC (M6 U3).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "engine/Transport.h"
#include "session/AppCommands.h"
#include "session/BankDocument.h"
#include "session/Session.h"
#include "session/TransportCommands.h"

using namespace pedalboard3;
using Catch::Approx;

namespace
{

struct Rig
{
    engine::AudioEngine eng;
    engine::PluginHost host;
    session::Session s{eng, host};
    session::TransportCommands commands{s, eng.getTransport()};

    Rig() { eng.initialise(); }
};

} // namespace

TEST_CASE("TransportCommands: Play / Pause, Stop and Return to Zero drive the transport", "[session][transport]")
{
    Rig rig;
    auto& transport = rig.eng.getTransport();

    rig.commands.playPause();
    REQUIRE(transport.isPlaying());
    rig.commands.playPause();
    REQUIRE_FALSE(transport.isPlaying());

    rig.commands.playPause();
    rig.commands.returnToZero();
    REQUIRE(transport.isPlaying()); // Return to Zero keeps playing
    rig.commands.stop();
    REQUIRE_FALSE(transport.isPlaying());
}

TEST_CASE("TransportCommands: a tapped tempo goes into the patch and the transport", "[session][transport]")
{
    Rig rig;
    rig.commands.tapAt(10000.0);
    rig.commands.tapAt(10500.0);
    REQUIRE(rig.s.getActivePatch().tempo == Approx(120.0));
    REQUIRE(rig.eng.getTransport().getTempo() == Approx(120.0));

    rig.commands.tapAt(11250.0); // 10000, 10500, 11250: 625 ms on average
    REQUIRE(rig.s.getActivePatch().tempo == Approx(96.0));
}

TEST_CASE("TransportCommands: MMC drives the transport only while it's switched on", "[session][transport][mmc]")
{
    Rig rig;
    auto& transport = rig.eng.getTransport();
    const auto play = juce::MidiMessage::midiMachineControlCommand(juce::MidiMessage::mmc_play);
    const auto stop = juce::MidiMessage::midiMachineControlCommand(juce::MidiMessage::mmc_stop);

    REQUIRE_FALSE(rig.commands.handleMmc(play));
    REQUIRE_FALSE(transport.isPlaying());

    rig.commands.setMmcEnabled(true);
    REQUIRE(rig.commands.handleMmc(play));
    REQUIRE(transport.isPlaying());
    REQUIRE(rig.commands.handleMmc(stop)); // Stop stops: it doesn't toggle
    REQUIRE_FALSE(transport.isPlaying());
    REQUIRE(rig.commands.handleMmc(stop));
    REQUIRE_FALSE(transport.isPlaying());
    REQUIRE_FALSE(rig.commands.handleMmc(juce::MidiMessage::noteOn(1, 60, 1.0f)));
}

TEST_CASE("TransportCommands: the transport app commands run through invokeAppCommand", "[session][transport]")
{
    Rig rig;
    session::BankDocument doc(rig.s);
    auto& transport = rig.eng.getTransport();

    REQUIRE(session::invokeAppCommand(doc, session::appcommands::kPlayPause, nullptr, &rig.commands));
    REQUIRE(transport.isPlaying());
    REQUIRE(session::invokeAppCommand(doc, session::appcommands::kReturnToZero, nullptr, &rig.commands));
    REQUIRE(session::invokeAppCommand(doc, session::appcommands::kStop, nullptr, &rig.commands));
    REQUIRE_FALSE(transport.isPlaying());
    REQUIRE(session::invokeAppCommand(doc, session::appcommands::kTapTempo, nullptr, &rig.commands));

    // Without a transport target the commands are unhandled, like undo without a history.
    REQUIRE_FALSE(session::invokeAppCommand(doc, session::appcommands::kPlayPause));
}

TEST_CASE("TransportCommands: with Tempo / BPM off, the commands, taps and MMC do nothing, and switching off pauses",
          "[session][transport][features]")
{
    Rig rig;
    auto& transport = rig.eng.getTransport();
    rig.commands.playPause();
    REQUIRE(transport.isPlaying());

    rig.commands.setEnabled(false);
    REQUIRE_FALSE(transport.isPlaying());
    rig.commands.playPause();
    rig.commands.returnToZero();
    REQUIRE_FALSE(transport.isPlaying());
    rig.commands.tapAt(10000.0);
    rig.commands.tapAt(10625.0); // would be 96 BPM
    REQUIRE(rig.s.getActivePatch().tempo == Approx(120.0));
    rig.commands.setMmcEnabled(true);
    REQUIRE_FALSE(rig.commands.handleMmc(juce::MidiMessage::midiMachineControlCommand(juce::MidiMessage::mmc_play)));
    REQUIRE_FALSE(transport.isPlaying());

    rig.commands.setEnabled(true);
    rig.commands.playPause();
    REQUIRE(transport.isPlaying());
}
