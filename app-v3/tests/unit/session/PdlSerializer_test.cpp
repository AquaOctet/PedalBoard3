// Tests for pedalboard3::session::pdl -- .pdl file read/write (master
// plan §4.5). These exercise the real file path (write to a temp .pdl,
// read it back) plus the schema-major gate and malformed-input handling.

#include <catch2/catch_test_macros.hpp>

#include "session/PdlSerializer.h"

using namespace pedalboard3;

namespace
{

session::Patch makeChain()
{
    session::Patch p;
    p.name = "Test Board";

    session::PatchNode n;
    n.id = 1;
    n.desc.name = "EQ";
    n.desc.pluginFormatName = "VST3";
    n.desc.fileOrIdentifier = "/fake/EQ.vst3";
    n.bypass = true;
    n.state.append("statebytes", 10); // 10-byte plugin state blob
    p.nodes.push_back(n);

    p.connections.push_back({session::graphIO, 0, 1, 0});
    p.connections.push_back({1, 0, session::graphIO, 0});
    return p;
}

juce::File tempPdl(const juce::String& tag)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("pb3-pdl-test-" + tag + "-" + juce::String(juce::Time::currentTimeMillis()) + ".pdl");
}

} // namespace

TEST_CASE("pdl: write then read round-trips a patch with plugin state", "[session][pdl]")
{
    const auto file = tempPdl("roundtrip");
    const auto patch = makeChain();

    const auto wrote = session::pdl::writePatch(patch, file);
    REQUIRE(wrote.wasOk());
    REQUIRE(file.existsAsFile());

    auto loaded = session::pdl::readPatch(file);
    REQUIRE(loaded.patch != nullptr);
    REQUIRE(loaded.error.isEmpty());
    REQUIRE(loaded.patch->name == "Test Board");
    REQUIRE(loaded.patch->nodes.size() == 1);
    REQUIRE(loaded.patch->nodes[0].bypass);
    REQUIRE(loaded.patch->nodes[0].desc.name == "EQ");
    REQUIRE(loaded.patch->nodes[0].state.getSize() == 10);
    REQUIRE(loaded.patch->nodes[0].state.toString() == "statebytes");
    REQUIRE(loaded.patch->connections.size() == 2);

    file.deleteFile();
}

TEST_CASE("pdl: write then read round-trips MIDI and OSC mappings", "[session][pdl]")
{
    const auto file = tempPdl("mappings");
    auto patch = makeChain();

    control::MidiMapping mm;
    mm.source.kind = control::MidiSourceKind::ControlChange;
    mm.source.channel = 1;
    mm.source.controller = 7;
    mm.rangeMin = 0.1f;
    mm.rangeMax = 0.9f;
    mm.inverted = true;
    mm.latch = true;
    mm.target.node = 1;
    mm.target.parameterIndex = 3;

    control::OscMapping om;
    om.address = "/pb3/gain";
    om.argumentIndex = 0;
    om.target.kind = control::MidiTargetKind::AppCommand;
    // XML metacharacters must escape on write and unescape on parse through the
    // real .pdl text path (in-memory round-trips never exercise this).
    om.target.commandId = "a & b < c > \"d\" 'e'/f";

    patch.midiMappings = {mm};
    patch.oscMappings = {om};

    REQUIRE(session::pdl::writePatch(patch, file).wasOk());
    auto loaded = session::pdl::readPatch(file);
    REQUIRE(loaded.patch != nullptr);
    REQUIRE(loaded.error.isEmpty());
    REQUIRE(loaded.patch->midiMappings == patch.midiMappings);
    REQUIRE(loaded.patch->oscMappings == patch.oscMappings);

    file.deleteFile();
}

