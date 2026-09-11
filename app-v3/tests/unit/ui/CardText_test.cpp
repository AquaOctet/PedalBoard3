// Tests for ui::card (ui/CardText.h): what a plugin card says, and its VU
// meter readout (UI refresh U7).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/CardText.h"

using namespace pedalboard3;
using Catch::Approx;

namespace
{

session::Session::NodeSnapshot pluginNode()
{
    session::Session::NodeSnapshot s;
    s.name = "Pro-Q 3";
    s.category = "Fx|EQ";
    s.format = "VST3";
    s.vendor = "FabFilter";
    return s;
}

} // namespace

TEST_CASE("CardText: a plugin shows its category, then its format and vendor", "[ui][card]")
{
    const auto lines = ui::card::linesFor(pluginNode(), 0);
    REQUIRE(lines.kind == "EQ");
    REQUIRE(lines.meta == "VST3" + ui::card::kDot + "FabFilter");
}

TEST_CASE("CardText: mappings replace the vendor, and a bypassed card says so", "[ui][card]")
{
    REQUIRE(ui::card::linesFor(pluginNode(), 1).meta == "VST3" + ui::card::kDot + "1 mapping");
    REQUIRE(ui::card::linesFor(pluginNode(), 3).meta == "VST3" + ui::card::kDot + "3 mappings");

    auto bypassed = pluginNode();
    bypassed.bypassed = true;
    REQUIRE(ui::card::linesFor(bypassed, 0).kind == "EQ" + ui::card::kDot + "bypassed");
}

TEST_CASE("CardText: a built-in says Built-in, with no format or vendor", "[ui][card]")
{
    session::Session::NodeSnapshot level;
    level.name = "Level";
    level.builtIn = true;
    level.internalType = "LevelProcessor";
    const auto lines = ui::card::linesFor(level, 0);
    REQUIRE(lines.kind == "Built-in");
    REQUIRE(lines.meta.isEmpty());
    REQUIRE(ui::card::linesFor(level, 2).meta == "2 mappings");
}

TEST_CASE("CardText: the VU readout is dBFS with a -60 dB floor", "[ui][card]")
{
    REQUIRE(ui::card::dbfsText(1.0f) == "0.0");
    REQUIRE(ui::card::dbfsText(0.5f) == "-6.0");
    REQUIRE(ui::card::dbfsText(0.0f) == "-inf");

    REQUIRE(ui::card::meterFraction(1.0f) == Approx(1.0f));
    REQUIRE(ui::card::meterFraction(0.0f) == Approx(0.0f));
    REQUIRE(ui::card::meterFraction(0.0316228f) == Approx(0.5f).margin(0.001)); // -30 dBFS
    REQUIRE(ui::card::meterFraction(2.0f) == Approx(1.0f));                     // clipping stays full
}
