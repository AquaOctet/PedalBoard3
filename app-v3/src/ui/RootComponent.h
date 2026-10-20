#pragma once

#include "AppContext.h"
#include "BankSidebar.h"
#include "BoardMappingsPanel.h"
#include "BodyLayout.h"
#include "FeaturePrompt.h"
#include "FileActions.h"
#include "PatchRail.h"
#include "PluginBrowser.h"
#include "session/AppCommands.h"
#include "session/BankDocument.h"
#include "session/BoardFiles.h"
#include "session/EditHistory.h"
#include "session/Preferences.h"
#include "SignalCanvas.h"
#include "StatusBar.h"
#include "TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

namespace rootlayout
{
constexpr int kTransportBarHeight = 48;
constexpr int kStatusBarHeight = 32;
constexpr int kReadOnlyBannerHeight = 26;
constexpr int kInitialWidth = 960;
constexpr int kInitialHeight = 540;
} // namespace rootlayout

// Container for the three-zone vertical layout. MainWindow owns one
// of these; everything chain-related lives inside.
// The root is also the drag container, so a row dragged out of the plugin
// browser can land on the canvas (UI refresh U5).
class RootComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget
{
public:
    explicit RootComponent(AppContext& ctxRef)
        : ctx_(ctxRef), files_(ctx_), canvas_(ctx_), transportBar_(ctx_, canvas_, files_), statusBar_(ctx_),
          bankSidebar_(ctx_), patchRail_(ctx_), pluginBrowser_(ctx_), mappingsPanel_(ctx_), featurePrompt_(ctx_)
    {
        addAndMakeVisible(transportBar_);
        addAndMakeVisible(viewport_);
        addAndMakeVisible(statusBar_);

        // A board a newer version of the app wrote opens read-only (M5 U4, plan
        // KD7); this strip under the transport bar says so.
        readOnlyBanner_.setText("Read-only: this board was saved by a newer version of Pedalboard 3. "
                                "Use Save As to keep a copy.",
                                juce::dontSendNotification);
        readOnlyBanner_.setJustificationType(juce::Justification::centred);
        readOnlyBanner_.setColour(juce::Label::backgroundColourId, tokens::accentWarning.withAlpha(0.18f));
        readOnlyBanner_.setColour(juce::Label::textColourId, tokens::accentWarning);
        addChildComponent(readOnlyBanner_);

        viewport_.setViewedComponent(&canvas_, /*deleteComponentWhenNoLongerNeeded*/ false);
        // Both axes scroll now that cards are freely positioned (a card can be
        // dragged anywhere); this also makes the mouse wheel pan the canvas
        // (wheel = vertical, shift+wheel = horizontal — Viewport defaults).
        viewport_.setScrollBarsShown(/*vert*/ true, /*horiz*/ true);

        // The body's side panel (UI refresh U4 / U6): the patch bank on the
        // left. Its three-bar toggle, top-left under File, collapses it to an
        // icon strip and opens it again at any window size (so does Window >
        // Patch Bank on macOS); below the fold width it folds on its own and the
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
        // The board mappings panel under the canvas (UI refresh U9): its
        // three-bar toggle and Plugins > Mappings Panel open it or collapse it
        // to its header, and it follows the canvas selection both ways.
        addAndMakeVisible(mappingsPanel_);
        mappingsOpen_ = ctx_.preferences.getPanelShown("mappings", true);
        mappingsPanel_.setOpen(mappingsOpen_);
        mappingsPanel_.onToggle = [this] { toggleMappingsPanel(); };
        mappingsPanel_.onOpenDialog = [this](session::NodeId id) { canvas_.openMappings(id); };
        mappingsPanel_.onSelectNode = [this](session::NodeId id) { canvas_.selectNode(id); };
        canvas_.onSelectionChanged = [this](std::optional<session::NodeId> id) { mappingsPanel_.setSelectedNode(id); };
        // Right-click the board to add a plugin, where you clicked (#120).
        // This replaced the top bar's [+], which could only ever append to
        // the end of the chain.
        canvas_.onCanvasContextMenu = [this](juce::Point<float> at) { transportBar_.openAddPluginMenuAt(at); };
        transportBar_.onToggleMappingsPanel = [this] { toggleMappingsPanel(); };
        transportBar_.isMappingsPanelShown = [this] { return mappingsOpen_; };

        // The status bar's three toggles (#90) drive the same three panels.
        // refreshPanelToggles() is called from resized(), which every path
        // that opens or closes a panel ends in.
        statusBar_.onTogglePatchBank = [this] { togglePatchBank(); };
        statusBar_.isPatchBankShown = [this] { return bankSidebar_.isVisible(); };
        statusBar_.onToggleMappingsPanel = [this] { toggleMappingsPanel(); };
        statusBar_.isMappingsPanelShown = [this] { return mappingsOpen_; };
        statusBar_.onTogglePluginBrowser = [this] { togglePluginBrowser(); };
        statusBar_.isPluginBrowserShown = [this] { return pluginBrowser_.isVisible(); };
        statusBar_.isTestToneOn = [this] { return transportBar_.isTestToneOn(); };
        transportBar_.onTestToneChanged = [this] { statusBar_.refreshTestTone(); };

        // A click on the canvas closes a slid-open panel.
        viewport_.addMouseListener(this, true);

        setSize(rootlayout::kInitialWidth, rootlayout::kInitialHeight);
    }

