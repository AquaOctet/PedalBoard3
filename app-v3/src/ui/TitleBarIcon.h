#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

// Which way the window's title bar is painted, and so which single-colour icon
// belongs in its corner.
enum class TitleBarTone
{
    Light, // a light title bar: the black drawing
    Dark   // dark, or painted in the accent colour: the white drawing
};

// Windows paints the title bar light only when the app theme is light and the
// accent colour is not being used on title bars.
//
// The accent counts as dark whatever the theme says. Windows only offers accent
// colours dark enough to carry white caption text, so a black glyph on one
// disappears -- and that combination (light theme, accent title bars) is a
// common enough setup to be worth getting right rather than discovering.
constexpr TitleBarTone titleBarToneFor(bool appsUseLightTheme, bool colorPrevalence) noexcept
{
    return (appsUseLightTheme && !colorPrevalence) ? TitleBarTone::Light : TitleBarTone::Dark;
}

// The small icon in the top-left corner of the main window's title bar
// (Windows only; a no-op elsewhere).
//
// Windows draws two icons for a window from two different places: the title
// bar's corner takes ICON_SMALL, while the taskbar and Alt+Tab take ICON_BIG
// and fall back to the executable's own icon. Setting only ICON_SMALL puts the
// single-colour drawing where a 16 px full-colour icon turns to mush, and
// leaves the colour icon everywhere it has room to be itself.
//
// Which is why this does not go through juce::DocumentWindow::setIcon: that
// sets both, so the taskbar would lose the colour icon too.
//
// macOS has no title-bar icon at all, and a Linux window manager draws the
// taskbar from the same window icon, so neither has anything to set here.
class TitleBarIcon
{
public:
    // `window` must already be on the desktop: the icon goes to its peer's
    // native handle, and there is no handle before that.
    explicit TitleBarIcon(juce::Component& window);
    ~TitleBarIcon();

    // Re-reads the theme and the window's DPI and sets the icon again. Called
    // for you when Windows says either has changed.
    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TitleBarIcon)
};

} // namespace pedalboard3::ui
