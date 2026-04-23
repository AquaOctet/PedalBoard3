#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace pedalboard3::engine
{

// Thin facade over juce::AudioPluginFormatManager + KnownPluginList.
//
// Phase 1.C.3 scope:
//   - Registers default plugin formats on construction (VST3 + AU + LV2
//     + whatever juce_audio_processors ships with). CLAP support is added
//     in Phase 1.F via a vendored juce_clap_hosting fork.
//   - Owns a KnownPluginList with reader/writer synchronisation so the
//     UI thread and worker thread can read + mutate it safely.
//   - Exposes synchronous directory scanning and single-instance
//     creation — the minimum surface later layers (Phase 1.D patch
//     build, Phase 3 plugin-list UI) need.
//
// Phase 1.C.4 will add sandboxed out-of-process scanning with a crash
// list (plan AC-PL-2, AC-SE-23..26). Not in this slice.
//
// Not RT-safe; message-thread only. Scans hold the write lock for their
// duration; readers block briefly during a scan. The audio thread never
// touches this object directly — it calls into live plugin instances
// that PluginHost returned.
class PluginHost
{
public:
    PluginHost();
    ~PluginHost();

    // How many AudioPluginFormat subclasses are currently registered.
    // After construction this is always > 0 on every supported platform.
    int getNumFormats() const noexcept;

    // Names of every registered format ("VST3", "AudioUnit", "LV2", …).
    // UI uses this to present a per-format pick list next to a default-path
    // editor. Size always equals getNumFormats().
    juce::StringArray getFormatNames() const;

    // Platform-appropriate default directories that `formatName` searches
    // by default (e.g. "~/Library/Audio/Plug-Ins/VST3" on macOS). Returns
    // an empty FileSearchPath if the format isn't registered. Mirrors
    // juce::AudioPluginFormat::getDefaultLocationsToSearch() semantics.
    juce::FileSearchPath getDefaultSearchPathForFormat(const juce::String& formatName) const;

    // Synchronously scan `path` with every registered format. Returns the
    // number of NEWLY discovered plugins (not counting ones already in the
    // known list). Non-existent or unreadable paths return 0 and do not
    // throw.
    int scanDirectory(const juce::FileSearchPath& path);

    // Convenience: walk every registered format's default OS paths and scan
    // them. Returns the total number of newly-discovered plugins across all
    // formats. Suitable for a "first-run scan" button.
    int scanDefaultPaths();

    // Remove every entry from the known plugin list.
    void clearKnownPlugins();

    // Snapshot copy of the known plugin list. Returned by value so callers
    // can iterate safely while a concurrent scan adds to the live list.
    juce::Array<juce::PluginDescription> getKnownPlugins() const;

    // Create a single plugin instance. Returns nullptr and fills `errorOut`
    // on any failure (plugin missing, format unregistered, plugin ctor
    // threw, etc.). Caller takes ownership; the Phase 1.D patch builder
    // wraps the returned instance in a BypassableInstance.
    std::unique_ptr<juce::AudioPluginInstance> createInstance(const juce::PluginDescription& desc,
                                                              double sampleRate,
                                                              int blockSize,
                                                              juce::String& errorOut);

    // Escape hatch for code that needs the raw JUCE format manager (e.g.
    // JUCE's own PluginListComponent in a future settings dialog).
    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formats_; }

    // Mutable reference to the underlying KnownPluginList. Intended for
    // JUCE's built-in PluginListComponent, which mutates the list as the
    // user adds/removes plugins through the UI. Message-thread-only —
    // the ReadWriteLock used by scanDirectory/getKnownPlugins is NOT
    // held for UI edits. Phase 1.D revisits this when the patch-switch
    // worker thread also needs to read the list.
    juce::KnownPluginList& getKnownPluginList() noexcept { return knownList_; }

    // Path to the "dead man's pedal" file JUCE uses to remember which
    // plugin binary is mid-scan if the scanner crashes. Lives under
    // UserDataDir/crashed_plugins.xml; parent directory is created
    // lazily on first access. Caller is responsible for passing this to
    // PluginDirectoryScanner / PluginListComponent.
    juce::File getCrashedPluginsFile() const;

private:
    juce::AudioPluginFormatManager formats_;
    juce::KnownPluginList knownList_;
    mutable juce::ReadWriteLock listLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};

} // namespace pedalboard3::engine