    ~RootComponent() override
    {
        canvas_.onSelectionChanged = nullptr; // the panel is destroyed first
        canvas_.onCanvasContextMenu = nullptr;
        viewport_.removeMouseListener(this);
    }

    // The canvas, for the UI snapshot tool: selecting a node is the only way
    // to show the selected-card chrome in a still image (#105).
    SignalCanvas& canvas() noexcept { return canvas_; }

    void mouseDown(const juce::MouseEvent&) override { closeOverlays(); }

    void paint(juce::Graphics& g) override { g.fillAll(tokens::bgWindow); }

    // ---- juce::FileDragAndDropTarget (#31): a .pdl dropped anywhere on the
    // window opens as File > Open does, after the unsaved-changes prompt.
    // Other files are ignored. The window is ringed in teal while one hovers.
    bool isInterestedInFileDrag(const juce::StringArray& paths) override
    {
        return session::boardfiles::firstBoardFile(paths) != juce::File();
    }
    void fileDragEnter(const juce::StringArray&, int, int) override { setDropHighlight(true); }
    void fileDragExit(const juce::StringArray&) override { setDropHighlight(false); }
    void filesDropped(const juce::StringArray& paths, int, int) override
    {
        setDropHighlight(false);
        if (const auto file = session::boardfiles::firstBoardFile(paths); file != juce::File())
            files_.openDropped(file);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (!dropHighlight_)
            return;
        g.setColour(tokens::accentPrimary.withAlpha(0.08f));
        g.fillRect(getLocalBounds());
        g.setColour(tokens::accentPrimary);
        g.drawRect(getLocalBounds(), 3);
    }

    TransportBar& transportBar() noexcept { return transportBar_; }
    FileActions& files() noexcept { return files_; }

