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
    deviceManager_.addChangeListener(this);
    initialised_ = true;

    if (deviceManager_.getCurrentAudioDevice() != nullptr)
        lastGoodState_ = deviceManager_.createStateXml();

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

    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(&player_);
    player_.setProcessor(nullptr);
    deviceManager_.closeAudioDevice();
    initialised_ = false;
}

std::unique_ptr<juce::AudioProcessorGraph> AudioEngine::loadGraph(std::unique_ptr<juce::AudioProcessorGraph> next)
{
    return runtime_->swapPreparedGraph(std::move(next));
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // AudioDeviceManager broadcasts for a setup change and for a device-list
    // change alike, so this runs often and has to be cheap and idempotent.
    if (reopening_)
        return;

    if (deviceManager_.getCurrentAudioDevice() != nullptr)
    {
        // Working: remember this, it is what we put back later.
        lastGoodState_ = deviceManager_.createStateXml();
        return;
    }

    // No device. If we ever had one, this is an unplug -- or the replug
    // after it, which is the case JUCE does not handle.
    if (lastGoodState_ != nullptr)
        reopenRememberedDevice();
}

bool AudioEngine::reopenRememberedDevice()
{
    if (!initialised_ || lastGoodState_ == nullptr)
        return deviceManager_.getCurrentAudioDevice() != nullptr;

    if (deviceManager_.getCurrentAudioDevice() != nullptr)
        return true;

    // setAudioDeviceSetup / initialise broadcast, and we are inside a
    // broadcast: without this the failure path would re-enter and spin.
    const juce::ScopedValueSetter<bool> busy(reopening_, true);

    // selectDefaultDeviceOnFailure stays FALSE here on purpose. This runs on
    // every device-list change while nothing is open -- including the
    // unplug itself -- and falling back to "some other device" would quietly
    // move a player's output to the laptop speakers mid-set. Either the
    // device they chose is back or we stay silent and say so.
    const auto err = deviceManager_.initialise(2, 2, lastGoodState_.get(), /*selectDefaultOnFailure*/ false);

    const bool open = deviceManager_.getCurrentAudioDevice() != nullptr;
    if (open)
    {
        lastInitError_.clear();
        lastGoodState_ = deviceManager_.createStateXml();
        deviceRecoveredBroadcaster_.sendChangeMessage();
    }
    else if (err.isNotEmpty())
    {
        lastInitError_ = err;
    }
    return open;
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

void AudioEngine::setTestToneEnabled(bool shouldPlay)
{
    if (runtime_->getTestTone().isEnabled() == shouldPlay)
        return; // same as the mute: no broadcast for a no-op

    runtime_->getTestTone().setEnabled(shouldPlay);
    testToneBroadcaster_.sendChangeMessage();
}

bool AudioEngine::isTestToneEnabled() const noexcept
{
    return runtime_->getTestTone().isEnabled();
}

} // namespace pedalboard3::engine
