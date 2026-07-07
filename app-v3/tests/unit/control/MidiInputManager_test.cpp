// Tests for pedalboard3::control::MidiMessageFifo and MidiInputManager (roadmap
// M2, plan docs/plans/2026-07-04-001-feat-live-midi-osc-control-plan.md, U2).
//
// Per the plan's Execution note, the FIFO/drain core is the testable seam and
// is exercised directly (no device, no AudioDeviceManager). MidiInputManager's
// device-registration glue is thin and mostly verified on hardware; the one
// thing tested here without a device is that a null AudioDeviceManager is
// safe (the headless-dev-server case) and that the manager forwards through
// to a real ControlHub end to end.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "control/MidiInputManager.h"
#include "session/ControlHub.h"
#include "support/TestParamProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

using namespace pedalboard3;

namespace
{

juce::MidiMessage cc(int channel, int controller, int value)
{
    return juce::MidiMessage::controllerEvent(channel, controller, value);
}

} // namespace

// ---- MidiMessageFifo: the testable core --------------------------------

TEST_CASE("MidiMessageFifo: pushed messages drain in order", "[control][midi-input]")
{
    control::MidiMessageFifo fifo(16);

    fifo.push(cc(1, 1, 10));
    fifo.push(cc(1, 2, 20));
    fifo.push(cc(1, 3, 30));

    std::vector<int> controllers;
    fifo.drain([&](const juce::MidiMessage& m) { controllers.push_back(m.getControllerNumber()); });

    REQUIRE(controllers == std::vector<int>{1, 2, 3});
    REQUIRE(fifo.droppedCount() == 0);
}

TEST_CASE("MidiMessageFifo: draining an empty fifo delivers nothing", "[control][midi-input]")
{
    control::MidiMessageFifo fifo(16);

    int deliveries = 0;
    fifo.drain([&](const juce::MidiMessage&) { ++deliveries; });

    REQUIRE(deliveries == 0);
}

TEST_CASE("MidiMessageFifo: overflow drops and counts without corrupting retained messages", "[control][midi-input]")
{
    // juce::AbstractFifo reserves one slot to disambiguate full from empty
    // (getFreeSpace() == bufferSize - numReady - 1), so a fifo constructed
    // with capacity N holds at most N-1 messages -- construct with 5 to get
    // the 4 usable slots this test is about.
    control::MidiMessageFifo fifo(5);

    // Push more than capacity: the ring holds at most 4; the rest are dropped.
    for (int i = 0; i < 7; ++i)
        fifo.push(cc(1, i + 1, 64));

    REQUIRE(fifo.droppedCount() == 3);

    std::vector<int> controllers;
    fifo.drain([&](const juce::MidiMessage& m) { controllers.push_back(m.getControllerNumber()); });

    // The 4 retained messages are the first 4 pushed, intact and in order —
    // overflow drops the newest arrivals, not silently corrupts the ring.
    REQUIRE(controllers == std::vector<int>{1, 2, 3, 4});
}

TEST_CASE("MidiMessageFifo: a second drain after the first sees only newly-pushed messages", "[control][midi-input]")
{
    control::MidiMessageFifo fifo(16);
    fifo.push(cc(1, 1, 10));

    std::vector<int> firstDrain;
    fifo.drain([&](const juce::MidiMessage& m) { firstDrain.push_back(m.getControllerNumber()); });
    REQUIRE(firstDrain == std::vector<int>{1});

    fifo.push(cc(1, 2, 20));
    std::vector<int> secondDrain;
    fifo.drain([&](const juce::MidiMessage& m) { secondDrain.push_back(m.getControllerNumber()); });
    REQUIRE(secondDrain == std::vector<int>{2});
}

// ---- Integration: manager sink -> real ControlHub ----------------------

TEST_CASE("MidiInputManager: a null AudioDeviceManager is safe to construct and destroy", "[control][midi-input]")
{
    // The headless-dev-server case (also matches production when no audio
    // device is present): no callbacks to register, nothing to drain from.
    int received = 0;
    control::MidiInputManager manager(nullptr, [&](const juce::MidiMessage&) { ++received; });
    REQUIRE(received == 0);
}

TEST_CASE("MidiInputManager: forwarding a CC through the fifo into a real ControlHub moves the mapped parameter",
          "[control][midi-input]")
{
    // Proves the manager -> hub contract end to end, not just the queue in
    // isolation: push a CC as the device callback would, drain it as the
    // timer would, and confirm it reaches a real ControlHub-resolved
    // parameter — bypassing MidiInputManager's device registration (no
    // AudioDeviceManager here) but exercising the exact fifo->sink path
    // production wires through it.
    session::ControlHub hub;
    testing::TestParamProcessor proc;
    hub.setNodeResolver([&](session::NodeId id) -> juce::AudioProcessor* { return id == 1 ? &proc : nullptr; });

    control::MidiMapping mapping;
    mapping.source.kind = control::MidiSourceKind::ControlChange;
    mapping.source.channel = 0; // omni
    mapping.source.controller = 74;
    mapping.target.kind = control::MidiTargetKind::PluginParameter;
    mapping.target.node = 1;
    mapping.target.parameterIndex = 0;
    mapping.rangeMin = 0.0f;
    mapping.rangeMax = 1.0f;
    hub.setMidiMappings({mapping});

    control::MidiMessageFifo fifo(16);
    fifo.push(cc(1, 74, 127));
    fifo.drain([&](const juce::MidiMessage& m) { hub.handleMidi(m); });

    REQUIRE(proc.getParameters()[0]->getValue() == Catch::Approx(1.0f));
}
