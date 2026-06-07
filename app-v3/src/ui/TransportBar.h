#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::ui
{

class SignalCanvas;

// TransportBar — fixed-height (48px) strip across the top of the
// MainWindow. Houses the four user-facing entry points the Path γ
// rewrite must preserve:
//
//   • [+ Add Plugin]    -> juce::PopupMenu populated from
//                          KnownPluginList; selection calls
//                          Session::addPlugin (closes AC-UI-N — the
//                          regression risk caught in the deepen pass:
//                          MainWindow rewrite removed RootComponent
//                          which held the only existing addPlugin
//                          call site).
//   • Audio Settings... -> juce::DialogWindow with the standard
//                          AudioDeviceSelectorComponent.
//   • Test Tone (toggle)-> diagnostic loopback bypassing the chain.
//                          Closes all open editors via SignalCanvas
//                          BEFORE clearPatch (D1b / AC-UI-P).
//   • Plugin List...    -> juce::DialogWindow with PluginListComponent
//                          (scan/blacklist/enable per-format).
//
// Centre area shows a device-info pill (driver / sample rate / buffer
// size) read from AppContext::audioEngine.getDeviceManager(); CPU /
// latency placeholders display em-dashes until the Phase 1.B+
// MetricsRegistry lands.
//
// Threading: message thread only. Listens to nothing; AppContext is
// queried each time the Audio Settings dialog closes (via the dialog's
// close callback) to refresh the centre label.
class TransportBar : public juce::Component, private juce::ChangeListener
{
public:
    TransportBar(AppContext& ctx, SignalCanvas& canvas);
    ~TransportBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Re-read the AudioDeviceManager and refresh the centre label.
    // Called by RootComponent when the Audio Settings dialog closes
    // and by the listener on Session state changes (so the label
    // reflects e.g. a new sample rate after device swap).
    void refreshDeviceInfo();

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void openAddPluginMenu();
    void openFileMenu();
    void savePatch();
    void openPatch();
    void openAudioSettings();
    void openPluginList();
    void rescanPlugins();
    void onTestToneToggled();
    void updateAddPluginEnabled();

    AppContext& ctx_;
    SignalCanvas& canvas_;

    juce::TextButton fileButton_{"File"};
    juce::TextButton addPluginButton_{"+ Add Plugin"};
    juce::TextButton settingsButton_{"Audio Settings..."};
    juce::ToggleButton toneButton_{"Test Tone"};
    juce::TextButton rescanButton_{"Rescan Plugins"};
    juce::TextButton pluginListButton_{"Plugin List..."};

    juce::Label titleLabel_;
    juce::Label deviceInfoLabel_;

    // Held as a member so it outlives the async launchAsync callback.
    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace pedalboard3::ui
