// Tests for pedalboard3::engine::PluginHost -- the thin facade over
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
    // Scope to VST3 only. AU's scanner ignores the FileSearchPath and would
    // enumerate every installed AudioComponent on the runner -- fine on a
    // bare CI runner, fatal on a dev Mac with a buggy AU plugin installed.
    const int added = host.scanDirectory(path, juce::StringArray{"VST3"});

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
    const int added = host.scanDirectory(path, juce::StringArray{"VST3"});

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
// Phase 1.C.5 -- format introspection + default-path enumeration
// -----------------------------------------------------------------------

TEST_CASE("PluginHost: getFormatNames reports at least as many entries as getNumFormats",
          "[engine][plugin-host][paths]")
{
    engine::PluginHost host;
    const auto names = host.getFormatNames();
    REQUIRE(names.size() == host.getNumFormats());
    REQUIRE(names.size() > 0);

    // Every reported name must be non-empty -- a blank format name would
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

    // At least one registered format must expose default search paths so a
    // first-run "scan everywhere" UI has something to walk. JUCE 8's default
    // format order on macOS puts AudioUnit first (juce_AudioPluginFormatManager.cpp),
    // and AU's getDefaultLocationsToSearch() returns {} because AU plugins
    // are registry-based. So we iterate, not assume names[0].
    bool sawNonEmpty = false;
    for (const auto& name : names)
    {
        if (host.getDefaultSearchPathForFormat(name).getNumPaths() > 0)
        {
            sawNonEmpty = true;
            break;
        }
    }
    REQUIRE(sawNonEmpty);
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

    // We can only verify non-crash + sane-state semantics when the
    // filesystem actually IS bare. On a developer machine with installed
    // VST3 / LV2 plugins, JUCE's PluginDirectoryScanner instantiates each
    // plugin to introspect its metadata -- and a single buggy plugin in
    // that set will SIGABRT the test process. The proper fix is Phase 1.C.4
    // (out-of-process sandbox scanning); until then, skip when any default
    // path contains a plugin candidate.
    for (const auto& name : host.getFormatNames())
    {
        const auto path = host.getDefaultSearchPathForFormat(name);
        for (int i = 0; i < path.getNumPaths(); ++i)
        {
            if (!path[i].isDirectory())
                continue;
            const auto children = path[i].findChildFiles(juce::File::findFilesAndDirectories, /*recursive*/ false);
            if (!children.isEmpty())
                SKIP("default plugin path is populated; defer to Phase 1.C.4 sandbox scan");
        }
    }

    const int added = host.scanDefaultPaths();
    REQUIRE(added >= 0);
    REQUIRE(host.getKnownPlugins().size() == added);
}

// -----------------------------------------------------------------------
// getKnownEffects -- filter instruments out of the chain-add path
// -----------------------------------------------------------------------

namespace
{
juce::PluginDescription makeFakePluginDesc(const juce::String& name, bool isInstrument)
{
    juce::PluginDescription d;
    d.name = name;
    d.manufacturerName = "Pedalboard 3 Tests";
    d.pluginFormatName = "VST3";
    d.fileOrIdentifier = "/fake/" + name + ".vst3";
    d.uniqueId = juce::String(name).hashCode();
    d.deprecatedUid = d.uniqueId;
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
    d.isInstrument = isInstrument;
    return d;
}
} // namespace

TEST_CASE("PluginHost: getKnownEffects returns empty when the list is empty", "[engine][plugin-host][effects]")
{
    engine::PluginHost host;
    REQUIRE(host.getKnownEffects().isEmpty());
}

TEST_CASE("PluginHost: isDescriptorSafeForChain gates instruments and blacklisted files",
          "[engine][plugin-host][restore]")
{
    engine::PluginHost host;

    const auto effect = makeFakePluginDesc("SafeEffect", /*isInstrument*/ false);
    const auto instrument = makeFakePluginDesc("BadSynth", /*isInstrument*/ true);

    // A plain effect not on the blacklist is safe to restore into a chain.
    REQUIRE(host.isDescriptorSafeForChain(effect));

    // Instruments are never safe (v3.0 effects-only + heavy ctors).
    REQUIRE_FALSE(host.isDescriptorSafeForChain(instrument));

    // A blacklisted effect (known crash-on-construct) is not safe either.
    host.getKnownPluginList().addToBlacklist(effect.fileOrIdentifier);
    REQUIRE_FALSE(host.isDescriptorSafeForChain(effect));

    // An empty descriptor (no fileOrIdentifier) can never be instantiated -> not
    // safe. Guards corrupt .pdl files and the forward-compat case where an
    // internal node read by an older binary degrades to an empty-desc plugin.
    REQUIRE_FALSE(host.isDescriptorSafeForChain(juce::PluginDescription{}));
}

