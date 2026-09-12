#pragma once

#include "Features.h"

#include <juce_events/juce_events.h>

#include <array>
#include <functional>

namespace pedalboard3::session
{

class BankDocument;

// Whether each optional feature is available right now (plan
// docs/plans/2026-09-12-002-feat-optional-features-plan.md, KD3): switched on in
// its Settings section (for the whole app, kept in Preferences) AND on for the
// open board (kept in the .pdl, on unless the board turns it off).
//
// Everything that shows or runs an optional feature asks isAvailable() and
// listens for this broadcaster. It broadcasts after a Settings change and
// whenever the board's switches change what's available (a board toggle, or
// another board loaded).
//
// Message thread only.
class FeatureGate : public juce::ChangeBroadcaster, private juce::ChangeListener
{
public:
    // Where the Settings switches live. App binds it to Preferences; tests use
    // memory. Unset functions fall back to the defaults and don't save.
    struct GlobalStore
    {
        std::function<bool(Feature)> load;
        std::function<void(Feature, bool)> save;
    };

    FeatureGate(GlobalStore store, BankDocument& board);
    ~FeatureGate() override;

    // Settings: on for the whole app.
    bool isEnabled(Feature feature) const noexcept { return global_[indexOf(feature)]; }
    void setEnabled(Feature feature, bool enabled);

    // The open board's own switch (only meaningful while the feature is enabled).
    bool isOnForBoard(Feature feature) const noexcept;
    void setOnForBoard(Feature feature, bool on);

    // Both.
    bool isAvailable(Feature feature) const noexcept { return isEnabled(feature) && isOnForBoard(feature); }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override; // the board changed
    std::array<bool, kFeatureCount> availability() const noexcept;

    GlobalStore store_;
    BankDocument& board_;
    std::array<bool, kFeatureCount> global_{};
    std::array<bool, kFeatureCount> lastAvailable_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeatureGate)
};

} // namespace pedalboard3::session
