#pragma once

#include "ActivityLights.h"
#include "AppContext.h"
#include "CpuMeter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::ui
{

class FileActions;
class SignalCanvas;

// TransportBar — fixed-height (48px) strip across the top of the
// MainWindow:
//
//   left:   [File] (New, Open, Save, Save As, Save as Default, Reset
//           Default, App Mappings; the file actions are ui::FileActions),
//           [Plugins] (Add Plugin, Plugin List..., Rescan) and [+], which
//           opens the Add Plugin menu directly: KnownPluginList effects and
//           built-in nodes, added through EditHistory (AC-UI-N; UI refresh
//           U1, #32).
//   centre: the patch strip (roadmap M3 U5): [<] [n/N  Name] [>]. The
//           arrows step the bank; the name button opens the bank menu
//           (new, duplicate, rename, remove, go to a patch). Every switch
//           goes through BankDocument, which closes open editors first via
//           beforeSwap (D1b / AC-UI-P).
//   right:  the MIDI / OSC activity lights and the CPU meter (UI refresh
//           U3 / U2), then Settings (audio device, MIDI inputs and the OSC
//           listener; U10) and Test Tone (diagnostic loopback bypassing the
//           chain) as outline buttons.
//
// The audio-device readout lives in the StatusBar.
//
// Threading: message thread only. Listens to Session (Add Plugin
// enablement) and BankDocument (patch strip, Test Tone toggle sync).
class TransportBar : public juce::Component, private juce::ChangeListener
{
public:
    TransportBar(AppContext& ctx, SignalCanvas& canvas, FileActions& files);
    ~TransportBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // File > App Mappings... (M4 U5). MainWindow owns the window and the key
    // mappings it edits, so it installs this.
    std::function<void()> onOpenAppMappings;

    // Rename / remove the playing patch through the bar's prompts; the bank
    // sidebar (UI refresh U4) uses them too.
    void promptRenamePatch();
    void confirmRemovePatch();

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void openAddPluginMenu();
    // The Add Plugin menu, shared by [+] and Plugins > Add Plugin; each item
    // carries its own action.
    juce::PopupMenu makeAddPluginMenu();
    bool canAddPlugin() const; // Session Idle and Test Tone off
    void openFileMenu();
    void openPluginList();
    void rescanPlugins();
    void onTestToneToggled();
    void updateAddPluginEnabled();
    void openPluginsMenu();

    // Patch strip (M3 U5).
    void refreshPatchStrip();
    void openPatchMenu();

    AppContext& ctx_;
    SignalCanvas& canvas_;
    FileActions& files_;

    juce::TextButton fileButton_{"File"};
    juce::TextButton addPluginButton_{"+"};
    juce::TextButton settingsButton_{"Settings"};
    juce::TextButton toneButton_{"Test Tone"}; // toggles
    juce::TextButton pluginsButton_{"Plugins"};
    ActivityLights activityLights_;
    CpuMeter cpuMeter_;

    juce::TextButton prevPatchButton_{"<"};
    juce::TextButton patchButton_;
    juce::TextButton nextPatchButton_{">"};

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
