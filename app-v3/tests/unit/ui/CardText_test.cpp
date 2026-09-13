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

TEST_CASE("CardText: the Metronome's readout shows its time signature, and synced or its tempo", "[ui][card]")
{
    using pedalboard3::ui::card::metronomeReadout;
    const juce::String dot(juce::CharPointer_UTF8(" \xc2\xb7 "));
    REQUIRE(metronomeReadout(4, 4, true, 120.0) == "4/4" + dot + "synced");
    REQUIRE(metronomeReadout(3, 4, false, 96.0) == "3/4" + dot + "96 BPM");
    REQUIRE(metronomeReadout(7, 8, false, 92.5) == "7/8" + dot + "92.5 BPM");
}

TEST_CASE("CardText: the Looper's status is REC, PLAY or a dash", "[ui][card]")
{
    using pedalboard3::ui::card::looperStatus;
    REQUIRE(looperStatus(true, false, false) == "REC");
    REQUIRE(looperStatus(true, true, true) == "REC");
    REQUIRE(looperStatus(false, true, true) == "PLAY");
    REQUIRE(looperStatus(false, true, false) == juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")));
    REQUIRE(looperStatus(false, false, true) == juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")));
}

TEST_CASE("CardText: a missing plugin's card says Missing, and why (M5 U3)", "[ui][card]")
{
    REQUIRE(ui::card::titleFor(pluginNode()) == "Pro-Q 3");

    auto missing = pluginNode();
    missing.missingReason = "Not found: install it, or scan for plugins";
    REQUIRE(missing.isMissing());
    REQUIRE(ui::card::titleFor(missing) == "Missing: Pro-Q 3");
    REQUIRE(ui::card::linesFor(missing, 0).kind == missing.missingReason);
    REQUIRE(ui::card::linesFor(missing, 0).meta == "VST3" + ui::card::kDot + "FabFilter");

    missing.customName = "Mix EQ";
    REQUIRE(ui::card::titleFor(missing) == "Missing: Mix EQ");
}

TEST_CASE("CardText: a plugin that died says Crashed or Stopped, and what a click does (M5 U5)", "[ui][card]")
{
    using Death = session::Session::NodeSnapshot::Death;

    auto crashed = pluginNode();
    crashed.death = Death::Crashed;
    crashed.fault = "access violation";
    REQUIRE(crashed.isDead());
    REQUIRE(ui::card::titleFor(crashed) == "Crashed: Pro-Q 3");
    REQUIRE(ui::card::linesFor(crashed, 0).kind == "Access violation" + ui::card::kDot + "click to reload");

    auto stalled = pluginNode();
    stalled.death = Death::Stalled;
    REQUIRE(ui::card::titleFor(stalled) == "Stopped: Pro-Q 3");
    REQUIRE(ui::card::linesFor(stalled, 0).kind ==
            "Too slow for the audio block" + ui::card::kDot + "click to restart");
}
