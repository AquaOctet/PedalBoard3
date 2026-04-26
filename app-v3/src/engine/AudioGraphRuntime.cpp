#include "AudioGraphRuntime.h"

#include <chrono>
#include <thread>

namespace pedalboard3::engine
{

AudioGraphRuntime::AudioGraphRuntime()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

AudioGraphRuntime::~AudioGraphRuntime()
{
    // Whoever owns us is responsible for destructing the live graph via
    // swapPreparedGraph(nullptr_replacement) before teardown in production.
    // Keep the pointer here so tests that leak can notice via the leak
    // detector; delete if we outlive our owner.
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

    if (auto* g = live_.load(std::memory_order_acquire))
        g->prepareToPlay(sampleRate, samplesPerBlock);
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

void AudioGraphRuntime::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals nodn;

    // Single atomic read per block — the one contention point with the
    // message-thread swap path.
    auto* g = live_.load(std::memory_order_acquire);
    if (g == nullptr)
    {
        buffer.clear();
        midi.clear();
    }
    else
    {
        g->processBlock(buffer, midi);
    }

    sanitiser_.processBlock(buffer, midi);

    // Publish that we've completed a block. Release so a concurrent reader
    // using `acquire` sees all the buffer writes that preceded this store.
    generationAckedByAudio_.fetch_add(1, std::memory_order_release);
}

std::unique_ptr<juce::AudioProcessorGraph> AudioGraphRuntime::swapPreparedGraph(
    std::unique_ptr<juce::AudioProcessorGraph> next)
{
    // Caller must be on the message thread. The assert is free in release
    // but catches misuse in debug (message-thread contract for state that
    // the audio thread reads via atomic).
    jassert(juce::MessageManager::existsAndIsCurrentThread() || !juce::MessageManager::getInstanceWithoutCreating());

    auto* rawNext = next.release(); // nullptr allowed — clears the runtime
    auto* rawPrev = live_.exchange(rawNext, std::memory_order_acq_rel);

    // Wait for the audio thread to move past the swap so it's impossible
    // for `rawPrev->processBlock` to still be in flight. If the runtime
    // hasn't been prepared yet (no audio device attached), the generation
    // counter never advances — short-circuit so tests don't hang.
    if (currentBlockSize_ > 0 && currentSampleRate_ > 0.0)
    {
        const auto startGen = generationAckedByAudio_.load(std::memory_order_acquire);
        const auto targetGen = startGen + 2; // one full block of slack
        const auto blockMs = static_cast<int>(1000.0 * currentBlockSize_ / currentSampleRate_);
        const auto maxWaitIters = juce::jmax(5, blockMs * 3); // up to ~3 blocks
        for (int i = 0; i < maxWaitIters; ++i)
        {
            if (generationAckedByAudio_.load(std::memory_order_acquire) >= targetGen)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (rawPrev != nullptr)
        rawPrev->releaseResources();

    return std::unique_ptr<juce::AudioProcessorGraph>(rawPrev);
}

} // namespace pedalboard3::engine
