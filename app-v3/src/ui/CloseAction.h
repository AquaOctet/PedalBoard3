#pragma once

namespace pedalboard3::ui
{

// What the main window's close button does (#147).
enum class CloseAction
{
    Quit,       // through the save prompt, the way it always did
    HideToTray, // the app keeps running, audio and all
};

// Closing keeps the app running when the user asked for that *and* there is
// a tray icon to get the window back from.
//
// The second condition is the one that matters. A window that hides with
// nothing to restore it is not a setting behaving oddly, it is an app that has
// vanished with the user's audio device still claimed and no way to reach it
// short of Task Manager. Linux is where this actually bites: JUCE's tray needs
// an appindicator host and some desktop sessions have none, so the icon simply
// never appears. Quitting is the right answer whenever the way back is not
// provably there.
constexpr CloseAction closeActionFor(bool settingEnabled, bool trayAvailable) noexcept
{
    return settingEnabled && trayAvailable ? CloseAction::HideToTray : CloseAction::Quit;
}

} // namespace pedalboard3::ui
