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

TEST_CASE("BrowserModel: the Add Plugin menu lists the effects A to Z, then the built-ins, each with its format tag",
          "[ui][browser]")
{
    auto entries = sampleEntries();
    entries.push_back({"plugin:ch", "Chorus", "Apple", "AudioUnit", "Modulation", false});
    const auto sections = browser::addPluginSections(entries);

    juce::StringArray effects, effectTags, builtIns, builtInTags;
    for (const auto& e : sections.effects)
    {
        effects.add(e.name);
        effectTags.add(browser::formatTag(e));
    }
    for (const auto& e : sections.builtIn)
    {
        builtIns.add(e.name);
        builtInTags.add(browser::formatTag(e));
    }

    REQUIRE(effects == juce::StringArray{"Chorus", "Decapitator", "Pro-Q 3", "ValhallaRoom"});
    REQUIRE(effectTags == juce::StringArray{"AU", "VST3", "VST3", "VST3"});
    REQUIRE(builtIns == juce::StringArray{"Level", "VU Meter"});
    REQUIRE(builtInTags == juce::StringArray{"INT", "INT"});
}

TEST_CASE("BrowserModel: a section whose rows all share a format has it once, not on every row", "[ui][browser]")
{
    // The whole point of VR-U4's row caption: eight rows reading "VST3" under
    // ALL EFFECTS is eight copies of one fact. sharedFormat answering tells
    // the browser to print it in the header instead.
    const browser::Group effects{"All effects",
                                 {plugin("plugin:a", "Decapitator", "Soundtoys", "Fx|Distortion"),
                                  plugin("plugin:b", "Pro-Q 3", "FabFilter", "Fx|EQ")}};
    REQUIRE(browser::sharedFormat(effects) == "VST3");

    const browser::Group builtIns{
        "Built-in", {builtIn("internal:LevelProcessor", "Level"), builtIn("internal:VuMeterProcessor", "VU Meter")}};
    REQUIRE(browser::sharedFormat(builtIns) == "Built-in");
}

TEST_CASE("BrowserModel: a mixed section keeps the format on its rows", "[ui][browser]")
{
    // Recently used is the one that mixes, and there the caption is the only
    // thing separating a built-in from a plugin that shares its name.
    const browser::Group recents{
        "Recently used",
        {builtIn("internal:LevelProcessor", "Level"), plugin("plugin:a", "Decapitator", "Soundtoys", "Fx|Distortion")}};
    REQUIRE(browser::sharedFormat(recents).isEmpty());
}

TEST_CASE("BrowserModel: an empty section shares no format", "[ui][browser]")
{
    REQUIRE(browser::sharedFormat(browser::Group{"All effects", {}}).isEmpty());
}

TEST_CASE("BrowserModel: one row is a section that shares its format", "[ui][browser]")
{
    // Not a corner case for its own sake: a search narrowing ALL EFFECTS to a
    // single plugin is the commonest way this group gets one row, and printing
    // "VST3" twice on one screen is exactly what the rule exists to stop.
    const browser::Group one{"All effects", {plugin("plugin:a", "Decapitator", "Soundtoys", "Fx|Distortion")}};
    REQUIRE(browser::sharedFormat(one) == "VST3");
}

TEST_CASE("BrowserModel: a filter in use survives the options being rebuilt", "[ui][browser]")
{
    // The scenario VR-U4's swap has to not break: a scan finishes, the filter
    // bar is rebuilt underneath someone reading a filtered list, and their
    // choice is still there.
    const juce::StringArray before{"All", "Built-in", "Reverb"};
    const juce::StringArray after{"All", "Built-in", "Delay", "Reverb"};

    REQUIRE(browser::keepOrResetChip(after, "Reverb") == "Reverb");
    REQUIRE(browser::keepOrResetChip(after, browser::kBuiltInChip) == browser::kBuiltInChip);
    REQUIRE(browser::keepOrResetChip(before, browser::kAllChip) == browser::kAllChip);
}

TEST_CASE("BrowserModel: a filter that is no longer offered falls back to All", "[ui][browser]")
{
    // Plugins can go away too -- a rescan after a folder is removed. Leaving
    // the browser filtered to a category nothing files under any more shows an
    // empty list with no way to tell why.
    const juce::StringArray after{"All", "Built-in", "Delay"};
    REQUIRE(browser::keepOrResetChip(after, "Reverb") == browser::kAllChip);
    REQUIRE(browser::keepOrResetChip({}, "Reverb") == browser::kAllChip);
}
