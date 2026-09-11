#include "MainWindow.h"

#include "AppContext.h"
#include "AppMappingsWindow.h"
#include "BankSidebar.h"
#include "BodyLayout.h"
#include "ColourScheme.h"
#include "FileActions.h"
#include "PatchRail.h"
#include "PluginBrowser.h"
#include "session/AppCommands.h"
#include "session/BankDocument.h"
#include "session/Preferences.h"
#include "SignalCanvas.h"
#include "StatusBar.h"
#include "TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

namespace
{
constexpr int kTransportBarHeight = 48;
constexpr int kStatusBarHeight = 32;
constexpr int kInitialWidth = 960;
constexpr int kInitialHeight = 540;
} // namespace

// Container for the three-zone vertical layout. MainWindow owns one
// of these; everything chain-related lives inside.
// The root is also the drag container, so a row dragged out of the plugin
// browser can land on the canvas (UI refresh U5).
class MainWindow::RootComponent : public juce::Component,
                                  public juce::ApplicationCommandTarget,
                                  public juce::DragAndDropContainer
{
public:
    explicit RootComponent(AppContext& ctxRef)
        : ctx_(ctxRef), files_(ctx_), canvas_(ctx_), transportBar_(ctx_, canvas_, files_), statusBar_(ctx_),
          bankSidebar_(ctx_), patchRail_(ctx_), pluginBrowser_(ctx_)
    {
        addAndMakeVisible(transportBar_);
        addAndMakeVisible(viewport_);
        addAndMakeVisible(statusBar_);

        viewport_.setViewedComponent(&canvas_, /*deleteComponentWhenNoLongerNeeded*/ false);
        // Both axes scroll now that cards are freely positioned (a card can be
        // dragged anywhere); this also makes the mouse wheel pan the canvas
        // (wheel = vertical, shift+wheel = horizontal — Viewport defaults).
        viewport_.setScrollBarsShown(/*vert*/ true, /*horiz*/ true);

        // The body's side panel (UI refresh U4 / U6): the patch bank on the
        // left. Its three-bar toggle, top-left under File, collapses it to an
        // icon strip and opens it again at any window size (and File > Patch
        // Bank does the same); below the fold width it folds on its own and the
        // toggle slides it open over the canvas.
        addChildComponent(bankSidebar_);
        addChildComponent(patchRail_);
        patchesOpen_ = ctx_.preferences.getPanelShown("patches", true);
        bankSidebar_.onCollapse = [this] { togglePatchBank(); };
        bankSidebar_.onDismiss = [this] { closeOverlays(); };
        bankSidebar_.onRenameActive = [this] { transportBar_.promptRenamePatch(); };
        bankSidebar_.onRemoveActive = [this] { transportBar_.confirmRemovePatch(); };
        patchRail_.onOpenPanel = [this] { togglePatchBank(); };
        transportBar_.onTogglePatchBank = [this] { togglePatchBank(); };
        transportBar_.isPatchBankShown = [this] { return bankSidebar_.isVisible(); };

        // The plugin browser on the right (UI refresh U5): the same open /
        // collapse / slide-open behaviour, its toggle at the top-right, and
        // Plugins > Plugin Browser.
        addChildComponent(pluginBrowser_);
        addChildComponent(browserRail_);
        browserOpen_ = ctx_.preferences.getPanelShown("browser", true);
        pluginBrowser_.onCollapse = [this] { togglePluginBrowser(); };
        pluginBrowser_.onDismiss = [this] { closeOverlays(); };
        browserRail_.onOpenPanel = [this] { togglePluginBrowser(); };
        transportBar_.onTogglePluginBrowser = [this] { togglePluginBrowser(); };
        transportBar_.isPluginBrowserShown = [this] { return pluginBrowser_.isVisible(); };
        // A click on the canvas closes a slid-open panel.
        viewport_.addMouseListener(this, true);

        setSize(kInitialWidth, kInitialHeight);
    }

    ~RootComponent() override { viewport_.removeMouseListener(this); }

    void mouseDown(const juce::MouseEvent&) override { closeOverlays(); }

    void paint(juce::Graphics& g) override { g.fillAll(tokens::bgWindow); }

    TransportBar& transportBar() noexcept { return transportBar_; }
    FileActions& files() noexcept { return files_; }

    // The canvas sits in a Viewport, which scrolls on Page Up / Page Down and
    // would swallow them before they reached the window's key listener; a
    // listener on the viewport sees them first.
    void attachKeyMappings(juce::KeyPressMappingSet* keys) { viewport_.addKeyListener(keys); }
    void detachKeyMappings(juce::KeyPressMappingSet* keys) { viewport_.removeKeyListener(keys); }

    // ---- juce::ApplicationCommandTarget: the app commands (session/AppCommands.h)
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }

    void getAllCommands(juce::Array<juce::CommandID>& commands) override
    {
        for (const auto& c : session::kAppCommands)
            commands.add(c.juceCommandId);
    }

    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override
    {
        const auto* c = session::findAppCommandByJuceId(commandID);
        if (c == nullptr)
            return;
        const juce::String id(c->id);
        // The category is the id's prefix: "patch.next" -> "Patch".
        const auto prefix = id.upToFirstOccurrenceOf(".", false, false);
        result.setInfo(c->name, c->name, prefix.substring(0, 1).toUpperCase() + prefix.substring(1), 0);
        // Page Down / Page Up: what a Bluetooth page-turner foot pedal sends
        // (plan KD3).
        if (id == session::appcommands::kNextPatch)
            result.addDefaultKeypress(juce::KeyPress::pageDownKey, juce::ModifierKeys());
        else if (id == session::appcommands::kPreviousPatch)
            result.addDefaultKeypress(juce::KeyPress::pageUpKey, juce::ModifierKeys());
        else if (id == session::appcommands::kSave)
            result.addDefaultKeypress('s', juce::ModifierKeys::commandModifier); // Ctrl+S / Cmd+S
        else if (id == session::appcommands::kUndo)
            result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier); // Ctrl+Z / Cmd+Z
        else if (id == session::appcommands::kRedo)
        {
            result.addDefaultKeypress('y', juce::ModifierKeys::commandModifier); // Ctrl+Y
            result.addDefaultKeypress(
                'z', juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier));
        }
    }

    bool perform(const InvocationInfo& info) override
    {
        const auto* c = session::findAppCommandByJuceId(info.commandID);
        return c != nullptr && session::invokeAppCommand(ctx_.bankDocument, c->id, &ctx_.editHistory);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        transportBar_.setBounds(bounds.removeFromTop(kTransportBarHeight));
        statusBar_.setBounds(bounds.removeFromBottom(kStatusBarHeight));

        // Bank | canvas | plugin browser. A panel is docked when open on a wide
        // window, and otherwise an icon strip whose toggle slides it open over
        // the canvas (plan KD3).
        // The canvas re-centres itself when the viewport's size changes.
        const auto panels = body::layout(bounds,
                                         patchesOpen_ ? body::Panel::Open : body::Panel::Collapsed,
                                         browserOpen_ ? body::Panel::Open : body::Panel::Collapsed);
        if (!panels.narrow)
            patchesOverlay_ = browserOverlay_ = false;
        viewport_.setBounds(panels.canvas);

        patchRail_.setVisible(panels.leftFolded);
        if (panels.leftFolded)
            patchRail_.setBounds(panels.left);

        const bool docked = !panels.leftFolded;
        const bool overlaid = panels.leftFolded && patchesOverlay_;
        bankSidebar_.setVisible(docked || overlaid);
        if (docked)
        {
            bankSidebar_.setBounds(panels.left);
        }
        else if (overlaid)
        {
            bankSidebar_.setBounds(body::overlayFor(panels, /*leftSide*/ true));
            bankSidebar_.toFront(true);
        }

        browserRail_.setVisible(panels.rightFolded);
        if (panels.rightFolded)
            browserRail_.setBounds(panels.right);

        const bool browserDocked = !panels.rightFolded;
        const bool browserOverlaid = panels.rightFolded && browserOverlay_;
        pluginBrowser_.setVisible(browserDocked || browserOverlaid);
        if (browserDocked)
        {
            pluginBrowser_.setBounds(panels.right);
        }
        else if (browserOverlaid)
        {
            pluginBrowser_.setBounds(body::overlayFor(panels, /*leftSide*/ false));
            pluginBrowser_.toFront(true);
        }
    }

    // The bank's three-bar toggle and File > Patch Bank. On a wide window,
    // open or collapse the docked panel (remembered); on a narrow one, slide
    // it open over the canvas or shut.
    void togglePatchBank()
    {
        if (getWidth() < body::kDefaultMetrics.foldBelow)
        {
            patchesOverlay_ = !patchesOverlay_;
            browserOverlay_ = false; // one slid-open panel at a time
        }
        else
        {
            patchesOpen_ = !patchesOpen_;
            patchesOverlay_ = false;
            ctx_.preferences.setPanelShown("patches", patchesOpen_);
        }
        resized();
    }

    // The browser's three-bar toggle and Plugins > Plugin Browser, like the
    // bank's.
    void togglePluginBrowser()
    {
        if (getWidth() < body::kDefaultMetrics.foldBelow)
        {
            browserOverlay_ = !browserOverlay_;
            patchesOverlay_ = false; // one slid-open panel at a time
        }
        else
        {
            browserOpen_ = !browserOpen_;
            browserOverlay_ = false;
            ctx_.preferences.setPanelShown("browser", browserOpen_);
        }
        resized();
    }

    // A pick, Escape or a click on the canvas closes a slid-open panel.
    void closeOverlays()
    {
        if (!patchesOverlay_ && !browserOverlay_)
            return;
        patchesOverlay_ = browserOverlay_ = false;
        resized();
    }

