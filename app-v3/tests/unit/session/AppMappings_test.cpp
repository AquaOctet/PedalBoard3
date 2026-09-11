// Tests for pedalboard3::session::AppMappings — app-level MIDI/OSC mappings
// persisted in Preferences and fed to the hub beside the patch's mappings
// (roadmap M4 U3). Pure data + change broadcasting; no engine.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "session/AppMappings.h"

using namespace pedalboard3;
using Catch::Approx;

namespace
{

control::MidiMapping ccCommand(int cc, const char* commandId)
{
    control::MidiMapping m;
    m.source.kind = control::MidiSourceKind::ControlChange;
    m.source.channel = 3;
    m.source.controller = cc;
    m.target.kind = control::MidiTargetKind::AppCommand;
    m.target.commandId = commandId;
    m.rangeMin = 0.25f;
    m.latch = true;
    return m;
}

control::OscMapping oscCommand(const juce::String& address, const char* commandId)
{
    control::OscMapping m;
    m.address = address;
    m.argumentIndex = 1;
    m.target.kind = control::MidiTargetKind::AppCommand;
    m.target.commandId = commandId;
    m.inverted = true;
    return m;
}

struct Counter : juce::ChangeListener
{
    int count = 0;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
};

} // namespace

TEST_CASE("AppMappings: MIDI and OSC tables round-trip through XML with every field", "[session][app-mappings]")
{
    session::AppMappings a;
    a.setMidi({ccCommand(64, "patch.next"), ccCommand(65, "patch.previous")});
    a.setOsc({oscCommand("/pb3/next", "patch.next")});

    const auto xml = a.toXml();
    REQUIRE(xml != nullptr);
    REQUIRE(xml->hasTagName("APP_MAPPINGS"));

    session::AppMappings b;
    REQUIRE(b.restoreFromXml(*xml));
    REQUIRE(b.midi() == a.midi());
    REQUIRE(b.osc() == a.osc());
    REQUIRE(b.midi()[0].source.channel == 3);
    REQUIRE(b.midi()[0].rangeMin == Approx(0.25f));
    REQUIRE(b.midi()[0].latch);
    REQUIRE(b.osc()[0].argumentIndex == 1);
    REQUIRE(b.osc()[0].inverted);
}

TEST_CASE("AppMappings: empty tables serialise and restore empty", "[session][app-mappings]")
{
    session::AppMappings a;
    const auto xml = a.toXml();
    REQUIRE(xml->getChildByName("MAPPINGS") == nullptr);

    session::AppMappings b;
    b.setMidi({ccCommand(1, "patch.next")});
    REQUIRE(b.restoreFromXml(*xml));
    REQUIRE(b.midi().empty());
    REQUIRE(b.osc().empty());
}

TEST_CASE("AppMappings: a changing set broadcasts; an identical set does not", "[session][app-mappings]")
{
    // sendChangeMessage posts through the MessageManager; with no instance the
    // post fails and nothing is left pending for dispatchPendingMessages. The
    // engine-backed suites get one from their fixtures; this one creates it.
    juce::MessageManager::getInstance();

    session::AppMappings a;
    Counter counter;
    a.addChangeListener(&counter);

    a.setMidi({ccCommand(64, "patch.next")});
    a.dispatchPendingMessages();
    REQUIRE(counter.count == 1);

    a.setMidi({ccCommand(64, "patch.next")}); // identical
    a.setOsc({});                             // identical (already empty)
    a.dispatchPendingMessages();
    REQUIRE(counter.count == 1);

    a.setOsc({oscCommand("/pb3/next", "patch.next")});
    a.dispatchPendingMessages();
    REQUIRE(counter.count == 2);

    a.removeChangeListener(&counter);
}

TEST_CASE("AppMappings: restoreFromXml rejects a foreign element and keeps the tables", "[session][app-mappings]")
{
    session::AppMappings a;
    a.setMidi({ccCommand(64, "patch.next")});

    juce::XmlElement foreign("PATCH");
    REQUIRE_FALSE(a.restoreFromXml(foreign));
    REQUIRE(a.midi().size() == 1);
}
