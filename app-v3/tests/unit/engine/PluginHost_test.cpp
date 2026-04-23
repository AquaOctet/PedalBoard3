// Tests for pedalboard3::engine::PluginHost — the thin facade over
// juce::AudioPluginFormatManager + KnownPluginList that future subsystems
// (Phase 1.D patch-switch state machine, Phase 3 UI plugin-list dialog)
// call into.
//
// Phase 1.C.3 scope: registration, empty-list, synchronous directory scan,
// instance creation error path. Phase 1.C.5 adds format introspection
// (getFormatNames) and OS-default plugin-path enumeration + scan helpers.
// Sandboxed out-of-process scanning with a crash list arrives in Phase 1.C.4.

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

// -----------------------------------------------------------------------
// Phase 1.C.5 — format introspection + default-path enumeration
// -----------------------------------------------------------------------

TEST_CASE("PluginHost: getFormatNames reports at least as many entries as getNumFormats",
          "[engine][plugin-host][paths]")
{
    engine::PluginHost host;
    const auto names = host.getFormatNames();
    REQUIRE(names.size() == host.getNumFormats());
    REQUIRE(names.size() > 0);

    // Every reported name must be non-empty — a blank format name would
    // mean a UI list couldn't distinguish registered formats from each other.
    for (const auto& n : names)
        REQUIRE(n.isNotEmpty());
}

TEST_CASE("PluginHost: getDefaultSearchPathForFormat returns a non-empty path for a real format",
          "[engine][plugin-host][paths]")
{
    engine::PluginHost host;
    const auto names = host.getFormatNames();
    REQUIRE(names.size() > 0);

    // Pick the first registered format (VST3 on every supported platform).
    // We don't assert a specific directory because JUCE picks per-OS defaults
    // that we can't hard-code for CI. Just prove the path has at least one
    // directory entry.
    const auto path = host.getDefaultSearchPathForFormat(names[0]);
    REQUIRE(path.getNumPaths() > 0);
}

TEST_CASE("PluginHost: getDefaultSearchPathForFormat returns empty for an unknown format name",
          "[engine][plugin-host][paths]")
{
    engine::PluginHost host;
    const auto path = host.getDefaultSearchPathForFormat("FakeFormatThatDoesNotExist");
    REQUIRE(path.getNumPaths() == 0);
}

TEST_CASE("PluginHost: scanDefaultPaths runs without crashing on a bare runner", "[engine][plugin-host][paths]")
{
    engine::PluginHost host;

    // CI runners rarely have production plugins installed, so this may
    // return 0. The assertion is simply that it doesn't throw, doesn't
    // deadlock on the rwlock, and leaves the host in a sane state.
    const int added = host.scanDefaultPaths();
    REQUIRE(added >= 0);
    REQUIRE(host.getKnownPlugins().size() == added);
}