TEST_CASE("pdl: a file without <MAPPINGS> reads with empty mapping tables", "[session][pdl]")
{
    const auto file = tempPdl("nomaps");

    juce::XmlElement root("PEDALBOARD");
    root.setAttribute("schemaMajor", session::pdl::kSchemaMajor);
    root.setAttribute("schemaMinor", 0); // pre-mappings minor
    auto* patchEl = root.createNewChildElement("PATCH");
    patchEl->setAttribute("name", "Legacy");
    patchEl->createNewChildElement("NODES");
    patchEl->createNewChildElement("CONNECTIONS");
    REQUIRE(root.writeTo(file));

    auto loaded = session::pdl::readPatch(file);
    REQUIRE(loaded.patch != nullptr);
    REQUIRE(loaded.error.isEmpty());
    REQUIRE(loaded.patch->midiMappings.empty());
    REQUIRE(loaded.patch->oscMappings.empty());

    file.deleteFile();
}

TEST_CASE("pdl: a newer schemaMajor is rejected with a friendly error", "[session][pdl]")
{
    const auto file = tempPdl("newer");

    juce::XmlElement root("PEDALBOARD");
    root.setAttribute("schemaMajor", session::pdl::kSchemaMajor + 1);
    root.setAttribute("schemaMinor", 0);
    auto* patchEl = root.createNewChildElement("PATCH");
    patchEl->createNewChildElement("NODES");
    patchEl->createNewChildElement("CONNECTIONS");
    REQUIRE(root.writeTo(file));

    auto loaded = session::pdl::readPatch(file);
    REQUIRE(loaded.patch == nullptr);
    REQUIRE(loaded.error.isNotEmpty());

    file.deleteFile();
}

TEST_CASE("pdl: non-pedalboard XML is rejected", "[session][pdl]")
{
    const auto file = tempPdl("bogus");
    file.replaceWithText("<NOT_A_PEDALBOARD/>");

    auto loaded = session::pdl::readPatch(file);
    REQUIRE(loaded.patch == nullptr);
    REQUIRE(loaded.error.isNotEmpty());

    file.deleteFile();
}

TEST_CASE("pdl: a missing file is rejected", "[session][pdl]")
{
    auto loaded = session::pdl::readPatch(juce::File("/totally/nonexistent/board.pdl"));
    REQUIRE(loaded.patch == nullptr);
    REQUIRE(loaded.error.isNotEmpty());
}

TEST_CASE("pdl: write then read round-trips an internal node", "[session][pdl][internal]")
{
    const auto file = tempPdl("internal");

    session::Patch p;
    p.name = "Built-in Board";

    session::PatchNode level;
    level.id = 1;
    level.kind = session::NodeKind::Internal;
    level.internalType = "LevelProcessor";
    level.bypass = true;
    level.customName = "Trim";
    p.nodes.push_back(level);

    p.connections.push_back({session::graphIO, 0, 1, 0});
    p.connections.push_back({1, 0, session::graphIO, 0});

    const auto wrote = session::pdl::writePatch(p, file);
    REQUIRE(wrote.wasOk());
    REQUIRE(file.existsAsFile());

    auto loaded = session::pdl::readPatch(file);
    REQUIRE(loaded.patch != nullptr);
    REQUIRE(loaded.error.isEmpty());
    REQUIRE(loaded.patch->nodes.size() == 1);

    const auto& n0 = loaded.patch->nodes[0];
    REQUIRE(n0.kind == session::NodeKind::Internal);
    REQUIRE(n0.internalType == "LevelProcessor");
    REQUIRE(n0.bypass);
    REQUIRE(n0.customName == "Trim");
    REQUIRE(loaded.patch->connections.size() == 2);

    file.deleteFile();
}

TEST_CASE("pdl: write then read round-trips node canvas positions", "[session][pdl][position]")
{
    const auto file = tempPdl("positions");

    auto p = makeChain(); // single node "EQ"
    p.nodes[0].x = 256.0f;
    p.nodes[0].y = 96.5f;

    REQUIRE(session::pdl::writePatch(p, file).wasOk());

    auto loaded = session::pdl::readPatch(file);
    REQUIRE(loaded.patch != nullptr);
    REQUIRE(loaded.error.isEmpty());
    REQUIRE(loaded.patch->nodes.size() == 1);
    REQUIRE(loaded.patch->nodes[0].hasPosition());
    REQUIRE(loaded.patch->nodes[0].x == 256.0f);
    REQUIRE(loaded.patch->nodes[0].y == 96.5f);

    file.deleteFile();
}

