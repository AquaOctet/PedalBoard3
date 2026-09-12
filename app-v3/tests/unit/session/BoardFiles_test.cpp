// Tests for session::boardfiles (session/BoardFiles.h): Open Recent's list
// (#30) and which dropped file opens (#31).

#include <catch2/catch_test_macros.hpp>

#include "session/BoardFiles.h"

using namespace pedalboard3::session;

namespace
{
juce::File tempDir()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory);
}
} // namespace

TEST_CASE("BoardFiles: a pushed file goes to the front, once, and the list is capped", "[session][boardfiles]")
{
    const auto a = tempDir().getChildFile("a.pdl");
    const auto b = tempDir().getChildFile("b.pdl");

    auto list = boardfiles::pushRecent({}, a);
    list = boardfiles::pushRecent(list, b);
    REQUIRE(list == juce::StringArray{b.getFullPathName(), a.getFullPathName()});

    list = boardfiles::pushRecent(list, a); // opened again: back to the front, not twice
    REQUIRE(list == juce::StringArray{a.getFullPathName(), b.getFullPathName()});

    juce::StringArray many;
    for (int i = 0; i < 15; ++i)
        many = boardfiles::pushRecent(many, tempDir().getChildFile("board" + juce::String(i) + ".pdl"));
    REQUIRE(many.size() == boardfiles::kMaxRecent);
    REQUIRE(many[0] == tempDir().getChildFile("board14.pdl").getFullPathName());
    REQUIRE(many[boardfiles::kMaxRecent - 1] == tempDir().getChildFile("board5.pdl").getFullPathName());
}

TEST_CASE("BoardFiles: empty or relative entries from a hand-edited settings file are dropped", "[session][boardfiles]")
{
    const auto a = tempDir().getChildFile("a.pdl");
    REQUIRE(boardfiles::pushRecent({"", "relative.pdl"}, a) == juce::StringArray{a.getFullPathName()});
}

TEST_CASE("BoardFiles: a file that has gone is removed from the list", "[session][boardfiles]")
{
    const auto a = tempDir().getChildFile("a.pdl");
    const auto b = tempDir().getChildFile("b.pdl");
    const juce::StringArray list{b.getFullPathName(), a.getFullPathName()};
    REQUIRE(boardfiles::removeRecent(list, b) == juce::StringArray{a.getFullPathName()});
    REQUIRE(boardfiles::removeRecent(list, tempDir().getChildFile("other.pdl")) == list);
}

TEST_CASE("BoardFiles: an Open Recent item names the board and its folder", "[session][boardfiles]")
{
    const auto file = tempDir().getChildFile("Live.pdl");
    REQUIRE(boardfiles::menuLabel(file) ==
            "Live" + juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) + tempDir().getFileName());
}

TEST_CASE("BoardFiles: the first existing .pdl among dropped files opens", "[session][boardfiles]")
{
    const auto dir = tempDir().getChildFile("pb3_boardfiles_test");
    dir.createDirectory();
    const auto text = dir.getChildFile("notes.txt");
    const auto board = dir.getChildFile("Gig.PDL"); // the extension matches in any case
    REQUIRE(text.create().wasOk());
    REQUIRE(board.create().wasOk());

    REQUIRE(boardfiles::firstBoardFile({text.getFullPathName(), board.getFullPathName()}) == board);
    REQUIRE(boardfiles::firstBoardFile({text.getFullPathName()}) == juce::File());
    REQUIRE(boardfiles::firstBoardFile({dir.getChildFile("gone.pdl").getFullPathName()}) == juce::File());
    REQUIRE(boardfiles::firstBoardFile({}) == juce::File());

    dir.deleteRecursively();
}

TEST_CASE("BoardFiles: audio files a File Player plays are recognised", "[session][boardfiles]")
{
    REQUIRE(boardfiles::isAudioFile(tempDir().getChildFile("take.wav")));
    REQUIRE(boardfiles::isAudioFile(tempDir().getChildFile("Loop.AIFF")));
    REQUIRE(boardfiles::isAudioFile(tempDir().getChildFile("song.flac")));
    REQUIRE_FALSE(boardfiles::isAudioFile(tempDir().getChildFile("board.pdl")));
    REQUIRE_FALSE(boardfiles::isAudioFile(tempDir().getChildFile("notes.txt")));
    REQUIRE(boardfiles::firstAudioFile({tempDir().getChildFile("gone.wav").getFullPathName()}) == juce::File());
}
