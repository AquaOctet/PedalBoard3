#include "MainWindow.h"

#include "AppContext.h"
#include "AppMappingsWindow.h"
#include "ColourScheme.h"
#include "session/AppCommands.h"
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
class MainWindow::RootComponent : public juce::Component, public juce::ApplicationCommandTarget
{
public:
    explicit RootComponent(AppContext& ctxRef)
        : ctx_(ctxRef), canvas_(ctx_), transportBar_(ctx_, canvas_), statusBar_(ctx_)
    {
        addAndMakeVisible(transportBar_);
        addAndMakeVisible(viewport_);
        addAndMakeVisible(statusBar_);

        viewport_.setViewedComponent(&canvas_, /*deleteComponentWhenNoLongerNeeded*/ false);
        // Both axes scroll now that cards are freely positioned (a card can be
        // dragged anywhere); this also makes the mouse wheel pan the canvas
        // (wheel = vertical, shift+wheel = horizontal — Viewport defaults).
        viewport_.setScrollBarsShown(/*vert*/ true, /*horiz*/ true);

        setSize(kInitialWidth, kInitialHeight);
    }

    void paint(juce::Graphics& g) override { g.fillAll(tokens::bgWindow); }

    TransportBar& transportBar() noexcept { return transportBar_; }

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
        result.setInfo(c->name, c->name, "Patch", 0);
        // Page Down / Page Up: what a Bluetooth page-turner foot pedal sends
        // (plan KD3).
        if (juce::String(c->id) == session::appcommands::kNextPatch)
            result.addDefaultKeypress(juce::KeyPress::pageDownKey, juce::ModifierKeys());
        else if (juce::String(c->id) == session::appcommands::kPreviousPatch)
            result.addDefaultKeypress(juce::KeyPress::pageUpKey, juce::ModifierKeys());
    }

    bool perform(const InvocationInfo& info) override
    {
        const auto* c = session::findAppCommandByJuceId(info.commandID);
        return c != nullptr && session::invokeAppCommand(ctx_.bankDocument, c->id);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        transportBar_.setBounds(bounds.removeFromTop(kTransportBarHeight));
        statusBar_.setBounds(bounds.removeFromBottom(kStatusBarHeight));
        viewport_.setBounds(bounds);
    }

private:
    AppContext& ctx_;
    SignalCanvas canvas_;
    juce::Viewport viewport_;
    TransportBar transportBar_;
    StatusBar statusBar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RootComponent)
};

MainWindow::MainWindow(const juce::String& name, AppContext& context)
    : juce::DocumentWindow(name, tokens::bgWindow, juce::DocumentWindow::allButtons), ctx_(context)
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

    centreWithSize(kInitialWidth, kInitialHeight);
    setVisible(true);
}

MainWindow::~MainWindow()
{
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

void MainWindow::changeListenerCallback(juce::ChangeBroadcaster*)
{
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

} // namespace pedalboard3::ui
