// pb3-ui-snapshots — renders the app's menus, dialogs and alerts to PNG through
// its real LookAndFeel (UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U1 / KD6), so every UI
// change can show its surfaces before and after.
//
// Usage: pb3-ui-snapshots [<output-folder>]   (./build.ps1 snapshots runs it)
//
// It builds the subsystems the UI reaches for, as App does, with settings in a
// throwaway folder and a few made-up plugins, patches and recent files so the
// menus have something in them. The main window is the real RootComponent at a
// few sizes and panel states, with built-in nodes on the board, which is why
// RootComponent lives in its own header (#90). Menus are laid out and
// drawn the way JUCE's own menu window does it (ItemComponent,
// HeaderItemComponent and MenuWindow in juce_PopupMenu.cpp), with one item
// highlighted; everything else is a component snapshot. A dev tool, not shipped.

#include "AppContext.h"
#include "control/OscServer.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/AppCommands.h"
#include "session/AppMappings.h"
#include "session/BankDocument.h"
#include "session/BoardFiles.h"
#include "session/ControlHub.h"
#include "session/EditHistory.h"
#include "session/FeatureGate.h"
#include "session/Features.h"
#include "session/Preferences.h"
#include "session/Session.h"
#include "session/TransportCommands.h"
#include "ui/AboutWindow.h"
#include "ui/Alert.h"
#include "ui/AppMappingsWindow.h"
#include "ui/BankSidebar.h"
#include "ui/ColourScheme.h"
#include "ui/DialogFrame.h"
#include "ui/FeatureSettingsWindows.h"
#include "ui/FileActions.h"
#include "ui/MappingsDialog.h"
#include "ui/Pb3LookAndFeel.h"
#include "ui/PluginBrowser.h"
#include "ui/RootComponent.h"
#include "ui/SettingsWindow.h"
#include "ui/SignalCanvas.h"
#include "ui/TransportBar.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace pedalboard3;

// The Test Tone probe (#111): how much audio to push through the runtime to
// decide whether the tone is actually reaching the output, and the floor it
// has to clear.
//
// The settle blocks matter. The crossfade is 15 ms -- about a block and a
// half at 48k/512 -- so a probe that measures from the first block after the
// switch reads the fade, not the steady state. Measuring the fade-OUT is how
// the first version of this check reported the tone still playing at 0.242
// immediately after it had been switched off, which is a true measurement of
// the wrong instant. Four blocks of settle is ~43 ms, comfortably past it.
//
// The floor sits well under the tone's 0.25 amplitude and well over the
// silence the rig's board produces.
constexpr double kToneProbeRate = 48000.0;
constexpr int kToneProbeBlock = 512;
constexpr int kToneSettleBlocks = 4;
constexpr int kToneProbeBlocks = 4;
constexpr float kToneAudibleFloor = 0.05f;

// Everything the UI's constructors reach for, built as App builds it.
struct Rig
{
    explicit Rig(const juce::File& folder)
        : preferences(folder.getChildFile("Pedalboard 3.settings")), liveSession(audioEngine, pluginHost),
          bankDocument(liveSession),
          featureGate(session::FeatureGate::GlobalStore{[this](session::Feature feature)
                                                        { return preferences.getFeatureEnabled(feature); },
                                                        [this](session::Feature feature, bool on)
                                                        { preferences.setFeatureEnabled(feature, on); }},
                      bankDocument),
          editHistory(liveSession), transportCommands(liveSession, audioEngine.getTransport()),
          oscServer([](const juce::OSCMessage&) {}), context{audioEngine,
                                                             pluginHost,
                                                             preferences,
                                                             liveSession,
                                                             bankDocument,
                                                             controlHub,
                                                             appMappings,
                                                             oscServer,
                                                             editHistory,
                                                             transportCommands,
                                                             featureGate}
    {
    }

    session::Preferences preferences;
    // pluginHost before audioEngine, so the engine (and the graph holding the
    // built-in processors) is destroyed first and the disk thread outlives its
    // clients — the same order App::shutdown uses, for the reason set out in
    // engine/DiskThread.h.
    engine::PluginHost pluginHost;
    engine::AudioEngine audioEngine;
    session::Session liveSession;
    session::BankDocument bankDocument;
    session::FeatureGate featureGate;
    session::EditHistory editHistory;
    session::TransportCommands transportCommands;
    session::ControlHub controlHub;
    session::AppMappings appMappings;
    control::OscServer oscServer;
    AppContext context;
};

