#include "AudioGraphRuntime.h"

#include <chrono>
#include <thread>

namespace pedalboard3::engine
{

namespace
{
constexpr int kMaxAckWaitMs = 2000;  // hard ceiling per AC-1.D.2-R
constexpr int kFlushChannelsMin = 2; // stereo at minimum for the silence scratch
constexpr int kSilenceScratchMinFrames = 4096;
constexpr int kFlushBytesPerEvent = 12; // conservative per-event byte budget for ensureSize
constexpr int kFlushEventsPerChannel = 3;
constexpr int kMidiChannels = 16;
constexpr int kCcResetAllControllers = 121;
} // namespace

AudioGraphRuntime::AudioGraphRuntime()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

AudioGraphRuntime::~AudioGraphRuntime()
{
    // Drain both atomics — a published-but-never-consumed pending_ at
    // teardown is the first-load-then-quit edge case (Phase 1.D.2). If
    // the audio device is still pumping us, this is unsafe, but JUCE's
    // AudioDeviceManager teardown order in production guarantees the
    // callback is stopped before the runtime dtor runs.
    if (auto* p = pending_.exchange(nullptr, std::memory_order_acq_rel))
        delete p;
    if (auto* g = live_.exchange(nullptr, std::memory_order_acq_rel))
        delete g;
}

void AudioGraphRuntime::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;
    currentBlockSize_ = samplesPerBlock;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    sanitiser_.setPlayConfigDetails(numIn, numOut, sampleRate, samplesPerBlock);
    sanitiser_.prepareToPlay(sampleRate, samplesPerBlock);

    // Pre-build the all-notes-off flush template once. 16ch × 3 events.
    // Order per channel: allSoundOff → allNotesOff → resetAllControllers
    // (CC#121). This ordering matters: synths that latch CC#64 (sustain)
    // ignore allNotesOff until sustain is released; allSoundOff kills
    // tails immediately, then resetAllControllers releases sustain so
    // the next note-off path is clean.
    const int eventCount = kMidiChannels * kFlushEventsPerChannel;
    flushMidiTemplate_.clear();
    flushMidiTemplate_.ensureSize(eventCount * kFlushBytesPerEvent + 64);
    for (int ch = 1; ch <= kMidiChannels; ++ch)
    {
        flushMidiTemplate_.addEvent(juce::MidiMessage::allSoundOff(ch), 0);
        flushMidiTemplate_.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
        flushMidiTemplate_.addEvent(juce::MidiMessage::controllerEvent(ch, kCcResetAllControllers, 0), 0);
    }
    flushMidiScratch_.ensureSize(eventCount * kFlushBytesPerEvent + 64);

    // Pre-allocate silence scratch sized for adversarial hosts (4× block,
    // floor of 4096 frames). Pro Tools / Reaper anticipative-FX / CoreAudio
    // SR-switch all dispatch oversized blocks; we'd rather pay 32 KB up
    // front than allocate on the audio thread.
    const int maxBlock = juce::jmax(samplesPerBlock * 4, kSilenceScratchMinFrames);
    const int maxChan = juce::jmax(numOut, kFlushChannelsMin);
    silenceScratch_.setSize(maxChan, maxBlock, false, false, true);
    silenceScratch_.clear();

    if (auto* g = live_.load(std::memory_order_acquire))
        g->prepareToPlay(sampleRate, samplesPerBlock);
    if (auto* p = pending_.load(std::memory_order_acquire))
        p->prepareToPlay(sampleRate, samplesPerBlock);
}

void AudioGraphRuntime::releaseResources()
{
    sanitiser_.releaseResources();
    if (auto* g = live_.load(std::memory_order_acquire))
        g->releaseResources();
}

bool AudioGraphRuntime::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() &&
           !layouts.getMainInputChannelSet().isDisabled();
}

void AudioGraphRuntime::flushMidiReceivers(juce::AudioProcessorGraph* outgoing) noexcept
{
    if (outgoing == nullptr)
        return;

    // Copy the prebuilt template into our scratch — plugins that
    // producesMidi() will write through, so we can't pass the template
    // itself. ensureSize() in prepareToPlay made this an in-place copy,
    // not a reallocation.
    flushMidiScratch_ = flushMidiTemplate_;

    // Build a lightweight view over silenceScratch_ matching the runtime's
    // negotiated block + the graph's output channel count. juce::AudioBuffer
    // can wrap an existing float** without copying.
    const int frames = juce::jmin(silenceScratch_.getNumSamples(), juce::jmax(currentBlockSize_, 1));
    const int channels =
        juce::jmin(silenceScratch_.getNumChannels(), juce::jmax(outgoing->getTotalNumOutputChannels(), 1));
    juce::AudioBuffer<float> view(silenceScratch_.getArrayOfWritePointers(), channels, frames);

    outgoing->processBlock(view, flushMidiScratch_);
}

