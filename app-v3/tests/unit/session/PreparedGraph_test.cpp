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

#include "engine/PluginHost.h"
#include "session/PreparedGraph.h"

using namespace pedalboard3;

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
