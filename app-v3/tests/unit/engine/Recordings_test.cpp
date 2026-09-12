// Tests for engine::recordings (engine/Recordings.h): where takes go and what
// they're called (M6 U7, plan KD6).

#include <catch2/catch_test_macros.hpp>

#include "engine/Recordings.h"

using namespace pedalboard3::engine;

namespace
{
const juce::Time kWhen(2026, 8, 12, 21, 3, 15); // 12 September 2026, 21:03:15 (months count from 0)
}

TEST_CASE("Recordings: a take is named after the patch and the time", "[engine][recordings]")
{
    REQUIRE(recordings::takeName("Verse", kWhen) == "Verse 2026-09-12 21-03-15");
    REQUIRE(recordings::takeName("  Chorus  ", kWhen) == "Chorus 2026-09-12 21-03-15");
}

TEST_CASE("Recordings: characters a file name can't hold are dropped, and no name is Take", "[engine][recordings]")
{
    const auto name = recordings::takeName("A/B: live?", kWhen);
    REQUIRE_FALSE(name.containsAnyOf("/\\:?*\"<>|"));
    REQUIRE(name.endsWith(" 2026-09-12 21-03-15"));
    REQUIRE(recordings::takeName("", kWhen) == "Take 2026-09-12 21-03-15");
    REQUIRE(recordings::takeName("???", kWhen) == "Take 2026-09-12 21-03-15");
}

TEST_CASE("Recordings: a take never overwrites one with the same name", "[engine][recordings]")
{
    const auto folder =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pb3_takes", "");
    REQUIRE(folder.createDirectory());

    const auto first = recordings::newTakeFile(folder, "Verse", kWhen);
    REQUIRE(first.getFileName() == "Verse 2026-09-12 21-03-15.wav");
    REQUIRE(first.create().wasOk());
    const auto second = recordings::newTakeFile(folder, "Verse", kWhen);
    REQUIRE(second.getFileName() == "Verse 2026-09-12 21-03-15 2.wav");
    REQUIRE(second.create().wasOk());
    REQUIRE(recordings::newTakeFile(folder, "Verse", kWhen).getFileName() == "Verse 2026-09-12 21-03-15 3.wav");

    folder.deleteRecursively();
}

TEST_CASE("Recordings: the default folder is Documents/Pedalboard 3/Recordings", "[engine][recordings]")
{
    const auto folder = recordings::defaultFolder();
    REQUIRE(folder.getFileName() == "Recordings");
    REQUIRE(folder.getParentDirectory().getFileName() == "Pedalboard 3");
}
