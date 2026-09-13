// Tests for pedalboard3::engine::PluginScanner -- the parent-side of the
// out-of-process plugin scanning rig (master plan §4.2.A). The actual
// subprocess work happens in pb3-scanner-helper; PluginScanner orchestrates
// helper invocation, timeout handling, and KnownPluginList mutation.
//
// Three layers of coverage:
//
//   1. Unit-level -- no helper binary needed: the negative paths, the
//      result parsing, the blacklist semantics and the helper lookup.
//
//   2. With pb3-fake-scanner-helper (tests/support/FakeScannerHelper.cpp,
//      M5 U6): plain C++ with no JUCE, so it runs on a headless runner. It
//      hangs, crashes, floods its output or reports an instrument on demand.
//
//   3. Integration with the real pb3-scanner-helper -- gated on the
//      helper binary being reachable, and skipped when it can't run
//      (headless CI).

#include <catch2/catch_test_macros.hpp>

#include "engine/PluginHost.h"
#include "engine/PluginScanner.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <atomic>

using namespace pedalboard3;
using Outcome = engine::PluginScanner::Outcome;

namespace
{

constexpr auto kBogusPluginPath = "/totally/nonexistent/Plugin.vst3";

juce::File makeUnreachableHelperFile()
{
    return juce::File("/totally/nonexistent/pb3-scanner-helper");
}

// A helper path the build pinned for us in a locator text file (see
// tests/CMakeLists.txt): written at build time with forward slashes so it
// survives string-literal embedding on Windows. Empty when absent.
juce::File helperFromLocator([[maybe_unused]] const char* locatorPath)
{
    juce::File locator(juce::String::fromUTF8(locatorPath));
    if (!locator.existsAsFile())
        return {};
    const auto helperPath = locator.loadFileAsString().trim();
    return helperPath.isEmpty() ? juce::File() : juce::File(helperPath);
}

juce::File makeRealHelperFile()
{
#ifdef PB3_TEST_SCANNER_HELPER_LOCATOR
    return helperFromLocator(PB3_TEST_SCANNER_HELPER_LOCATOR);
#else
    return {};
#endif
}

juce::File makeFakeHelperFile()
{
#ifdef PB3_TEST_FAKE_SCANNER_HELPER_LOCATOR
    return helperFromLocator(PB3_TEST_FAKE_SCANNER_HELPER_LOCATOR);
#else
    return {};
#endif
}

// Drop a 1-line text file under tempDirectory and return its path. Used
// as a "definitely not a plugin" target the helper can run against
// without crashing.
juce::File makeNonPluginTempFile(const juce::String& nameTag)
{
    auto f =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("pb3-scanner-test-" + nameTag + "-" + juce::String(juce::Time::currentTimeMillis()) + ".txt");
    f.replaceWithText("not a plugin");
    return f;
}

juce::PluginDescription fakeDescription(const juce::String& name, bool instrument)
{
    juce::PluginDescription d;
    d.name = name;
    d.pluginFormatName = "VST3";
    d.fileOrIdentifier = "/plugins/" + name + ".vst3";
    d.isInstrument = instrument;
    return d;
}

} // namespace

// -----------------------------------------------------------------------
// Unit-level -- no helper required
// -----------------------------------------------------------------------

TEST_CASE("PluginScanner: isHelperAvailable false when constructed with empty File", "[engine][plugin-scanner]")
{
    engine::PluginHost host;
    engine::PluginScanner scanner(host, juce::File{});
    REQUIRE_FALSE(scanner.isHelperAvailable());
}

TEST_CASE("PluginScanner: isHelperAvailable false when binary path doesn't exist", "[engine][plugin-scanner]")
{
    engine::PluginHost host;
    engine::PluginScanner scanner(host, makeUnreachableHelperFile());
    REQUIRE_FALSE(scanner.isHelperAvailable());
}

TEST_CASE("PluginScanner: scanFile without a helper is never blacklisted", "[engine][plugin-scanner]")
{
    engine::PluginHost host;
    engine::PluginScanner scanner(host, juce::File{});

    auto result = scanner.scanFile(kBogusPluginPath);

    REQUIRE(result.outcome == Outcome::NoHelper);
    REQUIRE(result.helperCrashed);
    REQUIRE_FALSE(result.timedOut);
    REQUIRE(result.descriptions.isEmpty());
    REQUIRE(result.error.isNotEmpty());
    REQUIRE(result.pluginPath == kBogusPluginPath);

    // Never attempted (no helper): a future scan retries the path rather than
    // poisoning it permanently.
    REQUIRE_FALSE(result.shouldBlacklist);
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().isEmpty());
}