private:
    AppContext& ctx_;
    FileActions files_; // before transportBar_, which holds a reference to it
    SignalCanvas canvas_;
    juce::Viewport viewport_;
    TransportBar transportBar_;
    StatusBar statusBar_;
    BankSidebar bankSidebar_;
    PatchRail patchRail_;
    bool patchesOpen_{true};     // docked, or collapsed to its strip
    bool patchesOverlay_{false}; // a folded panel slid open over the canvas
    PluginBrowser pluginBrowser_;
    BrowserRail browserRail_;
    bool browserOpen_{true};
    bool browserOverlay_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RootComponent)
};

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

    // Finish pass U3: file.save (Ctrl/Cmd+S, or a mapped trigger) saves through
    // the File workflow, and the title follows the board's file and unsaved
    // changes.
    ctx_.bankDocument.onSaveRequested = [this] { root_->files().save(); };
    ctx_.bankDocument.addChangeListener(this);
    updateTitle();

    centreWithSize(kInitialWidth, kInitialHeight);
    restoreWindowPosition();
    setVisible(true);
}

MainWindow::~MainWindow()
{
    // Finish pass U6: remember where the window was. Preferences saves it on
    // teardown (App::shutdown destroys this window before Preferences).
    ctx_.preferences.setWindowState(getWindowStateAsString());
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

void MainWindow::updateTitle()
{
    const auto& doc = ctx_.bankDocument;
    setName(juce::String(doc.isModified() ? "* " : "") + boardDisplayName(doc.currentFile()) + " - " + appName_);
}

void MainWindow::restoreWindowPosition()
{
    const auto saved = ctx_.preferences.getWindowState();
    if (saved.isEmpty() || !restoreWindowStateFromString(saved))
        return;

    // The title bar has to land on a display the user can reach; after a
    // monitor is unplugged the saved spot can be off every screen.
    const auto titleStrip = getBounds().withHeight(kTransportBarHeight).reduced(kTransportBarHeight, 0);
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
        if (display.userArea.intersects(titleStrip))
            return;
    centreWithSize(kInitialWidth, kInitialHeight);
}

} // namespace pedalboard3::ui
