# The icon system

Status: adopted 2026-10-19 (#125). Supersedes the ad-hoc icons added for #90
and #124.

The plugin cards' icons came out confusing and blurry. Three measurable
faults, not taste:

1. **Rendered at 8×8.** The top bar gives a 36 px button an inset of 10, so
   16 px of icon. The card gave a 22 px button an inset of 7, so **8 px** —
   a power ring, a gap and a stem in a box the size of a lower-case letter.
2. **Fixed stroke.** `strokeIcon` used `1.17f` whatever it drew into: 7% of
   a 16 px glyph, **15%** of an 8 px one. Enclosed shapes filled in and the
   power ring became a dot.
3. **Off the pixel grid.** Paths are authored on a 24-unit grid, scaled to a
   14-unit box, then scaled again to fit the button. Two arbitrary factors in
   series put every run at a fractional coordinate, and a 1.17 px stroke
   centred at x=5.3 spreads over three columns at a third intensity each.

## The rules

**R1. Never render below 14 px.** Below that, use a single-feature glyph (a
cross, a dot, a chevron) or a word. Card buttons are 22 px with a 4 px inset,
so a 14 px box; the hit target stays 22 px.

**R2. Stroke is a fraction of the box.** `thickness = box / 11`, clamped to
[1.0, 1.8] — 9.1% at both 14 and 16 px. A constant ratio is what makes two
sizes look like one family.

**R3. Snap to the pixel grid.** `strokeIcon` translates so odd-width strokes
land on half-pixel centres. A 1 px line at y=10.5 covers one row; at y=10.0
it covers two at half intensity.

**R4. Detail budget**, in units of the 24-grid (scale ≈ 0.58 at a 14 px box):

| 24-grid | at 14 px | meaning |
|---|---|---|
| 2 | 1.2 px | the stroke itself |
| 3 | 1.75 px | minimum gap between strokes |
| 4 | 2.3 px | minimum readable dot or hole |
| 6 | 3.5 px | minimum shape that must be *identified* |

**R5. Three features maximum**, counting distinct closed shapes or strokes.
Three sliders with three handles is six, which is why that one failed.

**R6. Optical size comes from the inset, never the path.** `strokeIcon`
scales a glyph's **own** bounding box to fill the box it is given, so the
extent you draw at has no effect: a cross authored 6..18 and the same cross
authored 4.5..19.5 render identically. A glyph that looks too big is fixed by
giving that button a larger inset.

The cross needs it. A diagonal inscribed in a square reaches 1.41× the radius
of an inscribed circle, so at a shared inset it is visibly the largest thing
in the row. It gets an inset of 5.5 against the others' 4, an 11 px box.

*Corollary:* the contact sheet must render each glyph at its own box, not at
one uniform size, or it hides the correction it exists to check.

**R7. Silhouette before detail.** Squint at it as a filled blob. If the blob
could be three other icons in the set, the drawing is doing work the shape
should do.

## The set

Authored on the 24-unit grid, origin top-left, nominal 2-unit stroke.

| glyph | means | shape | features |
|---|---|---|---|
| **power** | Bypass | ring open at the top, stem through the gap | 2 |
| **faders** | Mappings | two tracks, one square handle each, offset | 2 |
| **plug** | Open the plugin's interface | two prongs, a body, a cord | 3 |
| **cross** | Remove | two diagonals, 11 px box (R6) — not on the card | 1 |

**Power** is the hardware and DAW convention, so it needs no learning. The
ring runs 300° to 240°: a 60° gap is a visible notch at 14 px.

**Faders** replaced three sliders with round handles — two tracks for R5, and
square handles because a 4-unit square keeps its corners at 2.3 px where a
4-unit circle smudges (R4). Two handles at different positions is what says
"controls that move", which is what a mapping is.

**Plug** was a window, and at 14 px its title rule is one pixel, leaving a
rectangle with a lighter top edge: a folder. A plug reads at any size (R7) and
is the same glyph the top bar uses for the Plugins menu, which is a feature.

**The cross is no longer on the card.** It appeared on hover in the exact
position your hand was travelling to for the other three, so reaching for the
editor put a delete under the cursor. Removing lives in the right-click menu
(with "Del" beside it) and on the Delete key. The glyph stays in the set.

### Rejected

| design | why not |
|---|---|
| lucide `sliders-horizontal` verbatim | three tracks, three round 4-unit handles: six features, and they smudge (R4, R5) |
| a chain link for mappings | the canvas already uses cables for connection |
| a MIDI DIN for mappings | mappings are MIDI *or* OSC |
| a pencil for the editor | implies renaming, a different action on the same card |
| a bin for remove | says "deleted forever" when Ctrl+Z brings it back |
| a window for the editor | reads as a folder at 14 px |

## Verifying

`build.ps1 snapshots` renders `icons-card` and `icons-bar`: every glyph at its
real size, and again at 4×. Judge the real-size row — a glyph you can't tell
from its neighbour there has failed however good it looks at 4×.
