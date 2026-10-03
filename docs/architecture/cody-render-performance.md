# Cody rendering performance on the physical board

Copyright umbertogiacobbi.biz 2026 - confidential

## Comparison with Luna Shell

This comparison reads the current source in `E:\ESP32 Luna Shell` and Cody's
integrated working copy. It does not run or flash Luna. Parallel display/frame
ownership and snapshot refactoring changes are preserved separately.

| Responsibility | Luna Shell UI | Cody standalone PoC |
| --- | --- | --- |
| Rendering | LVGL emits changed areas into PSRAM render tiles | Named geometry is rasterized directly into the owned internal DMA slot |
| Cached pixels | PSRAM RGB565 frame retained for cardinal rotation replay | PSRAM RGB565 previous frame compared for changed regions |
| Pixel transfer | Transient internal DMA bounce slots; queued SDK color transfers | Transient internal DMA lease reused only within the bounded frame; 32-row maximum strips |
| Completion | Queue drain waits for real transfer completion | ISR signals a semaphore; the worker blocks until completion or its deadline |
| Inactivity | UI/event scheduling avoids an unnecessary full repaint | Static boot artwork waits for touch/revision changes; the active breathing/contour demo deliberately advances each frame |

Neither path sends the PSRAM framebuffer directly to panel DMA. Cody keeps the
original panel clock and physical touch owner, without importing LVGL or its
adapter. The SDK uses polling for short commands and queued DMA for color data;
the color payload is not transmitted through a newly introduced polling loop.

## Changes and measurement boundaries

1. Retain a 434,312-byte PSRAM previous frame and a 59,648-byte render tile.
   The latest source removes that render tile and borrows the frame-owned
   internal DMA slot instead. Compare actual RGB565 pixels, send paired-column changed rectangles and force
   a full redraw after startup, failed frames, rotation or display invalidation.
2. Bound stroke scanlines by the infinite line band, then keep the original
   closest-segment pixel test for exact round-cap coverage. Long diagonal bones
   no longer test every pixel in their enclosing rectangle.
3. Copy only the measured changed region into the candidate cache before packing
   its transfer payload. A partially updated cache stays invalid until the whole
   frame completes.
4. Compare only conservative old/new rig coverage, HUD and changed navigation
   regions. Include previous coverage when artwork moves or shrinks.
5. Rasterize into the owned internal DMA lease: actual capacity determines tile
   height, normally 32 rows. Copy changed native pixels to the PSRAM cache before
   packing and endian conversion in place. Redraw the tile before native reuse.
   Touch polling stays bounded independently of degraded one-row allocation.
6. Keep the 6,144-byte worker stack in PSRAM. Latest source uses squared cross
   products for interior stroke coverage, with the original projection near
   rounding-sensitive boundaries; opaque polygon spans use contiguous fills.
   Navigation gradient colors are computed once per row. Optional monotonic
   layer timings separate clear, artwork, skeleton and navigation.

Timing logs separate geometry, raster including reuse, transfer and the complete
frame. An additional line separates drawing from cache comparison/packing and
copies the actual HUD FPS and CPU text from the canonical snapshot. CPU1 is SDK
core 0; CPU2 is SDK core 1. Unavailable measurements remain `N/A`.

Completed-frame FPS includes scheduling/logging overhead; it is not the inverse
of raster time. A 30 FPS budget or requested CPU frequency is not a measured
result. Observed frame distributions are kept separately for artwork/skeleton.

## Evidence collected on 2026-10-03

All firmware/check/capture operations used `scripts/device.ps1`, ESP-IDF 6.0.2
and at most eight compiler jobs. Runtime outputs remain under
`artifacts/build-cody-companion/device-evidence/`.

| Checkpoint | Physical measurements |
| --- | --- |
| Original full-frame transfers | Artwork approximately 11 FPS reported by the user; raster mean 14.9 ms, transfer mean 72.9 ms, full-frame mean 94.7 ms. Skeleton approximately 6 FPS, full-frame mean about 159 ms. |
| Changed rectangles and taller tiles | Skeleton full-frame mean about 102 ms; transfer about 15 ms. The user reported at least 25 FPS with the overlay hidden. The capture mainly records skeleton mode and does not independently reproduce that artwork figure. |
| Bounded strokes and changed-region cache copies | Skeleton 13.3..14.6 FPS in completed-frame readouts; draw about 28 ms, reuse about 23..25 ms, transfer about 14..15 ms, full-frame mean about 68 ms. Canonical CPU1 85..87%, CPU2 0%; the user's readout was CPU1 86%, CPU2 0%, FPS 11. |

The latest integrated host runtime check passes 1,200 comparisons against the
original exhaustive stroke raster, including fractional/clipped coordinates,
small diameters and 8/16/32/64-row tiles. Actual rig dirty updates match full
framebuffers in both overlay modes at 1/8/16/32/64 rows, including movement,
shrinking, selection and overlay changes. Analytic fixtures cover alpha/opaque
fills, gradient rows, off-screen spans and padding guards. Response parity and
the shared 256-color palette also pass. Saved report:
`poc-cross-span-host-check/report.json`. These are host correctness checks, with
parallel uncommitted changes included in the integrated fixture.

## Subsequent physical measurements

| Candidate | Observed physical result |
| --- | --- |
| Conservative comparison regions | Skeleton 16.3-17.4 FPS, about 57 ms per frame; CPU1 83-84%, CPU2 0%. |
| Raster in the owned internal DMA slot | Skeleton 18.8-19.9 FPS, about 50-51 ms per frame; CPU1 81-83%, CPU2 0%. |
| Layer profiling and navigation gradient hoist | Skeleton about 19-21 FPS, CPU1 80-83%. Artwork 26.4-29 FPS, CPU1 78-79%, CPU2 0%. User confirmed 27 FPS without lines and CPU around 80%. |

The last row was flashed from the integrated working copy: firmware size
1,704,288 bytes, SHA256
`34F5FA42B5FE0A58639A7BEC42C9202CD5071360359CD05B59ECE9901A6AAADD`.
Capture: `poc-layer-demo-20261003.log`. Latest cross-product/opaque-span source
has no physical performance result yet; these older measurements do not
validate that candidate.

In that capture the skeleton costs about 10.3-10.9 ms per frame; artwork costs
7.5-8.7 ms, clear about 2 ms and navigation about 1.3 ms. Cache reuse costs about
9-12 ms and transfer 11-15 ms. Removing the skeleton increases completed frames
per second, so CPU utilization need not fall proportionally to per-frame cost.
The remaining utilization still requires investigation.

The attempted idle captures (`poc-idle-demo` and `poc-idle-confirm`) still log
continuous skeleton frames without a Back transition. They do not establish
CPU utilization on a static boot screen despite the user's reported navigation.
Obtain a logged route transition and subsequent canonical processor sample
before assigning the remaining utilization to hardware services or idle work.

Scoped CPU-maximum, internal-stack, 16-row PSRAM-tile and row-scratch trials did
not improve the combined physical result and were removed. Global Luna power
policy remains unchanged. No full validation package was run.

The capture after the PC interruption ended when the serial port closed; this
is not classified as a firmware panic. Snapshot truncation, the disabled RTC
summary and startup audio/refresh warnings are separate existing issues.

Physical rotation/standby stress, long-run stability, battery response and audio
quality remain separate checks. Radio initialization is not evidence of a
connected Wi-Fi/BLE session or coexistence under network load.
