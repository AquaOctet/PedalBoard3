#include "DefaultBank.h"

#include "PdlSerializer.h"
#include "Preferences.h"

namespace pedalboard3::session
{

juce::File defaultBankFile(Preferences& prefs)
{
    if (auto* store = prefs.getStore())
        return store->getFile().getParentDirectory().getChildFile("default.pdl");
    return {};
}

std::unique_ptr<PatchBank> readDefaultBank(const juce::File& file)
{
    if (file == juce::File() || !file.existsAsFile())
        return nullptr;
    auto loaded = pdl::readBank(file);
    if (loaded.bank == nullptr)
        juce::Logger::writeToLog("Pedalboard 3: couldn't read the default bank - " + loaded.error);
    return std::move(loaded.bank);
}

PatchBank newBoardBank(const juce::File& defaultFile)
{
    if (auto bank = readDefaultBank(defaultFile))
        return std::move(*bank);
    return {};
}

void revertPersistedBank(Preferences& prefs, const juce::File& savedFile)
{
    if (savedFile != juce::File() && savedFile.existsAsFile())
    {
        auto loaded = pdl::readBank(savedFile);
        if (loaded.bank != nullptr)
        {
            prefs.setActiveBank(*loaded.bank);
            prefs.setActiveBankIdentity(savedFile, false, loaded.readOnly);
            return;
        }
    }
    prefs.clearActiveBank();
}

} // namespace pedalboard3::session
