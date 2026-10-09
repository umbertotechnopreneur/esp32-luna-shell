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
| Cody Debug Pause/Play | Authored at center (158, 47), currently hidden and non-interactive |
| Cody capture countdown | Center overlay bounds [153,313) x [174,292); no touch target |
| Cody Debug Skeleton | Authored at center (308, 47), currently hidden and non-interactive |
| Cody status overlay | Date at (122,32), HH:MM at (122,47); battery, Wi-Fi and Bluetooth at y=43–64 |

The 60-pixel Back fits entirely to the left of the square: its right exclusive
edge is x=70, leaving three pixels before x=73. Its left extreme is x=10,
ten pixels inside the nominal circle at its widest horizontal chord.
The complete bounding box also fits within the circle.

The right navigation slot can host Cody's discreet forward chevron. Its circular
60-pixel touch target stays separate from the QR and central content. The setup
journey instead uses an icon-and-caption gradient footer: Show QR on 05, Pair Cody on 06,
and Cancel on 07. These routes hide the lateral chevrons and their hit targets;
06 still accepts a previous-route swipe. The compact QR footer bounds are (123, 390),
220 x 44, with every corner inside the occupied circular chord.

The important Cody UI decision approved on 2026-10-09 is recorded in `AGENTS.md`:
two actions use rounded cards side by side; three add one centered action below;
one uses a centered wide capsule with icon left of its caption. Dark gradients,
thin semantic borders and white ASCII captions apply across native setup screens.
The reference images are in `docs/Cody/ui-proposals/20261009/`.
The usual single action is (113,352,240,72); QR pages and the tall skin list retain
the compact (123,390,220,44) variant. Scene content and the lower action have
separate validated/clipped rectangles, rather than one rectangle borrowing width
from a wider chord above. Retired-frame damage includes both old and new slots.
The two card rectangles are (73,260,154,104) and (239,260,154,104), with 28-pixel
corners. Their last-row extremes have squared distance 42,500 from the center,
below radius squared 54,289. Options is (153,376,160,52); its extremes have
squared distance 44,036. The compact footer extremes are 52,100. These are
static geometry calculations, not simulator or physical touch evidence.

On 2026-10-09 the user also requested antialiasing and lower action icons, using
ten pixels as a visual indication rather than a fixed requirement. Native
controls use compile-time 4x4 coverage masks for the current
outer/inner corner radii and 24x24 semantic icons. Masks are immutable flash data,
shared across controls; no per-frame supersampling or heap cache is needed for
the current layouts. RGB565 compositing respects the borrowed tile's byte order,
stride and existing background. The border follows a concentric two-pixel inset,
and gradients interpolate each row with an eight-bit weight. Text already uses
the authored antialiased font coverage. Other valid public-primitive radii retain
the same coverage sampling rule, computed only when those shapes are requested.
Hit testing retains the original pixel-center geometry and full touch bounds.

Card icons now center at y=293, with their 24-pixel extent ending before y=305;
two-line captions begin at y=308 and y=335 and end before y=360. Ordinary single
icons center at y=394 (+6), Options at y=406 (+4) and compact QR/skin footer icons
at y=416 (+4), ending at y=428 with six pixels of bottom padding. Icon coverage is clipped
to the rounded control, while all authored control/content rectangles remain
unchanged. These are source geometry statements; physical appearance is pending.

The Cody Debug Screenshot control is hidden and non-interactive. Pause/Play and
Skeleton retain their stable authored identities but set `visible_by_default`
false, which disables both raster and hit testing. The source
`assets/cody/navigation.json` generates the native table and supplies the
simulator geometry; rendering and hit testing share the exact visible circle.
The Screenshot identity remains in that authored table only because the current
navigation schema requires all four historical identities; no camera button or
touch action is exposed at runtime.
The remaining authored Debug targets stay above the central safe square and
within the circle's local chord. The physical owner maps them through all
cardinal rotations. The
debug-view Back remains at its canonical position but `visible_by_default` is
false, disabling paint and touch together. Route-specific OOBE Back/Cancel
remains a separate control.

After first boot, Cody's standalone status component renders a quiet English date
and larger local `HH:MM` on the left; seconds are intentionally omitted. Battery,
Wi-Fi and Bluetooth use a consistent two-pixel visual weight on the right. Battery
is gray when absent, red when low,
orange in the middle range, green when healthy and cyan with a white charging
glyph while charging. All text and icon row masks are compile-time glyphs kept
in firmware flash rodata. The component contains no LVGL or Brookesia
dependency and reads only one coherent `DeviceSnapshotService` projection. The
status layer and remaining Debug controls use the same touch-activity opacity:
touch reveals them and five seconds without activity starts the same 330-ms
fade. Release firmware omits the Debug controls but retains the post-first-boot
informational layer.

Debug firmware can add five centered one-pixel-scale rows below the routine title:
measured FPS with CPU1/CPU2, IPv4 address, free internal RAM and PSRAM, confirmed
SD presence/access, and measured SD free space. These rows consume y=390–445 and
read only one coherent canonical diagnostic projection. A board without a card
detect signal reports `SD UNCONFIRMED`, never a fabricated physical absence.

A short PWR press while Cody is active and its runtime capture flag is enabled
shows a centered 3, 2, 1 countdown. Its [153,313) x [174,292) panel sits in
the central safe square. After a complete overlay-free frame retires, screenshot
capture forces one complete native composition into temporary 466 x 466 RGB565
PSRAM, then transfers ownership to the SD service. The owner publishes a
top-down 24-bit BMP atomically under `LUNASHELL/screenshots/`; mirrored logs
remain under `LUNASHELL/logs/`. The panel then reports SAVING, SAVED, or FAILED
from the real storage result.

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
word Back. Preserve the 60 x 60 touch target; Cody uses a subdued graphite fill
and light chevron instead of Luna's blue gradient;
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
- `components/LUNASHELL_app/src/runtime/LUNASHELL_app.cpp` defines `kDisplaySizeDp=466`,
  `kChromeInsetDp=73`, and `kContentSafeSizeDp=320`.
- `components/LUNASHELL_app/gui/root.json` declares 320-pixel content surfaces
  beginning at x=73.
- `components/LUNASHELL_app/package/res/templates/LUNASHELL_chrome.json`
  declares the Back and right-side navigation controls and their placement.
- `components/LUNASHELL_app/src/chrome/LUNASHELL_edge_navigation.cpp` updates visibility
  and clickability according to the navigation history.
- `simulator/windows/lunashell_simulator.py` uses `SAFE_ORIGIN=73`,
  `SAFE_SIZE=320`, and draws Back at (10, 203).
- `simulator/windows/README.md` explains the expanded content viewports.

Only source files were read for these measurements. No simulator, firmware
validation, hardware operation, or LVGL integration into Cody was performed.
The Cody boot consumer's reusable cloud/title/content layout uses this geometry,
documented in `cody/runtime.md`. Its network OOBE routes and separate lateral Back
control are documented in `cody/onboarding.md`; no LVGL dependency is admitted.
