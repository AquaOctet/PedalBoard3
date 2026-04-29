#pragma once

#include "PluginHost.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <functional>

namespace pedalboard3::engine
{

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
// Replaces the in-process juce::PluginListComponent scan path AND
// the engine/KnownProblemPlugins.h floor list (which becomes
// redundant once this is wired in).
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
    };

    // Scan one plugin file. On success: descriptions are added to
    // the host's KnownPluginList. On any failure (helper crashed,
    // timed out, returned no descriptors but exited cleanly): the
    // path is added to the host's blacklist so subsequent scans skip
    // it. Returns the result struct so callers can surface errors in
    // the UI.
    ScanResult scanFile(const juce::String& pluginPath);

    // Walk every registered plugin format's default search path,
    // dedupe, and call scanFile on each. onProgress is invoked
    // before each scan with (currentIndex, total, currentPath).
    // Pass an empty function to skip progress callbacks.
    using ProgressCallback = std::function<void(int currentIndex, int total, const juce::String& currentPath)>;
    int scanAllDefaultPaths(ProgressCallback onProgress);

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
