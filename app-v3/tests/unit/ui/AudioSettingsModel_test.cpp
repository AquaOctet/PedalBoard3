// Tests for the Audio Settings panel's headless part (ui/AudioSettingsModel.h;
// UI chrome plan docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U6):
// latency text, channel pairs, and the rate and buffer choices.

#include <catch2/catch_test_macros.hpp>

#include "ui/AudioSettingsModel.h"

using namespace pedalboard3::ui;

TEST_CASE("AudioSettingsModel: latency is samples over the rate, rounded to 0.1 ms", "[ui][audio]")
{
    REQUIRE(audiosettings::latencyText(256, 48000.0) == "5.3 ms");
    REQUIRE(audiosettings::latencyText(480, 48000.0) == "10.0 ms");
    REQUIRE(audiosettings::latencyText(128, 44100.0) == "2.9 ms");
    REQUIRE(audiosettings::latencyText(64, 96000.0) == "0.7 ms");
    REQUIRE(audiosettings::latencyText(256, 0.0).isEmpty()); // no rate yet
}

TEST_CASE("AudioSettingsModel: each direction uses what the device reports, or its buffer", "[ui][audio]")
{
    REQUIRE(audiosettings::directionLatencySamples(576, 480) == 576);
    REQUIRE(audiosettings::directionLatencySamples(0, 480) == 480);
}

TEST_CASE("AudioSettingsModel: channels pair up as 1-2, 3-4 and a lone 5", "[ui][audio]")
{
    const auto dash = audiosettings::pairDash();
    const auto pairs = audiosettings::channelPairs(5);

    REQUIRE(pairs.size() == 3);
    REQUIRE(pairs[0].label == "1" + dash + "2");
    REQUIRE(pairs[1].label == "3" + dash + "4");
    REQUIRE(pairs[2].label == "5");
    REQUIRE(pairs[2].first == 4);
    REQUIRE(pairs[2].count == 1);
    REQUIRE(audiosettings::channelPairs(0).empty());
}

TEST_CASE("AudioSettingsModel: a chip turns its whole pair off when any of it is on, else on", "[ui][audio]")
{
    const auto pairs = audiosettings::channelPairs(4);
    juce::BigInteger channels;
    channels.setBit(0); // only the left of 1-2

    REQUIRE(audiosettings::isPairOn(channels, pairs[0]));
    REQUIRE_FALSE(audiosettings::isPairOn(channels, pairs[1]));

    channels = audiosettings::togglePair(channels, pairs[0]);
    REQUIRE_FALSE(channels[0]);
    REQUIRE_FALSE(channels[1]);

    channels = audiosettings::togglePair(channels, pairs[1]);
    REQUIRE(channels[2]);
    REQUIRE(channels[3]);
}

TEST_CASE("AudioSettingsModel: rates read in kHz and buffers with their latency", "[ui][audio]")
{
    REQUIRE(audiosettings::rateText(44100.0) == "44.1 kHz");
    REQUIRE(audiosettings::rateText(48000.0) == "48 kHz");
    REQUIRE(audiosettings::rateText(176400.0) == "176.4 kHz");
    REQUIRE(audiosettings::bufferText(256, 48000.0) == "256 samples (5.3 ms)");
    REQUIRE(audiosettings::bufferText(256, 0.0) == "256 samples");
}

TEST_CASE("AudioSettingsModel: the buffer list keeps the device's own values", "[ui][audio]")
{
    const juce::Array<int> device{480, 960, 1440};
    REQUIRE(audiosettings::bufferChoices(device, 480) == device);

    // A current size the device didn't list joins the list, in order.
    REQUIRE(audiosettings::bufferChoices({64, 128, 256}, 100) == juce::Array<int>{64, 100, 128, 256});
}
