#include "PatchSwitcher.h"

#include "AppCommands.h"
#include "BankDocument.h"
#include "ControlHub.h"

#include <utility>

namespace pedalboard3::session
{

PatchSwitcher::PatchSwitcher(BankDocument& bank, ControlHub& hub) : bank_(bank), hub_(hub)
{
    hub_.setUnhandledTargetSink([this](const control::ParameterUpdate& update) { onUnhandledTarget(update); });
}

PatchSwitcher::~PatchSwitcher()
{
    hub_.setUnhandledTargetSink(nullptr);
}

void PatchSwitcher::handleMidi(const juce::MidiMessage& msg)
{
    // Read before the hub call: a captured message disarms learn.
    const bool learnWasArmed = hub_.isLearning();

    mappingOwnedThisMessage_ = false;
    hub_.handleMidi(msg);
    const bool mappingOwned = std::exchange(mappingOwnedThisMessage_, false);

    if (learnWasArmed || mappingOwned || !msg.isProgramChange())
        return;

    bank_.select(msg.getProgramChangeNumber()); // no-op when out of range or already active
}

void PatchSwitcher::onUnhandledTarget(const control::ParameterUpdate& update)
{
    switch (update.target.kind)
    {
    case control::MidiTargetKind::PatchSelect:
        mappingOwnedThisMessage_ = true;
        bank_.select(update.target.patchIndex);
        break;
    case control::MidiTargetKind::AppCommand:
        mappingOwnedThisMessage_ = true;
        if (update.press)
            invokeAppCommand(bank_, update.target.commandId); // unknown ids are ignored
        break;
    case control::MidiTargetKind::PluginParameter:
        break; // applied by ControlHub; never reaches this sink
    }
}

} // namespace pedalboard3::session
