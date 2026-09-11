// Tests for pedalboard3::session::Preferences -- the per-user persistence
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

#include "session/DefaultBank.h"
#include "session/PdlSerializer.h"
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

juce::PluginDescription makeFakeDesc()
{
    juce::PluginDescription d;
    d.name = "Pedalboard 3 Unit-Test Synth";
    d.descriptiveName = "Pedalboard 3 Unit-Test Synth (descriptive)";
    d.pluginFormatName = "VST3";
    d.category = "Synth";
    d.manufacturerName = "Pedalboard 3 Tests";
    d.version = "9.9.9";
    d.fileOrIdentifier = "/nonexistent/Pedalboard3UnitTestSynth.vst3";
    d.uniqueId = static_cast<int>(0x0EADBEEF);
    d.deprecatedUid = static_cast<int>(0x0EADBEEF);
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
    d.isInstrument = true;
    return d;
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

TEST_CASE("Preferences: OSC port defaults to 9000 and round-trips", "[session][preferences]")
{
    session::Preferences prefs;

    // Default when unset -- scrub first in case a previous aborted run left
    // a value behind (this test's own cleanup could not run).
    if (auto* store = prefs.getStore())
        store->removeValue("oscPort");
    prefs.flush();
    REQUIRE(prefs.getOscPort() == session::Preferences::kDefaultOscPort);

    prefs.setOscPort(9001);
    prefs.flush();
    REQUIRE(prefs.getOscPort() == 9001);

    if (auto* store = prefs.getStore())
        store->removeValue("oscPort");
    prefs.flush();
}

TEST_CASE("Preferences: OSC multicast address defaults to empty and round-trips", "[session][preferences]")
{
    session::Preferences prefs;

    if (auto* store = prefs.getStore())
        store->removeValue("oscMulticastAddress");
    prefs.flush();
    REQUIRE(prefs.getOscMulticastAddress().isEmpty());

    prefs.setOscMulticastAddress("239.1.2.3");
    prefs.flush();
    REQUIRE(prefs.getOscMulticastAddress() == "239.1.2.3");

    if (auto* store = prefs.getStore())
        store->removeValue("oscMulticastAddress");
    prefs.flush();
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
    prefs.getStore()->removeValue("activePluginDescription");
    prefs.flush();

    REQUIRE(prefs.getAudioDeviceState() == nullptr);
    REQUIRE(prefs.getKnownPluginList() == nullptr);
    REQUIRE(prefs.getActivePluginDescription() == nullptr);
}

TEST_CASE("Preferences: ActivePluginDescription round-trips through the store", "[session][preferences]")
{
    session::Preferences prefs;
    prefs.clearActivePluginDescription();
    prefs.flush();

    auto desc = makeFakeDesc();
    prefs.setActivePluginDescription(desc);
    prefs.flush();

    auto roundTripped = prefs.getActivePluginDescription();
    REQUIRE(roundTripped != nullptr);
    REQUIRE(roundTripped->name == desc.name);
    REQUIRE(roundTripped->pluginFormatName == desc.pluginFormatName);
    REQUIRE(roundTripped->fileOrIdentifier == desc.fileOrIdentifier);
    REQUIRE(roundTripped->manufacturerName == desc.manufacturerName);
    REQUIRE(roundTripped->isInstrument == desc.isInstrument);
    REQUIRE(roundTripped->uniqueId == desc.uniqueId);

    prefs.clearActivePluginDescription();
    prefs.flush();
}

TEST_CASE("Preferences: ActivePluginDescription persists across instance reconstruction", "[session][preferences]")
{
    {
        session::Preferences first;
        first.setActivePluginDescription(makeFakeDesc());
        first.flush();
    }

    {
        session::Preferences second;
        auto desc = second.getActivePluginDescription();
        REQUIRE(desc != nullptr);
        REQUIRE(desc->fileOrIdentifier == "/nonexistent/Pedalboard3UnitTestSynth.vst3");
        second.clearActivePluginDescription();
        second.flush();
    }
}

TEST_CASE("Preferences: clearActivePluginDescription drops the value", "[session][preferences]")
{
    session::Preferences prefs;
    prefs.setActivePluginDescription(makeFakeDesc());
    prefs.flush();
    REQUIRE(prefs.getActivePluginDescription() != nullptr);

    prefs.clearActivePluginDescription();
    prefs.flush();
    REQUIRE(prefs.getActivePluginDescription() == nullptr);
}

TEST_CASE("Preferences: malformed ActivePluginDescription XML returns nullptr (no crash)", "[session][preferences]")
{
    session::Preferences prefs;

    // Write something that is *not* a <PLUGIN> root -- loadFromXml should
    // refuse it and the getter should return nullptr instead of UB.
    juce::XmlElement bogus("NOT_A_PLUGIN");
    bogus.setAttribute("garbage", "yes");
    prefs.getStore()->setValue("activePluginDescription", &bogus);
    prefs.flush();

    REQUIRE(prefs.getActivePluginDescription() == nullptr);

    prefs.clearActivePluginDescription();
    prefs.flush();
}

TEST_CASE("Preferences: ActivePluginDescription is XXE-safe (AC-1.D.2-K)", "[session][preferences][security]")
{
    session::Preferences prefs;

    // Construct a payload that, on a DTD-resolving parser, would expand
    // &x; into the contents of /etc/passwd. juce::parseXML(const String&)
    // does not install an inputSource, so SYSTEM-entity expansion silently
    // fails and the parsed file attribute stays empty (or unchanged).
    const juce::String malicious = "<!DOCTYPE foo [<!ENTITY x SYSTEM \"file:///etc/passwd\">]>"
                                   "<PLUGIN name=\"Evil\" format=\"VST3\" file=\"&x;\"/>";

    prefs.getStore()->setValue("activePluginDescription", malicious);
    prefs.flush();

    if (auto desc = prefs.getActivePluginDescription())
    {
        // Whatever loadFromXml produced, the file path must NOT contain
        // contents of /etc/passwd (which would start with "root:" on
        // every Unix system on Earth).
        REQUIRE_FALSE(desc->fileOrIdentifier.startsWith("root:"));
        REQUIRE_FALSE(desc->fileOrIdentifier.contains("/bin/bash"));
    }
    // If desc is null, that's also acceptable -- a strict parser refused
    // the payload outright, which is the safest outcome.

    prefs.clearActivePluginDescription();
    prefs.flush();
}

// ---- Phase 1.D.3: ActivePatch round-trip ---------------------------------

namespace
{

session::Patch makeFakeChain()
{
    session::Patch p;
    p.name = "TestChain";

    session::PatchNode a;
    a.id = 1;
    a.bypass = false;
    a.customName = "Comp";
    a.desc.name = "Compressor";
    a.desc.pluginFormatName = "VST3";
    a.desc.fileOrIdentifier = "/fake/Compressor.vst3";

    session::PatchNode b;
    b.id = 2;
    b.bypass = true;
    b.customName = "Verb";
    b.desc.name = "Reverb";
    b.desc.pluginFormatName = "VST3";
    b.desc.fileOrIdentifier = "/fake/Reverb.vst3";

    p.nodes.push_back(a);
    p.nodes.push_back(b);

    p.connections.push_back({session::graphIO, 0, 1, 0});
    p.connections.push_back({session::graphIO, 1, 1, 1});
    p.connections.push_back({1, 0, 2, 0});
    p.connections.push_back({1, 1, 2, 1});
    p.connections.push_back({2, 0, session::graphIO, 0});
    p.connections.push_back({2, 1, session::graphIO, 1});
    return p;
}

} // namespace

TEST_CASE("Preferences: ActivePatch round-trips through the store", "[session][preferences][patch]")
{
    session::Preferences prefs;
    prefs.clearActivePatch();
    prefs.flush();
    REQUIRE(prefs.getActivePatch() == nullptr);

    auto p = makeFakeChain();
    prefs.setActivePatch(p);
    prefs.flush();

    auto restored = prefs.getActivePatch();
    REQUIRE(restored != nullptr);
    REQUIRE(restored->name == "TestChain");
    REQUIRE(restored->nodes.size() == 2);
    REQUIRE(restored->nodes[0].id == 1);
    REQUIRE(restored->nodes[0].desc.name == "Compressor");
    REQUIRE(restored->nodes[0].customName == "Comp");
    REQUIRE_FALSE(restored->nodes[0].bypass);
    REQUIRE(restored->nodes[1].id == 2);
    REQUIRE(restored->nodes[1].bypass);
    REQUIRE(restored->connections.size() == 6);

    prefs.clearActivePatch();
    prefs.flush();
}

TEST_CASE("Preferences: ActivePatch persists across instance reconstruction", "[session][preferences][patch]")
{
    {
        session::Preferences first;
        first.setActivePatch(makeFakeChain());
        first.flush();
    }
    {
        session::Preferences second;
        auto p = second.getActivePatch();
        REQUIRE(p != nullptr);
        REQUIRE(p->nodes.size() == 2);
        REQUIRE(p->name == "TestChain");
        second.clearActivePatch();
        second.flush();
    }
}

TEST_CASE("Preferences: clearActivePatch drops the value", "[session][preferences][patch]")
{
    session::Preferences prefs;
    prefs.setActivePatch(makeFakeChain());
    prefs.flush();
    REQUIRE(prefs.getActivePatch() != nullptr);

    prefs.clearActivePatch();
    prefs.flush();
    REQUIRE(prefs.getActivePatch() == nullptr);
}

TEST_CASE("Preferences: malformed ActivePatch XML returns nullptr", "[session][preferences][patch]")
{
    session::Preferences prefs;
    juce::XmlElement bogus("NOT_AN_ACTIVE_PATCH");
    prefs.getStore()->setValue("activePatch", &bogus);
    prefs.flush();
    REQUIRE(prefs.getActivePatch() == nullptr);

    prefs.clearActivePatch();
    prefs.flush();
}

TEST_CASE("Preferences: ActivePatch with structural corruption is rejected", "[session][preferences][patch][validate]")
{
    session::Preferences prefs;

    juce::XmlElement root("ACTIVE_PATCH");
    root.setAttribute("name", "Bad");
    auto* nodesEl = root.createNewChildElement("NODES");
    auto* n1 = nodesEl->createNewChildElement("NODE");
    n1->setAttribute("id", 1);
    auto* id1 = n1->createNewChildElement("IDENTITY");
    id1->setAttribute("name", "A");
    id1->setAttribute("format", "VST3");
    id1->setAttribute("file", "/fake/A.vst3");
    auto* n2 = nodesEl->createNewChildElement("NODE");
    n2->setAttribute("id", 1); // dup!
    auto* id2 = n2->createNewChildElement("IDENTITY");
    id2->setAttribute("name", "B");
    id2->setAttribute("format", "VST3");
    id2->setAttribute("file", "/fake/B.vst3");

    prefs.getStore()->setValue("activePatch", &root);
    prefs.flush();

    REQUIRE(prefs.getActivePatch() == nullptr);

    prefs.clearActivePatch();
    prefs.flush();
}

// ---- Active BANK (roadmap M3 U3) ---------------------------------------------

TEST_CASE("Preferences: ActiveBank round-trips and supersedes the legacy ActivePatch", "[session][preferences][bank]")
{
    session::Preferences prefs;
    prefs.clearActiveBank();
    prefs.flush();
    REQUIRE(prefs.getActiveBank() == nullptr);

    session::PatchBank bank;
    auto second = makeFakeChain();
    second.name = "Second";
    bank.patches = {makeFakeChain(), second};
    bank.activeIndex = 1;

    prefs.setActiveBank(bank);
    prefs.flush();

    auto restored = prefs.getActiveBank();
    REQUIRE(restored != nullptr);
    REQUIRE(restored->size() == 2);
    REQUIRE(restored->activeIndex == 1);
    REQUIRE(restored->patches[0].name == "TestChain");
    REQUIRE(restored->patches[1].name == "Second");
    REQUIRE(restored->patches[1].nodes.size() == 2);
    REQUIRE(restored->patches[1].nodes[1].bypass);

    // The legacy single-patch value is gone once a bank is written.
    REQUIRE(prefs.getActivePatch() == nullptr);

    prefs.clearActiveBank();
    prefs.flush();
    REQUIRE(prefs.getActiveBank() == nullptr);
}

TEST_CASE("Preferences: a legacy ActivePatch restores as a one-patch bank", "[session][preferences][bank]")
{
    session::Preferences prefs;
    prefs.clearActiveBank();
    prefs.setActivePatch(makeFakeChain());
    prefs.flush();

    auto bank = prefs.getActiveBank();
    REQUIRE(bank != nullptr);
    REQUIRE(bank->size() == 1);
    REQUIRE(bank->activeIndex == 0);
    REQUIRE(bank->active().name == "TestChain");

    prefs.clearActiveBank();
    prefs.flush();
    REQUIRE(prefs.getActivePatch() == nullptr);
}

TEST_CASE("Preferences: a bank with a corrupt patch is rejected", "[session][preferences][bank]")
{
    session::Preferences prefs;
    prefs.clearActiveBank();

    auto* store = prefs.getStore();
    REQUIRE(store != nullptr);
    auto bad = makeFakeChain();
    bad.connections.push_back({1, 0, 99, 0}); // dangling
    session::PatchBank bank;
    bank.patches = {makeFakeChain(), bad};
    auto xml = session::bankToXml(bank);
    REQUIRE(xml != nullptr);
    store->setValue("activeBank", xml.get());
    prefs.flush();

    REQUIRE(prefs.getActiveBank() == nullptr);

    prefs.clearActiveBank();
    prefs.flush();
}

TEST_CASE("Preferences: app mappings XML round-trips through the store", "[session][preferences][app-mappings]")
{
    session::Preferences prefs;
    juce::XmlElement xml("APP_MAPPINGS");
    xml.setAttribute("probe", 7);
    prefs.setAppMappings(xml);
    prefs.flush();

    auto back = prefs.getAppMappings();
    REQUIRE(back != nullptr);
    REQUIRE(back->hasTagName("APP_MAPPINGS"));
    REQUIRE(back->getIntAttribute("probe") == 7);

    if (auto* store = prefs.getStore())
        store->removeValue("appMappings");
    prefs.flush();
}

TEST_CASE("Preferences: key mappings XML round-trips through the store", "[session][preferences][key-mappings]")
{
    session::Preferences prefs;
    juce::XmlElement xml("KEYMAPPINGS");
    xml.setAttribute("basedOnDefaults", true);
    prefs.setKeyMappings(xml);
    prefs.flush();

    auto back = prefs.getKeyMappings();
    REQUIRE(back != nullptr);
    REQUIRE(back->hasTagName("KEYMAPPINGS"));
    REQUIRE(back->getBoolAttribute("basedOnDefaults"));

    if (auto* store = prefs.getStore())
        store->removeValue("keyMappings");
    prefs.flush();
}

TEST_CASE("Preferences: the active bank's file and unsaved flag round-trip and clear with the bank",
          "[session][preferences][bank]")
{
    session::Preferences prefs;
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gig.pdl");

    prefs.setActiveBankIdentity(file, true);
    prefs.flush();
    REQUIRE(prefs.getActiveBankFile() == file);
    REQUIRE(prefs.getActiveBankModified());

    prefs.setActiveBankIdentity(juce::File(), false);
    REQUIRE(prefs.getActiveBankFile() == juce::File());
    REQUIRE_FALSE(prefs.getActiveBankModified());

    prefs.setActiveBankIdentity(file, true);
    prefs.clearActiveBank();
    REQUIRE(prefs.getActiveBankFile() == juce::File());
    REQUIRE_FALSE(prefs.getActiveBankModified());
    prefs.flush();
}

// ---- Default bank and quit-without-saving (finish pass U4 / U5) --------------

TEST_CASE("DefaultBank: the default lives beside the settings file", "[session][default-bank]")
{
    session::Preferences prefs;
    REQUIRE(prefs.getStore() != nullptr);
    const auto file = session::defaultBankFile(prefs);
    REQUIRE(file.getFileName() == "default.pdl");
    REQUIRE(file.getParentDirectory() == prefs.getStore()->getFile().getParentDirectory());
}

TEST_CASE("DefaultBank: New starts from the default bank, or one empty patch", "[session][default-bank]")
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("pb3-default-bank-test");
    dir.deleteRecursively();
    const auto file = dir.getChildFile("default.pdl");

    REQUIRE(session::readDefaultBank(file) == nullptr);
    const auto empty = session::newBoardBank(file);
    REQUIRE(empty.size() == 1);
    REQUIRE(empty.active().nodes.empty());

    session::PatchBank bank;
    auto second = makeFakeChain();
    second.name = "Second";
    bank.patches = {makeFakeChain(), second};
    REQUIRE(session::pdl::writeBank(bank, file).wasOk());

    const auto fresh = session::newBoardBank(file);
    REQUIRE(fresh.size() == 2);
    REQUIRE(fresh.patches[1].name == "Second");

    dir.deleteRecursively();
}

