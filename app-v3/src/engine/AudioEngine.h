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
// Initialisation is headless-safe. The chosen device is saved in Preferences
// (by PreferencesPersister), and session::Session swaps each built patch in
// through `loadGraph`.
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

    // Accessors for code that needs the low-level JUCE objects: the audio
    // settings and MIDI input want the ADM, tests want the runtime.
    AudioGraphRuntime* getRuntime() noexcept { return runtime_.get(); }
    juce::AudioDeviceManager* getDeviceManager() noexcept { return &deviceManager_; }

    // The transport (M6 U1): play, stop, tempo, time signature; every hosted
    // plugin's play head.
    Transport& getTransport() noexcept { return runtime_->getTransport(); }

    // The master mute (#106). Silences the output without touching the board:
    // it is a stage in the runtime, not a node, so it works on an empty board,
    // survives a patch change, and can never mark a board unsaved. Listeners
    // are told on the message thread so the top bar and status bar can show
    // it; a muted rig that looks live is worse than no mute at all.
    void setMasterMuted(bool shouldMute);
    bool isMasterMuted() const noexcept;
    void toggleMasterMute() { setMasterMuted(!isMasterMuted()); }
    juce::ChangeBroadcaster& masterMuteChanged() noexcept { return masterMuteBroadcaster_; }

    // The test tone (#111). A master stage, not a graph: it crossfades over
    // the board's output and leaves the board itself completely alone, so
    // switching it on no longer clears the patch and switching it off no
    // longer rebuilds every plugin from its saved state. Listeners are told
    // on the message thread so the top bar and status bar can show it.
    void setTestToneEnabled(bool shouldPlay);
    bool isTestToneEnabled() const noexcept;
    void toggleTestTone() { setTestToneEnabled(!isTestToneEnabled()); }
    juce::ChangeBroadcaster& testToneChanged() noexcept { return testToneBroadcaster_; }

    // Peak in and out since the last call, 0..1, and resets (#117). Driven
    // by the IN / OUT meters' timer.
    float readInputPeak() noexcept { return runtime_->getInputLevel().readAndClear(); }
    float readOutputPeak() noexcept { return runtime_->getOutputLevel().readAndClear(); }

    // The error message from the most recent initialise() call (empty when
    // the device opened cleanly). Retained so the UI can tell the user WHY
    // there's no sound — otherwise a failed device open is silent except
    // for a log line. Matches the last initialise() Result's message.
    const juce::String& getLastInitError() const noexcept { return lastInitError_; }

private:
    juce::ChangeBroadcaster masterMuteBroadcaster_;
    juce::ChangeBroadcaster testToneBroadcaster_;
    juce::AudioDeviceManager deviceManager_;
    juce::AudioProcessorPlayer player_;
    std::unique_ptr<AudioGraphRuntime> runtime_;

    bool initialised_{false};
    juce::String lastInitError_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

} // namespace pedalboard3::engine
