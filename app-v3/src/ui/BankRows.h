#pragma once

#include "session/PatchBank.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace pedalboard3::ui::bank
{

// One row per patch for the bank sidebar (UI refresh U4), free of components
// so the headless tests can cover it. The Program Change number is the index
// (PatchSwitcher: Program Change n selects index n). The active patch's node
// count comes from the caller (the live Session when it is playing that
// patch), because the bank's slot is only a cache until you switch away.
struct Row
{
    int programChange;
    juce::String name;
    int nodeCount;
    bool active;
};

inline std::vector<Row> rowsFor(const session::PatchBank& bank, int activeNodeCount)
{
    std::vector<Row> rows;
    rows.reserve(bank.patches.size());
    for (int i = 0; i < bank.size(); ++i)
    {
        const auto& patch = bank.patches[static_cast<size_t>(i)];
        const bool active = i == bank.activeIndex;
        rows.push_back({i, patch.name, active ? activeNodeCount : static_cast<int>(patch.nodes.size()), active});
    }
    return rows;
}

} // namespace pedalboard3::ui::bank