TEST_CASE("PluginHost: getKnownEffects filters out PluginDescription::isInstrument", "[engine][plugin-host][effects]")
{
    engine::PluginHost host;

    auto& list = host.getKnownPluginList();
    list.addType(makeFakePluginDesc("FakeReverb", /*isInstrument*/ false));
    list.addType(makeFakePluginDesc("FakeSynth", /*isInstrument*/ true));
    list.addType(makeFakePluginDesc("FakeDelay", /*isInstrument*/ false));

    // Three entries pre-listener: KnownPluginList::sendChangeMessage is async
    // and tests don't pump a MessageManager loop, so the instrument-filter
    // listener hasn't fired yet. getKnownEffects still filters at read time.
    const auto all = host.getKnownPlugins();
    REQUIRE(all.size() == 3);

    const auto effects = host.getKnownEffects();
    REQUIRE(effects.size() == 2);
    for (const auto& d : effects)
    {
        REQUIRE_FALSE(d.isInstrument);
        REQUIRE((d.name == "FakeReverb" || d.name == "FakeDelay"));
    }
}

TEST_CASE("PluginHost: ChangeListener strips instruments and blacklists their files", "[engine][plugin-host][effects]")
{
    engine::PluginHost host;

    auto& list = host.getKnownPluginList();
    list.addType(makeFakePluginDesc("FakeReverb", /*isInstrument*/ false));
    list.addType(makeFakePluginDesc("FakeSynth", /*isInstrument*/ true));
    list.addType(makeFakePluginDesc("FakeDelay", /*isInstrument*/ false));

    // Drive the listener synchronously. In production, the listener fires on
    // the message loop tick after KnownPluginList::sendChangeMessage; here
    // we bypass the loop. This is what the production app does on every
    // scan path (subprocess scanner, juce::PluginListComponent's internal
    // Scan button, scanDirectory, scanDefaultPaths, loadKnownList).
    list.sendSynchronousChangeMessage();

    const auto remaining = host.getKnownPlugins();
    REQUIRE(remaining.size() == 2);
    for (const auto& d : remaining)
        REQUIRE_FALSE(d.isInstrument);

    REQUIRE(list.getBlacklistedFiles().contains("/fake/FakeSynth.vst3"));
    REQUIRE_FALSE(list.getBlacklistedFiles().contains("/fake/FakeReverb.vst3"));
    REQUIRE_FALSE(list.getBlacklistedFiles().contains("/fake/FakeDelay.vst3"));
}

TEST_CASE("PluginHost: ChangeListener is idempotent across repeated triggers", "[engine][plugin-host][effects]")
{
    engine::PluginHost host;

    auto& list = host.getKnownPluginList();
    list.addType(makeFakePluginDesc("Synth1", /*isInstrument*/ true));
    list.addType(makeFakePluginDesc("Effect1", /*isInstrument*/ false));

    list.sendSynchronousChangeMessage();
    REQUIRE(host.getKnownPlugins().size() == 1);
    REQUIRE(list.getBlacklistedFiles().contains("/fake/Synth1.vst3"));

    // Firing the listener again with no instruments left must be a no-op
    // -- not loop, not double-blacklist, not throw.
    list.sendSynchronousChangeMessage();
    REQUIRE(host.getKnownPlugins().size() == 1);
    REQUIRE(list.getBlacklistedFiles().size() == 1);
}

// -----------------------------------------------------------------------
// applyKnownProblemBlacklist -- KnownProblemPlugins.h floor list
// -----------------------------------------------------------------------

