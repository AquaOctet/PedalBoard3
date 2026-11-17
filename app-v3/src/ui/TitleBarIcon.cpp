#include "TitleBarIcon.h"

#if JUCE_WINDOWS
#include "Pb3TrayIcons.h"

#include <windows.h>

#include <map>
#endif

namespace pedalboard3::ui
{

#if JUCE_WINDOWS

namespace
{
// A DWORD under HKCU, defaulting to `fallback` when the value is missing --
// which it is on a fresh profile, and on every Windows before the setting
// existed.
bool readUserDword(const wchar_t* key, const wchar_t* value, bool fallback)
{
    DWORD data{};
    DWORD size = sizeof(data);
    const auto result = RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size);
    return result == ERROR_SUCCESS ? data != 0 : fallback;
}

TitleBarTone currentTone()
{
    // Light is Windows' own default for both, so a missing value means light.
    // Raw literals: a registry path is all backslashes, and an escaped one is
    // a silent wrong answer rather than a build failure -- a key that does not
    // resolve reads as "setting absent", which is a perfectly ordinary state.
    const auto appsUseLightTheme =
        readUserDword(LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)", L"AppsUseLightTheme", true);
    const auto colorPrevalence = readUserDword(LR"(Software\Microsoft\Windows\DWM)", L"ColorPrevalence", false);
    return titleBarToneFor(appsUseLightTheme, colorPrevalence);
}

// The .ico for a tone, as bytes. Both hold 16, 20, 24, 32, 40 and 48 px drawn
// by hand, which is the ladder SM_CXSMICON walks across the DPI settings.
const char* iconBytesFor(TitleBarTone tone, int& size)
{
    if (tone == TitleBarTone::Light)
    {
        size = pb3tray::trayblack_icoSize;
        return pb3tray::trayblack_ico;
    }
    size = pb3tray::traywhite_icoSize;
    return pb3tray::traywhite_ico;
}

// The size Windows will draw the title bar's icon at, for this window's
// monitor: 16 px at 100%, 20 at 125%, 24 at 150%, 32 at 200%.
int smallIconSizeFor(HWND hwnd)
{
    const auto dpi = GetDpiForWindow(hwnd);
    if (dpi == 0)
        return GetSystemMetrics(SM_CXSMICON); // no per-monitor answer: the system one
    return GetSystemMetricsForDpi(SM_CXSMICON, dpi);
}

// The best-fitting image out of an in-memory .ico, as an icon Windows can
// draw.
//
// LookupIconIdFromDirectoryEx picks the image, CreateIconFromResourceEx builds
// it at the size asked for. This is the from-memory pair for
// LoadIconWithScaleDown, which only reads files and resources -- and the icons
// are embedded as binary data here, like every other piece of artwork this app
// draws at runtime.
HICON createIconFromIco(const char* bytes, int length, int size)
{
    if (bytes == nullptr || length <= 0)
        return nullptr;

    auto* directory = reinterpret_cast<PBYTE>(const_cast<char*>(bytes));
    const auto offset = LookupIconIdFromDirectoryEx(directory, TRUE, size, size, LR_DEFAULTCOLOR);
    if (offset <= 0 || offset >= length)
        return nullptr;

    return CreateIconFromResourceEx(
        directory + offset, static_cast<DWORD>(length - offset), TRUE, 0x00030000, size, size, LR_DEFAULTCOLOR);
}
} // namespace

class TitleBarIcon::Impl
{
public:
    explicit Impl(juce::Component& window) : window_(window)
    {
        attach();
        refresh();
    }

    ~Impl()
    {
        detach();

        if (icon_ != nullptr)
        {
            // Clear it first: destroying an icon the window is still drawing
            // leaves it drawing freed memory.
            if (IsWindow(hwnd_))
                SendMessage(hwnd_, WM_SETICON, ICON_SMALL, 0);
            DestroyIcon(icon_);
        }
    }

    void refresh()
    {
        // The peer is recreated by things like a move between desktops, and
        // the old handle's subclass goes with it.
        if (auto* peer = window_.getPeer(); peer != nullptr && peer->getNativeHandle() != hwnd_)
        {
            detach();
            attach();
        }

        if (hwnd_ == nullptr)
            return;

        const auto tone = currentTone();
        const auto size = smallIconSizeFor(hwnd_);
        if (icon_ != nullptr && tone == tone_ && size == size_)
            return;

        int length = 0;
        const auto* bytes = iconBytesFor(tone, length);
        auto* icon = createIconFromIco(bytes, length, size);
        if (icon == nullptr)
            return; // keep whatever is up rather than clearing the corner

        SendMessage(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));

        if (icon_ != nullptr)
            DestroyIcon(icon_); // only once the window has stopped using it
        icon_ = icon;
        tone_ = tone;
        size_ = size;
    }

private:
    void attach()
    {
        auto* peer = window_.getPeer();
        if (peer == nullptr)
            return;

        hwnd_ = static_cast<HWND>(peer->getNativeHandle());
        if (hwnd_ == nullptr)
            return;

        // Subclassed rather than hooked, and chained rather than replaced:
        // this is how juce::SystemTrayIconComponent watches its own window on
        // Windows, so it is a pattern JUCE already relies on in this process.
        // WM_SETTINGCHANGE is how a theme or accent change arrives, and
        // WM_DPICHANGED how a move to another monitor does.
        original_ = reinterpret_cast<WNDPROC>(SetWindowLongPtr(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hook)));
        owners()[hwnd_] = this;
    }

    void detach()
    {
        if (hwnd_ == nullptr)
            return;

        if (original_ != nullptr && IsWindow(hwnd_))
            SetWindowLongPtr(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_));

        owners().erase(hwnd_);
        original_ = nullptr;
        hwnd_ = nullptr;
    }

    static std::map<HWND, Impl*>& owners()
    {
        static std::map<HWND, Impl*> map;
        return map;
    }

    static LRESULT CALLBACK hook(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        const auto it = owners().find(hwnd);
        if (it == owners().end())
            return DefWindowProc(hwnd, message, wParam, lParam);

        auto* self = it->second;
        const auto result = CallWindowProc(self->original_, hwnd, message, wParam, lParam);

        // After the original, so the window has already taken the new DPI.
        if (message == WM_SETTINGCHANGE || message == WM_DPICHANGED)
            self->refresh();

        return result;
    }

    juce::Component& window_;
    HWND hwnd_{nullptr};
    WNDPROC original_{nullptr};
    HICON icon_{nullptr};
    TitleBarTone tone_{TitleBarTone::Dark};
    int size_{0};
};

TitleBarIcon::TitleBarIcon(juce::Component& window) : impl_(std::make_unique<Impl>(window)) {}

TitleBarIcon::~TitleBarIcon() = default;

void TitleBarIcon::refresh()
{
    impl_->refresh();
}

#else

// macOS draws no title-bar icon, and a Linux window manager takes the title
// bar and the taskbar from the same window icon, so there is nothing here to
// set that would not also change the taskbar.
class TitleBarIcon::Impl
{
};

TitleBarIcon::TitleBarIcon(juce::Component& window)
{
    juce::ignoreUnused(window);
}

TitleBarIcon::~TitleBarIcon() = default;

void TitleBarIcon::refresh() {}

#endif

} // namespace pedalboard3::ui
