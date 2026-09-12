#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

namespace pedalboard3::ui
{

// Asks, when a board opens, about the features it uses that are off in
// Settings (optional-features plan U3, KD5): "This board uses Looper and OSC,
// which are off in Settings. Turn them on?"
//   - Turn On switches them on in Settings.
//   - Not Now leaves them off; the board is untouched and saves as it was.
// Every way a board opens goes through BankDocument::loadBank (Open, Open
// Recent, a drop, New from the default board, the relaunch restore), so this
// asks once per load. A board that switched a feature off for itself isn't
// asked about it.
//
// Message thread only. Owned by the main window.
class FeaturePrompt : private juce::ChangeListener
{
public:
    explicit FeaturePrompt(AppContext& ctx);
    ~FeaturePrompt() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override; // the board changed

    AppContext& ctx_;
    std::uint64_t seenLoads_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(FeaturePrompt)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeaturePrompt)
};

} // namespace pedalboard3::ui
