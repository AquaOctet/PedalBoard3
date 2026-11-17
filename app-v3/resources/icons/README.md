# Pedalboard 3 icon set

Colours: indigo `#1C1A30`/`#2F2B4F`, off-white `#F4F1FA`, amber gradient `#FFD36A` -> `#F28A1A`.

Sizes up to 32 px (app, tray and menu bar) are hand-drawn pixel art and 40-64 px are
simplified drawings, not downscales of the big icon. `windows/_layers/` holds the PNG
layers that make up the `.ico` files, and the small sizes are byte-identical across
`windows/_layers/`, `linux/hicolor/`, `linux/tray/` and `macos/AppIcon.iconset/` -- the
same drawing, filed under each platform's own name.

| Where | File |
|---|---|
| macOS Dock / Finder | `macos/AppIcon.icns` (or build it from `macos/AppIcon.iconset` with `iconutil -c icns AppIcon.iconset` on a Mac, which is the most complete) |
| macOS menu bar status item | `macos/menubar/TrayIconTemplate.png` + `@2x` / `@3x`. These are template images: black and transparent only, so macOS tints them for light and dark menu bars. |
| Windows taskbar / exe / shortcuts | `windows/Pedalboard3.ico` (16-256 px; 32 px and below use a simplified drawing) |
| Windows notification area (tray) | `windows/tray.ico` (full colour, 16-48 px; works on light and dark taskbars) |
| Linux app icon | `linux/hicolor/**` -> copy to `/usr/share/icons/hicolor/` (or `~/.local/share/icons/hicolor/`) and use `Icon=pedalboard3` in the `.desktop` file |
| Linux tray (KDE, GNOME with AppIndicator, XFCE, etc.) | `linux/tray/pedalboard3-tray-{16,22,24,32,48}.png` (full colour) |
| JUCE `ICON_BIG` / `ICON_SMALL` | `juce/icon-big.png`, `juce/icon-small.png` |
| Masters | `master/*.svg` (Mac tile with margin, full-bleed square, one-colour glyph) |

## What the build actually uses

`app-v3/src/CMakeLists.txt` is the only consumer; the comments there carry the reasoning,
and this is the index.

**The app icon** goes through `juce_add_gui_app`, which takes exactly two images and
converts them at configure time -- `AppIcon.icns` inside the bundle on macOS, `icon.ico`
compiled into the exe's resources on Windows, and nothing at all on Linux.

- `ICON_BIG` = `master/pedalboard3-mac-1024.png` on macOS, `juce/icon-big.png` elsewhere.
  Apple's tile needs the transparent margin macOS expects around it; Windows and Linux
  want the square full-bleed.
- `ICON_SMALL` = `linux/hicolor/32x32/apps/pedalboard3.png`, the hand-drawn 32 px art, on
  every platform. JUCE picks, per output size, the smaller of the two images that is still
  big enough, so this lands in the 32 px slot exactly as drawn and the 16 px slot becomes a
  halving of it rather than of a 1024 px render.

JUCE has no way to accept a finished `.ico` or `.icns`, so `windows/Pedalboard3.ico` and
`macos/AppIcon.icns` do not reach the exe or the bundle. `tools/package.ps1` does hand
`windows/Pedalboard3.ico` to Inno Setup, which takes a file, so the installer wizard and
the Add/Remove Programs entry show every drawn size.

**The tray icon** (`app-v3/src/ui/TrayIcon.cpp`, `ui/TrayArt.h`) is embedded as binary
data, because the app-icon conversion above leaves nothing to draw with at runtime:

- Windows and Linux: the colour ladder `windows/_layers/tray-color-{16,20,24,32,40,48}.png`.
  The shell asks for the icon at the tray's own pixel size, so `ui/TrayArt.h` picks the
  drawing that matches instead of letting one image be rescaled into every slot.
- macOS: `macos/menubar/TrayIconTemplate.png` (18 px) as the second argument to
  `setIconImage`, which is the one macOS uses. JUCE sets `NSImage.isTemplate` on it for us.
  The `@2x` and `@3x` files are not embedded: JUCE's `setIconImage` takes a plain
  `juce::Image` with no scale factor, so a 36 px image would become a 36 pt menu bar item
  rather than a sharp 18 pt one. They are kept here for whenever that is worth working
  around.

## Linux packaging

There is none yet -- no `install()` rules, no `.desktop` file, and `ICON_BIG` does nothing
on Linux. When packaging lands it needs:

1. `install(DIRECTORY linux/hicolor/ DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor)`,
   keeping the `<size>/apps/pedalboard3.png` layout (and `scalable/apps/pedalboard3.svg`).
2. A `pedalboard3.desktop` file with `Icon=pedalboard3`, `Exec=pedalboard3`,
   `Categories=AudioVideo;Audio;` installed into `${CMAKE_INSTALL_DATADIR}/applications`.
3. `gtk-update-icon-cache` on the hicolor theme in the package's post-install step.

Until then a Linux window carries no icon at all, since a JUCE window on Linux takes its
icon from the desktop's theme lookup rather than from anything compiled in.
