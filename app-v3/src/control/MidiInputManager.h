#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace pedalboard3::control
{

// Single-producer/single-consumer ring of juce::MidiMessage. The testable
// seam (roadmap M2, plan docs/plans/2026-07-04-001-feat-live-midi-osc-control-
// plan.md, U1/U2, KTD1): headless-testable without any device or
// AudioDeviceManager.
//
// RT contract: push() is allocation-free for every message shape the control
// layer actually routes (ControlChange / Note / ProgramChange / PitchBend —
// control::MidiSourceKind's complete set), all ≤3 bytes and covered by
// juce::MidiMessage's small-message inline storage. On overflow it drops the
// message and counts it rather than blocking the calling (MIDI) thread —
// control loss is preferable to stalling a real-time-ish input thread.
class MidiMessageFifo
{
public:
    explicit MidiMessageFifo(int capacity = 512);

    // MIDI-thread safe: single producer. noexcept because a dropped/overflowed
    // write must never let an exception unwind through a device callback;
    // every message this feature copies is a small, non-allocating shape (see
    // class doc), so this holds in practice, not just in principle.
    void push(const juce::MidiMessage& msg) noexcept;

    // Message-thread only: single consumer. Delivers every currently
    // available message, in order, to `sink`, then marks them consumed.
    void drain(const std::function<void(const juce::MidiMessage&)>& sink);

    // Cumulative count of pushes dropped due to a full ring since construction.
    int droppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    juce::AbstractFifo fifo_;
    std::vector<juce::MidiMessage> buffer_;
    std::atomic<int> dropped_{0};
};

// Registers a MidiMessageFifo-backed callback for every currently-enabled MIDI
// input device on the given AudioDeviceManager, and drains it on a
// message-thread juce::Timer, delivering each message to the injected sink
// (ControlHub::handleMidi in production). Device *enablement* stays owned by
// AudioDeviceSelectorComponent (Audio Settings) and its persisted state; this
// class only consumes whatever is currently enabled and re-syncs when
// AudioDeviceManager (a juce::ChangeBroadcaster) reports a change.
//
// Threading: handleIncomingMidiMessage runs on JUCE's MIDI input thread (one
// per open device). Construction, the drain timer, changeListenerCallback,
// and destruction are message-thread only.
class MidiInputManager : private juce::MidiInputCallback, private juce::ChangeListener, private juce::Timer
{
public:
    using Sink = std::function<void(const juce::MidiMessage&)>;

    // `deviceManager` may be nullptr (e.g. a headless dev server with no
    // audio device) — construction/destruction stay safe with no callbacks
    // registered and nothing to drain.
    MidiInputManager(juce::AudioDeviceManager* deviceManager, Sink sink);
    ~MidiInputManager() override;

    // ~120 Hz: fast enough that a knob sweep reads as continuous, cheap
    // enough to run unconditionally on the message thread.
    static constexpr int kDrainIntervalMs = 8;

    int droppedCount() const noexcept { return fifo_.droppedCount(); }

    // MIDI on or off (optional-features plan KD6; session::InputGates drives
    // it). Off unregisters every device callback, ignores device changes and
    // drops whatever is queued, so no MIDI reaches anything. On registers the
    // enabled devices again. On by default.
    void setEnabled(bool enabled);
    bool isEnabled() const noexcept { return enabled_; }

private:
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // Diffs the currently-enabled MIDI input set against registeredDeviceIds_
    // and adds/removes device callbacks for the delta only (JUCE's
    // add/removeMidiInputDeviceCallback are idempotent per pair, but
    // unconditionally re-registering on every AudioDeviceManager change
    // message would be wasteful).
    void syncEnabledInputs();

    juce::AudioDeviceManager* deviceManager_;
    Sink sink_;
    MidiMessageFifo fifo_;
    juce::StringArray registeredDeviceIds_;
    bool enabled_{true};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiInputManager)
};

} // namespace pedalboard3::control