// ---- Banks (schemaMinor 4, roadmap M3 U2) ----------------------------------

namespace
{

session::Patch namedChain(const juce::String& name, int nodeId)
{
    auto p = makeChain();
    p.name = name;
    p.nodes[0].id = static_cast<session::NodeId>(nodeId);
    p.nodes[0].desc.name = name + " EQ";
    p.connections = {{session::graphIO, 0, static_cast<session::NodeId>(nodeId), 0},
                     {static_cast<session::NodeId>(nodeId), 0, session::graphIO, 0}};
    return p;
}

} // namespace

TEST_CASE("pdl: writeBank then readBank round-trips a 3-patch bank with its active index", "[session][pdl][bank]")
{
    const auto file = tempPdl("bank");

    session::PatchBank bank;
    bank.patches = {namedChain("One", 1), namedChain("Two", 2), namedChain("Three", 3)};
    bank.activeIndex = 2;

    REQUIRE(session::pdl::writeBank(bank, file).wasOk());

    auto loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank != nullptr);
    REQUIRE(loaded.error.isEmpty());
    REQUIRE(loaded.bank->size() == 3);
    REQUIRE(loaded.bank->activeIndex == 2);
    REQUIRE(loaded.bank->patches[0].name == "One");
    REQUIRE(loaded.bank->patches[1].name == "Two");
    REQUIRE(loaded.bank->patches[2].name == "Three");
    REQUIRE(loaded.bank->patches[1].nodes[0].id == 2);
    REQUIRE(loaded.bank->patches[1].nodes[0].state.toString() == "statebytes");
    REQUIRE(loaded.bank->active().name == "Three");

    // The on-disk shape is the <BANK> form with the new minor.
    auto xml = juce::parseXML(file);
    REQUIRE(xml != nullptr);
    REQUIRE(xml->getIntAttribute("schemaMinor") == session::pdl::kSchemaMinor);
    REQUIRE(xml->getChildByName("BANK") != nullptr);
    REQUIRE(xml->getChildByName("PATCH") == nullptr);

    file.deleteFile();
}

TEST_CASE("pdl: writePatch writes a one-patch bank and readPatch returns it", "[session][pdl][bank]")
{
    const auto file = tempPdl("bank-single");
    REQUIRE(session::pdl::writePatch(makeChain(), file).wasOk());

    auto bank = session::pdl::readBank(file);
    REQUIRE(bank.bank != nullptr);
    REQUIRE(bank.bank->size() == 1);
    REQUIRE(bank.bank->activeIndex == 0);

    auto single = session::pdl::readPatch(file);
    REQUIRE(single.patch != nullptr);
    REQUIRE(single.patch->name == "Test Board");

    file.deleteFile();
}

TEST_CASE("pdl: a schemaMinor 3 file with a bare <PATCH> reads as a one-patch bank", "[session][pdl][bank]")
{
    const auto file = tempPdl("bank-legacy");

    juce::XmlElement root("PEDALBOARD");
    root.setAttribute("schemaMajor", session::pdl::kSchemaMajor);
    root.setAttribute("schemaMinor", 3);
    root.addChildElement(session::patchToXml(makeChain()).release());
    REQUIRE(root.writeTo(file));

    auto loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank != nullptr);
    REQUIRE(loaded.error.isEmpty());
    REQUIRE(loaded.bank->size() == 1);
    REQUIRE(loaded.bank->activeIndex == 0);
    REQUIRE(loaded.bank->active().name == "Test Board");
    REQUIRE(loaded.bank->active().nodes.size() == 1);

    file.deleteFile();
}

TEST_CASE("pdl: readPatch on a multi-patch bank returns the active patch", "[session][pdl][bank]")
{
    const auto file = tempPdl("bank-active");

    session::PatchBank bank;
    bank.patches = {namedChain("One", 1), namedChain("Two", 2)};
    bank.activeIndex = 1;
    REQUIRE(session::pdl::writeBank(bank, file).wasOk());

    auto single = session::pdl::readPatch(file);
    REQUIRE(single.patch != nullptr);
    REQUIRE(single.patch->name == "Two");

    file.deleteFile();
}

