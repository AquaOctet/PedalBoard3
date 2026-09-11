// Tests for pedalboard3::session::buildPreparedGraph -- the worker-thread-
// safe builder extracted from PluginSlot::load in Phase 1.D.2.
//
// We can't unit-test "build a real VST3 graph" headlessly: every JUCE
// plugin loader expects an actual binary on disk. These tests cover:
//   - bogus PluginDescription returns an empty PreparedGraph + error
//   - PreparedGraph value-type defaults are sane and movable
// Building a real plugin's graph (and asserting the prepared graph
// reports a non-zero sample rate) is a manual macOS test.

#include <catch2/catch_test_macros.hpp>

#include <catch2/catch_approx.hpp>

#include "engine/BypassableInstance.h"
#include "engine/PluginHost.h"
#include "session/PreparedGraph.h"

using namespace pedalboard3;
using Catch::Approx;

namespace
{
session::PatchNode internalNode(session::NodeId id, const juce::String& type)
{
    session::PatchNode n;
    n.id = id;
    n.kind = session::NodeKind::Internal;
    n.internalType = type;
    return n;
}
} // namespace

TEST_CASE("PreparedGraph: default-constructed is empty and not valid", "[session][prepared-graph]")
{
    session::PreparedGraph pg;
    REQUIRE(pg.graph == nullptr);
    REQUIRE(pg.nodes.empty());
    REQUIRE_FALSE(pg.isValid());
}

TEST_CASE("PreparedGraph: empty value moves cleanly", "[session][prepared-graph]")
{
    session::PreparedGraph src;
    auto dst = std::move(src);
    REQUIRE_FALSE(dst.isValid());
    // Moved-from is also not valid; this is a structural value type.
    REQUIRE_FALSE(src.isValid());
}

TEST_CASE("buildPreparedGraph: empty Patch builds a passthrough graph", "[session][prepared-graph]")
{
    engine::PluginHost host;
    session::Patch empty;

    juce::String error;
    auto prepared = session::buildPreparedGraph(host, empty, 48000.0, 256, error);
    REQUIRE(prepared.isValid());
    REQUIRE(prepared.nodes.empty());
    REQUIRE(error.isEmpty());

    // Graph should have at least the four IO processor nodes
    // (audioIn, audioOut, midiIn, midiOut). plus zero plugin nodes.
    REQUIRE(prepared.graph->getNumNodes() >= 4);
}

TEST_CASE("buildPreparedGraph: multi-node patch with bogus desc returns empty + node-named error",
          "[session][prepared-graph]")
{
    engine::PluginHost host;
    session::Patch patch;
    session::PatchNode n1;
    n1.id = 1;
    n1.desc.name = "First";
    n1.desc.pluginFormatName = "VST3";
    n1.desc.fileOrIdentifier = "/nope/First.vst3";
    patch.nodes.push_back(n1);

    juce::String error;
    auto prepared = session::buildPreparedGraph(host, patch, 48000.0, 256, error);
    REQUIRE_FALSE(prepared.isValid());
    REQUIRE(prepared.nodes.empty());
    REQUIRE(error.contains("First"));
}

// ---- Internal (built-in) nodes — instantiable headless, unlike plugins ----

TEST_CASE("PreparedGraph: an internal Level node builds and processes audio", "[session][prepared-graph][internal]")
{
    engine::PluginHost host;

    session::Patch patch;
    patch.nodes.push_back(internalNode(1, "LevelProcessor"));
    patch.connections = session::linearChainConnections(patch.nodes);

    juce::String err;
    auto pg = session::buildPreparedGraph(host, patch, 48000.0, 256, err);
    REQUIRE(pg.isValid());
    REQUIRE(err.isEmpty());
    REQUIRE(pg.nodes.count(1) == 1);
    REQUIRE(pg.nodes[1].hosted != nullptr);            // the widened AudioProcessor* is populated
    REQUIRE(pg.nodes[1].hosted->getName() == "Level"); // it is the internal processor

    // Default level 0.5 == unity; set to 1.0 (2x) and render DC 0.25 through the
    // whole graph (audioIn -> Level -> audioOut) -> 0.5.
    pg.nodes[1].hosted->getParameters()[0]->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> buf(2, 256);
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < 256; ++n)
            buf.setSample(ch, n, 0.25f);
    juce::MidiBuffer midi;
    pg.graph->processBlock(buf, midi);

    REQUIRE(buf.getSample(0, 128) == Approx(0.5f));
}

TEST_CASE("PreparedGraph: an unknown internal type aborts the build with an error",
          "[session][prepared-graph][internal]")
{
    engine::PluginHost host;

    session::Patch patch;
    patch.nodes.push_back(internalNode(1, "NoSuchProcessor"));
    patch.connections = session::linearChainConnections(patch.nodes);

    juce::String err;
    auto pg = session::buildPreparedGraph(host, patch, 48000.0, 256, err);
    REQUIRE_FALSE(pg.isValid());
    REQUIRE(err.isNotEmpty());
}

TEST_CASE("PreparedGraph: isNodeSafeForChain accepts known internals, rejects unknown",
          "[session][prepared-graph][internal]")
{
    engine::PluginHost host;

    REQUIRE(session::isNodeSafeForChain(host, internalNode(1, "LevelProcessor")));
    REQUIRE(session::isNodeSafeForChain(host, internalNode(2, "VuMeterProcessor")));
    REQUIRE_FALSE(session::isNodeSafeForChain(host, internalNode(3, "NoSuchProcessor")));

    // An internal node with an empty type token is unrecognized -> dropped. This
    // is the corrupt/hand-edited-.pdl and missing-`type`-attribute case.
    REQUIRE_FALSE(session::isNodeSafeForChain(host, internalNode(4, "")));

    // A real plugin node (non-empty fileOrIdentifier, not an instrument, not
    // blacklisted) is safe to materialize into a chain.
    session::PatchNode plugin;
    plugin.id = 5;
    plugin.desc.name = "Comp";
    plugin.desc.pluginFormatName = "VST3";
    plugin.desc.fileOrIdentifier = "/fake/Comp.vst3";
    REQUIRE(session::isNodeSafeForChain(host, plugin));

    // A Plugin-kind node with an EMPTY descriptor can never be instantiated, so
    // it is unsafe-for-chain -> dropped + chain rebuilt, rather than failing deep
    // in createInstance. This also covers a forward-compat hazard: an internal
    // node written by a newer build, read by an older binary that lacks the kind
    // discriminator, deserializes to exactly this empty-desc plugin node.
    session::PatchNode emptyPlugin;
    emptyPlugin.id = 6;
    REQUIRE_FALSE(session::isNodeSafeForChain(host, emptyPlugin));
}

TEST_CASE("PreparedGraph: a node's MIDI channel filter is applied to its wrapper at build",
          "[session][prepared-graph][midi-channel]")
{
    engine::PluginHost host;

    session::Patch patch;
    auto filtered = internalNode(1, "LevelProcessor");
    filtered.midiChannel = 5;
    patch.nodes.push_back(filtered);
    patch.nodes.push_back(internalNode(2, "LevelProcessor"));
    patch.connections = session::linearChainConnections(patch.nodes);

    juce::String err;
    auto pg = session::buildPreparedGraph(host, patch, 48000.0, 256, err);
    REQUIRE(pg.isValid());
    REQUIRE(pg.nodes[1].bypass != nullptr);
    REQUIRE(pg.nodes[1].bypass->getMidiChannel() == 5);
    REQUIRE(pg.nodes[2].bypass->getMidiChannel() == 0);
}