TEST_CASE("PluginScanner: applyScanResults adds effects and instruments, and blacklists only failures",
          "[engine][plugin-scanner]")
{
    engine::PluginHost host;
    engine::PluginScanner scanner(host, juce::File{});

    engine::PluginScanner::ScanResult effect;
    effect.pluginPath = "/plugins/Good.vst3";
    effect.outcome = Outcome::Found;
    effect.descriptions.add(fakeDescription("Good", false));

    // An instrument is kept in the list, not blacklisted (M5 U6, plan KD8).
    engine::PluginScanner::ScanResult instrument;
    instrument.pluginPath = "/plugins/Synth.vst3";
    instrument.outcome = Outcome::Instrument;
    instrument.descriptions.add(fakeDescription("Synth", true));

    engine::PluginScanner::ScanResult failed;
    failed.pluginPath = "/plugins/Bad.vst3";
    failed.outcome = Outcome::Crashed;
    failed.shouldBlacklist = true;

    engine::PluginScanner::ScanResult cancelled;
    cancelled.pluginPath = "/plugins/Cancelled.vst3";
    cancelled.outcome = Outcome::Cancelled;

    const int added = scanner.applyScanResults({effect, instrument, failed, cancelled});

    REQUIRE(added == 2);
    REQUIRE(host.getKnownPlugins().size() == 2);
    REQUIRE(host.getKnownEffects().size() == 1);
    const auto& blacklist = host.getKnownPluginList().getBlacklistedFiles();
    CHECK(blacklist.contains("/plugins/Bad.vst3"));
    CHECK_FALSE(blacklist.contains("/plugins/Synth.vst3"));
    CHECK_FALSE(blacklist.contains("/plugins/Cancelled.vst3"));
    CHECK_FALSE(blacklist.contains("/plugins/Good.vst3"));
}

TEST_CASE("PluginScanner: scanAllDefaultPaths returns no results when no helper is available",
          "[engine][plugin-scanner]")
{
    engine::PluginHost host;
    engine::PluginScanner scanner(host, juce::File{});

    int progressCalls = 0;
    const auto results =
        scanner.scanAllDefaultPaths([&progressCalls](int, int, const juce::String&) { ++progressCalls; }, {});

    REQUIRE(results.empty());
    REQUIRE(progressCalls == 0); // no scan loop runs without helper
}

TEST_CASE("PluginScanner: the helper is found beside the app, and only there (M5 U6)", "[engine][plugin-scanner]")
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("pb3-helper-lookup-" + juce::String(juce::Time::currentTimeMillis()));
    const auto app = dir.getChildFile("app").getChildFile("Pedalboard 3");
    REQUIRE(app.getParentDirectory().createDirectory().wasOk());

#if JUCE_WINDOWS
    const juce::String helperName = "pb3-scanner-helper.exe";
#else
    const juce::String helperName = "pb3-scanner-helper";
#endif

    // One further up the tree is never picked up.
    REQUIRE(dir.getChildFile(helperName).replaceWithText("elsewhere"));
    REQUIRE(engine::PluginScanner::findHelperBinary(app) == juce::File());

    // One beside the app is.
    const auto sibling = app.getSiblingFile(helperName);
    REQUIRE(sibling.replaceWithText("helper"));
    REQUIRE(engine::PluginScanner::findHelperBinary(app) == sibling);

    dir.deleteRecursively();
}

TEST_CASE("PluginScanner: a shell bundle gets 90 s, anything else 30 s (M5 U6)", "[engine][plugin-scanner]")
{
    engine::PluginHost host;
    engine::PluginScanner scanner(host, juce::File{});
    REQUIRE(scanner.timeoutFor("/Library/Audio/Plug-Ins/VST3/WaveShell1-VST3 14.12.vst3") ==
            engine::PluginScanner::kShellTimeoutMs);
    REQUIRE(scanner.timeoutFor("/plugins/Pro-Q 3.vst3") == engine::PluginScanner::kPerPluginTimeoutMs);
    REQUIRE(engine::PluginScanner::kShellTimeoutMs == 90000);
}

// -----------------------------------------------------------------------
// Parsing -- no helper required
// -----------------------------------------------------------------------

