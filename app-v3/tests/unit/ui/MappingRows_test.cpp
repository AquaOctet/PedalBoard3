// Tests for ui::mappingrows (ui/MappingRows.h): the board mappings panel's
// rows (UI refresh U9).

#include <catch2/catch_test_macros.hpp>

#include "ui/MappingRows.h"

#include <utility>

using namespace pedalboard3;
using ui::mappingrows::Kind;

namespace
{

session::PatchNode plugin(session::NodeId id, const juce::String& name)
{
    session::PatchNode n;
    n.id = id;
    n.desc.name = name;
    return n;
}

control::MidiMapping cc(session::NodeId node, int controller)
{
    control::MidiMapping m;
    m.source.controller = controller;
    m.target.node = node;
    return m;
}

control::OscMapping osc(session::NodeId node, const juce::String& address)
{
    control::OscMapping m;
    m.address = address;
    m.target.node = node;
    return m;
}

// Two plugins, Pro-Q 3 (1) then ValhallaRoom (2), with their mappings stored
// out of chain order.
session::Patch board()
{
    session::Patch p;
    p.nodes = {plugin(1, "Pro-Q 3"), plugin(2, "ValhallaRoom")};
    p.midiMappings = {cc(2, 7), cc(1, 1), cc(1, 2)};
    p.oscMappings = {osc(2, "/room/mix"), osc(1, "/eq/gain")};
    return p;
}

} // namespace

TEST_CASE("MappingRows: every mapping on the board, in chain order", "[ui][mappings]")
{
    const auto midi = ui::mappingrows::rowsFor(board(), Kind::Midi, std::nullopt);
    REQUIRE(midi.size() == 3);
    REQUIRE(midi[0].node == 1);
    REQUIRE(midi[0].plugin == "Pro-Q 3");
    REQUIRE(midi[0].midi.source.controller == 1);
    REQUIRE(midi[0].nodeIndex == 0);
    REQUIRE(midi[1].midi.source.controller == 2);
    REQUIRE(midi[1].nodeIndex == 1);
    REQUIRE(midi[2].node == 2);
    REQUIRE(midi[2].nodeIndex == 0);
    REQUIRE(midi[2].chainIndex == 1);

    const auto oscRows = ui::mappingrows::rowsFor(board(), Kind::Osc, std::nullopt);
    REQUIRE(oscRows.size() == 2);
    REQUIRE(oscRows[0].osc.address == "/eq/gain");
    REQUIRE(oscRows[1].osc.address == "/room/mix");
}

TEST_CASE("MappingRows: the order follows the chain, not the mapping table", "[ui][mappings]")
{
    auto patch = board();
    std::swap(patch.nodes[0], patch.nodes[1]);
    const auto midi = ui::mappingrows::rowsFor(patch, Kind::Midi, std::nullopt);
    REQUIRE(midi.size() == 3);
    REQUIRE(midi[0].node == 2);
    REQUIRE(midi[1].node == 1);
    REQUIRE(midi[2].node == 1);
}

TEST_CASE("MappingRows: the selected card's rows are flagged, not filtered", "[ui][mappings]")
{
    const auto midi = ui::mappingrows::rowsFor(board(), Kind::Midi, session::NodeId{2});
    REQUIRE(midi.size() == 3);
    REQUIRE_FALSE(midi[0].selected);
    REQUIRE_FALSE(midi[1].selected);
    REQUIRE(midi[2].selected);
}

TEST_CASE("MappingRows: a mapping to a removed node, or to a patch or command, has no row", "[ui][mappings]")
{
    auto patch = board();
    patch.midiMappings.push_back(cc(9, 11)); // node 9 is not on the board
    control::MidiMapping select;
    select.target.kind = control::MidiTargetKind::PatchSelect;
    patch.midiMappings.push_back(select);
    REQUIRE(ui::mappingrows::rowsFor(patch, Kind::Midi, std::nullopt).size() == 3);
}

TEST_CASE("MappingRows: a custom name or a built-in's type names the row", "[ui][mappings]")
{
    auto patch = board();
    patch.nodes[0].customName = "Vocal EQ";
    session::PatchNode level;
    level.id = 3;
    level.kind = session::NodeKind::Internal;
    level.internalType = "LevelProcessor";
    patch.nodes.push_back(level);
    patch.midiMappings.push_back(cc(3, 20));

    const auto midi = ui::mappingrows::rowsFor(patch, Kind::Midi, std::nullopt);
    REQUIRE(midi.front().plugin == "Vocal EQ");
    REQUIRE(midi.back().plugin == "LevelProcessor");

    const auto named = ui::mappingrows::rowsFor(
        patch, Kind::Midi, std::nullopt, [](const session::PatchNode& n) { return "node " + juce::String(n.id); });
    REQUIRE(named.back().plugin == "node 3");
}

TEST_CASE("MappingRows: sameMappings ignores the selection and sees an edit", "[ui][mappings]")
{
    const auto plain = ui::mappingrows::rowsFor(board(), Kind::Midi, std::nullopt);
    const auto selected = ui::mappingrows::rowsFor(board(), Kind::Midi, session::NodeId{1});
    REQUIRE(ui::mappingrows::sameMappings(plain, selected));

    auto edited = board();
    edited.midiMappings[1].rangeMax = 0.5f;
    REQUIRE_FALSE(ui::mappingrows::sameMappings(plain, ui::mappingrows::rowsFor(edited, Kind::Midi, std::nullopt)));
    REQUIRE_FALSE(ui::mappingrows::sameMappings(plain, {}));
}
