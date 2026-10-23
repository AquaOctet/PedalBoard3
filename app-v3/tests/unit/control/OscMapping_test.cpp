// Tests for the Phase 4 OSC mapping helpers: address parsing, argument value
// extraction, MIDI-over-OSC decoding (AC-IO-6), and multicast-range validation
// (AC-IO-7). All headless — pure juce::OSC data types, no sockets.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "control/OscMapping.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_osc/juce_osc.h>

using namespace pedalboard3::control;
using Catch::Approx;

TEST_CASE("OSC: tryParseOscAddress accepts valid and rejects invalid addresses", "[control][osc]")
{
    REQUIRE(tryParseOscAddress("/pb3/track1/gain").has_value());
    REQUIRE(tryParseOscAddress("/a").has_value());

    REQUIRE_FALSE(tryParseOscAddress("").has_value());
    REQUIRE_FALSE(tryParseOscAddress("noleadingslash").has_value());
    REQUIRE_FALSE(tryParseOscAddress("/has space").has_value());
    REQUIRE_FALSE(tryParseOscAddress("/has#hash").has_value());
}

TEST_CASE("OSC: oscArgumentValue reads float32 and promotes int32", "[control][osc]")
{
    juce::OSCMessage msg("/x");
    msg.addFloat32(0.5f);
    msg.addInt32(64);
    msg.addString("nope");

    REQUIRE(*oscArgumentValue(msg, 0) == Approx(0.5f));
    REQUIRE(*oscArgumentValue(msg, 1) == Approx(64.0f));
    REQUIRE_FALSE(oscArgumentValue(msg, 2).has_value()); // string
    REQUIRE_FALSE(oscArgumentValue(msg, 3).has_value()); // out of range
    REQUIRE_FALSE(oscArgumentValue(msg, -1).has_value());
}

TEST_CASE("OSC: midiFromOsc decodes a numeric triplet", "[control][osc]")
{
    // 0xB0 = CC on channel 1, controller 7, value 127.
    juce::OSCMessage msg("/midi", 176, 7, 127);
    auto m = midiFromOsc(msg);
    REQUIRE(m.has_value());
    REQUIRE(m->isController());
    REQUIRE(m->getChannel() == 1);
    REQUIRE(m->getControllerNumber() == 7);
    REQUIRE(m->getControllerValue() == 127);
}

TEST_CASE("OSC: midiFromOsc decodes float args and a 2-byte pair", "[control][osc]")
{
    juce::OSCMessage floats("/midi", 176.0f, 7.0f, 100.0f);
    auto cc = midiFromOsc(floats);
    REQUIRE(cc.has_value());
    REQUIRE(cc->isController());
    REQUIRE(cc->getControllerValue() == 100);

    // 0xC0 = program change on channel 1, program 5 (2-byte message).
    juce::OSCMessage pc("/midi", 192, 5);
    auto m = midiFromOsc(pc);
    REQUIRE(m.has_value());
    REQUIRE(m->isProgramChange());
    REQUIRE(m->getChannel() == 1);
    REQUIRE(m->getProgramChangeNumber() == 5);
}

TEST_CASE("OSC: midiFromOsc decodes 3- and 4-byte blobs", "[control][osc]")
{
    const juce::uint8 three[3] = {176, 7, 64};
    juce::OSCMessage blob3("/midi");
    blob3.addBlob(juce::MemoryBlock(three, 3));
    auto m3 = midiFromOsc(blob3);
    REQUIRE(m3.has_value());
    REQUIRE(m3->isController());
    REQUIRE(m3->getControllerNumber() == 7);
    REQUIRE(m3->getControllerValue() == 64);

    // OSC 'm' convention: leading port byte is dropped.
    const juce::uint8 four[4] = {0, 176, 7, 64};
    juce::OSCMessage blob4("/midi");
    blob4.addBlob(juce::MemoryBlock(four, 4));
    auto m4 = midiFromOsc(blob4);
    REQUIRE(m4.has_value());
    REQUIRE(m4->isController());
    REQUIRE(m4->getControllerNumber() == 7);
}

