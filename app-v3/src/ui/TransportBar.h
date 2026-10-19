#pragma once

#include "ActivityLights.h"
#include "AppContext.h"
#include "CpuMeter.h"
#include "DeviceBadge.h"
#include "IconButton.h"
#include "InputMonitor.h"
#include "LatencyReadout.h"
#include "PatchStrip.h"
#include "TransportControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace pedalboard3::ui
{

class FileActions;
class SignalCanvas;

// TransportBar — fixed-height (48px) strip across the top of the
// MainWindow:
//
//   left:   a folder, a gear and a plug, each 36 px square, each opening the
//           menu it used to spell out (#90: the words cost 178 px that the
//           device badge and the patch name both wanted). [Folder] is New,
//           Open, Save, Save As, Save as Default, Reset Default, Settings,
//           Patch Bank, App Mappings, About; the file actions are
//           ui::FileActions. [Gear] is Audio Devices..., MIDI Settings...,
//           OSC Settings... and the Test Tone switch (UI refresh U10).
//           [Plug] is Add Plugin, Plugin List..., Rescan. Then [+], which
//           opens the Add Plugin menu directly: KnownPluginList effects and
//           built-in nodes, added through EditHistory (AC-UI-N; U1, #32).
//   centre: the patch strip (roadmap M3 U5; one control since #109) —
//           `‹ | 2/6 Verse Clean ▾ | ›`. The chevrons step the bank; the
//           name opens the bank menu (new, duplicate, rename, remove, go to
//           a patch) and carries a caret so you can tell. Every switch goes
//           through BankDocument, which closes open editors first via
//           beforeSwap (D1b / AC-UI-P).
//   then:   the transport pill (M6 U4, optional-features U4), only while
//           Tempo / BPM is available: Play / Pause, Return to Zero, the tempo
//           (its popover has Tap and the time signature), the position and
//           the Metronome's click button. On a narrow window the position
//           folds away, then the click button.
//   right:  the MIDI / OSC activity lights, the audio input monitor, the
//           round-trip latency, the CPU meter and the master mute (UI refresh
//           U3 / U2; latency is #90, mute is #106, the input monitor is
//           #117). The latency folds away first on a narrow window: the
//           lights and the meters move second by second and it only moves
//           when the device does. The mute never folds away — it is the
//           control you reach for when something is already wrong.
//
// Nothing folds while there is slack between the patch strip and the right
// cluster (#116): the bar prices itself against the window and walks a fold
// ladder only as far as the shortfall needs.
//
// Between [+] and the patch strip sits the audio-device badge (#90): the
// driver and rate as a chip, then the input and output devices with a mic and
// a speaker icon, and a click opens Settings > Audio Devices. It used to be
// text in the status bar's right-hand corner. It folds to the chip alone, and
// then away entirely, as the window narrows.
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

    // What the bar needs with nothing folded away and the patch strip at its
    // floor (#116). MainWindow makes this the window's minimum width, so the
    // bar's slack is spent before anything is taken off it, and the point
    // where the slack runs out is as narrow as the window goes.
    int naturalWidth() const;

    // naturalWidth() changed -- a device with a longer name, a feature
    // switched on. MainWindow re-applies its resize limits.
    std::function<void()> onNaturalWidthChanged;

    // What the fold ladder actually did, for the snapshot tool. The bug
    // this replaced was "it collapses things while there is white space
    // sitting there", so the evidence has to be a measurement of the space
    // and of what is still showing, not a picture at one width.
    // The unclaimed run between the patch strip and whatever is right of
    // it: the "white space" the fold ladder is required to spend first.
    int barSlack() const noexcept { return slack_; }
    int patchStripSlack() const { return patchStrip_.getWidth() - PatchStrip::kMinimumWidth; }
    int deviceBadgeWidth() const { return deviceBadge_.isVisible() ? deviceBadge_.getWidth() : 0; }
    const juce::Component& latencyReadoutForTest() const { return latencyReadout_; }
    const juce::Component& cpuMeterForTest() const { return cpuMeter_; }
    const juce::Component& inputMonitorForTest() const { return inputMonitor_; }
    const juce::Component& activityLightsForTest() const { return activityLights_; }

    // Settings > App Mappings... (M4 U5). MainWindow owns the window and the key
    // mappings it edits, so it installs this.
    std::function<void()> onOpenAppMappings;

    // Show or hide the patch bank (UI refresh U6), for the macOS Window menu:
    // MainWindow's root component installs both; the item is ticked while the
    // bank panel is showing.
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

    // Switch the Test Tone (Settings > Test Tone, and the Audio Devices
    // dialog's toggle). Public so the UI snapshot tool can drive the real
    // thing end to end rather than a reimplementation of it (#111): the tone
    // is the one control whose bug was in the sequencing, which a test that
    // repeats the sequence by hand cannot catch.
    void toggleTestTone();
    bool isTestToneOn() const noexcept { return toneActive_; }
    // Where the tone actually is, as opposed to what the switch says. The
    // two can only differ if something bypasses the bar, so the snapshot
    // tool checks them against each other.
    bool isTestToneLive() const noexcept;

    // The tone switched. RootComponent uses this to re-read the status bar,
    // which is where the user is told (#111).
    std::function<void()> onTestToneChanged;

    // Plugins > Rescan Plugins: the out-of-process scan and its summary. The
    // prompt after a settings reset runs it too (M5 U7).
    void rescanPlugins();

    // The File, Settings and Plugins menus, shared by the bar's buttons and the
    // macOS menu bar (#25). With `menuBarCommands` the File menu is the menu
    // bar's: Save runs through the app command, so it shows its shortcut, and
    // About is left to the app menu. Settings, with App Mappings, is a menu of
    // its own everywhere; the patch bank shows and hides from the bank itself
    // (and the macOS Window menu).
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
    bool canAddPlugin() const; // Session Idle
    void openFileMenu();
    void openPluginList();
    // The Test Tone switch changed (the Settings menu or the Audio Devices
    // dialog): start or stop the tone.
    void valueChanged(juce::Value&) override;
    void updateAddPluginEnabled();
    // Icon, colour and tooltip for the master mute (#106).
    void refreshMuteButton();
    void openPluginsMenu();

    // Patch strip (M3 U5, reworked for #109).
    void openPatchMenu();

    AppContext& ctx_;
    SignalCanvas& canvas_;
    FileActions& files_;

    // The three menus are icons, not words (#90). Their tooltips carry the
    // name, and on macOS the menu bar still spells them out.
    IconButton fileButton_{folderIcon(), "File"};
    juce::TextButton addPluginButton_{"+"};
    IconButton settingsButton_{gearIcon(), "Settings"};
    IconButton pluginsButton_{plugIcon(), "Plugins"};
    DeviceBadge deviceBadge_;
    ActivityLights activityLights_;
    InputMonitor inputMonitor_;
    LatencyReadout latencyReadout_;
    CpuMeter cpuMeter_;
    IconButton muteButton_{speakerIcon(), "Master mute"};
    TransportControls transportControls_;

    // The Test Tone switch: the Settings menu's tick and the Audio Devices
    // dialog's toggle share it; valueChanged() asks the engine to start or
    // stop the tone.
    //
    // There is nothing else here any more. The tone used to be a graph that
    // replaced the patch, so the bar had to clear the board, wait for that
    // clear to actually commit on Session's worker, remember which patch
    // generation it had asked at, stash the board in the bank slot, suspend
    // live capture, close every open editor, and reload the lot on the way
    // back. All of it existed to make a diagnostic that deleted your rig
    // survivable. The tone is a master stage in the runtime now (#111): the
    // board is untouched, so the bar just forwards the switch.
    juce::Value testTone_{false};
    bool toneActive_{false}; // what is actually playing

    PatchStrip patchStrip_;

    // The unclaimed width left in the middle after the last resized().
    int slack_{0};

    // Where the group dividers go, filled in by resized() and drawn by
    // paint(). The bar has six groups and had nothing between them (#109).
    std::vector<int> dividerX_;

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
