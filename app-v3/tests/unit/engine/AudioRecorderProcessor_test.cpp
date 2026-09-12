// Tests for engine::processors::AudioRecorderProcessor (M6 U7): takes are
// recorded exactly, synced takes start on every transport play (AC-TR-4), and
// nothing is ever overwritten.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/DiskThread.h"
#include "engine/processors/AudioRecorderProcessor.h"
#include "engine/Transport.h"

#include <memory>

using namespace pedalboard3::engine;
using Catch::Approx;
using processors::AudioRecorderProcessor;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

float signal(std::int64_t frame)
{
    return static_cast<float>((frame % 1000) - 500) / 1000.0f; // a sawtooth within +-0.5
}

juce::AudioParameterBool& parameter(AudioRecorderProcessor& recorder, const juce::String& id)
{
    for (auto* p : recorder.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p); b != nullptr && b->paramID == id)
            return *b;
    FAIL("no parameter " << id);
    throw std::logic_error("unreachable");
}

struct Rig
{
    DiskThread disk;
    juce::File folder;
    std::unique_ptr<AudioRecorderProcessor> recorder;
    std::int64_t frame{0};

    Rig()
    {
        folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pb3_recorder", "");
        recorder = std::make_unique<AudioRecorderProcessor>(disk);
        recorder->setFolder(folder);
        recorder->setTakePrefix("Test Patch");
        recorder->prepareToPlay(kRate, kBlock);
    }

    ~Rig()
    {
        recorder.reset();
        folder.deleteRecursively();
    }

    // One block of the test signal through the recorder; returns it, as it came out.
    juce::AudioBuffer<float> block()
    {
        juce::AudioBuffer<float> buffer(2, kBlock);
        for (int n = 0; n < kBlock; ++n)
        {
            buffer.setSample(0, n, signal(frame + n));
            buffer.setSample(1, n, -signal(frame + n));
        }
        frame += kBlock;
        juce::MidiBuffer midi;
        recorder->processBlock(buffer, midi);
        return buffer;
    }

    bool waitForTakes(int count)
    {
        for (int i = 0; i < 500; ++i)
        {
            if (recorder->getTakeCount() >= count)
                return true;
            juce::Thread::sleep(10);
        }
        return false;
    }

    juce::Array<juce::File> takes() const { return folder.findChildFiles(juce::File::findFiles, false, "*.wav"); }
};

std::unique_ptr<juce::AudioFormatReader> readerFor(const juce::File& file)
{
    static juce::AudioFormatManager formats;
    if (formats.getNumKnownFormats() == 0)
        formats.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
}

} // namespace

TEST_CASE("AudioRecorder: a take holds exactly what passed through, and the audio carries on unchanged",
          "[engine][processors][recorder]")
{
    Rig rig;
    rig.recorder->toggleRecord();
    const std::int64_t first = rig.frame;
    for (int b = 0; b < 10; ++b)
    {
        const auto out = rig.block();
        REQUIRE(out.getSample(0, 7) == Approx(signal(rig.frame - kBlock + 7))); // passed through
    }
    rig.recorder->toggleRecord();
    rig.block(); // the audio thread sees the stop
    REQUIRE(rig.waitForTakes(1));

    const auto take = rig.recorder->getLastTake();
    REQUIRE(take.existsAsFile());
    REQUIRE(take.getFileName().startsWith("Test Patch "));
    auto reader = readerFor(take);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->numChannels == 2);
    REQUIRE(reader->bitsPerSample == 24);
    REQUIRE(reader->sampleRate == Approx(kRate));
    REQUIRE(reader->lengthInSamples == 10 * kBlock);

    juce::AudioBuffer<float> audio(2, 10 * kBlock);
    reader->read(&audio, 0, 10 * kBlock, 0, true, true);
    for (int n = 0; n < 10 * kBlock; n += 97)
    {
        REQUIRE(audio.getSample(0, n) == Approx(signal(first + n)).margin(1e-5));
        REQUIRE(audio.getSample(1, n) == Approx(-signal(first + n)).margin(1e-5));
    }
}

TEST_CASE("AudioRecorder: synced, every transport play starts a take and stop ends it",
          "[engine][processors][recorder][transport]")
{
    Rig rig;
    Transport transport;
    rig.recorder->setPlayHead(&transport);
    parameter(*rig.recorder, "sync").setValueNotifyingHost(1.0f);

    const auto run = [&](int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            transport.beginBlock(kRate);
            rig.block();
            transport.endBlock(kBlock);
        }
    };
    for (int take = 0; take < 3; ++take)
    {
        transport.play(); // as a MIDI-mapped Play would
        run(4);
        REQUIRE(rig.recorder->isRecording());
        transport.stop();
        run(2);
        REQUIRE_FALSE(rig.recorder->isRecording());
    }
    REQUIRE(rig.waitForTakes(3));
    REQUIRE(rig.takes().size() == 3); // three plays, three separate files
    for (const auto& take : rig.takes())
        REQUIRE(readerFor(take)->lengthInSamples == 4 * kBlock);
}

TEST_CASE("AudioRecorder: a mapped Record acts on the press", "[engine][processors][recorder]")
{
    Rig rig;
    auto& record = parameter(*rig.recorder, "record");
    record.setValueNotifyingHost(1.0f); // press: start
    record.setValueNotifyingHost(0.0f); // release: nothing
    rig.block();
    rig.block();
    REQUIRE(rig.recorder->isRecording());
    REQUIRE(rig.recorder->getTakeSeconds() == Approx(2.0 * kBlock / kRate));
    record.setValueNotifyingHost(1.0f); // press: stop
    rig.block();
    REQUIRE(rig.waitForTakes(1));
}

TEST_CASE("AudioRecorder: an empty take leaves no file, and the folder round-trips", "[engine][processors][recorder]")
{
    Rig rig;
    rig.recorder->toggleRecord();
    rig.recorder->toggleRecord();
    rig.block();
    juce::Thread::sleep(200);
    REQUIRE(rig.recorder->getTakeCount() == 0);
    REQUIRE(rig.takes().isEmpty());

    juce::MemoryBlock state;
    rig.recorder->getStateInformation(state);
    AudioRecorderProcessor restored(rig.disk);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(restored.getFolder() == rig.folder);

    AudioRecorderProcessor fresh(rig.disk);
    REQUIRE(fresh.getFolder().getFileName() == "Recordings"); // the default
}

TEST_CASE("AudioRecorder: a take still recording when the node goes is kept", "[engine][processors][recorder]")
{
    Rig rig;
    rig.recorder->toggleRecord();
    rig.block();
    rig.block();
    rig.recorder.reset(); // removed mid-take
    const auto takes = rig.folder.findChildFiles(juce::File::findFiles, false, "*.wav");
    REQUIRE(takes.size() == 1);
    REQUIRE(readerFor(takes[0])->lengthInSamples == 2 * kBlock);
}
