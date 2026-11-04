#pragma once

namespace pedalboard3::ui::metrics
{

// The window's measurements, in one place (UI rethink U1).
//
// They used to live as `constexpr int` blocks in each component's anonymous
// namespace, which is how the window ended up writing 12 six times, 36 five
// times, and two different numbers for the same role: the bank's footer was
// 48 px and the browser's was 44, and nothing said whether that was a
// decision or a slip. One header makes the scale visible, so a density
// change is one edit and the drift shows up as two names that should be one.
//
// What belongs here: spacing, row heights, the heights of headers and
// footers, the icon cells, and the shared radii. What does not: geometry
// that is intrinsic to one component and means nothing outside it -- a
// card's port positions, its grip width, the bar's meter widths. A metrics
// header that swallows those becomes a junk drawer and stops being a scale.
//
// U1 moved these values; it changed none of them. Every snapshot before and
// after it is identical, which is the only way to be sure.

// Spacing ------------------------------------------------------------------
// The ramp the window already used, once the duplicates were collected.
// Gaps, padding and insets come from here.

inline constexpr int kSpaceXs = 2;   // a hairline gap: mapping rows, status toggles
inline constexpr int kSpaceSm = 4;   // inside a chip or an accent inset
inline constexpr int kSpaceMd = 6;   // between buttons, between chips
inline constexpr int kSpaceLg = 8;   // a dot from its text
inline constexpr int kSpaceXl = 12;  // the standard panel and bar padding
inline constexpr int kSpaceXxl = 24; // the canvas margin

static_assert(kSpaceXs < kSpaceSm && kSpaceSm < kSpaceMd && kSpaceMd < kSpaceLg && kSpaceLg < kSpaceXl &&
                  kSpaceXl < kSpaceXxl,
              "The spacing ramp has to increase, or a 'bigger' step silently gets smaller.");

// Rows ---------------------------------------------------------------------
// By role, not by panel, because the role is what decides the height. The
// one that matters is kRowTouch: the patch list is hit during a set, on a
// laptop at arm's length, so it is sized for a glance and a stab rather than
// for density. The others are read at a desk with a mouse.

inline constexpr int kRowCompact = 20; // column headings, filter chips
inline constexpr int kRowDense = 22;   // mapping rows
inline constexpr int kRowList = 26;    // plugin browser rows
inline constexpr int kRowTouch = 40;   // patch rows -- hit while playing

// Nothing smaller than this can hold the UI font and still be clicked at
// speed. A density pass that crosses it has gone too far, and should say so
// deliberately rather than discover it on stage.
inline constexpr int kRowMinimum = 18;

static_assert(kRowCompact >= kRowMinimum && kRowDense >= kRowMinimum && kRowList >= kRowMinimum &&
                  kRowTouch >= kRowMinimum,
              "A row height has dropped below what stays legible and clickable.");
static_assert(kRowCompact < kRowDense && kRowDense < kRowList && kRowList < kRowTouch,
              "The row scale has to increase, so 'denser' and 'larger' keep meaning what they say.");

// Panel furniture ----------------------------------------------------------

inline constexpr int kPanelHeader = 36;    // a side panel's title row
inline constexpr int kPanelBoardName = 28; // the bank's board-name line
inline constexpr int kSearchHeight = 30;

// U1 found these four pixels apart with nothing recording why, and left
// them alone. U2's answer: it was a slip, and they were never the same
// thing anyway. The bank's footer holds two buttons that get clicked, so it
// stays a touch target; the browser's holds one line of hint text, so it is
// sized for text.
inline constexpr int kFooterBank = 48;    // + New / Duplicate -- clicked
inline constexpr int kFooterBrowser = 28; // one line of hint -- read

// Icons --------------------------------------------------------------------
//
// From the old ChromeMetrics.h, whose reasoning still holds and is the
// reason these are shared at all:
//
//   The top bar's icon buttons are 36 px squares inset 12 px, so their icons
//   sit 30 px in from the window edge. The side panels' corner toggles were
//   18 px icons inset 12 px, so theirs sat 21 px in. Nine pixels apart, on
//   every edge, which is close enough to look like a mistake and far enough
//   to see: the master mute and the plugin browser's toggle are vertically
//   adjacent and did not line up, and neither did the file button and the
//   bank sidebar's toggle on the other side.
//
//   One number for the lot: kIconButtonSize is the cell an icon is centred
//   in, so every icon in the window's corners sits on the same two vertical
//   lines. The bar's buttons fill that cell, because they have a hover
//   background to draw; the panels' toggles only borrow its width and keep
//   their own kIconSize glyph, so they land on the line without growing to
//   fill a header that is shorter than 36 px.
//
// That was only half true when it was written. The panels did use
// kIconButtonSize, but the top bar -- the other half of the alignment it
// describes -- kept its own kMenuButtonWidth and kMuteButtonWidth, both
// spelled 36 by hand. They point here now, so the two edges are held
// together by the constant that claims to hold them.

inline constexpr int kIconButtonSize = 36;                              // the cell an icon is centred in
inline constexpr int kIconSize = 18;                                    // the glyph inside it
inline constexpr int kIconCentreInset = kSpaceXl + kIconButtonSize / 2; // 30

static_assert(kIconSize < kIconButtonSize, "The glyph has to fit inside its cell.");

// Shape --------------------------------------------------------------------

inline constexpr float kRadiusCard = 6.0f;
inline constexpr int kBorderHairline = 1;

} // namespace pedalboard3::ui::metrics
