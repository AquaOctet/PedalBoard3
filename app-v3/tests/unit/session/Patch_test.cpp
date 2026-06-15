// Tests for pedalboard3::session::Patch -- the pure-data topology record
// introduced in Phase 1.D.3. No engine dependencies; these are
// value-semantic tests over node-id allocation, connection structure,
// and the validate-on-restore guard.

#include <catch2/catch_test_macros.hpp>

#include "session/Patch.h"

#include <algorithm>
#include <vector>

using namespace pedalboard3;

namespace
{

session::PatchNode makeNode(session::NodeId id, const juce::String& name = "Plugin")
{
    session::PatchNode n;
    n.id = id;
    n.desc.name = name;
    n.desc.pluginFormatName = "VST3";
    n.desc.fileOrIdentifier = "/fake/" + name + ".vst3";
    return n;
}

session::PatchConnection conn(session::NodeId src, int srcCh, session::NodeId dst, int dstCh)
{
    return {src, srcCh, dst, dstCh};
}

} // namespace

TEST_CASE("Patch: default-constructed is empty", "[session][patch]")
{
    session::Patch p;
    REQUIRE(p.nodes.empty());
    REQUIRE(p.connections.empty());
    REQUIRE(p.name == "Untitled");
    REQUIRE(p.nextNodeId() == 1);
}

TEST_CASE("Patch: nextNodeId returns max+1 even after deletes", "[session][patch]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.nodes.push_back(makeNode(2));
    p.nodes.push_back(makeNode(5));
    REQUIRE(p.nextNodeId() == 6);

    // Erase the middle one -- nextNodeId should still be 6 (max+1, no reuse of 2).
    p.nodes.erase(p.nodes.begin() + 1);
    REQUIRE(p.nextNodeId() == 6);

    // Erase the highest -- nextNodeId now drops to 2.
    p.nodes.erase(p.nodes.begin() + 1); // was id 5
    REQUIRE(p.nextNodeId() == 2);
}

TEST_CASE("Patch: findNode + indexOfNode", "[session][patch]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1, "A"));
    p.nodes.push_back(makeNode(2, "B"));

    REQUIRE(p.findNode(1) != nullptr);
    REQUIRE(p.findNode(1)->desc.name == "A");
    REQUIRE(p.findNode(2)->desc.name == "B");
    REQUIRE(p.findNode(99) == nullptr);

    REQUIRE(p.indexOfNode(1) == 0);
    REQUIRE(p.indexOfNode(2) == 1);
    REQUIRE(p.indexOfNode(99) == -1);
}

TEST_CASE("Patch: PatchConnection equality", "[session][patch]")
{
    auto c1 = conn(1, 0, 2, 0);
    auto c2 = conn(1, 0, 2, 0);
    auto c3 = conn(1, 0, 2, 1);
    REQUIRE(c1 == c2);
    REQUIRE_FALSE(c1 == c3);
}

TEST_CASE("Patch: validatePatch -- empty patch is OK", "[session][patch][validate]")
{
    session::Patch p;
    REQUIRE(session::validatePatch(p) == session::PatchValidation::Ok);
}

TEST_CASE("Patch: validatePatch -- well-formed linear chain is OK", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.nodes.push_back(makeNode(2));
    // graphIO -> 1 -> 2 -> graphIO (stereo audio)
    p.connections.push_back(conn(session::graphIO, 0, 1, 0));
    p.connections.push_back(conn(session::graphIO, 1, 1, 1));
    p.connections.push_back(conn(1, 0, 2, 0));
    p.connections.push_back(conn(1, 1, 2, 1));
    p.connections.push_back(conn(2, 0, session::graphIO, 0));
    p.connections.push_back(conn(2, 1, session::graphIO, 1));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::Ok);
}

TEST_CASE("Patch: validatePatch -- duplicate NodeId rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.nodes.push_back(makeNode(1));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::DuplicateNodeId);
}

TEST_CASE("Patch: validatePatch -- graphIO as a node id rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(session::graphIO));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::DuplicateNodeId);
}

TEST_CASE("Patch: validatePatch -- dangling connection (src) rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.connections.push_back(conn(99, 0, 1, 0));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::DanglingConnection);
}

TEST_CASE("Patch: validatePatch -- dangling connection (dst) rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.connections.push_back(conn(1, 0, 99, 0));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::DanglingConnection);
}

TEST_CASE("Patch: validatePatch -- negative channel index rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.connections.push_back(conn(session::graphIO, -1, 1, 0));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::InvalidChannel);
}

TEST_CASE("Patch: validatePatch -- cycle rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.nodes.push_back(makeNode(2));
    p.nodes.push_back(makeNode(3));
    // 1 -> 2 -> 3 -> 1 (audio cycle)
    p.connections.push_back(conn(1, 0, 2, 0));
    p.connections.push_back(conn(2, 0, 3, 0));
    p.connections.push_back(conn(3, 0, 1, 0));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::Cycle);
}

