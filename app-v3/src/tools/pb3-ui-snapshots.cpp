// pb3-ui-snapshots — renders the app's menus, dialogs and alerts to PNG through
// its real LookAndFeel (UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U1 / KD6), so every UI
// change can show its surfaces before and after.
//
// Usage: pb3-ui-snapshots [<output-folder>]   (./build.ps1 snapshots runs it)
//
// It builds the subsystems the UI reaches for, as App does, with settings in a
// throwaway folder and a few made-up plugins, patches and recent files so the
// menus have something in them, and no main window. Menus are laid out and
// drawn the way JUCE's own menu window does it (ItemComponent,
// HeaderItemComponent and MenuWindow in juce_PopupMenu.cpp), with one item
// highlighted; everything else is a component snapshot. A dev tool, not shipped.

#include "AppContext.h"
#include "control/OscServer.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/AppMappings.h"
#include "session/BankDocument.h"
#include "session/ControlHub.h"
#include "session/EditHistory.h"
#include "session/FeatureGate.h"
#include "session/Features.h"
#include "session/Preferences.h"
#include "session/Session.h"
#include "session/TransportCommands.h"
#include "ui/AboutWindow.h"
#include "ui/ColourScheme.h"
#include "ui/FeatureSettingsWindows.h"
#include "ui/FileActions.h"
#include "ui/MappingsDialog.h"
#include "ui/Pb3LookAndFeel.h"
#include "ui/SettingsWindow.h"
#include "ui/SignalCanvas.h"
#include "ui/TransportBar.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace
{

using namespace pedalboard3;

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
    engine::AudioEngine audioEngine;
    engine::PluginHost pluginHost;
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

    const auto boards = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Pedalboard 3");
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

// A menu as JUCE's menu window lays it out: each item's ideal size (a section
// header's grown by a half and a quarter), one column as wide as its widest
// item plus the border on both sides, at least the minimum width.
juce::Image renderMenu(const juce::PopupMenu& menu, int highlight)
{
    const juce::PopupMenu::Options options;
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

    // A dialog's content on the background its window paints behind it.
    void addDialog(const juce::String& name, juce::Component& content)
    {
        juce::Image image(
            juce::Image::ARGB, juce::jmax(1, content.getWidth()), juce::jmax(1, content.getHeight()), true);
        {
            juce::Graphics g(image);
            g.fillAll(ui::tokens::bgPopover);
            g.drawImageAt(content.createComponentSnapshot(content.getLocalBounds(), true, 1.0f), 0, 0);
        }
        add(name, image);
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
        out.addMenu("menu-add-plugin-built-in", addPlugin != nullptr ? findSubMenu(*addPlugin, "Built-in") : nullptr);
        const auto patchMenu = bar.makePatchMenu();
        out.addMenu("menu-patch", &patchMenu, 2);

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
                 return sized(std::make_unique<juce::PluginListComponent>(rig.pluginHost.getFormatManager(),
                                                                          rig.pluginHost.getKnownPluginList(),
                                                                          scratch.getChildFile("crashed_plugins.xml"),
                                                                          nullptr,
                                                                          false),
                              720,
                              520);
             }},
        };
        for (const auto& [name, make] : dialogs)
        {
            const auto content = make();
            out.addDialog(name, *content);
        }

        // Alerts, as the app raises them.
        const std::unique_ptr<juce::AlertWindow> unsaved(lookAndFeel.createAlertWindow(
            "Unsaved changes",
            "Save the changes to \"Friday Set\" before quitting? Changes you don't save are lost.",
            "Save",
            "Don't Save",
            "Cancel",
            juce::MessageBoxIconType::QuestionIcon,
            3,
            nullptr));
        out.addComponent("alert-unsaved-changes", *unsaved);
        const std::unique_ptr<juce::AlertWindow> message(
            lookAndFeel.createAlertWindow("Default saved",
                                          "File > New now starts from this board.",
                                          "OK",
                                          {},
                                          {},
                                          juce::MessageBoxIconType::InfoIcon,
                                          1,
                                          nullptr));
        out.addComponent("alert-message", *message);

        std::printf("%d surfaces in %s\n", out.count(), out.folder().getFullPathName().toRawUTF8());
        failures = out.failures();
    }

    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    scratch.deleteRecursively();
    return failures == 0 ? 0 : 1;
}
