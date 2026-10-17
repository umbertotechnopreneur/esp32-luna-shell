# Cody personality channel

Cody displays one manga symbol in the main character view or the full-size Sleep
presentation. The automatic clock greeting during Idle belongs to the main
companion view and permits symbols and carezze. Explicit onboarding/greeting
routes, settings, QR, boot and small-header scenes disable the channel.
Scene permission is independent of `CodySize`: the ten
stable size identities and scalable masters remain available, while Micro still
suppresses symbols. All skins use the same fourteen fixed-palette masters.

## Selection and ownership

`set_personality_cue()` selects a narrative target. `set_personality_device_cue()`
selects None, LowBattery, Charging, NoWifi, WifiConnected, BatteryReady or
BatteryLow. Repeating the same request does not
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
requires a measured percentage at or below the 20-percent threshold;
Charging follows the actual charging state; NoWifi requires a started/configured
radio with no connected link. Stale or disproven facts immediately invalidate
their current symbol, so artistic hold never preserves a false device status.
Canonical read/contract failures retain their real errors.

On a real software-standby wake into Working, a current detected battery with
a valid percentage can precede working content for four visible seconds.
BatteryLow is red with a worried face at or below 20 percent; BatteryReady is
green with a happy face above that threshold. Charging, absent, invalid or stale
battery data skips this prelude. No USB connection is inferred from absence.
Backend processing continues; route changes or unavailable layout cancel the
prelude. Its timer starts only after a visible frame retires.

Four native compile-time 64 x 64 alpha planes complement the fourteen bitmap
masters: WifiConnected, Search, BatteryReady and BatteryLow. These new cues are
silent; Search animates eight small lens poses without per-frame trigonometry or
a separate task. Runtime rendering and stack headroom remain unverified.

From 2026-10-11 the firmware admits short battery notices after ten uninterrupted
seconds on free Home. Touch, affection, other narrative cues, work, questions,
setup and technical panels defer admission. Semantic Sleep retains priority;
notices never extend standby or replace a route. The latest supported pending
event is retained, not a queue of old battery announcements.

| Current fact | Caption | Manga | Repeat |
| --- | --- | --- | --- |
| Discharging at or below 20 percent | A little tired. Charge me? | LowBattery | Five awake minutes |
| Discharging at or below 10 percent | Please charge me soon. | LowBattery | One awake minute |
| Actual charging starts | Thanks! Recharging. | Charging | Once per observed entry |
| Measured percentage reaches exactly 100 | All charged. Happy! | Joy | Once per observed entry |

The caption and matching visible manga dwell together for four awake seconds.
Charging without a valid percentage can announce its real charging state;
continuous charging is otherwise silent. No percentages, cable-presence claims
or spoken synthesis are added. The existing optional cue sound remains governed
by Sounds/quiet preferences. A 25-percent recovery re-arms low warnings; critical
warnings re-arm at 15 percent. Source loss immediately clears pending and active
notices. These facts refer to Cody's battery, not the PC battery.

The canonical status projection is retained by the display owner instead of
copied onto the nested frame stack. Studio's manual power studies retain the
native factual selector and explicitly synthetic facts; they do not establish
the firmware's automatic notice timing or caption behavior. That integration,
compiled frames, stack headroom and physical behavior remain pending validation.
The previous results below do not validate this increment.

## Timing and semantic Sleep

The channel advances through Hidden, FadeIn, Visible and FadeOut using elapsed
active animation time. Each transition starts at zero on its entry frame;
visible timing is quantized by frame cadence, not by a count of frames.

| Setting | Value |
| --- | ---: |
| Stable desired target before ordinary admission | 300 ms |
| Fade in, smoothstep | 400 ms |
| Minimum hold at full opacity | 2,500 ms |
| Fade out, smoothstep | 400 ms |
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
Sleep releases its hold and lets it fade out for 400 ms before Sleep fades in.
An entirely transparent prior cue can be replaced immediately. Leaving semantic
Sleep fades its current opacity out without waiting for minimum hold. The release
intent survives pause until the controller applies it. A manually selected Sleep
symbol outside the semantic pose follows ordinary admission timing.

Physical Sleep still closes the eyes over 400 ms and holds them for 300 ms before
the power owner may continue standby. A 400-ms replacement plus a 400-ms entry
cannot finish within that 700-ms preparation. Power timing is not extended: the
panel may turn off before a replacement sequence finishes. A sustained manual
Sleeping pose can show the complete sequence. Boot/wake presentations disable
the channel according to their own panel-visibility ownership.

