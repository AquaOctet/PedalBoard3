// Tests for ui::body (ui/BodyLayout.h): the main window body's panels open,
// collapse to icon strips, or fold on a narrow window, and the canvas takes
// what is left (UI refresh U6).

#include <catch2/catch_test_macros.hpp>

#include "ui/BodyLayout.h"

using namespace pedalboard3::ui;
using body::Panel;

TEST_CASE("BodyLayout: a wide window shows open panels at full width", "[ui][body]")
{
    const auto l = body::layout({0, 48, 1280, 700}, Panel::Open, Panel::Open);
    REQUIRE_FALSE(l.narrow);
    REQUIRE_FALSE(l.leftFolded);
    REQUIRE(l.left.getWidth() == 248);
    REQUIRE(l.right.getWidth() == 256);
    REQUIRE(l.canvas.getX() == 248);
    REQUIRE(l.canvas.getWidth() == 1280 - 248 - 256);
    REQUIRE(l.canvas.getY() == 48);
}

TEST_CASE("BodyLayout: a collapsed panel is a 48 px strip at any width", "[ui][body]")
{
    const auto l = body::layout({0, 0, 1600, 700}, Panel::Collapsed, Panel::Open);
    REQUIRE(l.leftFolded);
    REQUIRE_FALSE(l.rightFolded);
    REQUIRE(l.left.getWidth() == 48);
    REQUIRE(l.right.getWidth() == 256);
}

TEST_CASE("BodyLayout: below the fold width open panels fold to strips and slide open over the canvas", "[ui][body]")
{
    const auto l = body::layout({0, 0, 960, 460}, Panel::Open, Panel::Open);
    REQUIRE(l.narrow);
    REQUIRE(l.leftFolded);
    REQUIRE(l.rightFolded);
    REQUIRE(l.left.getWidth() == 48);
    REQUIRE(l.right.getWidth() == 48);
    REQUIRE(l.canvas.getWidth() == 960 - 96);

    const auto over = body::overlayFor(l, /*leftSide*/ true);
    REQUIRE(over.getX() == 48);
    REQUIRE(over.getWidth() == 248);
    REQUIRE(over.getHeight() == 460);
}

TEST_CASE("BodyLayout: a hidden panel takes no room, and the canvas never goes negative", "[ui][body]")
{
    const auto hidden = body::layout({0, 0, 1280, 700}, Panel::Hidden, Panel::Open);
    REQUIRE(hidden.left.isEmpty());
    REQUIRE(hidden.canvas.getX() == 0);

    const auto tiny = body::layout({0, 0, 30, 100}, Panel::Open, Panel::Open);
    REQUIRE(tiny.canvas.getWidth() >= 0);
    REQUIRE(tiny.left.getWidth() + tiny.right.getWidth() + tiny.canvas.getWidth() == 30);
}
