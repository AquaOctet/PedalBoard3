#pragma once

#include "PluginHost.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace pedalboard3::engine
{

// Parse a <PB3_SCAN_RESULT> XML tree (the helper's stdout contract: one
// <PLUGIN> child per discovered descriptor) into effect-only descriptions.
//
// Drops isInstrument entries at the source. v3.0 ships as an effects-only
// host (master plan §4.2.D): the Add-to-chain picker filters via
// PluginHost::getKnownEffects() and the instrument sweep in
// PluginHost::changeListenerCallback blacklists any instrument that lands
// in the list. Filtering here keeps the simpler invariant true —
// KnownPluginList never carries an instrument descriptor at all. A bundle
// exposing BOTH effect and instrument types (legal but rare) keeps only
// the effect; a bundle of ONLY instruments yields an empty array, which
// PluginScanner::scanFile treats as a failed scan and blacklists.
//
// Free function (not a private method) so it is unit-testable without
// spawning the helper subprocess — runHelper calls it on the parsed root.
juce::Array<juce::PluginDescription> effectDescriptionsFromScanXml(const juce::XmlElement& scanResult);

// Out-of-process plugin scanner (master plan §4.2.A).
//
// Each plugin file is scanned in its own subprocess (the
// pb3-scanner-helper binary). When a plugin crashes during
// construction — Current.vst3's MKURequestManager pure-virtual call,
// Kontakt's BusinessAnalytics async-deinit abort, anything else —
// only the helper subprocess dies. The host stays alive, the parent
// observes the non-zero exit code, blacklists the path, and moves on.
//
// This is what production hosts (Reaper, Bitwig, Logic, Live) do.
// It is the primary path for plugin scanning. It does NOT subsume
// engine/KnownProblemPlugins.h: that floor is a launch-time,
// pre-construction, path-based blacklist for plugins that crash before
// any descriptor exists, and scanAllDefaultPaths skips already-blacklisted
// paths — so the scanner never even runs on them. The two are
// complementary, not redundant.
//
// Threading: scanFile / scanDirectory run on the message thread by
// design. The helper is the part that may block (waitForProcessToFinish)
// or hang. Callers wanting async scanning should run scanDirectory on
// a Thread or via TimeSliceClient.
//
// State: the scanner mutates the supplied PluginHost's KnownPluginList
// (adds descriptors on success, blacklists paths on failure). It
// holds no other state — the helper binary path is captured at
// construction.
class PluginScanner
{
public:
    // Caller supplies the host (whose KnownPluginList we mutate) and
    // the absolute path to the pb3-scanner-helper binary. Use
    // findHelperBinary() to locate it. Throws nothing — if the helper
    // doesn't exist, scan calls fail per-plugin with isHelperAvailable()
    // returning false.
    PluginScanner(PluginHost& host, juce::File helperBinary);

    // Best-effort lookup of the bundled scanner-helper binary. Searches
    // a few paths derived from the host process's currentExecutableFile:
    //   * Sibling of currentExecutableFile (Linux/Windows installs +
    //     macOS where POST_BUILD has copied the helper into the .app
    //     bundle's Contents/MacOS/).
    //   * Parent's parent + "pb3-scanner-helper_artefacts/<config>/" for
    //     in-tree development builds where each JUCE target has its own
    //     artefacts subtree under build/<preset>/src/.
    //
    // Returns a non-existent juce::File if none of these resolve.
    static juce::File findHelperBinary();

    bool isHelperAvailable() const noexcept;

    // Result of one scanFile invocation. Populated either with at
    // least one description (success) or with a non-empty error
    // string (helper failure: timeout, non-zero exit, parse error).
    struct ScanResult
    {
        juce::String pluginPath;
        juce::Array<juce::PluginDescription> descriptions;
        bool helperCrashed{false}; // non-zero exit / signal
        bool timedOut{false};
        juce::String error;

        // True when applyScanResults should blacklist this path (helper ran
        // but crashed / timed out / produced no usable effect descriptors).
        // False when we never attempted the scan (no helper) — those are
        // retried on a future scan rather than poisoned permanently.
        bool shouldBlacklist{false};
    };

    // Scan one plugin file in the subprocess and RETURN the result. Pure
    // producer: does NOT touch the host's KnownPluginList, so it is safe to
    // call from a worker thread. The result carries the effect descriptions
    // to add (on success) or shouldBlacklist=true (on a failure that ran the
    // helper). Commit it with applyScanResults() on the MESSAGE THREAD —
    // KnownPluginList is not thread-safe and is owned solely by the message
    // thread, so mutating it from the scan worker races the list's
    // ChangeListeners and corrupts it (the crash this split fixes).
    ScanResult scanFile(const juce::String& pluginPath);

    // Walk every registered plugin format's default search path, dedupe,
    // run the subprocess on each, and RETURN the results. Worker-thread
    // safe: it mutates no shared state and snapshots the current blacklist
    // once up front to skip already-blacklisted paths. onProgress is invoked
    // before each scan with (currentIndex, total, currentPath).
    using ProgressCallback = std::function<void(int currentIndex, int total, const juce::String& currentPath)>;
    std::vector<ScanResult> scanAllDefaultPaths(ProgressCallback onProgress);

    // Commit scan results to the host's KnownPluginList: addType for each
    // description, addToBlacklist for each shouldBlacklist result. Returns
    // the number of descriptors added. MESSAGE-THREAD ONLY — this is the
    // single writer of KnownPluginList on the scan path, serialised with the
    // list's ChangeListeners (instrument sweep, preferences persistence)
    // which also run on the message thread. Calling it off the message
    // thread reintroduces the data race this split exists to prevent.
    int applyScanResults(const std::vector<ScanResult>& results);

private:
    // Helper: actually run the subprocess. Returns true if the helper
    // exited cleanly with parseable XML. result is populated either
    // with descriptions (on success) or error/flags (on failure).
    bool runHelper(const juce::String& pluginPath, ScanResult& result) const;

    PluginHost& host_;
    juce::File helperBinary_;

    // Per-plugin scan budget. Most plugins instantiate in <1s; some
    // heavy synths (Kontakt-class) take 5-10s. 30s is generous enough
    // that we don't false-positive a slow plugin as crashed, while
    // still bounding hung scans.
    static constexpr int kPerPluginTimeoutMs = 30000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanner)
};

} // namespace pedalboard3::engine
