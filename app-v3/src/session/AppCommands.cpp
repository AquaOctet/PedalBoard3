#include "AppCommands.h"

#include "BankDocument.h"
#include "EditHistory.h"

namespace pedalboard3::session
{

const AppCommandInfo* findAppCommand(const juce::String& id) noexcept
{
    for (const auto& c : kAppCommands)
        if (id == c.id)
            return &c;
    return nullptr;
}

const AppCommandInfo* findAppCommandByJuceId(int juceCommandId) noexcept
{
    for (const auto& c : kAppCommands)
        if (c.juceCommandId == juceCommandId)
            return &c;
    return nullptr;
}

bool invokeAppCommand(BankDocument& bank, const juce::String& id, EditHistory* history)
{
    if (id == appcommands::kNextPatch)
    {
        bank.next();
        return true;
    }
    if (id == appcommands::kPreviousPatch)
    {
        bank.previous();
        return true;
    }
    if (id == appcommands::kSave)
    {
        if (!bank.onSaveRequested)
            return false;
        bank.onSaveRequested();
        return true;
    }
    if (id == appcommands::kUndo || id == appcommands::kRedo)
    {
        // Test Tone replaces the chain; the history's nodes aren't live then.
        if (history == nullptr || !bank.isLiveCaptureEnabled())
            return false;
        return id == appcommands::kUndo ? history->undo() : history->redo();
    }
    return false;
}

} // namespace pedalboard3::session
