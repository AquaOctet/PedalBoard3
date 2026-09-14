// Tests for pedalboard3::session::buildPreparedGraph -- the worker-thread-
// safe builder extracted from PluginSlot::load in Phase 1.D.2.
//
// We can't unit-test "build a real VST3 graph" headlessly: every JUCE
// plugin loader expects an actual binary on disk. These tests cover:
//   - a plugin that fails to load becomes a pass-through placeholder (M5 U3)
//   - PreparedGraph value-type defaults are sane and movable
// Building a real plugin's graph (and asserting the prepared graph
// reports a non-zero sample rate) is a manual macOS test.

#include <catch2/catch_test_macros.hpp>

#include <catch2/catch_approx.hpp>

#include "engine/BypassableInstance.h"
#include "engine/PluginHost.h"
#include "engine/processors/MissingPluginProcessor.h"
#include "session/PreparedGraph.h"
#include "support/TestGainProcessor.h"

#include <stdexcept>

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

session::PatchNode pluginNode(session::NodeId id, const juce::String& name)
{
    session::PatchNode n;
    n.id = id;
    n.desc.name = name;
    n.desc.pluginFormatName = "VST3";
    n.desc.manufacturerName = "Pedalboard 3 Tests";
    n.desc.uniqueId = name.hashCode();
    return n;
}

// A plugin as the scanner lists it: the same identity, plus where it lives.
juce::PluginDescription scanned(const juce::String& name, const juce::String& path)
{
    auto d = pluginNode(0, name).desc;
    d.fileOrIdentifier = path;
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
    return d;
}

// Render one block of DC through the whole graph; the level that comes out.
float renderDc(session::PreparedGraph& pg, float level)
{
    juce::AudioBuffer<float> buf(2, 256);
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < 256; ++n)
            buf.setSample(ch, n, level);
    juce::MidiBuffer midi;
    pg.graph->processBlock(buf, midi);
    return buf.getSample(0, 128);
}

// A plugin that throws on its saved state (a stale or foreign blob).
class ThrowingStateProcessor final : public testing::TestGainProcessor
{
public:
    void setStateInformation(const void*, int) override { throw std::runtime_error("bad state"); }
};
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

TEST_CASE("buildPreparedGraph: a plugin that fails to load becomes a pass-through placeholder, and the rest builds",
          "[session][prepared-graph][missing]")
{
    engine::PluginHost host;
    session::Patch patch;
    patch.nodes.push_back(internalNode(1, "LevelProcessor"));
    auto gone = pluginNode(2, "First");
    gone.desc.fileOrIdentifier = "/nope/First.vst3";
    gone.state.append("saved-bytes", 11);
    patch.nodes.push_back(gone);
    patch.nodes.push_back(internalNode(3, "LevelProcessor"));
    patch.connections = session::linearChainConnections(patch.nodes);

    juce::String error;
    auto pg = session::buildPreparedGraph(host, patch, 48000.0, 256, error);
    REQUIRE(pg.isValid());
    REQUIRE(pg.nodes.size() == 3);
    REQUIRE(pg.missing.size() == 1);
    REQUIRE(pg.missing[2].startsWith("Couldn't load"));

    auto* placeholder = dynamic_cast<engine::processors::MissingPluginProcessor*>(pg.nodes[2].hosted);
    REQUIRE(placeholder != nullptr);
    REQUIRE(placeholder->getName() == "First");
    juce::MemoryBlock state;
    placeholder->getStateInformation(state);
    REQUIRE(state == gone.state);

    // DC through Level (unity), the placeholder and Level again comes out unchanged.
    REQUIRE(renderDc(pg, 0.25f) == Approx(0.25f));
}

TEST_CASE("buildPreparedGraph: a node already known to be missing is never loaded",
          "[session][prepared-graph][missing]")
{
    engine::PluginHost host;
    session::Patch patch;
    auto gone = pluginNode(1, "Gone");
    gone.desc.fileOrIdentifier = "/nope/Gone.vst3";
    gone.missingReason = session::missingreason::notFound();
    patch.nodes.push_back(gone);
    patch.connections = session::linearChainConnections(patch.nodes);

    juce::String error;
    auto pg = session::buildPreparedGraph(host, patch, 48000.0, 256, error);
    REQUIRE(pg.isValid());
    // Its own reason, not a load error: createInstance never ran.
    REQUIRE(pg.missing[1] == session::missingreason::notFound());
    REQUIRE(renderDc(pg, 0.5f) == Approx(0.5f));
}

TEST_CASE("pluginOrPlaceholder: a plugin that throws on its saved state becomes a placeholder that keeps it",
          "[session][prepared-graph][missing]")
{
    auto node = pluginNode(1, "Fragile");
    node.state.append("foreign-blob", 12);

    auto built = session::pluginOrPlaceholder(std::make_unique<ThrowingStateProcessor>(), {}, node);
    REQUIRE(built.missingReason == session::missingreason::settingsFailed());
    auto* placeholder = dynamic_cast<engine::processors::MissingPluginProcessor*>(built.processor.get());
    REQUIRE(placeholder != nullptr);
    juce::MemoryBlock state;
    placeholder->getStateInformation(state);
    REQUIRE(state == node.state);

    // One that takes its state is used as it is.
    auto fine = session::pluginOrPlaceholder(std::make_unique<testing::TestGainProcessor>(), {}, node);
    REQUIRE(fine.missingReason.isEmpty());
    REQUIRE(dynamic_cast<testing::TestGainProcessor*>(fine.processor.get()) != nullptr);
}

