#include "PluginScanner.h"

#include <unordered_set>

namespace pedalboard3::engine
{

namespace
{

// Wrap an arbitrary path arg for execv-style argument passing. JUCE's
// ChildProcess.start(StringArray) handles quoting on each platform; we
// just supply argv as a StringArray and let it do the right thing.
juce::StringArray buildHelperArgs(const juce::File& helper, const juce::String& pluginPath)
{
    juce::StringArray args;
    args.add(helper.getFullPathName());
    args.add(pluginPath);
    return args;
}

// Try to locate pb3-scanner-helper alongside the running binary.
// Search order matches header doc.
juce::File locateHelper()
{
    const auto self = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

    // The executable name is platform-dependent: Windows appends ".exe".
    // BOTH lookups below must use the right name — otherwise on Windows the
    // helper is silently "not found" even though it built fine, and the host
    // falls back to the crash-prone in-process scan (the exact symptom this
    // fixes). macOS/Linux helpers have no extension.
#if JUCE_WINDOWS
    const juce::String helperName = "pb3-scanner-helper.exe";
#else
    const juce::String helperName = "pb3-scanner-helper";
#endif

    // 1. Sibling of the host binary (production install, or the post-build
    //    copy next to the executable / into the .app's Contents/MacOS/).
    const auto sibling = self.getSiblingFile(helperName);
    if (sibling.existsAsFile())
        return sibling;

    // 2. JUCE in-tree dev layout: each target has its own artefacts subtree
    //    under build/<preset>/src/<TargetName>_artefacts/<config>/. The host
    //    and helper live in SEPARATE artefacts trees, so walk up to find the
    //    sibling pb3-scanner-helper_artefacts directory and search it.
    auto walker = self;
    for (int i = 0; i < 6 && walker.exists(); ++i)
    {
        const auto candidate = walker.getSiblingFile("pb3-scanner-helper_artefacts");
        if (candidate.isDirectory())
        {
            // <config>/ subfolder is typically Debug / Release / RelWithDebInfo.
            auto found = candidate.findChildFiles(juce::File::findFiles, /*recursive*/ true, helperName);
            if (!found.isEmpty())
                return found.getReference(0);
        }
        walker = walker.getParentDirectory();
    }

    return {};
}

} // namespace

juce::Array<juce::PluginDescription> effectDescriptionsFromScanXml(const juce::XmlElement& scanResult)
{
    juce::Array<juce::PluginDescription> descriptions;

    // Edge case: a single .vst3 bundle that exposes BOTH effect AND
    // instrument types (rare but legal in the VST3 spec) keeps the
    // effect descriptor and drops only the instrument one.
    //
    // Edge case: a bundle that exposes ONLY instrument types ends up
    // with an empty array — scanFile then blacklists the path so future
    // rescans skip it. v3.1 instrument support would un-blacklist via the
    // Plugin List dialog if added later.
    for (auto* desc : scanResult.getChildIterator())
    {
        juce::PluginDescription d;
        if (d.loadFromXml(*desc) && !d.isInstrument)
            descriptions.add(d);
    }

    return descriptions;
}

PluginScanner::PluginScanner(PluginHost& host, juce::File helperBinary)
    : host_(host), helperBinary_(std::move(helperBinary))
{
}

juce::File PluginScanner::findHelperBinary()
{
    return locateHelper();
}

bool PluginScanner::isHelperAvailable() const noexcept
{
    return helperBinary_.existsAsFile();
}

bool PluginScanner::runHelper(const juce::String& pluginPath, ScanResult& result) const
{
    juce::ChildProcess child;

    if (!child.start(buildHelperArgs(helperBinary_, pluginPath),
                     juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        result.error = "failed to spawn pb3-scanner-helper";
        result.helperCrashed = true;
        return false;
    }

    if (!child.waitForProcessToFinish(kPerPluginTimeoutMs))
    {
        child.kill();
        result.timedOut = true;
        result.error = "scan timed out after " + juce::String(kPerPluginTimeoutMs / 1000) + "s";
        return false;
    }

    const auto stdoutText = child.readAllProcessOutput();
    const auto exitCode = child.getExitCode();

    if (exitCode != 0)
    {
        result.helperCrashed = true;
        // Include up to 256 chars of merged stdout+stderr so a UI
        // surfacing this error has something diagnosable beyond the
        // exit code (and so test failures on CI tell us *why* the
        // helper died rather than just that it did).
        result.error =
            "helper exited with code " + juce::String(exitCode) + "; output: " + stdoutText.substring(0, 256).trim();
        return false;
    }

    return parseHelperOutput(stdoutText, result);
}

bool parseHelperOutput(const juce::String& stdoutText, PluginScanner::ScanResult& result)
{
    // Sentinel gate first (crash-hunt #6): the helper prints kScanOkSentinel
    // only after its JUCE teardown completed. A signal-killed helper reports
    // exit code 0 on POSIX, and a plugin can flush valid descriptors before
    // dying in teardown — so exit code + parseable XML alone would whitelist
    // a plugin that then crashes the host in-process. No sentinel = crash;
    // descriptions stay empty so the caller blacklists the path.
    const int sentinelPos = stdoutText.indexOf(kScanOkSentinel);
    if (sentinelPos < 0)
    {
        result.error = "helper died before printing the completion sentinel (teardown crash?); output: " +
                       stdoutText.substring(0, 256).trim();
        result.helperCrashed = true;
        return false;
    }

    auto root = juce::parseXML(stdoutText.substring(0, sentinelPos));
    if (root == nullptr || !root->hasTagName("PB3_SCAN_RESULT"))
    {
        result.error = "helper produced unparseable output: " + stdoutText.substring(0, 256).trim();
        result.helperCrashed = true;
        return false;
    }

    // Drop instrument plugins at the source — v3.0 is an effects-only
    // host (master plan §4.2.D). Extracted to a free function so the
    // filtering invariant is unit-testable without spawning the helper.
    result.descriptions = effectDescriptionsFromScanXml(*root);

    return true;
}

PluginScanner::ScanResult PluginScanner::scanFile(const juce::String& pluginPath)
{
    ScanResult result;
    result.pluginPath = pluginPath;

    if (!isHelperAvailable())
    {
        result.error = "pb3-scanner-helper binary not found";
        result.helperCrashed = true;
        // Never attempted — leave shouldBlacklist false so a future scan
        // (once a helper is present) retries this path rather than poisoning
        // it permanently.
        return result;
    }

    const bool ok = runHelper(pluginPath, result);

    // Decide intent ONLY — do not touch KnownPluginList here. The helper ran,
    // so a crash / timeout / no-effect-descriptors outcome means this path
    // should be blacklisted. The actual list mutation happens in
    // applyScanResults() on the message thread (KnownPluginList is not
    // thread-safe and scanFile may run on a worker thread).
    result.shouldBlacklist = (!ok || result.descriptions.isEmpty());

    return result;
}

int PluginScanner::applyScanResults(const std::vector<ScanResult>& results)
{
    // MESSAGE THREAD ONLY. Single writer of KnownPluginList for the scan
    // path; runs serialised with the list's ChangeListeners (which also fire
    // on the message thread), so no data race.
    auto& list = host_.getKnownPluginList();
    int added = 0;

    for (const auto& result : results)
    {
        if (!result.descriptions.isEmpty())
        {
            // addType dedupes by identifier, so re-adding is a no-op.
            for (const auto& d : result.descriptions)
                list.addType(d);
            added += result.descriptions.size();
        }
        else if (result.shouldBlacklist)
        {
            if (!list.getBlacklistedFiles().contains(result.pluginPath))
                list.addToBlacklist(result.pluginPath);
        }
    }

    return added;
}

std::vector<PluginScanner::ScanResult> PluginScanner::scanAllDefaultPaths(ProgressCallback onProgress,
                                                                          juce::StringArray blacklistSnapshot)
{
    std::vector<ScanResult> results;
    if (!isHelperAvailable())
        return results;

    // Enumerate every (format, path) pair, then dedupe by path so a
    // plugin under more than one search path only gets scanned once.
    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();

    juce::StringArray uniquePaths;
    std::unordered_set<std::string> seen;

    for (int f = 0; f < formats.getNumFormats(); ++f)
    {
        auto* format = formats.getFormat(f);
        if (format == nullptr || !format->canScanForPlugins())
            continue;

        const auto search = format->getDefaultLocationsToSearch();
        const auto paths = format->searchPathsForPlugins(search, /*recursive*/ true, /*allowAsync*/ false);

        for (const auto& path : paths)
        {
            if (seen.insert(path.toStdString()).second)
                uniquePaths.add(path);
        }
    }

    // The blacklist snapshot arrives from the CALLER, captured on the message
    // thread (crash-hunt #6): reading the live KnownPluginList here — this
    // method runs on the scan worker — raced concurrent addToBlacklist calls
    // from the message thread (instrument sweep, Plugin List dialog). Nothing
    // in this method touches the live list; results are committed later via
    // applyScanResults on the message thread.
    const juce::StringArray& blacklisted = blacklistSnapshot;

    const int total = uniquePaths.size();
    results.reserve(static_cast<size_t>(total));

    for (int i = 0; i < total; ++i)
    {
        const auto& path = uniquePaths.getReference(i);
        if (onProgress)
            onProgress(i, total, path);

        // Skip paths the user (or a previous scan) already blacklisted.
        if (blacklisted.contains(path))
            continue;

        results.push_back(scanFile(path));
    }

    return results;
}

} // namespace pedalboard3::engine
