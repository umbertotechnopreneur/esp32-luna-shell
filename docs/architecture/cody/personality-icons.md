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
A compile-time assertion limits the entire channel/follower state to 512 bytes;
preparation also takes one transient copy on the existing worker stack. Its work
is bounded independently of frame frequency; it adds no layer framebuffer or
separate animation worker. These source assertions have not yet been compiled.

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

## Validation and integration status

This increment has asset generation and static source review only. No tests, validation scans, build, size,
simulator run, audio playback or hardware operation has been performed. Native
and simulator behavior, minimal readability, extreme motion, pause/rotation,
old-pixel erasure and resource costs require explicitly authorized validation.

The isolated worktree is `E:\ESP32 Luna Cody Emotions`, branch
`feat/personality-icons`, originally based on `290b6da3`. Newer main changes and
the independent renderer optimization worktree have not been merged here.
