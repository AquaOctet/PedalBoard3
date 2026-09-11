#pragma once

#include "control/MidiMapping.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace pedalboard3::session
{

class BankDocument;
class ControlHub;

// Turns patch-switch and app-command intents into bank actions (roadmap M3
// U4, plan docs/plans/2026-09-11-001-feat-multi-patch-banks-plan.md, KD3 /
// KTD3; app commands: M4 U1, docs/plans/2026-09-11-002-feat-mappings-ui-plan.md):
//
//   - A PatchSelect mapping, MIDI or OSC, selects its patchIndex.
//   - An AppCommand mapping runs its command (session/AppCommands.h) on a
//     press only (control::ParameterUpdate::press), so a button's release
//     does not run it a second time.
//   Both arrive through ControlHub's unhandled-target sink, which this object
//   installs.
//   - The default rule: a MIDI Program Change n, on any channel, selects bank
//     index n when it is in range. It is skipped when a PatchSelect or
//     AppCommand mapping matched the same message (the mapping owns it, even
//     when its index or command is invalid) and when MIDI learn was armed as
//     the message arrived (the learn flow owns it; learning a Program Change
//     must not also jump patches).
//
// handleMidi() is the MIDI drain sink. It forwards the message to
// ControlHub::handleMidi first and applies the default rule afterwards, so a
// mapping always gets first say. Every switch goes through BankDocument
// (capture, then swap); selecting the active or an out-of-range index is a
// no-op, which makes a PatchSelect button's press/release pair harmless.
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
    void onUnhandledTarget(const control::ParameterUpdate& update);

    BankDocument& bank_;
    ControlHub& hub_;
    bool mappingOwnedThisMessage_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchSwitcher)
};

} // namespace pedalboard3::session
