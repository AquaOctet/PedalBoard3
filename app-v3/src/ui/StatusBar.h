#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

// StatusBar — fixed-height (32px) strip across the bottom of
// MainWindow. Listens to Session and the AudioDeviceManager and renders:
//
//   left:   ● (state dot, coloured per Session::State) +
//           "Chain: N plugin(s)" + state text
//   middle: lastError pill (when set) and the audio-device readout
//           (device, sample rate, buffer; or why there is no device),
//           refreshed on every AudioDeviceManager change, at the right
// The version is in the About window, not here.
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

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh();
    void refreshDeviceInfo();

    AppContext& ctx_;
    juce::Label leftLabel_;
    juce::Label errorLabel_;
    juce::Label deviceLabel_;
    juce::Colour stateDotColour_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};

} // namespace pedalboard3::ui
