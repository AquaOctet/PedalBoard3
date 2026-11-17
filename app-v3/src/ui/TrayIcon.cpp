#include "TrayIcon.h"

#include "Pb3TrayIcons.h"
#include "TrayArt.h"

namespace pedalboard3::ui
{

namespace
{
// The pixel height the notification area is drawing at. It asks for the
// system's small-icon size, which is 16 logical pixels scaled by the display:
// 20 at 125%, 24 at 150%, 32 at 200%. JUCE builds the native icon at whatever
// size the image happens to be and leaves the shell to rescale, so getting
// this right is what makes the hand-drawn art arrive intact.
int trayHeightPx()
{
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return juce::roundToInt(16.0 * display->scale);

    return 16;
}

juce::Image trayColourArt()
{
    switch (trayArtSizeFor(trayHeightPx()))
    {
    case 16:
        return juce::ImageCache::getFromMemory(pb3tray::traycolor16_png, pb3tray::traycolor16_pngSize);
    case 20:
        return juce::ImageCache::getFromMemory(pb3tray::traycolor20_png, pb3tray::traycolor20_pngSize);
    case 24:
        return juce::ImageCache::getFromMemory(pb3tray::traycolor24_png, pb3tray::traycolor24_pngSize);
    case 32:
        return juce::ImageCache::getFromMemory(pb3tray::traycolor32_png, pb3tray::traycolor32_pngSize);
    case 40:
        return juce::ImageCache::getFromMemory(pb3tray::traycolor40_png, pb3tray::traycolor40_pngSize);
    default:
        return juce::ImageCache::getFromMemory(pb3tray::traycolor48_png, pb3tray::traycolor48_pngSize);
    }
}
} // namespace

TrayIcon::TrayIcon(std::function<void()> onShow, std::function<void()> onQuit)
    : onShow_(std::move(onShow)), onQuit_(std::move(onQuit))
{
    const auto colour = trayColourArt();

    // The second argument is the template image, and it is what macOS uses in
    // the menu bar: a black-and-transparent silhouette the system tints for
    // light and dark menu bars, which JUCE marks as a template for us. It is a
    // separate drawing, not the colour one -- a menu bar given colour artwork
    // shows a photo-like blob. Windows and Linux ignore it, and ignore the
    // colour one on macOS, so both are always passed.
    const auto templateArt =
        juce::ImageCache::getFromMemory(pb3tray::TrayIconTemplate_png, pb3tray::TrayIconTemplate_pngSize);

    if (colour.isValid() && templateArt.isValid())
    {
        setIconImage(colour, templateArt);
        setIconTooltip("Pedalboard 3");
    }

    // There is no API that answers "did the desktop accept this icon", so the
    // peer is the proxy: on a session with no tray host, JUCE creates no
    // native window for the component and getPeer() stays null. A false
    // negative costs a quit the user asked a setting to avoid; a false
    // positive strands them with no window. The cheap failure is the right
    // one to risk.
    available_ = colour.isValid() && templateArt.isValid() && getPeer() != nullptr;
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
