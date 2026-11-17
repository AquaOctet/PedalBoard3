// Tests for ui::titleBarToneFor (ui/TitleBarIcon.h): which single-colour icon
// belongs in the title bar's corner. The icon itself is Windows, a window and
// a desktop; this decision is none of those, and it is the part that can put a
// black drawing on a black bar.

#include <catch2/catch_test_macros.hpp>

#include "ui/TitleBarIcon.h"

using namespace pedalboard3::ui;

TEST_CASE("titleBarToneFor: a plain light theme takes the black drawing", "[ui][titlebar]")
{
    REQUIRE(titleBarToneFor(/*appsUseLightTheme*/ true, /*colorPrevalence*/ false) == TitleBarTone::Light);
}

TEST_CASE("titleBarToneFor: a plain dark theme takes the white drawing", "[ui][titlebar]")
{
    REQUIRE(titleBarToneFor(false, false) == TitleBarTone::Dark);
}

TEST_CASE("titleBarToneFor: an accent-coloured title bar is dark whatever the theme says", "[ui][titlebar]")
{
    // The one that matters, and the one a light/dark check alone gets wrong.
    // Windows only offers accent colours dark enough to carry white caption
    // text, so a black glyph on one vanishes -- and "light theme with accent
    // title bars" is an ordinary way to have Windows set up.
    REQUIRE(titleBarToneFor(true, true) == TitleBarTone::Dark);
    REQUIRE(titleBarToneFor(false, true) == TitleBarTone::Dark);
}
