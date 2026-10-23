#pragma once

#include "MidiMapping.h" // MidiTarget + scaleToRange (shared with MIDI)

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_osc/juce_osc.h>

#include <optional>

// Phase 4 OSC control surface (master plan §4.2.D). The headless, pure data +
// transform half, mirroring control/MidiMapping. No socket / OscServer here —
// binding a UDP port is the device layer's job; these types only describe a
// mapping and turn a parsed juce::OSCMessage into a value. Keeping it pure makes
// the address-matching, value-scaling, MIDI-over-OSC, and multicast rules
// unit-testable from the Linux dev box.
namespace pedalboard3::control
{

// One OSC address -> target binding. `address` is a concrete OSC address (no
// wildcards), e.g. "/pb3/track1/gain"; an incoming message whose address
// pattern matches it drives `target`. `argumentIndex` selects which OSC
// argument supplies the value (OSC senders typically send a normalized 0..1
// float). Range/invert shape it exactly like a MIDI mapping.
struct OscMapping
{
    juce::String address;
    int argumentIndex{0};
    MidiTarget target;
    float rangeMin{0.0f};
    float rangeMax{1.0f};
    bool inverted{false};

    bool operator==(const OscMapping&) const noexcept = default;
};

// Parse `address` into a juce::OSCAddress, returning nullopt instead of throwing
// when it is not a valid OSC address (OSCAddress's constructor throws
// OSCFormatError). Used to validate/cache mapping addresses up front so the hot
// path never constructs (or throws) per message.
std::optional<juce::OSCAddress> tryParseOscAddress(const juce::String& address);

// Read argument `index` of `msg` as a float: float32 directly, int32 promoted.
// Returns nullopt when the index is out of range or the argument is neither
// numeric type.
std::optional<float> oscArgumentValue(const juce::OSCMessage& msg, int index) noexcept;

// MIDI-over-OSC (AC-IO-6). JUCE's OSC layer has no native MIDI ('m') argument
// type, so a MIDI message arrives either as:
//   - a numeric triplet/pair: 2 or 3 int32/float32 args = MIDI bytes, or
//   - a single blob: 3 raw MIDI bytes, or 4 bytes in the OSC 'm' convention
//     (portId, status, data1, data2) where the leading port byte is dropped.
// Returns nullopt for anything that isn't one of those shapes.
//
// The first byte must be a channel-voice status, 0x80..0xEF. Every ordinary
// OSC message is a numeric argument list too, and without that test an XY pad
// sending "/pb3/xy 0.3 0.7" would read as a two-byte MIDI message with status
// 0 — a message no MIDI device can produce, built out of data that was never
// MIDI. System messages (0xF0 and up) are excluded for the opposite reason:
// SysEx and the real-time bytes have no three-byte form for this to decode.
// session::ControlHub::handleOsc runs this on every incoming message, so this
// predicate is the whole of the "is this MIDI?" decision.
std::optional<juce::MidiMessage> midiFromOsc(const juce::OSCMessage& msg);

// True when `ipv4` is in the IPv4 multicast range 224.0.0.0 - 239.255.255.255
// (AC-IO-7). Rejects malformed strings, out-of-range octets, and non-IPv4 input.
bool isOscMulticastAddress(const juce::String& ipv4);

} // namespace pedalboard3::control
