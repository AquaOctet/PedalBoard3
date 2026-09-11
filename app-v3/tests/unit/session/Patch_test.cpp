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

session::PatchNode internalNode(session::NodeId id, const juce::String& type)
{
    session::PatchNode n;
    n.id = id;
    n.kind = session::NodeKind::Internal;
    n.internalType = type;
    return n;
}

session::PatchConnection conn(session::NodeId src, int srcCh, session::NodeId dst, int dstCh)
{
    return {src, srcCh, dst, dstCh};
}

control::MidiMapping midiMap(control::MidiSourceKind kind, int channel, int controller)
{
    control::MidiMapping m;
    m.source.kind = kind;
    m.source.channel = channel;
    m.source.controller = controller;
    return m;
}

control::OscMapping oscMap(const juce::String& address)
{
    control::OscMapping m;
    m.address = address;
    return m;
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

TEST_CASE("Patch: MIDI and OSC mappings round-trip with all fields", "[session][patch][xml][mappings]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1, "Comp"));

    auto cc = midiMap(control::MidiSourceKind::ControlChange, 1, 7);
    cc.rangeMin = -0.5f; // out-of-0..1 + reversed exactness
    cc.rangeMax = 2.0f;
    cc.inverted = true;
    cc.latch = true;
    cc.target.kind = control::MidiTargetKind::PluginParameter;
    cc.target.node = 1;
    cc.target.parameterIndex = 12;

    auto note = midiMap(control::MidiSourceKind::Note, 10, 60);
    note.target.kind = control::MidiTargetKind::PatchSelect;
    note.target.patchIndex = 4;

    // controller is ignored-for-kind on ProgramChange but must store verbatim.
    auto pc = midiMap(control::MidiSourceKind::ProgramChange, 0, 42);
    pc.target.kind = control::MidiTargetKind::AppCommand;
    pc.target.commandId = "transport.tap-tempo";

    p.midiMappings = {cc, note, pc};

    auto osc = oscMap("/pb3/track1/gain");
    osc.argumentIndex = 2;
    osc.rangeMin = 0.2f;
    osc.rangeMax = 0.8f;
    osc.inverted = true;
    osc.target.kind = control::MidiTargetKind::AppCommand;
    osc.target.commandId = "a & b < c > \"d\" 'e'/f"; // XML metacharacters must escape + restore
    p.oscMappings = {osc};

    auto xml = session::patchToXml(p);
    REQUIRE(xml != nullptr);
    auto rt = session::patchFromXml(*xml);
    REQUIRE(rt != nullptr);

    REQUIRE(rt->midiMappings == p.midiMappings);
    REQUIRE(rt->oscMappings == p.oscMappings);
    REQUIRE(rt->midiMappings.size() == 3);
    REQUIRE(rt->midiMappings[2].source.controller == 42); // ignored-for-kind stored verbatim
    REQUIRE(session::validatePatch(*rt) == session::PatchValidation::Ok);
}

TEST_CASE("Patch: a patch with no <MAPPINGS> round-trips to empty tables", "[session][patch][xml][mappings]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1, "X"));
    p.connections.push_back(conn(session::graphIO, 0, 1, 0));

    auto xml = session::patchToXml(p);
    REQUIRE(xml->getChildByName("MAPPINGS") == nullptr); // omitted when empty

    auto rt = session::patchFromXml(*xml);
    REQUIRE(rt != nullptr);
    REQUIRE(rt->midiMappings.empty());
    REQUIRE(rt->oscMappings.empty());
    REQUIRE(session::validatePatch(*rt) == session::PatchValidation::Ok);
}

TEST_CASE("Patch: unknown MIDI source kind drops that mapping; garbage child skipped",
          "[session][patch][xml][mappings]")
{
    juce::XmlElement root("PATCH");
    root.setAttribute("name", "X");
    auto* maps = root.createNewChildElement("MAPPINGS");

    auto* good = maps->createNewChildElement("MIDI_MAP");
    good->setAttribute("kind", "cc");
    good->setAttribute("channel", 1);
    good->setAttribute("controller", 7);

    auto* bad = maps->createNewChildElement("MIDI_MAP"); // unknown kind -> dropped
    bad->setAttribute("kind", "future");
    bad->setAttribute("controller", 9);

    maps->createNewChildElement("FOO"); // garbage child -> skipped

    auto rt = session::patchFromXml(root);
    REQUIRE(rt != nullptr);
    REQUIRE(rt->midiMappings.size() == 1);
    REQUIRE(rt->midiMappings[0].source.controller == 7);
}

TEST_CASE("Patch: missing mapping attributes fall back to defaults", "[session][patch][xml][mappings]")
{
    juce::XmlElement root("PATCH");
    auto* maps = root.createNewChildElement("MAPPINGS");
    maps->createNewChildElement("MIDI_MAP")->setAttribute("kind", "cc"); // all else absent, no <TARGET>

    auto rt = session::patchFromXml(root);
    REQUIRE(rt != nullptr);
    REQUIRE(rt->midiMappings.size() == 1);
    // Equals a default mapping (rangeMin 0, rangeMax 1, flags false, default target).
    REQUIRE(rt->midiMappings[0] == midiMap(control::MidiSourceKind::ControlChange, 0, 0));
}

TEST_CASE("Patch: a mapping targeting an absent node still validates", "[session][patch][xml][mappings]")
{
    session::Patch p; // no nodes at all
    auto m = midiMap(control::MidiSourceKind::ControlChange, 1, 7);
    m.target.node = 999; // not present in the (empty) node table
    p.midiMappings = {m};

    auto rt = session::patchFromXml(*session::patchToXml(p));
    REQUIRE(rt != nullptr);
    REQUIRE(rt->midiMappings == p.midiMappings);
    REQUIRE(session::validatePatch(*rt) == session::PatchValidation::Ok);
}

TEST_CASE("Patch: patchFromXml accepts the legacy <ACTIVE_PATCH> tag", "[session][patch][xml]")
{
    session::Patch p;
    p.nodes.push_back(makeNode(1, "Legacy"));
    auto m = midiMap(control::MidiSourceKind::ControlChange, 1, 7);
    m.target.node = 1;
    p.midiMappings = {m};

    auto xml = session::patchToXml(p);
    REQUIRE(xml != nullptr);
    xml->setTagName("ACTIVE_PATCH"); // simulate a pre-Phase-2 Preferences file

    auto rt = session::patchFromXml(*xml);
    REQUIRE(rt != nullptr);
    REQUIRE(rt->nodes.size() == 1);
    REQUIRE(rt->nodes[0].desc.name == "Legacy");
    REQUIRE(rt->midiMappings == p.midiMappings); // mappings ride the legacy tag too
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

// ---- Internal (built-in) node kind --------------------------------------

TEST_CASE("Patch: a default-constructed PatchNode is a Plugin", "[session][patch][internal]")
{
    session::PatchNode n;
    REQUIRE(n.kind == session::NodeKind::Plugin);
    REQUIRE(n.internalType.isEmpty());
}

TEST_CASE("Patch: an internal node round-trips kind + type and skips IDENTITY", "[session][patch][xml][internal]")
{
    session::Patch p;
    p.name = "With Built-ins";

    auto level = internalNode(1, "LevelProcessor");
    level.bypass = true;
    level.customName = "Trim";
    p.nodes.push_back(level);
    p.nodes.push_back(makeNode(2, "Reverb")); // a normal plugin alongside

    p.connections.push_back(conn(session::graphIO, 0, 1, 0));
    p.connections.push_back(conn(1, 0, 2, 0));

    auto xml = session::patchToXml(p);
    REQUIRE(xml != nullptr);

    // The internal node serializes kind/type and omits the plugin IDENTITY.
    auto* nodesEl = xml->getChildByName("NODES");
    REQUIRE(nodesEl != nullptr);
    auto* firstNode = nodesEl->getFirstChildElement();
    REQUIRE(firstNode != nullptr);
    REQUIRE(firstNode->getStringAttribute("kind") == "internal");
    REQUIRE(firstNode->getStringAttribute("type") == "LevelProcessor");
    REQUIRE(firstNode->getChildByName("IDENTITY") == nullptr);

    auto rt = session::patchFromXml(*xml);
    REQUIRE(rt != nullptr);
    REQUIRE(rt->nodes.size() == 2);

    const auto& n0 = rt->nodes[0];
    REQUIRE(n0.id == 1);
    REQUIRE(n0.kind == session::NodeKind::Internal);
    REQUIRE(n0.internalType == "LevelProcessor");
    REQUIRE(n0.bypass);
    REQUIRE(n0.customName == "Trim");

    // The plugin node alongside it stays a Plugin.
    REQUIRE(rt->nodes[1].kind == session::NodeKind::Plugin);
    REQUIRE(rt->nodes[1].desc.name == "Reverb");
}

TEST_CASE("Patch: a node with no kind attribute loads as a Plugin (back-compat)", "[session][patch][xml][internal]")
{
    // A pre-internal-nodes .pdl has <NODE> with no kind attribute; it must
    // continue to load as a plugin node.
    session::Patch p;
    p.nodes.push_back(makeNode(1, "Legacy"));
    auto xml = session::patchToXml(p);

    auto* nodesEl = xml->getChildByName("NODES");
    REQUIRE(nodesEl != nullptr);
    auto* firstNode = nodesEl->getFirstChildElement();
    REQUIRE(firstNode != nullptr);
    REQUIRE_FALSE(firstNode->hasAttribute("kind")); // plugin nodes write no kind attr

    auto rt = session::patchFromXml(*xml);
    REQUIRE(rt != nullptr);
    REQUIRE(rt->nodes.size() == 1);
    REQUIRE(rt->nodes[0].kind == session::NodeKind::Plugin);
    REQUIRE(rt->nodes[0].desc.name == "Legacy");
}

TEST_CASE("Patch: patchIdentityString distinguishes internal node types", "[session][patch][internal]")
{
    session::Patch level;
    level.nodes.push_back(internalNode(1, "LevelProcessor"));

    session::Patch vu;
    vu.nodes.push_back(internalNode(1, "VuMeterProcessor"));

    // Same id, different built-in type -> different identity.
    REQUIRE(session::patchIdentityString(level) != session::patchIdentityString(vu));

    // Identity is type-keyed, not id-keyed: a different id but the same type matches.
    session::Patch levelOtherId;
    levelOtherId.nodes.push_back(internalNode(7, "LevelProcessor"));
    REQUIRE(session::patchIdentityString(level) == session::patchIdentityString(levelOtherId));

    // An internal node and a plugin node never collide.
    session::Patch plugin;
    plugin.nodes.push_back(makeNode(1, "LevelProcessor")); // a plugin literally named "LevelProcessor"
    REQUIRE(session::patchIdentityString(level) != session::patchIdentityString(plugin));
}

// ---- Canvas node positions (interactive editor) --------------------------

TEST_CASE("Patch: a default-constructed node is unpositioned", "[session][patch][position]")
{
    session::PatchNode n;
    REQUIRE_FALSE(n.hasPosition());
}

TEST_CASE("Patch: node x/y round-trips through XML when set", "[session][patch][xml][position]")
{
    session::Patch p;
    auto a = makeNode(1, "Comp");
    a.x = 120.5f;
    a.y = 48.0f;
    p.nodes.push_back(a);
    p.nodes.push_back(makeNode(2, "Reverb")); // left unpositioned

    // Hold the unique_ptr in a local — getChildByName returns a raw pointer into
    // the tree, which a temporary patchToXml(p) would free at the semicolon.
    auto xml = session::patchToXml(p);
    auto* nodesEl = xml->getChildByName("NODES");
    REQUIRE(nodesEl != nullptr);
    REQUIRE(nodesEl->getFirstChildElement()->hasAttribute("x"));
    // The unpositioned node writes no x/y.
    REQUIRE_FALSE(nodesEl->getChildElement(1)->hasAttribute("x"));

    auto rt = session::patchFromXml(*xml);
    REQUIRE(rt != nullptr);
    REQUIRE(rt->nodes[0].hasPosition());
    REQUIRE(rt->nodes[0].x == 120.5f);
    REQUIRE(rt->nodes[0].y == 48.0f);
    REQUIRE_FALSE(rt->nodes[1].hasPosition()); // stays auto-layout
}

TEST_CASE("Patch: a node with no x/y attributes loads unpositioned (back-compat)", "[session][patch][xml][position]")
{
    // A pre-position .pdl has <NODE> with no x/y; it must read back unpositioned
    // so SignalCanvas auto-lays-out rather than stacking nodes at (0,0).
    session::Patch p;
    p.nodes.push_back(makeNode(1, "Legacy"));
    auto rt = session::patchFromXml(*session::patchToXml(p));
    REQUIRE(rt != nullptr);
    REQUIRE_FALSE(rt->nodes[0].hasPosition());

    // A node with only x (malformed) is also treated as unpositioned.
    auto xml = session::patchToXml(p);
    xml->getChildByName("NODES")->getFirstChildElement()->setAttribute("x", 10.0);
    auto rt2 = session::patchFromXml(*xml);
    REQUIRE_FALSE(rt2->nodes[0].hasPosition());
}

TEST_CASE("Patch: a node's MIDI channel filter round-trips; omni writes nothing; out of range clamps",
          "[session][patch][xml][midi-channel]")
{
    session::Patch p;
    auto filtered = makeNode(1, "Filtered");
    filtered.midiChannel = 10;
    p.nodes = {filtered, makeNode(2, "Omni")};

    auto xml = session::patchToXml(p);
    REQUIRE(xml != nullptr);
    auto* nodes = xml->getChildByName("NODES");
    REQUIRE(nodes != nullptr);
    REQUIRE(nodes->getChildElement(0)->getIntAttribute("midiChannel") == 10);
    REQUIRE_FALSE(nodes->getChildElement(1)->hasAttribute("midiChannel"));

    auto back = session::patchFromXml(*xml);
    REQUIRE(back != nullptr);
    REQUIRE(back->nodes[0].midiChannel == 10);
    REQUIRE(back->nodes[1].midiChannel == 0);

    nodes->getChildElement(0)->setAttribute("midiChannel", 99);
    REQUIRE(session::patchFromXml(*xml)->nodes[0].midiChannel == 16);
}
