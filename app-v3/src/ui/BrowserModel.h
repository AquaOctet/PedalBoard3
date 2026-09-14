#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <vector>

namespace pedalboard3::ui::browser
{

// The plugin browser's model (UI refresh U5), free of components so the
// headless tests can cover it: category chips, search, the Built-in /
// Recently used / All effects groups, and the recents list.

struct Entry
{
    juce::String id; // "plugin:<PluginDescription identifier>" or "internal:<typeId>"
    juce::String name;
    juce::String vendor;
    juce::String format;   // "VST3", "AU", ...; "Built-in" for internal nodes
    juce::String category; // the chip it files under
    bool builtIn{false};
};

struct Group
{
    juce::String title;
    std::vector<Entry> entries;
};

inline constexpr int kMaxRecents = 8;
inline const juce::String kAllChip{"All"};
inline const juce::String kBuiltInChip{"Built-in"};

// A chip from PluginDescription::category, which VST3 writes as
// "Fx|Reverb": the first part that isn't "Fx", or "Other".
inline juce::String chipForCategory(const juce::String& category)
{
    for (const auto& part : juce::StringArray::fromTokens(category, "|", ""))
    {
        const auto name = part.trim();
        if (name.isNotEmpty() && !name.equalsIgnoreCase("Fx"))
            return name;
    }
    return "Other";
}

// The chips to offer: All, Built-in, then the plugins' categories A to Z.
inline juce::StringArray chipsFor(const std::vector<Entry>& entries)
{
    juce::StringArray categories;
    for (const auto& e : entries)
        if (!e.builtIn)
            categories.addIfNotAlreadyThere(e.category);
    categories.sortNatural();

    juce::StringArray chips{kAllChip, kBuiltInChip};
    chips.addArray(categories);
    return chips;
}

// A search matches the name or the vendor, case-insensitively; a chip other
// than All keeps only its category.
inline bool matches(const Entry& e, const juce::String& query, const juce::String& chip)
{
    if (chip.isNotEmpty() && chip != kAllChip && e.category != chip)
        return false;
    const auto q = query.trim();
    return q.isEmpty() || e.name.containsIgnoreCase(q) || e.vendor.containsIgnoreCase(q);
}

// Built-in, Recently used (in `recents` order) and All effects (A to Z), each
// filtered by the search and the chip. Empty groups are left out; a recent
// entry also stays in its own group.
inline std::vector<Group> groupsFor(const std::vector<Entry>& entries,
                                    const juce::StringArray& recents,
                                    const juce::String& query,
                                    const juce::String& chip)
{
    Group builtIn{"Built-in", {}};
    Group recent{"Recently used", {}};
    Group all{"All effects", {}};

    for (const auto& e : entries)
        if (matches(e, query, chip))
            (e.builtIn ? builtIn : all).entries.push_back(e);

    for (const auto& id : recents)
    {
        const auto it = std::find_if(entries.begin(), entries.end(), [&id](const Entry& e) { return e.id == id; });
        if (it != entries.end() && matches(*it, query, chip))
            recent.entries.push_back(*it);
    }

    std::sort(all.entries.begin(),
              all.entries.end(),
              [](const Entry& a, const Entry& b) { return a.name.compareNatural(b.name) < 0; });

    std::vector<Group> groups;
    for (auto* group : {&builtIn, &recent, &all})
        if (!group->entries.empty())
            groups.push_back(std::move(*group));
    return groups;
}

// The Add Plugin menu (UI chrome U3): the effects A to Z, then the built-ins
// in their own order.
struct AddPluginSections
{
    std::vector<Entry> effects;
    std::vector<Entry> builtIn;
};

inline AddPluginSections addPluginSections(const std::vector<Entry>& entries)
{
    AddPluginSections sections;
    for (const auto& e : entries)
        (e.builtIn ? sections.builtIn : sections.effects).push_back(e);
    std::sort(sections.effects.begin(),
              sections.effects.end(),
              [](const Entry& a, const Entry& b) { return a.name.compareNatural(b.name) < 0; });
    return sections;
}

// The tag on the right of an Add Plugin item: the plugin's format ("VST3",
// "AU" for an AudioUnit), or "INT" for a built-in.
inline juce::String formatTag(const Entry& e)
{
    if (e.builtIn)
        return "INT";
    if (e.format == "AudioUnit")
        return "AU";
    return e.format;
}

// The recents after using `id`: it moves to the front, with no duplicates and
// at most kMaxRecents.
inline juce::StringArray pushRecent(juce::StringArray recents, const juce::String& id)
{
    recents.removeString(id);
    recents.insert(0, id);
    while (recents.size() > kMaxRecents)
        recents.remove(recents.size() - 1);
    return recents;
}

} // namespace pedalboard3::ui::browser
