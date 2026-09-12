#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>

namespace pedalboard3::engine
{

// Wraps a hosted AudioProcessor with host-managed bypass, MIDI channel
// filtering, and per-node CPU metering.
//
// Phase 1.A scope: the ramped bypass + MIDI filter + metering primitives.
// Phase 1.B adds SEH (Windows) / signal (POSIX) guards around processBlock
// and a 3-strikes block-budget watchdog, per plan Appendix A.2.
//
// RT contract: processBlock does not allocate, lock, or perform I/O. The
// per-instance ramp state and CPU counters live in value types owned by
// this object; dry-buffer scratch is reserved in prepareToPlay.
//
// Bypass ramp: ~20 ms linear fade between wet and dry, sample-rate aware.
// At 48 kHz / 256-sample buffers that's a little over 4 blocks.
class BypassableInstance : public juce::AudioProcessor
{
public:
    struct CpuStats
    {
        std::uint64_t elapsedNanos{0};
        std::uint64_t blockCount{0};
    };

    explicit BypassableInstance(std::unique_ptr<juce::AudioProcessor> inner);
    ~BypassableInstance() override = default;

    // AudioProcessor ---------------------------------------------------------
    const juce::String getName() const override { return inner_->getName(); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { inner_->releaseResources(); }

    // The graph hands each node its play head every block. Pass it on, or the
    // hosted plugin never sees the transport (M6 U1).
    void setPlayHead(juce::AudioPlayHead* newPlayHead) override
    {
        juce::AudioProcessor::setPlayHead(newPlayHead);
        inner_->setPlayHead(newPlayHead);
    }

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    bool acceptsMidi() const override { return inner_->acceptsMidi(); }
    bool producesMidi() const override { return inner_->producesMidi(); }
    bool isMidiEffect() const override { return inner_->isMidiEffect(); }
    double getTailLengthSeconds() const override { return inner_->getTailLengthSeconds(); }
    int getLatencySamples(); // not override: we compute locally

    juce::AudioProcessorEditor* createEditor() override { return inner_->createEditor(); }
    bool hasEditor() const override { return inner_->hasEditor(); }

    int getNumPrograms() override { return inner_->getNumPrograms(); }
    int getCurrentProgram() override { return inner_->getCurrentProgram(); }
    void setCurrentProgram(int i) override { inner_->setCurrentProgram(i); }
    const juce::String getProgramName(int i) override { return inner_->getProgramName(i); }
    void changeProgramName(int i, const juce::String& n) override { inner_->changeProgramName(i, n); }

    void getStateInformation(juce::MemoryBlock& m) override { inner_->getStateInformation(m); }
    void setStateInformation(const void* d, int s) override { inner_->setStateInformation(d, s); }

    // Public controls --------------------------------------------------------

    // Flip the bypass target. Ramp completes over roughly 20 ms.
    void setBypass(bool shouldBypass) noexcept;
    bool isBypassed() const noexcept { return wantBypass_.load(std::memory_order_relaxed); }

    // MIDI channel filter. 0 = omni (pass everything). 1-16 = only pass
    // events whose channel matches. Matches Pedalboard2's MIDIChannel attr.
    void setMidiChannel(int channel1To16Or0) noexcept;
    int getMidiChannel() const noexcept { return midiChannel_; }

    // Lock-free CPU-metering read. Resets counters as a side effect so the
    // next read accumulates fresh numbers. Called by UI/diagnostics Timer.
    CpuStats consumeCpuStats() noexcept;

    // Lets tests poke the hosted processor. In production code, the Session
    // owns the BypassableInstance; nothing else needs this.
    juce::AudioProcessor* getHostedProcessor() const noexcept { return inner_.get(); }

    // Watchdog — block-budget overrun tracking ----------------------------
    //
    // Measures how long each call into the hosted processor takes. If it
    // exceeds `blockBudgetNanos_` three times in a row, the instance is
    // marked "dead": subsequent processBlock calls output silence without
    // invoking the hosted processor at all. A healthy (under-budget) block
    // resets the overrun counter. The Phase 1.B SEH/signal guard lands on
    // top of this and flips the same flag on a hard fault.
    //
    // Default budget in prepareToPlay is 75% of one block's wall-clock
    // time, so a plugin that eats the entire block is immediately over.
    // Tests set budget to 0 (every block overruns) or to uint64 max
    // (unreachable) to drive behaviour deterministically.

    void setBlockBudgetNanos(std::uint64_t ns) noexcept;
    std::uint64_t getBlockBudgetNanos() const noexcept { return blockBudgetNanos_.load(std::memory_order_relaxed); }

    bool isDead() const noexcept { return dead_.load(std::memory_order_acquire); }
    void clearDeath() noexcept;

    static constexpr int kConsecutiveOverrunsToDie = 3;

private:
    static constexpr double kRampSeconds = 0.020; // 20 ms linear bypass fade

    std::unique_ptr<juce::AudioProcessor> inner_;

    juce::AudioBuffer<float> dryScratch_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryRamp_;

    std::atomic<bool> wantBypass_{false};
    int midiChannel_ = 0; // UI-thread writer, RT reader

    std::atomic<std::uint64_t> elapsedNanosAccum_{0};
    std::atomic<std::uint64_t> blockCountAccum_{0};

    // Watchdog state. Default budget is "infinite" so an un-prepared
    // instance cannot trip the watchdog spuriously. prepareToPlay lowers
    // it to a real value; setBlockBudgetNanos(0) in tests means "every
    // block is an overrun".
    std::atomic<std::uint64_t> blockBudgetNanos_{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<int> consecutiveOverruns_{0};
    std::atomic<bool> dead_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BypassableInstance)
};

} // namespace pedalboard3::engine
