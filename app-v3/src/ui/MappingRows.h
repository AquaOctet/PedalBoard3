#pragma once

#include "session/Patch.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

namespace pedalboard3::ui::mappingrows
{

// The board mappings panel's rows (UI refresh U9), free of components so the
// headless tests can cover them: every plugin-parameter mapping on the board,
// in chain order. Each row records where it sits among its node's mappings, so
// an edit can write that node's tables back through Session::setNodeMappings.

enum class Kind
{
    Midi,
    Osc
};

struct Row
{
    Kind kind{Kind::Midi};
    session::NodeId node{0};
    int nodeIndex{0}; // its place among the node's mappings of this kind (Session::getNodeMappings order)
    juce::String plugin;
    control::MidiMapping midi; // Kind::Midi
    control::OscMapping osc;   // Kind::Osc
    bool selected{false};      // its node is the selected card

    bool operator==(const Row&) const = default;
};

// What a row calls its node when no better name is given: the custom name,
// the plugin's own, or the built-in's type.
inline juce::String defaultName(const session::PatchNode& node)
{
    if (node.customName.isNotEmpty())
        return node.customName;
    return node.kind == session::NodeKind::Internal ? node.internalType : node.desc.name;
}

using NameOf = std::function<juce::String(const session::PatchNode&)>;

inline bool targetsNode(const control::MidiTarget& target, session::NodeId node)
{
    return target.kind == control::MidiTargetKind::PluginParameter && target.node == node;
}

// The rows of one kind. A mapping whose node has left the patch has no row, and
// neither do patch-select or app-command mappings (App Mappings lists those).
inline std::vector<Row> rowsFor(const session::Patch& patch,
                                Kind kind,
                                std::optional<session::NodeId> selected,
                                const NameOf& nameOf = defaultName)
{
    std::vector<Row> rows;
    for (std::size_t chain = 0; chain < patch.nodes.size(); ++chain)
    {
        const auto& node = patch.nodes[chain];
        Row base;
        base.kind = kind;
        base.node = node.id;
        base.plugin = nameOf(node);
        base.selected = selected == node.id;

        const auto collect = [&rows, &base, &node](const auto& table, auto fill)
        {
            int nodeIndex = 0;
            for (const auto& mapping : table)
                if (targetsNode(mapping.target, node.id))
                {
                    auto row = base;
                    row.nodeIndex = nodeIndex++;
                    fill(row, mapping);
                    rows.push_back(std::move(row));
                }
        };
        if (kind == Kind::Midi)
            collect(patch.midiMappings, [](Row& row, const control::MidiMapping& m) { row.midi = m; });
        else
            collect(patch.oscMappings, [](Row& row, const control::OscMapping& m) { row.osc = m; });
    }
    return rows;
}

// True when two row lists show the same mappings, whatever is selected. The
// panel rebuilds its editors only when this is false, so the Session change
// its own edit causes doesn't destroy the control being dragged.
inline bool sameMappings(const std::vector<Row>& a, const std::vector<Row>& b)
{
    return std::equal(a.begin(),
                      a.end(),
                      b.begin(),
                      b.end(),
                      [](Row x, const Row& y)
                      {
                          x.selected = y.selected;
                          return x == y;
                      });
}

} // namespace pedalboard3::ui::mappingrows
