// Tests for ui::body (ui/BodyLayout.h): the main window body's panels fold to
// icon strips on a narrow window, and the canvas takes what is left (UI
// refresh U6).

#include <catch2/catch_test_macros.hpp>

#include "ui/BodyLayout.h"

using namespace pedalboard3::ui;

TEST_CASE("BodyLayout: a wide window shows both panels open and the canvas takes the rest", "[ui][body]")
{
    const auto l = body::layout({0, 48, 1280, 700}, true, true);
    REQUIRE_FALSE(l.folded);
    REQUIRE(l.left.getWidth() == 248);
    REQUIRE(l.right.getWidth() == 256);
    REQUIRE(l.canvas.getX() == 248);
    REQUIRE(l.canvas.getWidth() == 1280 - 248 - 256);
    REQUIRE(l.canvas.getY() == 48);
}

TEST_CASE("BodyLayout: below the fold width both panels become 48 px strips", "[ui][body]")
{
    const auto l = body::layout({0, 0, 960, 460}, true, true);
    REQUIRE(l.folded);
    REQUIRE(l.left.getWidth() == 48);
    REQUIRE(l.right.getWidth() == 48);
    REQUIRE(l.canvas.getWidth() == 960 - 96);

    // A strip's panel slides open over the canvas, next to the strip.
    const auto over = body::overlayFor(l, /*leftSide*/ true);
    REQUIRE(over.getX() == 48);
    REQUIRE(over.getWidth() == 248);
    REQUIRE(over.getHeight() == 460);
}

TEST_CASE("BodyLayout: a hidden panel takes no room, and the canvas never goes negative", "[ui][body]")
{
    const auto hidden = body::layout({0, 0, 1280, 700}, /*leftShown*/ false, /*rightShown*/ true);
    REQUIRE(hidden.left.isEmpty());
    REQUIRE(hidden.canvas.getX() == 0);

    const auto tiny = body::layout({0, 0, 30, 100}, true, true);
    REQUIRE(tiny.canvas.getWidth() >= 0);
    REQUIRE(tiny.left.getWidth() + tiny.right.getWidth() + tiny.canvas.getWidth() == 30);
}
