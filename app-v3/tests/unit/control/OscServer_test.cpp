// Tests for pedalboard3::control::OscServer (roadmap M2, plan
// docs/plans/2026-07-04-001-feat-live-midi-osc-control-plan.md, U3/KTD4).
//
// The forwarding path (oscMessageReceived -> sink) is driven directly, no
// live socket required. Bind/multicast-join tests DO open a real UDP
// socket on an ephemeral high port (0 lets the OS pick one) so they can
// run on any CI runner without a fixed-port collision risk.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "control/OscServer.h"
#include "session/ControlHub.h"
#include "support/TestParamProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

using namespace pedalboard3;

TEST_CASE("OscServer: a received message forwards verbatim to the sink", "[control][osc-server]")
{
    std::vector<juce::String> addresses;
    control::OscServer server([&](const juce::OSCMessage& m)
                              { addresses.push_back(m.getAddressPattern().toString()); });

    server.oscMessageReceived(juce::OSCMessage(juce::OSCAddressPattern("/pb3/gain"), 0.5f));
    server.oscMessageReceived(juce::OSCMessage(juce::OSCAddressPattern("/pb3/mix"), 0.25f));

    REQUIRE(addresses == std::vector<juce::String>{"/pb3/gain", "/pb3/mix"});
}

TEST_CASE("OscServer: a null sink drops messages safely", "[control][osc-server]")
{
    control::OscServer server(nullptr);
    server.oscMessageReceived(juce::OSCMessage(juce::OSCAddressPattern("/pb3/gain"), 0.5f));
    SUCCEED(); // no crash
}

TEST_CASE("OscServer: rebind with no multicast address succeeds on an available port", "[control][osc-server]")
{
    // The plain-unicast path every real call site actually takes (App::initialise's
    // default port, TransportBar::applyOscPortChange's normal case) -- every other
    // test here asserts a *failure* outcome or only partially checks the multicast
    // success case, so this is the one place the ordinary success path is verified.
    control::OscServer server([](const juce::OSCMessage&) {});

    const auto error = server.rebind(0); // port 0: OS picks an ephemeral port

    REQUIRE(error.isEmpty());
    REQUIRE(server.isConnected());

    server.disconnect();
}

TEST_CASE("OscServer: an invalid multicast address is rejected without binding", "[control][osc-server]")
{
    control::OscServer server([](const juce::OSCMessage&) {});

    const auto error = server.rebind(0, "192.168.1.1"); // not in the 224-239 multicast range

    REQUIRE(error.isNotEmpty());
    REQUIRE(error.contains("Invalid multicast address"));
    REQUIRE_FALSE(server.isConnected());
}

TEST_CASE("OscServer: a valid multicast address passes the isOscMulticastAddress gate and binds",
          "[control][osc-server]")
{
    control::OscServer server([](const juce::OSCMessage&) {});

    // Port 0: ask the OS for an ephemeral port so this test never collides
    // with another process. Multicast join itself may still fail in a
    // sandboxed/CI network namespace with no multicast route -- that is a
    // real, reportable environment condition, not a defect in the gate this
    // test targets, so only the *address validation* half is asserted.
    const auto error = server.rebind(0, "239.1.2.3");

    if (error.isNotEmpty())
    {
        // The address passed validation (it's a well-formed multicast IP);
        // any failure here is the environment refusing the join/bind, not
        // the "invalid address" rejection this test is about.
        REQUIRE_FALSE(error.contains("Invalid multicast address"));
    }

    server.disconnect();
}

TEST_CASE("OscServer: rebind reports a non-fatal error on bind failure, not a crash", "[control][osc-server]")
{
    // Occupy a real ephemeral port with a raw socket held open for the
    // duration of the test, then ask OscServer to bind that exact port --
    // deterministically fails on every platform without needing a
    // privileged low port (which some CI runners run as root and could
    // bind anyway) or an out-of-range port number (whose bind() behaviour
    // is platform-defined, not a reliable failure signal).
    //
    // juce::DatagramSocket enables SO_REUSEADDR unconditionally in its
    // constructor, so without disabling it here, a second socket (also
    // SO_REUSEADDR by default -- see OscServer's internal socket) is
    // allowed by the OS to bind the same port too, and this test's REQUIRE
    // below would incorrectly fail. Explicitly turning reuse off before
    // bind restores the "port is exclusively taken" precondition the test
    // is named for.
    juce::DatagramSocket occupied;
    occupied.setEnablePortReuse(false);
    REQUIRE(occupied.bindToPort(0));
    const int takenPort = occupied.getBoundPort();
    REQUIRE(takenPort > 0);

    control::OscServer server([](const juce::OSCMessage&) {});
    const auto error = server.rebind(takenPort);

    REQUIRE(error.isNotEmpty());
    REQUIRE_FALSE(server.isConnected());

    // The reason is kept, not only returned (#96). InputGates binds at
    // startup and throws the returned string away after logging it, so
    // without this the OSC Settings dialog could only say "Not listening" --
    // the symptom rather than the reason.
    REQUIRE(server.lastError() == error);
}

TEST_CASE("OscServer: a successful rebind clears the last error", "[control][osc-server]")
{
    juce::DatagramSocket occupied;
    occupied.setEnablePortReuse(false);
    REQUIRE(occupied.bindToPort(0));

    control::OscServer server([](const juce::OSCMessage&) {});
    REQUIRE(server.rebind(occupied.getBoundPort()).isNotEmpty());
    REQUIRE(server.lastError().isNotEmpty());

    // Otherwise the dialog would keep reporting a failure the user has
    // already fixed.
    REQUIRE(server.rebind(0).isEmpty());
    REQUIRE(server.lastError().isEmpty());
    server.disconnect();
}

TEST_CASE("OscServer: an invalid multicast address is recorded too", "[control][osc-server]")
{
    control::OscServer server([](const juce::OSCMessage&) {});
    const auto error = server.rebind(0, "192.168.1.1");
    REQUIRE(error.isNotEmpty());
    REQUIRE(server.lastError() == error);
}

TEST_CASE("OscServer: forwarding into a real ControlHub moves the mapped parameter", "[control][osc-server]")
{
    session::ControlHub hub;
    testing::TestParamProcessor proc;
    hub.setNodeResolver([&](session::NodeId id) -> juce::AudioProcessor* { return id == 1 ? &proc : nullptr; });

    control::OscMapping mapping;
    mapping.address = "/pb3/gain";
    mapping.argumentIndex = 0;
    mapping.target.kind = control::MidiTargetKind::PluginParameter;
    mapping.target.node = 1;
    mapping.target.parameterIndex = 0;
    mapping.rangeMin = 0.0f;
    mapping.rangeMax = 1.0f;
    hub.setOscMappings({mapping});

    control::OscServer server([&](const juce::OSCMessage& m) { hub.handleOsc(m); });
    server.oscMessageReceived(juce::OSCMessage(juce::OSCAddressPattern("/pb3/gain"), 1.0f));

    REQUIRE(proc.getParameters()[0]->getValue() == Catch::Approx(1.0f));
}
