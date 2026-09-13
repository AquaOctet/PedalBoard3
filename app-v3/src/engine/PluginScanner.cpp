#include "PluginScanner.h"

#include <unordered_set>

namespace pedalboard3::engine
{

namespace
{

// The executable name is platform-dependent: Windows appends ".exe".
#if JUCE_WINDOWS
constexpr const char* kHelperName = "pb3-scanner-helper.exe";
#else
constexpr const char* kHelperName = "pb3-scanner-helper";
#endif

constexpr int kPollSliceMs = 100;
// How long to wait for a killed helper to go, so it's reaped (on POSIX,
// waitForProcessToFinish polls waitpid) rather than left a zombie.
constexpr int kReapWaitMs = 5000;

// argv for the helper: its own path, the plugin, and the file it writes its
// result to. JUCE's ChildProcess::start(StringArray) handles quoting.
juce::StringArray buildHelperArgs(const juce::File& helper,
                                  const juce::String& pluginPath,
                                  const juce::File& resultFile)
{
    juce::StringArray args;
    args.add(helper.getFullPathName());
    args.add(pluginPath);
    args.add(resultFile.getFullPathName());
    return args;
}

void killAndReap(juce::ChildProcess& child)
{
    child.kill();
    child.waitForProcessToFinish(kReapWaitMs);
}

} // namespace

juce::Array<juce::PluginDescription> descriptionsFromScanXml(const juce::XmlElement& scanResult)
{
    juce::Array<juce::PluginDescription> descriptions;
    for (auto* desc : scanResult.getChildIterator())
    {
        juce::PluginDescription d;
        if (d.loadFromXml(*desc))
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
    return findHelperBinary(juce::File::getSpecialLocation(juce::File::currentExecutableFile));
}

juce::File PluginScanner::findHelperBinary(const juce::File& executable)
{
    // Only beside the app (M5 U6, plan KD8), never searched for further up the
    // folder tree, where a binary of that name could be anything.
    const auto sibling = executable.getSiblingFile(kHelperName);
    return sibling.existsAsFile() ? sibling : juce::File();
}

bool PluginScanner::isHelperAvailable() const noexcept
{
    return helperBinary_.existsAsFile();
}

void PluginScanner::setTimeouts(int perPluginMs, int shellMs) noexcept
{
    perPluginTimeoutMs_ = juce::jmax(kPollSliceMs, perPluginMs);
    shellTimeoutMs_ = juce::jmax(kPollSliceMs, shellMs);
}

bool PluginScanner::isShellBundle(const juce::String& pluginPath)
{
    return juce::File(pluginPath).getFileName().containsIgnoreCase("shell");
}

int PluginScanner::timeoutFor(const juce::String& pluginPath) const
{
    return isShellBundle(pluginPath) ? shellTimeoutMs_ : perPluginTimeoutMs_;
}

void PluginScanner::runHelper(const juce::String& pluginPath,
                              ScanResult& result,
                              const AbortPredicate& shouldAbort) const
{
    // The helper writes its result to this file and runs with no pipes at all
    // (M5 U6, plan KD8): a big result, or a plugin that prints a lot, can't
    // fill a pipe and block the helper until the timeout blacklists a working
    // plugin. (Without wantStdOut / wantStdErr, JUCE points the child's output
    // at /dev/null on POSIX and at nothing on Windows.)
    const auto resultFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getNonexistentChildFile("pb3-scan-result", ".xml", false);
    const juce::ScopeGuard removeResult{[&resultFile] { resultFile.deleteFile(); }};

    juce::ChildProcess child;
    if (!child.start(buildHelperArgs(helperBinary_, pluginPath, resultFile), 0))
    {
        // The plugin was never tried, so it isn't blamed for it.
        result.error = "failed to spawn pb3-scanner-helper";
        result.helperCrashed = true;
        result.outcome = Outcome::NoHelper;
        return;
    }

    // Poll in short slices instead of one uninterruptible waitForProcessToFinish
    // so a cancel or shutdown (shouldAbort) can kill an in-flight scan and let
    // the owning thread's join return promptly (crash-hunt #8).
    const int budgetMs = timeoutFor(pluginPath);
    bool finished = false;
    for (int waited = 0; waited < budgetMs; waited += kPollSliceMs)
    {
        if (shouldAbort && shouldAbort())
        {
            // The plugin did nothing wrong: it's tried again on the next scan.
            killAndReap(child);
            result.error = "scan cancelled";
            result.outcome = Outcome::Cancelled;
            return;
        }
        if (child.waitForProcessToFinish(kPollSliceMs))
        {
            finished = true;
            break;
        }
    }

    if (!finished)
    {
        killAndReap(child);
        result.timedOut = true;
        result.outcome = Outcome::TimedOut;
        result.error = "scan timed out after " + juce::String(budgetMs / 1000.0, 1) + " s";
        return;
    }

    const auto exitCode = child.getExitCode();
    if (exitCode != 0)
    {
        result.helperCrashed = true;
        result.outcome = Outcome::Crashed;
        result.error = "helper exited with code " + juce::String(exitCode);
        return;
    }

    parseHelperOutput(resultFile.existsAsFile() ? resultFile.loadFileAsString() : juce::String(), result);
}

bool parseHelperOutput(const juce::String& resultText, PluginScanner::ScanResult& result)
{
    // Sentinel gate first (crash-hunt #6): the helper writes kScanOkSentinel
    // only after its JUCE teardown completed. A signal-killed helper reports
    // exit code 0 on POSIX, and a plugin can flush valid descriptors before
    // dying in teardown — so exit code + parseable XML alone would whitelist
    // a plugin that then crashes the host in-process. No sentinel = crash;
    // descriptions stay empty so the caller blacklists the path.
    const int sentinelPos = resultText.indexOf(kScanOkSentinel);
    if (sentinelPos < 0)
    {
        result.error = "helper died before writing the completion sentinel (teardown crash?)";
        result.helperCrashed = true;
        result.outcome = PluginScanner::Outcome::Crashed;
        return false;
    }

    auto root = juce::parseXML(resultText.substring(0, sentinelPos));
    if (root == nullptr || !root->hasTagName("PB3_SCAN_RESULT"))
    {
        result.error = "helper produced unparseable output: " + resultText.substring(0, 256).trim();
        result.helperCrashed = true;
        result.outcome = PluginScanner::Outcome::Crashed;
        return false;
    }

    result.descriptions = descriptionsFromScanXml(*root);
    bool anyEffect = false;
    for (const auto& d : result.descriptions)
        anyEffect = anyEffect || !d.isInstrument;
    if (result.descriptions.isEmpty())
        result.outcome = PluginScanner::Outcome::NotAPlugin;
    else
        result.outcome = anyEffect ? PluginScanner::Outcome::Found : PluginScanner::Outcome::Instrument;
    return true;
}

PluginScanner::ScanResult PluginScanner::scanFile(const juce::String& pluginPath, const AbortPredicate& shouldAbort)
{
    ScanResult result;
    result.pluginPath = pluginPath;

    if (!isHelperAvailable())
    {
        result.error = "pb3-scanner-helper binary not found";
        result.helperCrashed = true;
        result.outcome = Outcome::NoHelper;
        return result;
    }

    runHelper(pluginPath, result, shouldAbort);

    // Decide intent ONLY — the list is changed by applyScanResults() on the
    // message thread. Blacklist what the helper showed to be broken, or not a
    // plugin at all; never a cancelled scan, a missing helper or an instrument
    // (M5 U6, plan KD8).
    result.shouldBlacklist = result.outcome == Outcome::Crashed || result.outcome == Outcome::TimedOut ||
                             result.outcome == Outcome::NotAPlugin;
    return result;
}

int PluginScanner::applyScanResults(const std::vector<ScanResult>& results)
{
    // MESSAGE THREAD ONLY. Single writer of KnownPluginList for the scan path.
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
                                                                          juce::StringArray blacklistSnapshot,
                                                                          AbortPredicate shouldAbort)
{
    if (!isHelperAvailable())
        return {};

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

    return scanPaths(uniquePaths, onProgress, blacklistSnapshot, shouldAbort);
}

std::vector<PluginScanner::ScanResult> PluginScanner::scanPaths(const juce::StringArray& paths,
                                                                const ProgressCallback& onProgress,
                                                                const juce::StringArray& blacklistSnapshot,
                                                                const AbortPredicate& shouldAbort)
{
    std::vector<ScanResult> results;
    if (!isHelperAvailable())
        return results;

    // The path a scan was on when the host died (M5 U6, plan KD8): skipped this
    // once, then tried again by the scan after.
    const bool marking = inFlightMarker_ != juce::File();
    const auto diedOn =
        marking && inFlightMarker_.existsAsFile() ? inFlightMarker_.loadFileAsString().trim() : juce::String();

    const int total = paths.size();
    results.reserve(static_cast<size_t>(total));

    for (int i = 0; i < total; ++i)
    {
        // Bail out of the whole scan on a cancel or shutdown so an owning
        // thread's join is bounded (crash-hunt #8) — checked here between
        // plugins and again while polling each subprocess wait.
        if (shouldAbort && shouldAbort())
            break;

        const auto& path = paths.getReference(i);
        if (onProgress)
            onProgress(i, total, path);

        // Skip paths the user (or a previous scan) already blacklisted. The
        // snapshot was captured on the message thread (crash-hunt #6).
        if (blacklistSnapshot.contains(path))
            continue;

        if (diedOn.isNotEmpty() && path == diedOn)
        {
            ScanResult skipped;
            skipped.pluginPath = path;
            skipped.outcome = Outcome::SkippedLastTime;
            skipped.error = "the last scan stopped while scanning it";
            results.push_back(std::move(skipped));
            continue;
        }

        if (marking)
            inFlightMarker_.replaceWithText(path);
        results.push_back(scanFile(path, shouldAbort));
        if (marking)
            inFlightMarker_.deleteFile();
    }

    if (marking)
        inFlightMarker_.deleteFile(); // the scan ended: nothing is in flight
    return results;
}

} // namespace pedalboard3::engine
