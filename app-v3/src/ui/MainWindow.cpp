#include "MainWindow.h"

#include "RootComponent.h"

#include "AboutWindow.h"
#include "Alert.h"
#include "AppContext.h"
#include "AppMappingsWindow.h"
#include "ColourScheme.h"
#include "FileActions.h"
#include "session/AppCommands.h"
#include "session/BankDocument.h"
#include "session/BoardFiles.h"
#include "session/EditHistory.h"
#include "session/Preferences.h"
#include "TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

namespace
{
// Floors under the floor: whatever the bar asks for, the window stays usable
// and resizable.
constexpr int kFloorWidth = 640;
constexpr int kFloorHeight = 400;
constexpr int kCeilingSize = 8192;
} // namespace

MainWindow::MainWindow(const juce::String& name, AppContext& context)
    : juce::DocumentWindow(name, tokens::bgWindow, juce::DocumentWindow::allButtons), ctx_(context), appName_(name)
{
    setUsingNativeTitleBar(true);
    setResizable(/*shouldBeResizable*/ true, /*useBottomRightCornerResizer*/ false);

    root_ = std::make_unique<RootComponent>(context);
    setContentNonOwned(root_.get(), /*resizeToFitWhenContentChangesSize*/ true);

    // App commands (M4 U5). Registering adds each command's default keys to
    // the mapping set; the saved mappings (differences from those defaults)
    // are layered on top.
    commandManager_.registerAllCommandsForTarget(root_.get());
    commandManager_.setFirstCommandTarget(root_.get());
    auto* keys = commandManager_.getKeyMappings();
    if (auto xml = ctx_.preferences.getKeyMappings())
        keys->restoreFromXml(*xml);
    addKeyListener(keys);
    root_->attachKeyMappings(keys);
    keys->addChangeListener(this);
    root_->transportBar().onOpenAppMappings = [this] { openAppMappings(); };

    // The macOS menu bar (#25). Undo, Redo and Save are command items, so they
    // show their shortcuts and follow the saved key mappings.
    setApplicationCommandManagerToWatch(&commandManager_);
#if JUCE_MAC
    juce::PopupMenu appMenu;
    appMenu.addItem("About Pedalboard 3", [] { showAboutWindow(); });
    juce::MenuBarModel::setMacMainMenu(this, &appMenu);
#endif

    // Finish pass U3: file.save (Ctrl/Cmd+S, or a mapped trigger) saves through
    // the File workflow, and the title follows the board's file and unsaved
    // changes.
    ctx_.bankDocument.onSaveRequested = [this] { root_->files().save(); };
    ctx_.bankDocument.addChangeListener(this);
    // Undo and Redo grey out with an empty history (#96), which only works if
    // something re-asks when the history moves.
    ctx_.editHistory.addChangeListener(this);
    updateTitle();

    root_->transportBar().onNaturalWidthChanged = [this] { applyMinimumSize(); };
    applyMinimumSize();

    const auto* limits = getConstrainer();
    const int minWidth = limits != nullptr ? limits->getMinimumWidth() : rootlayout::kInitialWidth;
    centreWithSize(juce::jmax(rootlayout::kInitialWidth, minWidth), rootlayout::kInitialHeight);
    restoreWindowPosition();
    setVisible(true);

    // Again once the content has actually been laid out. The first call
    // cannot know how much of the window's width the OS frame takes, because
    // the content has no size yet; this one can measure it.
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<MainWindow>(this)]
        {
            if (safe != nullptr)
                safe->applyMinimumSize();
        });
}

MainWindow::~MainWindow()
{
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr); // before anything its menus reach goes
#endif
    // Finish pass U6: remember where the window was. Preferences saves it on
    // teardown (App::shutdown destroys this window before Preferences).
    ctx_.preferences.setWindowState(getWindowStateAsString());
    ctx_.editHistory.removeChangeListener(this);
    ctx_.bankDocument.removeChangeListener(this);
    ctx_.bankDocument.onSaveRequested = nullptr;
    appMappingsWindow_.reset();
    auto* keys = commandManager_.getKeyMappings();
    keys->removeChangeListener(this);
    root_->detachKeyMappings(keys);
    removeKeyListener(keys);
    commandManager_.setFirstCommandTarget(nullptr);

    // setContentNonOwned holds a raw pointer; drop the window's content
    // ref before root_ is destroyed so paint() doesn't fire on a moved
    // component during teardown.
    setContentNonOwned(nullptr, false);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &ctx_.bankDocument)
    {
        updateTitle();
        commandManager_.commandStatusChanged(); // Save follows read-only (M5 U4)
        return;
    }
    if (source == &ctx_.editHistory)
    {
        commandManager_.commandStatusChanged(); // Undo / Redo follow the history (#96)
        return;
    }
    // Otherwise the key mappings changed (App Mappings window): persist them.
    if (auto xml = commandManager_.getKeyMappings()->createXml(/*saveDifferencesFromDefaultSet*/ true))
        ctx_.preferences.setKeyMappings(*xml);
}

