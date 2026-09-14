// Tests for the dialog title bar's layout (ui/DialogFrame.h; UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U4): the close button a
// square at the right, the title left-aligned before it.

#include <catch2/catch_test_macros.hpp>

#include "ui/DialogFrame.h"

using namespace pedalboard3::ui;

TEST_CASE("DialogFrame: the close button is a square at the right of the title bar, the title before it",
          "[ui][dialog]")
{
    const auto layout = dialogframe::titleBarLayout({0, 0, 400, dialogframe::kTitleBarHeight});

    REQUIRE(layout.close == juce::Rectangle<int>(372, 0, 28, 28));
    REQUIRE(layout.title.getX() == dialogframe::kTitlePadding);
    REQUIRE(layout.title.getRight() <= layout.close.getX());
    REQUIRE(layout.title.getY() == 0);
    REQUIRE(layout.title.getHeight() == dialogframe::kTitleBarHeight);
}

TEST_CASE("DialogFrame: the layout follows the title bar inside a resizable dialog's border", "[ui][dialog]")
{
    // A resizable dialog's border is 4 px, so its title bar starts at (4, 4).
    const auto layout = dialogframe::titleBarLayout({4, 4, 392, dialogframe::kTitleBarHeight});

    REQUIRE(layout.close == juce::Rectangle<int>(368, 4, 28, 28));
    REQUIRE(layout.title.getX() == 4 + dialogframe::kTitlePadding);
    REQUIRE(layout.title.getRight() <= layout.close.getX());
}
