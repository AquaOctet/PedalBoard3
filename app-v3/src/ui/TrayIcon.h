#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace pedalboard3::ui
{

// The system tray icon the main window hides into (#147).
//
// One class for all three platforms, because juce::SystemTrayIconComponent
// already is one: a notification-area icon on Windows, a menu bar extra on
// macOS, and an appindicator item on Linux. What differs is whether the host
// desktop provides somewhere to put it, which `isAvailable` answers and
// CloseAction.h acts on.
class TrayIcon : public juce::SystemTrayIconComponent
{
public:
    // `onShow` brings the window back; `onQuit` goes through the save prompt,
    // exactly as Quit from the menu bar does. Both are called on the message
    // thread.
    TrayIcon(std::function<void()> onShow, std::function<void()> onQuit);
    ~TrayIcon() override;

    // False when the desktop gave us nowhere to live. Closing must then quit
    // rather than hide: see CloseAction.h.
    bool isAvailable() const noexcept { return available_; }

    // Said once per run, the first time the window hides. People reach for the
    // close button meaning "stop", and an app that answers by disappearing
    // while still holding the audio device has to say so -- otherwise the next
    // thing they do is wonder why their interface is busy.
    void announceFirstHide();

private:
    void mouseDown(const juce::MouseEvent&) override;
    void showMenu();

    std::function<void()> onShow_;
    std::function<void()> onQuit_;
    bool available_{false};
    bool announced_{false};
};

} // namespace pedalboard3::ui