void MainWindow::openAppMappings()
{
    if (appMappingsWindow_ != nullptr)
    {
        appMappingsWindow_->toFront(true);
        return;
    }
    // Same contract as EditorWindow: the close button calls back and the
    // owner drops the window.
    appMappingsWindow_ = std::make_unique<AppMappingsWindow>(
        ctx_, *commandManager_.getKeyMappings(), [this] { appMappingsWindow_.reset(); });
}

void MainWindow::requestQuit()
{
    root_->files().confirmDiscard("quitting",
                                  [this](bool discarded)
                                  {
                                      if (onQuitConfirmed)
                                          onQuitConfirmed(discarded);
                                      else
                                          juce::JUCEApplication::quit();
                                  });
}

void MainWindow::offerRescanAfterReset(const juce::File& setAside)
{
    // Never a silent reset (M5 U7, plan KD8, AC-SE-24): the plugin list went
    // with the damaged file, and a scan brings it back.
    juce::Component::SafePointer<MainWindow> safeThis(this);
    alert::show({"Settings couldn't be read",
                 "Pedalboard 3's settings file was damaged, so it has been set aside as \"" + setAside.getFileName() +
                     "\" and the app has started with fresh settings. Your plugin list is empty until you scan for "
                     "plugins.",
                 {{"Scan for Plugins", 1, alert::Role::Main}, {"Later", 0, alert::Role::Cancel}},
                 {}},
                [safeThis](const alert::Response& response)
                {
                    if (response.result == 1 && safeThis != nullptr)
                        safeThis->root_->transportBar().rescanPlugins();
                });
}

void MainWindow::updateTitle()
{
    const auto& doc = ctx_.bankDocument;
    setName(juce::String(doc.isModified() ? "* " : "") + boardDisplayName(doc.currentFile()) +
            (doc.isReadOnly() ? " (read-only)" : "") + " - " + appName_);
    root_->setReadOnly(doc.isReadOnly());
}

void MainWindow::applyMinimumSize()
{
    // The top bar decides this, because the top bar is what runs out of room
    // first: the canvas below it scrolls and the panels either side close.
    //
    // This is the other half of #116. Folding on slack means the bar gives
    // nothing up until its own spare space is gone -- and the user's reading
    // of that is "when that white space is unavailable, that's the minimum it
    // can shrink". So that width becomes the floor, and the snapping they
    // called dated mostly stops happening at all, because you cannot get the
    // window narrow enough to cause it.
    const int barWidth = root_->transportBar().naturalWidth();

    // How much of the window is frame rather than content. Zero until the
    // content has been laid out once, which is why the constructor asks
    // again on the next message.
    const int chrome = root_->getWidth() > 0 ? juce::jmax(0, getWidth() - root_->getWidth()) : 0;

    // Never wider than the screen can show. A machine with a small display,
    // or a device whose name is enormous, must still be able to open the
    // window; on one of those the bar folds as it used to and that is the
    // right trade.
    int usable = barWidth + chrome;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        usable = juce::jmin(usable, display->userArea.getWidth());

    setResizeLimits(juce::jmax(kFloorWidth, usable), kFloorHeight, kCeilingSize, kCeilingSize);
}

void MainWindow::restoreWindowPosition()
{
    const auto saved = ctx_.preferences.getWindowState();
    if (saved.isEmpty() || !restoreWindowStateFromString(saved))
        return;

    // The title bar has to land on a display the user can reach; after a
    // monitor is unplugged the saved spot can be off every screen.
    const auto titleStrip =
        getBounds().withHeight(rootlayout::kTransportBarHeight).reduced(rootlayout::kTransportBarHeight, 0);
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
        if (display.userArea.intersects(titleStrip))
            return;
    centreWithSize(rootlayout::kInitialWidth, rootlayout::kInitialHeight);
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return {"File", "Edit", "Settings", "Plugins", "Window"};
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String&)
{
    auto& bar = root_->transportBar();
    switch (topLevelMenuIndex)
    {
    case 0:
        return bar.makeFileMenu(&commandManager_);
    case 1:
    {
        // Undo / Redo run the edit history (finish pass U7-U8).
        juce::PopupMenu menu;
        for (const auto* id : {session::appcommands::kUndo, session::appcommands::kRedo})
            if (const auto* command = session::findAppCommand(id))
                menu.addCommandItem(&commandManager_, command->juceCommandId, command->name);
        return menu;
    }
    case 2:
        return bar.makeSettingsMenu();
    case 3:
        return bar.makePluginsMenu();
    case 4:
    {
        juce::PopupMenu menu;
        menu.addItem("Minimize", [this] { setMinimised(true); });
        menu.addItem("Zoom", [this] { setFullScreen(!isFullScreen()); });
        menu.addSeparator();
        const auto panel =
            [&menu](const juce::String& name, const std::function<bool()>& shown, const std::function<void()>& toggle)
        {
            menu.addItem(name,
                         true,
                         shown && shown(),
                         [toggle]
                         {
                             if (toggle)
                                 toggle();
                         });
        };
        panel("Patch Bank", bar.isPatchBankShown, bar.onTogglePatchBank);
        panel("Plugin Browser", bar.isPluginBrowserShown, bar.onTogglePluginBrowser);
        panel("Mappings Panel", bar.isMappingsPanelShown, bar.onToggleMappingsPanel);
        return menu;
    }
    default:
        return {};
    }
}

} // namespace pedalboard3::ui