TEST_CASE("descriptionsFromScanXml keeps effects and instruments (M5 U6)", "[engine][plugin-scanner]")
{
    const auto effect = fakeDescription("DemoEQ", false);
    const auto instrument = fakeDescription("DemoSynth", true);

    SECTION("a bundle exposing both keeps both")
    {
        juce::XmlElement root("PB3_SCAN_RESULT");
        root.addChildElement(effect.createXml().release());
        root.addChildElement(instrument.createXml().release());
        REQUIRE(engine::descriptionsFromScanXml(root).size() == 2);
    }

    SECTION("an instrument-only bundle keeps its instrument")
    {
        juce::XmlElement root("PB3_SCAN_RESULT");
        root.addChildElement(instrument.createXml().release());
        const auto kept = engine::descriptionsFromScanXml(root);
        REQUIRE(kept.size() == 1);
        REQUIRE(kept[0].isInstrument);
    }

    SECTION("an empty <PB3_SCAN_RESULT/> yields an empty array")
    {
        juce::XmlElement root("PB3_SCAN_RESULT");
        REQUIRE(engine::descriptionsFromScanXml(root).isEmpty());
    }
}

// A signal-killed helper reports exit code 0 on POSIX, so parseHelperOutput
// must refuse a result that lacks the completion sentinel even when the XML
// parses. This is the seam that stops a teardown-crashing plugin from being
// whitelisted and later constructed in the host.
TEST_CASE("parseHelperOutput requires the completion sentinel, and records the outcome", "[engine][plugin-scanner]")
{
    const auto effect = fakeDescription("DemoEQ", false);
    juce::XmlElement root("PB3_SCAN_RESULT");
    root.addChildElement(effect.createXml().release());
    const auto xml = root.toString(juce::XmlElement::TextFormat{}.singleLine());

    SECTION("valid XML followed by the sentinel is accepted")
    {
        engine::PluginScanner::ScanResult result;
        REQUIRE(engine::parseHelperOutput(xml + "\n" + engine::kScanOkSentinel + "\n", result));
        REQUIRE_FALSE(result.helperCrashed);
        REQUIRE(result.outcome == Outcome::Found);
        REQUIRE(result.descriptions.size() == 1);
        CHECK(result.descriptions[0].name == "DemoEQ");
    }

    SECTION("valid XML WITHOUT the sentinel is treated as a teardown crash")
    {
        engine::PluginScanner::ScanResult result;
        REQUIRE_FALSE(engine::parseHelperOutput(xml + "\n", result));
        REQUIRE(result.helperCrashed);
        REQUIRE(result.outcome == Outcome::Crashed);
        REQUIRE(result.descriptions.isEmpty());
    }

    SECTION("the sentinel alone (no PB3_SCAN_RESULT) still fails to parse")
    {
        engine::PluginScanner::ScanResult result;
        REQUIRE_FALSE(engine::parseHelperOutput(juce::String(engine::kScanOkSentinel) + "\n", result));
        REQUIRE(result.helperCrashed);
    }

    SECTION("an empty scan result with the sentinel is not a plugin")
    {
        juce::XmlElement empty("PB3_SCAN_RESULT");
        const auto emptyXml = empty.toString(juce::XmlElement::TextFormat{}.singleLine());
        engine::PluginScanner::ScanResult result;
        REQUIRE(engine::parseHelperOutput(emptyXml + "\n" + engine::kScanOkSentinel, result));
        REQUIRE(result.descriptions.isEmpty());
        REQUIRE(result.outcome == Outcome::NotAPlugin);
    }
}

// -----------------------------------------------------------------------
// With the fake helper (M5 U6)
// -----------------------------------------------------------------------

#define PB3_REQUIRE_FAKE_HELPER(helper)                                                                                \
    if (!(helper).existsAsFile())                                                                                      \
    SKIP("pb3-fake-scanner-helper not built alongside tests")

TEST_CASE("PluginScanner: an effect is found, and an instrument-only bundle is kept, not blacklisted (M5 U6)",
          "[engine][plugin-scanner][fake-helper]")
{
    const auto helper = makeFakeHelperFile();
    PB3_REQUIRE_FAKE_HELPER(helper);
    engine::PluginHost host;
    engine::PluginScanner scanner(host, helper);

    const auto effect = scanner.scanFile("/fake/effect.vst3");
    REQUIRE(effect.outcome == Outcome::Found);
    REQUIRE(effect.descriptions.size() == 1);
    REQUIRE_FALSE(effect.shouldBlacklist);

    const auto instrument = scanner.scanFile("/fake/instrument.vst3");
    REQUIRE(instrument.outcome == Outcome::Instrument);
    REQUIRE_FALSE(instrument.shouldBlacklist);

    scanner.applyScanResults({effect, instrument});
    REQUIRE(host.getKnownPlugins().size() == 2);
    REQUIRE(host.getKnownEffects().size() == 1);
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().isEmpty());
}

