// Tests for pedalboard3::session::PluginSlot — the single-plugin host
// slot from Phase 1.D.1.
//
// We can't unit-test "load a real VST3" headlessly: every JUCE plugin
// loader expects an actual binary on disk and the host machine. Instead
// these tests cover:
//   - construction is a no-op (no graph mutation, no plugin loaded)
//   - load() with a bogus PluginDescription returns false + error
//   - unload() is a no-op when nothing was loaded
//   - bypass control is a no-op (and reports false) when nothing is loaded
// End-to-end "load a real plugin and hear it" is a manual macOS test.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/PluginSlot.h"

using namespace pedalboard3;

TEST_CASE("PluginSlot: construction does not load a plugin", "[session][plugin-slot]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;

    session::PluginSlot slot(eng, host);
    REQUIRE(!slot.isLoaded());
    REQUIRE(slot.getLoadedName().isEmpty());
    REQUIRE(slot.getHostedPlugin() == nullptr);
}

TEST_CASE("PluginSlot: load() with a bogus description returns false + error message", "[session][plugin-slot]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);

    juce::PluginDescription bogus;
    bogus.name = "Totally Fake Plugin";
    bogus.fileOrIdentifier = "/nonexistent/path/to/plugin.vst3";
    bogus.pluginFormatName = "VST3";

    juce::String error;
    const bool ok = slot.load(bogus, error);
    REQUIRE(!ok);
    REQUIRE(error.isNotEmpty());
    REQUIRE(!slot.isLoaded());
    REQUIRE(slot.getLoadedName().isEmpty());
}

TEST_CASE("PluginSlot: unload() is safe before any load", "[session][plugin-slot]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);

    slot.unload(); // must not crash
    slot.unload(); // idempotent
    REQUIRE(!slot.isLoaded());
}

TEST_CASE("PluginSlot: bypass controls are inert when nothing is loaded", "[session][plugin-slot]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);

    REQUIRE(!slot.isBypassed());
    slot.setBypassed(true);
    REQUIRE(!slot.isBypassed()); // still false because nothing is loaded
    slot.setBypassed(false);
    REQUIRE(!slot.isBypassed());
}

TEST_CASE("PluginSlot: failed load leaves the slot in an empty state", "[session][plugin-slot]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);

    juce::PluginDescription bogus;
    bogus.name = "Bogus";
    bogus.fileOrIdentifier = "/nope/Bogus.vst3";
    bogus.pluginFormatName = "VST3";

    juce::String error;
    slot.load(bogus, error);
    REQUIRE(!slot.isLoaded());

    // A subsequent unload still works.
    slot.unload();
    REQUIRE(!slot.isLoaded());
}