// Something in each menu: scanned effects, a set list, recent boards, and two
// features on in Settings.
void seed(Rig& rig)
{
    auto& list = rig.pluginHost.getKnownPluginList();
    const std::pair<const char*, const char*> effects[] = {
        {"Pro-Q 3", "FabFilter"},
        {"Pro-C 2", "FabFilter"},
        {"ValhallaRoom", "Valhalla DSP"},
        {"ValhallaDelay", "Valhalla DSP"},
        {"Decapitator", "Soundtoys"},
        {"Kilohearts Chorus", "Kilohearts"},
    };
    for (const auto& [name, vendor] : effects)
    {
        juce::PluginDescription d;
        d.name = name;
        d.descriptiveName = name;
        d.manufacturerName = vendor;
        d.pluginFormatName = "VST3";
        d.category = "Fx";
        d.version = "1.0";
        d.fileOrIdentifier = juce::String("C:/Program Files/Common Files/VST3/") + name + ".vst3";
        d.uniqueId = juce::String(name).hashCode();
        d.numInputChannels = 2;
        d.numOutputChannels = 2;
        list.addType(d);
    }

    const auto boards = session::boardfiles::patchesFolder();
    session::PatchBank bank;
    bank.patches.clear();
    for (const char* name : {"Soundcheck", "Intro Pad", "Verse Clean", "Chorus Wide", "Bridge Fuzz", "Solo Lead"})
    {
        session::Patch patch;
        patch.name = name;
        bank.patches.push_back(patch);
    }
    bank.activeIndex = 2;
    rig.bankDocument.loadBank(std::move(bank), boards.getChildFile("Friday Set.pdl"));

    rig.preferences.setRecentFiles({boards.getChildFile("Friday Set.pdl").getFullPathName(),
                                    boards.getChildFile("Rehearsal.pdl").getFullPathName(),
                                    boards.getChildFile("Home Rig.pdl").getFullPathName()});
    rig.featureGate.setEnabled(session::Feature::Tempo, true);
    rig.featureGate.setEnabled(session::Feature::Metronome, true);
}