TEST_CASE("OSC: midiFromOsc rejects non-MIDI shapes", "[control][osc]")
{
    REQUIRE_FALSE(midiFromOsc(juce::OSCMessage("/x", 176)).has_value());                        // single number
    REQUIRE_FALSE(midiFromOsc(juce::OSCMessage("/x", juce::String("hi"), 7, 127)).has_value()); // non-numeric
    juce::OSCMessage twoByteBlob("/x");
    const juce::uint8 two[2] = {176, 7};
    twoByteBlob.addBlob(juce::MemoryBlock(two, 2));
    REQUIRE_FALSE(midiFromOsc(twoByteBlob).has_value()); // unsupported blob length
}

TEST_CASE("OSC: midiFromOsc rejects argument lists that are numbers but not MIDI", "[control][osc]")
{
    // An XY pad's two normalized floats, and a three-value control message.
    // Without the status-byte test these decode to a MIDI message with status
    // 0, built out of data that was never MIDI (#97).
    REQUIRE_FALSE(midiFromOsc(juce::OSCMessage("/pb3/xy", 0.3f, 0.7f)).has_value());
    REQUIRE_FALSE(midiFromOsc(juce::OSCMessage("/pb3/rgb", 12, 200, 64)).has_value());

    // 0xF0 and up is a system message: SysEx and the real-time bytes have no
    // three-byte form for this to decode.
    REQUIRE_FALSE(midiFromOsc(juce::OSCMessage("/midi", 0xF0, 7, 127)).has_value());
    REQUIRE_FALSE(midiFromOsc(juce::OSCMessage("/midi", 0xF8, 0, 0)).has_value());

    // Same test on the blob forms, in both layouts.
    juce::OSCMessage threeBlob("/midi");
    const juce::uint8 three[3] = {12, 200, 64};
    threeBlob.addBlob(juce::MemoryBlock(three, 3));
    REQUIRE_FALSE(midiFromOsc(threeBlob).has_value());

    juce::OSCMessage fourBlob("/midi");
    const juce::uint8 four[4] = {0, 12, 200, 64}; // port byte, then a non-status byte
    fourBlob.addBlob(juce::MemoryBlock(four, 4));
    REQUIRE_FALSE(midiFromOsc(fourBlob).has_value());

    // The boundaries themselves: 0x80 is a note-off, 0xEF the top of pitch bend.
    REQUIRE(midiFromOsc(juce::OSCMessage("/midi", 0x80, 60, 0)).has_value());
    REQUIRE(midiFromOsc(juce::OSCMessage("/midi", 0xEF, 0, 64)).has_value());
    REQUIRE_FALSE(midiFromOsc(juce::OSCMessage("/midi", 0x7F, 60, 0)).has_value());
}

TEST_CASE("OSC: isOscMulticastAddress validates the 224-239 range", "[control][osc]")
{
    REQUIRE(isOscMulticastAddress("224.0.0.1"));
    REQUIRE(isOscMulticastAddress("239.255.255.255"));
    REQUIRE(isOscMulticastAddress("230.1.2.3"));

    REQUIRE_FALSE(isOscMulticastAddress("223.255.255.255")); // just below
    REQUIRE_FALSE(isOscMulticastAddress("240.0.0.0"));       // just above
    REQUIRE_FALSE(isOscMulticastAddress("192.168.1.1"));
    REQUIRE_FALSE(isOscMulticastAddress("224.0.0.256")); // octet out of range
    REQUIRE_FALSE(isOscMulticastAddress("224.0.0"));     // too few octets
    REQUIRE_FALSE(isOscMulticastAddress("not.an.ip.addr"));
    REQUIRE_FALSE(isOscMulticastAddress(""));
}
