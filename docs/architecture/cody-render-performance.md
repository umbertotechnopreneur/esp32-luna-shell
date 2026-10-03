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

The latest diagnostic control adds Pause/Play at logical (233, 40), in the upper
arc. A pause freezes geometry, face, contour and response clocks while the
canonical CPU readout refreshes at 1 Hz. Once the pause icon is drawn, only HUD
tiles are rasterized until an input or display invalidation requires a full
redraw. Paused refreshes do not enter animation benchmark distributions.
Logs explicitly identify `Animation PAUSED`, `PAUSED geometry=frozen HUD=1Hz`,
and `Animation RESUMED`. Native checks cover control targets and exact full/
dirty-frame parity through pause, unchanged pause and resume. The control compiled and flashed successfully (1,705,456 bytes,
SHA256 `BD89756B4569CD3723832CD12154FDC9F863F4A79C1F9D4B867FD4061B642FE2`).
The user confirmed CPU1 1% in pause. `poc-pause-demo-20261003.log` independently
records the pause transition and repeated canonical CPU1 1%, CPU2 0% samples.
Paused HUD drawing is about 0.4 ms and reuse about 0.75 ms once per second.
This establishes a low-load baseline and locates the major extra utilization
in the active animation/render path. It does not yet identify which active
stage is CPU-bound versus waiting for the panel.

| Candidate | Observed physical result |
| --- | --- |
| Conservative comparison regions | Skeleton 16.3-17.4 FPS, about 57 ms per frame; CPU1 83-84%, CPU2 0%. |
| Raster in the owned internal DMA slot | Skeleton 18.8-19.9 FPS, about 50-51 ms per frame; CPU1 81-83%, CPU2 0%. |
| Layer profiling and navigation gradient hoist | Skeleton about 19-21 FPS, CPU1 80-83%. Artwork 26.4-29 FPS, CPU1 78-79%, CPU2 0%. User confirmed 27 FPS without lines and CPU around 80%. |

The last row was flashed from the integrated working copy: firmware size
1,704,288 bytes, SHA256
`34F5FA42B5FE0A58639A7BEC42C9202CD5071360359CD05B59ECE9901A6AAADD`.
Capture: `poc-layer-demo-20261003.log`.

The subsequent cross-product/opaque-span candidate also compiled and flashed:
1,704,208 bytes, SHA256
`99D6B8471123AFD5B44F5A0DB652194F3BF4FD2F2B79642019EE1A79CEE896C8`.
The 100-second `poc-cross-span-demo-20261003.log` contains continuous skeleton
frames, typically around 21 FPS (readout range 18.5-23) with CPU1 79-83%
and CPU2 0%. Skeleton raster costs about
9 ms per frame. This is a modest improvement; the CPU target remains open.
No Back transition was captured, so this run also does not establish static
boot CPU usage. Artwork-only performance of this candidate is not measured.

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

## October 3 pipeline isolation

The physical pipeline capture (`poc-pipeline-demo-20261003.log`) separates
comparison, packing, endian conversion, submission and sleeping DMA waits.
Median sampled artwork costs were 4.747 ms comparison, 4.114 ms packing,
1.588 ms conversion, 2.894 ms submission and 7.140 ms completion wait.
DMA completion wait is elapsed time while blocked, not CPU utilization.
The corresponding skeleton medians were 5.416, 5.715, 2.080, 2.897 and
8.227 ms. The pause baseline remains CPU1 1%, CPU2 0%.

Candidate `6d5636d4` skipped static raster tiles and tried 64-byte memcmp
blocks. Its skeleton capture showed reduced raster work but variable
comparison costs of roughly 4-10 ms. The user reported artwork at 26 FPS,
CPU1 80%, CPU2 0%. This does not establish a performance improvement.
The next candidate retains static-tile rejection and restores direct
RGB565 comparison to isolate those two changes. Its physical result is
pending; do not attribute the earlier measurements to that candidate.

At the user's request, further validation is limited to compilation, flash
and physical performance observations. Automated tests and validation scans
remain deferred unless separately requested or approved.

### Panel rectangle alignment

The user also reported dirty background pixels on candidate `6d5636d4`.
Direct comparison with Luna's `rounder_event_cb` found that Luna aligns both
X and Y to even half-open bounds. Cody's damage stage only paired columns,
allowing odd Y starts/ends. The firmware now expands vertical damage before
cache copying and packing, and uses even raster tile heights so the expanded
rows always belong to the rendered tile. Fewer than two available raster
rows is reported as insufficient capacity rather than sending an unaligned
rectangle. No managed panel driver or Luna UI dependency is introduced.
This corrects a transport-contract discrepancy; physical observation is still
needed to establish whether it removes the reported artifacts.

### Cream theme and paired-row device capture