TEST_CASE("resolvePluginNodes: finds plugins by ID, and marks the ones it can't use as missing",
          "[session][prepared-graph][missing]")
{
    engine::PluginHost host;
    auto& list = host.getKnownPluginList();
    list.addType(scanned("EQ", "/scanned/EQ.vst3"));
    list.addType(scanned("Crashy", "/scanned/Crashy.vst3"));
    list.addToBlacklist("/scanned/Crashy.vst3");

    auto synth = pluginNode(3, "Synth");
    synth.desc.isInstrument = true;
    session::PatchBank bank;
    bank.patches[0].nodes = {
        pluginNode(1, "EQ"), pluginNode(2, "Gone"), synth, pluginNode(4, "Crashy"), internalNode(5, "LevelProcessor")};
    bank.patches[0].connections = session::linearChainConnections(bank.patches[0].nodes);

    REQUIRE(session::resolvePluginNodes(bank, host) == 3);
    const auto& nodes = bank.patches[0].nodes;
    REQUIRE(nodes[0].missingReason.isEmpty());
    REQUIRE(nodes[0].desc.fileOrIdentifier == "/scanned/EQ.vst3"); // found by ID, loaded from the scan
    REQUIRE(nodes[1].missingReason == session::missingreason::notFound());
    REQUIRE(nodes[2].missingReason == session::missingreason::instrument());
    REQUIRE(nodes[3].missingReason == session::missingreason::blocked());

    // The placeholders stay in the board: nothing is dropped.
    REQUIRE(session::dropUnsafeNodes(bank, host) == 0);
    REQUIRE(bank.patches[0].nodes.size() == 5);
}

TEST_CASE("resolvePluginNodes: an older board's file= finds its plugin only through the scanned list",
          "[session][prepared-graph][missing]")
{
    const auto xml = juce::parseXML("<PATCH name=\"Old\"><NODES><NODE id=\"1\">"
                                    "<IDENTITY format=\"VST3\" name=\"EQ\" manufacturer=\"Pedalboard 3 Tests\""
                                    " file=\"/old/place/EQ.vst3\" uniqueId=\"" +
                                    juce::String::toHexString(juce::String("EQ").hashCode()) +
                                    "\"/></NODE></NODES><CONNECTIONS/></PATCH>");
    REQUIRE(xml != nullptr);
    const auto patch = session::patchFromXml(*xml);
    REQUIRE(patch != nullptr);
    REQUIRE(patch->nodes[0].fileHint == "/old/place/EQ.vst3");
    REQUIRE(patch->nodes[0].desc.fileOrIdentifier.isEmpty()); // the old path is never loaded

    session::PatchBank bank;
    bank.patches[0] = *patch;

    // Not scanned: missing, although the old path is right there.
    engine::PluginHost host;
    REQUIRE(session::resolvePluginNodes(bank, host) == 1);

    // Scanned, wherever it lives now: found.
    host.getKnownPluginList().addType(scanned("EQ", "/scanned/EQ.vst3"));
    REQUIRE(session::resolvePluginNodes(bank, host) == 0);
    REQUIRE(bank.patches[0].nodes[0].desc.fileOrIdentifier == "/scanned/EQ.vst3");
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

    // A placeholder loads nothing, so it's always safe to keep (M5 U3).
    auto placeholder = pluginNode(7, "Gone");
    placeholder.missingReason = session::missingreason::notFound();
    REQUIRE(session::isNodeSafeForChain(host, placeholder));
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

TEST_CASE("PreparedGraph: a plugin a later scan finds stops counting as unresolved, with the scanned file",
          "[session][prepared][missing]")
{
    engine::PluginHost host;

    session::PatchBank bank;
    session::PatchNode node;
    node.id = 1;
    node.desc.pluginFormatName = "VST3";
    node.desc.name = "Rescan EQ";
    node.desc.manufacturerName = "Pedalboard 3 Tests";
    node.desc.uniqueId = 0x1234abcd;
    bank.patches[0].nodes.push_back(node);

    // A board's plugin that isn't in the scanned list: a placeholder, unresolved.
    REQUIRE(session::unresolvedNodeCount(bank) == 1);
    REQUIRE(session::resolvePluginNodes(bank, host) == 1);
    REQUIRE(session::unresolvedNodeCount(bank) == 1);

    // A scan adds it: the same board now finds it, with the scanned file.
    auto scanned = node.desc;
    scanned.fileOrIdentifier = "/scanned/Rescan EQ.vst3";
    host.getKnownPluginList().addType(scanned);
    REQUIRE(session::resolvePluginNodes(bank, host) == 0);
    REQUIRE(session::unresolvedNodeCount(bank) == 0);
    REQUIRE(bank.patches[0].nodes[0].desc.fileOrIdentifier == "/scanned/Rescan EQ.vst3");
    REQUIRE(bank.patches[0].nodes[0].missingReason.isEmpty());
}
