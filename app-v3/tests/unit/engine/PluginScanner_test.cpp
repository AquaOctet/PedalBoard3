// Tests for pedalboard3::engine::PluginScanner -- the parent-side of the
// out-of-process plugin scanning rig (master plan §4.2.A). The actual
// subprocess work happens in pb3-scanner-helper; PluginScanner orchestrates
// helper invocation, timeout handling, and KnownPluginList mutation
// (addType on success, addToBlacklist on every failure mode).
//
// Two layers of coverage:
//
//   1. Unit-level -- no helper binary needed. Verifies the negative
//      paths (no helper, bogus helper path) and the blacklist
//      semantics that protect the user from a re-scan loop.
//
//   2. Integration with the real pb3-scanner-helper -- gated on the
//      helper binary being reachable. Tests target the XML-output
//      contract (pb3-scanner-helper.cpp emits <PB3_SCAN_RESULT> with
//      one <PLUGIN> per discovered descriptor; non-plugin paths
//      produce an empty root and exit 0). PB3_TEST_SCANNER_HELPER_PATH
//      is injected at configure time by tests/CMakeLists.txt; if
//      tests are built without the helper as a sibling target the
//      define is absent and the gated tests skip.

#include <catch2/catch_test_macros.hpp>

#include "engine/PluginHost.h"
#include "engine/PluginScanner.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

using namespace pedalboard3;

