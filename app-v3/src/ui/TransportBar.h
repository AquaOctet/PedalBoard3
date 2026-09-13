#pragma once

#include "ActivityLights.h"
#include "AppContext.h"
#include "CpuMeter.h"
#include "TransportControls.h"

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
//           Default, Settings, Patch Bank, App Mappings, About; the file
//           actions are ui::FileActions), [Settings] (Audio Devices..., MIDI
//           Settings..., OSC Settings... and the Test Tone switch; UI refresh
//           U10), [Plugins] (Add Plugin, Plugin List..., Rescan) and [+],
//           which opens the Add Plugin menu directly: KnownPluginList effects
//           and built-in nodes, added through EditHistory (AC-UI-N; U1, #32).
//   centre: the patch strip (roadmap M3 U5): [<] [n/N  Name] [>]. The
//           arrows step the bank; the name button opens the bank menu
//           (new, duplicate, rename, remove, go to a patch). Every switch
//           goes through BankDocument, which closes open editors first via
//           beforeSwap (D1b / AC-UI-P).
//   then:   the transport pill (M6 U4, optional-features U4), only while
//           Tempo / BPM is available: Play / Pause, Return to Zero, the tempo
//           (its popover has Tap and the time signature), the position and
//           the Metronome's click button. On a narrow window the position
//           folds away, then the click button.
//   right:  the MIDI / OSC activity lights and the CPU meter (UI refresh
//           U3 / U2).
//
// The audio-device readout lives in the StatusBar.
//
// Threading: message thread only. Listens to Session (Add Plugin
// enablement) and BankDocument (patch strip, Test Tone toggle sync).
class TransportBar : public juce::Component, private juce::ChangeListener, private juce::Value::Listener
{
public:
    TransportBar(AppContext& ctx, SignalCanvas& canvas, FileActions& files);
    ~TransportBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // File > App Mappings... (M4 U5). MainWindow owns the window and the key
    // mappings it edits, so it installs this.
    std::function<void()> onOpenAppMappings;

    // File > Patch Bank (UI refresh U6): MainWindow's root component installs
    // both; the item is ticked while the bank panel is showing.
    std::function<void()> onTogglePatchBank;
    std::function<bool()> isPatchBankShown;

    // Plugins > Plugin Browser (UI refresh U5): the same pattern.
    std::function<void()> onTogglePluginBrowser;
    std::function<bool()> isPluginBrowserShown;

    // Plugins > Mappings Panel (UI refresh U9): the same pattern.
    std::function<void()> onToggleMappingsPanel;
    std::function<bool()> isMappingsPanelShown;

    // Rename / remove the playing patch through the bar's prompts; the bank
    // sidebar (UI refresh U4) uses them too.
    void promptRenamePatch();
    void confirmRemovePatch();

    // Plugins > Rescan Plugins: the out-of-process scan and its summary. The
    // prompt after a settings reset runs it too (M5 U7).
    void rescanPlugins();

    // The File, Settings and Plugins menus, shared by the bar's buttons and the
    // macOS menu bar (#25). With `menuBarCommands` the File menu is the menu
    // bar's: Save runs through the app command, so it shows its shortcut, and
    // Settings, Patch Bank and About are left to the menu bar's other menus.
    juce::PopupMenu makeFileMenu(juce::ApplicationCommandManager* menuBarCommands = nullptr);
    juce::PopupMenu makeSettingsMenu();
    juce::PopupMenu makePluginsMenu();
    // The patch strip's menu: new, duplicate, rename, remove, go to a patch,
    // and the board's feature switches.
    juce::PopupMenu makePatchMenu();

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void openAddPluginMenu();
    // The Add Plugin menu, shared by [+] and Plugins > Add Plugin; each item
    // carries its own action.
    juce::PopupMenu makeAddPluginMenu();
    bool canAddPlugin() const; // Session Idle and Test Tone off
    void openFileMenu();
    void openPluginList();
    // The Test Tone switch changed (the Settings menu or the Audio Devices
    // dialog): start or stop the tone.
    void valueChanged(juce::Value&) override;
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
    juce::TextButton pluginsButton_{"Plugins"};
    ActivityLights activityLights_;
    CpuMeter cpuMeter_;
    TransportControls transportControls_;

    // The Test Tone switch: the Settings menu's tick and the Audio Devices
    // dialog's toggle share it; valueChanged() starts or stops the tone.
    juce::Value testTone_{false};
    bool toneActive_{false}; // what is actually playing

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
