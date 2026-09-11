// Tests for ui::meter (ui/MeterMath.h): the top bar's CPU meter zones,
// smoothing and peak hold, and the activity lights' blink (UI refresh U2 / U3).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/MeterMath.h"

using namespace pedalboard3::ui;
using Catch::Approx;

TEST_CASE("MeterMath: zones switch at the 70% tick and at 90%", "[ui][meter]")
{
    REQUIRE(meter::zoneFor(0.0f) == meter::Zone::Normal);
    REQUIRE(meter::zoneFor(0.69f) == meter::Zone::Normal);
    REQUIRE(meter::zoneFor(0.70f) == meter::Zone::Warning);
    REQUIRE(meter::zoneFor(0.89f) == meter::Zone::Warning);
    REQUIRE(meter::zoneFor(0.90f) == meter::Zone::Critical);
    REQUIRE(meter::zoneFor(1.5f) == meter::Zone::Critical);
}

TEST_CASE("MeterMath: smoothing converges on the target and never overshoots", "[ui][meter]")
{
    float v = 0.0f;
    for (int i = 0; i < 60; ++i)
    {
        const float next = meter::smooth(v, 0.8f, 0.35f);
        REQUIRE(next >= v);
        REQUIRE(next <= 0.8f + 1.0e-6f);
        v = next;
    }
    REQUIRE(v == Approx(0.8f).margin(0.001));

    // The step is clamped: even a huge amount lands on the target, no further.
    REQUIRE(meter::smooth(0.5f, 0.2f, 2.0f) == Approx(0.2f));
}

TEST_CASE("MeterMath: the peak holds for its hold time, then falls to the current value", "[ui][meter]")
{
    meter::PeakHold p(1000.0);
    REQUIRE(p.update(0.6f, 0.0) == Approx(0.6f));
    REQUIRE(p.update(0.3f, 500.0) == Approx(0.6f));  // held
    REQUIRE(p.update(0.9f, 600.0) == Approx(0.9f));  // a higher value takes over at once
    REQUIRE(p.update(0.2f, 1500.0) == Approx(0.9f)); // still within 1 s of the 0.9
    REQUIRE(p.update(0.2f, 1700.0) == Approx(0.2f)); // the hold is over
}

TEST_CASE("MeterMath: an activity light stays lit briefly after its counter moves", "[ui][meter]")
{
    meter::ActivityBlink b(120.0);
    REQUIRE_FALSE(b.update(5, 0.0)); // the first look only records the counter
    REQUIRE_FALSE(b.update(5, 50.0));
    REQUIRE(b.update(6, 100.0));       // moved: lit
    REQUIRE(b.update(6, 200.0));       // 100 ms later: still lit
    REQUIRE_FALSE(b.update(6, 230.0)); // 130 ms later: out
}
