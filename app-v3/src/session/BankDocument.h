#pragma once

#include "PatchBank.h"

#include <juce_events/juce_events.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace pedalboard3::session
{

class Session;

// The app's one patch-bank document (roadmap M3, plan
// docs/plans/2026-09-11-001-feat-multi-patch-banks-plan.md, KTD1). Owns the
// PatchBank and is the only object that knows both the bank and the Session:
// switching the active patch means capturing the outgoing patch (with live
// plugin state) into its slot, then handing the incoming one to
// Session::replacePatch — the existing worker-build + atomic swap. Session
// itself stays single-patch and unaware of banks.
//
// Slot semantics: bank().patches[activeIndex()] is a CACHE of the live patch,
// refreshed by storeActive() (every switch, save snapshot, duplicate of the
// active patch) — read Session for the live topology. The other slots are
// authoritative. A slot's `name` is owned by the bank (rename never touches
// Session), so storeActive keeps it.
//
// Live capture can be suspended (setLiveCaptureEnabled(false)) while the live
// graph is NOT the active patch — Test Tone replaces it with a diagnostic
// graph — so a switch or save during that window does not overwrite the slot
// with an empty patch. Any swap this document performs re-enables capture.
//
// Broadcasts a change message after every bank mutation (switch, add,
// duplicate, rename, remove, load). Message-thread only, like Session's API.
class BankDocument : public juce::ChangeBroadcaster, private juce::ChangeListener
{
public:
    explicit BankDocument(Session& session);
    ~BankDocument() override;

    // Called right before every Session::replacePatch this document issues.
    // The UI binds it to closing open plugin editors (caller-managed editor
    // lifecycle: instances die in the swap).
    std::function<void()> beforeSwap;

    const PatchBank& bank() const noexcept { return bank_; }
    int activeIndex() const noexcept { return bank_.activeIndex; }
    int size() const noexcept { return bank_.size(); }
    const Patch& activePatch() const noexcept { return bank_.active(); }

    // Replace the whole document and make its active patch live. Does not
    // capture the outgoing patch (the old document is discarded). `file` is
    // where the bank came from (empty for an untitled or imported board); the
    // loaded content is the clean baseline.
    void loadBank(PatchBank bank, juce::File file = {});

    // Re-issue the active slot to Session without capturing first (Test Tone
    // off: the live graph was the tone, the slot is the truth).
    void reloadActive();

    // Switching (capture-then-swap). select returns false and does nothing for
    // the current index or an invalid one.
    bool select(int index);
    void next();
    void previous();

    // Bank edits. None of these switch the live patch except remove() of the
    // active patch, which makes the bank's new active patch live.
    int addPatch(Patch patch = Patch{});
    int duplicate(int index); // captures first when index is the active patch
    bool rename(int index, juce::String name);
    bool remove(int index);

    // Capture Session's active patch with plugin state into the active slot
    // (no-op while live capture is disabled).
    void storeActive();

    // storeActive(), then a copy of the bank — what Save writes.
    PatchBank snapshotForSave();

    void setLiveCaptureEnabled(bool enabled) noexcept { liveCaptureEnabled_ = enabled; }
    bool isLiveCaptureEnabled() const noexcept { return liveCaptureEnabled_; }

    // True when Session's live patch IS the active slot's patch: live capture
    // is enabled (not Test Tone) and the last swap this document issued has
    // committed. False while that swap is still building, and after it failed
    // — Session then still holds the PREVIOUS patch, so capturing "the live
    // patch" would overwrite the active slot with another slot's content (a
    // fast Program Change sweep, or a patch whose plugin won't load here).
    // storeActive and PreferencesPersister both gate on this.
    bool liveMatchesActiveSlot() const noexcept;

    // ---- File identity and unsaved changes (finish pass #26 / #27, plan
    // docs/plans/2026-09-11-003-feat-finish-pass-core-plan.md, KD1 / KD7).
    //
    // The file the bank was loaded from or last saved to (empty = untitled),
    // and whether the content differs from it: bank structure and patch names,
    // each patch's nodes, bypass, custom names, MIDI channel, connections and
    // mappings. Not the active patch index or card positions. Plugin state is
    // not compared; a hand edit to a live plugin parameter
    // (Session::parameterEdits) marks the bank modified until the next save or
    // load.
    // Re-evaluated after every bank edit and every Session change that
    // settles with the live patch being the active slot; a change in the
    // result broadcasts.
    const juce::File& currentFile() const noexcept { return currentFile_; }
    bool isModified() const noexcept { return modified_; }

    // The current content becomes the clean baseline and `file` the current
    // file (Save / Save As).
    void markSaved(const juce::File& file);

    // After a relaunch: the restored bank belongs to `file` and was unsaved
    // (`modified`) when the app last ran. A modified restore stays modified
    // until the next save or load.
    void restoreIdentity(const juce::File& file, bool modified);

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override; // Session settled
    void refreshModified();
    std::int64_t contentSignature() const;

    void swapTo(int newIndex); // storeActive (if enabled), set index, replacePatch
    void issueReplace();       // beforeSwap + replacePatch(active slot) + re-enable capture

    Session& session_;
    PatchBank bank_;
    bool liveCaptureEnabled_{true};
    // Session::getPatchGeneration() when the last swap was issued; a commit
    // bumps it past this value, a failed build leaves it. Empty until the
    // first swap (the live patch then is whatever Session started with, which
    // the default one-patch bank mirrors).
    std::optional<std::uint64_t> issuedAtGeneration_;

    juce::File currentFile_;
    std::int64_t savedSignature_{0};
    bool modified_{false};
    bool restoredModified_{false};
    bool stateEdited_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BankDocument)
};

} // namespace pedalboard3::session
