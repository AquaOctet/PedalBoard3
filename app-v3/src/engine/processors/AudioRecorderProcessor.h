#pragma once

#include "ProcessorBase.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace pedalboard3::engine
{
class DiskThread;
}

namespace pedalboard3::engine::processors
{

// Audio Recorder (M6 U7, plan KD6 / KD8 / KD9): records what passes through it
// (it passes its input on unchanged) into auto-named takes: a new 24-bit WAV
// per take in Documents/Pedalboard 3/Recordings, or a folder chosen in its
// window, named after the patch and the time. Nothing is ever overwritten.
//
//   - Record toggles a take; as a parameter a footswitch can map, a press
//     toggles it.
//   - Synced to the transport, every transport play starts a take and stop
//     ends it, decided on the audio thread, so a play from a MIDI mapping
//     always starts one (AC-TR-4).
//   - The audio thread only copies samples into a lock-free buffer and stamps
//     each take's start and end at an exact frame. The disk thread opens,
//     writes and closes the files. A finished take is closed for good and never
//     reopened (AC-FP-3); an empty one is deleted.
//   - About 8 s of slack at 48 kHz: if the disk falls further behind than
//     that, the dropped audio is reported.
class AudioRecorderProcessor final : public ProcessorBase,
                                     private juce::AudioProcessorParameter::Listener,
                                     private juce::TimeSliceClient
{
public:
    static constexpr int kBitsPerSample = 24;

    explicit AudioRecorderProcessor(DiskThread& disk);
    ~AudioRecorderProcessor() override;

    const juce::String getName() const override { return "Audio Recorder"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // The card and the window (message thread).
    void toggleRecord() noexcept;
    bool isRecording() const noexcept;
    double getTakeSeconds() const noexcept;
    float takeInputPeak() noexcept; // the loudest input since the last call, 0..1
    int getTakeCount() const noexcept { return takeCount_.load(std::memory_order_acquire); }
    juce::File getLastTake() const;
    juce::String getError() const;
    bool hasDroppedAudio() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    // Where takes go; File() means the default folder. Saved with the node.
    void setFolder(const juce::File& folder);
    juce::File getFolder() const; // the folder in use (the default when none is set)

    // The patch name takes are named after. The card keeps it current.
    void setTakePrefix(const juce::String& prefix);

private:
    struct Event
    {
        std::int64_t frame{0}; // in the stream of recorded frames
        bool start{false};
    };

    void writeState(juce::XmlElement& xml) const override;
    void readState(const juce::XmlElement& xml) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    int useTimeSlice() override; // the disk thread

    void pushEvent(bool start) noexcept; // audio thread
    void pump();                         // the disk thread (or the destructor, once it's detached)
    void drain(std::int64_t frames);
    void openTake();
    void closeTake();

    DiskThread& disk_;
    juce::AudioParameterBool* record_ = nullptr;
    juce::AudioParameterBool* sync_ = nullptr;

    // Audio thread -> disk thread: the recorded frames and the take boundaries.
    juce::AbstractFifo audioFifo_;
    juce::AudioBuffer<float> ring_;
    juce::AbstractFifo eventFifo_;
    std::array<Event, 64> events_{};
    std::atomic<std::int64_t> framesPushed_{0}; // written by the audio thread only
    std::int64_t framesPulled_{0};              // the disk thread only

    // Audio-thread state (atomics the UI reads).
    std::atomic<bool> wantRecording_{false};
    std::atomic<bool> capturing_{false};
    bool lastTransportPlaying_{false};
    std::atomic<std::int64_t> takeFrames_{0};
    std::atomic<float> peak_{0.0f};
    std::atomic<bool> dropped_{false};
    std::atomic<double> sampleRate_{48000.0};

    // The disk thread's take.
    std::unique_ptr<juce::AudioFormatWriter> writer_;
    juce::File currentTake_;
    std::int64_t takeWritten_{0};
    std::atomic<int> takeCount_{0};

    // Settings and results, never touched by the audio thread.
    mutable std::mutex mutex_;
    juce::File folder_;
    juce::String prefix_;
    juce::File lastTake_;
    juce::String error_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorderProcessor)
};

} // namespace pedalboard3::engine::processors
