// Tests for pedalboard3::engine::PluginScanner — the parent-side of the
// out-of-process plugin scanning rig (master plan §4.2.A). The actual
// subprocess work happens in pb3-scanner-helper; PluginScanner orchestrates
// helper invocation, timeout handling, and KnownPluginList mutation
// (addType on success, addToBlacklist on every failure mode).
//
// Two layers of coverage:
//
//   1. Unit-level — no helper binary needed. Verifies the negative
//      paths (no helper, bogus helper path) and the blacklist
//      semantics that protect the user from a re-scan loop.
//
//   2. Integration with the real pb3-scanner-helper — gated on the
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
// time" → integration cases SKIP.
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
// without crashing — fileMightContainThisPluginType() in juce
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
// Unit-level — no helper required
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

    // Intentional: when we couldn't even attempt the scan we DON'T
    // blacklist the path. Once a future helper is in place those plugins
    // should be re-tried, not poisoned permanently. The blacklist
    // update is reserved for cases where the helper actually ran and
    // either crashed, timed out, or produced no descriptors.
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().isEmpty());
}

TEST_CASE("PluginScanner: scanAllDefaultPaths returns 0 when no helper is available", "[engine][plugin-scanner]")
{
    engine::PluginHost host;
    engine::PluginScanner scanner(host, juce::File{});

    int progressCalls = 0;
    const int added = scanner.scanAllDefaultPaths([&progressCalls](int, int, const juce::String&) { ++progressCalls; });

    REQUIRE(added == 0);
    REQUIRE(progressCalls == 0); // no scan loop runs without helper
}

TEST_CASE("PluginScanner: findHelperBinary doesn't crash on a runner without a sibling helper",
          "[engine][plugin-scanner]")
{
    // We only assert "no crash + return value matches existsAsFile()
    // when wired into a PluginScanner". Whether the helper happens to
    // sit alongside the test exe depends on the build layout — both
    // present and absent are valid outcomes for this test.
    const auto found = engine::PluginScanner::findHelperBinary();

    engine::PluginHost host;
    engine::PluginScanner scanner(host, found);
    REQUIRE(scanner.isHelperAvailable() == found.existsAsFile());
}

// -----------------------------------------------------------------------
// Integration with the real pb3-scanner-helper binary
// -----------------------------------------------------------------------

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

    // Helper exited 0 with an empty <PB3_SCAN_RESULT/> — no crash,
    // no timeout, but also no descriptors. PluginScanner treats
    // "no descriptors" as a failed scan and blacklists the path so
    // we don't re-scan it next launch.
    REQUIRE_FALSE(result.helperCrashed);
    REQUIRE_FALSE(result.timedOut);
    REQUIRE(result.descriptions.isEmpty());
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

    REQUIRE_FALSE(result.timedOut);
    REQUIRE(result.descriptions.isEmpty());
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().contains("/totally/nonexistent/path/Plugin.vst3"));
}

TEST_CASE("PluginScanner: real helper — repeat scan of same failing path doesn't double-blacklist",
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

    scanner.scanFile(path);
    const auto sizeAfterFirst = host.getKnownPluginList().getBlacklistedFiles().size();
    REQUIRE(host.getKnownPluginList().getBlacklistedFiles().contains(path));

    scanner.scanFile(path);
    const auto sizeAfterSecond = host.getKnownPluginList().getBlacklistedFiles().size();

    // PluginScanner has an explicit `!contains(path)` guard before
    // calling addToBlacklist — both layers must hold this invariant
    // (the JUCE addToBlacklist guard AND our defensive check) so a
    // future JUCE behaviour change wouldn't introduce duplicates.
    REQUIRE(sizeAfterSecond == sizeAfterFirst);

    target.deleteFile();
}
