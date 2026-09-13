// Tests for the LookAndFeel's menu metrics (ui/Pb3LookAndFeel.h; UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U3): the mockup's row,
// separator and header heights, the panel's padding, and a see-through menu
// window so the rounded corners stay clear.

#include <catch2/catch_test_macros.hpp>

#include "ui/Pb3LookAndFeel.h"

using namespace pedalboard3::ui;

TEST_CASE("Pb3LookAndFeel: menu rows are 30 px, separators 9, section headers 16 before JUCE adds half", "[ui][menus]")
{
    Pb3LookAndFeel lookAndFeel;
    const juce::PopupMenu::Options options;
    int width = 0;
    int height = 0;

    lookAndFeel.getIdealPopupMenuItemSizeWithOptions("Save As...", false, 0, width, height, options);
    REQUIRE(height == 30);
    const int plainWidth = width;

    // JUCE measures an item as its text, three spaces and its shortcut: the
    // accessory gets room of its own.
    lookAndFeel.getIdealPopupMenuItemSizeWithOptions("Save As...   Ctrl+Shift+S", false, 0, width, height, options);
    REQUIRE(height == 30);
    REQUIRE(width > plainWidth);

    // ComboBox lists pass their own height; they get the mockup's rows too.
    lookAndFeel.getIdealPopupMenuItemSizeWithOptions("48000 Hz", false, 24, width, height, options);
    REQUIRE(height == 30);

    lookAndFeel.getIdealPopupMenuItemSizeWithOptions({}, true, 0, width, height, options);
    REQUIRE(height == 9);

    // Section headers are measured with a standard height of -1.
    lookAndFeel.getIdealPopupMenuItemSizeWithOptions("Go to Patch", false, -1, width, height, options);
    REQUIRE(height == 16);
}

TEST_CASE("Pb3LookAndFeel: the menu panel has 5 px of padding and a see-through window", "[ui][menus]")
{
    Pb3LookAndFeel lookAndFeel;
    REQUIRE(lookAndFeel.getPopupMenuBorderSizeWithOptions(juce::PopupMenu::Options{}) == 5);
    REQUIRE(lookAndFeel.getPopupMenuBorderSize() == 5);
    REQUIRE_FALSE(lookAndFeel.findColour(juce::PopupMenu::backgroundColourId).isOpaque());
    REQUIRE(lookAndFeel.getPopupMenuFont().getTypefacePtr() != nullptr);
}