Candidate `17e6de0a` compiled and flashed successfully on COM10. Firmware size
is 1,707,056 bytes; SHA256 is
`9296722BFA33E0799614ABC22B9A7FE291F35153148D0265F4BE820C79C5476F`.
It includes the scalar comparison restoration and paired Y damage, plus the
user-requested cream/pale-blue animated cloud. Controls stay blue. Boot image
assets are unchanged. Like previous candidates, this was an integrated build
with the user's separate uncommitted work present; only owned changes were
included in these checkpoint commits.

The 90-second `poc-cream-paired-demo-20261003.log` contains 44 readouts, all in
skeleton mode. Sample medians: 21.6 FPS, CPU1 79%, CPU2 0%; draw 17.774 ms,
comparison 5.508 ms, packing 5.671 ms, conversion 2.161 ms, submission
2.923 ms and sleeping DMA wait 8.599 ms. These are individual sampled-stage
medians, not additive frame percentiles. CPU remains unresolved. No
artwork-only transition was captured and artifact disappearance has not been
visually confirmed. The user reported that the previous candidate's dirty
pixels persisted in pause. No automated checks were run for these changes.

### Skeleton substage probes

Firmware `bc7c940b` compiled with ESP-IDF 6.0.2 and eight jobs, then flashed
successfully on COM10. The 60-second capture is
`artifacts/build-cody-companion/device-evidence/skeleton-substages-20261003.log`.
No automated tests or simulator checks ran for this change.

The capture contains 30 complete sampled frame reports. The existing `Layers`
report now includes links, tangents and point markers.
Their measured durations sum exactly to the skeleton layer on successful
debug frames. Two additional execution-clock reads per drawn tile separate
the three loops; logging still happens after the frame lock is released.

Across those samples, the median completed-frame readout is about 22 FPS;
median CPU1 is 80% and CPU2 stays at 0%. The overlapping 128-frame windows
have a median of frame medians around 45 ms and p95 values around 47 ms.

Observed skeleton costs are about 3 ms for links, 3 ms for tangents and 2 ms
for markers, roughly 8 ms total. The final complete 128-frame window reports
about 46 ms mean and 48 ms p95, with every frame exceeding the 33 ms budget.
Its completed-frame readout is about 21 FPS with CPU1 78% and CPU2 0%; the
user independently reports about 20 FPS and 80% on one core.

Compare/packing still costs about 11 ms per sampled frame. Conversion and
submission cost about 2 ms and 3 ms, with roughly 9 ms of sleeping DMA wait.
The synchronous diagnostic bundle itself costs about 113 ms every two seconds.
These are sampled elapsed stage costs, not active CPU measurements or proof
of an FPS improvement. Snapshot truncation warnings remain separate from the
complete skeleton probe records. Sustained performance work stays open.

### Pre-raster damage candidate

The next candidate implements the first two performance-plan steps without
changing the standalone CODY2D dependency boundary or the physical layout:

- Replace the recurring trace/model dump with three compact `PERF`, `STAGE`
  and `DRAW` records. Keep the monotonic microsecond tick, measured FPS/CPU,
  frame-window mean/p95/misses, stage durations, skeleton substages, payload
  pixels/bytes and the previous report's measured cost. Durations are rounded
  to whole milliseconds only when formatted; internal measurements stay in
  microseconds. Source-health transitions and operation errors remain logged.
- Compare current geometry with the last completely displayed `RigScene`.
  Filled paths invalidate their complete old/new bounds, including gradient
  changes and opacity. Strokes, skeleton links, tangents and markers track
  old/new primitive bounds and selection style. No geometry fingerprint or
  pixel approximation decides whether a change can be skipped.
- Accumulate coverage into 233 paired-row spans, then take one conservative
  compact rectangle per leased tile band. Align both axes before rasterization.
  Draw every current layer in its original order inside the dirty rectangle,
  including unchanged overlapping artwork, controls and HUD. Pixel coverage
  and gradient arithmetic keep absolute canvas coordinates; only destination
  addresses subtract the tile's horizontal origin.
- Submit the compact DMA surface directly. The live renderer no longer calls
  pixel comparison, PSRAM framebuffer copying or in-place payload packing.
  Replace the 434,312-byte pixel cache with one previous-geometry snapshot.
  The standalone BSP remains synchronous and still owns one DMA slot.
- Invalidate the HUD when its rendered strings change and obtain overlay and
  pause control bounds from their existing typed navigation table. First frame,
  route changes, content revision changes and wake from standby require a full
  redraw. Commit reference geometry and UI state only after successful frame
  retirement; retain the existing deadline, errors and DMA quarantine behavior.
  Touch polling also runs across skipped bands, and reports remain deferred
  until the complete frame has retired.

The stroke rounding guard now uses the same supported canvas extent for every
compact tile. The exact coverage predicate is unchanged. Regression cases are
added for compact/full raster parity and reconstructing frames from planned
damage, but have not been executed.

