#pragma once

#include "AppContext.h"
#include "session/Features.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace pedalboard3::ui
{

// The open board's feature switches as a strip of chips (optional-features
// plan U3, KD4), in the Mappings panel's bottom-right corner. There's one chip
// per feature that's on in Settings, lit while it's on for this board; a click
// switches it for the board (a board edit: the board shows unsaved). With no
// feature on in Settings the strip is empty and takes no room.
//
// Message thread only. Listens to FeatureGate.
class FeatureChips : public juce::Component, private juce::ChangeListener
{
public:
    explicit FeatureChips(AppContext& ctx);
    ~FeatureChips() override;

    // The width the chips need, 0 when there are none.
    int preferredWidth() const noexcept;

    // The set of chips changed, so the owner should lay out again.
    std::function<void()> onLayoutChanged;

    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();

    AppContext& ctx_;
    std::vector<session::Feature> shown_;
    std::vector<std::unique_ptr<juce::TextButton>> chips_;
    std::vector<int> widths_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeatureChips)
};

} // namespace pedalboard3::ui
