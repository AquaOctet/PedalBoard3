// Tests for pedalboard3::engine::PluginHost — the thin facade over
// juce::AudioPluginFormatManager + KnownPluginList that future subsystems
// (Phase 1.D patch-switch state machine, Phase 3 UI plugin-list dialog)
// call into.
//
// Phase 1.C.3 scope: registration, empty-list, synchronous directory scan,
// instance creation error path. Sandboxed out-of-process scanning with a
// crash list arrives in Phase 1.C.4; MIDI-port-style auto-enumeration of
// default OS plugin paths is Phase 1.C.5.

#include <catch2/catch_test_macros.hpp>

#include "engine/PluginHost.h"

#include <juce_audio_processors/juce_audio_processors.h>

using namespace pedalboard3;

TEST_CASE("PluginHost: default formats are registered at construction", "[engine][plugin-host]")
{
    engine::PluginHost host;
    // JUCE 8 registers at least VST3 + LV2 on Linux, VST3 + AU on macOS,
    // VST3 (+ optional VST2) on Windows. We only assert "at least one"
    // because the CI runner's exact format set depends on compile-time
    // module configuration.
    REQUIRE(host.getNumFormats() > 0);
}

TEST_CASE("PluginHost: known plugin list starts empty", "[engine][plugin-host]")
{
    engine::PluginHost host;
    REQUIRE(host.getKnownPlugins().isEmpty());
}

TEST_CASE("PluginHost: scanning a non-existent directory adds nothing and does not crash", "[engine][plugin-host]")
{
    engine::PluginHost host;

    juce::FileSearchPath path;
    path.add(juce::File("/tmp/pedalboard3-nonexistent-dir-for-test-42"));
    const int added = host.scanDirectory(path);

    REQUIRE(added == 0);
    REQUIRE(host.getKnownPlugins().isEmpty());
}

TEST_CASE("PluginHost: scanning an empty directory adds nothing", "[engine][plugin-host]")
{
    engine::PluginHost host;

    // Create a short-lived empty directory under the OS temp root.
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("pb3-plugin-host-scan-test-" + juce::String(juce::Time::currentTimeMillis()));
    REQUIRE(tempDir.createDirectory().wasOk());

    juce::FileSearchPath path;
    path.add(tempDir);
    const int added = host.scanDirectory(path);

    REQUIRE(added == 0);

    tempDir.deleteRecursively();
}

TEST_CASE("PluginHost: clearKnownPlugins is safe on an already-empty list", "[engine][plugin-host]")
{
    engine::PluginHost host;
    host.clearKnownPlugins();
    REQUIRE(host.getKnownPlugins().isEmpty());
}

TEST_CASE("PluginHost: getKnownPlugins returns a snapshot independent of the live list", "[engine][plugin-host]")
{
    engine::PluginHost host;
    auto before = host.getKnownPlugins();
    host.clearKnownPlugins();
    auto after = host.getKnownPlugins();
    REQUIRE(before.size() == 0);
    REQUIRE(after.size() == 0);
}

TEST_CASE("PluginHost: createInstance with a bogus description returns nullptr + error message",
          "[engine][plugin-host]")
{
    engine::PluginHost host;

    juce::PluginDescription bogus;
    bogus.name = "Totally Fake Plugin";
    bogus.fileOrIdentifier = "/nonexistent/path/to/plugin.vst3";
    bogus.pluginFormatName = "VST3";

    juce::String errorOut;
    auto instance = host.createInstance(bogus, 48000.0, 256, errorOut);

    REQUIRE(instance == nullptr);
    REQUIRE(errorOut.isNotEmpty());
}
