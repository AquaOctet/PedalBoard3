// Tests for session::TapTempo (session/TapTempo.h): the average of the last
// four taps, restarting after a gap of more than 2 s (M6 U3, AC-TR-2).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "session/TapTempo.h"

using Catch::Approx;
using pedalboard3::session::TapTempo;

TEST_CASE("TapTempo: taps half a second apart are 120 BPM, from the second tap", "[session][tap-tempo]")
{
    TapTempo tap;
    REQUIRE_FALSE(tap.tap(0.0).has_value());
    REQUIRE(*tap.tap(500.0) == Approx(120.0));
    REQUIRE(*tap.tap(1000.0) == Approx(120.0));
    REQUIRE(*tap.tap(1500.0) == Approx(120.0));
}

TEST_CASE("TapTempo: the tempo averages the last four taps", "[session][tap-tempo]")
{
    TapTempo tap;
    tap.tap(0.0);
    tap.tap(400.0);
    tap.tap(1000.0);
    REQUIRE(*tap.tap(1500.0) == Approx(120.0)); // (1500 - 0) / 3 = 500 ms

    // A fifth tap slides the window: 400, 1000, 1500, 1900 -> 500 ms again.
    REQUIRE(*tap.tap(1900.0) == Approx(120.0));
    // 1000, 1500, 1900, 2350 -> 450 ms = 133.3 BPM.
    REQUIRE(*tap.tap(2350.0) == Approx(133.3));
}

TEST_CASE("TapTempo: a gap of more than 2 s starts again", "[session][tap-tempo]")
{
    TapTempo tap;
    tap.tap(0.0);
    tap.tap(500.0);
    REQUIRE_FALSE(tap.tap(2600.0).has_value()); // 2.1 s after the last tap: a first tap again
    REQUIRE(*tap.tap(3350.0) == Approx(80.0));  // 750 ms from the restart
    // Exactly 2 s is still in the window: (5350 - 2600) / 2 = 1375 ms = 43.6 BPM.
    REQUIRE(*tap.tap(5350.0) == Approx(43.6));
}

TEST_CASE("TapTempo: the tempo is clamped and rounded to 0.1 BPM", "[session][tap-tempo]")
{
    TapTempo fast;
    fast.tap(0.0);
    REQUIRE(*fast.tap(100.0) == Approx(300.0)); // 600 BPM, clamped

    TapTempo odd;
    odd.tap(0.0);
    REQUIRE(*odd.tap(700.0) == Approx(85.7)); // 85.714...
}

TEST_CASE("TapTempo: a clock that goes backwards, or a repeated tap time, starts again", "[session][tap-tempo]")
{
    TapTempo tap;
    tap.tap(1000.0);
    tap.tap(1500.0);
    REQUIRE_FALSE(tap.tap(1500.0).has_value()); // same instant: no zero gap
    REQUIRE_FALSE(tap.tap(900.0).has_value());  // backwards
    REQUIRE(*tap.tap(1400.0) == Approx(120.0));

    tap.reset();
    REQUIRE_FALSE(tap.tap(2000.0).has_value());
}
