#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace pedalboard3::engine
{

// Writes takes (M6 U7 / U8, plan KD6 / KD8) for the Audio Recorder and the
// Looper. Each take is a new 24-bit stereo WAV, named after the patch and the
// time, in Documents/Pedalboard 3/Recordings or a chosen folder. Nothing is
// ever overwritten, and an empty take is deleted.
//   - The audio thread only copies frames into a lock-free ring (push) and
//     stamps where each take starts and ends at an exact frame (startTake,
//     endTake). No allocation, no locks, no disk.
//   - pump(), on the disk thread, opens, writes and closes the files. A
//     finished take is closed for good and never reopened (AC-FP-3).
//   - About 8 s of slack at 48 kHz: if the disk falls further behind than
//     that, the audio that didn't fit is dropped and reported.
//   - The destructor writes out what's left and closes an open take, so its
//     owner stops pumping it from the disk thread first.
class TakeWriter
{
public:
    static constexpr int kBitsPerSample = 24;
    static constexpr int kRingFrames = 4 * 96000; // 4 s at 96 kHz, 8 s at 48 kHz

    TakeWriter();
    ~TakeWriter();

    // Any thread: the rate new takes are written at.
    void setSampleRate(double rate) noexcept;

    // The audio thread.
    void startTake() noexcept;
    void endTake() noexcept;
    // Copies numSamples frames from `start` (a mono buffer fills both sides);
    // false when there was no room and they were dropped.
    bool push(const juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept;

    // The disk thread.
    void pump();
    bool isTakeOpen() const noexcept { return writer_ != nullptr; }

    // Settings and results (any thread but the audio thread).
    void setFolder(const juce::File& folder); // File() for the default folder
    juce::File getFolder() const;             // the folder in use
    juce::File getChosenFolder() const;       // File() when it's the default
    void setPrefix(const juce::String& prefix);
    juce::File getLastTake() const;
    juce::String getError() const;
    int getTakeCount() const noexcept { return takeCount_.load(std::memory_order_acquire); }
    bool hasDroppedAudio() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    static constexpr int kEventSlots = 64;

    struct Event
    {
        std::int64_t frame{0}; // in the stream of pushed frames
        bool start{false};
    };

    void pushEvent(bool start) noexcept;
    void drain(std::int64_t frames);
    void openTake();
    void closeTake();

    // Audio thread -> disk thread: the frames and the take boundaries.
    juce::AbstractFifo audioFifo_;
    juce::AudioBuffer<float> ring_;
    juce::AbstractFifo eventFifo_;
    std::array<Event, kEventSlots> events_{};
    std::atomic<std::int64_t> framesPushed_{0}; // written by the audio thread only
    std::int64_t framesPulled_{0};              // the disk thread only
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TakeWriter)
};

} // namespace pedalboard3::engine
