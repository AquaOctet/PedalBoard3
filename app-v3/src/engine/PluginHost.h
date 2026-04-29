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

    // Synchronously scan `path` with every registered format whose name is
    // listed in `formatNames`, or with EVERY registered format when
    // `formatNames` is empty. Returns the number of NEWLY discovered plugins
    // (not counting ones already in the known list). Non-existent or
    // unreadable paths return 0 and do not throw.
    //
    // Tip: use the per-format form for any caller that has chosen specific
    // paths. juce::AudioUnitPluginFormat::searchPathsForPlugins() ignores the
    // FileSearchPath argument and enumerates EVERY AudioComponent the OS
    // knows about, so passing AU through alongside a VST3-only path will
    // walk every installed AU plugin too. Phase 1.C.4 (sandboxed scanning)
    // will be the proper isolation; until then, scope by format.
    int scanDirectory(const juce::FileSearchPath& path, const juce::StringArray& formatNames = {});

    // Convenience: scan every format's default OS paths, one format at a
    // time, only with that format. Returns the total number of newly-
    // discovered plugins. Suitable for a "first-run scan" button.
    int scanDefaultPaths();

    // Replace the entire known list (and blacklist) from a previously-saved
    // XmlElement produced by createKnownListXml(). Used by App at startup
    // so a fresh launch doesn't have to rescan. Acquires the write lock for
    // the swap. Safe to call before any scan; safe to call repeatedly.
    void loadKnownList(const juce::XmlElement& xml);

    // Snapshot the known list (including the JUCE plugin blacklist) as
    // XML, suitable for storing via session::Preferences. Returns nullptr
    // if JUCE produces an empty list.
    std::unique_ptr<juce::XmlElement> createKnownListXml() const;

    // Remove every entry from the known plugin list.
    void clearKnownPlugins();

    // Snapshot copy of the known plugin list. Returned by value so callers
    // can iterate safely while a concurrent scan adds to the live list.
    juce::Array<juce::PluginDescription> getKnownPlugins() const;

    // Subset of getKnownPlugins() filtered to audio-effect plugins only —
    // entries with PluginDescription::isInstrument == false.
    //
    // v3.0 ships as an effect-rack host (master plan §4.2.D — "live
    // performance effect chain"). Instrument plugins (Kontakt, Diva,
    // Massive X, etc.) live in DAWs; routing MIDI into them is a
    // Phase 4 concern (MidiRouter), and many of them have heavy
    // license-check / online-refresh code in their constructors that's
    // exactly the failure mode the user has hit on Current.vst3 and
    // Kontakt during scan. Filtering instruments out at the chain-
    // entry point sidesteps both problems for v3.0.
    //
    // The Plugin List dialog still shows every scanned plugin (effects
    // and instruments) so the user can see what's installed; the
    // filter only applies at the "Add to chain" path.
    juce::Array<juce::PluginDescription> getKnownEffects() const;

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

    // Walk the default VST3 search paths, find every bundle whose
    // basename matches an entry in engine/KnownProblemPlugins.h, and
    // pre-add it to KnownPluginList's blacklist. Plugins on that list
    // are known to crash the host during scan / construction /
    // destruction (e.g. Minimal Audio Current's pure-virtual license
    // refresh, Kontakt's async analytics deinit) — once a crash fires
    // we can't recover, so the only protection is to never open them.
    //
    // Idempotent: addToBlacklist short-circuits duplicates. Returns
    // true if any new entries were added (caller can flush prefs to
    // persist).
    //
    // Replaced by an out-of-process subprocess scanner in v3.1
    // (master plan §4.2.A) — at that point this list goes away.
    bool applyKnownProblemBlacklist();

private:
    juce::AudioPluginFormatManager formats_;
    juce::KnownPluginList knownList_;
    mutable juce::ReadWriteLock listLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};

} // namespace pedalboard3::engine
