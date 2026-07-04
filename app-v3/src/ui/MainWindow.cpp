#include "MainWindow.h"

#include "AppContext.h"
#include "ColourScheme.h"
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
class MainWindow::RootComponent : public juce::Component
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
    : juce::DocumentWindow(name, tokens::bgWindow, juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(/*shouldBeResizable*/ true, /*useBottomRightCornerResizer*/ false);

    root_ = std::make_unique<RootComponent>(context);
    setContentNonOwned(root_.get(), /*resizeToFitWhenContentChangesSize*/ true);

    centreWithSize(kInitialWidth, kInitialHeight);
    setVisible(true);
}

MainWindow::~MainWindow()
{
    // setContentNonOwned holds a raw pointer; drop the window's content
    // ref before root_ is destroyed so paint() doesn't fire on a moved
    // component during teardown.
    setContentNonOwned(nullptr, false);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace pedalboard3::ui
