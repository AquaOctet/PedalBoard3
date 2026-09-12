#include "MidiInputManager.h"

#include <utility>

namespace pedalboard3::control
{

MidiMessageFifo::MidiMessageFifo(int capacity) : fifo_(capacity), buffer_(static_cast<size_t>(capacity)) {}

void MidiMessageFifo::push(const juce::MidiMessage& msg) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo_.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 + size2 < 1)
    {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    buffer_[static_cast<size_t>(start1)] = msg;
    fifo_.finishedWrite(1);
}

void MidiMessageFifo::drain(const std::function<void(const juce::MidiMessage&)>& sink)
{
    const int available = fifo_.getNumReady();
    if (available <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo_.prepareToRead(available, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        sink(buffer_[static_cast<size_t>(start1 + i)]);
    for (int i = 0; i < size2; ++i)
        sink(buffer_[static_cast<size_t>(start2 + i)]);

    fifo_.finishedRead(size1 + size2);
}

MidiInputManager::MidiInputManager(juce::AudioDeviceManager* deviceManager, Sink sink)
    : deviceManager_(deviceManager), sink_(std::move(sink))
{
    syncEnabledInputs();

    if (deviceManager_ != nullptr)
        deviceManager_->addChangeListener(this);

    startTimer(kDrainIntervalMs);
}

MidiInputManager::~MidiInputManager()
{
    stopTimer();

    if (deviceManager_ != nullptr)
    {
        deviceManager_->removeChangeListener(this);
        for (const auto& id : registeredDeviceIds_)
            deviceManager_->removeMidiInputDeviceCallback(id, this);
    }
}

void MidiInputManager::handleIncomingMidiMessage(juce::MidiInput* /*source*/, const juce::MidiMessage& message)
{
    fifo_.push(message); // MIDI thread
}

void MidiInputManager::changeListenerCallback(juce::ChangeBroadcaster*)
{
    syncEnabledInputs(); // message thread — AudioDeviceManager broadcasts here
}

void MidiInputManager::timerCallback()
{
    if (enabled_)
        fifo_.drain(sink_); // message thread
    else
        fifo_.drain([](const juce::MidiMessage&) {}); // switched off: drop what's queued
}

void MidiInputManager::setEnabled(bool enabled)
{
    if (enabled == enabled_)
        return;
    enabled_ = enabled;
    if (enabled_)
    {
        syncEnabledInputs();
        return;
    }
    if (deviceManager_ != nullptr)
        for (const auto& id : registeredDeviceIds_)
            deviceManager_->removeMidiInputDeviceCallback(id, this);
    registeredDeviceIds_.clear();
    fifo_.drain([](const juce::MidiMessage&) {});
}

void MidiInputManager::syncEnabledInputs()
{
    if (deviceManager_ == nullptr || !enabled_)
        return;

    juce::StringArray currentlyEnabled;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (deviceManager_->isMidiInputDeviceEnabled(device.identifier))
            currentlyEnabled.add(device.identifier);

    for (const auto& id : registeredDeviceIds_)
        if (!currentlyEnabled.contains(id))
            deviceManager_->removeMidiInputDeviceCallback(id, this);

    for (const auto& id : currentlyEnabled)
        if (!registeredDeviceIds_.contains(id))
            deviceManager_->addMidiInputDeviceCallback(id, this);

    registeredDeviceIds_ = currentlyEnabled;
}

} // namespace pedalboard3::control
