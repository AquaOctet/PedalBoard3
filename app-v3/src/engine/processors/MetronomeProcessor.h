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

// Metronome (M6 U9, plan KD9 / KD11, AC-TR-3): an accented click on beat 1 and
// a plain one on the other beats, added on top of whatever passes through it.
//   - Synced to the transport, it clicks on the transport's beats, in the
//     patch's time signature, while the transport plays. Each click lands on
//     its exact sample.
//   - Unsynced, Play starts it on beat 1 at the patch tempo, counting in its own
//     time signature: 1 to 16 beats of a 1, 2, 4, 8 or 16 note.
//   - The click is synthesized from the sample rate (Pedalboard2 assumed
//     44.1 kHz, so its pitch was off elsewhere): 880 Hz for the accent, 440 Hz
//     for the rest, 60 ms long. An accent sample and a click sample can replace
//     them. They're decoded on the disk thread and resampled to the device rate.
//   - Two voices, so a click that rings into the next beat, or a change of
//     Sync, never ends in a jump (AC-TR-3).
class MetronomeProcessor final : public ProcessorBase,
                                 private juce::AudioProcessorParameter::Listener,
                                 private juce::TimeSliceClient
{
public:
    static constexpr double kClickSeconds = 0.06;
    static constexpr double kAccentHz = 880.0;
    static constexpr double kClickHz = 440.0;
    static constexpr double kMaxSampleSeconds = 2.0;
    static constexpr float kMinLevelDb = -40.0f;
    static constexpr float kMaxLevelDb = 6.0f;

    explicit MetronomeProcessor(DiskThread& disk);
    ~MetronomeProcessor() override;

    const juce::String getName() const override { return "Metronome"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // The card and the window (message thread).
    void togglePlay() noexcept;
    bool isSynced() const noexcept { return sync_->get(); }

    // A new Metronome's state, starting at `db` (Settings > Metronome,
    // optional-features U2).
    static juce::MemoryBlock stateWithLevel(float db);
    bool isRunning() const noexcept { return running_.load(std::memory_order_relaxed); }
    int getBeatInBar() const noexcept { return beatInBar_.load(std::memory_order_relaxed); } // -1 when stopped
    int getBeatsPerBar() const noexcept { return beatsPerBar_.load(std::memory_order_relaxed); }

    // Samples in place of the synthesized clicks; File() for the synthesized one.
    void setSampleFiles(const juce::File& accent, const juce::File& click);
    juce::File getAccentFile() const;
    juce::File getClickFile() const;
    bool isLoading() const noexcept { return loading_.load(std::memory_order_acquire); }
    juce::String getLoadError() const;

private:
    struct Samples
    {
        juce::AudioBuffer<float> accent; // mono, at the device rate; empty for the synthesized click
        juce::AudioBuffer<float> click;
    };

    struct Voice
    {
        int age{-1}; // samples since it started; -1 when silent
        bool accent{false};
    };

    struct Beat
    {
        int offset{0}; // sample within the block
        bool accent{false};
    };

    void writeState(juce::XmlElement& xml) const override;
    void readState(const juce::XmlElement& xml) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    int useTimeSlice() override; // the disk thread
    void load();

    int findBeats(int numSamples, std::array<Beat, 32>& beats) noexcept; // audio thread
    float voiceSample(Voice& voice, const Samples* samples) noexcept;

    DiskThread& disk_;
    juce::AudioParameterBool* play_ = nullptr;
    juce::AudioParameterBool* sync_ = nullptr;
    juce::AudioParameterInt* numerator_ = nullptr;
    juce::AudioParameterChoice* denominator_ = nullptr;
    juce::AudioParameterFloat* level_ = nullptr;

    // Audio-thread state.
    double sampleRate_{48000.0};
    int clickLength_{2880};
    std::array<Voice, 2> voices_{};
    std::size_t nextVoice_{0};
    bool wasTransportPlaying_{false};
    bool lastSynced_{false};
    double lastPpqEnd_{0.0};
    double lastBeatLength_{1.0};
    std::int64_t nextBeatIndex_{0}; // synced: the next transport beat to click
    std::int64_t ownSamples_{0};    // unsynced: samples since Play
    double ownNextBeat_{0.0};
    std::int64_t ownBeat_{0};
    std::atomic<bool> ownRunning_{false};
    std::atomic<bool> restartOwn_{false};
    std::atomic<bool> running_{false};
    std::atomic<int> beatInBar_{-1};
    std::atomic<int> beatsPerBar_{4};
    std::atomic<double> deviceRate_{0.0};

    // The samples, swapped in by the disk thread; the audio thread only try-locks.
    juce::SpinLock swapLock_;
    std::unique_ptr<Samples> samples_;

    mutable std::mutex fileMutex_;
    juce::File accentFile_;
    juce::File clickFile_;
    bool loadRequested_{false};
    juce::String error_;
    std::atomic<bool> loading_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetronomeProcessor)
};

} // namespace pedalboard3::engine::processors
