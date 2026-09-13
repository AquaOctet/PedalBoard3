#pragma once

#include "PluginHost.h"
#include "ScanProtocol.h" // kScanOkSentinel — shared with the scanner helper

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace pedalboard3::engine
{

// Parse a <PB3_SCAN_RESULT> XML tree (the helper's result: one <PLUGIN> child
// per discovered descriptor) into descriptions, instruments included (M5 U6,
// plan KD8). An instrument is kept in the known list and left out of the
// effects-only pickers (PluginHost::getKnownEffects), rather than having its
// file blacklisted as though it had crashed.
//
// Free function (not a private method) so it is unit-testable without
// spawning the helper subprocess — parseHelperOutput calls it on the root.
juce::Array<juce::PluginDescription> descriptionsFromScanXml(const juce::XmlElement& scanResult);

// kScanOkSentinel (the helper's completion marker) lives in ScanProtocol.h,
// shared verbatim with pb3-scanner-helper.cpp so the two never drift.

// Out-of-process plugin scanner (master plan §4.2.A).
//
// Each plugin file is scanned in its own subprocess (the
// pb3-scanner-helper binary). When a plugin crashes during
// construction — Current.vst3's MKURequestManager pure-virtual call,
// Kontakt's BusinessAnalytics async-deinit abort, anything else —
// only the helper subprocess dies. The host stays alive, the parent
// observes the crash, blacklists the path, and moves on.
//
// This is what production hosts (Reaper, Bitwig, Logic, Live) do.
// It is the primary path for plugin scanning. It does NOT subsume
// engine/KnownProblemPlugins.h: that floor is a launch-time,
// pre-construction, path-based blacklist for plugins that crash before
// any descriptor exists, and scanAllDefaultPaths skips already-blacklisted
// paths — so the scanner never even runs on them. The two are
// complementary, not redundant.
//
// Robustness (M5 U6, plan KD8):
//   - The helper writes its result to a file and runs with no pipes, so a big
//     result, or a plugin that prints a lot, can never block it.
//   - A helper that's killed (a timeout, a cancel) is waited for, so it's
//     reaped rather than left a zombie.
//   - Each path records why it ended as it did (Outcome). Only a crash, a
//     timeout or "not a plugin" is blacklisted; a cancelled scan, a missing
//     helper and an instrument never are.
//   - A shell bundle (WaveShell) gets 90 s instead of 30 s.
//   - A scan-in-flight marker makes the next scan skip, not blacklist,
//     whatever was being scanned when the host died.
//   - The helper is looked for only next to the app.
//
// Threading: scanFile / scanAllDefaultPaths run on a worker by design. The
// helper is the part that may block or hang. Results are committed on the
// message thread with applyScanResults.
class PluginScanner
{
public:
    // Caller supplies the host (whose KnownPluginList we mutate) and
    // the absolute path to the pb3-scanner-helper binary. Use
    // findHelperBinary() to locate it. Throws nothing — if the helper
    // doesn't exist, scan calls fail per-plugin with isHelperAvailable()
    // returning false.
    PluginScanner(PluginHost& host, juce::File helperBinary);

    // The helper, looked for only next to the running app (M5 U6, plan KD8):
    // a POST_BUILD step copies it there in development builds, and an install
    // ships it there. A non-existent file when it isn't there.
    static juce::File findHelperBinary();
    // The same lookup beside `executable`.
    static juce::File findHelperBinary(const juce::File& executable);

    bool isHelperAvailable() const noexcept;

    // How a path ended up (M5 U6, plan KD8).
    enum class Outcome
    {
        Found,           // effect plugins: added
        Instrument,      // only instruments: added, and left out of the effect lists
        NotAPlugin,      // the helper found nothing it could load: blacklisted
        Crashed,         // the helper died, a plugin crashing it: blacklisted
        TimedOut,        // the helper hung past its budget, and was killed: blacklisted
        Cancelled,       // the scan was cancelled partway: tried again next time
        SkippedLastTime, // the host died while scanning it last time: skipped once
        NoHelper,        // never tried: there was no helper to run
    };

    struct ScanResult
    {
        juce::String pluginPath;
        juce::Array<juce::PluginDescription> descriptions;
        Outcome outcome{Outcome::NoHelper};
        bool helperCrashed{false}; // crashed, or couldn't run at all
        bool timedOut{false};
        juce::String error;

        // True when applyScanResults should blacklist this path: the helper
        // crashed on it, hung on it, or found no plugin in it. Never for a
        // cancelled scan, a missing helper or an instrument.
        bool shouldBlacklist{false};
    };

    // Predicate a worker passes so a long scan can be cut short (cancel, or
    // shutdown). Checked between plugins and while polling each subprocess
    // wait; when it returns true the scan abandons the current and remaining
    // files promptly, so an owning thread's join is bounded to one poll slice
    // (crash-hunt #8).
    using AbortPredicate = std::function<bool()>;

    // Scan one plugin file in the subprocess and RETURN the result. Pure
    // producer: does NOT touch the host's KnownPluginList, so it is safe to
    // call from a worker thread. Commit it with applyScanResults() on the
    // MESSAGE THREAD — KnownPluginList is not thread-safe.
    ScanResult scanFile(const juce::String& pluginPath, const AbortPredicate& shouldAbort = {});

    // Walk every registered plugin format's default search path, dedupe,
    // and scan each path (scanPaths). Worker-thread safe: it touches no live
    // KnownPluginList. `blacklistSnapshot` is the set of paths to skip,
    // captured by the CALLER on the message thread (crash-hunt #6). onProgress
    // is invoked before each path with (currentIndex, total, currentPath).
    using ProgressCallback = std::function<void(int currentIndex, int total, const juce::String& currentPath)>;
    std::vector<ScanResult> scanAllDefaultPaths(ProgressCallback onProgress,
                                                juce::StringArray blacklistSnapshot,
                                                AbortPredicate shouldAbort = {});

    // Scan the given paths, skipping the blacklisted ones and, once, the one
    // the scan-in-flight marker names.
    std::vector<ScanResult> scanPaths(const juce::StringArray& paths,
                                      const ProgressCallback& onProgress,
                                      const juce::StringArray& blacklistSnapshot,
                                      const AbortPredicate& shouldAbort = {});

    // Commit scan results to the host's KnownPluginList: addType for each
    // description (instruments included), addToBlacklist for each
    // shouldBlacklist result. Returns the number of descriptors added.
    // MESSAGE-THREAD ONLY — the single writer of KnownPluginList on the scan
    // path.
    int applyScanResults(const std::vector<ScanResult>& results);

    // The scan-in-flight marker (M5 U6, plan KD8): the path being scanned is
    // written here before its helper starts and cleared when it's done. When
    // the host dies mid-scan, the next scan finds the path there and skips it
    // once (Outcome::SkippedLastTime), rather than blacklisting a plugin that
    // may have done nothing wrong; the scan after that tries it again. Unset
    // (the default): no marker.
    void setInFlightMarker(juce::File marker) { inFlightMarker_ = std::move(marker); }

    // How long one helper may run: 30 s, or 90 s for a shell bundle holding many
    // plugins behind one file (Waves' WaveShell). Tests shorten both.
    static constexpr int kPerPluginTimeoutMs = 30000;
    static constexpr int kShellTimeoutMs = 90000;
    void setTimeouts(int perPluginMs, int shellMs) noexcept;
    int timeoutFor(const juce::String& pluginPath) const;
    static bool isShellBundle(const juce::String& pluginPath);

private:
    // Run the subprocess for one path, filling `result` (outcome, error,
    // descriptions). `shouldAbort`, when set and observed true, kills the
    // child and bails.
    void runHelper(const juce::String& pluginPath, ScanResult& result, const AbortPredicate& shouldAbort) const;

    PluginHost& host_;
    juce::File helperBinary_;
    juce::File inFlightMarker_;
    int perPluginTimeoutMs_{kPerPluginTimeoutMs};
    int shellTimeoutMs_{kShellTimeoutMs};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanner)
};

// Validate + parse the helper's result into `result` (exit code already
// checked by the caller). Requires kScanOkSentinel — absence means the helper
// died after writing its result (teardown crash with POSIX exit code 0), so the
// scan counts as a crash: descriptions stay empty and the path gets
// blacklisted, never added. Sets result.outcome to Found, Instrument or
// NotAPlugin on success. Free function so the contract is unit-testable with
// arbitrary strings, cross-platform, without spawning a subprocess.
bool parseHelperOutput(const juce::String& resultText, PluginScanner::ScanResult& result);

} // namespace pedalboard3::engine
