#include "AudioEngine.h"

namespace pedalboard3::engine
{

AudioEngine::AudioEngine() : runtime_(std::make_unique<AudioGraphRuntime>()) {}

AudioEngine::~AudioEngine()
{
    shutdown();
}

AudioEngine::Result AudioEngine::initialise(const juce::XmlElement* savedState)
{
    if (initialised_)
        return Result::ok();

    // 2 inputs + 2 outputs = the Phase 1 canonical stereo pipeline. Users
    // whose hardware can't provide both will see the result's error message.
    // On headless Linux CI the device manager returns a descriptive string
    // without throwing. With savedState non-null, JUCE tries the persisted
    // device first and falls back to defaults on failure (last arg = true).
    const auto err = deviceManager_.initialise(/*numInputs*/ 2,
                                               /*numOutputs*/ 2,
                                               savedState,
                                               /*selectDefaultDeviceOnFailure*/ true);

    player_.setProcessor(runtime_.get());
    deviceManager_.addAudioCallback(&player_);
    initialised_ = true;

    // Retain the message so the UI can surface WHY audio is silent; the
    // returned Result is otherwise the only carrier and callers drop it.
    lastInitError_ = err;

    if (err.isNotEmpty())
        return Result::fail(err);

    return Result::ok();
}

void AudioEngine::shutdown()
{
    if (!initialised_)
        return;

    deviceManager_.removeAudioCallback(&player_);
    player_.setProcessor(nullptr);
    deviceManager_.closeAudioDevice();
    initialised_ = false;
}

std::unique_ptr<juce::AudioProcessorGraph> AudioEngine::loadGraph(std::unique_ptr<juce::AudioProcessorGraph> next)
{
    return runtime_->swapPreparedGraph(std::move(next));
}

void AudioEngine::setMasterMuted(bool shouldMute)
{
    if (runtime_->getMasterMute().isMuted() == shouldMute)
        return; // no broadcast for a no-op, so listeners don't redraw for nothing

    runtime_->getMasterMute().setMuted(shouldMute);
    masterMuteBroadcaster_.sendChangeMessage();
}

bool AudioEngine::isMasterMuted() const noexcept
{
    return runtime_->getMasterMute().isMuted();
}

} // namespace pedalboard3::engine
