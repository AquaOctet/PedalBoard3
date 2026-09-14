#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::ui
{

class Pb3DialogLookAndFeel;

// The dialog title bar's layout (UI chrome plan U4), shared by the LookAndFeel
// that draws it and the tests: the close button a square at the right, the
// title left-aligned before it.
namespace dialogframe
{

inline constexpr int kTitleBarHeight = 28;
inline constexpr int kTitlePadding = 12;

struct TitleBarLayout
{
    juce::Rectangle<int> title;
    juce::Rectangle<int> close;
};

inline TitleBarLayout titleBarLayout(juce::Rectangle<int> bar)
{
    TitleBarLayout layout;
    layout.close = bar.removeFromRight(bar.getHeight());
    layout.title = bar.withTrimmedLeft(kTitlePadding).withTrimmedRight(kTitlePadding / 2);
    return layout;
}

} // namespace dialogframe

// Every dialog's window (UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U4 / KD2): the mockup's
// frame in place of the native title bar. A 28 px dark title bar with the
// title on the left and a close cross, a 1 px black edge, and the bgWindow
// body. Dragging the title bar moves it; the cross and Escape call
// closeButtonPressed(). The main window and plugin editors keep their native
// title bars.
//
// Inside, fields, buttons, toggles, sliders, scrollbars and tabs follow the
// mockup through the frame's own LookAndFeel (Pb3DialogLookAndFeel), so the
// main window's controls keep their look.
//
// launchModal() opens a modal dialog that deletes itself when closed, as
// DialogWindow::LaunchOptions::launchAsync did. Windows with an owner (the
// mappings windows) derive from it and override closeButtonPressed().
class DialogFrame : public juce::DocumentWindow
{
public:
    // `addToDesktop` false keeps it off the screen (the UI snapshots tool).
    explicit DialogFrame(const juce::String& title, bool addToDesktop = true);
    ~DialogFrame() override;

    // Opens `content` in a modal dialog that deletes itself when closed.
    static void launchModal(const juce::String& title, std::unique_ptr<juce::Component> content, bool resizable);

    // A modal dialog closes; any other frame hides.
    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress&) override;

    // New content gets a LookAndFeel change: it was built, and may have laid
    // itself out (tab widths, say), before it was in the frame, with the app's.
    void childrenChanged() override;

private:
    std::unique_ptr<Pb3DialogLookAndFeel> lookAndFeel_;
    juce::Component::SafePointer<juce::Component> lastContent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialogFrame)
};

} // namespace pedalboard3::ui