// Let the session finish whatever it is building, then hand the result to the
// components. It rebuilds the graph on a background thread and an add is
// refused while it is busy — which is why the first attempt at a board
// snapshot came out empty, with the status bar still reading "Loading...".
//
// This is the headless tests' pump (pumpUntilIdle in BankDocument_test.cpp):
// dispatchPendingResult() collects the background thread's work on this
// thread, so no message loop is needed — just as well, since
// JUCE_MODAL_LOOPS_PERMITTED is off and runDispatchLoopUntil doesn't exist in
// this build. dispatchPendingMessages() then delivers the change message to
// the canvas and the status bar now rather than on the next message-loop turn
// that a console app never takes.
bool settle(Rig& rig, int timeoutMs = 5000)
{
    const auto idle = [&rig] { return rig.liveSession.getState() == session::Session::State::Idle; };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool settled = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        rig.liveSession.dispatchPendingResult();
        if (idle())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            rig.liveSession.dispatchPendingResult();
            if (idle())
            {
                settled = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    rig.liveSession.dispatchPendingMessages();
    rig.bankDocument.dispatchPendingMessages();
    return settled;
}

// A board with cards on it. Built-ins only: they instantiate without a plugin
// on disk, so the canvas looks like a real chain on any machine, a bare CI
// runner included. They go on through addBrowserEntry, the same path the Add
// Plugin menu and a browser drag take, so these snapshots can't drift from
// what adding a node actually does.
//
// Returns how many went on. The caller checks it: a board snapshot that
// quietly came out empty would be worse than no snapshot, because it would
// look like a deliberate "empty board" shot.
int fillBoard(Rig& rig)
{
    // Metronome rather than Looper: seed() turns the Metronome feature on, and
    // builtInAvailable() refuses a node whose feature is off.
    int added = 0;
    for (const char* type : {"LevelProcessor", "FilePlayerProcessor", "VuMeterProcessor", "MetronomeProcessor"})
    {
        if (!settle(rig))
            break;
        if (ui::addBrowserEntry(rig.context, juce::String("internal:") + type, std::nullopt))
            ++added;
    }
    settle(rig);
    return added;
}

// A menu as JUCE's menu window lays it out: each item's ideal size (a section
// header's grown by a half and a quarter), one column as wide as its widest
// item plus the border on both sides, at least the minimum width.
juce::Image renderMenu(const juce::PopupMenu& menu, int highlight)
{
    // The app opens its menus at the mockup's minimum width (UI chrome U3).
    const auto options = juce::PopupMenu::Options{}.withMinimumWidth(ui::Pb3LookAndFeel::kMenuMinimumWidth);
    auto& lookAndFeel = juce::LookAndFeel::getDefaultLookAndFeel();
    const int border = lookAndFeel.getPopupMenuBorderSizeWithOptions(options);
    const int standard = options.getStandardItemHeight();

    struct Row
    {
        const juce::PopupMenu::Item* item;
        int height;
    };
    std::vector<Row> rows;
    int width = standard;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
    {
        const auto& item = it.getItem();
        int w = 80;
        int h = 16;
        if (item.isSectionHeader)
        {
            lookAndFeel.getIdealPopupMenuItemSizeWithOptions(item.text, false, -1, w, h, options);
            h += h / 2;
            w += w / 4;
        }
        else
        {
            const auto text =
                item.shortcutKeyDescription.isEmpty() ? item.text : item.text + "   " + item.shortcutKeyDescription;
            lookAndFeel.getIdealPopupMenuItemSizeWithOptions(text, item.isSeparator, standard, w, h, options);
        }
        rows.push_back({&item, juce::jlimit(1, 600, h)});
        width = juce::jmax(width, w);
    }
    width = juce::jmax(width + border * 2, options.getMinimumWidth());
    int height = border * 2;
    for (const auto& row : rows)
        height += row.height;

    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);
    g.fillAll(ui::tokens::bgWindow); // what a menu opens over: the app window
    lookAndFeel.drawPopupMenuBackgroundWithOptions(g, width, height, options);

    int y = border;
    int selectable = 0;
    for (const auto& row : rows)
    {
        const juce::Graphics::ScopedSaveState state(g);
        g.setOrigin(0, y);
        const juce::Rectangle<int> area(width, row.height);
        if (row.item->isSectionHeader)
        {
            lookAndFeel.drawPopupMenuSectionHeaderWithOptions(g, area.reduced(border, 0), row.item->text, options);
        }
        else
        {
            const bool canHighlight = !row.item->isSeparator && row.item->isEnabled;
            const bool highlighted = canHighlight && selectable++ == highlight;
            lookAndFeel.drawPopupMenuItemWithOptions(g, area, highlighted, *row.item, options);
        }
        y += row.height;
    }
    return image;
}

const juce::PopupMenu* findSubMenu(const juce::PopupMenu& menu, const juce::String& text)
{
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().text == text && it.getItem().subMenu != nullptr)
            return it.getItem().subMenu.get();
    return nullptr;
}

// Writes each surface as <name>.png and counts what couldn't be.
class Snapshots
{
public:
    explicit Snapshots(juce::File folder) : folder_(std::move(folder)) { folder_.createDirectory(); }

    void add(const juce::String& name, const juce::Image& image)
    {
        const auto file = folder_.getChildFile(name + ".png");
        file.deleteFile();
        bool ok = image.isValid() && image.getWidth() > 0 && image.getHeight() > 0;
        if (ok)
        {
            juce::FileOutputStream out(file);
            juce::PNGImageFormat png;
            ok = out.openedOk() && png.writeImageToStream(image, out);
        }
        std::printf(
            "%s %s (%d x %d)\n", ok ? "wrote " : "FAILED", name.toRawUTF8(), image.getWidth(), image.getHeight());
        ++count_;
        if (!ok)
            ++failures_;
    }

    void addMenu(const juce::String& name, const juce::PopupMenu* menu, int highlight = 0)
    {
        if (menu == nullptr)
        {
            std::printf("FAILED %s (no such menu)\n", name.toRawUTF8());
            ++count_;
            ++failures_;
            return;
        }
        add(name, renderMenu(*menu, highlight));
    }

    void addComponent(const juce::String& name, juce::Component& component)
    {
        add(name, component.createComponentSnapshot(component.getLocalBounds(), true, 1.0f));
    }

    // A dialog: its content in the frame its window opens with (UI chrome U4),
    // kept off the screen.
    void addDialog(const juce::String& name, const juce::String& title, juce::Component& content)
    {
        ui::DialogFrame frame(title, false);
        frame.setContentNonOwned(&content, true);
        frame.setVisible(true);
        add(name, frame.createComponentSnapshot(frame.getLocalBounds(), true, 1.0f));
        frame.clearContentComponent();
    }

