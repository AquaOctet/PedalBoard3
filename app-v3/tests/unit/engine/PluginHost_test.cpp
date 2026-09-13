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

TEST_CASE("PluginHost: resolve finds a board's plugin by format and ID, never by path (M5 U3)",
          "[engine][plugin-host][restore]")
{
    engine::PluginHost host;
    const auto eq = makeFakePluginDesc("FakeEQ", /*isInstrument*/ false);
    host.getKnownPluginList().addType(eq);
    host.getKnownPluginList().addType(makeFakePluginDesc("FakeVerb", /*isInstrument*/ false));

    // By ID alone: a board has no path.
    juce::PluginDescription saved;
    saved.pluginFormatName = "VST3";
    saved.uniqueId = eq.uniqueId;
    const auto byId = host.resolve(saved);
    REQUIRE(byId.has_value());
    REQUIRE(byId->fileOrIdentifier == eq.fileOrIdentifier);

    // By name and vendor when the ID changed (an update).
    juce::PluginDescription updated;
    updated.pluginFormatName = "VST3";
    updated.name = "FakeEQ";
    updated.manufacturerName = eq.manufacturerName;
    updated.uniqueId = 42;
    const auto byName = host.resolve(updated);
    REQUIRE(byName.has_value());
    REQUIRE(byName->name == "FakeEQ");

    // Another format, or a plugin that isn't scanned: not found.
    saved.pluginFormatName = "AudioUnit";
    REQUIRE_FALSE(host.resolve(saved).has_value());
    juce::PluginDescription gone;
    gone.pluginFormatName = "VST3";
    gone.name = "Gone";
    gone.uniqueId = 7;
    REQUIRE_FALSE(host.resolve(gone).has_value());

    // A path alone finds nothing.
    juce::PluginDescription pathOnly;
    pathOnly.pluginFormatName = "VST3";
    REQUIRE_FALSE(host.resolve(pathOnly, eq.fileOrIdentifier).has_value());
}

TEST_CASE("PluginHost: resolve uses an older board's path only to pick between equal matches",
          "[engine][plugin-host][restore]")
{
    engine::PluginHost host;
    auto first = makeFakePluginDesc("Twin", /*isInstrument*/ false);
    first.fileOrIdentifier = "/a/Twin.vst3";
    auto second = first;
    second.fileOrIdentifier = "/b/Twin.vst3";
    host.getKnownPluginList().addType(first);
    host.getKnownPluginList().addType(second);

    juce::PluginDescription saved;
    saved.pluginFormatName = "VST3";
    saved.name = "Twin";
    saved.uniqueId = first.uniqueId;
    REQUIRE(host.resolve(saved, "/b/Twin.vst3")->fileOrIdentifier == "/b/Twin.vst3");
    REQUIRE(host.resolve(saved, "/a/Twin.vst3")->fileOrIdentifier == "/a/Twin.vst3");
}

TEST_CASE("PluginHost: getKnownEffects filters out PluginDescription::isInstrument", "[engine][plugin-host][effects]")
{
    engine::PluginHost host;

    auto& list = host.getKnownPluginList();
    list.addType(makeFakePluginDesc("FakeReverb", /*isInstrument*/ false));
    list.addType(makeFakePluginDesc("FakeSynth", /*isInstrument*/ true));
    list.addType(makeFakePluginDesc("FakeDelay", /*isInstrument*/ false));

    // All three stay in the known list (M5 U6: instruments are kept);
    // getKnownEffects filters them at read time.
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

TEST_CASE("PluginHost: instruments stay in the known list, out of the effect lists and off the blacklist (M5 U6)",
          "[engine][plugin-host][effects]")
{
    engine::PluginHost host;

    auto& list = host.getKnownPluginList();
    list.addType(makeFakePluginDesc("FakeReverb", /*isInstrument*/ false));
    list.addType(makeFakePluginDesc("FakeSynth", /*isInstrument*/ true));
    list.sendSynchronousChangeMessage(); // what every scan path does

    // Kept, not swept into the blacklist as though it had crashed (plan KD8).
    REQUIRE(host.getKnownPlugins().size() == 2);
    REQUIRE(list.getBlacklistedFiles().isEmpty());

    // The pickers and the chain still leave it out.
    REQUIRE(host.getKnownEffects().size() == 1);
    REQUIRE_FALSE(host.isDescriptorSafeForChain(makeFakePluginDesc("FakeSynth", /*isInstrument*/ true)));
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
    REQUIRE(types.size() == 7);

    bool hasLevel = false;
    bool hasVu = false;
    bool hasOutputToggle = false;
    bool hasFilePlayer = false;
    bool hasRecorder = false;
    bool hasMetronome = false;
    bool hasLooper = false;
    for (const auto& t : types)
    {
        if (t.typeId == "LevelProcessor" && t.displayName == "Level")
            hasLevel = true;
        if (t.typeId == "VuMeterProcessor" && t.displayName == "VU Meter")
            hasVu = true;
        if (t.typeId == "OutputToggleProcessor" && t.displayName == "Output Toggle")
            hasOutputToggle = true;
        if (t.typeId == "FilePlayerProcessor" && t.displayName == "File Player")
            hasFilePlayer = true;
        if (t.typeId == "AudioRecorderProcessor" && t.displayName == "Audio Recorder")
            hasRecorder = true;
        if (t.typeId == "MetronomeProcessor" && t.displayName == "Metronome")
            hasMetronome = true;
        if (t.typeId == "LooperProcessor" && t.displayName == "Looper")
            hasLooper = true;
    }
    REQUIRE(hasLevel);
    REQUIRE(hasVu);
    REQUIRE(hasOutputToggle);
    REQUIRE(hasFilePlayer);
    REQUIRE(hasRecorder);
    REQUIRE(hasMetronome);
    REQUIRE(hasLooper);
}

TEST_CASE("PluginHost: the crash list is read strictly: a malformed one is reported, not half-read (M5 U7)",
          "[engine][plugin-host][blacklist]")
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("pb3-crash-list-" + juce::String(juce::Time::currentTimeMillis()));
    engine::PluginHost host;
    host.setUserDataFolder(folder);
    const auto file = host.getCrashedPluginsFile();
    REQUIRE(file.getParentDirectory() == folder);

    // None yet: nothing to read, nothing wrong.
    REQUIRE(host.readCrashList().paths.isEmpty());
    REQUIRE(host.readCrashList().error.isEmpty());

#if JUCE_WINDOWS
    const juce::String crashy = "C:\\Plugins\\Crashy.vst3";
    const juce::String other = "C:\\Plugins\\Other.vst3";
#else
    const juce::String crashy = "/plugins/Crashy.vst3";
    const juce::String other = "/plugins/Other.vst3";
#endif
    REQUIRE(file.replaceWithText(crashy + "\n\n" + other + "\nAudioUnit:Effects/aufx,dely,appl\n"));
    const auto good = host.readCrashList();
    REQUIRE(good.error.isEmpty());
    REQUIRE(good.paths.size() == 3);

    // One line that names no plugin: none of it is used.
    REQUIRE(file.replaceWithText(crashy + "\nnot a plugin\n" + other + "\n"));
    const auto bad = host.readCrashList();
    REQUIRE(bad.paths.isEmpty());
    REQUIRE(bad.error.contains("line 2"));

    folder.deleteRecursively();
}
