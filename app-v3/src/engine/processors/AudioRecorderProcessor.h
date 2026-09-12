#pragma once

#include "engine/TakeWriter.h"
#include "ProcessorBase.h"

#include <atomic>
#include <cstdint>

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
//   - The audio thread only hands the samples and each take's start and end
//     to a TakeWriter. The disk thread opens, writes and closes the files. A
//     finished take is closed for good and never reopened (AC-FP-3); an empty
//     one is deleted.
//   - About 8 s of slack at 48 kHz: if the disk falls further behind than
//     that, the dropped audio is reported.
class AudioRecorderProcessor final : public ProcessorBase,
                                     private juce::AudioProcessorParameter::Listener,
                                     private juce::TimeSliceClient
{
public:
    static constexpr int kBitsPerSample = TakeWriter::kBitsPerSample;

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
    int getTakeCount() const noexcept { return takes_.getTakeCount(); }
    juce::File getLastTake() const { return takes_.getLastTake(); }
    juce::String getError() const { return takes_.getError(); }
    bool hasDroppedAudio() const noexcept { return takes_.hasDroppedAudio(); }

    // Where takes go; File() means the default folder. Saved with the node.
    void setFolder(const juce::File& folder) { takes_.setFolder(folder); }
    juce::File getFolder() const { return takes_.getFolder(); } // the folder in use

    // The patch name takes are named after. The card keeps it current.
    void setTakePrefix(const juce::String& prefix) { takes_.setPrefix(prefix); }

private:
    void writeState(juce::XmlElement& xml) const override;
    void readState(const juce::XmlElement& xml) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    int useTimeSlice() override; // the disk thread

    DiskThread& disk_;
    TakeWriter takes_; // writes out and closes a take still open when it goes
    juce::AudioParameterBool* record_ = nullptr;
    juce::AudioParameterBool* sync_ = nullptr;

    // Audio-thread state (atomics the UI reads).
    std::atomic<bool> wantRecording_{false};
    std::atomic<bool> capturing_{false};
    bool lastTransportPlaying_{false};
    std::atomic<std::int64_t> takeFrames_{0};
    std::atomic<float> peak_{0.0f};
    std::atomic<double> sampleRate_{48000.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorderProcessor)
};

} // namespace pedalboard3::engine::processors