Pause and standby contribute zero active time. Partial-frame invalidation and
wake may re-prime geometric following, but do not replay fade/hold clocks.

## Configurable facial anchor and layout

`assets/cody/personality.yaml` is the authored source for timing and Main/Sleep
profiles. The generator emits `assets/cody/personality/generated/CODY_personality_config.hpp`;
the simulator reads the same validated YAML values.
Each profile defines rig-local X/Y offsets from the evaluated Nose anchor and
an icon extent per nominal character scale. The renderer scales around the
icon's center, not a bottom edge or an automatically inferred body-top point.

Main and Sleep share local offsets `(0.09, -1.14)` and base extent factor `0.20`.
The readability correction moves their centers 30 logical pixels right
and 40 pixels down. The 2026-10-06 revision increases the previous threefold
size by 20 percent, to a 3.6 multiplier around the same center.
At nominal Full scale 145 the side rounds from 104.4 to 104 pixels. Greeting
rounds to 94 pixels and Medium 76; smaller eligible sizes retain the existing
12-pixel base minimum, now displayed at 43 pixels. Micro still suppresses the
channel before geometry preparation. Scene permission remains independent of
size, so an excluded onboarding/settings/header scene does not gain symbols.
Sharing a center keeps a pose transition from relocating an already visible symbol.
Separate profiles allow later art-direction adjustments without changing assets.
The already evaluated face/body supply motion; no sensor is read by this layer.

Small deformation follows the upper-lobe span with a 400-ms history window. Its
bound is proportional to base extent: at most two pixels at a 58-pixel base,
approximately one pixel at the default 29-pixel base. The final 3.6-fold
presentation multiplies that deformation too. Position follows the same
evaluated face direction through a 360-ms window (about 180-ms mean lag), with
a radial separation limit of four pixels from its current authored anchor.
This limit applies before raster rounding; integer bounds can add up to about
0.71 pixel of center quantization.
The maximum destination support is 231 x 231; the source masters remain 64 x 64.

The follower uses fixed 25-ms time buckets and time-weighted means. Expired
history is discarded, so an input that stops changing reaches its target within
400 ms of active animation time, at the first available rendered frame. Bucket
granularity can shorten the effective window by up to 25 ms. Retargeting never
restarts a transition deadline. There is no infinite exponential tail and no
independent drifting motion. Pause contributes no elapsed time; a geometric
reset primes the complete history at the current anchor. A large abrupt move
can invoke the four-pixel tether, prioritizing attachment over inertia.

The full status footprint returned by `status_overlay_damage_bounds()` and the lateral Back target are
reserved even when chrome is transparent. Touch cannot resize or teleport the
symbol. There is no adaptive shrinking, upward search or lateral relocation.
The entire configured rectangle must fit the circle and avoid both exclusions.
Main/Sleep explicitly allow the enlarged symbol to overlay Cody's upper body;
the symbol is painted after the character so it remains visible. Profiles that
disable body overlap retain the four-pixel silhouette clearance. Geometry is
not relocated or shrunk when a pose lacks room.

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

The fourteen logical 256 x 256 index/coverage pairs retain their complete
reference binaries and PNGs. Immutable flash storage uses the minimal rectangle
containing every nonzero-alpha pixel: 1,475,720 bytes including the exact
512-byte RGB565 palette, before descriptors/alignment. Omitted pixels remain
transparent in logical coordinates; colors, opacity, sampling and placement
are unchanged. This saves 302,904 bytes against the former fixed border and
359,800 against full planes, without runtime decompression or allocation.
The native contract compares every logical pixel and actual row composition
against the full raw planes at both byte orders and two opacity levels.

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

There is one in-flight attempt and no backlog. The current visible icon can
retry busy admission during its first 400 ms, including a worker-side busy
outcome. Once dispatched, the runtime observes its sequence until playback
starts or a terminal outcome occurs; it never plays the same appearance twice.
The worker waits at most 50 ms for a short click to release the codec.
The visual channel owns repeat cooldowns; there is no second global audio
cooldown. Requests expire after 400 ms. Both fade-in and fade-out take 400 ms. Hidden scenes,
display parking and teardown suspend/cancel the channel. Quiet hours and the
current volume policy apply; zero volume suppresses playback and saved volume
is unchanged. The owner temporarily releases and restores idle codec mute.
Resampling uses the
existing codec clock. Errors remain retained and published by the audio owner;
an optional audio failure does not stop character rendering.

