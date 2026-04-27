// Tests for pedalboard3::session::buildPreparedGraph — the worker-thread-
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
    REQUIRE(pg.bypass == nullptr);
    REQUIRE(pg.hosted == nullptr);
    REQUIRE(pg.name.isEmpty());
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

TEST_CASE("buildPreparedGraph: bogus desc returns empty PreparedGraph + error", "[session][prepared-graph]")
{
    engine::PluginHost host;
    juce::PluginDescription bogus;
    bogus.name = "Totally Fake Plugin";
    bogus.fileOrIdentifier = "/nonexistent/path/to/plugin.vst3";
    bogus.pluginFormatName = "VST3";

    juce::String error;
    auto prepared = session::buildPreparedGraph(host, bogus, 48000.0, 256, error);
    REQUIRE_FALSE(prepared.isValid());
    REQUIRE(prepared.bypass == nullptr);
    REQUIRE(prepared.hosted == nullptr);
    REQUIRE(error.isNotEmpty());
}
