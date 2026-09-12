// Tests for engine::processors::FilePlayerProcessor (M6 U6): the file is
// decoded on the disk thread and played from memory, sample-exact, on its own
// or synced to the transport.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/DiskThread.h"
#include "engine/processors/FilePlayerProcessor.h"
#include "engine/Transport.h"

#include <memory>

using namespace pedalboard3::engine;
using Catch::Approx;
using processors::FilePlayerProcessor;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

float ramp(int n, int length)
{
    return static_cast<float>(n) / static_cast<float>(length);
}

// A 32-bit float stereo WAV: left ramps up from 0, right is its negative.
juce::File writeTestWav(double rate, int length)
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("pb3_fileplayer_test", ".wav");
    juce::AudioBuffer<float> audio(2, length);
    for (int n = 0; n < length; ++n)
    {
        audio.setSample(0, n, ramp(n, length));
        audio.setSample(1, n, -ramp(n, length));
    }
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), rate, 2, 32, {}, 0));
    REQUIRE(writer != nullptr);
    stream.release(); // the writer owns it now
    writer->writeFromAudioSampleBuffer(audio, 0, length);
    return file;
}

bool waitLoaded(FilePlayerProcessor& player)
{
    for (int i = 0; i < 500; ++i)
    {
        if (!player.isLoading() && player.getLengthSeconds() > 0.0)
            return true;
        juce::Thread::sleep(10);
    }
    return false;
}

juce::AudioParameterBool& parameter(FilePlayerProcessor& player, const juce::String& id)
{
    for (auto* p : player.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p); b != nullptr && b->paramID == id)
            return *b;
    FAIL("no parameter " << id);
    throw std::logic_error("unreachable");
}

juce::AudioBuffer<float> block(FilePlayerProcessor& player)
{
    juce::AudioBuffer<float> buffer(2, kBlock);
    buffer.clear();
    juce::MidiBuffer midi;
    player.processBlock(buffer, midi);
    return buffer;
}

struct Rig
{
    DiskThread disk;
    FilePlayerProcessor player{disk};
    juce::File file;

    explicit Rig(int length, double fileRate = kRate)
    {
        player.prepareToPlay(kRate, kBlock);
        file = writeTestWav(fileRate, length);
        player.setFile(file);
        REQUIRE(waitLoaded(player));
    }

    ~Rig() { file.deleteFile(); }
};

} // namespace

TEST_CASE("FilePlayer: plays the file sample for sample, then Return to Zero starts it again",
          "[engine][processors][file-player]")
{
    Rig rig(4800);
    REQUIRE(rig.player.getLengthSeconds() == Approx(0.1));
    REQUIRE(block(rig.player).getMagnitude(0, kBlock) == 0.0f); // not playing yet

    rig.player.togglePlay();
    auto first = block(rig.player);
    auto second = block(rig.player);
    for (int n = 0; n < kBlock; ++n)
    {
        REQUIRE(first.getSample(0, n) == Approx(ramp(n, 4800)).margin(1e-6));
        REQUIRE(first.getSample(1, n) == Approx(-ramp(n, 4800)).margin(1e-6));
        REQUIRE(second.getSample(0, n) == Approx(ramp(kBlock + n, 4800)).margin(1e-6));
    }

    rig.player.returnToZero();
    REQUIRE(block(rig.player).getSample(0, 1) == Approx(ramp(1, 4800)).margin(1e-6));
}

TEST_CASE("FilePlayer: at the end it stops and rewinds, or loops when Loop is on", "[engine][processors][file-player]")
{
    Rig rig(1000);
    rig.player.togglePlay();
    block(rig.player);                   // 0..511
    const auto tail = block(rig.player); // 512..999, then nothing
    REQUIRE(tail.getSample(0, 487) == Approx(ramp(999, 1000)).margin(1e-6));
    REQUIRE(tail.getSample(0, 488) == 0.0f);
    REQUIRE(block(rig.player).getMagnitude(0, kBlock) == 0.0f); // stopped at the end: silent...
    REQUIRE_FALSE(rig.player.isPlaying());                      // ...and says so

    parameter(rig.player, "looping").setValueNotifyingHost(1.0f);
    rig.player.togglePlay(); // from the top again
    block(rig.player);
    const auto wrapped = block(rig.player);
    REQUIRE(wrapped.getSample(0, 488) == Approx(ramp(0, 1000)).margin(1e-6)); // wrapped to the start
}

