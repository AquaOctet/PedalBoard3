#include "OscServer.h"

#include "OscMapping.h" // control::isOscMulticastAddress

#include <utility>

namespace pedalboard3::control
{

OscServer::OscServer(Sink sink) : sink_(std::move(sink))
{
    receiver_.addListener(this);
}

OscServer::~OscServer()
{
    receiver_.removeListener(this);
}

juce::String OscServer::rebind(int port, const juce::String& multicastAddress)
{
    disconnect();

    if (multicastAddress.isNotEmpty())
    {
        if (!isOscMulticastAddress(multicastAddress))
            return "Invalid multicast address: " + multicastAddress;

        auto socket = std::make_unique<juce::DatagramSocket>();
        if (!socket->bindToPort(port))
            return "Failed to bind UDP port " + juce::String(port);

        if (!socket->joinMulticast(multicastAddress))
            return "Failed to join multicast group " + multicastAddress;

        if (!receiver_.connectToSocket(*socket))
            return "OSCReceiver failed to attach to the bound socket";

        multicastSocket_ = std::move(socket); // receiver_ holds a reference; keep it alive
        connected_ = true;
        return {};
    }

    if (!receiver_.connect(port))
        return "Failed to bind UDP port " + juce::String(port);

    connected_ = true;
    return {};
}

void OscServer::disconnect()
{
    receiver_.disconnect();
    multicastSocket_.reset();
    connected_ = false;
}

void OscServer::oscMessageReceived(const juce::OSCMessage& message)
{
    if (sink_)
        sink_(message);
}

} // namespace pedalboard3::control
