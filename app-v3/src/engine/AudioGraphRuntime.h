#pragma once

#include "InputLevel.h"
#include "MasterMute.h"
#include "processors/OutputSanitiser.h"
#include "TestTone.h"
#include "Transport.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace pedalboard3::engine
{

// The thing JUCE's AudioDeviceManager actually drives. Internally holds a
// single-slot, atomic pointer to a `juce::AudioProcessorGraph` — the current
// patch's graph. `swapPreparedGraph` publishes a fully prepared replacement
// graph into a `pending_` slot; the audio thread observes that slot at the
// top of its next processBlock, runs an all-notes-off flush on the outgoing
// graph (16ch × {allSoundOff, allNotesOff, resetAllControllers}), then
// commits the swap by storing the new graph into `live_`. The message
// thread waits for a generation-counter bump confirming the audio thread
// has done all that, then takes ownership of the outgoing graph for
// deferred deletion.
//
// Every processBlock ends with the master stage, in this order and for
// these reasons:
//   OutputSanitiser  — no NaN/Inf/|x|>1.5 ever reaches the audio device,
//                      whatever the graph is doing.
//   TestTone (#111)  — after the sanitiser, so a chain producing NaN cannot
//                      poison the one signal being used to prove the path is
//                      clean.
//   MasterMute (#106)— last. Muting is a multiply by zero and `NaN * 0.0f`
//                      is NaN, so sanitising second would let a muted NaN
//                      out; and the tone has to be mutable, because the
//                      status bar promises that muted means silent.
//
// The input peak is taken at the very top, before the graph runs, so the
// input meter reads the cable rather than the board (#117).
//
// The runtime also owns the transport (M6 U1): it is the live graph's play
// head, which JUCE's graph hands to every node each block, and it advances
// once per block.
//
// Phase 1.D.2 added:
//   - `pending_` atomic so the swap and the all-notes-off flush both
//     happen on the audio thread at a buffer boundary (master plan §4.1.1
//     invariant 3).
//   - Pre-built flush MidiBuffer + 4× block silence scratch in
//     `prepareToPlay` to keep `processBlock` allocation-free.
//   - 2-second hard ceiling on the ack-wait; on timeout, fall back to a
//     synchronous message-thread swap with no flush (graceful degradation
//     for a stalled or stopped audio device).
//
// RT contract: processBlock does not allocate or lock. Both `live_` and
// `pending_` are atomic. Memory ordering:
//   - msg pending_.store(release) ↔ audio pending_.exchange(acquire)
//   - audio live_.store(release) — only audio writes live_
//   - audio generationAckedByAudio_.fetch_add(release) ↔ msg load(acquire)
class AudioGraphRuntime : public juce::AudioProcessor
{
public:
    AudioGraphRuntime();
    ~AudioGraphRuntime() override;

    // AudioProcessor ---------------------------------------------------------
    const juce::String getName() const override { return "Pedalboard 3 Graph Runtime"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // Runtime graph management -----------------------------------------------

    // Swap the current live graph for a fully prepared one.
    //
    // Preconditions (caller's responsibility):
    //   - Caller is on the message thread.
    //   - `next` has been `prepareToPlay`'d at the same sample rate + block
    //     size the runtime is currently running at.
    //
    // Behaviour:
    //   - If the runtime has not been prepared yet (no audio device), does
    //     a synchronous message-thread swap with no flush.
    //   - Otherwise publishes `next` into `pending_`. The audio thread
    //     observes it on its next block, flushes all-notes-off on the
    //     outgoing graph using a pre-built MidiBuffer + silence scratch,
    //     then commits the swap by storing into `live_`.
    //   - Blocks the caller for up to 2 s waiting for an ack; on ack-timeout
    //     falls back to a synchronous message-thread swap (best-effort).
    //   - Calls `releaseResources()` on the outgoing graph and returns it
    //     wrapped in unique_ptr so the caller can destruct it on the
    //     message thread (plugin dtors often require it).
    //
    // Returns nullptr if no graph was previously live. `next` may be null
    // (clears the runtime).
    std::unique_ptr<juce::AudioProcessorGraph> swapPreparedGraph(std::unique_ptr<juce::AudioProcessorGraph> next);

    // Snapshot of the currently-active graph pointer. Null iff the runtime
    // has no patch loaded. Provided for test + diagnostic inspection;
    // production code should operate on the Session view.
    juce::AudioProcessorGraph* getLiveGraph() const noexcept { return live_.load(std::memory_order_acquire); }

    // Monotonic counter of processBlock completions since construction.
    // Used by swapPreparedGraph to wait for the audio thread to observe
    // the new pointer; also useful in tests.
    std::uint64_t getGenerationAckedByAudio() const noexcept
    {
        return generationAckedByAudio_.load(std::memory_order_acquire);
    }

    // The transport every hosted plugin sees as its play head.
    Transport& getTransport() noexcept { return transport_; }

    // The master mute (#106). It lives here rather than in the graph so it
    // works on an empty board and survives a patch change untouched.
    MasterMute& getMasterMute() noexcept { return masterMute_; }

    // The test tone (#111). Here for the same reasons as the mute, plus the
    // one that matters most: as a master stage it does not touch the board,
    // so switching it on no longer empties the canvas and switching it off
    // no longer re-instantiates every plugin.
    TestTone& getTestTone() noexcept { return testTone_; }

    // What is arriving at the audio input, before the board sees it (#117).
    InputLevel& getInputLevel() noexcept { return inputLevel_; }

private:
    // Audio-thread helper: runs 16ch × {allSoundOff, allNotesOff,
    // resetAllControllers} into a silenced scratch buffer on the outgoing
    // graph so synths don't hang notes through the swap. Allocation-free
    // — buffers are sized in prepareToPlay.
    void flushMidiReceivers(juce::AudioProcessorGraph* outgoing) noexcept;

    std::atomic<juce::AudioProcessorGraph*> live_{nullptr};
    std::atomic<juce::AudioProcessorGraph*> pending_{nullptr};
    std::atomic<std::uint64_t> generationAckedByAudio_{0};
    processors::OutputSanitiser sanitiser_;
    TestTone testTone_;
    MasterMute masterMute_;
    InputLevel inputLevel_;
    Transport transport_;

    // Pre-built MIDI flush template + per-call scratch (plugins that
    // producesMidi() write through). Both sized in prepareToPlay.
    juce::MidiBuffer flushMidiTemplate_;
    juce::MidiBuffer flushMidiScratch_;

    // Pre-allocated silenced audio buffer for the flush block. Sized to
    // 4× samplesPerBlock × max(numOutputChannels, 2) to absorb adversarial
    // host block-size bursts without a real-time realloc.
    juce::AudioBuffer<float> silenceScratch_;

    // Cached prepareToPlay params. Used by the synchronous-fallback path
    // to detect "no audio device" (currentBlockSize_ <= 0) and by the
    // ack-wait timeout calculation.
    double currentSampleRate_{0.0};
    int currentBlockSize_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioGraphRuntime)
};

} // namespace pedalboard3::engine