TEST_CASE("pdl: an out-of-range activeIndex is clamped to 0, not fatal", "[session][pdl][bank]")
{
    const auto file = tempPdl("bank-clamp");

    juce::XmlElement root("PEDALBOARD");
    root.setAttribute("schemaMajor", session::pdl::kSchemaMajor);
    root.setAttribute("schemaMinor", session::pdl::kSchemaMinor);
    auto* bankEl = root.createNewChildElement("BANK");
    bankEl->setAttribute("activeIndex", 7);
    bankEl->addChildElement(session::patchToXml(namedChain("One", 1)).release());
    bankEl->addChildElement(session::patchToXml(namedChain("Two", 2)).release());
    REQUIRE(root.writeTo(file));

    auto loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank != nullptr);
    REQUIRE(loaded.bank->activeIndex == 0);

    file.deleteFile();
}

TEST_CASE("pdl: an invalid patch inside a bank fails the load and names its position", "[session][pdl][bank]")
{
    const auto file = tempPdl("bank-invalid");

    auto bad = namedChain("Broken", 2);
    bad.connections.push_back({2, 0, 99, 0}); // dangling: node 99 does not exist

    juce::XmlElement root("PEDALBOARD");
    root.setAttribute("schemaMajor", session::pdl::kSchemaMajor);
    root.setAttribute("schemaMinor", session::pdl::kSchemaMinor);
    auto* bankEl = root.createNewChildElement("BANK");
    bankEl->setAttribute("activeIndex", 0);
    bankEl->addChildElement(session::patchToXml(namedChain("One", 1)).release());
    bankEl->addChildElement(session::patchToXml(bad).release());
    REQUIRE(root.writeTo(file));

    auto loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank == nullptr);
    REQUIRE(loaded.error.contains("Patch 2"));
    REQUIRE(loaded.error.contains("Broken"));

    file.deleteFile();
}

TEST_CASE("pdl: an empty <BANK> is rejected", "[session][pdl][bank]")
{
    const auto file = tempPdl("bank-empty");

    juce::XmlElement root("PEDALBOARD");
    root.setAttribute("schemaMajor", session::pdl::kSchemaMajor);
    root.setAttribute("schemaMinor", session::pdl::kSchemaMinor);
    root.createNewChildElement("BANK");
    REQUIRE(root.writeTo(file));

    auto loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank == nullptr);
    REQUIRE(loaded.error.isNotEmpty());

    file.deleteFile();
}

// ---- M5 U1 / U2 and the board's feature switches ------------------------------

TEST_CASE("pdl: a board's feature switches round-trip through the file", "[session][pdl][features]")
{
    const auto file = tempPdl("features");
    session::PatchBank bank;
    bank.patches = {makeChain()};
    bank.setFeature(session::Feature::Looper, false);
    bank.setFeature(session::Feature::Tempo, false);
    REQUIRE(session::pdl::writeBank(bank, file).wasOk());

    const auto loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank != nullptr);
    for (const auto feature : session::kAllFeatures)
        REQUIRE(loaded.bank->featureOn(feature) ==
                (feature != session::Feature::Looper && feature != session::Feature::Tempo));
    file.deleteFile();
}

TEST_CASE("pdl: a board that declares an entity is refused, and the planted file is never read",
          "[session][pdl][security]")
{
    const auto secret =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pb3_secret", ".txt");
    REQUIRE(secret.replaceWithText("PB3-PLANTED-SECRET-MARKER"));
    const auto file = tempPdl("xxe");
    file.replaceWithText("<?xml version=\"1.0\"?><!DOCTYPE PEDALBOARD [<!ENTITY x SYSTEM \"" +
                         secret.getFullPathName() +
                         "\">]><PEDALBOARD schemaMajor=\"3\" schemaMinor=\"7\">"
                         "<BANK><PATCH name=\"&x;\"><NODES/><CONNECTIONS/></PATCH></BANK></PEDALBOARD>");

    const auto loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank == nullptr);
    REQUIRE(loaded.error.containsIgnoreCase("DTD"));
    REQUIRE_FALSE(loaded.error.contains("PB3-PLANTED-SECRET-MARKER"));
    file.deleteFile();
    secret.deleteFile();
}