`CODY_personality_audio.hpp` exposes preparation, notification with explicit
disposition, suspension, session-only global enable and retained status. Catalog
entries also carry an authored enable flag. Quiet/muted/failed attempts are not
retried. Listening quality, physical latency, stack use, codec
contention and PCM drain behavior remain unmeasured.

Each physical press edge in the awake Ready UI also requests a 20-ms feedback
click through the existing typewriter worker. Drag samples and a held contact
do not retrigger it. Touch requests survive scene-text suspension, expire
quickly, and are cancelled by transport standby. The click reuses the original
flash impulse with a bounded gain; saved volume and canonical quiet policy apply.

## Petting experiment

The normal Main presentation, including the automatic clock greeting while
Idle, recognizes a slow continuous drag within the
displayed body above the eyes or below the mouth. Regions follow the evaluated
face/body, in the existing logical touch coordinates. Shared thresholds are
authored in `assets/cody/personality.yaml` and emitted into
`assets/cody/personality/generated/CODY_petting_config.hpp`; the simulator reads
the validated YAML. Above-eye and
below-mouth margins are 0.10 and 0.08 times evaluated scale. This is gesture
recognition from position/time, not pressure measurement.

A contact must last at least 300 ms, accumulate at least 0.12 times its initial
scale in path length and travel at least 0.04 times scale from its origin.
Segments faster than twice scale per second, report gaps over 250 ms, contact
over three seconds, region changes and leaving the body reject that contact.
Movement at most 1.5 pixels is treated as jitter. At Full scale 145 this is
approximately 18 pixels of path, 6 pixels of displacement and a 290-pixel/s
speed ceiling. These are initial thresholds requiring device tuning.

Recognize once per contact with a six-second cooldown, then request Affection
for four active seconds and release through the existing hold/fade policy.
Affection interrupts ordinary lower-priority symbols instead of silently
dropping the recognized stroke. Its icon has no second repeat cooldown;
the gesture recognizer owns repeat admission. There is no delayed heart queue.
The latest explicit baseline is preserved. YAML priority arbitration lets an
admitted LowBattery900 release an automatic Affection200, while Charging100
waits for its expiry. Urgent transitions bypass stability/hold but keep the
smooth outgoing fade. Repeat admission cooldown and battery enter/exit
hysteresis prevent chatter; see [personality behavior](behavior.md).
Acquired eye taps still close the eyes and add artistic stress. Main additionally
requests Fear (the startled manga mark) or Frustration for four active seconds.
The first choice favors Frustration under prior stress or a Stressed preset;
successive contacts alternate these two identities. Both use priority 300 and
interrupt lower-priority symbols, while Sleep and valid LowBattery retain
precedence. Their existing original SFX follow completed visible frames, with
the same busy/quiet/volume/expiry safeguards. No archive sample was imported.
Sleep, scene changes, pause, orientation changes and display parking cancel
the transient gesture. A visible Sleep handoff keeps the normal smooth
heart fade-out before the Sleep fade-in; hidden or parked contacts do not replay.
Normal release ends the contact while the accepted heart
can finish. Debug skeleton mode does not recognize petting. Body drag still
uses its existing canonical publication and deformation response.

Contact diagnostics record admission at press and the recognizer state at
release, without per-move logging. `Mood caress` proves recognition; the next
message distinguishes a requested heart from an occupied-channel suppression.
`Personality layout blocked` reports nonfatal layout suppression separately.

The Windows source supports the same synthetic pointer flow in the normal Rig
view, with Debug geometry disabled and symbol selection None. The existing
optional Affection sound follows completed-frame audio admission; no new sound
or direct touch-side playback was added. The merged Debug build and host suite
passed on 2026-10-05 and the Rig simulator opened; listening, interactive gesture
evaluation and physical touch validation remain open.

## Evidence

Generated artwork, portable native parity, target compilation and physical
gesture/visual/audio quality are separate observations. Dated results, including
the 2026-10-04/05 suites and 2026-10-07 artwork revision, remain in
[done.md](../../done.md). [TODO.md](../../TODO.md) owns current validation gates.
Original transcripts/worktree history are preserved at Git checkpoint `9956446`.
