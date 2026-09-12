// Tests for engine::processors::LooperProcessor (M6 U8): record then play is
// sample-exact, the loop point has no step, Stop After Bar ends on the bar,
// running out of memory ends cleanly, synced loops follow the transport, and
// the loop's take, settings and audio round-trip through the node state.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/DiskThread.h"
#include "engine/processors/LooperProcessor.h"
#include "engine/Transport.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace pedalboard3::engine;
using Catch::Approx;
using processors::LooperProcessor;

namespace
{

constexpr int kBlock = 512;
constexpr double kRate = 48000.0;

template <typename Parameter>
Parameter& parameter(LooperProcessor& looper, const juce::String& id)
{
    for (auto* p : looper.getParameters())
        if (auto* typed = dynamic_cast<Parameter*>(p); typed != nullptr && typed->paramID == id)
            return *typed;
    FAIL("no parameter " << id);
    throw std::logic_error("unreachable");
}

// A folder for takes that goes, with its takes, after everything else.
struct TempFolder
{
    juce::File dir{juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pb3_looper", "")};
    TempFolder() { dir.createDirectory(); }
    ~TempFolder() { dir.deleteRecursively(); }
};

struct Rig
{
    TempFolder temp;
    DiskThread disk;
    LooperProcessor looper;
    Transport transport;
    std::int64_t frame{0}; // frames run so far

    explicit Rig(double maxSeconds = LooperProcessor::kDefaultMaxSeconds) : looper(disk, maxSeconds)
    {
        looper.setFolder(temp.dir);
        looper.prepareToPlay(kRate, kBlock);
        looper.setPlayHead(&transport);
    }

    // `blocks` blocks of input(frame) on both channels (silence without one);
    // returns channel 0 of the output.
    std::vector<float> run(int blocks, const std::function<float(std::int64_t)>& input = {})
    {
        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(blocks * kBlock));
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            for (int n = 0; n < kBlock; ++n)
            {
                const float value = input ? input(frame + n) : 0.0f;
                buffer.setSample(0, n, value);
                buffer.setSample(1, n, value);
            }
            transport.beginBlock(kRate);
            looper.processBlock(buffer, midi);
            transport.endBlock(kBlock);
            for (int n = 0; n < kBlock; ++n)
                out.push_back(buffer.getSample(0, n));
            frame += kBlock;
        }
        return out;
    }
};

float signal(std::int64_t g)
{
    const auto x = static_cast<double>(g);
    return static_cast<float>(0.4 * std::sin(x * 0.0123) + 0.1 * std::sin(x * 0.37));
}

float ramp(std::int64_t g)
{
    return static_cast<float>((g % 1000) + 1) / 2000.0f;
}

float at(const std::vector<float>& out, std::int64_t i)
{
    return out[static_cast<std::size_t>(i)];
}

bool waitFor(const std::function<bool()>& done)
{
    for (int i = 0; i < 500 && !done(); ++i)
        juce::Thread::sleep(10);
    return done();
}

} // namespace

TEST_CASE("Looper: record then play is sample-exact", "[engine][processors][looper]")
{
    Rig rig;
    rig.run(4); // silence before the loop
    rig.looper.toggleRecord();
    const auto recordStart = rig.frame;
    rig.run(40, signal);
    rig.looper.toggleRecord();
    const auto out = rig.run(100); // silence in; Auto-play plays the loop

    constexpr std::int64_t length = 40 * kBlock;
    REQUIRE(rig.looper.getLoopLength() == length);
    REQUIRE_FALSE(rig.looper.isRecording());
    REQUIRE(rig.looper.isPlaying());

    // The first pass after its 128-sample fade-in, up to the crossfade, then
    // the whole second pass up to the crossfade: exactly what was recorded.
    constexpr std::int64_t fade = LooperProcessor::kFadeFrames;
    int bad = 0;
    for (std::int64_t j = fade - 1; j < length - fade; ++j)
        if (at(out, j) != signal(recordStart + j))
            ++bad;
    for (std::int64_t j = 0; j < length - fade; ++j)
        if (at(out, length + j) != signal(recordStart + j))
            ++bad;
    REQUIRE(bad == 0);
}

