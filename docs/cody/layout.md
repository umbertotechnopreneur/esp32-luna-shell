# Cody layout on the Waveshare round display

Native `CODY2D` layout consumes the board's
[geometry](../bsp/boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md).
The repository contract is in [AGENTS.md](../../AGENTS.md). These coordinates
describe the current 466 x 466 adapter; they are not requirements of every future board.

## Content, actions and Back

Use the central [73,393) square as a baseline. Expand into the circle only with
local-chord admission of artwork, controls and complete hit bounds. Keep QR
quiet zones intact. Scene content and lower actions have separate rectangles.

Back occupies (10,203), 60 x 60, with one centered left chevron and no caption.
Show it with a valid previous route. Retain the complete circular touch target
and keep it separate from central content and primary/footer actions.
The native style is subdued graphite with a light chevron.

The approved 2026-10-09 action language uses semantic colors, restrained
gradients and icons. Two actions are rounded cards side by side; three add one
centered action below; one is a centered wide capsule with its icon left of the
caption. Complete controls appear after the title and main body line, with
touch admitted at the same time. Captions have no typewriter timing or character audio.

| Control | Logical rectangle |
| --- | --- |
| Two cards | (73,260,154,104) and (239,260,154,104), 28-pixel corners |
| Third action / Options | (153,376,160,52) |
| Ordinary single action | (113,352,240,72) |
| Compact QR/skin footer | (123,390,220,44) |

The corresponding extreme squared distances are 42,500, 44,036 and 52,100,
within radius squared 54,289. These are static calculations, not touch evidence.
Card icons center at y=293; two-line captions start at y=308/335. Icon centers
in ordinary, Options and compact capsules are y=387/401/411 respectively.
Coverage is clipped to the rounded control; complete hit bounds remain admitted.

## Setup, QR and Skin

| Screen | Artwork/content |
| --- | --- |
| 05 | Cody moves from (233,225,130) to (233,105,65) over 1000 ms; wait 300 ms before text and action admission |
| 06 | White background, Cody (233,94,58), installation QR 240 x 240 at (113,140) |
| 07 | Cody (233,80,46), title y=131, session QR 204 x 204 at (131,182), only while ReadyToScan |
| 10 / Make it yours | 34-pixel option rows at y=202/240/278/316; separate compact footer at y=390 |
| 11 / Skin | Preview (233,172,44), title y=78, viewport (117,226,232,146) |

Skin rows are 220 x 38 with 44-pixel steps. Only complete rows are drawn and
interactive; scrolling admits three visible rows. Stable look IDs and save gates
are described in [navigation](navigation.md).

Session QR storage supports version 10: at most 57 x 57 modules, 407 bytes.
Use integer modules and four white quiet modules. Payload/modules belong to the
current attempt; stale data cannot authorize presentation.
The current 05/06/07 QR journey uses compact footer actions and suppresses lateral
chevrons; 06 retains previous-route swipe. Its previous-route Back behavior still
needs reconciliation with the repository's lateral-Back contract in [TODO.md](../../TODO.md).

## Companion panels, status and Debug

Home uses three lower panels: Tokens, Model & Context, and Session & Link.
Panels rotate every eight visible seconds. A short released tap advances
immediately and restarts that interval; a deliberate canonical-IMU shake returns
to the default token panel. The character remains at (233,122), scale 73, with
the state title at y=190 and the model row in (73,236), 320 x 28. Only the lower
panel changes; there is no full-screen Activity page, page counter or Back control.
Setup and questions retain their existing routes and action controls. Hidden
views, standby and current compaction pause automatic panel rotation.

The selected [context-arc proposal](ui-proposals/20261014/02-context-arc.png)
is implemented with native text and antialiased strokes. The dashboard damage
envelope is (40,268)-(426,393); actual ink follows the local circular chords,
not the entire enclosing rectangle. Tokens show a large Free value, secondary
Last and Window values, elapsed time and the canonical IMU label. The mint arc
represents coherent free context. Missing, stale, invalid and over-limit data
retain explicit states and never produce an invented percentage.