Automated regression checks remain unexecuted at the user's request. Moving
the gradient-filled body can dirty most of
its bounding rectangle, so sent bytes may increase compared with exact pixel
comparison. Inspect `dirty`, `draw`, `dma`, `bytes` and `prev_log` together in
the next authorized capture before claiming an improvement. Historical raster
costs included compare/packing; the new `draw` field does not. Double-buffered
asynchronous DMA and the second raster worker remain subsequent steps.

### Pre-raster device capture

The integrated candidate compiled successfully with ESP-IDF 6.0.2 and eight
jobs, then flashed on COM10 with verified write hashes. The binary is
1,708,672 bytes (`0x1a1280`), built as `ef48bd7a-dirty`; SHA256:
`48BBB40D913A24FFF0564B2521BDF1D7284816B37AE109B74DBEE5BDEDFD05EC`.
Build/flash logs are in `artifacts/build-cody-companion/device-evidence/`
as `pre-raster-build-20261003.log` and `pre-raster-flash-20261003.log`.

The 60-second `pre-raster-20261003.log` contains 30 complete artwork reports.
The later 45-second `pre-raster-skeleton-20261003.log` also contains artwork:
its filename identifies the intended capture, not the observed mode. The
subsequent `pre-raster-skeleton-real-20261003.log` contains 18 complete
skeleton reports. CPU2 remains 0% in both observed views.

The first capture records:

- Artwork around 29 FPS sustained from frame counters and monotonic ticks.
  Median HUD is 29.4 FPS and CPU1 45%; CPU2 is 0%.
- Median sampled frame duration 25 ms; the overlapping 128-frame windows
  have median mean 26 ms and median p95 28 ms.
- Raster about 7 ms, geometry about 1 ms, damage planning under 0.5 ms
  (displayed as 0 ms), conversion 3 ms, submission 3 ms and sleeping DMA
  wait 11 ms. Individual medians need not add to a frame percentile.
- Timing-report cost about 20 ms, down from the prior artwork capture's
  108 ms and the prior skeleton capture's 113 ms.
- Median payload 130,464 bytes per sampled frame. The previous row-span-cache
  artwork segments submitted 91,632 bytes, so payload increased about 42%.

The earlier `row-span-cache-main-loop-20261003.log` has only six stable artwork
reports split across two short active segments. Those samples show about
34 ms per frame, 10 ms raster plus 10 ms compare/packing, CPU1 75% and
roughly 27-28 FPS sustained. This is a useful directional comparison, not
matched animation-phase evidence. The new candidate materially reduces CPU
and the removed stages, but conservative geometry rectangles increase transfer
volume. Transfer is now the largest artwork stage; asynchronous DMA is the
next planned change.

The matched skeleton capture records median HUD 28.2 FPS and CPU1 68%, with
34 ms sampled frames. The overlapping windows have median mean 34 ms and
median p95 38 ms. Raster is about 15 ms: 3 ms links, 3 ms tangents and 2 ms
markers after whole-millisecond rounding. Conversion costs 3 ms, submission
3 ms and sleeping DMA wait 11 ms.

An aligned 32-bit RGB565 pair swap then replaced the byte-at-a-time panel
conversion. `rgb565-word-swap-skeleton-20261003.log` contains 17 complete
skeleton reports: conversion falls from 3 ms to 2 ms, median sampled frame
duration from 34 ms to 33 ms, CPU1 from 68% to 66%, and measured sustained
FPS from 27.9 to 28.7. Median HUD rises from 28.2 to 29.1 FPS; window mean
falls from 34 ms to 33 ms and p95 from 38 ms to 36 ms. Draw remains 15 ms
and sleeping DMA wait 11 ms, so the gain does not come from sending less
content. A constant-size `memcpy` pair trial instead raised conversion to
14-15 ms and was discarded before commit.

The word-swap binary is 1,708,720 bytes (`0x1a12b0`); SHA256:
`E01C7F8DA08E91AC5027583CB036067D30CA8507996EB82301D02799B84E0A73`.
Build and flash logs are `rgb565-word-swap-build-20261003.log` and
`rgb565-word-swap-flash-20261003.log` in the same evidence directory.

Prepared debug strokes then move tile-invariant link and tangent setup out of
the compact-tile loop. A separate PSRAM cache owns opaque validated stroke
coefficients; preparation commits the complete cache before drawing, while the
legacy stroke path retains its cheap off-tile rejection. Cached links and
tangents are submitted in two ordered batches so each tile validates its
surface once per skeleton substage.

