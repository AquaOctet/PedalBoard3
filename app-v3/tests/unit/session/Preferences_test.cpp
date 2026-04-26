// Tests for pedalboard3::session::Preferences — the per-user persistence
// store backed by juce::ApplicationProperties.
//
// We can't easily redirect JUCE's storage path inside a unit test (the
// PropertiesFile::Options API doesn't expose an absolute-path override
// without going through the platform helpers). Instead these tests
// exercise round-trip semantics on the live store: write XML, read it
// back, verify content matches. They DO touch the user's real settings
// directory, so they use a unique top-level XML element name nobody
// else writes. Cleanup runs at end of each test.

#include <catch2/catch_test_macros.hpp>

#include "session/Preferences.h"

#include <juce_data_structures/juce_data_structures.h>

using namespace pedalboard3;

namespace
{

// Replace any value the test wrote with an empty marker so it doesn't
// pollute developers' real Preferences file across runs.
void scrubKey(session::Preferences& p, const juce::String& topElement)
{
    juce::XmlElement empty(topElement);
    if (topElement == "DEVICESETUP")
        p.setAudioDeviceState(empty);
    else if (topElement == "KNOWNPLUGINS")
        p.setKnownPluginList(empty);
    p.flush();
}

} // namespace

TEST_CASE("Preferences: getStore returns a usable PropertiesFile after construction", "[session][preferences]")
{
    session::Preferences prefs;
    REQUIRE(prefs.getStore() != nullptr);
    // It should be writeable (the OS-conventional path is created lazily).
    prefs.getStore()->setValue("__pb3_unit_test_smoke__", "ok");
    prefs.flush();
    REQUIRE(prefs.getStore()->getValue("__pb3_unit_test_smoke__") == "ok");
    prefs.getStore()->removeValue("__pb3_unit_test_smoke__");
    prefs.flush();
}

TEST_CASE("Preferences: AudioDeviceState round-trips through the store", "[session][preferences]")
{
    session::Preferences prefs;

    juce::XmlElement state("DEVICESETUP");
    state.setAttribute("deviceType", "TestDevice");
    state.setAttribute("audioOutputDeviceName", "Bus 1/2");
    state.setAttribute("sampleRate", "48000");

    prefs.setAudioDeviceState(state);
    prefs.flush();

    auto roundTripped = prefs.getAudioDeviceState();
    REQUIRE(roundTripped != nullptr);
    REQUIRE(roundTripped->getStringAttribute("deviceType") == "TestDevice");
    REQUIRE(roundTripped->getStringAttribute("audioOutputDeviceName") == "Bus 1/2");
    REQUIRE(roundTripped->getStringAttribute("sampleRate") == "48000");

    scrubKey(prefs, "DEVICESETUP");
}

TEST_CASE("Preferences: KnownPluginList round-trips through the store", "[session][preferences]")
{
    session::Preferences prefs;

    juce::XmlElement list("KNOWNPLUGINS");
    auto* entry = list.createNewChildElement("PLUGIN");
    entry->setAttribute("name", "Fake VST3");
    entry->setAttribute("format", "VST3");
    entry->setAttribute("file", "/nonexistent/Fake.vst3");

    prefs.setKnownPluginList(list);
    prefs.flush();

    auto roundTripped = prefs.getKnownPluginList();
    REQUIRE(roundTripped != nullptr);
    auto* recovered = roundTripped->getChildByName("PLUGIN");
    REQUIRE(recovered != nullptr);
    REQUIRE(recovered->getStringAttribute("name") == "Fake VST3");
    REQUIRE(recovered->getStringAttribute("format") == "VST3");

    scrubKey(prefs, "KNOWNPLUGINS");
}

TEST_CASE("Preferences: destroying and reconstructing preserves stored values", "[session][preferences]")
{
    {
        session::Preferences first;
        juce::XmlElement state("DEVICESETUP");
        state.setAttribute("audioOutputDeviceName", "PersistAcrossInstances");
        first.setAudioDeviceState(state);
        first.flush();
    }

    {
        session::Preferences second;
        auto state = second.getAudioDeviceState();
        REQUIRE(state != nullptr);
        REQUIRE(state->getStringAttribute("audioOutputDeviceName") == "PersistAcrossInstances");
        scrubKey(second, "DEVICESETUP");
    }
}

TEST_CASE("Preferences: flush is idempotent", "[session][preferences]")
{
    session::Preferences prefs;
    prefs.flush();
    prefs.flush();
    prefs.flush();
    SUCCEED();
}

TEST_CASE("Preferences: getters return nullptr when no value is stored", "[session][preferences]")
{
    session::Preferences prefs;
    // Make sure any stale data from a prior test run is cleared.
    prefs.getStore()->removeValue("audioDeviceState");
    prefs.getStore()->removeValue("knownPluginList");
    prefs.flush();

    REQUIRE(prefs.getAudioDeviceState() == nullptr);
    REQUIRE(prefs.getKnownPluginList() == nullptr);
}
