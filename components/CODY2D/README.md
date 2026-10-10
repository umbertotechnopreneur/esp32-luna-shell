# CODY2D

Small RGB565 raster, path, text and control primitives for standalone Cody.
Implementation lives in [src](src/README.md); public contracts stay in `include/`.
No LVGL, panel driver, allocation, task, clock or sensor owner is admitted.
Consumers own surfaces, images, touch samples and display transfers.

## Contracts

- `Surface` declares native or byte-swapped RGB565 storage. Colors/palettes stay
  native; each raster encodes its writes. `stride` and `pixel_capacity` are pixels,
  not bytes. The caller retains actual writable storage through the call.
- Width/height are nonzero and at most 1024; stride is at least width. Capacity
  covers `(height - 1) * stride + width`; padding remains untouched.
- Basic rectangle/circle/32-level-gradient operations require complete bounds;
  they do not silently clip invalid geometry. Zero-radius circles write one pixel;
  a one-row gradient uses its top color.
- Path operations use full-canvas coordinates and clip to a borrowed tile.
  Cubics are evaluated by the companion into reusable geometry.
- Indexed row-RLE has 1–256 native RGB565 palette entries, `height + 1` offsets
  and three-byte runs: nonzero little-endian uint16 count plus uint8 palette index.
  Offsets are run-aligned, begin at zero, end at `runs_size`, and each row expands
  to exactly image width.
- Validate immutable RLE once before starting the consumer. Row decoding checks
  metadata, extents and every requested row before writing. Destination width
  equals image width; stride may be larger. Pointer backing extents remain the
  caller's responsibility.
- Bad arguments return `ESP_ERR_INVALID_ARG`, insufficient surface/geometry
  `ESP_ERR_INVALID_SIZE`, malformed rows `ESP_ERR_INVALID_RESPONSE`. Log real
  rejection details; never substitute artwork or zero-fill a failed decode.

Dirty planning, OOBE and display submission belong to consumers. The companion
can raster directly into panel byte order; this does not establish a measured
CPU/FPS improvement. See [Cody rendering](../../docs/cody/rendering.md).

## Provenance

`Surface`/`Rect`, surface validation and row-based clear/gradient interpolation
derive from the user-owned `esp32_biagio_animatro` component in
`E:\ESP32 Biagio Animatronics` (`include/esp32_biagio_animatro_rgb565.hpp` and
`src/esp32_biagio_animatro_rgb565.cpp`). CODY2D adds explicit capacity, strict
bounds, fixed quantization and bounded path/text/control/RLE operations.
Biagio artwork, robot rig, face state machine and animation engine are not imported.