Model & Context contains the model name, qualitative Standard catalog price
capsule and thinking effort. Session & Link contains connection state, abbreviated
chat ID and the device's own canonical Wi-Fi IP; it omits firmware and PC IP.
Independent source validity remains visible. Model prices are green Cheap,
orange Reasonable price and red Expensive; they are not monetary chat spending.

Home uses the `TT` unit and decimal whole thousands with `K`, truncating the last
three digits: `Free 319K TT`. Periods group every three displayed digits; counts
below 1000 remain exact, including zero. Canonical and protocol counters retain
their full precision. Excessively wide secondary captions alternate within their
bounded row; the source values and timestamps do not change.

Commands use one clipped right-to-left marquee at (113,408), 240 x 25, without
an Available commands heading. The same scene clock drives its quantized motion;
hidden presentation does not accumulate scroll time. Attention and compaction
suppress the hints. Current compaction turns the title orange and the token arc
into an indeterminate accent, preserving the character cue and sound admission.
The independent fonts derive from the vendored OFL Telex face; other scenes keep
their existing typography. No bitmap/video playback or polling task is added.

The standalone status layer shows date at (122,32), local HH:MM at (122,47),
and battery/Wi-Fi/Bluetooth at y=43-64. Seconds are omitted. Battery is gray
when absent, red when low, orange midrange, green healthy, and cyan/white charging.
Flash row masks and canonical projections require no LVGL or hardware reads.
Touch reveals the layer; five seconds without activity begins the 330-ms fade.
Home translates its personality symbols twelve pixels downward from the standard
anchor, retaining their scale and the complete status exclusion. A change between
standard and Home layouts re-primes position even while animation is paused;
it does not restart the cue's fade, hold or sound. Wi-Fi and compaction symbols
therefore use the same reserved geometry in firmware and the native Studio bridge.

Debug Pause/Play at (158,47), Skeleton at (308,47), Screenshot and debug-view Back
retain authored IDs but are hidden and noninteractive by default. Raster and hit
testing share visibility. Setup Back/Cancel is a separate control. Release keeps
the informational layer while omitting Debug controls.

Debug diagnostic rows occupy y=390-445: measured FPS/CPU, IPv4, free internal RAM/
PSRAM, SD presence/access and measured free space. Invalid samples show their
actual unavailable state; without card detect, SD probe failure is UNCONFIRMED.

## Boot and capture

Cold-boot title Cody AI Companion is at y=60 and subtitle A distraction-free
sidekick at y=110, centered using their actual fonts. The complete floppy artwork
is (137,164), 192 x 219, with the original 42-frame/3500-ms cycle. Firmware and
Studio share decoder, serialized source-row ownership and text composition.

The Debug PWR capture countdown occupies [153,313) x [174,292) with no touch
target. After an overlay-free frame retires, capture composes temporary 466 x 466
RGB565 PSRAM, then transfers ownership to storage for atomic top-down 24-bit BMP
publication under `LUNASHELL/screenshots/`. SAVING/SAVED/FAILED follows the real
result. PWR timing and power transitions belong to the shared power owner.

## Native control rasterization

Current rounded radii use immutable 8 x 8 coverage masks; 24 x 24 semantic vector
icons use 4 x 4 coverage sampling. Whole cells bypass sampling. RGB565 composition
respects borrowed stride, byte order and background. Borders have a two-pixel
inset; gradients use eight-bit row weights. Masks are flash data, with no per-frame
supersampling/cache. Other supported primitive radii compute the same sampling
rule when requested. Hit testing preserves pixel-center geometry.

Committed fonts remain 2bpp. The updated generator supports 4bpp with 4x
supersampling, subject to explicit licensed-source regeneration and unchanged
metrics. Source preparation does not establish physical quality. Rendering and
retired-frame damage include old/new content and action bounds.
