#pragma once

#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>

#include <functional>
#include <memory>

namespace pedalboard3::control
{

// Live OSC input (roadmap M2, plan docs/plans/2026-07-04-001-feat-live-midi-
// osc-control-plan.md, U3/KTD4). Wraps a juce::OSCReceiver on a configurable
// port and forwards every received message to the injected sink
// (ControlHub::handleOsc in production).
//
// Threading: juce::OSCReceiver's default Listener<> (MessageLoopCallback)
// dispatches oscMessageReceived on the JUCE message thread, so no manual
// marshalling is needed here — unlike MIDI input, which arrives on a
// dedicated device thread (see MidiInputManager).
//
// Multicast: when a multicast address is supplied, this owns a
// juce::DatagramSocket directly (bind + joinMulticast) and attaches the
// receiver to it via connectToSocket — OSCReceiver::connect(port) alone has
// no multicast-join API. The address is validated with
// control::isOscMulticastAddress before any bind is attempted.
class OscServer : private juce::OSCReceiver::Listener<>
{
public:
    using Sink = std::function<void(const juce::OSCMessage&)>;

    explicit OscServer(Sink sink);
    ~OscServer() override;

    // Disconnects any existing binding, then attempts the new one.
    // `multicastAddress` empty means unicast-only (no group join). Returns an
    // empty string on success, or a human-readable error otherwise — a bind
    // failure (e.g. port already in use) or an invalid multicast address is
    // non-fatal: the server stays disconnected until rebind() is retried
    // with a different value, and the caller (App / a UI port control)
    // decides how to surface the error.
    juce::String rebind(int port, const juce::String& multicastAddress = {});

    void disconnect();

    bool isConnected() const noexcept { return connected_; }

    // Public despite overriding a privately-inherited juce::OSCReceiver
    // listener: this is the whole of the forwarding logic (one line), and
    // tests drive it directly to prove the sink contract without needing a
    // live socket (the socket/bind path is thin, hardware-verified glue).
    void oscMessageReceived(const juce::OSCMessage& message) override;

private:
    Sink sink_;
    juce::OSCReceiver receiver_;
    std::unique_ptr<juce::DatagramSocket> multicastSocket_; // only when a multicast join is active
    bool connected_{false};
};

} // namespace pedalboard3::control