TEST_CASE("Looper: the loop point is seamless, with no step where it wraps", "[engine][processors][looper]")
{
    Rig rig;
    parameter<juce::AudioParameterFloat>(rig.looper, "inputLevel") = -60.0f; // hear only the loop
    constexpr int loopBlocks = 37;
    const double hz = 174.5 * kRate / (loopBlocks * kBlock); // half a cycle out at the wrap
    const auto sine = [hz](std::int64_t g)
    {
        return static_cast<float>(0.5 *
                                  std::sin(juce::MathConstants<double>::twoPi * hz * static_cast<double>(g) / kRate));
    };

    rig.run(3, sine);
    rig.looper.toggleRecord();
    const auto start = rig.frame;
    rig.run(loopBlocks, sine);
    rig.looper.toggleRecord();
    const auto out = rig.run(120, sine);

    constexpr std::int64_t length = loopBlocks * kBlock;
    REQUIRE(rig.looper.getLoopLength() == length);
    // Without the crossfade the wrap would jump by this much.
    REQUIRE(std::abs(sine(start + length - 1) - sine(start)) > 0.3f);

    const double slope = 0.5 * juce::MathConstants<double>::twoPi * hz / kRate; // the sine's own steepest step
    double worst = 0.0;
    for (std::size_t i = 1; i < out.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(out[i] - out[i - 1])));
    REQUIRE(worst <= slope * 1.5);
    REQUIRE(worst >= slope * 0.9); // it really is playing
}

TEST_CASE("Looper: Stop After Bar ends the loop on the bar's exact sample, and it plays from the next",
          "[engine][processors][looper][transport]")
{
    Rig rig; // 120 BPM in 4/4: a bar is 96000 samples
    parameter<juce::AudioParameterBool>(rig.looper, "stopAfterBar") = true;
    rig.looper.toggleRecord();
    const auto out = rig.run(200, ramp);

    REQUIRE(rig.looper.getLoopLength() == 96000);
    REQUIRE_FALSE(rig.looper.isRecording());
    // From sample 96000: the input still passing through, plus the loop fading in from its start.
    int bad = 0;
    for (std::int64_t j = 0; j < 4000; ++j)
    {
        const float fade = std::min(1.0f, static_cast<float>(j + 1) / LooperProcessor::kFadeFrames);
        const float expected = ramp(96000 + j) + ramp(j) * fade;
        if (std::abs(at(out, 96000 + j) - expected) > 1e-6f)
            ++bad;
    }
    REQUIRE(bad == 0);
}

TEST_CASE("Looper: running out of loop memory ends the loop there, cleanly", "[engine][processors][looper]")
{
    Rig rig(0.5); // half a second: 24000 samples
    rig.looper.toggleRecord();
    rig.run(100, ramp);

    REQUIRE(rig.looper.getLoopLength() == 24000);
    REQUIRE(rig.looper.ranOutOfMemory());
    REQUIRE_FALSE(rig.looper.isRecording());
    REQUIRE(rig.looper.isPlaying()); // Auto-play
}

TEST_CASE("Looper: mapped Record, Return to Zero and Play act on the press", "[engine][processors][looper]")
{
    Rig rig;
    auto& record = parameter<juce::AudioParameterBool>(rig.looper, "record");
    record.setValueNotifyingHost(1.0f);
    rig.run(1, ramp);
    REQUIRE(rig.looper.isRecording());
    record.setValueNotifyingHost(0.0f); // the release does nothing
    rig.run(9, ramp);
    REQUIRE(rig.looper.isRecording());
    record.setValueNotifyingHost(1.0f);
    record.setValueNotifyingHost(0.0f);
    rig.run(4);
    REQUIRE_FALSE(rig.looper.isRecording());
    REQUIRE(rig.looper.getLoopLength() == 10 * kBlock);
    REQUIRE(rig.looper.getPositionSeconds() > 0.03);

    auto& returnToZero = parameter<juce::AudioParameterBool>(rig.looper, "returnToZero");
    returnToZero.setValueNotifyingHost(1.0f);
    returnToZero.setValueNotifyingHost(0.0f);
    rig.run(1); // fades out, jumps to the start, fades back in
    REQUIRE(rig.looper.getPositionSeconds() < (kBlock + 1.0) / kRate);
    REQUIRE(rig.looper.isPlaying());

    auto& play = parameter<juce::AudioParameterBool>(rig.looper, "play");
    play.setValueNotifyingHost(1.0f);
    play.setValueNotifyingHost(0.0f);
    REQUIRE_FALSE(rig.looper.isPlaying());
}