TEST_CASE("PluginHost: applyKnownProblemBlacklist returns false on a bare runner", "[engine][plugin-host][blacklist]")
{
    engine::PluginHost host;

    // Same precaution as scanDefaultPaths above: skip on a dev box with
    // populated plugin dirs. Here we only need *known-problem* basenames
    // (Current.vst3 / Kontakt.vst3 / Kontakt 7.vst3) -- but we can't tell
    // those apart from arbitrary VSTs without reading the OS plugin
    // dirs, and reading them is what we're testing. Skip if any default
    // path is populated; that keeps CI green on a bare runner where the
    // floor list legitimately matches nothing.
    for (const auto& name : host.getFormatNames())
    {
        const auto path = host.getDefaultSearchPathForFormat(name);
        for (int i = 0; i < path.getNumPaths(); ++i)
        {
            if (!path[i].isDirectory())
                continue;
            const auto children = path[i].findChildFiles(juce::File::findFiles, /*recursive*/ true);
            if (!children.isEmpty())
                SKIP("default plugin path is populated; floor-list outcome is host-specific");
        }
    }

    const bool added = host.applyKnownProblemBlacklist();
    REQUIRE_FALSE(added);
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().isEmpty());
}

TEST_CASE("PluginHost: applyKnownProblemBlacklist is idempotent", "[engine][plugin-host][blacklist]")
{
    engine::PluginHost host;

    // Pre-seed the blacklist with one of the floor-list basenames at a
    // synthetic path the host can't possibly already know. The second
    // applyKnownProblemBlacklist call must NOT re-add this path (the
    // contract: addToBlacklist short-circuits duplicates -> returns
    // false).
    auto& list = host.getKnownPluginList();
    const juce::String synthetic{"/synthetic/Current.vst3"};
    list.addToBlacklist(synthetic);
    REQUIRE(list.getBlacklistedFiles().contains(synthetic));

    // Calling on a bare runner won't add anything new, but the seeded
    // entry must survive (the blacklist is monotone).
    host.applyKnownProblemBlacklist();
    REQUIRE(list.getBlacklistedFiles().contains(synthetic));

    // Second call is a pure no-op on the bare-runner path.
    const bool added = host.applyKnownProblemBlacklist();
    REQUIRE_FALSE(added);
    REQUIRE(list.getBlacklistedFiles().contains(synthetic));
}

// ---- Internal (built-in) node factory --------------------------------------

TEST_CASE("PluginHost: createInternalInstance builds known types, rejects unknown", "[engine][plugin-host][internal]")
{
    engine::PluginHost host;

    juce::String err;
    auto level = host.createInternalInstance("LevelProcessor", 48000.0, 256, err);
    REQUIRE(level != nullptr);
    REQUIRE(level->getName() == "Level");
    REQUIRE(err.isEmpty());

    err.clear();
    auto vu = host.createInternalInstance("VuMeterProcessor", 48000.0, 256, err);
    REQUIRE(vu != nullptr);
    REQUIRE(vu->getName() == "VU Meter");
    REQUIRE(err.isEmpty());

    err.clear();
    auto toggle = host.createInternalInstance("OutputToggleProcessor", 48000.0, 256, err);
    REQUIRE(toggle != nullptr);
    REQUIRE(toggle->getName() == "Output Toggle");
    REQUIRE(err.isEmpty());

    err.clear();
    auto nope = host.createInternalInstance("Nonexistent", 48000.0, 256, err);
    REQUIRE(nope == nullptr);
    REQUIRE(err.isNotEmpty());
}

TEST_CASE("PluginHost: getInternalNodeTypes lists the built-in nodes", "[engine][plugin-host][internal]")
{
    engine::PluginHost host;
    const auto types = host.getInternalNodeTypes();
    REQUIRE(types.size() == 3);

    bool hasLevel = false;
    bool hasVu = false;
    bool hasOutputToggle = false;
    for (const auto& t : types)
    {
        if (t.typeId == "LevelProcessor" && t.displayName == "Level")
            hasLevel = true;
        if (t.typeId == "VuMeterProcessor" && t.displayName == "VU Meter")
            hasVu = true;
        if (t.typeId == "OutputToggleProcessor" && t.displayName == "Output Toggle")
            hasOutputToggle = true;
    }
    REQUIRE(hasLevel);
    REQUIRE(hasVu);
    REQUIRE(hasOutputToggle);
}
