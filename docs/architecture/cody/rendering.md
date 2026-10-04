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

The shared lip control points are 18 percent wider, preserving each preset's
relative width and opening. The existing breathing clock now closes and opens
the mouth around that preset and adds a small width pulse; it does not add a
timer or acquire measurements. A coral-pink tongue is a separate authored fill
under the same mouth anchor and width/opening channels. It appears at opening
0.28 and above, keeping Calm, Focused, Embarrassed and Worried tongue-free during
their normal breath cycle. Sleeping and Afraid retain their narrower mouths.
The tongue clips against the evaluated lip at each raster sample, so both paths
follow the same pearl deformation. Optional authored clip ownership requires an
earlier independent filled path; nested clips and overflowing combined geometry
are rejected.

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

One optional manga personality symbol now shares the evaluated pose and dirty
frame ownership. Its fixed-palette masters, typed sizes, bounded delayed follow,
canonical device-cue gating and source-only budgets are documented in
[personality symbols](personality-icons.md). It adds no second full-canvas warp.


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
The tongue brings the authored rig to 200 points, 62 cubics and 16 paths; point
storage is bounded at 200 and the debug tangent cache at 124. Clipped fills share
the existing 256-intersection scratch between subject and containing polygon,
without an additional raster array or mask allocation. Runtime stack, timing and
physical appearance after this mouth change remain unverified.
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

`assets/cody/runtime.yaml` selects a temporary fixed raster benchmark only when
Debug execution timings and the native Pearl renderer are enabled. The worker
enters once after boot reaches Idle, independently of ordinary demo routing.
It draws the authored Ready pose with Pearl Mist and face deformation enabled,
forcing the same artwork damage region on every frame. Navigation, status and
FPS/CPU text are not painted; the diagnostic readout still uses real canonical
observations. No benchmark data enters firmware sensor snapshots.

Each run warms up for five seconds, then measures forty seconds. Frames crossing
either boundary are excluded. A PSRAM-owned bounded array retains every measured
frame (up to 2,048); `BENCH frame_us` reports the full-run nearest-rank p95, mean
and maximum after the window. `PERF` remains the separate rolling 128-frame view.
The payload must remain nonzero and identical, and missed frames use the 40 ms
budget. Changed payload, invalid timestamps, empty completion and capacity
exhaustion return errors rather than producing a successful baseline.

A transient 60-second standby override covers the run without changing stored
settings. Touch, PWR capture, display revision, expression/face changes and power
lifecycle requests interrupt it; partial counts are not reported as a completed
baseline. Exit restores the selected look, ordinary standby policy and a deferred
first-run greeting. Sleep/Wake continues through its usual lifecycle. The worker
is pinned to core 1 in this temporary benchmark configuration; CPU frequency is
not locked or measured by the benchmark. Keep the configured clock, power mode,
radio state, display rotation and logging sinks identical between captures.

`benchmark.profile_blocks: false` is the baseline. It uses the ordinary fused C
pixel loop, with two coarse sprite timestamps per tile and the existing vector
timing. `ART` reports jelly, vector (face in Pearl mode), whole artwork and whether
detail is enabled. `profile_blocks: true` is a separate diagnostic experiment:
it stores up to 16 samples before composition, times row/block preparation,
interpolation and composition, and counts sampled/transparent/written pixels.
`JELLY ns_per_sample` divides both interpolation and composition by sampled pixels
because the composition loop also visits transparent samples. Unmeasured block
details are omitted from periodic logs and the summary when `detail=0`.

Detailed profiling has six clock calls per block, two per active row and two per
sprite pass, plus counters and temporary sample storage. Its observer cost and
changed loop structure are included in jelly/artwork time; individual substages
do not sum to the full sprite duration. Compare coarse and detailed runs before
using the split to rank kernels, and use coarse mode for subsequent C/SIMD gains.
All substage values are contained in artwork and must not be added to it.

Use separate repeated captures at scales 50, 75 and 100 (the accepted range is
50--100). After changing YAML, regenerate through
`scripts/device.ps1 generate -Generator cody-runtime-assets`. A completed run is
identified by `BENCH complete`; allow for normal boot plus the 45-second run.
Restore both benchmark booleans to `false`, set `kernel: scalar`, and regenerate before ordinary UI
validation. This fixed-pose workload is the first raster baseline; a repeatable
animated workload and confirmation in the full UI remain open.

