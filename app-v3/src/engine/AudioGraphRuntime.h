#pragma once

#include "processors/OutputSanitiser.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace pedalboard3::engine
{

// The thing JUCE's AudioDeviceManager actually drives. Internally holds a
// single-slot, atomic pointer to a `juce::AudioProcessorGraph` — the current
// patch's graph. `swapPreparedGraph` exchanges the pointer for one that has
// already had `prepareToPlay` called on a worker thread, and hands the
// outgoing graph back to the caller for deferred deletion on the message
// thread. Every processBlock ends with a final `OutputSanitiser` pass so no
// NaN/Inf/|x|>1.5 ever reaches the audio device regardless of graph state.
//
// Phase 1.A: raw atomic-swap primitive + null-safety. The full patch-switch
// state machine (Idle → Preparing → Ready_To_Swap → Cleanup with
// all-notes-off flush, queued coalescing, and depth-1 latest-wins) is built
// on top of this primitive in Phase 1.D. See plan §4.1.1 and Appendix A.1.
//
// RT contract: processBlock does not allocate or lock. `live.load` is the
// only cross-thread read. The generation counter lets the swap side know
// when a block has actually been observed using the new pointer.
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
    //     size the runtime is currently running at (match
    //     getSampleRate() / getBlockSize()).
    //   - `next` is not null.
    //
    // Behaviour:
    //   - Atomically exchanges the live pointer. The audio thread will pick
    //     up `next` on its next block.
    //   - Blocks the caller briefly (~1 buffer's worth) until the audio
    //     thread has been observed to advance past the swap, using a
    //     generation counter.
    //   - Calls `releaseResources()` on the outgoing graph.
    //   - Returns the outgoing graph wrapped in unique_ptr so the caller can
    //     destruct it on the message thread (plugin dtors often require it).
    //
    // Returns nullptr if no graph was previously live.
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

private:
    std::atomic<juce::AudioProcessorGraph*> live_{nullptr};
    std::atomic<std::uint64_t> generationAckedByAudio_{0};
    processors::OutputSanitiser sanitiser_;

    // Cached prepareToPlay params so the AudioGraphRuntime can forward them
    // to a newly swapped-in graph if we ever allow swapping an un-prepared
    // one. Phase 1.A forbids that; kept here for Phase 1.D use.
    double currentSampleRate_{0.0};
    int currentBlockSize_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioGraphRuntime)
};

} // namespace pedalboard3::engine