The 40-second `prepared-strokes-skeleton-20261003.log` contains 20 complete
skeleton reports. Against the word-swap capture, median draw falls from 15 ms
to 14 ms and skeleton from 7 ms to 6 ms. Links and tangents each fall from
3 ms to 2 ms; markers remain 2 ms. Median sampled frame duration falls from
33 ms to 32 ms and CPU1 from 66% to 64%; CPU2 remains 0%. Measured sustained
FPS changes from 28.7 to 28.9 and median HUD from 29.1 to 29.2 because the
renderer is already close to its 33 ms schedule. Conversion remains 2 ms,
sleeping DMA wait 11 ms, and median payload is effectively unchanged at
143,628 bytes. Window mean remains 33 ms and p95 36 ms.

The prepared-stroke binary is 1,711,872 bytes (`0x1a1f00`); SHA256:
`50E7346F5BF79283AD7C8095E9DCAA1670ADA48C97125732B434B14E436D9A9F`.
The final build and verified flash logs are
`prepared-strokes-build-final-20261003.log` and
`prepared-strokes-flash-20261003.log`. The capture contains only the existing
snapshot-summary truncation warning; no allocation, display, touch or cache
error is recorded.

Prepared marker disks then extend the same cache without changing their
authored order, diameter or selected/anchor/handle colors. The selection and
scene are part of the cache provenance, and every marker is prepared before
the cache becomes valid. The 40-second
`prepared-markers-skeleton-20261003.log` contains 20 complete reports. Against
the prepared-link/tangent capture, median skeleton time falls from 6 ms to
5 ms, marker time from 2 ms to 1 ms, draw from 14 ms to 13 ms and CPU1 from
64% to 63%; CPU2 remains 0%. Median sampled frame duration remains 32 ms and
measured sustained FPS changes from 28.9 to 29.2. Links, tangents, conversion
and sleeping DMA wait remain 2 ms, 2 ms, 2 ms and 11 ms respectively. Median
payload is 144,576 bytes.

The prepared-marker binary is 1,712,304 bytes (`0x1a20b0`); SHA256:
`6843C0B613908BB4BD9C643BDD911C263497CF6EA77AF26BDE0C7049C00166CE`.
Build, verified flash and capture logs are
`prepared-markers-build-20261003.log`,
`prepared-markers-flash-20261003.log` and
`prepared-markers-capture-20261003.log` in the same evidence directory.

The artwork fill candidate keeps the original scanline crossing predicate,
intersection expression, edge order, sorting, clipping and blend operations,
but builds a conservative authored-order edge shortlist for each compact tile.
Edges whose half-open vertical range cannot contain any tile pixel center are
not revisited for every row. Eight of the nine filled paths use one palette
slot, so those paths also skip the otherwise constant gradient division; the
body gradient retains its original arithmetic. The shortlist adds 256 bytes to
the existing fill stack arrays and is compile-time bounded to the 256 authored
vertex limit.

The 40-second `fill-edge-shortlist-artwork-20261003.log` contains 20 complete
artwork reports. Against the earlier pre-raster artwork capture, median CPU1
falls from 45% to 33%, artwork raster from about 7 ms to 4 ms and frame-window
mean from about 26 ms to 21 ms. CPU2 remains 0%; measured sustained FPS is
29.4 against the 30 FPS schedule. Median stages are 1 ms geometry, 0 ms dirty
planning, 4 ms draw, 2 ms conversion, 3 ms submission and 11 ms sleeping DMA
wait after whole-millisecond rounding. Median payload is 129,272 bytes across
8 rectangles and 8 compact tiles, closely matching the earlier artwork load.

The matched 40-second `fill-edge-shortlist-skeleton-20261003.log` also contains
20 complete reports. Against prepared markers, median CPU1 falls from 63% to
55%, artwork raster from 7 ms to 4 ms, total draw from 13 ms to 10 ms and frame
mean from 32 ms to 29 ms. Skeleton remains 5 ms; measured sustained FPS reaches
29.4. This sample covers a heavier animation phase: median payload is 178,464
bytes across 10 rectangles and 10 tiles, so its 13 ms DMA wait is not a direct
regression against the earlier 11 ms/8-tile sample.

The fill-edge-shortlist binary is 1,712,560 bytes (`0x1a21b0`); SHA256:
`D06081735567ECC1A5A3A6C82BE0BEF8843ECFB4CF355736A8C95AD200AB800A`.
Build and verified-flash logs are `fill-edge-shortlist-build-20261003.log` and
`fill-edge-shortlist-flash-20261003.log`; the two capture logs use the matching
`fill-edge-shortlist-*-capture-20261003.log` names.

No automated tests, simulator checks, size workflow or validation scans ran.
Both fill-edge-shortlist captures contain 20 complete PERF/STAGE/DRAW triplets.
They contain the existing snapshot-summary truncation warning and RTC error
state; no allocation, display, touch or fill error is recorded.
The user observed no dirty pixels from the fill-edge-shortlist candidate in the
checked orientation. Rotation and power stress remain open, separate from the
compilation and performance observations.