TEST_CASE("Looper: synced, it follows the transport, and the transport's Return to Zero lands it on its start",
          "[engine][processors][looper][transport]")
{
    Rig rig;
    parameter<juce::AudioParameterBool>(rig.looper, "sync") = true;
    rig.transport.play();
    rig.looper.toggleRecord(); // recorded from the transport's zero
    rig.run(20, signal);
    rig.looper.toggleRecord();
    rig.run(30); // Auto-play: it plays with the transport

    const auto length = rig.looper.getLoopLength();
    REQUIRE(length == 20 * kBlock);
    REQUIRE(rig.looper.isPlaying());
    REQUIRE(rig.looper.getPositionSeconds() ==
            Approx(static_cast<double>((50 * kBlock) % length) / kRate).margin(1.0 / kRate));

    rig.transport.returnToZero();
    rig.run(3);
    REQUIRE(rig.looper.isPlaying()); // Pedalboard2 paused it here
    REQUIRE(rig.looper.getPositionSeconds() == Approx(3.0 * kBlock / kRate).margin(1.0 / kRate));

    // Pausing the transport silences it; playing again picks up where the transport is.
    rig.transport.pause();
    rig.run(1);
    const auto silent = rig.run(4);
    REQUIRE(std::all_of(silent.begin(), silent.end(), [](float v) { return v == 0.0f; }));
    rig.transport.play();
    rig.run(2);
    REQUIRE(rig.looper.getPositionSeconds() ==
            Approx(static_cast<double>((5 * kBlock) % length) / kRate).margin(1.0 / kRate));
}

TEST_CASE("Looper: the loop's take, its settings and the loop itself round-trip through the node state",
          "[engine][processors][looper]")
{
    Rig a;
    a.run(2, signal);
    a.looper.toggleRecord();
    const auto recordStart = a.frame;
    a.run(20, signal);
    a.looper.toggleRecord();
    a.run(2);
    const auto length = a.looper.getLoopLength();
    REQUIRE(length == 20 * kBlock);

    // The take holds the loop, at 24 bits.
    REQUIRE(waitFor([&] { return a.looper.getLoopFile() != juce::File(); }));
    const auto take = a.looper.getLoopFile();
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(take));
        REQUIRE(reader != nullptr);
        REQUIRE(reader->lengthInSamples == length);
        REQUIRE(reader->bitsPerSample == 24);
        juce::AudioBuffer<float> audio(2, static_cast<int>(length));
        reader->read(&audio, 0, static_cast<int>(length), 0, true, true);
        int bad = 0;
        for (int j = 0; j < static_cast<int>(length); ++j)
            if (std::abs(audio.getSample(0, j) - signal(recordStart + j)) > 1e-6f)
                ++bad;
        REQUIRE(bad == 0);
    }

    parameter<juce::AudioParameterBool>(a.looper, "autoPlay") = false;
    parameter<juce::AudioParameterBool>(a.looper, "stopAfterBar") = true;
    parameter<juce::AudioParameterInt>(a.looper, "numerator") = 3;
    parameter<juce::AudioParameterChoice>(a.looper, "denominator") = 3; // "8"
    parameter<juce::AudioParameterFloat>(a.looper, "inputLevel") = -6.0f;
    parameter<juce::AudioParameterFloat>(a.looper, "loopLevel") = -3.0f;
    juce::MemoryBlock state;
    a.looper.getStateInformation(state);

    Rig b;
    b.looper.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(waitFor([&] { return b.looper.getLoopLength() == length && !b.looper.isLoading(); }));
    REQUIRE(b.looper.getLoopFile() == take);
    REQUIRE_FALSE(parameter<juce::AudioParameterBool>(b.looper, "autoPlay").get());
    REQUIRE(parameter<juce::AudioParameterBool>(b.looper, "stopAfterBar").get());
    REQUIRE(parameter<juce::AudioParameterInt>(b.looper, "numerator").get() == 3);
    REQUIRE(parameter<juce::AudioParameterChoice>(b.looper, "denominator").getCurrentChoiceName() == "8");
    REQUIRE(parameter<juce::AudioParameterFloat>(b.looper, "inputLevel").get() == Approx(-6.0f).margin(0.01f));
    REQUIRE(parameter<juce::AudioParameterFloat>(b.looper, "loopLevel").get() == Approx(-3.0f).margin(0.01f));

    // The reloaded loop plays what was recorded, crossfading into the same
    // audio from before its start.
    parameter<juce::AudioParameterFloat>(b.looper, "loopLevel") = 0.0f;
    b.run(1); // picks up the loaded loop
    b.looper.togglePlay();
    const auto out = b.run(21);
    constexpr std::int64_t fade = LooperProcessor::kFadeFrames;
    int bad = 0;
    for (std::int64_t j = fade - 1; j < length; ++j)
    {
        float expected = signal(recordStart + j);
        if (j >= length - fade)
        {
            const auto i = j - (length - fade);
            const float w = static_cast<float>(i + 1) / static_cast<float>(fade);
            expected = expected * (1.0f - w) + signal(recordStart - fade + i) * w;
        }
        if (std::abs(at(out, j) - expected) > 1e-5f)
            ++bad;
    }
    REQUIRE(bad == 0);
}
