#include "TrayIcon.h"

#include "Pb3Icon.h"

namespace pedalboard3::ui
{

TrayIcon::TrayIcon(std::function<void()> onShow, std::function<void()> onQuit)
    : onShow_(std::move(onShow)), onQuit_(std::move(onQuit))
{
    const auto icon = juce::ImageCache::getFromMemory(pb3icon::icon_png, pb3icon::icon_pngSize);
    if (icon.isValid())
    {
        // The template image is the second argument, and it is what macOS uses
        // in the menu bar: a black-and-transparent silhouette the system tints
        // for light and dark. Passing the colour icon for both would make a
        // Mac menu bar show a photo-like blob. Windows and Linux ignore it.
        setIconImage(icon, icon);
        setIconTooltip("Pedalboard 3");
    }

    // There is no API that answers "did the desktop accept this icon", so the
    // peer is the proxy: on a session with no tray host, JUCE creates no
    // native window for the component and getPeer() stays null. A false
    // negative costs a quit the user asked a setting to avoid; a false
    // positive strands them with no window. The cheap failure is the right
    // one to risk.
    available_ = icon.isValid() && getPeer() != nullptr;
}

TrayIcon::~TrayIcon()
{
    // JUCE removes the native icon in the base destructor; naming it here is
    // only so that a future "hide but keep the object" does not get added
    // without noticing it has to.
}

void TrayIcon::announceFirstHide()
{
    if (announced_)
        return;
    announced_ = true;

    // Plain words, and the audio device is the part worth saying: it is the
    // consequence that reaches outside this app. Another program asking for
    // the same interface will be refused, and nothing on screen would explain
    // why.
    showInfoBubble("Pedalboard 3 is still running",
                   "Your board is still live and still using the audio device. "
                   "Click this icon to bring the window back, or use Quit to stop.");
}

void TrayIcon::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    if (onShow_)
        onShow_();
}

void TrayIcon::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem("Show Pedalboard 3",
                 [this]
                 {
                     if (onShow_)
                         onShow_();
                 });
    menu.addSeparator();
    // Quit here is the same quit as everywhere else -- it goes through the
    // unsaved-changes prompt. Hiding discards nothing and so asks nothing;
    // quitting can lose work and so must.
    menu.addItem("Quit",
                 [this]
                 {
                     if (onQuit_)
                         onQuit_();
                 });
#if JUCE_MAC
    // macOS only: JUCE guards showDropdownMenu with #if JUCE_MAC, because a
    // menu bar extra opens its menu through the status item rather than as a
    // free-floating popup. Using it is what makes the item stay highlighted
    // while the menu is down, the way every other menu bar extra does.
    showDropdownMenu(menu);
#else
    // Elsewhere the tray icon is not a menu host, so this is an ordinary
    // popup placed at the pointer. It picks up Pb3LookAndFeel like every other
    // menu, because that is the default look and feel for the process.
    menu.showMenuAsync(juce::PopupMenu::Options{}.withMousePosition());
#endif
}

} // namespace pedalboard3::ui