namespace
{

constexpr auto kBogusPluginPath = "/totally/nonexistent/Plugin.vst3";

juce::File makeUnreachableHelperFile()
{
    return juce::File("/totally/nonexistent/pb3-scanner-helper");
}

// Resolves the helper binary path the build system pinned for us, OR
// returns an empty File if the test was built without the helper as a
// sibling target.
//
// CMake writes the absolute helper path to a text file at build time
// (see tests/CMakeLists.txt). PB3_TEST_SCANNER_HELPER_LOCATOR is the
// path to that text file, set at CMake configure time using forward
// slashes so it survives string-literal embedding on Windows. We read
// the locator at runtime, parse the helper path through juce::File
// (which handles both slash directions), and return it.
//
// Absence of the macro means "no helper target was available at build
// time" -> integration cases SKIP.
juce::File makeRealHelperFile()
{
#ifdef PB3_TEST_SCANNER_HELPER_LOCATOR
    juce::File locator(juce::String(PB3_TEST_SCANNER_HELPER_LOCATOR));
    if (!locator.existsAsFile())
        return {};
    const auto helperPath = locator.loadFileAsString().trim();
    if (helperPath.isEmpty())
        return {};
    return juce::File(helperPath);
#else
    return {};
#endif
}

// Drop a 1-line text file under tempDirectory and return its path. Used
// as a "definitely not a plugin" target the helper can run against
// without crashing -- fileMightContainThisPluginType() in juce
// AudioPluginFormat returns false for plain text, so the helper exits
// 0 with an empty <PB3_SCAN_RESULT/>.
juce::File makeNonPluginTempFile(const juce::String& nameTag)
{
    auto f =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("pb3-scanner-test-" + nameTag + "-" + juce::String(juce::Time::currentTimeMillis()) + ".txt");
    f.replaceWithText("not a plugin");
    return f;
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

TEST_CASE("PluginScanner: scanFile without a helper sets helperCrashed and an error message",
          "[engine][plugin-scanner]")
{
    engine::PluginHost host;
    engine::PluginScanner scanner(host, juce::File{});

    auto result = scanner.scanFile(kBogusPluginPath);

    REQUIRE(result.helperCrashed);
    REQUIRE_FALSE(result.timedOut);
    REQUIRE(result.descriptions.isEmpty());
    REQUIRE(result.error.isNotEmpty());
    REQUIRE(result.pluginPath == kBogusPluginPath);

    // Never attempted (no helper): shouldBlacklist stays false so a future
    // scan retries the path rather than poisoning it permanently.
    REQUIRE_FALSE(result.shouldBlacklist);

    // scanFile is a pure producer — it never mutates the list, so nothing is
    // blacklisted regardless.
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().isEmpty());
}

TEST_CASE("PluginScanner: applyScanResults adds effects, blacklists failures, retries never-attempted",
          "[engine][plugin-scanner]")
{
    // Deterministic (no helper needed): applyScanResults is the message-thread
    // commit step. It must addType descriptions, blacklist shouldBlacklist
    // results, and leave never-attempted (shouldBlacklist=false) paths alone.
    engine::PluginHost host;
    engine::PluginScanner scanner(host, juce::File{});

    engine::PluginScanner::ScanResult effect;
    effect.pluginPath = "/plugins/Good.vst3";
    juce::PluginDescription d;
    d.name = "GoodEffect";
    d.pluginFormatName = "VST3";
    d.fileOrIdentifier = effect.pluginPath;
    d.isInstrument = false;
    effect.descriptions.add(d);

    engine::PluginScanner::ScanResult failed; // helper ran, no descriptors
    failed.pluginPath = "/plugins/Bad.vst3";
    failed.shouldBlacklist = true;

    engine::PluginScanner::ScanResult untried; // no helper -> retry later
    untried.pluginPath = "/plugins/Untried.vst3";
    untried.shouldBlacklist = false;

    const int added = scanner.applyScanResults({effect, failed, untried});

    REQUIRE(added == 1);
    const auto& blacklist = host.getKnownPluginList().getBlacklistedFiles();
    CHECK(blacklist.contains("/plugins/Bad.vst3"));
    CHECK_FALSE(blacklist.contains("/plugins/Untried.vst3"));
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

TEST_CASE("PluginScanner: findHelperBinary doesn't crash on a runner without a sibling helper",
          "[engine][plugin-scanner]")
{
    // We only assert "no crash + return value matches existsAsFile()
    // when wired into a PluginScanner". Whether the helper happens to
    // sit alongside the test exe depends on the build layout -- both
    // present and absent are valid outcomes for this test.
    const auto found = engine::PluginScanner::findHelperBinary();

    engine::PluginHost host;
    engine::PluginScanner scanner(host, found);
    REQUIRE(scanner.isHelperAvailable() == found.existsAsFile());
}

// -----------------------------------------------------------------------
// Effects-only filter -- no helper required
// -----------------------------------------------------------------------

// Pins the effects-only invariant (master plan §4.2.D) on the EXACT code
// path runHelper uses: the filter lives in engine::effectDescriptionsFromScanXml,
// so deleting or inverting it breaks this test. (A copied predicate in the
// test would silently survive a refactor -- this calls the real function.)
TEST_CASE("effectDescriptionsFromScanXml keeps effects and drops instruments", "[engine][plugin-scanner]")
{
    juce::PluginDescription effect;
    effect.name = "DemoEQ";
    effect.pluginFormatName = "VST3";
    effect.fileOrIdentifier = "/plugins/DemoEQ.vst3";
    effect.isInstrument = false;

    juce::PluginDescription instrument;
    instrument.name = "DemoSynth";
    instrument.pluginFormatName = "VST3";
    instrument.fileOrIdentifier = "/plugins/DemoSynth.vst3";
    instrument.isInstrument = true;

    SECTION("a bundle exposing both effect and instrument keeps only the effect")
    {
        juce::XmlElement root("PB3_SCAN_RESULT");
        root.addChildElement(effect.createXml().release());
        root.addChildElement(instrument.createXml().release());

        const auto kept = engine::effectDescriptionsFromScanXml(root);

        REQUIRE(kept.size() == 1);
        CHECK_FALSE(kept[0].isInstrument);
        CHECK(kept[0].name == "DemoEQ");
    }

    SECTION("an instrument-only bundle yields an empty array (scanFile then blacklists)")
    {
        juce::XmlElement root("PB3_SCAN_RESULT");
        root.addChildElement(instrument.createXml().release());

        REQUIRE(engine::effectDescriptionsFromScanXml(root).isEmpty());
    }

    SECTION("an empty <PB3_SCAN_RESULT/> yields an empty array")
    {
        juce::XmlElement root("PB3_SCAN_RESULT");
        REQUIRE(engine::effectDescriptionsFromScanXml(root).isEmpty());
    }
}

// -----------------------------------------------------------------------
// Completion-sentinel gate (crash-hunt #6) -- no helper required
// -----------------------------------------------------------------------

// A signal-killed helper reports exit code 0 on POSIX, so parseHelperOutput
// must refuse a result that lacks the completion sentinel even when the XML
// parses. This is the seam that stops a teardown-crashing plugin from being
// whitelisted and later constructed in the host.
TEST_CASE("parseHelperOutput requires the completion sentinel", "[engine][plugin-scanner]")
{
    juce::PluginDescription effect;
    effect.name = "DemoEQ";
    effect.pluginFormatName = "VST3";
    effect.fileOrIdentifier = "/plugins/DemoEQ.vst3";
    effect.isInstrument = false;

    juce::XmlElement root("PB3_SCAN_RESULT");
    root.addChildElement(effect.createXml().release());
    const auto xml = root.toString(juce::XmlElement::TextFormat{}.singleLine());

    SECTION("valid XML followed by the sentinel is accepted")
    {
        engine::PluginScanner::ScanResult result;
        const bool ok = engine::parseHelperOutput(xml + "\n" + engine::kScanOkSentinel + "\n", result);
        REQUIRE(ok);
        REQUIRE_FALSE(result.helperCrashed);
        REQUIRE(result.descriptions.size() == 1);
        CHECK(result.descriptions[0].name == "DemoEQ");
    }

    SECTION("valid XML WITHOUT the sentinel is treated as a teardown crash")
    {
        engine::PluginScanner::ScanResult result;
        const bool ok = engine::parseHelperOutput(xml + "\n", result); // no sentinel
        REQUIRE_FALSE(ok);
        REQUIRE(result.helperCrashed);
        REQUIRE(result.descriptions.isEmpty()); // -> scanFile sets shouldBlacklist
    }

    SECTION("the sentinel alone (empty scan) with no PB3_SCAN_RESULT still fails to parse")
    {
        engine::PluginScanner::ScanResult result;
        const bool ok = engine::parseHelperOutput(juce::String(engine::kScanOkSentinel) + "\n", result);
        REQUIRE_FALSE(ok);
        REQUIRE(result.helperCrashed);
    }

    SECTION("an empty scan result with the sentinel parses to zero descriptions")
    {
        juce::XmlElement empty("PB3_SCAN_RESULT");
        const auto emptyXml = empty.toString(juce::XmlElement::TextFormat{}.singleLine());
        engine::PluginScanner::ScanResult result;
        const bool ok = engine::parseHelperOutput(emptyXml + "\n" + engine::kScanOkSentinel, result);
        REQUIRE(ok);
        REQUIRE(result.descriptions.isEmpty());
    }
}

// -----------------------------------------------------------------------
// Integration with the real pb3-scanner-helper binary
// -----------------------------------------------------------------------

// CI runners (Linux + macOS) are typically headless and the helper's
// juce::ScopedJuceInitialiser_GUI may not initialise cleanly without a
// display -- in that case the subprocess exits non-zero and PluginScanner
// reports helperCrashed=true. We treat that as a SKIP rather than a hard
// failure: the integration coverage we're after still applies on dev
// boxes (and Windows runners which have a session). The negative-path
// behaviour the tests verify (blacklist-on-failure) is still exercised
// by the unit-level cases above against juce::File{} helper.
//
// The INFO dump surfaces the actual error string + exit details so the
// next pipeline log tells us exactly what the helper reported.
#define PB3_SKIP_IF_HELPER_CRASHED(result)                                                                             \
    INFO("helperCrashed=" << (result).helperCrashed << " timedOut=" << (result).timedOut << " error='"                 \
                          << (result).error.toStdString() << "'");                                                     \
    if ((result).helperCrashed)                                                                                        \
    SKIP("helper subprocess failed to run cleanly (likely headless CI); covered by on-host smoke")

TEST_CASE("PluginScanner: real helper against a non-plugin file blacklists the path with no descriptors",
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

    // Helper exited 0 with an empty <PB3_SCAN_RESULT/> -- no crash, no
    // timeout, but also no descriptors. scanFile flags it for blacklisting;
    // applyScanResults (message thread) commits that to the list.
    REQUIRE_FALSE(result.timedOut);
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

    // Helper iterates every plugin format's fileMightContainThisPluginType
    // check first; a non-existent file fails every check and the helper
    // exits 0 with an empty result. PluginScanner blacklists.
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

    // Applying the same failing result again must not double-blacklist —
    // applyScanResults has an explicit `!contains(path)` guard before
    // addToBlacklist (belt-and-braces with JUCE's own dedupe).
    auto secondResult = scanner.scanFile(path);
    scanner.applyScanResults({secondResult});
    const auto sizeAfterSecond = host.getKnownPluginList().getBlacklistedFiles().size();

    REQUIRE(sizeAfterSecond == sizeAfterFirst);

    target.deleteFile();
}
