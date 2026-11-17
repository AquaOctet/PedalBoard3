// Tests for ui::trayArtSizeFor (ui/TrayArt.h): which hand-drawn tray drawing a
// given tray size gets (#147). The component needs a desktop and a shell to
// put an icon in; this choice needs neither.

#include <catch2/catch_test_macros.hpp>

#include "ui/TrayArt.h"

using namespace pedalboard3::ui;

TEST_CASE("trayArtSizeFor: the DPI sizes Windows asks for are drawn, not derived", "[ui][tray]")
{
    // 100%, 125%, 150% and 200% of a 16px notification area. Each is a size
    // the ladder holds, so each is the drawing itself.
    REQUIRE(trayArtSizeFor(16) == 16);
    REQUIRE(trayArtSizeFor(20) == 20);
    REQUIRE(trayArtSizeFor(24) == 24);
    REQUIRE(trayArtSizeFor(32) == 32);
}

TEST_CASE("trayArtSizeFor: an in-between size rounds up, never down", "[ui][tray]")
{
    // The one that matters. 175% of 16 is 28, which nothing in the ladder
    // draws. Handing over the 24 would make the shell stretch pixel art and
    // smear every edge; the 32 is shrunk instead, which only softens it.
    REQUIRE(trayArtSizeFor(28) == 32);
    REQUIRE(trayArtSizeFor(17) == 20);
}

TEST_CASE("trayArtSizeFor: past the top of the ladder the biggest drawing is used", "[ui][tray]")
{
    REQUIRE(trayArtSizeFor(64) == 48);
    REQUIRE(trayArtSizeFor(1024) == 48);
}

TEST_CASE("trayArtSizeFor: a nonsense size still answers with a drawing", "[ui][tray]")
{
    // A display that reports no scale, or a platform that reports nothing at
    // all, must not leave the tray empty.
    REQUIRE(trayArtSizeFor(0) == 16);
    REQUIRE(trayArtSizeFor(-1) == 16);
}
