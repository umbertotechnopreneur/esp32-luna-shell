# Cody personality channel

Cody displays one manga symbol in the main character view or the full-size Sleep
presentation. Onboarding, greetings, settings, QR, boot and small-header scenes
disable the channel. Scene permission is independent of `CodySize`: the ten
stable size identities and scalable masters remain available, while Micro still
suppresses symbols. All skins use the same fourteen fixed-palette masters.

## Selection and ownership

`set_personality_cue()` selects a narrative target. `set_personality_device_cue()`
selects None, LowBattery, Charging or NoWifi. Repeating the same request does not
restart its fade, stability period or hold. Only the latest desired identity is
retained; there is no queue of previous requests and no per-symbol animation task.

`get_personality_cue()` returns the explicit session request.
`get_personality_frame()` returns the last fully retired frame, including
opacity, bounds and `Hidden`, `Visible` or `BlockedByLayout`. It rejects reads
while the completed-frame reference is invalid. Request admission and prepared
geometry are not evidence that a frame reached the display.

The existing Cody worker owns the channel under the display lock. Real device
cues use one coherent `DeviceSnapshotService` projection for both the desired
and current channel identities, including a just-started, zero-opacity fade.
The component must be Ready, valid, fresh, error-free and timestamped. LowBattery
requires a measured percentage at or below the existing 15-percent threshold;
Charging follows the actual charging state; NoWifi requires a started/configured
radio with no connected link. Stale or disproven facts immediately invalidate
their current symbol, so artistic hold never preserves a false device status.
Canonical read/contract failures retain their real errors.

## Timing and semantic Sleep

The channel advances through Hidden, FadeIn, Visible and FadeOut using elapsed
active animation time. Each transition starts at zero on its entry frame;
visible timing is quantized by frame cadence, not by a count of frames.

| Setting | Value |
| --- | ---: |
| Stable desired target before ordinary admission | 300 ms |
| Fade in, smoothstep | 400 ms |
| Minimum hold at full opacity | 2,500 ms |
| Fade out, smoothstep | 600 ms |
| Position-follow history window | 360 ms |
| Scale-follow history window | 400 ms |
| Maximum positional separation from current anchor | 4 pixels |

Ordinary replacements let fade-in and minimum hold finish before fade-out.
Requests received during fade-out replace the single desired target. Once the
old cue reaches zero opacity, the latest stable target can enter. There is never
a crossfade between two sprites. None releases the channel through the same
ordinary hold/fade policy.

An explicit Sleeping expression in the main view, or `LifecyclePhase::Sleep`,
owns `PersonalityCue::Sleep`. Eye openness and blink values never select it.
Semantic Sleep bypasses the 300-ms stability wait. If another cue is visible,
Sleep releases its hold and lets it fade out for 600 ms before Sleep fades in.
An entirely transparent prior cue can be replaced immediately. Leaving semantic
Sleep fades its current opacity out without waiting for minimum hold. The release
intent survives pause until the controller applies it. A manually selected Sleep
symbol outside the semantic pose follows ordinary admission timing.

Physical Sleep still closes the eyes over 400 ms and holds them for 300 ms before
the power owner may continue standby. A 600-ms replacement plus a 400-ms entry
cannot finish within that 700-ms preparation. Power timing is not extended: the
panel may turn off before a replacement sequence finishes. A sustained manual
Sleeping pose can show the complete sequence. Boot/wake presentations disable
the channel according to their own panel-visibility ownership.

Pause and standby contribute zero active time. Partial-frame invalidation and
wake may re-prime geometric following, but do not replay fade/hold clocks.

## Configurable facial anchor and layout

`components/CODY_companion/include/CODY_personality_config.hpp` is the authored
source for timing and Main/Sleep profiles. The simulator reads the same values.
Each profile defines rig-local X/Y offsets from the evaluated Nose anchor and
an icon extent per nominal character scale. The renderer scales around the
icon's center, not a bottom edge or an automatically inferred body-top point.

The initial Main and Sleep profiles share offsets `(0.09, -1.14)` and extent
factor `0.20`, approximately 29 pixels at nominal Full scale 145. Sharing a
center keeps a pose transition from relocating an already visible symbol.
Separate profiles allow later art-direction adjustments without changing assets.
The already evaluated face/body supply motion; no sensor is read by this layer.

