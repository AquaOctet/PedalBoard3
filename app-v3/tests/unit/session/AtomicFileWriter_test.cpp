// Tests for session::AtomicFileWriter (M5 U2, master plan AC-FS-6): whatever
// step a save stops at, the target holds the old contents or the new ones,
// never a mix, and no temp file is left behind.

#include <catch2/catch_test_macros.hpp>

#include "session/AtomicFileWriter.h"

using pedalboard3::session::AtomicFileWriter;

namespace
{

struct TempFolder
{
    juce::File dir{juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pb3_atomic", "")};
    TempFolder() { dir.createDirectory(); }
    ~TempFolder() { dir.deleteRecursively(); }
};

int tempFilesIn(const juce::File& dir)
{
    return dir.findChildFiles(juce::File::findFiles, false, "*.tmp").size();
}

} // namespace

TEST_CASE("AtomicFileWriter: writes a new file, and replaces an existing one", "[session][files]")
{
    const TempFolder folder;
    const auto target = folder.dir.getChildFile("board.pdl");

    REQUIRE(AtomicFileWriter::writeText(target, "one").wasOk());
    REQUIRE(target.loadFileAsString() == "one");
    REQUIRE(AtomicFileWriter::writeText(target, "two").wasOk());
    REQUIRE(target.loadFileAsString() == "two");
    REQUIRE(tempFilesIn(folder.dir) == 0);
}

TEST_CASE("AtomicFileWriter: stopped after writing or flushing, the old file is intact and no temp file is left",
          "[session][files]")
{
    const TempFolder folder;
    const auto target = folder.dir.getChildFile("board.pdl");
    for (const auto step : {AtomicFileWriter::Step::Written, AtomicFileWriter::Step::Flushed})
    {
        REQUIRE(AtomicFileWriter::writeText(target, "old").wasOk());
        const auto result =
            AtomicFileWriter::writeText(target, "new", [step](AtomicFileWriter::Step s) { return s != step; });
        REQUIRE(result.failed());
        REQUIRE(target.loadFileAsString() == "old");
        REQUIRE(tempFilesIn(folder.dir) == 0);
    }
}

TEST_CASE("AtomicFileWriter: stopped after the replace, the new file is complete", "[session][files]")
{
    const TempFolder folder;
    const auto target = folder.dir.getChildFile("board.pdl");
    REQUIRE(AtomicFileWriter::writeText(target, "old").wasOk());

    const auto result = AtomicFileWriter::writeText(
        target, "new", [](AtomicFileWriter::Step s) { return s != AtomicFileWriter::Step::Replaced; });
    REQUIRE(result.failed());
    REQUIRE(target.loadFileAsString() == "new");
    REQUIRE(tempFilesIn(folder.dir) == 0);
}

TEST_CASE("AtomicFileWriter: a body that fails leaves the target as it was", "[session][files]")
{
    const TempFolder folder;
    const auto target = folder.dir.getChildFile("board.pdl");
    REQUIRE(AtomicFileWriter::writeText(target, "old").wasOk());

    REQUIRE(AtomicFileWriter::write(target, [](juce::OutputStream&) { return false; }).failed());
    REQUIRE(target.loadFileAsString() == "old");
    REQUIRE(tempFilesIn(folder.dir) == 0);
}

TEST_CASE("AtomicFileWriter: a folder that can't be created fails and writes nothing", "[session][files]")
{
    const TempFolder folder;
    const auto blocker = folder.dir.getChildFile("blocker"); // a file where the folder would go
    REQUIRE(blocker.replaceWithText("not a folder"));

    const auto result = AtomicFileWriter::writeText(blocker.getChildFile("board.pdl"), "new");
    REQUIRE(result.failed());
    REQUIRE(blocker.loadFileAsString() == "not a folder");
}
