// Tests for ui::bank (ui/BankRows.h): the bank sidebar's rows (UI refresh U4).

#include <catch2/catch_test_macros.hpp>

#include "ui/BankRows.h"

using namespace pedalboard3;

namespace
{

session::Patch patchWith(const char* name, int nodes)
{
    session::Patch p;
    p.name = name;
    for (int i = 0; i < nodes; ++i)
    {
        session::PatchNode n;
        n.id = static_cast<session::NodeId>(i + 1);
        p.nodes.push_back(n);
    }
    return p;
}

} // namespace

TEST_CASE("BankRows: one row per patch, numbered by Program Change, the active one live", "[ui][bank]")
{
    session::PatchBank bank;
    bank.patches = {patchWith("Clean", 1), patchWith("Crunch", 2), patchWith("Lead", 3)};
    bank.activeIndex = 1;

    const auto rows = ui::bank::rowsFor(bank, 5); // the playing patch has 5 plugins now
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].programChange == 0);
    REQUIRE(rows[2].programChange == 2);
    REQUIRE(rows[2].name == "Lead");
    REQUIRE_FALSE(rows[0].active);
    REQUIRE(rows[1].active);
    REQUIRE(rows[1].nodeCount == 5); // live, not the cached slot's 2
    REQUIRE(rows[2].nodeCount == 3);
}

TEST_CASE("BankRows: a fresh bank is one Untitled row", "[ui][bank]")
{
    const auto rows = ui::bank::rowsFor(session::PatchBank{}, 0);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].name == "Untitled");
    REQUIRE(rows[0].active);
    REQUIRE(rows[0].nodeCount == 0);
}
