#pragma once

#include "AudioGraphRuntime.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

namespace pedalboard3::engine
{

// The boundary between JUCE's hardware-audio layer and our graph runtime.
//
// Owns:
//   - juce::AudioDeviceManager (hardware enumeration + current device)
//   - juce::AudioProcessorPlayer (drives the runtime from the ADM callback)
//   - engine::AudioGraphRuntime (our atomic graph slot)
//
// Call `initialise()` once after construction; `shutdown()` before dtor.
// `loadGraph()` is the production path for patch switching — it forwards
// to `AudioGraphRuntime::swapPreparedGraph`.
//
// Phase 1.C.1 scope: the wiring itself + headless-safe initialisation.
// Phase 1.C.2 will add device-selection persistence via Preferences.
// The full patch-switch state machine (worker-thread build + queued
// coalescing) on top of `loadGraph` is Phase 1.D.
//
// Not RT-safe. Message-thread only. `AudioDeviceManager` itself handles
// locking for its audio callbacks.
class AudioEngine
{
public:
    // Result of `initialise`. Keeps the API explicit about whether hardware
    // could actually be opened (common failure on headless dev servers).
    class Result
    {
    public:
        static Result ok() { return Result({}); }
        static Result fail(juce::String message) { return Result(std::move(message)); }

        bool isOk() const noexcept { return errorMessage_.isEmpty(); }
        const auto& getErrorMessage() const noexcept { return errorMessage_; }

    private:
        explicit Result(juce::String m) : errorMessage_(std::move(m)) {}
        juce::String errorMessage_;
    };

    AudioEngine();
    ~AudioEngine();

    // Attempt to open the system's default audio device (stereo in + stereo
    // out) and start pumping the runtime. Safe to call even when no audio
    // hardware is present — returns a failure Result and leaves the runtime
    // reachable but silent. Idempotent.
    //
    // When `savedState` is non-null, the AudioDeviceManager is initialised
    // with that XML (produced by AudioDeviceManager::createStateXml()), so
    // a previously-chosen device + sample rate + buffer size are restored
    // across launches. Pass nullptr for "use OS defaults" — that's the
    // first-run case and the path the unit tests take.
    Result initialise(const juce::XmlElement* savedState = nullptr);

    // Stop the audio callback and close the device. Idempotent.
    void shutdown();

    // Swap the runtime's live graph. Returns the outgoing graph (may be
    // nullptr) for caller-side deferred destruction. Thin wrapper over
    // AudioGraphRuntime::swapPreparedGraph that exists so higher layers
    // don't hand out a raw runtime reference unnecessarily.
    std::unique_ptr<juce::AudioProcessorGraph> loadGraph(std::unique_ptr<juce::AudioProcessorGraph> next);

    // Accessors for code that needs the low-level JUCE objects. The
    // AudioDeviceSelectorComponent wants the ADM. Tests want the runtime.
    AudioGraphRuntime* getRuntime() noexcept { return runtime_.get(); }
    juce::AudioDeviceManager* getDeviceManager() noexcept { return &deviceManager_; }

private:
    juce::AudioDeviceManager deviceManager_;
    juce::AudioProcessorPlayer player_;
    std::unique_ptr<AudioGraphRuntime> runtime_;

    bool initialised_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

} // namespace pedalboard3::engine