TEST_CASE("DefaultBank: Don't Save at quit reverts the persisted bank to the saved file, or clears it",
          "[session][default-bank][preferences]")
{
    session::Preferences prefs;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("pb3-revert-test");
    dir.deleteRecursively();
    const auto saved = dir.getChildFile("gig.pdl");

    session::PatchBank onDisk;
    onDisk.patches = {makeFakeChain()};
    onDisk.patches[0].name = "On disk";
    REQUIRE(session::pdl::writeBank(onDisk, saved).wasOk());

    session::PatchBank edited;
    edited.patches = {makeFakeChain()};
    edited.patches[0].name = "Edited";
    prefs.setActiveBank(edited);
    prefs.setActiveBankIdentity(saved, true);

    session::revertPersistedBank(prefs, saved);
    auto restored = prefs.getActiveBank();
    REQUIRE(restored != nullptr);
    REQUIRE(restored->active().name == "On disk");
    REQUIRE(prefs.getActiveBankFile() == saved);
    REQUIRE_FALSE(prefs.getActiveBankModified());

    // Untitled: nothing saved to go back to, so the next launch opens the default.
    prefs.setActiveBank(edited);
    session::revertPersistedBank(prefs, juce::File());
    REQUIRE(prefs.getActiveBank() == nullptr);

    prefs.flush();
    dir.deleteRecursively();
}

TEST_CASE("Preferences: the window state round-trips", "[session][preferences][window]")
{
    session::Preferences prefs;
    const auto previous = prefs.getWindowState(); // the developer's real window; put it back

    prefs.setWindowState("120 80 1280 720");
    prefs.flush();
    REQUIRE(prefs.getWindowState() == "120 80 1280 720");

    prefs.setWindowState(previous);
    prefs.flush();
}
