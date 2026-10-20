#pragma once

namespace pedalboard3::ui::chrome
{

// The measurements that have to agree between the window's edges.
//
// The top bar's icon buttons are 36 px squares inset 12 px, so their icons
// sit 30 px in from the window edge. The side panels' corner toggles were
// 18 px icons inset 12 px, so theirs sat 21 px in. Nine pixels apart, on
// every edge, which is close enough to look like a mistake and far enough to
// see: the master mute and the plugin browser's toggle are vertically
// adjacent and did not line up, and neither did the file button and the bank
// sidebar's toggle on the other side.
//
// One number for the lot: kIconButtonSize is the cell an icon is centred
// in, so every icon in the window's corners sits on the same two vertical
// lines. The bar's buttons fill that cell, because they have a hover
// background to draw; the panels' toggles only borrow its width and keep
// their own kIconSize glyph, so they land on the line without growing to
// fill a header that is shorter than 36 px.
inline constexpr int kIconButtonSize = 36;
inline constexpr int kIconSize = 18;
inline constexpr int kEdgePadding = 12;
inline constexpr int kIconCentreInset = kEdgePadding + kIconButtonSize / 2; // 30

} // namespace pedalboard3::ui::chrome
