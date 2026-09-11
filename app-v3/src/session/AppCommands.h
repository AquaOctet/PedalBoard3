#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace pedalboard3::session
{

class BankDocument;
class EditHistory;

// App-level commands a key, MIDI or OSC trigger can run (roadmap M4, plan
// docs/plans/2026-09-11-002-feat-mappings-ui-plan.md, KD2 / KTD2). One table
// serves every trigger: MIDI/OSC mappings name a command by `id` (persisted as
// control::MidiTarget::commandId), the UI's juce::ApplicationCommandManager by
// `juceCommandId`. Adding a command is one table row plus one branch in
// invokeAppCommand.
struct AppCommandInfo
{
    const char* id;    // stable and persisted, e.g. "patch.next"
    const char* name;  // shown in menus and the App Mappings window
    int juceCommandId; // juce::CommandID; non-zero and stable
};

namespace appcommands
{
inline constexpr const char* kNextPatch = "patch.next";
inline constexpr const char* kPreviousPatch = "patch.previous";
inline constexpr const char* kSave = "file.save";
inline constexpr const char* kUndo = "edit.undo";
inline constexpr const char* kRedo = "edit.redo";
} // namespace appcommands

// Every command, in display order.
inline constexpr std::array<AppCommandInfo, 5> kAppCommands{{
    {appcommands::kNextPatch, "Next Patch", 0x2001},
    {appcommands::kPreviousPatch, "Previous Patch", 0x2002},
    {appcommands::kSave, "Save", 0x2003},
    {appcommands::kUndo, "Undo", 0x2004},
    {appcommands::kRedo, "Redo", 0x2005},
}};

// Lookups; nullptr when unknown.
const AppCommandInfo* findAppCommand(const juce::String& id) noexcept;
const AppCommandInfo* findAppCommandByJuceId(int juceCommandId) noexcept;

// Run a command against the bank (message thread). Returns false, doing
// nothing, for an unknown id. file.save hands off to
// BankDocument::onSaveRequested and is unhandled (false) until the UI
// installs one.
// edit.undo / edit.redo run on `history`; unhandled without one, or while
// Test Tone has replaced the live graph.
bool invokeAppCommand(BankDocument& bank, const juce::String& id, EditHistory* history = nullptr);

} // namespace pedalboard3::session
