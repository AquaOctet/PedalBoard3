# The icon system

Status: adopted 2026-10-19 (#125). Supersedes the ad-hoc icons added for #90
and #124.

The plugin cards' icons came out confusing and blurry. This is why, what the
rules are now, and what each glyph is meant to say.

---

## What was actually wrong

Not taste. Three measurable faults, each of which would have been enough on
its own.

### 1. The icons were rendered at 8×8

| where | button | inset | icon box |
|---|---|---|---|
| top bar | 36 px | 10 | **16 px** |
| plugin card (before) | 22 px | 7 | **8 px** |

Eight pixels. The same artwork that reads at 16 px on the bar was being
asked to carry a power ring, a gap and a stem inside a box the size of a
lower-case letter. No amount of redrawing fixes a glyph that small; the box
has to grow.

### 2. The stroke did not scale with the box

`strokeIcon` took a fixed `1.17f` whatever it was drawing into. At the bar's
16 px that is 7% of the glyph — fine. At 8 px it is **15%**, so adjacent
strokes bled into each other and every enclosed shape filled in. The power
ring became a dot.

### 3. Nothing landed on the pixel grid

Paths are authored on a 24-unit grid, scaled to a 14-unit box by
`scaledToIconBox`, then scaled *again* by `getTransformToScaleToFit` to
whatever the button offers. Two arbitrary scale factors in series put every
horizontal and vertical run at a fractional coordinate, and a 1.17 px stroke
centred at x=5.3 is spread across three columns of pixels at a third of the
intended density each. That is the blur, literally.

---

## The rules

### R1. Never render an icon below 14 px

14 px is the floor for anything with more than one feature. Below that, use
a single-feature glyph (a cross, a dot, a chevron) or use a word.

The card buttons move to 22 px with a 4 px inset, giving a **14 px** box.
The hit target stays 22 px, which is what matters for the mouse; only the
drawing grows.

### R2. Stroke is a fraction of the box, not a constant

`thickness = box / 11`, clamped to [1.0, 1.8].

| box | stroke | ratio |
|---|---|---|
| 14 px | 1.27 | 9.1% |
| 16 px | 1.45 | 9.1% |
| 22 px | 1.8 (clamped) | 8.2% |

A constant ratio is what makes a 14 px icon and a 16 px icon look like
members of the same family rather than one bold and one hairline.

### R3. Snap to the pixel grid

After placing, the path is translated so that strokes of odd width land on
half-pixel centres. A 1 px line centred at y=10.5 covers exactly one row of
pixels; the same line at y=10.0 covers two rows at half intensity each.

`strokeIcon` does this once, for every caller, so no icon has to think about
it.

### R4. A detail budget, in units of the 24-grid

At a 14 px box the scale is 14/24 ≈ 0.58, so:

| 24-grid | on screen at 14 px | verdict |
|---|---|---|
| 2 units | 1.2 px | the stroke itself |
| 3 units | 1.75 px | **minimum gap** between two strokes |
| 4 units | 2.3 px | minimum size of a readable dot or hole |
| 6 units | 3.5 px | minimum for a shape that must be *identified* |

Anything finer than 3 units disappears or merges. This is the rule the
rejected designs below break.

### R5. Three features, maximum

Counted as distinct closed shapes or strokes. A ring plus a stem is two. A
window frame plus a title bar is two. Three sliders with three handles is
six, and it is why that one failed.

### R6. Optical size is set by the inset, never by the path

`strokeIcon` scales a glyph's **own bounding box** to fill the box it is
given. So the extent you draw at has no effect on the result: a cross
authored 6..18 and the same cross authored 4.5..19.5 render pixel for
pixel identically. Only the *shape* carries over, never the size.

Which means a glyph that looks too big cannot be fixed by drawing it
smaller. It is fixed by giving that one button a larger inset.

The cross needs this. A diagonal inscribed in a square reaches the corners —
1.41× the radius of a circle inscribed in the same square — so at a shared
inset it is unmistakably the largest thing in the row even though every
glyph is nominally 14 px. It gets an inset of 5.5 against the others' 4,
putting it in an 11 px box.

This was worth getting wrong once. The first attempt at this rule said
"draw every member to the same extent", which is true of a design grid and
false of this renderer, and the sheet showed the change had done precisely
nothing.

**Corollary for the contact sheet:** it must render each glyph at its own
box, not at one uniform size, or it hides the very correction it exists to
check.

### R7. Silhouette before detail

The glyph must be identifiable as a filled blob with the detail thrown away.
Squint at it; if the blob could be any of three other icons in the set, the
drawing is doing work the shape should be doing.

---

## The set

Authored on the 24-unit grid, origin top-left, with a nominal 2-unit stroke.

### Card buttons

| glyph | means | shape | features |
|---|---|---|---|
| **power** | Bypass | ring open at the top, vertical stem through the gap | 2 |
| **faders** | Mappings | two horizontal tracks, one square handle each, offset | 2 |
| **plug** | Open the plugin's own interface | two prongs, a body, a cord | 3 |
| **cross** | Remove | two diagonals, in an 11 px box (R6) — **not on the card**, see below | 1 |

**Power** is the convention for bypass on hardware and in every DAW, so it
needs no learning. The ring is drawn from 300° round to 240° — a 60° gap,
which at 14 px is a visible notch rather than a suggestion of one.

**Faders** replaced three sliders with round handles. Two tracks instead of
three (R5), and square handles instead of round: a 4-unit square keeps its
corners at 2.3 px where a 4-unit circle renders as a smudge (R4). Two
handles at different positions is what says "these are controls that move",
which is what a mapping is.

**Plug** opens the plugin's own interface. It was a window — a rounded rect
with a title rule — and it read as a folder: at 14 px the rule is one pixel
and what is left is a rectangle with a lighter top edge. A plug has an
unmistakable silhouette at any size (R7) and says "plugin" without reading
as anything else. It is the same glyph the top bar uses for the Plugins
menu, which is a feature: both mean "a plugin".

**The cross is no longer on the card.** It appeared on hover, in the exact
position your hand was already travelling to for the other three buttons,
so reaching for the editor put a delete under the cursor. Removing is a
deliberate act: it lives in the right-click menu (with "Del" spelled out
beside it) and on the Delete key with the card selected. The glyph stays in
the set because it is still the right drawing if a cross is ever needed
again.

**Cross** is the one glyph with its own box size, for the optical reason in
R6. Its source extent is irrelevant.

### Rejected

| design | why not |
|---|---|
| lucide `sliders-horizontal` verbatim | three tracks and three 4-unit *round* handles — six features, and the handles smudge (R4, R5) |
| a chain link for mappings | the canvas already uses cables for connection; a second connection metaphor on the same screen |
| a MIDI DIN for mappings | mappings are MIDI *or* OSC; a DIN says only one of them |
| a pencil for the editor | implies renaming, which is a different action on the same card |
| a bin for remove | heavier than a cross and says "deleted forever", when Ctrl+Z brings it back |
| a window for the editor | at 14 px the title rule is one pixel and the frame reads as a folder |

---

## Verifying

`build.ps1 snapshots` renders `icons-card` and `icons-bar`: every glyph at
its real size, and again at 4×, on the canvas background. Judge the real-size
row; the 4× row is only for finding which stroke is wrong once the small one
looks off.

A glyph that cannot be told apart from its neighbour in the real-size row
has failed, however good it looks at 4×.