Small deformation follows the upper-lobe span with a 400-ms history window. Its
bound is proportional to base extent: at most two pixels at a 58-pixel base,
approximately one pixel at the default 29-pixel base. Position follows the same
evaluated face direction through a 360-ms window (about 180-ms mean lag), with
a radial separation limit of four pixels from its current authored anchor.
This limit applies before raster rounding; integer bounds can add up to about
0.71 pixel of center quantization.
The maximum renderer support remains 64 x 64.

The follower uses fixed 25-ms time buckets and time-weighted means. Expired
history is discarded, so an input that stops changing reaches its target within
400 ms of active animation time, at the first available rendered frame. Bucket
granularity can shorten the effective window by up to 25 ms. Retargeting never
restarts a transition deadline. There is no infinite exponential tail and no
independent drifting motion. Pause contributes no elapsed time; a geometric
reset primes the complete history at the current anchor. A large abrupt move
can invoke the four-pixel tether, prioritizing attachment over inertia.

The full status footprint `(106,37)-(362,58)` and full lateral Back target are
reserved even when chrome is transparent. Touch cannot resize or teleport the
symbol. There is no adaptive shrinking, upward search or lateral relocation.
The entire configured rectangle must fit the circle and stay above the actual
jelly/vector silhouette with four pixels of clearance.

Ordinary lack of space produces `BlockedByLayout`, clears visible channel state,
and leaves Cody rendering. Once space returns, the desired cue enters with a
fresh fade. The runtime logs admission/recovery transitions. This explicit
presentation outcome is not a swallowed rendering error: malformed geometry,
invalid identities and bad surface/configuration values still return errors.
Extreme movement may therefore suppress the decorative icon. Visual review of
the selected profile and motion envelope remains required.

## Rendering and resources

`PersonalityFrame` travels with `RigScene`. Identity, opacity, status and bounds
participate in dirty comparison; both old and new footprints are erased/redrawn.
Rasterization multiplies stored alpha8 coverage by the channel's opacity with
integer rounding and composites directly into the existing RGB565 tile.

The fourteen 64 x 64 index/coverage pairs use 114,688 bytes, plus the exact
512-byte RGB565 palette: 115,200 flash bytes before descriptors, alignment and
code. There are no per-skin copies, new framebuffer or native per-frame heap
allocations. At most 4,096 destination pixels are sampled for one complete
visible sprite frame; the initial 29 x 29 profile normally covers much less.
The geometric follower stores seventeen fixed 24-byte buckets (408 bytes).
A compile-time assertion limits the entire channel/follower state to 768 bytes;
preparation also takes one transient copy on the existing worker stack. Its work
is bounded independently of frame frequency; it adds no layer framebuffer or
separate animation worker. The 2026-10-05 native host compile reports 616 bytes
for this state, including fourteen 8-byte repeat timestamps (112 bytes).
These are host ABI sizes, not a firmware stack measurement.

These are source estimates, not compiled size, stack or FPS measurements. The
existing paired-row dirty planner can transfer gaps between the icon and body,
so sprite area alone does not predict display cost. `ART personality=` keeps
raster timing distinct while including it in total artwork time.

## Optional symbol sound

Fourteen original analytically synthesized sounds accompany the cue identities.
The versioned WAV/PCM catalog, recipes and audition reel live in
`assets/cody/personality-icons/audio/`. Clips are mono PCM16 at 16 kHz, 170-300 ms,
with -18 dBFS authored peaks. Native sample payload is 103,360 bytes before
descriptors/code. The listening reel and WAV wrappers are not firmware inputs.

Only a newly visible symbol in a successfully retired frame requests playback.
Evaluating a pose or failing a frame cannot trigger its sound. The existing
platform audio owner handles codec access through a dormant 4-KiB worker stack,
reusing its existing resampling scratch under the codec mutex. Render-side
notification performs no codec I/O and never waits for playback.

There is one in-flight attempt and no backlog: busy requests drop, actual starts
have an eight-second cooldown, and requests expire after 120 ms. Hidden scenes,
display parking and teardown suspend/cancel the channel. Quiet hours and the
current volume policy apply; zero volume suppresses playback and saved volume
is unchanged. The owner temporarily releases and restores idle codec mute.
Resampling uses the
existing codec clock. Errors remain retained and published by the audio owner;
an optional audio failure does not stop character rendering.

`CODY_personality_audio.hpp` exposes preparation, notification with explicit
disposition, suspension, session-only global enable and retained status. Catalog
entries also carry an authored enable flag. Dropped/suppressed attempts are not
retried on later frames. Listening quality, physical latency, stack use, codec
contention and PCM drain behavior remain unmeasured.

