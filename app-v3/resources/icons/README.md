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

**The app icon** is `windows/Pedalboard3.ico` on Windows and `macos/AppIcon.icns` on
macOS -- the finished files, with all ten and all seven drawn sizes in them.

JUCE has no argument for a finished icon file. `juce_add_gui_app` converts `ICON_BIG` and
`ICON_SMALL` at configure time into its own `icon.ico` / `AppIcon.icns`, and by the time it
returns, that path is already written into the info file `juceaide` reads afterwards, so
overriding the target property is too late. `pb3_use_drawn_icon_file` in
`app-v3/src/CMakeLists.txt` therefore writes the drawn file over the one JUCE just
generated, checking the folder and the name first so that a JUCE upgrade which moves either
stops the build instead of quietly shipping the downscales again.

`ICON_BIG` and `ICON_SMALL` are still passed, because without them JUCE writes no icon file
to replace, and on Linux they are all there is:

- `ICON_BIG` = `master/pedalboard3-mac-1024.png` on macOS, `juce/icon-big.png` elsewhere.
  Apple's tile needs the transparent margin macOS expects around it; Windows and Linux
  want the square full-bleed.
- `ICON_SMALL` = `linux/hicolor/32x32/apps/pedalboard3.png`, the hand-drawn 32 px art. JUCE
  picks, per output size, the smaller of the two images that is still big enough.

`tools/package.ps1` gives Inno Setup the same `windows/Pedalboard3.ico`, so the installer
wizard and the Add/Remove Programs entry show every drawn size too.

One trap worth knowing about on Windows: the generated `.rc` names the icon file rather
than containing it, so its text does not change when the icon does, and CMake's Ninja
custom commands `restat`. Replacing the `.ico` without touching the `.rc` leaves the
resource compiler un-run and the previous icon inside the exe, through a build that reports
no problem at all. `pb3_use_drawn_icon_file` touches it.

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
