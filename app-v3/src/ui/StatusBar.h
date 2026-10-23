#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace pedalboard3::ui
{

// StatusBar — fixed-height (32px) strip across the bottom of
// MainWindow. Listens to Session and renders:
//
//   left:   ● (state dot, coloured per Session::State) +
//           "Chain: N plugin(s)" + state text
//   centre: the three panel toggles (#90) — patches on the left, mappings
//           along the bottom, the plugin browser on the right, each lit while
//           its panel is showing. The UI demo puts them here; the app had them
//           only on the panel edges and in the Plugins menu, which are both
//           places you have to know about already.
//   right:  the lastError pill, when there is one
//
// The audio-device readout moved to the top bar's DeviceBadge (#90); the
// version is in the About window. Neither was ever read down here.
//
// Listener-driven (per AC-UI-K and the deferred-implementation
// note about Timer-poll alternative). If Idle/Building flickers
// during a fast addPlugin burst, switch to a 5 Hz Timer poll;
// not yet observed.
//
// AC-UI-O: surfaces Session::getLastError() after every state→Idle
// transition. The error text persists in the pill until the next
// successful op (which clears lastError_ via Session.cpp's
// commitLoadPatch / commitAddPlugin success paths).
class StatusBar : public juce::Component, private juce::ChangeListener
{
public:
    explicit StatusBar(AppContext& ctx);
    ~StatusBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // The three panel toggles. RootComponent owns the panels, so it installs
    // both halves of each pair; the same lambdas already drive the Plugins
    // menu's items and the panels' own edge toggles.
    std::function<void()> onTogglePatchBank;
    std::function<bool()> isPatchBankShown;
    std::function<void()> onToggleMappingsPanel;
    std::function<bool()> isMappingsPanelShown;
    std::function<void()> onTogglePluginBrowser;
    std::function<bool()> isPluginBrowserShown;

    // Whether the Test Tone is playing (#111). TransportBar owns the switch,
    // so RootComponent installs this the way it installs the panel toggles.
    // The tone is a master stage, so the board behind it is untouched — but
    // what you are hearing still is not your board, and the bar says so.
    std::function<bool()> isTestToneOn;

    // The tone went on or off; re-read it.
    void refreshTestTone() { refresh(); }

    // Re-read which panels are showing and re-light the toggles. RootComponent
    // calls this from its resized(), which every path that opens or closes a
    // panel ends in — including a window resize that crosses the fold width
    // and turns an open panel into an overlay.
    void refreshPanelToggles();

private:
    class PanelToggle; // a small icon button; defined in StatusBar.cpp

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh();

    AppContext& ctx_;
    juce::Label leftLabel_;
    juce::Label errorLabel_;
    juce::Colour stateDotColour_;

    std::unique_ptr<PanelToggle> patchesToggle_;
    std::unique_ptr<PanelToggle> mappingsToggle_;
    std::unique_ptr<PanelToggle> browserToggle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};

} // namespace pedalboard3::ui