TEST_CASE("PluginScanner: a crashing helper records \"crashed\" and is blacklisted (M5 U6)",
          "[engine][plugin-scanner][fake-helper]")
{
    const auto helper = makeFakeHelperFile();
    PB3_REQUIRE_FAKE_HELPER(helper);
    engine::PluginHost host;
    engine::PluginScanner scanner(host, helper);

    const auto result = scanner.scanFile("/fake/crash.vst3");
    REQUIRE(result.outcome == Outcome::Crashed);
    REQUIRE(result.shouldBlacklist);
}

TEST_CASE("PluginScanner: a 1 MB result, with 1 MB printed besides, is read without a deadlock (M5 U6)",
          "[engine][plugin-scanner][fake-helper]")
{
    const auto helper = makeFakeHelperFile();
    PB3_REQUIRE_FAKE_HELPER(helper);
    engine::PluginHost host;
    engine::PluginScanner scanner(host, helper);
    scanner.setTimeouts(10000, 10000);

    const auto result = scanner.scanFile("/fake/flood.vst3");
    INFO(result.error);
    REQUIRE(result.outcome == Outcome::Found);
    REQUIRE_FALSE(result.timedOut);
    REQUIRE(result.descriptions.size() == 1);
    REQUIRE(result.descriptions[0].descriptiveName.length() == 1024 * 1024);
}

TEST_CASE("PluginScanner: a helper that hangs is killed at the timeout and reaped (M5 U6)",
          "[engine][plugin-scanner][fake-helper]")
{
    const auto helper = makeFakeHelperFile();
    PB3_REQUIRE_FAKE_HELPER(helper);
    engine::PluginHost host;
    engine::PluginScanner scanner(host, helper);
    scanner.setTimeouts(1500, 1500);

    const auto result = scanner.scanFile("/fake/hang.vst3");
    REQUIRE(result.outcome == Outcome::TimedOut);
    REQUIRE(result.timedOut);
    REQUIRE(result.shouldBlacklist);

#if JUCE_LINUX
    // The hung helper wrote its pid beside the (now deleted) result file. Once
    // it's reaped, /proc has no entry for it; a zombie would still be listed.
    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto pidFiles = temp.findChildFiles(juce::File::findFiles, false, "pb3-scan-result*.xml.pid");
    REQUIRE_FALSE(pidFiles.isEmpty());
    for (const auto& pidFile : pidFiles)
    {
        const auto pid = pidFile.loadFileAsString().trim();
        REQUIRE(pid.getIntValue() > 0);
        REQUIRE_FALSE(juce::File("/proc/" + pid).exists());
        pidFile.deleteFile();
    }
#endif
}

TEST_CASE("PluginScanner: a cancelled scan kills the helper and blacklists nothing (M5 U6)",
          "[engine][plugin-scanner][fake-helper]")
{
    const auto helper = makeFakeHelperFile();
    PB3_REQUIRE_FAKE_HELPER(helper);
    engine::PluginHost host;
    engine::PluginScanner scanner(host, helper);

    std::atomic<int> polls{0};
    const auto result = scanner.scanFile("/fake/hang.vst3", [&polls] { return ++polls > 3; });
    REQUIRE(result.outcome == Outcome::Cancelled);
    REQUIRE_FALSE(result.shouldBlacklist);

    scanner.applyScanResults({result});
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().isEmpty());

    for (const auto& pidFile : juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .findChildFiles(juce::File::findFiles, false, "pb3-scan-result*.xml.pid"))
        pidFile.deleteFile();
}