void AudioGraphRuntime::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals nodn;

    // The transport's snapshot for this block: every plugin in the block
    // sees the same position, and a Return to Zero lands here.
    transport_.beginBlock(currentSampleRate_);

    // 1. Atomic check for a pending swap. exchange(acquire) ensures all
    //    of the message thread's writes that happened-before publishing
    //    the pending pointer (including the prepared graph's prepareToPlay)
    //    are visible to the audio thread before we use it.
    auto* incoming = pending_.exchange(nullptr, std::memory_order_acquire);
    auto* g = live_.load(std::memory_order_acquire);

    if (incoming != nullptr)
    {
        // Run the all-notes-off flush on the OUTGOING graph BEFORE we
        // publish the new live pointer. This is the §4.1.1 invariant —
        // outgoing MIDI receivers see allNotesOff/allSoundOff/CC121 on
        // every channel, so synths don't hang notes through the swap.
        flushMidiReceivers(g);

        // Commit the swap. release pairs with the message thread's
        // acquire on a future getLiveGraph() observation; also pairs
        // with the next processBlock's acquire load of live_.
        live_.store(incoming, std::memory_order_release);
        g = incoming;
    }

    // 2. Single atomic read per block — the contention point with the
    //    message-thread swap path.
    if (g == nullptr)
    {
        buffer.clear();
        midi.clear();
    }
    else
    {
        // The graph passes its play head on to every node as it processes.
        g->setPlayHead(&transport_);
        g->processBlock(buffer, midi);
    }
    transport_.endBlock(buffer.getNumSamples());

    sanitiser_.processBlock(buffer, midi);

    // 3. Publish that we've completed a block. Release so a concurrent
    //    reader using `acquire` sees all the buffer writes that preceded
    //    this store.
    generationAckedByAudio_.fetch_add(1, std::memory_order_release);
}

std::unique_ptr<juce::AudioProcessorGraph> AudioGraphRuntime::swapPreparedGraph(
    std::unique_ptr<juce::AudioProcessorGraph> next)
{
    // Caller must be on the message thread. The assert is free in release
    // but catches misuse in debug.
    jassert(juce::MessageManager::existsAndIsCurrentThread() || !juce::MessageManager::getInstanceWithoutCreating());

    auto* rawNext = next.release(); // nullptr allowed — clears the runtime

    // Synchronous fast path #1: if we haven't been prepared (no audio
    // device), do a plain message-thread swap with no flush, no wait.
    // Synchronous fast path #2: if `next` is null (unload), the audio-
    // thread handshake can't carry a sentinel — exchange(nullptr) on
    // pending_ would mean "no swap requested". Do the unload on the
    // message thread directly. This skips the all-notes-off flush; for
    // 1.D.2 scope acceptable, since the typical unload follows an
    // explicit user click and any held notes will be released by the
    // outgoing graph's destructor anyway.
    if (currentBlockSize_ <= 0 || currentSampleRate_ <= 0.0 || rawNext == nullptr)
    {
        auto* prev = live_.exchange(rawNext, std::memory_order_acq_rel);
        if (prev != nullptr)
            prev->releaseResources();
        return std::unique_ptr<juce::AudioProcessorGraph>(prev);
    }

    // Wait for any prior pending swap to be consumed before we publish a
    // new one. The previous swap's wait should already have ensured this,
    // but a timeout-then-fallback path could leave residue. Use the same
    // wait budget as the main ack-wait below.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxAckWaitMs);
    while (pending_.load(std::memory_order_acquire) != nullptr)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            // Stuck pending_ — audio thread isn't pumping. Force-clear
            // and fall through to the synchronous fallback below.
            if (auto* stuck = pending_.exchange(nullptr, std::memory_order_acq_rel))
                delete stuck;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Snapshot the previous live for caller-side reclamation. Audio thread
    // only writes live_ when consuming pending_, and pending_ is currently
    // null, so this read is stable.
    auto* prev = live_.load(std::memory_order_acquire);

    // Publish.
    pending_.store(rawNext, std::memory_order_release);

    // Wait for the audio thread to consume pending_ AND complete one
    // block on the new graph. Two acks: one for the swap block, one
    // safety margin so the outgoing graph is no longer being processed.
    const auto startGen = generationAckedByAudio_.load(std::memory_order_acquire);
    const auto targetGen = startGen + 2;
    const auto blockMs = static_cast<int>(1000.0 * currentBlockSize_ / currentSampleRate_);
    const auto budgetMs = juce::jmin(kMaxAckWaitMs, juce::jmax(50, blockMs * 10));
    const auto ackDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);

    bool acked = false;
    while (std::chrono::steady_clock::now() < ackDeadline)
    {
        if (generationAckedByAudio_.load(std::memory_order_acquire) >= targetGen)
        {
            acked = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!acked)
    {
        // Timeout — audio thread didn't consume our pending_. Fall back
        // to a synchronous message-thread swap (no flush). Snatch
        // pending_ back so we don't leave it dangling.
        auto* stuck = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (stuck == rawNext)
        {
            // Still ours; force the swap synchronously.
            auto* oldLive = live_.exchange(rawNext, std::memory_order_acq_rel);
            if (oldLive != nullptr)
                oldLive->releaseResources();
            return std::unique_ptr<juce::AudioProcessorGraph>(oldLive);
        }
        // Audio thread snatched it concurrently; live_ already holds
        // rawNext. Fall through and treat prev as the outgoing.
    }

    if (prev != nullptr)
        prev->releaseResources();
    return std::unique_ptr<juce::AudioProcessorGraph>(prev);
}

} // namespace pedalboard3::engine
