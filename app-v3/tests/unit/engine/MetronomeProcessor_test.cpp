// Tests for engine::processors::MetronomeProcessor (M6 U9): clicks land on the
// exact beat samples with the accent on beat 1, synced or on its own, at the
// right pitch at any rate, and a change of sync never jumps (AC-TR-3).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/DiskThread.h"
#include "engine/processors/MetronomeProcessor.h"
#include "engine/Transport.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace pedalboard3::engine;
using Catch::Approx;
using processors::MetronomeProcessor;

namespace
{

constexpr int kBlock = 512;

template <typename Parameter>
Parameter& parameter(MetronomeProcessor& m, const juce::String& id)
{
    for (auto* p : m.getParameters())
        if (auto* typed = dynamic_cast<Parameter*>(p); typed != nullptr && typed->paramID == id)
            return *typed;
    FAIL("no parameter " << id);
    throw std::logic_error("unreachable");
}

struct Rig
{
    DiskThread disk;
    MetronomeProcessor metronome{disk};
    Transport transport;
    double rate;

    explicit Rig(double sampleRate = 48000.0) : rate(sampleRate)
    {
        metronome.prepareToPlay(rate, kBlock);
        metronome.setPlayHead(&transport);
    }

    // `blocks` blocks of silence through the metronome, as one long recording.
    std::vector<float> run(int blocks)
    {
        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(blocks * kBlock));
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buffer(2, kBlock);
            buffer.clear();
            transport.beginBlock(rate);
            metronome.processBlock(buffer, midi);
            transport.endBlock(kBlock);
            for (int n = 0; n < kBlock; ++n)
                out.push_back(buffer.getSample(0, n));
        }
        return out;
    }
};

// Where each click starts: a zero sample (the sine's start) after a silence,
// followed by sound.
std::vector<int> onsets(const std::vector<float>& out)
{
    std::vector<int> found;
    int silent = 100; // the start counts as silence
    for (std::size_t i = 0; i + 1 < out.size(); ++i)
    {
        if (out[i] == 0.0f)
        {
            if (silent >= 100 && out[i + 1] != 0.0f)
                found.push_back(static_cast<int>(i));
            ++silent;
        }
        else
        {
            silent = 0;
        }
    }
    return found;
}

// Zero crossings in the 20 ms after `start`, as a frequency.
double pitchAt(const std::vector<float>& out, int start, double rate)
{
    const int length = static_cast<int>(rate * 0.02);
    REQUIRE(static_cast<std::size_t>(start + length) <= out.size()); // a short run fails here, not in the CRT
    int crossings = 0;
    for (int i = start + 2; i < start + length; ++i)
        if ((out[static_cast<std::size_t>(i - 1)] < 0.0f) != (out[static_cast<std::size_t>(i)] < 0.0f))
            ++crossings;
    return crossings / 2.0 / 0.02;
}

bool isAccent(const std::vector<float>& out, int start, double rate)
{
    return pitchAt(out, start, rate) > 660.0; // 880 Hz accent vs 440 Hz click
}

} // namespace

TEST_CASE("Metronome: synced at 120 BPM in 4/4, clicks land on the beat samples, accent on beat 1",
          "[engine][processors][metronome][transport]")
{
    Rig rig;
    parameter<juce::AudioParameterBool>(rig.metronome, "sync").setValueNotifyingHost(1.0f);
    rig.transport.play();
    const auto out = rig.run(200); // two seconds and a bit, so the last click rings for 20 ms

    const auto found = onsets(out);
    REQUIRE(found == std::vector<int>{0, 24000, 48000, 72000, 96000});
    REQUIRE(isAccent(out, 0, rig.rate));
    REQUIRE_FALSE(isAccent(out, 24000, rig.rate));
    REQUIRE_FALSE(isAccent(out, 72000, rig.rate));
    REQUIRE(isAccent(out, 96000, rig.rate)); // the next bar
}

TEST_CASE("Metronome: synced at 90 BPM in 7/8, it clicks every eighth note, accent every seven",
          "[engine][processors][metronome][transport]")
{
    Rig rig;
    rig.transport.setTempo(90.0);
    rig.transport.setTimeSignature(7, 8);
    parameter<juce::AudioParameterBool>(rig.metronome, "sync").setValueNotifyingHost(1.0f);
    rig.transport.play();
    const auto out = rig.run(260); // 8.3 beats of 16000 samples

    const auto found = onsets(out);
    REQUIRE(found.size() == 9);
    for (std::size_t i = 0; i < found.size(); ++i)
    {
        REQUIRE(found[i] == static_cast<int>(i) * 16000);
        REQUIRE(isAccent(out, found[i], rig.rate) == (i % 7 == 0));
    }
}

