#pragma once

#include "PatchBank.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace pedalboard3::session
{

class Preferences;

// The user's default bank and the quit-without-saving revert (finish pass KD2,
// KD3 and KD7; plan docs/plans/2026-09-11-003-feat-finish-pass-core-plan.md).
//
// The default bank is a .pdl named default.pdl beside the settings file, the
// way Pedalboard2 kept its default. File > Save as Default writes it, File >
// Reset Default deletes it, File > New starts from it, and a launch with
// nothing to restore opens it. Every function but defaultBankFile takes the
// file explicitly so tests never touch the user's real default.

// Where the default bank lives; empty when the settings store is unavailable.
juce::File defaultBankFile(Preferences&);

// The default bank in `file`, or nullptr when there is none or it can't be read.
std::unique_ptr<PatchBank> readDefaultBank(const juce::File& file);

// What File > New starts from: the default bank, or one empty patch.
PatchBank newBoardBank(const juce::File& defaultFile);

// Quit with Don't Save (KD7): make the persisted session the last saved
// content, so the next launch doesn't bring back the discarded edits. With a
// readable `savedFile` that is the file's content, marked clean. Otherwise
// (untitled, missing, unreadable) the persisted bank is cleared and the next
// launch opens the default bank.
void revertPersistedBank(Preferences&, const juce::File& savedFile);

} // namespace pedalboard3::session
