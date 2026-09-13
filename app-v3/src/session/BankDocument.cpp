#include "BankDocument.h"

#include "Session.h"

#include <limits>

namespace pedalboard3::session
{

namespace
{

// A cheap content fingerprint for "unsaved changes": the bank as it would be
// written, minus what KD1 excludes (plugin state, card positions, which patch
// is active).
std::int64_t signatureOf(PatchBank bank)
{
    bank.activeIndex = 0;
    for (auto& patch : bank.patches)
        for (auto& node : patch.nodes)
        {
            node.state.reset();
            node.x = std::numeric_limits<float>::quiet_NaN();
            node.y = std::numeric_limits<float>::quiet_NaN();
        }
    const auto xml = bankToXml(bank);
    return xml != nullptr ? xml->toString(juce::XmlElement::TextFormat().singleLine().withoutHeader()).hashCode64() : 0;
}

} // namespace

BankDocument::BankDocument(Session& session) : session_(session)
{
    savedSignature_ = signatureOf(bank_);
    session_.addChangeListener(this);
    session_.parameterEdits().addChangeListener(this);
}

BankDocument::~BankDocument()
{
    session_.parameterEdits().removeChangeListener(this);
    session_.removeChangeListener(this);
}

void BankDocument::loadBank(PatchBank bank, juce::File file, bool readOnly)
{
    if (bank.patches.empty())
        bank = PatchBank{};
    if (!bank.isValidIndex(bank.activeIndex))
        bank.activeIndex = 0;

    ++loadCount_;
    bank_ = std::move(bank);
    currentFile_ = std::move(file);
    readOnly_ = readOnly;
    savedSignature_ = signatureOf(bank_);
    restoredModified_ = false;
    stateEdited_ = false;
    modified_ = false;
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
    refreshModified();
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
    refreshModified();
    sendChangeMessage();
    return idx;
}

bool BankDocument::rename(int index, juce::String name)
{
    if (!bank_.rename(index, std::move(name)))
        return false;
    refreshModified();
    sendChangeMessage();
    return true;
}

bool BankDocument::setBoardFeature(Feature feature, bool on)
{
    if (bank_.featureOn(feature) == on)
        return false;
    bank_.setFeature(feature, on);
    refreshModified();
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
    refreshModified();
    sendChangeMessage();
    return true;
}

bool BankDocument::liveMatchesActiveSlot() const noexcept
{
    if (!liveCaptureEnabled_)
        return false;
    if (!issuedAtGeneration_.has_value())
        return true;
    return session_.getState() == Session::State::Idle && session_.getPatchGeneration() > *issuedAtGeneration_;
}

void BankDocument::storeActive()
{
    if (!liveMatchesActiveSlot())
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
    issuedAtGeneration_ = session_.getPatchGeneration();
    session_.replacePatch(bank_.active());
    liveCaptureEnabled_ = true; // after this swap the live graph IS the active patch
}

void BankDocument::markSaved(const juce::File& file)
{
    currentFile_ = file;
    readOnly_ = false; // Save As wrote a copy in this version's format
    savedSignature_ = contentSignature();
    restoredModified_ = false;
    stateEdited_ = false;
    modified_ = false;
    sendChangeMessage();
}

void BankDocument::restoreIdentity(const juce::File& file, bool modified, bool readOnly)
{
    currentFile_ = file;
    readOnly_ = readOnly;
    restoredModified_ = modified;
    refreshModified();
    sendChangeMessage();
}

void BankDocument::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // Only a settled Session says anything about the live patch; while a swap
    // is building (or after it failed) the live patch is not the active slot
    // and contentSignature() falls back to the slot itself.
    if (source == &session_.parameterEdits())
    {
        // A hand edit to a live plugin. Plugin state is not in the content
        // signature, so this sticks until the next save or load.
        if (liveMatchesActiveSlot())
        {
            stateEdited_ = true;
            refreshModified();
        }
        return;
    }
    if (source == &session_ && session_.getState() == Session::State::Idle)
        refreshModified();
}

void BankDocument::refreshModified()
{
    const bool modified = restoredModified_ || stateEdited_ || contentSignature() != savedSignature_;
    if (modified == modified_)
        return;
    modified_ = modified;
    sendChangeMessage();
}

std::int64_t BankDocument::contentSignature() const
{
    PatchBank copy = bank_;
    if (liveMatchesActiveSlot())
    {
        auto live = session_.getActivePatch(); // topology only; no plugin-state capture
        live.name = copy.active().name;
        copy.active() = std::move(live);
    }
    return signatureOf(std::move(copy));
}

} // namespace pedalboard3::session
