#include "BypassableInstance.h"

#include <chrono>

namespace pedalboard3::engine
{

namespace
{

// Every instance that has died in this process (M5 U5). Constant-initialised,
// so bumping it from the audio thread is a plain atomic add.
std::atomic<std::uint64_t> gDeathCount{0};

// The calls that run the hosted processor's code, as the crash guard takes
// them: callables that don't throw. A C++ exception out of the plugin is caught
// here, below the guard, so it unwinds properly.
struct ProcessCall
{
    juce::AudioProcessor* processor;
    juce::AudioBuffer<float>* audio;
    juce::MidiBuffer* events;
    bool threw;
    void operator()() noexcept
    {
        try
        {
            processor->processBlock(*audio, *events);
        }
        catch (...)
        {
            threw = true;
        }
    }
};

struct PrepareCall
{
    juce::AudioProcessor* processor;
    double rate;
    int blockSize;
    bool threw;
    void operator()() noexcept
    {
        try
        {
            processor->setRateAndBufferSizeDetails(rate, blockSize);
            processor->prepareToPlay(rate, blockSize);
        }
        catch (...)
        {
            threw = true;
        }
    }
};

struct GetStateCall
{
    juce::AudioProcessor* processor;
    juce::MemoryBlock* destination;
    bool threw;
    void operator()() noexcept
    {
        try
        {
            processor->getStateInformation(*destination);
        }
        catch (...)
        {
            threw = true;
        }
    }
};

struct SetStateCall
{
    juce::AudioProcessor* processor;
    const void* data;
    int size;
    bool threw;
    void operator()() noexcept
    {
        try
        {
            processor->setStateInformation(data, size);
        }
        catch (...)
        {
            threw = true;
        }
    }
};

// Run a call through the guard: the fault, Fault::Exception if it threw, or
// Fault::None.
template <typename Call>
platform::Fault run(Call& call) noexcept
{
    const auto fault = platform::guarded(call);
    if (fault != platform::Fault::None)
        return fault;
    return call.threw ? platform::Fault::Exception : platform::Fault::None;
}

// A MIDI event's channel, 1 to 16, or 0 for a system message: what
// juce::MidiMessage::getChannel says, without building a MidiMessage (which
// allocates for a long one).
int channelOf(const juce::uint8* data, int numBytes) noexcept
{
    if (numBytes < 1)
        return 0;
    const int status = data[0];
    return (status >= 0x80 && status < 0xf0) ? (status & 0x0f) + 1 : 0;
}

} // namespace

BypassableInstance::BypassableInstance(std::unique_ptr<juce::AudioProcessor> inner)
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      inner_(std::move(inner))
{
    jassert(inner_ != nullptr);

    // Lock inner_'s bus topology once, here, before any prepareToPlay
    // sees it. Without this, plugins that expose sidechain or other
    // aux buses (FabFilter Pro-Q 3, most NI compressors, etc.) leave
    // aux-buffer pointers half-wired inside their JUCE VST3 wrapper —
    // the plugin's processBlock then reads from those pointers and
    // the access goes through null/garbage, SIGSEGVing the audio
    // thread. disableNonMainBuses + the explicit stereo constraint
    // matches what hosts like Pedalboard 1/2 and Ableton Live do for
    // their "everything is a stereo effect slot" chain semantics.
    inner_->disableNonMainBuses();

    auto layout = inner_->getBusesLayout();
    if (!layout.inputBuses.isEmpty())
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    if (!layout.outputBuses.isEmpty())
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    if (inner_->checkBusesLayoutSupported(layout))
        inner_->setBusesLayout(layout);

    name_ = inner_->getName();
    acceptsMidi_ = inner_->acceptsMidi();
    producesMidi_ = inner_->producesMidi();
    isMidiEffect_ = inner_->isMidiEffect();
}

BypassableInstance::~BypassableInstance()
{
    // A crashed hosted plugin may still hold the locks it held when it faulted
    // (its destructors never ran), so deleting it could hang the app: it's
    // leaked on purpose (M5 U5). Our own processors are safe to delete.
    if (hasCrashed() && dynamic_cast<juce::AudioPluginInstance*>(inner_.get()) != nullptr)
        (void)inner_.release();
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
    filtered_.ensureSize(kMidiReserveBytes);

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

    if (hasCrashed())
        return; // never called again

    // Bus topology is locked in the constructor (see comment there).
    // prepareToPlay only forwards rate/block-size details, through the guard.
    PrepareCall call{inner_.get(), sampleRate, samplesPerBlock, false};
    if (const auto fault = run(call); fault != platform::Fault::None)
        markCrashed(fault);
}

void BypassableInstance::releaseResources()
{
    if (!hasCrashed())
        inner_->releaseResources();
}

void BypassableInstance::setStateInformation(const void* data, int sizeInBytes)
{
    if (hasCrashed())
        return;
    // A plugin that throws on a blob it doesn't like hasn't crashed; one that
    // faults has.
    SetStateCall call{inner_.get(), data, sizeInBytes, false};
    if (const auto fault = run(call); fault != platform::Fault::None && fault != platform::Fault::Exception)
        markCrashed(fault);
}

bool BypassableInstance::captureState(juce::MemoryBlock& out)
{
    if (hasCrashed())
        return false;
    GetStateCall call{inner_.get(), &out, false};
    const auto fault = run(call);
    if (fault != platform::Fault::None && fault != platform::Fault::Exception)
        markCrashed(fault);
    return fault == platform::Fault::None;
}

void BypassableInstance::setBlockBudgetNanos(std::uint64_t ns) noexcept
{
    blockBudgetNanos_.store(ns, std::memory_order_relaxed);
}

void BypassableInstance::clearDeath() noexcept
{
    if (hasCrashed())
        return; // a crashed plugin isn't called again: Session::rearmNode loads a fresh one
    consecutiveOverruns_.store(0, std::memory_order_relaxed);
    dead_.store(false, std::memory_order_release);
}

void BypassableInstance::markCrashed(platform::Fault fault) noexcept
{
    fault_.store(static_cast<int>(fault));
    crashed_.store(true, std::memory_order_release);
    markDead();
}

void BypassableInstance::markDead() noexcept
{
    if (!dead_.exchange(true, std::memory_order_acq_rel))
        gDeathCount.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t BypassableInstance::deathCount() noexcept
{
    return gDeathCount.load(std::memory_order_relaxed);
}

int BypassableInstance::getLatencySamples()
{
    if (wantBypass_.load(std::memory_order_relaxed) || hasCrashed())
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

    // Dead path: the hosted processor crashed, or the watchdog stopped it.
    // Pass the input through untouched (M5 U5, plan KD2), as if bypassed, so the
    // rest of the chain keeps playing, and don't call it. A watchdog death lasts
    // until clearDeath(); a crash lasts for this instance's life.
    if (dead_.load(std::memory_order_acquire))
    {
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

    // 2. The plugin, through the crash guard. The MIDI channel filter (0 =
    //    omni) reads the raw bytes into a buffer reserved in prepareToPlay, so
    //    this path doesn't allocate.
    const int channel = midiChannel_;
    juce::MidiBuffer* events = &midi;
    if (channel != 0)
    {
        filtered_.clear();
        for (const auto meta : midi)
            if (channelOf(meta.data, meta.numBytes) == channel)
                filtered_.addEvent(meta.data, meta.numBytes, meta.samplePosition);
        events = &filtered_;
    }

    ProcessCall call{inner_.get(), &buffer, events, false};
    if (const auto fault = run(call); fault != platform::Fault::None)
    {
        // It crashed partway through: this block is the input as it came in.
        markCrashed(fault);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom(ch, 0, dryScratch_, ch, 0, numSamples);
        blockCountAccum_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (channel != 0 && producesMidi_)
        midi.swapWith(filtered_);

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
            markDead();
    }
    else
    {
        consecutiveOverruns_.store(0, std::memory_order_relaxed);
    }
}

} // namespace pedalboard3::engine
