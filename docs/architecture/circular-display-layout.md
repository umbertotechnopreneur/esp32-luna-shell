# Circular display layout

The Waveshare display has a 466 x 466 logical canvas. Luna's central safe
square is 320 x 320, beginning at (73, 73). This is the verified source layout
for Cody's circular screen. The rounded regions outside the square are useful
space for navigation, status, and artwork.

![Nominal circle, safe square, and lateral navigation slots](images/circular-display-layout.svg)

The implementation rule lives in `AGENTS.md`. This document explains the
geometry and its source references; it does not introduce a second instruction
file or a dependency on the Luna GUI.

## Source geometry

| Region | Logical position and extent |
| --- | --- |
| Full canvas | [0, 466) x [0, 466), addressable pixel indices 0 through 465 |
| Nominal circle | Center (233, 233), radius 233 |
| Central safe square | [73, 393) x [73, 393), 320 x 320 |
| Left navigation / Back | Origin (10, 203), 60 x 60, circular radius 30 |
| Back center | (40, 233), at the middle-left / 9 o'clock position |
| Existing Luna right navigation slot | Origin (396, 203), 60 x 60, center (426, 233) |
| Cody PoC Pause/Play | Origin (203, 10), 60 x 60, circular radius 30, center (233, 40) |

The 60-pixel Back fits entirely to the left of the square: its right exclusive
edge is x=70, leaving three pixels before x=73. Its left extreme is x=10,
ten pixels inside the nominal circle at its widest horizontal chord.
The complete bounding box also fits within the circle.

The right navigation slot records available space in the source UI. A primary
OOBE action such as Next, Pair Cody, or Setup guide can remain centered in the
content area; it does not need to move into that slot.

The Cody PoC uses the upper arc for an icon-only Pause/Play control. Its full
target lies above y=73 and inside the circle. Drawing and hit testing share one
named control table; the physical owner maps the same logical target through
all cardinal rotations. Pause freezes the evaluated character and its clocks.
The CPU readout remains live at one update per second, displaying `PAUSED` in
place of animation FPS. Resume excludes the paused interval from animation and
motion integration. Back and overlay remain available while paused.

## Rounded space

The four nominal straight insets are (466 - 320) / 2 = 73 pixels. They are
insets of the square, not the width of every visible circular segment.
For continuous logical layout coordinates, the nominal horizontal chord at y
has these bounds:

```text
half_width(y) = sqrt(233^2 - (y - 233)^2)
left(y)       = 233 - half_width(y)
right(y)      = 233 + half_width(y)

left_side_space(y)  = max(0, 73 - left(y))
right_side_space(y) = max(0, right(y) - 393)
```

| Logical y | Circle left x | Circle right x | Space outside the square, per side |
| ---: | ---: | ---: | ---: |
| 73 | 63.62 | 402.38 | 9.38 |
| 100 | 41.69 | 424.31 | 31.31 |
| 150 | 15.28 | 450.72 | 57.72 |
| 203 | 1.94 | 464.06 | 71.06 |
| 233 | 0.00 | 466.00 | 73.00 |
| 263 | 1.94 | 464.06 | 71.06 |
| 316 | 15.28 | 450.72 | 57.72 |
| 366 | 41.69 | 424.31 | 31.31 |
| 393 | 63.62 | 402.38 | 9.38 |

At y=233, each side has 73 pixels outside the safe square. At the square's
upper and lower geometric edges, y=73 and y=393, each side has approximately
9.38 pixels. A tall control must fit the narrowest chord it occupies; the
73-pixel center measurement cannot be applied to its entire height.

The mathematically largest inscribed square would have side
466 / sqrt(2), approximately 329.51 pixels. Luna uses the smaller 320-pixel
baseline. About 40 percent of the nominal disk area lies outside that baseline
square; those regions must be planned rather than discarded. These are
source-based nominal geometry calculations, not a measurement of physical
bezel clearance or touch behavior.

## Layout and interaction

Use the central square as a reliable content baseline. Expand outside it only
with circle-aware clipping and explicit space for navigation. A complete QR
rectangle, including its white quiet zone, must remain unobstructed.

Back uses the left lateral slot when a previous route exists. Its control and
touch target must not overlap content or an independent action. It is not a
left footer button. Render a single centered left-chevron icon, without the
word Back. Preserve the 60 x 60 touch target and the shared blue gradient;
the smaller icon does not reduce the interactive area. Back remains the
semantic action name in layout data and navigation history.

The existing Luna content viewport can release top and bottom arc space:
320 x 320 at y=73 with both arcs, 320 x 393 at y=73 with only the upper arc,
320 x 393 at y=0 with only the lower arc, and 320 x 466 at y=0 with neither.
These expanded rectangles are layout viewports inside a circular display,
not proof that every point of the rectangle is visible. Controls in released
top and bottom space still need the local chord and padding.

The selected logical orientation applies to drawing, clipping, and touch
together, including 180 degrees. The Back slot remains the user's left side
in that orientation. Do not mirror labels or maintain a separate physical
hit box after rotation. The controller's six-pixel address gap is hardware
calibration, not an additional UI inset.

## References and scope

- `components/esp32_s3_touch_amoled_1_75/include/bsp/display.h` defines
  `BSP_LCD_H_RES` and `BSP_LCD_V_RES` as 466.
- `components/LUNASHELL_app/LUNASHELL_app.cpp` defines `kDisplaySizeDp=466`,
  `kChromeInsetDp=73`, and `kContentSafeSizeDp=320`.
- `components/LUNASHELL_app/gui/root.json` declares 320-pixel content surfaces
  beginning at x=73.
- `components/LUNASHELL_app/package/res/templates/LUNASHELL_chrome.json`
  declares the Back and right-side navigation controls and their placement.
- `components/LUNASHELL_app/LUNASHELL_edge_navigation.cpp` updates visibility
  and clickability according to the navigation history.
- `simulator/windows/lunashell_simulator.py` uses `SAFE_ORIGIN=73`,
  `SAFE_SIZE=320`, and draws Back at (10, 203).
- `simulator/windows/README.md` explains the expanded content viewports.

Only source files were read for these measurements. No simulator, firmware
validation, hardware operation, or LVGL integration into Cody was performed.
The current Cody boot consumer does not yet implement OOBE navigation.
