#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace pedalboard3::ui
{

// The tempo popover (optional-features plan U4, KD8), opened from the transport
// pill's "120 BPM ▾":
//   - the patch's tempo: -/+ a whole BPM, or drag the number (Shift for fine
//     steps) or double-click it to type
//   - a big Tap button
//   - the time signature as chips, with "Other..." for anything else
// Changes go through Session, so they're saved with the patch. It follows the
// transport at 10 Hz, so a tap or a mapped change shows straight away.
//
// Message thread only. Shown in a CallOutBox, which owns it.
class TempoPopover : public juce::Component, private juce::Timer
{
public:
    explicit TempoPopover(AppContext& ctx);
    ~TempoPopover() override;

    // "Other...": the popover closes and the transport asks for a signature.
    std::function<void()> onOtherSignature;

    void resized() override;

private:
    class TempoField;

    void timerCallback() override { refresh(); }
    void refresh();
    void nudge(double delta);

    AppContext& ctx_;
    juce::TextButton minus_{"-"};
    juce::TextButton plus_{"+"};
    std::unique_ptr<TempoField> tempo_;
    juce::Label bpmLabel_;
    juce::TextButton tap_{"TAP"};
    std::vector<std::unique_ptr<juce::TextButton>> signatures_;
    juce::TextButton other_{"Other..."};
    juce::Label note_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoPopover)
};

} // namespace pedalboard3::ui
