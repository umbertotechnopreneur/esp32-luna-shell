# CODY2D

Small RGB565 software raster primitives for the private Cody companion firmware,
targeting ESP-IDF 6.0.2. The consumer owns every image, surface, allocation,
clock, task, touch sample, and display transfer. This component owns none of
those resources and depends only on ESP-IDF error definitions and logging.

## Source provenance

The external `Surface`/`Rect` model and row-based clear/vertical RGB565 color
interpolation are adapted from the user-owned component in
`E:\ESP32 Biagio Animatronics\components\esp32_biagio_animatro`:

- `include/esp32_biagio_animatro_rgb565.hpp`, the `Surface` and `Rect` definitions.
- `src/esp32_biagio_animatro_rgb565.cpp`, `valid_surface`, `Compositor::clear`,
  and `Compositor::clear_vertical_gradient`.

CODY2D adds explicit pixel capacity, strict geometry bounds, a fixed 32-level
gradient, direct rectangle/circle fills, and the indexed row-RLE decoder for
the generated sleeping-blue Cody boot asset. It does not import Biagio artwork,
sprite transformations, its 25-joint robot rig, its facial state machine, or
its animation engine. No LVGL, Brookesia, host image dependency, panel driver,
allocation, or scheduler is included.

## Surface and raster contract

`Surface` explicitly declares native or byte-swapped RGB565 storage; omitted
aggregate fields default to native. Public drawing colors and palettes remain
native RGB565, and each raster operation encodes its writes for the declared
surface order. `stride` and `pixel_capacity` are measured in pixels, not bytes.
The Cody firmware now rasterizes borrowed DMA tiles directly in panel byte order,
so the display submission can avoid a second complete byte-swap pass. Coverage
AA and the pearl-jelly sampler still blend native RGB565 channels, then encode
the result for the destination. This integrates the `cody-perf` worktree with
the newer jelly renderer; the CPU/FPS effect has not been measured or validated.
Width and height are nonzero and no larger than 1024; stride must be at least
width. The caller must provide the actual writable capacity and retain the
storage through the call. The final visible pixel is checked using
`(height - 1) * stride + width`; row padding remains untouched.

`clear` fills visible pixels. `fill_rect`, `fill_circle`, and
`fill_vertical_gradient32` require the complete requested geometry to fit
inside the surface; they reject empty or out-of-bounds geometry rather than
clip it. A zero-radius circle writes its center pixel. The gradient uses up
to 32 quantized RGB565 colors, with exact endpoint colors when its height is
at least two; a one-row gradient uses the top color.

## Indexed RLE boot contract

`IndexedRleImage` contains width, height, palette count, a readable RGB565
palette, `height + 1` readable `uint32_t` row offsets, and the declared run
storage. Palette count is 1 through 256. Each run is exactly three bytes:
nonzero little-endian `uint16_t` count, then a `uint8_t` palette index.
The first offset is zero, the last is `runs_size`, offsets increase on
complete run boundaries, and every row expands to exactly image width.

`validate_indexed_rle_image(image)` checks the metadata, storage extents, and
every source row. The boot consumer performs this complete check once for each
immutable flash image before starting its worker.

`decode_rows(image, first_row, destination)` expands `destination.height`
rows starting at `first_row`. Destination width must equal image width;
its stride may exceed width. Metadata, storage extents, destination bounds,
and every requested source row are checked before any pixel is written.
This bounds each strip's validation work to the rows it will decode. The
caller supplies real readable array extents and keeps the source immutable
during the call; the component cannot infer the backing allocation size
of a raw pointer.

Bad arguments return `ESP_ERR_INVALID_ARG`; insufficient surface capacity
or geometry returns `ESP_ERR_INVALID_SIZE`; malformed source rows return
`ESP_ERR_INVALID_RESPONSE`. Rejections include the operation, relevant values,
and error in component logs. There is no substitute image or zero filling.

The component does not track dirty redraws or flush the panel. The boot consumer
decodes a bounded strip and sends it through the independently owned platform
display API. Font, QR, widgets, cloud Bézier geometry, OOBE, and PC transport
remain outside this base component. The local rig PoC adds bounded path fills
and round line/dot strokes through `CODY2D_paths.hpp`. That API explicitly uses
full-canvas coordinates and clips to a caller-owned strip; it does not change
the strict bounds of the original rectangle/circle primitives. Cubics are
evaluated by the companion into one reusable scene before rasterization. See
`docs/architecture/cody-rig-poc.md` for host/device differences and pending
performance validation.

Only static source review was performed for this implementation. No configure,
build, test, simulator, size, or physical device validation has been run.