    // Show or hide the read-only banner (M5 U4).
    void setReadOnly(bool readOnly)
    {
        if (readOnly == readOnlyBanner_.isVisible())
            return;
        readOnlyBanner_.setVisible(readOnly);
        resized();
    }

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
        {
            result.addDefaultKeypress('s', juce::ModifierKeys::commandModifier); // Ctrl+S / Cmd+S
            // A read-only board (M5 U4) isn't saved over; File > Save As keeps a copy.
            result.setActive(!ctx_.bankDocument.isReadOnly());
        }
        else if (id == session::appcommands::kPlayPause)
            result.addDefaultKeypress(juce::KeyPress::spaceKey, juce::ModifierKeys()); // M6 KD4
        else if (id == session::appcommands::kReturnToZero)
            result.addDefaultKeypress(juce::KeyPress::homeKey, juce::ModifierKeys());
        else if (id == session::appcommands::kUndo)
        {
            result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier); // Ctrl+Z / Cmd+Z
            result.setActive(canEdit());
        }
        else if (id == session::appcommands::kRedo)
        {
            result.addDefaultKeypress('y', juce::ModifierKeys::commandModifier); // Ctrl+Y
            result.addDefaultKeypress(
                'z', juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier));
            result.setActive(canRedo());
        }
    }

    // Undo and Redo grey out with nothing to undo or redo (#96). EditHistory
    // has said so since it was written and nothing asked. The live-capture
    // check is the same one invokeAppCommand makes: with the live graph off
    // the active slot the history's nodes are not live and the command would
    // refuse anyway — a menu item that does nothing when you pick it is
    // worse than a grey one.
    bool canEdit() const { return ctx_.bankDocument.isLiveCaptureEnabled() && ctx_.editHistory.canUndo(); }
    bool canRedo() const { return ctx_.bankDocument.isLiveCaptureEnabled() && ctx_.editHistory.canRedo(); }

    bool perform(const InvocationInfo& info) override
    {
        const auto* c = session::findAppCommandByJuceId(info.commandID);
        return c != nullptr &&
               session::invokeAppCommand(
                   ctx_.bankDocument, c->id, &ctx_.editHistory, &ctx_.transportCommands, &ctx_.audioEngine);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        transportBar_.setBounds(bounds.removeFromTop(rootlayout::kTransportBarHeight));
        statusBar_.setBounds(bounds.removeFromBottom(rootlayout::kStatusBarHeight));
        if (readOnlyBanner_.isVisible())
            readOnlyBanner_.setBounds(bounds.removeFromTop(rootlayout::kReadOnlyBannerHeight));

        // Bank | canvas | plugin browser. A panel is docked when open on a wide
        // window, and otherwise an icon strip whose toggle slides it open over
        // the canvas (plan KD3).
        // The canvas re-centres itself when the viewport's size changes.
        const auto panels = body::layout(bounds,
                                         patchesOpen_ ? body::Panel::Open : body::Panel::Collapsed,
                                         browserOpen_ ? body::Panel::Open : body::Panel::Collapsed);
        if (!panels.narrow)
            patchesOverlay_ = browserOverlay_ = false;
        // The board mappings panel docks under the canvas (UI refresh U9).
        const auto split = body::splitBottom(
            panels.canvas, mappingsOpen_, BoardMappingsPanel::kOpenHeight, BoardMappingsPanel::kHeaderHeight);
        viewport_.setBounds(split.canvas);
        mappingsPanel_.setBounds(split.bottom);

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

        // Last, because it reads the visibility the lines above just set.
        statusBar_.refreshPanelToggles();
    }

    // The bank's three-bar toggle, and Window > Patch Bank on macOS. On a wide window,
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

    // The mappings panel's three-bar toggle and Plugins > Mappings Panel: open
    // it or collapse it to its header (remembered).
    void toggleMappingsPanel()
    {
        mappingsOpen_ = !mappingsOpen_;
        ctx_.preferences.setPanelShown("mappings", mappingsOpen_);
        mappingsPanel_.setOpen(mappingsOpen_);
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
    BoardMappingsPanel mappingsPanel_;
    FeaturePrompt featurePrompt_; // asks when a board uses a feature that's off in Settings
    bool mappingsOpen_{true};
    bool dropHighlight_{false};  // a .pdl is being dragged over the window
    juce::Label readOnlyBanner_; // a board a newer version wrote (M5 U4)

    void setDropHighlight(bool on)
    {
        if (on == dropHighlight_)
            return;
        dropHighlight_ = on;
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RootComponent)
};

} // namespace pedalboard3::ui
