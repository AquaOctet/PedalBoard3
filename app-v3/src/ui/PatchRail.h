#pragma once

#include "AppContext.h"
#include "ColourScheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace pedalboard3::ui
{

// The patch bank collapsed to a 48 px icon strip (UI refresh U6, plan KD3): the
// three-bar toggle, top-left under File, that opens the bank (docked on a wide
// window, slid over the canvas on a narrow one), and the previous / playing /
// next patch by Program Change number, one click away.
//
// Threading: message thread only. Listens to BankDocument.
class PatchRail : public juce::Component, private juce::ChangeListener
{
public:
    explicit PatchRail(AppContext& ctx);
    ~PatchRail() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // The bank icon or the playing patch: slide the bank panel open or shut.
    std::function<void()> onOpenPanel;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh();

    AppContext& ctx_;
    juce::ShapeButton panelButton_{"Patches", tokens::fgMuted, tokens::fgPrimary, tokens::accentPrimary};
    juce::TextButton previous_;
    juce::TextButton current_;
    juce::TextButton next_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchRail)
};

} // namespace pedalboard3::ui
