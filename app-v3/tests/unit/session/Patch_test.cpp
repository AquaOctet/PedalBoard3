// Tests for pedalboard3::session::Patch — the pure-data topology record
// introduced in Phase 1.D.3. No engine dependencies; these are
// value-semantic tests over node-id allocation, connection structure,
// and the validate-on-restore guard.

#include <catch2/catch_test_macros.hpp>

#include "session/Patch.h"

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

    // Erase the middle one — nextNodeId should still be 6 (max+1, no reuse of 2).
    p.nodes.erase(p.nodes.begin() + 1);
    REQUIRE(p.nextNodeId() == 6);

    // Erase the highest — nextNodeId now drops to 2.
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

TEST_CASE("Patch: validatePatch — empty patch is OK", "[session][patch][validate]")
{
    session::Patch p;
    REQUIRE(session::validatePatch(p) == session::PatchValidation::Ok);
}

TEST_CASE("Patch: validatePatch — well-formed linear chain is OK", "[session][patch][validate]")
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

TEST_CASE("Patch: validatePatch — duplicate NodeId rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.nodes.push_back(makeNode(1));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::DuplicateNodeId);
}

TEST_CASE("Patch: validatePatch — graphIO as a node id rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(session::graphIO));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::DuplicateNodeId);
}

TEST_CASE("Patch: validatePatch — dangling connection (src) rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.connections.push_back(conn(99, 0, 1, 0));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::DanglingConnection);
}

TEST_CASE("Patch: validatePatch — dangling connection (dst) rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.connections.push_back(conn(1, 0, 99, 0));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::DanglingConnection);
}

TEST_CASE("Patch: validatePatch — negative channel index rejected", "[session][patch][validate]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1));
    p.connections.push_back(conn(session::graphIO, -1, 1, 0));
    REQUIRE(session::validatePatch(p) == session::PatchValidation::InvalidChannel);
}

TEST_CASE("Patch: validatePatch — cycle rejected", "[session][patch][validate]")
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

TEST_CASE("Patch: validatePatch — graphIO -> graphIO is OK (passthrough)", "[session][patch][validate]")
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
