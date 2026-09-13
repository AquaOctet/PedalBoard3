// Tests for session::safexml (M5 U1, master plan AC-SE-7 / AC-SE-8): board
// files are parsed from memory, capped, with no DTDs or entities, and nothing
// outside the file can be read.

#include <catch2/catch_test_macros.hpp>

#include "session/SafeXml.h"

using namespace pedalboard3::session;

namespace
{

// A file outside the board that a hostile entity would try to pull in.
struct PlantedSecret
{
    juce::File file{
        juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pb3_secret", ".txt")};
    static constexpr const char* kMarker = "PB3-PLANTED-SECRET-MARKER";
    PlantedSecret() { file.replaceWithText(kMarker); }
    ~PlantedSecret() { file.deleteFile(); }
};

} // namespace

TEST_CASE("SafeXml: an ordinary document parses", "[session][security]")
{
    const auto parsed = safexml::parseText("<?xml version=\"1.0\"?><A x=\"1\"><B y=\"a&gt;b\"/></A>");
    REQUIRE(parsed.xml != nullptr);
    REQUIRE(parsed.error.isEmpty());
    REQUIRE(parsed.xml->getChildByName("B")->getStringAttribute("y") == "a>b");
}

TEST_CASE("SafeXml: a DTD, an internal entity, an external entity or a parameter entity is refused",
          "[session][security]")
{
    const PlantedSecret secret;
    const auto path = secret.file.getFullPathName();
    const juce::StringArray payloads{
        "<!DOCTYPE A><A/>",
        "<!DOCTYPE A [<!ENTITY x \"boom\">]><A v=\"&x;\"/>",
        "<!DOCTYPE A [<!ENTITY x SYSTEM \"" + path + "\">]><A v=\"&x;\"/>",
        "<!DOCTYPE A SYSTEM \"" + path + "\"><A/>",
        "<!DOCTYPE A [<!ENTITY % p SYSTEM \"" + path + "\"> %p;]><A/>",
        "<A><!ENTITY x \"boom\"></A>",
    };
    for (const auto& payload : payloads)
    {
        const auto parsed = safexml::parseText(payload);
        REQUIRE(parsed.xml == nullptr);
        REQUIRE(parsed.error.containsIgnoreCase("DTD"));
        REQUIRE_FALSE(parsed.error.contains(PlantedSecret::kMarker));
    }
}

TEST_CASE("SafeXml: billion laughs is refused before any expansion", "[session][security]")
{
    const juce::String laughs = "<?xml version=\"1.0\"?><!DOCTYPE lolz ["
                                "<!ENTITY lol \"lol\">"
                                "<!ENTITY lol1 \"&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;\">"
                                "<!ENTITY lol2 \"&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;&lol1;\">"
                                "<!ENTITY lol3 \"&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;\">"
                                "]><lolz>&lol3;</lolz>";
    const auto parsed = safexml::parseText(laughs);
    REQUIRE(parsed.xml == nullptr);
    REQUIRE(parsed.error.isNotEmpty());
}

TEST_CASE("SafeXml: nesting is measured past comments, CDATA and a quoted '>'", "[session][security]")
{
    REQUIRE(safexml::nestingDepth("<a><b><c/></b></a>") == 3);
    REQUIRE(safexml::nestingDepth("<a><!-- <x><y><z> --><b x=\"1>2\"/></a>") == 2);
    REQUIRE(safexml::nestingDepth("<a><![CDATA[<x><y><z>]]></a>") == 1);
    REQUIRE(safexml::nestingDepth("<?xml version=\"1.0\"?><a/>") == 1);
}

TEST_CASE("SafeXml: nesting deeper than the limit is refused", "[session][security]")
{
    juce::String deep;
    for (int i = 0; i < safexml::kMaxDepth + 1; ++i)
        deep << "<n>";
    for (int i = 0; i < safexml::kMaxDepth + 1; ++i)
        deep << "</n>";
    const auto parsed = safexml::parseText(deep);
    REQUIRE(parsed.xml == nullptr);
    REQUIRE(parsed.error.contains(juce::String(safexml::kMaxDepth)));
}

TEST_CASE("SafeXml: truncated XML, garbage and an empty file give an error, not a crash", "[session][security]")
{
    for (const auto& text :
         {juce::String("<A><B x=\"1\"/><C"), juce::String("\x01\x02garbage\x7f not xml"), juce::String()})
    {
        const auto parsed = safexml::parseText(text);
        REQUIRE(parsed.xml == nullptr);
        REQUIRE(parsed.error.isNotEmpty());
    }
}

TEST_CASE("SafeXml: a file over 32 MB is refused before it's read", "[session][security]")
{
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pb3_huge", ".pdl");
    {
        juce::FileOutputStream out(file);
        REQUIRE(out.openedOk());
        const juce::String chunk = juce::String::repeatedString("x", 1024 * 1024);
        for (int i = 0; i < 33; ++i)
            out.writeText(chunk, false, false, nullptr);
    }
    const auto parsed = safexml::parseFile(file);
    REQUIRE(parsed.xml == nullptr);
    REQUIRE(parsed.error.contains("32 MB"));
    file.deleteFile();
}

TEST_CASE("SafeXml: a file with an entity pointing at a planted file never reads it", "[session][security]")
{
    const PlantedSecret secret;
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pb3_xxe", ".pdl");
    file.replaceWithText("<?xml version=\"1.0\"?><!DOCTYPE A [<!ENTITY x SYSTEM \"" + secret.file.getFileName() +
                         "\">]><A v=\"&x;\"/>"); // relative to the file, as JUCE resolves
    const auto parsed = safexml::parseFile(file);
    REQUIRE(parsed.xml == nullptr);
    REQUIRE_FALSE(parsed.error.contains(PlantedSecret::kMarker));
    file.deleteFile();
}