The opt-in `scripts/device.ps1 check -Check cody-raster-benchmark` suite compiles
the portable session with Pearl disabled, exercises finite-window/full-p95/error
contracts, then runs `cody-native`, including reference pixel parity for the
unprofiled, coarse and block loops across all textures, fractional samples, clipped
tiles, RGB565 orders, backgrounds, guards and partial blocks. It then repeats the
native contracts with an explicit host SIMD arithmetic model, exercising the real
packer, kernel dispatch, odd tails, alignment and injected mismatch rejection.
That model does not execute or validate ESP32-S3 instructions. This suite does not
compile the firmware worker or prove device behavior. Its execution, firmware
builds and physical captures still require explicit authorization under AGENTS.md.
The ESP32-S3 Pearl renderer starts with the 16-pixel SIMD kernel and generated
row coverage in ordinary Debug and Release rendering. Portable host builds
start with the scalar sampler. The benchmark is disabled in the authored runtime
configuration; it allocates no measurement buffer and does not interrupt the
normal startup/UI. `benchmark.kernel` selects a diagnostic override only when
the benchmark is enabled, and completion restores the previously selected
kernel. `scalar` selects the fused C sampler, `simd` selects the PIE assembly
sampler, and `simd_verify` selects an explicit verification run. Unsupported
selections return an error; they never silently substitute C.
The combined main integration at `e714737` passed the portable native/model/
benchmark contracts and the S3 Debug and Release builds with benchmark autostart
disabled. Both modes retain the common `-O2` compiler optimization; Release
excludes development frame diagnostics. The combined image has not been flashed
or timed on the device, so these builds do not establish a Release speedup.
The 2026-10-05 S3 verifier passed both the exhaustive weight
contract and a completed 489-frame live comparison at 100% scale before row
trimming was added. A fresh scalar control recorded 684 frames, mean 56,950 us,
p95 57,634 us and jelly mean 37,106 us. The first 16-pixel SIMD run recorded
697 frames, mean 55,846 us and p95 56,495 us. Skipping composition for all-zero
16-pixel blocks lowered frame mean to 55,612 us and p95 to 56,374 us over 699
frames; enlarging blocks to 64 pixels regressed to 59,794 us mean because fewer
blocks were entirely transparent. Generated per-row coverage for all five skins
then removed spans that cannot affect the destination. This run completed 766
frames at mean 50,715 us, p95 51,463 us and jelly mean 30,736 us, skipping
14,544,808 transparent pixels before packing. It is 6,235 us faster by frame
mean than the fresh scalar control, but every frame still exceeded 40 ms. An
exact shift/add replacement for `/255` in RGB565 composition regressed to
51,497 us mean and was removed. The detailed run is a separate diagnostic
experiment; its clock calls perturb whole-frame timing. It ranked preparation
and composition ahead of interpolation, without measuring their uninstrumented
standalone costs.

The same 40-second fixed route with SIMD and row coverage submitted 35,256
pixels per frame at 75% scale: mean 33,888 us, p95 34,512 us, maximum 35,147
us, with zero frames over the 40 ms budget. At 50% it submitted 15,600 pixels
per frame: mean 21,834 us, p95 22,335 us, maximum 23,179 us, again with zero
misses. These scale captures do not isolate a C/SIMD gain. The authored benchmark
configuration and device were restored to 100% afterward. A UI-wide performance
claim still requires measurement outside the fixed benchmark route.

The kernel widens packed RGBA8 texels with ZIP and computes two pixels at a time
in eight 16-bit lanes. Unsigned multiply-accumulate retains each weighted sum in
40-bit QACC lanes. Each of the two horizontal lerps and the final vertical lerp
starts with 128 and shifts by eight, preserving the original separate rounding
steps. The maximum accumulator value is 65,408 and the extracted channel is at
most 255, so the signed 16-bit extraction cannot saturate valid inputs. These
instructions and alignment semantics come from Espressif's
[ESP32-S3 TRM, chapter 1](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf).

Aligned scratch is owned by one row invocation: up to eight 48-byte pairs, 64
bytes of output and 32 bytes of constants (480 bytes of data, excluding compiler
spills). Generated per-row coverage is a union of all five skin textures, using
nonzero packed RGBA words and an exclusive right bound. Positive-step SIMD rows
skip only spans whose two bilinear source rows contain no contributing texel;
`simd_verify` retains the full visible row for live comparison. The host suite
checks coverage across every generated row and skin and compares final pixels
across scalar, SIMD and verification modes.
The C packer gathers only clamped in-bounds texels; SIMD loads never address the
flash texture or panel buffer directly. Full pairs write eight aligned bytes;
the odd last pixel uses the original C sampler. Entire zero-footprint blocks
skip interpolation and composition exactly. RGB565 arithmetic and byte swapping remain the
original code. Packing, call overhead, tails and composition are all included in
the coarse jelly/frame duration; detailed `prep` additionally includes gathering.
SIMD uses task-owned CP3 state through the IDF port, without disabling interrupts
or writing CPENABLE. The benchmark worker remains pinned to core 1. The original
PIE ABI and arithmetic passed target parity before row trimming; the new
coverage skip still needs a fresh physical pixel-parity run. The 16-pixel
scratch size restores the original stack footprint.

`simd_verify` first checks the actual kernel against independent per-channel C
arithmetic over all 256x256 weight combinations and four distinct pixel
footprints (262,144 samples), including zero-count, output guards and immutable
input checks. It then compares every live RGBA sample before composition during
the fixed route. Any difference returns `ESP_ERR_INVALID_RESPONSE`; the frame
is rejected rather than replaced with scalar output. Successful completion
requires nonzero compared and vectorized counts and logs `BENCH verification
complete`. PERF/STAGE timing results are suppressed during this run, because
running both implementations would contaminate a performance comparison.

After an explicitly authorized verification passes, change only `kernel: simd`
for a separately authorized timing capture with detail disabled, then compare
with a fresh `scalar` run under identical conditions. Keep the distinction between
source-level arithmetic bounds, host-model contracts and executed S3 evidence;
none is interchangeable with another. The fixed-route gains above do not
establish frame times in the combined main UI. Physical motion/rotation, long-run buffer
retirement, high-water memory and the intermittent display/touch stop remain open.
