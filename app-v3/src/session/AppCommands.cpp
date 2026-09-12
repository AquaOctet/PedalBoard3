#include "AppCommands.h"

#include "BankDocument.h"
#include "EditHistory.h"
#include "TransportCommands.h"

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

bool invokeAppCommand(BankDocument& bank, const juce::String& id, EditHistory* history, TransportCommands* transport)
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
    if (id.startsWith("transport."))
    {
        if (transport == nullptr)
            return false;
        if (id == appcommands::kPlayPause)
            transport->playPause();
        else if (id == appcommands::kStop)
            transport->stop();
        else if (id == appcommands::kReturnToZero)
            transport->returnToZero();
        else if (id == appcommands::kTapTempo)
            transport->tap();
        else
            return false;
        return true;
    }
    return false;
}

} // namespace pedalboard3::session
