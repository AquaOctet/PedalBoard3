#pragma once

#include "control/MidiMapping.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace pedalboard3::session
{

class BankDocument;
class ControlHub;

// Turns patch-switch intents into bank switches (roadmap M3 U4, plan
// docs/plans/2026-09-11-001-feat-multi-patch-banks-plan.md, KD3 / KTD3):
//
//   - A PatchSelect mapping, MIDI or OSC, selects its patchIndex. It arrives
//     through ControlHub's unhandled-target sink, which this object installs.
//   - The default rule: a MIDI Program Change n, on any channel, selects bank
//     index n when it is in range. It is skipped when a PatchSelect mapping
//     matched the same message (the mapping owns it, even when its index is
//     out of range) and when MIDI learn was armed as the message arrived
//     (the learn flow owns it; learning a Program Change must not also jump
//     patches).
//
// handleMidi() is the MIDI drain sink. It forwards the message to
// ControlHub::handleMidi first and applies the default rule afterwards, so a
// mapping always gets first say. Every switch goes through
// BankDocument::select (capture, then swap); selecting the active or an
// out-of-range index is a no-op, which makes a mapped button's press/release
// pair harmless.
//
// Message-thread only. Destroy it after every input edge that can call it
// (MidiInputManager, OscServer) and before the ControlHub and BankDocument it
// references; the destructor uninstalls the hub sink.
class PatchSwitcher
{
public:
    PatchSwitcher(BankDocument& bank, ControlHub& hub);
    ~PatchSwitcher();

    void handleMidi(const juce::MidiMessage& msg);

private:
    void onUnhandledTarget(const control::MidiTarget& target, float value);

    BankDocument& bank_;
    ControlHub& hub_;
    bool mappingSelectedThisMessage_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchSwitcher)
};

} // namespace pedalboard3::session
