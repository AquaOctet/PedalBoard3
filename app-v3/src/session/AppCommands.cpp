#include "AppCommands.h"

#include "BankDocument.h"

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

bool invokeAppCommand(BankDocument& bank, const juce::String& id)
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
    return false;
}

} // namespace pedalboard3::session
