#pragma once

#include "Features.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <utility>

namespace pedalboard3::control
{
class MidiInputManager;
class OscServer;
} // namespace pedalboard3::control

namespace pedalboard3::session
{

class TransportCommands;

// Runs MIDI input and the OSC listener only while their features are available
// (plan docs/plans/2026-09-12-002-feat-optional-features-plan.md, KD6):
//   - MIDI off stops all MIDI input: MidiInputManager unregisters every device.
//   - OSC off closes the listener's port. On, it binds to the port and
//     multicast group from Settings. A failed bind is logged and kept for
//     lastOscError(), never fatal.
// It applies again whenever `features` broadcasts (FeatureGate in the app).
//
// Message thread only.
class InputGates : private juce::ChangeListener
{
public:
    using Availability = std::function<bool(Feature)>;
    using OscConfig = std::function<std::pair<int, juce::String>()>; // port, multicast group ("" for none)

    InputGates(juce::ChangeBroadcaster& features,
               Availability available,
               control::MidiInputManager& midi,
               control::OscServer& osc,
               OscConfig oscConfig);
    ~InputGates() override;

    void apply();
    const juce::String& lastOscError() const noexcept { return oscError_; }

    // The transport's commands follow Tempo / BPM (U4): off, they do nothing
    // and the transport pauses.
    void setTransport(TransportCommands* transport) noexcept { transport_ = transport; }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { apply(); }

    juce::ChangeBroadcaster& features_;
    Availability available_;
    control::MidiInputManager& midi_;
    control::OscServer& osc_;
    OscConfig oscConfig_;
    juce::String oscError_;
    TransportCommands* transport_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputGates)
};

} // namespace pedalboard3::session
