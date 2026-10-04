# Cody rendering, rig and response

Cody stays front-facing. Approved artwork and named controls are authored data,
not renderer guesses. The standalone graph shares display/touch owners without
LVGL, a runtime YAML/PNG parser or an SD dependency for core visuals.

## Authored rig and assets

`assets/cody/cody.yaml` defines the versioned point/curve/path/entity/color graph,
face bindings and response parameters. Typed names/IDs must be unique; ambiguous
references or colliding generated identities fail. Parents precede children,
viewer-left/right stays consistent and face anchors are explicit. Generated
immutable C++ tables live in internal flash; never hand-edit them. The RGB565
source palette has 256 entries; partial coverage/alpha compositing can legitimately
produce blended display colors outside that palette.

The face has ten channels and the contour has four controls: fullness, amplitude,
speed and phase spread. Lobe motion affects authored anchors/descendants, not a
frame-random silhouette. Integrated phase follows speed without resetting on
preset changes; zero amplitude/fullness preserves the source outline. Face and
contour smoothing/breath/blink phase also survive preset changes and pause. Vector
drawing, debug geometry and semantic hit testing consume the same evaluated pose.

The active native jelly finish derives from approved concept 08 in
`assets/cody/appearance/jelly-scanline/`. Five skins share the crop
`(28,206,1229,1082)`, matched alpha, 320×233 premultiplied RGBA8 and RGB565-quantized
color with 8-bit alpha. Pearl Mist, Aurora Veil, Cosmic Stardust, Enchanted Mosaic
and Dreamwave use stable IDs in `skins.json` and shared bindings, not duplicated
rigs. Lighting is baked paint: there is no dynamic refraction, blur or height field.

Thirty-three weighted row controls bind lobes and a pinned center into destination
left/right/y supports. Q16 inverse bilinear sampling deforms alpha and paint
together; nonfinite, collapsed or folded row maps fail. A row warp cannot move two
interior points at the same y independently; stronger local shear would require a
different mesh and is not implemented. Face anchors/handles/vertices use the exact
forward body warp before their bounds are recalculated.

`CODY_companion_set_face_deformation()` is session-scoped, lock-protected
and queues damage. While paused it rebuilds the saved pose without advancing
time or inventing telemetry. Its getter reports the requested state, not proof
that a DMA frame displayed it. Jelly and face-follow are selected in the current
profile; explicitly disabling jelly excludes its textures/caches and selects the
normal vector path, never a fallback after renderer error.

## Snapshot-driven response

Only `DeviceSnapshotService` supplies real firmware inputs, validity/health,
errors and original timestamps. Cody never reads sensors/BSP/I2C directly or
uses stale data as current. The response consumes acceleration, not gyro; its
high-pass motion cue is not a validated gravity-free linear-acceleration estimate.
The authored basis maps board Y→panel X and board X→panel Y with unchanged positive Z;
physical sign confirmation remains open. Panel-space springs apply inverse
cardinal rotation once; touch is already logical and must not be rotated again.

The first valid sample primes a baseline. Duplicate timestamps are not filtered
twice; stale input removes drive and disarms the baseline, so the next fresh
sample primes rather than creating a fake impulse. Standby also rebases. Future,
reversed, nonfinite or out-of-range input retains an explicit error rather than
clamping a raw measurement. The canonical IMU owner has an active nominal 100-ms
path independent of full-snapshot cadence; this is not measured physical cadence.

Analytical second-order springs do bounded work without catch-up loops or
per-frame allocation. Normalized stress/fatigue are artistic state, not health or
emotion measurements. Unavailable battery holds the artistic contribution with
unknown source status; charging never manufactures a battery percentage. Actual
microphone mean/peak comes from successful reads and playback level only follows
successful codec writes. Attention suppresses microphone contribution during own
playback without changing the measurement; audio level is not speech detection.

Drag uses canonical pressed/contact data, not measured force. Gesture impulses
are consumed once even when reports arrive between frames; re-rendering does not
repeat a tap. Evaluated body/eye hit regions exclude navigation. Eye taps close
the relevant eyelid and affect brows; repeated taps restart the event. Active-time
idle/yawn behavior is authored, with pause/standby excluded. YAML owns gains and
timings rather than duplicated tuning constants in this guide.

## Damage, tiles and DMA

Keep 466×466 logical coordinates and a `(233,233)` center. The safe square is
half-open `[73,393)` on both axes, not a rectangular replacement for the circle.
Use the actual circle chord for controls/QR bounds. Drawing, clipping, gesture
and hit testing share the same 0/90/180/270 orientation transaction; text is never
mirrored. Exact navigation/footer exceptions belong in
[circular layout](../circular-display-layout.md).