TEST_CASE("Patch: validatePatch -- graphIO -> graphIO is OK (passthrough)", "[session][patch][validate]")
{
    session::Patch p;
    p.connections.push_back(conn(session::graphIO, 0, session::graphIO, 0));
    p.connections.push_back(conn(session::graphIO, 1, session::graphIO, 1));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::Ok);
}

TEST_CASE("Patch: patchIdentityString is order-sensitive + stable", "[session][patch]")
{
    session::Patch p1;
    p1.nodes.push_back(makeNode(1, "A"));
    p1.nodes.push_back(makeNode(2, "B"));

    session::Patch p2;
    p2.nodes.push_back(makeNode(99, "A")); // different ids
    p2.nodes.push_back(makeNode(100, "B"));

    // Same identity string regardless of NodeId values (identity is by
    // pluginFormatName + fileOrIdentifier).
    REQUIRE(session::patchIdentityString(p1) == session::patchIdentityString(p2));

    session::Patch p3;
    p3.nodes.push_back(makeNode(1, "B")); // reordered
    p3.nodes.push_back(makeNode(2, "A"));
    REQUIRE(session::patchIdentityString(p1) != session::patchIdentityString(p3));
}

TEST_CASE("Patch: patchToXml/patchFromXml round-trips nodes, connections, and state", "[session][patch][xml]")
{
    session::Patch p;
    p.name = "My Board";

    auto a = makeNode(1, "Comp");
    a.bypass = true;
    a.customName = "Punch";
    a.state.append("hello-state", 11); // non-empty plugin state blob
    p.nodes.push_back(a);
    p.nodes.push_back(makeNode(2, "Reverb"));

    p.connections.push_back(conn(session::graphIO, 0, 1, 0));
    p.connections.push_back(conn(1, 0, 2, 0));

    auto xml = session::patchToXml(p);
    REQUIRE(xml != nullptr);
    REQUIRE(xml->hasTagName("PATCH"));

    auto rt = session::patchFromXml(*xml);
    REQUIRE(rt != nullptr);
    REQUIRE(rt->name == "My Board");
    REQUIRE(rt->nodes.size() == 2);
    REQUIRE(rt->connections.size() == 2);

    const auto& n0 = rt->nodes[0];
    REQUIRE(n0.id == 1);
    REQUIRE(n0.bypass);
    REQUIRE(n0.customName == "Punch");
    REQUIRE(n0.desc.name == "Comp");
    REQUIRE(n0.state.getSize() == 11);
    REQUIRE(n0.state.toString() == "hello-state");

    // The node that had no state stays empty after the round-trip.
    REQUIRE(rt->nodes[1].state.getSize() == 0);
}

TEST_CASE("Patch: patchFromXml accepts the legacy <ACTIVE_PATCH> tag", "[session][patch][xml]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1, "Legacy"));

    auto xml = session::patchToXml(p);
    REQUIRE(xml != nullptr);
    xml->setTagName("ACTIVE_PATCH"); // simulate a pre-Phase-2 Preferences file

    auto rt = session::patchFromXml(*xml);
    REQUIRE(rt != nullptr);
    REQUIRE(rt->nodes.size() == 1);
    REQUIRE(rt->nodes[0].desc.name == "Legacy");
}

TEST_CASE("Patch: patchFromXml rejects an unrelated element", "[session][patch][xml]")
{
    juce::XmlElement bogus("SOMETHING_ELSE");
    REQUIRE(session::patchFromXml(bogus) == nullptr);
}

TEST_CASE("Patch: linearChainConnections empty -> audio + MIDI passthrough", "[session][patch][connections]")
{
    const auto conns = session::linearChainConnections({});
    // L + R audio passthrough + one MIDI passthrough.
    REQUIRE(conns.size() == 3);

    session::Patch p;
    p.connections = conns;
    REQUIRE(session::validatePatch(p) == session::PatchValidation::Ok);
}

TEST_CASE("Patch: linearChainConnections wires a valid linear chain over survivors", "[session][patch][connections]")
{
    // Mirrors App's restore: after dropping an unsafe node we rebuild the
    // chain over the survivors; the result must validate (no dangling refs).
    std::vector<session::PatchNode> nodes{makeNode(1, "A"), makeNode(2, "B")};
    const auto conns = session::linearChainConnections(nodes);

    session::Patch p;
    p.nodes = nodes;
    p.connections = conns;
    REQUIRE(session::validatePatch(p) == session::PatchValidation::Ok);

    // graphIO -> 1 and 2 -> graphIO must both exist on the stereo channels.
    const auto has = [&](session::NodeId s, int sc, session::NodeId d, int dc)
    {
        return std::any_of(conns.begin(),
                           conns.end(),
                           [&](const session::PatchConnection& c)
                           { return c.src == s && c.srcChannel == sc && c.dst == d && c.dstChannel == dc; });
    };
    REQUIRE(has(session::graphIO, 0, 1, 0)); // input -> first node L
    REQUIRE(has(1, 0, 2, 0));                // first -> second L
    REQUIRE(has(2, 0, session::graphIO, 0)); // last node -> output L
}
