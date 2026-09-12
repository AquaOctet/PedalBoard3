#pragma once

#include "engine/TakeWriter.h"
#include "LoopBuffer.h"
#include "ProcessorBase.h"

#include <array>
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

// Looper (M6 U8, plan KD6-KD9 / KD11): records a loop from what passes through
// it, then plays it back on top of the input, seamlessly.
//   - Record starts a loop and, pressed again, ends it. Auto-play (on by
//     default) plays it straight away. Stop After Bar ends it by itself after
//     one bar of the Looper's own length (1-16 beats of a 1, 2, 4, 8 or 16
//     note) at the transport's tempo, on the exact sample.
//   - The loop point crossfades over 128 samples into the audio that came just
//     before the loop started, so each pass runs into the next without a step.
//     Starting, pausing and jumping fade over 128 samples too.
//   - Sync to Transport: the loop plays while the transport plays, at the
//     transport's position counted from where the loop was recorded, so
//     play/stop can't drift it and the transport's Return to Zero lands it on
//     its start without pausing it (Pedalboard2 paused it). Play arms or mutes
//     it.
//   - Memory comes in chunks the disk thread allocates ahead of the recording,
//     up to 10 minutes; running out ends the loop there, cleanly.
//   - Each loop is also written as a take (TakeWriter), and the patch reloads
//     it from there. A loop file can be loaded too. It's decoded on the disk
//     thread and swapped in with a try-lock, so nothing ever waits (Pedalboard2
//     could hang changing the file while playing).
class LooperProcessor final : public ProcessorBase,
                              private juce::AudioProcessorParameter::Listener,
                              private juce::TimeSliceClient
{
public:
    static constexpr int kFadeFrames = 128;
    static constexpr double kDefaultMaxSeconds = 10.0 * 60.0;
    static constexpr int kOverviewPoints = 256;

    explicit LooperProcessor(DiskThread& disk, double maxLoopSeconds = kDefaultMaxSeconds);
    ~LooperProcessor() override;

    const juce::String getName() const override { return "Looper"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // The card and the window (message thread).
    void togglePlay() noexcept; // while recording: end the loop and play it
    void returnToZero() noexcept;
    void toggleRecord() noexcept;
    void seekTo(double fraction) noexcept;                                               // 0..1 of the loop
    bool isPlaying() const noexcept { return playing_.load(std::memory_order_relaxed); } // armed, when synced
    bool isRecording() const noexcept { return recording_.load(std::memory_order_relaxed); }
    bool isSynced() const noexcept { return sync_->get(); }
    bool ranOutOfMemory() const noexcept { return ranOut_.load(std::memory_order_relaxed); }
    std::int64_t getLoopLength() const noexcept { return length_.load(std::memory_order_acquire); } // frames
    double getLengthSeconds() const noexcept;
    double getPositionSeconds() const noexcept;
    double getRecordedSeconds() const noexcept;
    std::vector<float> getOverview() const; // a peak per slice, 0..1; empty with no loop
    int getOverviewGeneration() const noexcept { return overviewGeneration_.load(std::memory_order_acquire); }
    bool isLoading() const noexcept { return loading_.load(std::memory_order_acquire); }
    juce::String getError() const;

    // The loop's file: the take it was written to, or a file loaded into it.
    void loadLoop(const juce::File& file);
    juce::File getLoopFile() const;

    // Where takes go (File() for the default folder), and the patch name
    // they're named after. The card keeps the name current.
    void setFolder(const juce::File& folder) { takes_.setFolder(folder); }
    juce::File getFolder() const { return takes_.getFolder(); }
    void setTakePrefix(const juce::String& prefix) { takes_.setPrefix(prefix); }

    // A new Looper's state, saving loops in `folder` (Settings > Looper,
    // optional-features U2).
    static juce::MemoryBlock stateForFolder(const juce::File& folder);

private:
    using PreRoll = std::array<std::array<float, kFadeFrames>, LoopBuffer::kChannels>;

    // The loop the audio thread plays and records into. A load swaps in a new
    // one whole; a recording writes into the one it has.
    struct Loop
    {
        explicit Loop(std::int64_t maxFrames) : memory(maxFrames) {}
        LoopBuffer memory;
        std::uint64_t generation{0};
        std::int64_t length{0};
        std::int64_t origin{0}; // the transport sample it was recorded from
        PreRoll preRoll{};      // the kFadeFrames of input just before it
    };

    void writeState(juce::XmlElement& xml) const override;
    void readState(const juce::XmlElement& xml) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    int useTimeSlice() override; // the disk thread

    void requestLoad(const juce::File& file, const PreRoll& preRoll, std::int64_t origin);
    void load(const juce::File& file, const PreRoll& preRoll, std::int64_t origin); // the disk thread
    void publishOverview(std::vector<float> overview);
    PreRoll currentPreRoll() const noexcept;

    // The audio thread.
    void adopt(const Loop& loop) noexcept;
    void startRecording(std::int64_t transportSample, double bpm) noexcept;
    void stopRecording() noexcept;
    void jumpTo(std::int64_t frame, bool synced, std::int64_t transportSample) noexcept;
    int fadeOutInto(PreRoll& out, const LoopBuffer& memory, int frames) noexcept;
    void render(juce::AudioBuffer<float>& buffer,
                const LoopBuffer& memory,
                int from,
                bool wantAudible,
                std::int64_t follow,
                float loopGainStart,
                float loopGainEnd) noexcept;
    float loopSample(const LoopBuffer& memory, int channel, std::int64_t position) const noexcept;

    DiskThread& disk_;
    const double maxSeconds_;
    const std::int64_t maxFrames_; // maxSeconds_ at up to 192 kHz
    TakeWriter takes_;             // writes out and closes a loop still recording when it goes
    juce::AudioParameterBool* play_ = nullptr;
    juce::AudioParameterBool* returnToZero_ = nullptr;
    juce::AudioParameterBool* record_ = nullptr;
    juce::AudioParameterBool* sync_ = nullptr;
    juce::AudioParameterBool* stopAfterBar_ = nullptr;
    juce::AudioParameterBool* autoPlay_ = nullptr;
    juce::AudioParameterInt* numerator_ = nullptr;
    juce::AudioParameterChoice* denominator_ = nullptr;
    juce::AudioParameterFloat* inputLevel_ = nullptr;
    juce::AudioParameterFloat* loopLevel_ = nullptr;

    // The loop, swapped by the disk thread; the audio thread only try-locks.
    juce::SpinLock swapLock_;
    std::unique_ptr<Loop> loop_;
    std::uint64_t nextGeneration_{1}; // the disk thread (and the constructor)

    // Requests for the next block.
    std::atomic<std::uint32_t> requests_{0};
    std::atomic<std::int64_t> seekRequest_{-1};

    // Audio-thread state.
    double sampleRate_{48000.0};
    std::uint64_t seenGeneration_{0};
    bool recordingNow_{false};
    std::int64_t written_{0};
    std::int64_t barFrames_{0}; // Stop After Bar's length for this recording; 0 for none
    std::int64_t lengthNow_{0};
    std::int64_t pos_{0};
    std::int64_t origin_{0};
    std::int64_t pendingJump_{-1};
    bool jumpFollows_{false}; // a synced jump lands where the transport has got to
    float gain_{0.0f};        // the play fade, 0..1
    float lastInputGain_{1.0f};
    float lastLoopGain_{1.0f};
    bool firstBlock_{true};
    PreRoll ring_{}; // the last kFadeFrames of input, always
    std::size_t ringPos_{0};
    PreRoll preRoll_{};

    // Shared with the UI and the disk thread.
    std::atomic<bool> playing_{false};
    std::atomic<bool> recording_{false};
    std::atomic<bool> ranOut_{false};
    std::atomic<std::int64_t> length_{0};
    std::atomic<std::int64_t> shownPos_{0};
    std::atomic<std::int64_t> writePos_{0};
    std::atomic<std::int64_t> originShared_{0};
    std::atomic<std::int64_t> limitFrames_{0};
    std::atomic<double> deviceRate_{0.0};
    std::atomic<double> loopRate_{0.0}; // the rate the loop in memory is at
    std::array<std::atomic<float>, 2 * kFadeFrames> preRollShared_{};
    std::atomic<bool> fileValid_{false}; // loopFile_ is the loop in memory
    std::atomic<bool> loading_{false};
    std::atomic<int> overviewGeneration_{0};

    // Files, never touched by the audio thread.
    mutable std::mutex fileMutex_;
    juce::File loopFile_;
    bool loadRequested_{false};
    juce::File pendingFile_;
    PreRoll pendingPreRoll_{};
    std::int64_t pendingOrigin_{0};
    juce::String error_;
    std::vector<float> overview_;
    int takesSeen_{0}; // the disk thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LooperProcessor)
};

} // namespace pedalboard3::engine::processors