## Petting experiment

The normal Main presentation recognizes a slow continuous drag within the
displayed body above the eyes or below the mouth. Regions follow the evaluated
face/body, in the existing logical touch coordinates. Shared thresholds are
authored in `assets/cody/personality.yaml` and emitted into
`assets/cody/personality/generated/CODY_petting_config.hpp`; the simulator reads
the validated YAML. Above-eye and
below-mouth margins are 0.10 and 0.08 times evaluated scale. This is gesture
recognition from position/time, not pressure measurement.

A contact must last at least 500 ms, accumulate at least 0.16 times its initial
scale in path length and travel at least 0.08 times scale from its origin.
Segments faster than twice scale per second, report gaps over 250 ms, contact
over three seconds, region changes and leaving the body reject that contact.
Movement at most 1.5 pixels is treated as jitter. At Full scale 145 this is
approximately 23 pixels of path, 12 pixels of displacement and a 290-pixel/s
speed ceiling. These are initial thresholds requiring device tuning.

Recognize once per contact with a six-second cooldown. If both the requested
cue and the occupied channel permit it, request Affection for four active
seconds, then release through the existing hold/fade policy. An occupied
non-Affection channel drops the reaction; there is no delayed heart queue.
The latest explicit baseline is preserved. YAML priority arbitration lets an
admitted LowBattery900 release an automatic Affection200, while Charging100
waits for its expiry. Urgent transitions bypass stability/hold but keep the
smooth outgoing fade. Repeat admission cooldown and battery enter/exit
hysteresis prevent chatter; see [personality behavior](personality-behavior.md).
Sleep, scene changes, pause, orientation changes and display parking cancel
the transient gesture. A visible Sleep handoff keeps the normal smooth
heart fade-out before the Sleep fade-in; hidden or parked contacts do not replay.
Normal release ends the contact while the accepted heart
can finish. Debug skeleton mode does not recognize petting. Body drag still
uses its existing canonical publication and deformation response.

The Windows source supports the same synthetic pointer flow in the normal Rig
view, with Debug geometry disabled and symbol selection None. The existing
optional Affection sound follows completed-frame audio admission; no new sound
or direct touch-side playback was added. No simulator run, new build, listening
or physical touch validation has been performed for this increment.

## Validation and integration status

The Debug Cody firmware configured, compiled and linked successfully on
2026-10-04 using ESP-IDF 6.0.2 and eight build jobs, at implementation checkpoint
`2518f83a`. The image is `0x3c5870` bytes and its application partition retains
73 percent free. Build evidence is retained under
`artifacts/build-cody-companion-debug/evidence/personality-icons-debug-build-20261004.log`.

The focused `scripts/device.ps1 check -Check cody-personality` suite passed
before the petting increment on 2026-10-04: 748 native assertions and 86 native/
Python parity frames, plus fourteen icon and fourteen audio asset contracts.
The preserved report is under
`artifacts/build-cody-companion-debug/evidence/personality-host-20261004/report.json`.
On 2026-10-05 the expanded suite compiled and ran 1,041 native assertions, with
two failures in new zero-opacity handoff expectations. Those assertions now
check the controller identity and empty rendered frame; a corrected run awaits
fresh authorization. Native/Python parity and independent host fixtures were
not reached. First-attempt evidence is preserved at
`artifacts/build-cody-companion-debug/evidence/personality-host-20261005-first-attempt/`.
Gesture trace parity has not been established. Historical results do not
validate the new combined sources. The suite compiles the
actual portable vector controller/raster/damage sources and compares sequential
frames with the simulator; it also checks the icon and PCM/WAV asset contracts.
An installed host C++ developer environment, Pillow and PyYAML are required.
No independent size report, interactive simulator run, audio playback, validation scans or
hardware operation has been performed. Readability, physical clearance, FPS,
stack/codec contention and Sleep behavior remain unverified on the device.

The isolated worktree is `E:\ESP32 Luna Cody Emotions`, branch
`feat/personality-icons`, originally based on `290b6da3`. Remote main at
`4d026d13` was integrated on 2026-10-05, including mouth/tongue breathing,
the authored boot greeting, pairing timing and BLE/display DMA ownership fixes.
The combined worker preserves atomic parked-frame classification alongside
the happiness clock and transient heart cancellation. The independent renderer
optimization worktree remains separate. Combined validation is recorded above.
