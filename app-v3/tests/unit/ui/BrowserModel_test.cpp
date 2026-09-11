// Tests for ui::browser (ui/BrowserModel.h): the plugin browser's chips,
// search, groups and recents (UI refresh U5).

#include <catch2/catch_test_macros.hpp>

#include "ui/BrowserModel.h"

#include <vector>

using namespace pedalboard3::ui;

namespace
{

browser::Entry plugin(const char* id, const char* name, const char* vendor, const char* category)
{
    return {id, name, vendor, "VST3", browser::chipForCategory(category), false};
}

browser::Entry builtIn(const char* id, const char* name)
{
    return {id, name, {}, "Built-in", browser::kBuiltInChip, true};
}

std::vector<browser::Entry> sampleEntries()
{
    return {builtIn("internal:LevelProcessor", "Level"),
            builtIn("internal:VuMeterProcessor", "VU Meter"),
            plugin("plugin:vr", "ValhallaRoom", "Valhalla DSP", "Fx|Reverb"),
            plugin("plugin:pq", "Pro-Q 3", "FabFilter", "Fx|EQ"),
            plugin("plugin:dc", "Decapitator", "Soundtoys", "Fx|Distortion")};
}

juce::StringArray namesOf(const browser::Group& g)
{
    juce::StringArray names;
    for (const auto& e : g.entries)
        names.add(e.name);
    return names;
}

} // namespace

TEST_CASE("BrowserModel: a VST3 category files under its first part that isn't Fx", "[ui][browser]")
{
    REQUIRE(browser::chipForCategory("Fx|Reverb") == "Reverb");
    REQUIRE(browser::chipForCategory("Delay") == "Delay");
    REQUIRE(browser::chipForCategory("Fx") == "Other");
    REQUIRE(browser::chipForCategory("") == "Other");
}

TEST_CASE("BrowserModel: chips are All, Built-in, then the plugins' categories A to Z", "[ui][browser]")
{
    const auto chips = browser::chipsFor(sampleEntries());
    REQUIRE(chips == juce::StringArray{"All", "Built-in", "Distortion", "EQ", "Reverb"});
}

TEST_CASE("BrowserModel: with no filter, the groups are Built-in, Recently used and All effects", "[ui][browser]")
{
    const juce::StringArray recents{"plugin:pq", "internal:VuMeterProcessor"};
    const auto groups = browser::groupsFor(sampleEntries(), recents, {}, browser::kAllChip);
    REQUIRE(groups.size() == 3);
    REQUIRE(groups[0].title == "Built-in");
    REQUIRE(namesOf(groups[0]) == juce::StringArray{"Level", "VU Meter"});
    REQUIRE(groups[1].title == "Recently used");
    REQUIRE(namesOf(groups[1]) == juce::StringArray{"Pro-Q 3", "VU Meter"}); // in recents order
    REQUIRE(groups[2].title == "All effects");
    REQUIRE(namesOf(groups[2]) == juce::StringArray{"Decapitator", "Pro-Q 3", "ValhallaRoom"}); // A to Z
}

TEST_CASE("BrowserModel: search matches name or vendor, and a chip keeps its category", "[ui][browser]")
{
    const juce::StringArray recents{"plugin:pq"};

    const auto byVendor = browser::groupsFor(sampleEntries(), recents, "fab", browser::kAllChip);
    REQUIRE(byVendor.size() == 2); // no built-in matches, so that group is left out
    REQUIRE(byVendor[0].title == "Recently used");
    REQUIRE(namesOf(byVendor[1]) == juce::StringArray{"Pro-Q 3"});

    const auto reverb = browser::groupsFor(sampleEntries(), recents, {}, "Reverb");
    REQUIRE(reverb.size() == 1);
    REQUIRE(namesOf(reverb[0]) == juce::StringArray{"ValhallaRoom"});

    const auto builtIns = browser::groupsFor(sampleEntries(), recents, {}, browser::kBuiltInChip);
    REQUIRE(builtIns.size() == 1);
    REQUIRE(builtIns[0].title == "Built-in");
}

TEST_CASE("BrowserModel: using an entry moves it to the front of recents, once, capped at 8", "[ui][browser]")
{
    juce::StringArray recents{"a", "b", "c"};
    recents = browser::pushRecent(recents, "b");
    REQUIRE(recents == juce::StringArray{"b", "a", "c"});

    for (int i = 0; i < 10; ++i)
        recents = browser::pushRecent(recents, "x" + juce::String(i));
    REQUIRE(recents.size() == browser::kMaxRecents);
    REQUIRE(recents[0] == "x9");
}
