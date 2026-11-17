#pragma once

#include <array>

namespace pedalboard3::ui
{

// Which of the embedded tray drawings to hand the system (#147).
//
// The artwork is in resources/icons; CMake embeds the colour ladder as
// pb3_ui_tray_icons. The choice is here, away from the component, because it
// is the part that can be wrong without anyone noticing: a tray icon that is
// merely the wrong size still appears, just blurred.

// The sizes, in pixels, of the embedded colour artwork. Ascending.
inline constexpr std::array<int, 6> kTraySizes{16, 20, 24, 32, 40, 48};

// The drawing to use for a tray slot `wantedPx` pixels tall.
//
// The smallest one that is at least as big. These are hand-drawn pixel art,
// not renders: shrinking 20 into 16 loses detail, but stretching 16 into 20
// loses the drawing itself, because every edge then lands between pixels.
// Above the ladder the largest is all there is, and a 48 stretched to 64 is
// still better than a 16 stretched to 64.
constexpr int trayArtSizeFor(int wantedPx) noexcept
{
    for (const auto size : kTraySizes)
        if (size >= wantedPx)
            return size;

    return kTraySizes.back();
}

} // namespace pedalboard3::ui
