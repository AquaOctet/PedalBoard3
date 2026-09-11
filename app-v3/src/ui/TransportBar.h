#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::ui
{

class SignalCanvas;

// TransportBar — fixed-height (48px) strip across the top of the
// MainWindow:
//
//   left:   [File] (Open / Save As) and [+ Add Plugin] (KnownPluginList
//           effects + built-in nodes; Session::addPlugin — AC-UI-N).
//   centre: the patch strip (roadmap M3 U5): [<] [n/N  Name] [>]. The
//           arrows step the bank; the name button opens the bank menu
//           (new, duplicate, rename, remove, go to a patch). Every switch
//           goes through BankDocument, which closes open editors first via
//           beforeSwap (D1b / AC-UI-P).
//   right:  OSC port, Audio Settings..., Test Tone (diagnostic loopback
//           bypassing the chain) and [Plugins] (Plugin List..., Rescan).
//
// The audio-device readout lives in the StatusBar.
//
// Threading: message thread only. Listens to Session (Add Plugin
// enablement) and BankDocument (patch strip, Test Tone toggle sync).
class TransportBar : public juce::Component, private juce::ChangeListener
{
public:
    TransportBar(AppContext& ctx, SignalCanvas& canvas);
    ~TransportBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;

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
    // OSC port control (M2 U5): persists the new port and rebinds the live
    // listener; a bind failure (e.g. port already in use) surfaces inline via
    // the field's colour/tooltip rather than a blocking dialog.
    void applyOscPortChange();
    void openPluginsMenu();

    // Patch strip (M3 U5).
    void refreshPatchStrip();
    void openPatchMenu();
    void promptRenamePatch();
    void confirmRemovePatch();

    AppContext& ctx_;
    SignalCanvas& canvas_;

    juce::TextButton fileButton_{"File"};
    juce::TextButton addPluginButton_{"+ Add Plugin"};
    juce::Label oscPortLabel_;
    juce::TextEditor oscPortEditor_;
    juce::TextButton settingsButton_{"Audio Settings..."};
    juce::ToggleButton toneButton_{"Test Tone"};
    juce::TextButton pluginsButton_{"Plugins"};

    juce::TextButton prevPatchButton_{"<"};
    juce::TextButton patchButton_;
    juce::TextButton nextPatchButton_{">"};

    // Held as a member so it outlives the async launchAsync callback.
    std::unique_ptr<juce::FileChooser> fileChooser_;

    // The rename / remove-confirmation dialog, owned here (never
    // auto-deleted) so its async callback can read the text field; the
    // callback guards on a SafePointer to this bar.
    std::unique_ptr<juce::AlertWindow> dialog_;

    // The rescan worker is OWNED here, not fire-and-forget (crash-hunt #8):
    // the old `new ...; launchThread()` self-delete pattern let a quit
    // mid-scan destroy AppContext/Session/PluginHost out from under the
    // running thread. ~TransportBar signals + joins it; threadComplete
    // hands the object back via the onDone callback. Base-class pointer —
    // the concrete type lives in TransportBar.cpp's anonymous namespace.
    std::unique_ptr<juce::ThreadWithProgressWindow> rescanThread_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace pedalboard3::ui
