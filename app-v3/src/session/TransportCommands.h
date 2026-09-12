#pragma once

#include "TapTempo.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace pedalboard3::engine
{
class Transport;
}

namespace pedalboard3::session
{

class Session;

// The transport's app commands (M6 U3, plan KD3-KD5): Play / Pause, Stop,
// Return to Zero and Tap Tempo, whichever key, MIDI or OSC trigger runs them,
// plus optional MIDI Machine Control.
//   - One TapTempo serves every trigger, so taps from a key and a footswitch
//     count together.
//   - A tapped tempo goes through Session::setTempo, so it's saved with the
//     patch and the board's unsaved marker sees it.
//
// Message thread only. App owns it; AppContext hands it to the UI, and the
// patch switcher runs mapped commands and MMC through it.
class TransportCommands
{
public:
    TransportCommands(Session& session, engine::Transport& transport);

    void playPause();
    void stop();         // pauses and returns to zero
    void returnToZero(); // keeps playing if it was
    void tap();          // a Tap Tempo tap, now
    void tapAt(double nowMs);

    // MIDI Machine Control, off by default (MIDI Settings): Play and Deferred
    // Play play, Stop stops, Pause toggles, Rewind returns to zero. Returns true
    // when `msg` was an MMC command it acted on.
    void setMmcEnabled(bool enabled) noexcept { mmcEnabled_ = enabled; }
    bool isMmcEnabled() const noexcept { return mmcEnabled_; }
    bool handleMmc(const juce::MidiMessage& msg);

    // Tempo / BPM on or off (optional-features plan KD6; session::InputGates
    // drives it). Off, every command and MMC does nothing, and switching off
    // pauses the transport, so it can't keep playing with no controls on
    // screen. On by default.
    void setEnabled(bool enabled);
    bool isEnabled() const noexcept { return enabled_; }

    engine::Transport& transport() noexcept { return transport_; }

private:
    Session& session_;
    engine::Transport& transport_;
    TapTempo tapTempo_;
    bool mmcEnabled_{false};
    bool enabled_{true};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportCommands)
};

} // namespace pedalboard3::session