TEST_CASE("pdl: a truncated or damaged board gives a clear error", "[session][pdl][security]")
{
    const auto file = tempPdl("truncated");
    REQUIRE(session::pdl::writePatch(makeChain(), file).wasOk());
    const auto whole = file.loadFileAsString();
    file.replaceWithText(whole.substring(0, whole.length() / 2));
    auto loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank == nullptr);
    REQUIRE(loaded.error.isNotEmpty());

    const char garbage[] = {'\x7f', '\x00', '\x13', '\x37', 'P', 'K'};
    file.replaceWithData(garbage, sizeof(garbage));
    loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank == nullptr);
    REQUIRE(loaded.error.isNotEmpty());
    file.deleteFile();
}

TEST_CASE("pdl: a patch over the plugin limit is refused, naming the limit", "[session][pdl][security]")
{
    const auto file = tempPdl("toomany");
    juce::String xml = "<PEDALBOARD schemaMajor=\"3\" schemaMinor=\"7\"><BANK><PATCH name=\"Big\"><NODES>";
    for (int i = 1; i <= session::kMaxNodesPerPatch + 1; ++i)
        xml << "<NODE id=\"" << i << "\" kind=\"internal\" type=\"LevelProcessor\"/>";
    xml << "</NODES><CONNECTIONS/></PATCH></BANK></PEDALBOARD>";
    file.replaceWithText(xml);

    const auto loaded = session::pdl::readBank(file);
    REQUIRE(loaded.bank == nullptr);
    REQUIRE(loaded.error.contains(juce::String(session::kMaxNodesPerPatch)));
    file.deleteFile();
}

TEST_CASE("pdl: a plugin's saved settings over the limit, or damaged, are refused", "[session][pdl][security]")
{
    const auto patchWithState = [](const juce::String& encoded)
    {
        juce::XmlElement patch("PATCH");
        auto* node = patch.createNewChildElement("NODES")->createNewChildElement("NODE");
        node->setAttribute("id", 1);
        node->setAttribute("kind", "internal");
        node->setAttribute("type", "LevelProcessor");
        auto* state = node->createNewChildElement("STATE");
        state->setAttribute("encoding", "base64");
        state->addTextElement(encoded);
        return patch;
    };

    juce::String why;
    const auto huge = juce::String::repeatedString("A", static_cast<int>(session::kMaxStateBytes / 3 * 4 + 1024));
    REQUIRE(session::patchFromXml(patchWithState(huge), &why) == nullptr);
    REQUIRE(why.contains("MB"));

    why.clear();
    REQUIRE(session::patchFromXml(patchWithState("not-base64!!"), &why) == nullptr);
    REQUIRE(why.contains("damaged"));

    juce::MemoryBlock fine("settings", 8);
    REQUIRE(session::patchFromXml(patchWithState(fine.toBase64Encoding())) != nullptr);
}

TEST_CASE("pdl: saving is atomic: a failed save leaves the previous board intact", "[session][pdl][files]")
{
    const auto file = tempPdl("atomic");
    REQUIRE(session::pdl::writePatch(makeChain(), file).wasOk());
    const auto before = file.loadFileAsString();

    session::PatchBank empty;
    empty.patches.clear();
    REQUIRE(session::pdl::writeBank(empty, file).failed()); // refused before writing
    REQUIRE(file.loadFileAsString() == before);
    file.deleteFile();
}

TEST_CASE("pdl: a saved board names its plugins by ID, with no paths (M5 U3)", "[session][pdl][identity]")
{
    const auto file = tempPdl("nopath");
    REQUIRE(session::pdl::writePatch(makeChain(), file).wasOk());
    const auto text = file.loadFileAsString();
    REQUIRE(text.contains("<IDENTITY"));
    REQUIRE_FALSE(text.contains(" file=\""));
    REQUIRE_FALSE(text.contains("/fake/EQ.vst3"));
    file.deleteFile();
}
