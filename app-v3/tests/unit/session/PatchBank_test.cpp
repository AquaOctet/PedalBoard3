// Tests for pedalboard3::session::PatchBank — the ordered multi-patch document
// (roadmap M3 U1). Pure value semantics: list + active index invariants only;
// no Session, no engine.

#include <catch2/catch_test_macros.hpp>

#include "session/PatchBank.h"

using namespace pedalboard3;

namespace
{

session::Patch namedPatch(const juce::String& name, int nodeCount = 0)
{
    session::Patch p;
    p.name = name;
    for (int i = 0; i < nodeCount; ++i)
    {
        session::PatchNode n;
        n.id = static_cast<session::NodeId>(i + 1);
        n.desc.name = name + juce::String(i);
        n.desc.pluginFormatName = "VST3";
        n.desc.fileOrIdentifier = "/fake/" + name + ".vst3";
        n.state.append("abc", 3);
        p.nodes.push_back(n);
    }
    p.connections = session::linearChainConnections(p.nodes);
    control::MidiMapping m;
    m.source.kind = control::MidiSourceKind::ControlChange;
    m.source.channel = 1;
    m.source.controller = 7;
    p.midiMappings.push_back(m);
    return p;
}

session::PatchBank threePatchBank()
{
    session::PatchBank b;
    b.patches = {namedPatch("A"), namedPatch("B"), namedPatch("C")};
    b.activeIndex = 0;
    return b;
}

} // namespace

TEST_CASE("PatchBank: default holds one Untitled patch at index 0", "[session][bank]")
{
    session::PatchBank b;
    REQUIRE(b.size() == 1);
    REQUIRE(b.activeIndex == 0);
    REQUIRE(b.active().name == "Untitled");
    REQUIRE(b.active().nodes.empty());
}

TEST_CASE("PatchBank: add appends and leaves the active index alone", "[session][bank]")
{
    auto b = threePatchBank();
    b.select(1);
    const int idx = b.add(namedPatch("D"));
    REQUIRE(idx == 3);
    REQUIRE(b.size() == 4);
    REQUIRE(b.activeIndex == 1);
    REQUIRE(b.patches[3].name == "D");
}

TEST_CASE("PatchBank: select validates the index", "[session][bank]")
{
    auto b = threePatchBank();
    REQUIRE(b.select(2));
    REQUIRE(b.activeIndex == 2);
    REQUIRE_FALSE(b.select(3));
    REQUIRE_FALSE(b.select(-1));
    REQUIRE(b.activeIndex == 2);
}

TEST_CASE("PatchBank: next and previous wrap around", "[session][bank]")
{
    auto b = threePatchBank();
    b.next();
    REQUIRE(b.activeIndex == 1);
    b.next();
    b.next();
    REQUIRE(b.activeIndex == 0);
    b.previous();
    REQUIRE(b.activeIndex == 2);

    session::PatchBank single;
    single.next();
    single.previous();
    REQUIRE(single.activeIndex == 0);
}

TEST_CASE("PatchBank: duplicate is a deep copy inserted after the source", "[session][bank]")
{
    auto b = threePatchBank();
    b.patches[0] = namedPatch("A", 2);
    const int idx = b.duplicate(0);
    REQUIRE(idx == 1);
    REQUIRE(b.size() == 4);
    REQUIRE(b.patches[1].name == "A copy");
    REQUIRE(b.patches[1].nodes.size() == 2);
    REQUIRE(b.patches[1].connections == b.patches[0].connections);
    REQUIRE(b.patches[1].midiMappings.size() == 1);
    REQUIRE(b.patches[1].nodes[0].state.getSize() == 3);

    // Mutating the copy leaves the original untouched.
    b.patches[1].nodes.clear();
    b.patches[1].name = "changed";
    REQUIRE(b.patches[0].nodes.size() == 2);
    REQUIRE(b.patches[0].name == "A");

    REQUIRE(b.duplicate(99) == -1);
}

TEST_CASE("PatchBank: duplicate before the active patch keeps it active", "[session][bank]")
{
    auto b = threePatchBank();
    b.select(2); // "C"
    b.duplicate(0);
    REQUIRE(b.activeIndex == 3);
    REQUIRE(b.active().name == "C");

    b.duplicate(3); // copy of the active one, inserted after it
    REQUIRE(b.activeIndex == 3);
    REQUIRE(b.active().name == "C");
}

TEST_CASE("PatchBank: rename trims and never leaves an empty name", "[session][bank]")
{
    auto b = threePatchBank();
    REQUIRE(b.rename(1, "  Lead  "));
    REQUIRE(b.patches[1].name == "Lead");
    REQUIRE(b.rename(1, "   "));
    REQUIRE(b.patches[1].name == "Untitled");
    REQUIRE_FALSE(b.rename(5, "x"));
}

TEST_CASE("PatchBank: remove keeps the active index pointing at the same patch", "[session][bank]")
{
    auto b = threePatchBank();
    b.select(2); // "C"
    REQUIRE(b.remove(0));
    REQUIRE(b.size() == 2);
    REQUIRE(b.activeIndex == 1);
    REQUIRE(b.active().name == "C");
}

TEST_CASE("PatchBank: removing the active patch activates its neighbour", "[session][bank]")
{
    auto b = threePatchBank();
    b.select(1); // "B"
    REQUIRE(b.remove(1));
    REQUIRE(b.activeIndex == 1);
    REQUIRE(b.active().name == "C");

    // Removing the active last patch falls back to the new last one.
    REQUIRE(b.remove(1));
    REQUIRE(b.activeIndex == 0);
    REQUIRE(b.active().name == "A");
}

TEST_CASE("PatchBank: removing the only patch resets to one Untitled patch", "[session][bank]")
{
    session::PatchBank b;
    b.patches = {namedPatch("Solo", 3)};
    REQUIRE(b.remove(0));
    REQUIRE(b.size() == 1);
    REQUIRE(b.activeIndex == 0);
    REQUIRE(b.active().name == "Untitled");
    REQUIRE(b.active().nodes.empty());
    REQUIRE_FALSE(b.remove(1));
}
