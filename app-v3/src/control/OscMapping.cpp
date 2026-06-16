#include "OscMapping.h"

#include <cmath>

namespace pedalboard3::control
{

std::optional<juce::OSCAddress> tryParseOscAddress(const juce::String& address)
{
    try
    {
        return juce::OSCAddress(address);
    }
    catch (...) // OSCFormatError for an invalid address
    {
        return std::nullopt;
    }
}

std::optional<float> oscArgumentValue(const juce::OSCMessage& msg, int index) noexcept
{
    if (index < 0 || index >= msg.size())
        return std::nullopt;

    const auto& arg = msg[index];
    if (arg.isFloat32())
        return arg.getFloat32();
    if (arg.isInt32())
        return static_cast<float>(arg.getInt32());
    return std::nullopt;
}

namespace
{

// Read an OSC argument as a MIDI data byte (int32 directly, float32 rounded).
// Returns false if the argument is non-numeric.
bool oscArgAsByte(const juce::OSCArgument& arg, int& out) noexcept
{
    if (arg.isInt32())
    {
        out = arg.getInt32();
        return true;
    }
    if (arg.isFloat32())
    {
        out = static_cast<int>(std::lround(arg.getFloat32()));
        return true;
    }
    return false;
}

} // namespace

std::optional<juce::MidiMessage> midiFromOsc(const juce::OSCMessage& msg)
{
    // Blob form: 3 raw MIDI bytes, or 4 bytes in the OSC 'm' convention
    // (portId, status, data1, data2) — drop the leading port byte.
    if (msg.size() == 1 && msg[0].isBlob())
    {
        const auto& blob = msg[0].getBlob();
        const auto* b = static_cast<const juce::uint8*>(blob.getData());
        const auto n = blob.getSize();
        if (b == nullptr)
            return std::nullopt;
        if (n == 3u)
            return juce::MidiMessage(b[0], b[1], b[2]);
        if (n == 4u)
            return juce::MidiMessage(b[1], b[2], b[3]);
        return std::nullopt;
    }

    // Numeric triplet/pair: 2 or 3 int/float args interpreted as MIDI bytes.
    const int n = msg.size();
    if (n == 2 || n == 3)
    {
        int bytes[3] = {0, 0, 0};
        for (int i = 0; i < n; ++i)
            if (!oscArgAsByte(msg[i], bytes[i]))
                return std::nullopt; // a non-numeric arg means this isn't MIDI

        if (n == 3)
            return juce::MidiMessage(bytes[0], bytes[1], bytes[2]);
        return juce::MidiMessage(bytes[0], bytes[1]);
    }

    return std::nullopt;
}

bool isOscMulticastAddress(const juce::String& ipv4)
{
    auto parts = juce::StringArray::fromTokens(ipv4.trim(), ".", "");
    if (parts.size() != 4)
        return false;

    int octets[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i)
    {
        const auto p = parts[i].trim();
        if (p.isEmpty() || !p.containsOnly("0123456789"))
            return false;
        const int v = p.getIntValue();
        if (v < 0 || v > 255)
            return false;
        octets[i] = v;
    }

    return octets[0] >= 224 && octets[0] <= 239; // 224.0.0.0 .. 239.255.255.255
}

} // namespace pedalboard3::control
