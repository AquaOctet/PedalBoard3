// Tests for ui::SegmentedControl (ui/SegmentedControl.h): the browser's filter
// bar after VR-U4 swapped its pills for one joined control.
//
// The selection, not the drawing. What the swap could break is someone's
// filter quietly changing under them when the options are rebuilt, which is
// the one thing here that has consequences.

#include <catch2/catch_test_macros.hpp>

#include "ui/SegmentedControl.h"

using namespace pedalboard3::ui;

TEST_CASE("SegmentedControl: setting the options keeps a selection that is still offered", "[ui][browser]")
{
    SegmentedControl control;
    control.setOptions({"All", "Built-in", "Reverb"}, "Reverb");
    REQUIRE(control.getSelected() == "Reverb");

    // A scan finishes and adds a category. The choice survives.
    control.setOptions({"All", "Built-in", "Delay", "Reverb"}, control.getSelected());
    REQUIRE(control.getSelected() == "Reverb");
}

TEST_CASE("SegmentedControl: a selection that is gone falls back to the first option", "[ui][browser]")
{
    SegmentedControl control;
    control.setOptions({"All", "Built-in", "Reverb"}, "Reverb");

    control.setOptions({"All", "Built-in", "Delay"}, control.getSelected());
    REQUIRE(control.getSelected() == "All");
}

TEST_CASE("SegmentedControl: rebuilding the options is not someone choosing", "[ui][browser]")
{
    // onChange drives a refilter and, above that, what the list shows. Firing
    // it for a rebuild would make a finishing scan look like a click.
    SegmentedControl control;
    int changes = 0;
    control.onChange = [&changes](juce::String) { ++changes; };

    control.setOptions({"All", "Built-in"}, "Built-in");
    control.setOptions({"All", "Built-in", "Reverb"}, "Built-in");
    control.setOptions({"All"}, "Built-in"); // even when the selection has to move

    REQUIRE(changes == 0);
}

TEST_CASE("SegmentedControl: with no options there is nothing selected", "[ui][browser]")
{
    SegmentedControl control;
    control.setOptions({}, "All");
    REQUIRE(control.getSelected().isEmpty());
    REQUIRE(control.heightForWidth(256) == 0);
}