    int count() const noexcept { return count_; }
    int failures() const noexcept { return failures_; }
    const juce::File& folder() const noexcept { return folder_; }

private:
    juce::File folder_;
    int count_{0};
    int failures_{0};
};

using ContentFactory = std::function<std::unique_ptr<juce::Component>()>;

// The title each dialog's window shows.
juce::String dialogTitle(const juce::String& name)
{
    const std::pair<const char*, const char*> titles[] = {
        {"dialog-audio-devices", "Audio Devices"},
        {"dialog-midi-settings", "MIDI Settings"},
        {"dialog-osc-settings", "OSC Settings"},
        {"dialog-tempo-settings", "Tempo / BPM Settings"},
        {"dialog-metronome-settings", "Metronome Settings"},
        {"dialog-looper-settings", "Looper Settings"},
        {"dialog-about", "About Pedalboard 3"},
        {"dialog-mappings", "Mappings: Pro-Q 3"},
        {"dialog-plugin-list", "Plugin List"},
    };
    for (const auto& [surface, title] : titles)
        if (name == surface)
            return title;
    return name;
}

template <typename ComponentType>
std::unique_ptr<juce::Component> sized(std::unique_ptr<ComponentType> component, int width, int height)
{
    component->setSize(width, height);
    return component;
}

} // namespace

