#include "BypassableInstance.h"

#include <chrono>

namespace pedalboard3::engine
{

BypassableInstance::BypassableInstance(std::unique_ptr<juce::AudioProcessor> inner)
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      inner_(std::move(inner))
{
    jassert(inner_ != nullptr);
}

bool BypassableInstance::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() &&
           !layouts.getMainInputChannelSet().isDisabled();
}

void BypassableInstance::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    dryRamp_.reset(sampleRate, kRampSeconds);
    dryRamp_.setCurrentAndTargetValue(wantBypass_.load(std::memory_order_relaxed) ? 1.0f : 0.0f);

    // Scratch for the pre-plugin (dry) snapshot. 2x buffer gives a little
    // headroom for host block-size variation without reallocating mid-stream.
    const int scratchChannels = juce::jmax(getTotalNumInputChannels(), 2);
    const int scratchFrames = juce::jmax(samplesPerBlock * 2, 256);
    dryScratch_.setSize(scratchChannels, scratchFrames, false, false, true);

    // Default watchdog budget: 75% of a block's wall-clock duration. The
    // plugin is allowed to use most of the block, but not all of it — a
    // plugin that eats every sample of headroom is already hurting the
    // round-trip latency and hasn't left anything for JUCE or the OS.
    if (sampleRate > 0.0 && samplesPerBlock > 0)
    {
        const double blockSeconds = static_cast<double>(samplesPerBlock) / sampleRate;
        const auto budgetNs = static_cast<std::uint64_t>(blockSeconds * 0.75 * 1e9);
        blockBudgetNanos_.store(budgetNs, std::memory_order_relaxed);
    }
    consecutiveOverruns_.store(0, std::memory_order_relaxed);

    inner_->setRateAndBufferSizeDetails(sampleRate, samplesPerBlock);
    inner_->prepareToPlay(sampleRate, samplesPerBlock);
}

void BypassableInstance::setBlockBudgetNanos(std::uint64_t ns) noexcept
{
    blockBudgetNanos_.store(ns, std::memory_order_relaxed);
}

void BypassableInstance::clearDeath() noexcept
{
    consecutiveOverruns_.store(0, std::memory_order_relaxed);
    dead_.store(false, std::memory_order_release);
}

int BypassableInstance::getLatencySamples()
{
    if (wantBypass_.load(std::memory_order_relaxed))
        return 0;
    return inner_->getLatencySamples();
}

void BypassableInstance::setBypass(bool shouldBypass) noexcept
{
    wantBypass_.store(shouldBypass, std::memory_order_relaxed);
}

void BypassableInstance::setMidiChannel(int channel1To16Or0) noexcept
{
    midiChannel_ = juce::jlimit(0, 16, channel1To16Or0);
}

BypassableInstance::CpuStats BypassableInstance::consumeCpuStats() noexcept
{
    return CpuStats{
        elapsedNanosAccum_.exchange(0, std::memory_order_acq_rel),
        blockCountAccum_.exchange(0, std::memory_order_acq_rel),
    };
}

void BypassableInstance::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals nodn;

    // Dead path: plugin has been 3-strikes'd or fault-marked. Emit silence
    // and bail out without calling the hosted processor or touching the
    // watchdog state. Stays silent until clearDeath() is invoked from the
    // message thread.
    if (dead_.load(std::memory_order_acquire))
    {
        buffer.clear();
        midi.clear();
        blockCountAccum_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto startTicks = std::chrono::steady_clock::now();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    dryRamp_.setTargetValue(wantBypass_.load(std::memory_order_relaxed) ? 1.0f : 0.0f);

    // 1. Snapshot dry audio before the plugin trashes `buffer`.
    if (dryScratch_.getNumSamples() < numSamples || dryScratch_.getNumChannels() < numChannels)
    {
        // Defensive: prepareToPlay's 2x sizing should make this unreachable,
        // but if the host feeds us an oversized block we resize. This is the
        // only non-RT path in processBlock; unreachable after warm-up.
        dryScratch_.setSize(numChannels, numSamples, false, false, true);
    }
    for (int ch = 0; ch < numChannels; ++ch)
        dryScratch_.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // 2. Filter incoming MIDI by per-instance channel (0 = omni).
    if (midiChannel_ == 0)
    {
        inner_->processBlock(buffer, midi);
    }
    else
    {
        juce::MidiBuffer filtered;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.getChannel() == midiChannel_)
                filtered.addEvent(msg, meta.samplePosition);
        }
        inner_->processBlock(buffer, filtered);
        if (inner_->producesMidi())
            midi.swapWith(filtered);
    }

    // 3. Apply the ramped wet/dry mix. r == 0 -> fully wet (plugin output).
    //    r == 1 -> fully dry. Linear sample-accurate ramp.
    for (int n = 0; n < numSamples; ++n)
    {
        const float r = dryRamp_.getNextValue();
        const float wetGain = 1.0f - r;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = buffer.getWritePointer(ch);
            const auto* dry = dryScratch_.getReadPointer(ch);
            out[n] = out[n] * wetGain + dry[n] * r;
        }
    }

    // 4. Metering. Relaxed add is fine — no cross-thread publication of
    //    partial results matters; message thread reads via exchange(0).
    const auto elapsedNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startTicks).count();
    elapsedNanosAccum_.fetch_add(static_cast<std::uint64_t>(elapsedNs), std::memory_order_relaxed);
    blockCountAccum_.fetch_add(1, std::memory_order_relaxed);

    // 5. Watchdog. Default budget is UINT64_MAX, so un-prepared instances
    //    don't trip. prepareToPlay lowers it to a realistic value; tests
    //    set budget to 0 to force every block into the overrun branch.
    const auto budget = blockBudgetNanos_.load(std::memory_order_relaxed);
    if (static_cast<std::uint64_t>(elapsedNs) > budget)
    {
        if (consecutiveOverruns_.fetch_add(1, std::memory_order_relaxed) + 1 >= kConsecutiveOverrunsToDie)
        {
            dead_.store(true, std::memory_order_release);
        }
    }
    else
    {
        consecutiveOverruns_.store(0, std::memory_order_relaxed);
    }
}

} // namespace pedalboard3::engine