The renderer retains geometry of the last completed `RigScene`, not a full RGB
pixel cache. Old/new contour, face, opacity, style and sprite footprints feed 233
paired-row damage spans. Bands use conservative rectangles, half-open even bounds
in both axes and normally a 32-row internal-DMA tile lease. A capacity below two
raster rows returns an error instead of transmitting an odd rectangle. Redraw all
overlapping layers in original order inside each compact rectangle, using absolute
canvas gradient/coverage coordinates. RGB565 transport byte order is converted in
word pairs; native pixels are not copied out of a persistent packed framebuffer.

Vector edges use four vertical coverage subsamples plus 1/256-pixel horizontal
coverage, blending once; opaque full spans have a fast path. Fixed prepared debug
stroke/marker geometry and bounded fill shortlists preserve order and identity.
The BSP panel clock is unchanged. No second asynchronous DMA/render worker has
been added.

Commit geometry/text/content references only after complete successful display
retirement. A failed/partial frame invalidates the reference and forces a full
redraw. Route/rotation/wake also invalidate appropriately. An ambiguous DMA timeout
quarantines its buffer rather than freeing or claiming it for reuse while the
device may still own it. The 500-ms completion deadline is an error bound, not FPS.
The conservative geometry plan can transfer more than an exact pixel-difference
cache; avoiding a framebuffer is an ownership/memory choice, not a measured gain.

## Authored memory budgets

| Resource | Source budget / ownership |
| --- | --- |
| Five jelly textures | 298,240 bytes each; 1,491,200 total internal-flash data |
| Title font | 95 printable ASCII glyphs, 19×40, 18-pixel advance, packed 2-bit coverage; 19,000 flash bytes |
| Caption/action font | 18-pixel printable ASCII coverage; 7,125 flash bytes |
| Three pearl boot resources | 181,207 flash data bytes, excluding descriptors/alignment |
| Jelly destination supports | 33×12 = 396 bytes per prepared scene in PSRAM |
| Vector coverage scratch | 3,718 bounded bytes, not supersampled whole-frame storage |
| Display worker | 12-KiB PSRAM stack; display buffers require their own internal DMA budget |
| Screenshot exception | 434,312-byte PSRAM buffer allocated only on explicit capture |

These are authored payload/structure budgets, not a current linked-image size or
measured free-heap/high-water result. Fonts have no Unicode ranges, TTF runtime or
heap-owned text. Ordinary scenes add fixed state, not a framebuffer/render task.
Texture data, five catalog entries and core graphics stay available without SD.
Optional action/audio worker budgets are in [onboarding](onboarding.md) and
[runtime](runtime.md); flash capacity cannot substitute for internal RAM.

## Generation, host preview and performance evidence

Run asset preparation only through `scripts/device.ps1 generate` with the selected
`cody-rig`, `cody-jelly` or `cody-scenes` generator. Scene definitions-only changes
may use `--definitions-only`; font regeneration requires explicit `--font-path`,
not installed-font substitution. Manifests bind normalized source and glyph/asset
hashes; stale/corrupt preview data fails. Boot artwork uses its approved separate
packager and is not recolored automatically. Title/caption coverage carries the
JetBrains Mono [SIL Open Font License](../../../assets/cody/scenes/OFL.txt).

The Windows simulator uses shared authored geometry/equations and visibly synthetic
inputs that never enter firmware snapshots. Responsive layout/input use one current
presented transform; resizing does not advance animation time. Pillow rasterization
and host timing are not native pixel/performance parity. The jelly study mirrors
the implemented native path; opt-in material/PNG-mesh studies remain host
appearance studies, not firmware feasibility evidence. Simulator execution is validation
and still requires explicit request.

Debug stage benchmarks count 128 completed frames per overlay mode, then report
mean, lower-middle median, nearest-rank p95, maximum, population deviation and
budget misses after releasing the lock. Partial/paused diagnostic frames are not
animation evidence. DMA wait is blocked elapsed time, not CPU work; stage medians
do not sum into a frame percentile. The Pearl target is 40 ms (25 FPS), and its
budget has the same value; neither establishes a measured frame rate or CPU
reduction.

`assets/cody/runtime.yaml` has a temporary Debug fixed raster route. With
`benchmark.enabled: true`, it bypasses first-run routing after boot and repeatedly
draws the authored Ready pose at `benchmark.cloud_scale_percent`; it does not read,
alter or publish a synthetic snapshot. Run one capture per scale (for example 70,
100 and 130), with the same firmware, rotation, board state and 30--60-second
duration. Restore `enabled: false` before ordinary Cody validation.

The `ART` record reports total jelly work plus block preparation, interpolation,
blending and face/vector work in microseconds. `px` is
sampled/transparent/composited; `ns_px` is interpolation per sampled pixel and
blending per composited pixel. Sampling and composition are timed in 16-pixel
blocks, so their timestamps include the temporary sample-buffer preparation rather
than one timer call per pixel. All substage values remain within `artwork_us`, and
must not be summed with it. Transparent-footprint/alpha skips are source-only
optimization stage 1; exact-output checks and device gains are pending.
Physical motion/rotation, long-run buffer
retirement, high-water memory and the intermittent display/touch stop remain open.
