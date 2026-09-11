#include "BankDocument.h"

#include "Session.h"

namespace pedalboard3::session
{

BankDocument::BankDocument(Session& session) : session_(session) {}

void BankDocument::loadBank(PatchBank bank)
{
    if (bank.patches.empty())
        bank = PatchBank{};
    if (!bank.isValidIndex(bank.activeIndex))
        bank.activeIndex = 0;

    bank_ = std::move(bank);
    issueReplace();
    sendChangeMessage();
}

void BankDocument::reloadActive()
{
    issueReplace();
    sendChangeMessage();
}

bool BankDocument::select(int index)
{
    if (!bank_.isValidIndex(index) || index == bank_.activeIndex)
        return false;
    swapTo(index);
    return true;
}

void BankDocument::next()
{
    if (bank_.size() > 1)
        swapTo((bank_.activeIndex + 1) % bank_.size());
}

void BankDocument::previous()
{
    if (bank_.size() > 1)
        swapTo((bank_.activeIndex + bank_.size() - 1) % bank_.size());
}

int BankDocument::addPatch(Patch patch)
{
    const int idx = bank_.add(std::move(patch));
    sendChangeMessage();
    return idx;
}

int BankDocument::duplicate(int index)
{
    if (!bank_.isValidIndex(index))
        return -1;
    if (index == bank_.activeIndex)
        storeActive(); // the copy should carry the live knob state, not a stale slot
    const int idx = bank_.duplicate(index);
    sendChangeMessage();
    return idx;
}

bool BankDocument::rename(int index, juce::String name)
{
    if (!bank_.rename(index, std::move(name)))
        return false;
    sendChangeMessage();
    return true;
}

bool BankDocument::remove(int index)
{
    if (!bank_.isValidIndex(index))
        return false;

    const bool removingActive = index == bank_.activeIndex;
    bank_.remove(index);
    if (removingActive)
        issueReplace(); // the bank chose the new active patch; make it live
    sendChangeMessage();
    return true;
}

void BankDocument::storeActive()
{
    if (!liveCaptureEnabled_)
        return;
    auto captured = session_.getActivePatchWithState();
    captured.name = bank_.active().name; // the bank owns patch names
    bank_.active() = std::move(captured);
}

PatchBank BankDocument::snapshotForSave()
{
    storeActive();
    return bank_;
}

void BankDocument::swapTo(int newIndex)
{
    storeActive();
    bank_.activeIndex = newIndex;
    issueReplace();
    sendChangeMessage();
}

void BankDocument::issueReplace()
{
    if (beforeSwap)
        beforeSwap();
    session_.replacePatch(bank_.active());
    liveCaptureEnabled_ = true; // after this swap the live graph IS the active patch
}

} // namespace pedalboard3::session