TEST_CASE("Metronome: stopped transport, synced: silent; Return to Zero starts the count again",
          "[engine][processors][metronome][transport]")
{
    Rig rig;
    parameter<juce::AudioParameterBool>(rig.metronome, "sync").setValueNotifyingHost(1.0f);
    REQUIRE(onsets(rig.run(100)).empty());

    rig.transport.play();
    rig.run(60); // past beat 2
    rig.transport.returnToZero();
    const auto out = rig.run(10);
    REQUIRE(onsets(out).front() == 0);
    REQUIRE(isAccent(out, 0, rig.rate));
}

TEST_CASE("Metronome: unsynced, Play starts it on beat 1 at the patch tempo, in its own time signature",
          "[engine][processors][metronome]")
{
    Rig rig;
    parameter<juce::AudioParameterInt>(rig.metronome, "numerator") = 3;
    rig.metronome.togglePlay();
    const auto out = rig.run(188);

    const auto found = onsets(out);
    REQUIRE(found == std::vector<int>{0, 24000, 48000, 72000, 96000});
    REQUIRE(isAccent(out, 0, rig.rate));
    REQUIRE_FALSE(isAccent(out, 24000, rig.rate));
    REQUIRE(isAccent(out, 72000, rig.rate)); // beat 1 of the second 3/4 bar
    REQUIRE(rig.metronome.isRunning());

    rig.metronome.togglePlay();
    rig.run(20);
    REQUIRE(onsets(rig.run(100)).empty());
}

TEST_CASE("Metronome: turning Sync on or off mid-click never jumps", "[engine][processors][metronome][transport]")
{
    Rig rig;
    rig.metronome.togglePlay();
    const auto steady = rig.run(4); // the first click, ringing
    float steadySlope = 0.0f;
    for (std::size_t i = 1; i < steady.size(); ++i)
        steadySlope = juce::jmax(steadySlope, std::abs(steady[i] - steady[i - 1]));

    Rig toggled;
    toggled.metronome.togglePlay();
    auto before = toggled.run(1);
    parameter<juce::AudioParameterBool>(toggled.metronome, "sync").setValueNotifyingHost(1.0f); // transport stopped
    auto after = toggled.run(4);
    before.insert(before.end(), after.begin(), after.end());
    for (std::size_t i = 1; i < before.size(); ++i)
        REQUIRE(std::abs(before[i] - before[i - 1]) <= steadySlope * 1.01f);
}

TEST_CASE("Metronome: the click has the same pitch at 44.1 and 96 kHz", "[engine][processors][metronome]")
{
    for (const double rate : {44100.0, 96000.0})
    {
        Rig rig(rate);
        rig.metronome.togglePlay();
        const auto out = rig.run(8);
        REQUIRE(pitchAt(out, 0, rate) == Approx(MetronomeProcessor::kAccentHz).epsilon(0.05));
    }
}

TEST_CASE("Metronome: its settings round-trip through the node state", "[engine][processors][metronome]")
{
    DiskThread disk;
    MetronomeProcessor a(disk);
    parameter<juce::AudioParameterInt>(a, "numerator") = 5;
    parameter<juce::AudioParameterChoice>(a, "denominator") = 3; // "8"
    parameter<juce::AudioParameterFloat>(a, "level") = -12.0f;
    parameter<juce::AudioParameterBool>(a, "sync") = true;
    juce::MemoryBlock state;
    a.getStateInformation(state);

    MetronomeProcessor b(disk);
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(parameter<juce::AudioParameterInt>(b, "numerator").get() == 5);
    REQUIRE(parameter<juce::AudioParameterChoice>(b, "denominator").getCurrentChoiceName() == "8");
    REQUIRE(parameter<juce::AudioParameterFloat>(b, "level").get() == Approx(-12.0f).margin(0.01f));
    REQUIRE(parameter<juce::AudioParameterBool>(b, "sync").get());
}

TEST_CASE("Metronome: a click sample plays in place of the synthesized click", "[engine][processors][metronome]")
{
    // A 100-sample ramp as both the accent and the click.
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pb3_click", ".wav");
    {
        juce::AudioBuffer<float> ramp(1, 100);
        for (int n = 0; n < 100; ++n)
            ramp.setSample(0, n, (n + 1) / 200.0f);
        juce::WavAudioFormat wav;
        auto stream = std::make_unique<juce::FileOutputStream>(file);
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000.0, 1, 32, {}, 0));
        REQUIRE(writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer(ramp, 0, 100);
    }

    Rig rig;
    parameter<juce::AudioParameterFloat>(rig.metronome, "level") = 0.0f; // unity
    rig.metronome.setSampleFiles(file, file);
    for (int i = 0; i < 300 && rig.metronome.isLoading(); ++i)
        juce::Thread::sleep(10);
    REQUIRE(rig.metronome.getLoadError().isEmpty());

    rig.metronome.togglePlay();
    const auto out = rig.run(2);
    REQUIRE(out[0] == Approx(1 / 200.0f).margin(1e-6));
    REQUIRE(out[99] == Approx(100 / 200.0f).margin(1e-6));
    REQUIRE(out[100] == 0.0f);
    file.deleteFile();
}