TEST_CASE("FilePlayer: synced, it follows the transport through 1000 play / stop cycles without drifting",
          "[engine][processors][file-player][transport]")
{
    const int length = 48000;
    Rig rig(length);
    Transport transport;
    rig.player.setPlayHead(&transport);
    parameter(rig.player, "sync").setValueNotifyingHost(1.0f);
    parameter(rig.player, "looping").setValueNotifyingHost(1.0f);

    juce::Random random(42);
    int checked = 0;
    for (int cycle = 0; cycle < 1000; ++cycle)
    {
        transport.play();
        const int blocks = 1 + random.nextInt(4);
        for (int b = 0; b < blocks; ++b)
        {
            transport.beginBlock(kRate);
            const auto at = transport.getSamplePosition();
            const auto out = block(rig.player);
            transport.endBlock(kBlock);
            REQUIRE(out.getSample(0, 0) == Approx(ramp(static_cast<int>(at % length), length)).margin(1e-6));
            ++checked;
        }
        transport.pause();
        transport.beginBlock(kRate);
        REQUIRE(block(rig.player).getMagnitude(0, kBlock) == 0.0f); // paused: silent
        transport.endBlock(kBlock);
    }
    REQUIRE(checked > 1000);
}

TEST_CASE("FilePlayer: Trigger and a mapped Play act on a press", "[engine][processors][file-player]")
{
    Rig rig(4800);
    auto& trigger = parameter(rig.player, "trigger");
    trigger.setValueNotifyingHost(1.0f); // press: from the top, playing
    REQUIRE(block(rig.player).getSample(0, 1) == Approx(ramp(1, 4800)).margin(1e-6));
    trigger.setValueNotifyingHost(0.0f); // release: nothing
    REQUIRE(block(rig.player).getSample(0, 0) == Approx(ramp(kBlock, 4800)).margin(1e-6));

    auto& play = parameter(rig.player, "play");
    play.setValueNotifyingHost(1.0f); // press: pause
    play.setValueNotifyingHost(0.0f);
    REQUIRE(block(rig.player).getMagnitude(0, kBlock) == 0.0f);
}

TEST_CASE("FilePlayer: the file and its switches round-trip through the node state",
          "[engine][processors][file-player]")
{
    Rig rig(4800);
    parameter(rig.player, "looping").setValueNotifyingHost(1.0f);
    juce::MemoryBlock state;
    rig.player.getStateInformation(state);

    FilePlayerProcessor restored(rig.disk);
    restored.prepareToPlay(kRate, kBlock);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(restored.getFile() == rig.file);
    REQUIRE(parameter(restored, "looping").get());
    REQUIRE(waitLoaded(restored));
    REQUIRE(restored.getLengthSeconds() == Approx(0.1));

    // The state a dropped file gets.
    FilePlayerProcessor dropped(rig.disk);
    const auto dropState = FilePlayerProcessor::stateForFile(rig.file);
    dropped.setStateInformation(dropState.getData(), static_cast<int>(dropState.getSize()));
    REQUIRE(dropped.getFile() == rig.file);
}

TEST_CASE("FilePlayer: a file at another rate is resampled to the device rate", "[engine][processors][file-player]")
{
    Rig rig(4410, 44100.0); // 0.1 s at 44.1 kHz, played at 48 kHz
    REQUIRE(rig.player.getLengthSeconds() == Approx(0.1).margin(0.001));
    REQUIRE(rig.player.getOverview().size() == FilePlayerProcessor::kOverviewPoints);
}

TEST_CASE("FilePlayer: an unreadable file reports why and plays nothing", "[engine][processors][file-player]")
{
    DiskThread disk;
    FilePlayerProcessor player(disk);
    player.prepareToPlay(kRate, kBlock);
    auto bogus = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("pb3_not_audio.wav");
    REQUIRE(bogus.replaceWithText("not audio"));
    player.setFile(bogus);
    for (int i = 0; i < 200 && player.isLoading(); ++i)
        juce::Thread::sleep(10);
    REQUIRE(player.getLoadError().isNotEmpty());
    player.togglePlay();
    REQUIRE(block(player).getMagnitude(0, kBlock) == 0.0f);
    bogus.deleteFile();
}
