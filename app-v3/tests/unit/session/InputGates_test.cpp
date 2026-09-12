// Tests for session::InputGates (optional-features plan U2, KD6): MIDI input
// and the OSC listener run only while their features are available, and a
// failed OSC bind is reported without stopping anything else.

#include <catch2/catch_test_macros.hpp>

#include "control/MidiInputManager.h"
#include "control/OscServer.h"
#include "session/InputGates.h"

#include <array>

using namespace pedalboard3;
using session::Feature;

namespace
{

struct Rig
{
    juce::ChangeBroadcaster features; // FeatureGate in the app
    std::array<bool, session::kFeatureCount> available{};
    control::MidiInputManager midi{nullptr, [](const juce::MidiMessage&) {}};
    control::OscServer osc{[](const juce::OSCMessage&) {}};
    juce::String group;
    session::InputGates gates{features,
                              [this](Feature f) { return available[session::indexOf(f)]; },
                              midi,
                              osc,
                              [this] { return std::make_pair(0, group); }}; // port 0: the OS picks one

    void set(Feature feature, bool on)
    {
        available[session::indexOf(feature)] = on;
        features.sendSynchronousChangeMessage();
    }

    ~Rig() { osc.disconnect(); }
};

} // namespace

TEST_CASE("InputGates: MIDI input runs only while MIDI is available", "[session][features][midi-input]")
{
    Rig rig;
    rig.gates.apply();
    REQUIRE_FALSE(rig.midi.isEnabled());

    rig.set(Feature::Midi, true);
    REQUIRE(rig.midi.isEnabled());

    rig.set(Feature::Midi, false);
    REQUIRE_FALSE(rig.midi.isEnabled());
}

TEST_CASE("InputGates: the OSC listener binds only while OSC is available, and closes its port when it's not",
          "[session][features][osc-server]")
{
    Rig rig;
    rig.gates.apply();
    REQUIRE_FALSE(rig.osc.isConnected());

    rig.set(Feature::Osc, true);
    REQUIRE(rig.osc.isConnected());
    REQUIRE(rig.gates.lastOscError().isEmpty());

    rig.set(Feature::Osc, false);
    REQUIRE_FALSE(rig.osc.isConnected());
}

TEST_CASE("InputGates: a failed OSC bind is reported, and MIDI still follows its own switch",
          "[session][features][osc-server]")
{
    Rig rig;
    rig.group = "192.168.1.1"; // not a multicast group: the bind is refused
    rig.set(Feature::Osc, true);
    REQUIRE_FALSE(rig.osc.isConnected());
    REQUIRE(rig.gates.lastOscError().isNotEmpty());

    rig.set(Feature::Midi, true);
    REQUIRE(rig.midi.isEnabled());
}
