// Tests for engine::Transport (engine/Transport.h): the transport and the
// play head it gives plugins (M6 U1).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Transport.h"

using namespace pedalboard3::engine;
using Catch::Approx;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480; // 100 blocks = one second

// The audio thread's side: begin and end `blocks` blocks.
void run(Transport& transport, int blocks)
{
    for (int i = 0; i < blocks; ++i)
    {
        transport.beginBlock(kRate);
        transport.endBlock(kBlock);
    }
}
} // namespace

TEST_CASE("Transport: the position advances only while playing", "[engine][transport]")
{
    Transport transport;
    run(transport, 10);
    REQUIRE(transport.getSamplePosition() == 0);

    transport.play();
    run(transport, 100);
    REQUIRE(transport.getSamplePosition() == 48000);
    REQUIRE(transport.getPpqPosition() == Approx(2.0)); // 120 BPM

    transport.pause();
    run(transport, 10);
    REQUIRE(transport.getSamplePosition() == 48000); // paused: holds
    REQUIRE_FALSE(transport.isPlaying());

    transport.togglePlayPause();
    run(transport, 100);
    REQUIRE(transport.getSamplePosition() == 96000); // carries on from where it paused
}

TEST_CASE("Transport: plugins see the block's position, tempo and time signature", "[engine][transport]")
{
    Transport transport;
    transport.setTempo(90.0);
    transport.setTimeSignature(7, 8);
    transport.play();
    run(transport, 200); // two seconds at 90 BPM = 3 quarter notes

    transport.beginBlock(kRate);
    const auto position = transport.getPosition();
    REQUIRE(position.hasValue());
    REQUIRE(*position->getBpm() == Approx(90.0));
    REQUIRE(position->getTimeSignature()->numerator == 7);
    REQUIRE(position->getTimeSignature()->denominator == 8);
    REQUIRE(*position->getTimeInSamples() == 96000);
    REQUIRE(*position->getTimeInSeconds() == Approx(2.0));
    REQUIRE(*position->getPpqPosition() == Approx(3.0));
    REQUIRE(*position->getPpqPositionOfLastBarStart() == Approx(0.0)); // a 7/8 bar is 3.5 quarter notes
    REQUIRE(*position->getBarCount() == 0);
    REQUIRE(position->getIsPlaying());
    REQUIRE_FALSE(position->getIsRecording());
}

TEST_CASE("Transport: Return to Zero lands at the next block, and playing carries on", "[engine][transport]")
{
    Transport transport;
    transport.play();
    run(transport, 50);

    transport.beginBlock(kRate);
    transport.returnToZero(); // asked for mid-block
    transport.endBlock(kBlock);
    REQUIRE(transport.getSamplePosition() == 51 * kBlock); // this block still counts

    transport.beginBlock(kRate); // ...then the next one starts at 0
    REQUIRE(*transport.getPosition()->getTimeInSamples() == 0);
    transport.endBlock(kBlock);
    REQUIRE(transport.getSamplePosition() == kBlock);
    REQUIRE(transport.isPlaying());
}

TEST_CASE("Transport: Stop pauses and returns to zero", "[engine][transport]")
{
    Transport transport;
    transport.play();
    run(transport, 50);
    transport.stop();
    run(transport, 5);
    REQUIRE_FALSE(transport.isPlaying());
    REQUIRE(transport.getSamplePosition() == 0);
    REQUIRE(transport.getPpqPosition() == Approx(0.0));
}

TEST_CASE("Transport: a tempo change while playing keeps the position continuous", "[engine][transport]")
{
    Transport transport;
    transport.play();
    run(transport, 100); // 1 s at 120: 2 quarter notes
    transport.setTempo(60.0);
    run(transport, 100); // 1 s at 60: 1 more
    REQUIRE(transport.getPpqPosition() == Approx(3.0));
}

TEST_CASE("Transport: tempo clamps, and an invalid time signature is ignored", "[engine][transport]")
{
    Transport transport;
    transport.setTempo(1000.0);
    REQUIRE(transport.getTempo() == Approx(300.0));
    transport.setTempo(std::nan(""));
    REQUIRE(transport.getTempo() == Approx(300.0));

    transport.setTimeSignature(6, 8);
    transport.setTimeSignature(5, 3); // not a note value
    REQUIRE(transport.getNumerator() == 6);
    REQUIRE(transport.getDenominator() == 8);
}
