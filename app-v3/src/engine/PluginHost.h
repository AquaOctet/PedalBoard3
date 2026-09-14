#pragma once

#include "DiskThread.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <optional>

namespace pedalboard3::engine
{

// A built-in (non-plugin) node type the host can instantiate, as a
// {serialization typeId, display name} pair. `typeId` is the stable token
// written to .pdl (<NODE type="...">) and matched at restore; `displayName` is
// the Add-menu label.
struct InternalNodeType
{
    juce::String typeId;
    juce::String displayName;
};

// Thin facade over juce::AudioPluginFormatManager + KnownPluginList.
//   - Registers the default plugin formats on construction: VST3, plus AU on
//     macOS and LV2 on macOS and Linux.
//   - Owns a KnownPluginList with reader/writer synchronisation so the
//     UI thread and worker thread can read + mutate it safely.
//   - Exposes synchronous directory scanning, single-instance creation and
//     the built-in processors.
// The scan that runs each plugin in its own process, so a crashing plugin
// can't take the host down, is engine::PluginScanner, with pb3-scanner-helper.
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
    // walk every installed AU plugin too.
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
    // filter only applies at the "Add to chain" path. Instruments stay in
    // the known list (M5 U6, plan KD8) rather than being blacklisted as
    // though they had crashed.
    juce::Array<juce::PluginDescription> getKnownEffects() const;

    // True if `desc` is safe to construct into a live chain. False for
    // instruments (v3.0 is effects-only) and for descriptors whose file is
    // blacklisted (known crash-on-construct, e.g. Current.vst3). Used by
    // App's saved-patch restore and the .pdl loader to drop nodes BEFORE
    // createInstance: the known-problem floor only guards the scan path, so
    // the restore/load path — which calls createInstance
    // directly — would otherwise construct such a plugin unguarded and crash
    // the host on launch. Message-thread only.
    bool isDescriptorSafeForChain(const juce::PluginDescription& desc) const;

    // Find a board's plugin in the scanned list (M5 U3, plan KD3): by format and
    // ID (the unique id, or the older one), else by name and vendor. `fileHint`, a
    // path from an older board, only picks between equal matches; it's never
    // opened. Returns the scanned description (whose path createInstance uses), or
    // nullopt when the plugin isn't in the list. Message-thread only.
    std::optional<juce::PluginDescription> resolve(const juce::PluginDescription& saved,
                                                   const juce::String& fileHint = {}) const;

    // Create a single plugin instance. Returns nullptr and fills `errorOut`
    // on any failure (plugin missing, format unregistered, plugin ctor
    // threw, etc.). Caller takes ownership; the patch builder
    // (session::buildPreparedGraph) wraps it in a BypassableInstance.
    std::unique_ptr<juce::AudioPluginInstance> createInstance(const juce::PluginDescription& desc,
                                                              double sampleRate,
                                                              int blockSize,
                                                              juce::String& errorOut);

    // The built-in node types this host can instantiate (Level, VU, ...). Drives
    // the Add ▸ Internal menu and the restore-time "is this internal type
    // recognised?" filter that drops unknown types instead of aborting a load.
    juce::Array<InternalNodeType> getInternalNodeTypes() const;

    // Construct a built-in processor by its typeId (e.g. "LevelProcessor"). The
    // internal analogue of createInstance: returns nullptr + fills `errorOut`
    // for an unknown type (that nullptr IS the safety check — unknown types must
    // be filtered out before this is reached). Caller takes ownership and wraps
    // it in a BypassableInstance exactly like a plugin instance. The instance is
    // configured for `sampleRate`/`blockSize` but prepareToPlay is left to the
    // graph build / add-worker, matching createInstance.
    std::unique_ptr<juce::AudioProcessor> createInternalInstance(const juce::String& type,
                                                                 double sampleRate,
                                                                 int blockSize,
                                                                 juce::String& errorOut);

    // Escape hatch for code that needs the raw JUCE format manager (JUCE's
    // own PluginListComponent, in the Plugin List dialog).
    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formats_; }

    // Mutable reference to the underlying KnownPluginList. Intended for
    // JUCE's built-in PluginListComponent, which mutates the list as the
    // user adds/removes plugins through the UI. Message-thread-only —
    // the ReadWriteLock used by scanDirectory/getKnownPlugins is NOT
    // held for UI edits.
    juce::KnownPluginList& getKnownPluginList() noexcept { return knownList_; }

    // Path to the "dead man's pedal" file JUCE uses to remember which
    // plugin binary is mid-scan if the scanner crashes. Lives under
    // UserDataDir/crashed_plugins.xml; parent directory is created
    // lazily on first access. Caller is responsible for passing this to
    // PluginDirectoryScanner / PluginListComponent.
    juce::File getCrashedPluginsFile() const;

    // The folder the crash list and the scan-in-flight marker live in: the
    // settings file's (M5 U7, plan KD8), which App sets. Until it's set,
    // "Pedalboard 3" in the user's application data folder.
    void setUserDataFolder(juce::File folder);

    // The crash list, read strictly (M5 U7, plan KD8): every line must name a
    // plugin (an absolute path, or a "Format:identifier" such as an Audio
    // Unit's), or none of it is used. `error` says why a list was refused;
    // empty when it was fine or absent.
    struct CrashList
    {
        juce::StringArray paths;
        juce::String error;
    };
    CrashList readCrashList() const;

    // Where the out-of-process scanner keeps its scan-in-flight marker (M5 U6,
    // plan KD8): beside the crash list.
    juce::File getScanInFlightFile() const;

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
    // NOT replaced by the out-of-process PluginScanner (master plan
    // §4.2.A, shipped v3.0): this is the launch-time, pre-construction,
    // path-based floor for plugins that crash before any descriptor
    // exists (which neither scanner can observe). It stays. See
    // engine/KnownProblemPlugins.h for the full rationale.
    bool applyKnownProblemBlacklist();

private:
    DiskThread diskThread_; // first, so it outlives nothing that uses it: processors go with Session
    juce::AudioPluginFormatManager formats_;
    juce::KnownPluginList knownList_;
    mutable juce::ReadWriteLock listLock_;
    juce::File userDataFolder_; // see setUserDataFolder

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};

} // namespace pedalboard3::engine
