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
