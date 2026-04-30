// Tests for pedalboard3::engine::AudioEngine -- the orchestrator that owns
// the AudioDeviceManager, AudioProcessorPlayer, and AudioGraphRuntime and
// wires the three together.
//
// These tests run on headless Linux CI (no audio device available), so
// they must exercise the orchestrator without asserting that actual audio
// hardware opened. The "no device available" path is explicitly covered.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

using namespace pedalboard3;

TEST_CASE("AudioEngine: constructs without crashing even if no audio device is available", "[engine][audioengine]")
{
    engine::AudioEngine eng;
    REQUIRE(eng.getRuntime() != nullptr);
    REQUIRE(eng.getDeviceManager() != nullptr);
}

TEST_CASE("AudioEngine: initialise with default devices is safe on headless systems", "[engine][audioengine]")
{
    engine::AudioEngine eng;
    const auto result = eng.initialise();
    // On headless Linux CI there will often be no default audio device.
    // The orchestrator must cope: either success or a descriptive error.
    // Failure must NOT throw or leave the engine in an unusable state.
    if (result.isOk())
    {
        // If a device did open, runtime is still accessible.
        REQUIRE(eng.getRuntime() != nullptr);
    }
    else
    {
        INFO("audio init result: " << result.getErrorMessage().toStdString());
        REQUIRE(eng.getRuntime() != nullptr);
    }
}

TEST_CASE("AudioEngine: loadGraph(nullptr) clears the runtime", "[engine][audioengine]")
{
    engine::AudioEngine eng;
    eng.initialise();

    auto graph = std::make_unique<juce::AudioProcessorGraph>();
    graph->setPlayConfigDetails(2, 2, 48000.0, 256);
    graph->prepareToPlay(48000.0, 256);

    auto prev = eng.loadGraph(std::move(graph));
    REQUIRE(prev == nullptr);
    REQUIRE(eng.getRuntime()->getLiveGraph() != nullptr);

    auto cleared = eng.loadGraph(nullptr);
    REQUIRE(cleared != nullptr);
    REQUIRE(eng.getRuntime()->getLiveGraph() == nullptr);
}

TEST_CASE("AudioEngine: shutdown is idempotent and destructor is safe after it", "[engine][audioengine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    eng.shutdown();
    eng.shutdown(); // must not crash
    // Destructor runs at end of scope -- should not double-shutdown.
}
