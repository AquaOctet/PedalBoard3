#pragma once

#include "ProcessorBase.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace pedalboard3::engine
{
class DiskThread;
}

namespace pedalboard3::engine::processors
{

// File Player (M6 U6): plays an audio file into the chain, on top of whatever
// passes through it.
//   - The disk thread decodes the whole file into memory, resampled to the
//     device rate, when it's chosen (or the device rate changes). The audio
//     thread plays from memory: no disk, no allocation. It only ever try-locks
//     to pick up a newly loaded file, so it never waits (KD8).
//   - Play / Pause, Return to Zero, Loop and Sync to Transport. Synced, it
//     plays from the transport's own position, so play/stop cycles can't drift
//     (KD9, AC-TR-1). Unsynced, it keeps its own position.
//   - Every control is a parameter a footswitch can map. Play, Return to Zero
//     and Trigger act on a press (the value going from off to on). Trigger
//     restarts from the top and plays (Pedalboard2's hidden parameter, AC-FP-2).
//   - At the end of the file it stops and rewinds, or wraps when Loop is on.
//     Unlike Pedalboard2's, it never stops the transport.
//   - Files up to 30 minutes long; a mono file plays on both channels.
class FilePlayerProcessor final : public ProcessorBase,
                                  private juce::AudioProcessorParameter::Listener,
                                  private juce::TimeSliceClient
{
public:
    static constexpr double kMaxSeconds = 30.0 * 60.0;
    static constexpr int kOverviewPoints = 256;

    explicit FilePlayerProcessor(DiskThread& disk);
    ~FilePlayerProcessor() override;

    const juce::String getName() const override { return "File Player"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // Any thread but the audio thread: choose the file. It's decoded on the
    // disk thread; File() clears it.
    void setFile(const juce::File& file);
    juce::File getFile() const;

    // The card and the window (message thread).
    void togglePlay() noexcept;
    void returnToZero() noexcept;
    void seekTo(double fraction) noexcept; // 0..1 of the file
    bool isPlaying() const noexcept { return audible_.load(std::memory_order_relaxed); }
    bool isLoading() const noexcept { return loading_.load(std::memory_order_acquire); }
    double getLengthSeconds() const noexcept;
    double getPositionSeconds() const noexcept;
    juce::String getLoadError() const;
    std::vector<float> getOverview() const; // a peak per slice, 0..1; empty with no file
    int getOverviewGeneration() const noexcept { return overviewGeneration_.load(std::memory_order_acquire); }

    // A File Player's state holding `file`, for a node added by dropping one.
    static juce::MemoryBlock stateForFile(const juce::File& file);

private:
    struct Loaded
    {
        juce::AudioBuffer<float> audio; // one or two channels, at `sampleRate`
        double sampleRate{0.0};
    };

    void writeState(juce::XmlElement& xml) const override;
    void readState(const juce::XmlElement& xml) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    int useTimeSlice() override; // the disk thread
    void load(const juce::File& file, double deviceRate);

    DiskThread& disk_;
    juce::AudioParameterBool* play_ = nullptr;
    juce::AudioParameterBool* returnToZero_ = nullptr;
    juce::AudioParameterBool* trigger_ = nullptr;
    juce::AudioParameterBool* looping_ = nullptr;
    juce::AudioParameterBool* sync_ = nullptr;

    // The file and what its load produced. Never touched by the audio thread.
    mutable std::mutex fileMutex_;
    juce::File file_;
    bool loadRequested_{false};
    juce::String error_;
    std::vector<float> overview_;

    // The decoded file, swapped in by the disk thread; the audio thread only
    // try-locks this.
    juce::SpinLock swapLock_;
    std::unique_ptr<Loaded> loaded_;

    std::atomic<std::int64_t> position_{0};      // unsynced play position, in loaded samples
    std::atomic<std::int64_t> shownPosition_{0}; // where the audio thread last played, for the UI
    std::atomic<std::int64_t> length_{0};
    std::atomic<double> loadedRate_{0.0};
    std::atomic<double> deviceRate_{0.0};
    std::atomic<std::int64_t> seekRequest_{-1}; // a seek or Return to Zero for the next block
    std::atomic<bool> playing_{false};          // unsynced play state
    std::atomic<bool> audible_{false};          // playing as of the last block, synced or not
    std::atomic<bool> loading_{false};
    std::atomic<int> overviewGeneration_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilePlayerProcessor)
};

} // namespace pedalboard3::engine::processors
