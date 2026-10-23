#pragma once

#include "platform/CrashGuard.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>

namespace pedalboard3::engine
{

// Wraps a hosted AudioProcessor with host-managed bypass, MIDI channel
// filtering, per-node CPU metering, a block-budget watchdog and the crash guard.
//
// Crash guard (M5 U5, plan KD1 / KD2): every call that runs the hosted
// processor's code (processBlock, prepareToPlay, get/setStateInformation) goes
// through platform::guardedCall. A fault, or a C++ exception out of
// processBlock or prepareToPlay, marks the instance crashed. From then on its
// slot passes the input through, as if bypassed, so the rest of the chain keeps
// playing; the block it crashed in is the input too, from the dry snapshot. A
// crash can leave the plugin's locks held (its destructors never ran), so a
// crashed hosted plugin is never called again, not even to be deleted: getting
// it back means loading a fresh instance (Session::rearmNode).
//
// RT contract: processBlock does not allocate, lock, or perform I/O. The
// per-instance ramp state and CPU counters live in value types owned by this
// object; the dry snapshot and the MIDI filter's buffer are reserved in
// prepareToPlay.
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
    ~BypassableInstance() override;

    // AudioProcessor ---------------------------------------------------------
    // What the hosted processor says of itself is read once, here, so a crashed
    // plugin is never asked again.
    const juce::String getName() const override { return name_; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    // The graph hands each node its play head every block. Pass it on, or the
    // hosted plugin never sees the transport (M6 U1).
    void setPlayHead(juce::AudioPlayHead* newPlayHead) override
    {
        juce::AudioProcessor::setPlayHead(newPlayHead);
        if (!hasCrashed())
            inner_->setPlayHead(newPlayHead);
    }

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    bool acceptsMidi() const override { return acceptsMidi_; }
    bool producesMidi() const override { return producesMidi_; }
    bool isMidiEffect() const override { return isMidiEffect_; }
    double getTailLengthSeconds() const override { return hasCrashed() ? 0.0 : inner_->getTailLengthSeconds(); }
    int getLatencySamples(); // not override: we compute locally

    // createEditorAndMakeActive, not the inner's createEditor: JUCE 8.0.16 made
    // AudioProcessor::createEditor private so the active-editor slot cannot be
    // bypassed. The slot it fills is the INNER's -- the returned component holds
    // a reference to inner_, not to us -- so the host has two slots to clear
    // before it deletes the editor. ui::EditorWindow does that.
    juce::AudioProcessorEditor* createEditor() override
    {
        return hasCrashed() ? nullptr : inner_->createEditorAndMakeActive();
    }
    bool hasEditor() const override { return !hasCrashed() && inner_->hasEditor(); }

    int getNumPrograms() override { return hasCrashed() ? 1 : inner_->getNumPrograms(); }
    int getCurrentProgram() override { return hasCrashed() ? 0 : inner_->getCurrentProgram(); }
    void setCurrentProgram(int i) override
    {
        if (!hasCrashed())
            inner_->setCurrentProgram(i);
    }
    const juce::String getProgramName(int i) override
    {
        return hasCrashed() ? juce::String() : inner_->getProgramName(i);
    }
    void changeProgramName(int i, const juce::String& n) override
    {
        if (!hasCrashed())
            inner_->changeProgramName(i, n);
    }

    // The hosted processor's state, through the crash guard.
    void getStateInformation(juce::MemoryBlock& m) override { captureState(m); }
    void setStateInformation(const void* d, int s) override;

    // Append the hosted processor's state to `out`, through the crash guard.
    // False when it has crashed, crashes now or throws; `out` may then hold part
    // of a state, so capture into a fresh block.
    bool captureState(juce::MemoryBlock& out);

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
    // marked "dead": its slot passes the input through (M5 U5, plan KD2)
    // without invoking the hosted processor at all. A healthy (under-budget)
    // block resets the overrun counter; clearDeath() brings a dead one back.
    //
    // Default budget in prepareToPlay is 75% of one block's wall-clock
    // time, so a plugin that eats the entire block is immediately over.
    // Tests set budget to 0 (every block overruns) or to uint64 max
    // (unreachable) to drive behaviour deterministically.

    void setBlockBudgetNanos(std::uint64_t ns) noexcept;
    std::uint64_t getBlockBudgetNanos() const noexcept { return blockBudgetNanos_.load(std::memory_order_relaxed); }

    // Dead: the watchdog stopped it, or it crashed.
    bool isDead() const noexcept { return dead_.load(std::memory_order_acquire); }
    // Brings back a watchdog death. A crash stays: the plugin isn't called again.
    void clearDeath() noexcept;

    // Crashed: the hosted processor faulted (or threw), for good. isDead() too.
    bool hasCrashed() const noexcept { return crashed_.load(std::memory_order_acquire); }
    platform::Fault getFault() const noexcept { return static_cast<platform::Fault>(fault_.load()); }

    // How many instances have died (crashed, or stopped by the watchdog) in
    // this process. The canvas polls it to know when a card needs another look.
    static std::uint64_t deathCount() noexcept;

    static constexpr int kConsecutiveOverrunsToDie = 3;

private:
    static constexpr double kRampSeconds = 0.020; // 20 ms linear bypass fade
    static constexpr std::size_t kMidiReserveBytes = 4096;

    void markCrashed(platform::Fault fault) noexcept;
    void markDead() noexcept;

    std::unique_ptr<juce::AudioProcessor> inner_;
    juce::String name_;
    bool acceptsMidi_{false};
    bool producesMidi_{false};
    bool isMidiEffect_{false};

    juce::AudioBuffer<float> dryScratch_;
    juce::MidiBuffer filtered_; // the MIDI channel filter's events; reserved in prepareToPlay
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

    // Crash guard state (M5 U5).
    std::atomic<bool> crashed_{false};
    std::atomic<int> fault_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BypassableInstance)
};

} // namespace pedalboard3::engine
