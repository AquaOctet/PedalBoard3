// Tests for engine::transport (engine/TransportMath.h): the transport's
// position maths (M6 U1).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/TransportMath.h"

using namespace pedalboard3::engine;
using Catch::Approx;

TEST_CASE("TransportMath: one second at 120 BPM is two quarter notes", "[engine][transport]")
{
    REQUIRE(transport::advancePpq(0.0, 48000, 48000.0, 120.0) == Approx(2.0));
    REQUIRE(transport::advancePpq(2.0, 44100, 44100.0, 60.0) == Approx(3.0));
    REQUIRE(transport::advancePpq(1.5, 512, 0.0, 120.0) == Approx(1.5)); // no sample rate yet: holds
}

TEST_CASE("TransportMath: bar length and bar start follow the time signature", "[engine][transport]")
{
    REQUIRE(transport::quarterNotesPerBar(4, 4) == Approx(4.0));
    REQUIRE(transport::quarterNotesPerBar(7, 8) == Approx(3.5));
    REQUIRE(transport::quarterNotesPerBar(6, 8) == Approx(3.0));
    REQUIRE(transport::quarterNotesPerBar(3, 2) == Approx(6.0));

    REQUIRE(transport::barStartPpq(9.5, 4, 4) == Approx(8.0));
    REQUIRE(transport::barsBefore(9.5, 4, 4) == 2);
    REQUIRE(transport::barStartPpq(4.0, 7, 8) == Approx(3.5));
    REQUIRE(transport::barStartPpq(3.4999999999, 7, 8) == Approx(3.5)); // accumulated onto the bar line
    REQUIRE(transport::barStartPpq(0.0, 4, 4) == Approx(0.0));
}

TEST_CASE("TransportMath: the readout is bar.beat, counted from 1", "[engine][transport]")
{
    REQUIRE(transport::barsBeatsText(0.0, 4, 4) == "1.1");
    REQUIRE(transport::barsBeatsText(5.0, 4, 4) == "2.2");
    REQUIRE(transport::barsBeatsText(3.99, 4, 4) == "1.4");
    REQUIRE(transport::barsBeatsText(3.75, 7, 8) == "2.1"); // 7/8: bars of 3.5 quarter notes, beats of 0.5
    REQUIRE(transport::barsBeatsText(4.0, 7, 8) == "2.2");
}

TEST_CASE("TransportMath: tempo clamps and time signatures are checked", "[engine][transport]")
{
    REQUIRE(transport::clampTempo(10.0) == Approx(20.0));
    REQUIRE(transport::clampTempo(999.0) == Approx(300.0));
    REQUIRE(transport::clampTempo(128.5) == Approx(128.5));

    REQUIRE(transport::isValidTimeSignature(4, 4));
    REQUIRE(transport::isValidTimeSignature(7, 8));
    REQUIRE(transport::isValidTimeSignature(16, 16));
    REQUIRE_FALSE(transport::isValidTimeSignature(0, 4));
    REQUIRE_FALSE(transport::isValidTimeSignature(17, 4));
    REQUIRE_FALSE(transport::isValidTimeSignature(4, 3));
    REQUIRE_FALSE(transport::isValidTimeSignature(4, 0));
}
