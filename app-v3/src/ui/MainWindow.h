#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

namespace pedalboard3
{
struct AppContext;
}

namespace pedalboard3::ui
{

class AppMappingsWindow;

// Path γ MainWindow — a thin juce::DocumentWindow shell whose content
// is a vertical stack of TransportBar (h:48), juce::Viewport wrapping
// SignalCanvas (flex:1), and StatusBar (h:32). All UI orchestration
// (Add Plugin entry, Test Tone, Audio Settings dialog, plugin-list
// dialog, signal canvas, editor windows) is owned by those child
// components. MainWindow itself handles close -> quit and owns the app
// commands (roadmap M4 U5): a juce::ApplicationCommandManager whose target is
// the root component (next / previous patch through session::AppCommands,
// default keys Page Down / Page Up), key mappings restored from and saved to
// Preferences on every change, and the App Mappings window that edits them.
// The title shows the board and an unsaved marker ("* Gig - Pedalboard 3"),
// and quit goes through the save prompt (finish pass U3 / U4).
class MainWindow : public juce::DocumentWindow, private juce::ChangeListener
{
public:
    MainWindow(const juce::String& name, AppContext& context);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // Quit through the save prompt (finish pass KD4): Save / Don't Save /
    // Cancel when the board has unsaved changes. App routes
    // systemRequestedQuit (the close button, Cmd+Q) here.
    void requestQuit();

    // Called when quitting goes ahead; `discardUnsaved` is true after Don't
    // Save. App installs it and reverts the persisted session (KD7).
    std::function<void(bool discardUnsaved)> onQuitConfirmed;

private:
    // The bank document changed (title) or the key mappings did (persist them).
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void openAppMappings();
    void updateTitle();

    class RootComponent;
    AppContext& ctx_;
    juce::String appName_;
    // Declared before root_ and the window: both reference it, so it must be
    // destroyed after them.
    juce::ApplicationCommandManager commandManager_;
    std::unique_ptr<RootComponent> root_;
    std::unique_ptr<AppMappingsWindow> appMappingsWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace pedalboard3::ui