int main(int argc, char** argv)
{
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? juce::String::fromUTF8(argv[1])
                                                                                       : juce::String("ui-snapshots"));

    juce::SystemStats::setApplicationCrashHandler(
        [](void*)
        {
            std::fprintf(stderr, "\nCRASH\n%s\n", juce::SystemStats::getStackBacktrace().toRawUTF8());
            std::fflush(stderr);
        });

    const juce::ScopedJuceInitialiser_GUI scopedJuce;
    ui::Pb3LookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

    const auto scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getNonexistentChildFile("pb3-ui-snapshots", {}, false);
    scratch.createDirectory();

    int failures = 0;
    {
        Rig rig(scratch);
        (void)rig.audioEngine.initialise(nullptr); // the machine's devices, for Audio Devices
        seed(rig);
        Snapshots out(folder);

        // Menus, from the top bar's own builders.
        ui::SignalCanvas canvas(rig.context);
        ui::FileActions files(rig.context);
        ui::TransportBar bar(rig.context, canvas, files);
        bar.setSize(1280, 52);

        const auto fileMenu = bar.makeFileMenu();
        out.addMenu("menu-file", &fileMenu);
        out.addMenu("menu-file-open-recent", findSubMenu(fileMenu, "Open Recent"));
        const auto settingsMenu = bar.makeSettingsMenu();
        out.addMenu("menu-settings", &settingsMenu);
        const auto pluginsMenu = bar.makePluginsMenu();
        out.addMenu("menu-plugins", &pluginsMenu);
        const auto* addPlugin = findSubMenu(pluginsMenu, "Add Plugin");
        out.addMenu("menu-add-plugin", addPlugin, 1);
        ui::BankSidebar sidebar(rig.context);
        const auto rowMenu = sidebar.makeRowMenu(2);
        out.addMenu("menu-bank-row", &rowMenu);

        // Dialogs: each one's content, as its window opens it.
        juce::Value testTone;
        const std::pair<const char*, ContentFactory> dialogs[] = {
            {"dialog-audio-devices", [&] { return ui::makeAudioDevicesPanel(rig.context, testTone); }},
            {"dialog-midi-settings", [&] { return ui::makeMidiSettingsPanel(rig.context); }},
            {"dialog-osc-settings", [&] { return ui::makeOscSettingsPanel(rig.context); }},
            {"dialog-tempo-settings", [&] { return ui::makeTempoSettingsPanel(rig.context); }},
            {"dialog-metronome-settings", [&] { return ui::makeMetronomeSettingsPanel(rig.context); }},
            {"dialog-looper-settings", [&] { return ui::makeLooperSettingsPanel(rig.context); }},
            {"dialog-about", [] { return ui::makeAboutPanel(); }},
            {"dialog-mappings",
             [&] { return sized(std::make_unique<ui::MappingsPanel>(rig.context, session::NodeId{1}), 900, 460); }},
            {"dialog-plugin-list",
             [&]
             {
                 auto list = std::make_unique<juce::PluginListComponent>(rig.pluginHost.getFormatManager(),
                                                                         rig.pluginHost.getKnownPluginList(),
                                                                         scratch.getChildFile("crashed_plugins.xml"),
                                                                         nullptr,
                                                                         false);
                 list->getTableListBox().setRowHeight(28); // as TransportBar opens it (UI chrome U7)
                 list->getTableListBox().setHeaderHeight(28);
                 return sized(std::move(list), 720, 520);
             }},
        };
        for (const auto& [name, make] : dialogs)
        {
            const auto content = make();
            out.addDialog(name, dialogTitle(name), *content);
        }

        // App Mappings, with the app's commands registered as MainWindow
        // describes them (a few of their default keys), so the Keyboard tab
        // has rows (UI chrome U7).
        juce::ApplicationCommandManager commands;
        for (const auto& command : session::kAppCommands)
        {
            const juce::String id(command.id);
            const auto prefix = id.upToFirstOccurrenceOf(".", false, false);
            juce::ApplicationCommandInfo info(command.juceCommandId);
            info.setInfo(command.name, command.name, prefix.substring(0, 1).toUpperCase() + prefix.substring(1), 0);
            if (id == session::appcommands::kNextPatch)
                info.addDefaultKeypress(juce::KeyPress::pageDownKey, juce::ModifierKeys());
            else if (id == session::appcommands::kPreviousPatch)
                info.addDefaultKeypress(juce::KeyPress::pageUpKey, juce::ModifierKeys());
            else if (id == session::appcommands::kSave)
                info.addDefaultKeypress('s', juce::ModifierKeys::commandModifier);
            else if (id == session::appcommands::kPlayPause)
                info.addDefaultKeypress(juce::KeyPress::spaceKey, juce::ModifierKeys());
            commands.registerCommand(info);
        }
        const auto appMappings = ui::makeAppMappingsContent(rig.context, *commands.getKeyMappings());
        out.addDialog("dialog-app-mappings", "App Mappings", *appMappings);

        // Alerts, as the app raises them (UI chrome U5).
        using ui::alert::Role;
        const auto unsaved = ui::alert::makePanel(
            {"Unsaved changes",
             "Save the changes to \"Friday Set\" before quitting? Changes you don't save are lost.",
             {{"Save", 1, Role::Main}, {"Don't Save", 2, Role::Other}, {"Cancel", 0, Role::Cancel}},
             {}});
        out.addDialog("alert-unsaved-changes", "Unsaved changes", *unsaved);
        const auto rename = ui::alert::makePanel({"Rename Patch",
                                                  "Name for patch 3:",
                                                  {{"Rename", 1, Role::Main}, {"Cancel", 0, Role::Cancel}},
                                                  {{"name", "Verse Clean"}}});
        out.addDialog("alert-rename-patch", "Rename Patch", *rename);
        const auto remove =
            ui::alert::makePanel({"Remove Patch",
                                  "Remove \"Verse Clean\" and its 3 plugins from the bank? This can't be undone.",
                                  {{"Remove", 1, Role::Main, true}, {"Cancel", 0, Role::Cancel}},
                                  {}});
        out.addDialog("alert-remove-patch", "Remove Patch", *remove);
        const auto message =
            ui::alert::makePanel(ui::alert::message("Default saved", "File > New now starts from this board."));
        out.addDialog("alert-message", "Default saved", *message);

        // What JUCE still raises itself, such as the plugin scan's progress window.
        const std::unique_ptr<juce::AlertWindow> progress(lookAndFeel.createAlertWindow(
            "Scanning for plugins", "Pro-Q 3.vst3", "Cancel", {}, {}, juce::MessageBoxIconType::NoIcon, 1, nullptr));
        out.addComponent("alert-juce-progress", *progress);

        // The main window (#90). Until now every dialog had a snapshot and the
        // one surface people actually look at had none, which is a fair part of
        // why the dialogs got a chrome pass and the window didn't. An empty
        // board, a board with cards, both side panels collapsed to their rails,
        // and the narrow width where BodyLayout folds them on its own.
        {
            const auto shoot = [&out](const juce::String& name, ui::RootComponent& root, int width, int height)
            {
                root.setSize(width, height);
                root.setVisible(true);
                out.add(name, root.createComponentSnapshot(root.getLocalBounds(), true, 1.0f));
            };

            settle(rig); // the seeded bank is still building when we get here
            ui::RootComponent root(rig.context);
            shoot("window-empty", root, 1280, 800);

            const auto added = fillBoard(rig);
            if (added != 4)
                std::printf("WARNING window-board has %d of 4 nodes; the session refused the rest\n", added);
            shoot("window-board", root, 1280, 800);

            // Selecting a card is the only way a still image can show the two
            // accents apart (#105): the selected card and its mapping row take
            // the "chosen" pink, while the playing patch and the live cables
            // keep the "playing" cyan-green.
            const auto nodes = rig.liveSession.getNodeSnapshots();
            if (nodes.size() >= 2)
            {
                root.canvas().selectNode(nodes[1].id);
                settle(rig);
                shoot("window-selection", root, 1280, 800);
                root.canvas().selectNode(std::nullopt);
                settle(rig);
            }
            else
            {
                std::printf("WARNING window-selection skipped; the board has %d nodes\n",
                            static_cast<int>(nodes.size()));
            }

            // A panel's open state is read from Preferences in the constructor,
            // so a fresh root is how you snapshot it collapsed.
            rig.preferences.setPanelShown("patches", false);
            rig.preferences.setPanelShown("browser", false);
            ui::RootComponent collapsed(rig.context);
            shoot("window-panels-collapsed", collapsed, 1280, 800);

            rig.preferences.setPanelShown("patches", true);
            rig.preferences.setPanelShown("browser", true);
            ui::RootComponent narrow(rig.context);
            shoot("window-narrow", narrow, 900, 640);

            // How the top bar gives way as the window narrows (#116). The
            // complaint was that it folded while slack sat unused between the
            // patch strip and the activity lights, so the evidence has to be
            // a measurement of exactly that, not a picture of one width.
            //
            // At each width: what the bar needs whole, how much room the
            // patch strip got over its floor (the slack), and which of the
            // optional parts are still showing.
            {
                auto& topBar = root.transportBar();
                const int natural = topBar.naturalWidth();
                std::printf("TOP BAR natural width %d (window minimum)\n", natural);
                for (const int width : {natural + 400, natural + 120, natural, natural - 150, natural - 400})
                {
                    root.setSize(width, 800);
                    const auto shown = [&](const juce::Component& c) { return c.isVisible() ? "y" : "-"; };
                    std::printf("TOP BAR %5d  gap %4d  badge %3d @%4d  free %4d..%4d  centre %4d vs %4d  latency %s  "
                                "cpu/mem %s  in/out %s  midi/osc %s\n",
                                width,
                                topBar.barSlack(),
                                topBar.deviceBadgeWidth(),
                                topBar.deviceBadgeX(),
                                topBar.badgeFreeRegion().first,
                                topBar.badgeFreeRegion().second,
                                topBar.deviceBadgeX() + topBar.deviceBadgeWidth() / 2,
                                width / 2,
                                shown(topBar.latencyReadoutForTest()),
                                shown(topBar.systemMetersForTest()),
                                shown(topBar.ioMonitorForTest()),
                                shown(topBar.activityLightsForTest()));
                }
                root.setSize(1280, 800);
            }

            // The Test Tone, driven through the real TransportBar (#111).
            // A unit test can repeat the sequence valueChanged performs, but
            // it cannot catch a bug that IS the sequence -- and here the bug
            // turned out to be the design, not the sequence.
            //
            // So what this harness checks is now the opposite of what it
            // checked before. The old tone replaced the patch, and the check
            // asked whether the board came BACK. The tone is a master stage
            // now, so the board must never LEAVE: same graph pointer, same
            // node count, right through. Anything else is the thing the user
            // reported, which was "the whole chain vanishes".
            {
                auto& toneBar = root.transportBar();
                const int nodesBefore = static_cast<int>(rig.liveSession.getNodeSnapshots().size());
                auto* runtime = rig.audioEngine.getRuntime();
                auto* graphBefore = runtime != nullptr ? runtime->getLiveGraph() : nullptr;

                // Drive the runtime by hand for the probe, with the device
                // shut. Otherwise a dev box with a sound card has its audio
                // thread calling processBlock at the same time we do, and
                // the number we print is a race; a headless CI runner with
                // no device would meanwhile never have had prepareToPlay
                // called at all, leaving the tone's phase step at zero and
                // the probe silent for a reason that has nothing to do with
                // the tone. Closing it makes both machines agree.
                rig.audioEngine.getDeviceManager()->closeAudioDevice();
                if (runtime != nullptr)
                {
                    runtime->setPlayConfigDetails(2, 2, kToneProbeRate, kToneProbeBlock);
                    runtime->prepareToPlay(kToneProbeRate, kToneProbeBlock);
                }

                // Push blocks through the runtime and report the loudest
                // sample. The board in this rig is silent, so anything above
                // the floor is the tone. Without this the harness would pass
                // a tone that was switched on and never reached the output --
                // which is the original complaint.
                const auto outputPeak = [runtime]
                {
                    if (runtime == nullptr)
                        return 0.0f;
                    juce::AudioBuffer<float> block(2, kToneProbeBlock);
                    juce::MidiBuffer midi;
                    const auto pump = [&]
                    {
                        block.clear();
                        midi.clear();
                        runtime->processBlock(block, midi);
                    };

                    for (int i = 0; i < kToneSettleBlocks; ++i)
                        pump(); // let the crossfade finish before measuring

                    float peak = 0.0f;
                    for (int i = 0; i < kToneProbeBlocks; ++i)
                    {
                        pump();
                        peak = juce::jmax(peak, block.getMagnitude(0, block.getNumSamples()));
                    }
                    return peak;
                };

                toneBar.toggleTestTone();
                settle(rig);
                auto* graphDuring = runtime != nullptr ? runtime->getLiveGraph() : nullptr;
                const int nodesDuring = static_cast<int>(rig.liveSession.getNodeSnapshots().size());
                const float tonePeak = outputPeak();

                // The switch says on, the engine agrees, the output is
                // audible, and nothing about the board moved.
                const bool boardIntact = graphDuring == graphBefore && nodesDuring == nodesBefore;
                const bool audible = tonePeak > kToneAudibleFloor;
                const bool tookOver = toneBar.isTestToneOn() && toneBar.isTestToneLive() && boardIntact && audible;
                std::printf("TEST TONE on : switch=%s engine=%s board=%s nodes %d -> %d peak %.3f (%s)\n",
                            toneBar.isTestToneOn() ? "on" : "OFF",
                            toneBar.isTestToneLive() ? "on" : "OFF",
                            graphDuring == graphBefore ? "intact" : "SWAPPED",
                            nodesBefore,
                            nodesDuring,
                            static_cast<double>(tonePeak),
                            audible ? "audible" : "SILENT");
                shoot("window-test-tone", root, 1280, 800);

                toneBar.toggleTestTone();
                settle(rig);
                auto* graphAfter = runtime != nullptr ? runtime->getLiveGraph() : nullptr;
                const int nodesAfter = static_cast<int>(rig.liveSession.getNodeSnapshots().size());
                const float afterPeak = outputPeak();
                const bool wentQuiet = afterPeak <= kToneAudibleFloor;
                const bool stillThere = !toneBar.isTestToneOn() && !toneBar.isTestToneLive() &&
                                        nodesAfter == nodesBefore && graphAfter == graphBefore;
                std::printf("TEST TONE off: switch=%s engine=%s board=%s nodes %d -> %d peak %.3f error=%s\n",
                            toneBar.isTestToneOn() ? "ON" : "off",
                            toneBar.isTestToneLive() ? "ON" : "off",
                            graphAfter == graphBefore ? "intact" : "SWAPPED",
                            nodesBefore,
                            nodesAfter,
                            static_cast<double>(afterPeak),
                            rig.liveSession.getLastError().toRawUTF8());
                if (!tookOver || !stillThere || !wentQuiet)
                    std::printf("TEST TONE FAILED\n");
                else
                    std::printf("TEST TONE OK\n");
            }

            // Muted (#106). The top bar's button goes red and swaps to
            // volume-x, and the status bar's dot and text go with it, so the
            // lit state is in the evidence rather than only in the code.
            // The mute broadcast is async on purpose — in the app it can come
            // from the MIDI callback thread — so a still image has to push it
            // through rather than wait for a message loop there isn't one of.
            const auto setMuted = [&rig](bool muted)
            {
                rig.audioEngine.setMasterMuted(muted);
                rig.audioEngine.masterMuteChanged().sendSynchronousChangeMessage();
                settle(rig);
            };
            setMuted(true);
            shoot("window-muted", root, 1280, 800);
            setMuted(false);

            // Every icon at the size it is actually drawn, and again at 4x
            // (#125). The small row is the one to judge: a glyph that cannot
            // be told from its neighbour there has failed however good it
            // looks magnified. The big row is only for finding WHICH stroke
            // is wrong once the small one looks off.
            {
                struct Glyph
                {
                    const char* name;
                    juce::Path path;
                    // The box this glyph is actually drawn in, which is not
                    // the same for every member of a set: see R6. Rendering
                    // the sheet at one uniform box would hide the very
                    // optical correction it exists to check.
                    int box;
                };
                const Glyph cardGlyphs[] = {{"power", ui::powerIcon(), 14},
                                            {"faders", ui::slidersIcon(), 14},
                                            {"plug", ui::plugIcon(), 14},
                                            {"cross", ui::closeIcon(), 11},
                                            {"note", ui::noteIcon(), 14}};
                const Glyph barGlyphs[] = {{"folder", ui::folderIcon(), 16},
                                           {"gear", ui::gearIcon(), 16},
                                           {"plug", ui::plugIcon(), 16},
                                           {"speaker", ui::speakerIcon(), 16},
                                           {"muted", ui::volumeMutedIcon(), 16},
                                           {"mic", ui::micIcon(), 13},
                                           {"pulse", ui::activityIcon(), 13}};

                const auto sheet = [&out](const juce::String& name, const Glyph* glyphs, int count)
                {
                    constexpr int kScale = 4;
                    constexpr int kPad = 16;
                    constexpr int kCell = 96;
                    int box = 0;
                    for (int i = 0; i < count; ++i)
                        box = juce::jmax(box, glyphs[i].box);
                    const int w = kPad * 2 + kCell * count;
                    const int h = kPad * 3 + box + box * kScale + 18;

                    juce::Image image(juce::Image::ARGB, w, h, true);
                    // The Graphics must be dead before the image is read:
                    // JUCE's renderer is free to hold state until it is
                    // destroyed, and handing the image to out.add() with a
                    // live Graphics on it wrote a fully transparent PNG.
                    {
                        juce::Graphics g(image);
                        g.fillAll(ui::tokens::bgWindow);

                        for (int i = 0; i < count; ++i)
                        {
                            const int cx = kPad + kCell * i + kCell / 2;

                            // Real size, on the card's own background so the
                            // contrast is the contrast it will actually have.
                            const int thisBox = glyphs[i].box;
                            const juce::Rectangle<float> small(static_cast<float>(cx - thisBox / 2),
                                                               static_cast<float>(kPad + (box - thisBox) / 2),
                                                               static_cast<float>(thisBox),
                                                               static_cast<float>(thisBox));
                            g.setColour(ui::tokens::bgSecondary);
                            g.fillRoundedRectangle(small.expanded(4.0f), 3.0f);
                            g.setColour(ui::tokens::fgPrimary);
                            ui::strokeIcon(g, glyphs[i].path, small);

                            const int bigSide = thisBox * kScale;
                            const juce::Rectangle<float> big(static_cast<float>(cx - bigSide / 2),
                                                             static_cast<float>(kPad * 2 + box),
                                                             static_cast<float>(bigSide),
                                                             static_cast<float>(bigSide));
                            g.setColour(ui::tokens::bgSecondary);
                            g.fillRoundedRectangle(big.expanded(4.0f), 4.0f);
                            g.setColour(ui::tokens::fgPrimary);
                            // Stroked at the SMALL box's weight, scaled up, so
                            // the magnified view shows the real stroke ratio
                            // rather than a fresh thin one.
                            ui::strokeIcon(
                                g, glyphs[i].path, big, ui::iconStrokeFor(static_cast<float>(thisBox)) * kScale);

                            g.setColour(ui::tokens::fgMuted);
                            g.setFont(juce::Font(juce::FontOptions(11.0f)));
                            g.drawText(
                                glyphs[i].name, cx - kCell / 2, h - kPad - 2, kCell, 14, juce::Justification::centred);
                        }
                    }
                    out.add(name, image);
                };

                sheet("icons-card", cardGlyphs, 5);
                sheet("icons-bar", barGlyphs, 7);
            }

            // Wide enough that the device badge shows all three parts (#90):
            // the driver chip, the input with its mic and the output with its
            // speaker. Below 1400 it folds to the chip alone, so the narrower
            // surfaces above cannot show the icons at all.
            ui::RootComponent wide(rig.context);
            shoot("window-wide", wide, 1680, 900);
        }

        std::printf("%d surfaces in %s\n", out.count(), out.folder().getFullPathName().toRawUTF8());
        failures = out.failures();
    }

    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    scratch.deleteRecursively();
    return failures == 0 ? 0 : 1;
}
