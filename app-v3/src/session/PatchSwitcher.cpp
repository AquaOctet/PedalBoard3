#include "PatchSwitcher.h"

#include "BankDocument.h"
#include "ControlHub.h"

#include <utility>

namespace pedalboard3::session
{

PatchSwitcher::PatchSwitcher(BankDocument& bank, ControlHub& hub) : bank_(bank), hub_(hub)
{
    hub_.setUnhandledTargetSink([this](const control::MidiTarget& target, float value)
                                { onUnhandledTarget(target, value); });
}

PatchSwitcher::~PatchSwitcher()
{
    hub_.setUnhandledTargetSink(nullptr);
}

void PatchSwitcher::handleMidi(const juce::MidiMessage& msg)
{
    // Read before the hub call: a captured message disarms learn.
    const bool learnWasArmed = hub_.isLearning();

    mappingSelectedThisMessage_ = false;
    hub_.handleMidi(msg);
    const bool mappingSelected = std::exchange(mappingSelectedThisMessage_, false);

    if (learnWasArmed || mappingSelected || !msg.isProgramChange())
        return;

    bank_.select(msg.getProgramChangeNumber()); // no-op when out of range or already active
}

void PatchSwitcher::onUnhandledTarget(const control::MidiTarget& target, float /*value*/)
{
    // AppCommand targets reach the same sink; dispatching them is M4 work.
    if (target.kind != control::MidiTargetKind::PatchSelect)
        return;

    mappingSelectedThisMessage_ = true;
    bank_.select(target.patchIndex);
}

} // namespace pedalboard3::session