TEST_CASE("PluginScanner: a marker left by a dead scan makes the next one skip that path, once (M5 U6)",
          "[engine][plugin-scanner][fake-helper]")
{
    const auto helper = makeFakeHelperFile();
    PB3_REQUIRE_FAKE_HELPER(helper);
    engine::PluginHost host;
    engine::PluginScanner scanner(host, helper);

    const auto marker = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("pb3-scan-in-flight", ".txt", false);
    scanner.setInFlightMarker(marker);
    REQUIRE(marker.replaceWithText("/fake/died-effect.vst3")); // the host died scanning it

    const juce::StringArray paths{"/fake/died-effect.vst3", "/fake/other-effect.vst3"};
    const auto first = scanner.scanPaths(paths, {}, {});
    REQUIRE(first.size() == 2);
    REQUIRE(first[0].outcome == Outcome::SkippedLastTime);
    REQUIRE_FALSE(first[0].shouldBlacklist);
    REQUIRE(first[1].outcome == Outcome::Found);
    REQUIRE_FALSE(marker.existsAsFile()); // the scan finished: nothing in flight

    scanner.applyScanResults(first);
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().isEmpty());

    // The scan after that tries it again.
    const auto second = scanner.scanPaths(paths, {}, {});
    REQUIRE(second[0].outcome == Outcome::Found);
}

// -----------------------------------------------------------------------
// Integration with the real pb3-scanner-helper binary
// -----------------------------------------------------------------------

// CI runners (Linux + macOS) are typically headless and the helper's
// juce::ScopedJuceInitialiser_GUI may not initialise cleanly without a
// display -- in that case the subprocess fails and PluginScanner reports it
// as a crash. We treat that as a SKIP rather than a hard failure: the
// integration coverage we're after still applies on dev boxes (and Windows
// runners, which have a session).
#define PB3_SKIP_IF_HELPER_CRASHED(result)                                                                             \
    INFO("helperCrashed=" << (result).helperCrashed << " timedOut=" << (result).timedOut << " error='"                 \
                          << (result).error.toStdString() << "'");                                                     \
    if ((result).helperCrashed)                                                                                        \
    SKIP("helper subprocess failed to run cleanly (likely headless CI); covered by on-host smoke")

TEST_CASE("PluginScanner: real helper against a non-plugin file blacklists it as not a plugin",
          "[engine][plugin-scanner][integration]")
{
    const auto helper = makeRealHelperFile();
    if (!helper.existsAsFile())
        SKIP("pb3-scanner-helper not built alongside tests; integration test skipped");

    engine::PluginHost host;
    engine::PluginScanner scanner(host, helper);
    REQUIRE(scanner.isHelperAvailable());

    auto target = makeNonPluginTempFile("non-plugin");
    REQUIRE(target.existsAsFile());

    auto result = scanner.scanFile(target.getFullPathName());
    PB3_SKIP_IF_HELPER_CRASHED(result);

    REQUIRE_FALSE(result.timedOut);
    REQUIRE(result.outcome == Outcome::NotAPlugin);
    REQUIRE(result.descriptions.isEmpty());
    REQUIRE(result.shouldBlacklist);

    scanner.applyScanResults({result});
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().contains(target.getFullPathName()));

    target.deleteFile();
}

TEST_CASE("PluginScanner: real helper against a non-existent path still produces a clean result",
          "[engine][plugin-scanner][integration]")
{
    const auto helper = makeRealHelperFile();
    if (!helper.existsAsFile())
        SKIP("pb3-scanner-helper not built alongside tests; integration test skipped");

    engine::PluginHost host;
    engine::PluginScanner scanner(host, helper);

    auto result = scanner.scanFile("/totally/nonexistent/path/Plugin.vst3");
    PB3_SKIP_IF_HELPER_CRASHED(result);

    REQUIRE_FALSE(result.timedOut);
    REQUIRE(result.descriptions.isEmpty());
    REQUIRE(result.shouldBlacklist);

    scanner.applyScanResults({result});
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().contains("/totally/nonexistent/path/Plugin.vst3"));
}

TEST_CASE("PluginScanner: real helper -- repeat scan of same failing path doesn't double-blacklist",
          "[engine][plugin-scanner][integration]")
{
    const auto helper = makeRealHelperFile();
    if (!helper.existsAsFile())
        SKIP("pb3-scanner-helper not built alongside tests; integration test skipped");

    engine::PluginHost host;
    engine::PluginScanner scanner(host, helper);

    auto target = makeNonPluginTempFile("dedupe");
    REQUIRE(target.existsAsFile());
    const auto path = target.getFullPathName();

    auto firstResult = scanner.scanFile(path);
    PB3_SKIP_IF_HELPER_CRASHED(firstResult);

    scanner.applyScanResults({firstResult});
    const auto sizeAfterFirst = host.getKnownPluginList().getBlacklistedFiles().size();
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().contains(path));

    auto secondResult = scanner.scanFile(path);
    scanner.applyScanResults({secondResult});
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().size() == sizeAfterFirst);

    target.deleteFile();
}
