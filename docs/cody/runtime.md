# Cody runtime and lifecycle

The display worker owns scene/rig state under the shared display lock. Hardware,
settings, radios, power and audio retain their platform owners. A frame borrows
immutable state until every display transaction retires; partial writes never
become the next displayed-state reference. See [rendering](rendering.md) for the
damage/DMA contract and [onboarding](navigation.md) for route gates.

## Minimal character direction - 2026-10-10

The 2026-10-11 increment stays within the Codex companion. The shared director
adds restrained, continuously composed Thinking/Editing/Running accents,
one double blink on an observed attention edge, a smile and contour squash/stretch
on an observed same-turn reply, and a short puzzled reaction on interruption.
The PC approval route permits the attention performance while retaining its
existing controls. Other questions, setup and exact Debug/preset poses keep their
own presentation ownership.

Reply/interruption acting requires the exact current chat and turn identity from
coherent ACTIVITY/TECH publications. A missing/stale/mismatched turn, source gap,
pause, hidden route or rebase discards completion continuity. Repeated Ready or
Attention frames never restart a performance; animation elapsed time is not
evidence of task completion. Existing explicit compaction presentation, including
its Home indicator and exact sequence-end release, remains independent.

Character observations remain bounded to 96 bytes and their display-owned state
to 208 bytes after adding the exact turn identity and reaction clock. Transaction
copies remain bounded; compiled nested frames and physical stack headroom require
the final validation recorded in TODO.md. No task stack was enlarged.

Python only transports typed source observations into the native C++ director;
the new host code adds no operating-system observers or companion service.
Prepared native/Studio regression cases have not yet been executed for this
increment. Earlier 2026-10-11 results describe the preceding revision.

The initial `CODY_character_direction` revision added three artistic contributions after the
existing native response: gaze towards acquired body touch or visible information,
positive warmth after a recognized caress, and concentration/release during genuine
compaction. Rendering and touch keep the existing logical coordinates. Gaze can
follow the last fully retired manga bounds without a second face/icon evaluation.
An approval faces forward. No person tracking or new sensor sampling is introduced.

The display-lock owner retains bounded inputs (at most 96 bytes) and direction
state (at most 208 bytes). Existing native motion, blinking/protective eye closure,
audio-driven mouth opening, breathing, quiet hours, day/night routine and Desk/Away
policy retain their owners. Debug, explicit expression presets, setup and the
startup/Sleep/Wake lifecycle suppress these contributions and consume hidden events.
This work does not change expression debug screen 38 or Codex transport admission.

Compaction requires current canonical link/activity and technical observations with
matching chat and ACTIVITY revision. Concentration slightly narrows the mouth,
changes eye curves and raises one brow; the existing Confusion manga cue is requested
only when its current channel is free. A two-second stretch/smile requires an
observed current `Active -> Refreshed` transition of the same compaction sequence.
Source gaps, stale data, another chat, interruption and resume cannot manufacture
completion. They can suppress this optional release gesture. Existing Home
compaction text/status remains independently visible through the established UI.

Pause freezes artistic time while consuming source edges. A caress leaves a
30-second active-time warmth tail, blending back gently; positive happiness above
the authored initial value contributes additional warmth. Low/unattended happiness
does not create a sad expression. All numeric mood rules and their awake clock stay
unchanged. Physical timing, visual strength and nested stack headroom remain
unverified. The registered `cody-runtime` character cases and Studio execution
have dated passing evidence in [done.md](../../done.md).
Studio now binds this same director through its native mood/caress owner. The
ordinary rig supplies fresh acquired desktop pointer targets and recognized
caresses. SIMULATED CCP supplies visible text/pointer targets and coherence-qualified
compaction sequence IDs; changing the source or rotation breaks completion
continuity. Debug, explicit presets, setup and questions other than PC approval
disable the contribution.
The existing Long compaction and Refreshed / pending preview scenarios retain one
rendering-only sequence; opening the latter alone cannot fabricate an observed end.
Python does not duplicate the director's animation equations, and synthetic facts
never enter firmware snapshots. The preceding three-contribution revision's drivers
and named synthetic cases passed on 2026-10-11; this does not validate the later
Codex acting/power-cue increment or establish device behavior. Optional extra gestures stay in
[planned-todo.md](../../planned-todo.md); authorized generation/build/device
work is tracked in [TODO.md](../../TODO.md).

## Startup and expression sequencing

Cold boot or software reset first shows the approved VibeWare floppy insertion
animation on black: 42 original frames with 3.5 seconds of authored visible dwell
per cycle. Each native frame retires before its 80/90-ms dwell begins; transfers
and intentional startup DMA parking can add wall time. The current cycle finishes
only after the real Ready service checkpoint. The original Sleeping/Waking/Ready
clocks remain frozen under the introduction, including first-run greeting timers.
The unchanged GIF and shared flash RLE package live in
`assets/branding/VibeWare/`; the native path adds no LVGL or GIF-decoder dependency.

After the introduction, startup follows Sleeping 01 → Waking 02 → Ready 03. The animated
character fades in over 2 seconds, then Waking receives 1.5 seconds and Ready
receives 2 seconds (5.5 active seconds altogether, excluding real service waits).
Real startup checkpoints gate advancement: a service wait does not consume the
next phase's active duration. The VibeWare introduction precedes Cody, with no
Debug HUD or numeric progress invented from animation time. Actual deep-sleep
and ordinary standby wake keep their existing lifecycle and omit the logo.

First setup selected by an absent saved Wi-Fi profile enters Hello 04 with Happy and `Hi, I'm Cody!` at
`(233,225,130)`. Its 13 characters use 70 ms each; after the complete text has
actually retired to the panel it remains readable for one active second before
05. There is no second bitmap intro. Normal startup now selects network setup,
the optional Codex choice (40), reconnect waiting (42), or authenticated PC search
from the canonical saved-profile/link state; see [onboarding](navigation.md).
Only explicit development force-first-boot replays 04 regardless of that state.
Returning devices with validated first-boot false receive one four-second
random Affection or Joy request on entering Idle, using the existing icon and
completed-frame SFX owner. Urgent symbols and real interactions retain priority;
hidden requests are dropped. No pairing or credential keys are rewritten by this
presentation change. The requested 2026-10-06 Navigation Atlas revision documents
this boot sequence, with earlier pages retained and dated errata appended.

`cody_firstboot` is a validated persisted boolean. Only a genuinely absent key
logs and persists the default `true`; invalid type/value or storage failure stops
that operation. `CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT` is an explicit `y`/`n`
development boolean. The Cody profile sets it to `n`, so a valid saved false is
authoritative. After the current pairing attempt has healthy Wi-Fi and a nonempty
IP, `persist_first_boot(false)` writes and reads back the value before success UI;
write, readback or mismatch failure stays visible and retryable. Completing Hello
text alone never clears the key.

Expressions are separate from routes. Stable, case-sensitive catalog keys include
`calm`, `happy`, `sleeping`, `drowsy`, `embarrassed`, `boot.sleep`, `boot.waking`
and `boot.ready`; ambiguous `35` is rejected, while Sleep 35A and Wake 35B are
distinct. A session expression change preserves breathing/blink phase and is
available after boot reaches idle. Power/lifecycle phases take priority; choosing
a sleeping face alone does not request physical standby. Happy keeps both eyes
open between natural blinks.

## Standby and wake

Sleep preparation closes the eyes over 400 ms, then holds the closed breathing
pose for 300 ms. Acknowledgement follows the completed frame, not a queued draw.
The power owner parks producers and applies the shared 800-ms panel fade. Its
3-second preparation deadline and the renderer's 500-ms failure deadline are
failure bounds, not frame-rate promises. Failed sleep rolls back with its real
error; failed wake leaves the panel dark rather than presenting a partial success.
Standalone wireless restoration is optional for graphical wake: a failed restore
must first stop both radios successfully. Only that known-off state permits Cody
and touch to resume; uncertain cleanup still rejects wake. The original radio
error remains in the power snapshot, return value, trace and logs until verified
radio recovery. Ordinary touch/motion publication cannot erase it. A subsequent
standby reasserts radio shutdown through its owner after any explicit pairing.

Software wake first renders Sleeping behind the dark panel, exposes it through
the shared 800-ms panel fade, then gives Waking 1.5 seconds and Ready 2 seconds. It does
not multiply a second 2-second character fade over the panel fade. Route, pending
operation, debug/pause state and scene entrance/text clocks stay in RAM; hidden
routes do not update during the lifecycle wake sequence. Before showing the
retained route, reconcile the canonical pairing/Wi-Fi state. A BLE session closed
while asleep must not resurrect its old QR. Rebase the IMU response baseline.

Cold boot/reset still uses normal startup and first-boot policy. Actual deep sleep
resets RAM and starts Wake 35B rather than the cold-boot introduction, then
re-evaluates the same saved network/link admission. Ordinary standby resumes its
retained route; there is no persisted route-resume contract. The power implementation and
profile-specific silicon-sleep policy remain in
[power management](../bsp/services/power-management.md).

Every authored scene supplies `standby_override_seconds`: zero inherits the saved
user timeout, a positive value replaces it only for the active scene. Invitation
05 and installation 06 use 60 seconds; real pairing 07 uses 90 seconds. The unused
scan definition, Online 09, Debug-only 39, 10, 36 and 13-15 use 60 seconds.
Connect Codex 12 uses 180 seconds; successful canonical code delivery refreshes
its anchor once, so preparation time does not consume the code-reading interval.
Hello 04, error 08
and look 11 inherit. Scene entry supplies a fresh
inactivity baseline and touch refreshes it. Genuine canonical progress in the
current pairing attempt also refreshes its 90-second anchor without fabricating
a touch event. Repeated unchanged snapshots do not refresh it.
Departure/parking clears the override.
This never writes NVS or extends the backend's five-minute pairing deadline.

Standby retains the active route. In particular, wake from screen 09 returns to
09; the wake contact does not invoke its Continue action. Screen 39 is available
only in Debug builds and one awake tap dismisses it. Release contains no generated
scene definition or expression-catalog mapping for 39.

## Reusable scenes and text

### Companion information panels

Home retains the character, actual activity phase, alternating project/branch
and concrete activity description. Names and descriptions use native font metrics:
short text stays still; overflow scrolls within its own viewport. Identity fades
affect only its row. Attention and authentic compaction hold the project.

A short released passive tap opens Activity, then Model & Context, then Session &
Link, then Activity again. Each panel remains selected until another deliberate
gesture. A deliberate shake returns directly to Home and changes no remote
request, chat, approval or read acknowledgement. Panels have no lateral Back.
Setup and questions keep their explicit controls and existing navigation.
An explicit setup, remote question, PC approval or Debug view can preempt the
technical compositor. Its real controls then own touch; the retained panel choice
resumes when that route ends. This is attention arbitration, not a reading timeout.

Touch recognition retains the contact's origin, displayed orientation and original
timestamps. More than 12 pixels of displacement, a contact longer than 450 ms,
a discontinuity, a control action, a caress or a long settings hold invalidates
navigation. A view opens only after the acquired release; that contact never
reaches controls on the new view. Eye reactions retain their existing action.

Shake recognition consumes only distinct, fresh canonical measured acceleration.
The acquisition owner publishes a separate bias-corrected gesture projection
before software smoothing, from the same successful read and original timestamp.
Filtered animation/fusion inputs retain their existing smoothing. Current component
health, gesture validity and timestamp agreement are required; there is no driver
read or filtered-data fallback in the renderer.
Three alternating acceleration peaks within 900 ms require a previously settled
baseline; hysteresis, a 1.5-second cooldown and a new calm interval prevent replay.
Strong opposing samples qualify even when the 100-ms acquisition cadence misses
their intervening zero crossing. Sustained same-direction samples do not qualify.
Wake suppresses navigation recognition for two seconds. Missing, stale or invalid
IMU data cancels recognition. These initial thresholds require physical calibration;
source review and serial startup do not establish gesture quality while walking.
Pausing artistic animation does not pause passive navigation sampling.

Technical panels share native formatting, numeric validity, page identities and
raster with Studio. Panel content and its coherent canonical observation belong
to the serialized PSRAM renderer state, not nested frame-stack copies. Technical
labels and numbers bypass typewriter timing and its audio. Every passive detail
page retains Cody at logical (233, 115), scale 50, above a transparent content
viewport beginning at y=170. At most four curated facts replace the old dense
field list; unavailable or exact unknown placeholder values contribute no rows.
Known STALE/INVALID states, genuine compaction and confirmed connection loss remain
visible. A page with no model information offers one concise neutral explanation.
An unmeasured context bar is omitted instead of suggesting a value.
A page transition clears the retired full composition; subsequent changes damage
the character's old/new artwork, detail viewport and independent status footprint.
The base home route continues receiving events
without forwarding panel touches to its hidden controls.

| Metric | Actual source and meaning | Availability in this integration |
| --- | --- | --- |
| Hook model | Common hook metadata for the followed chat/turn; full bounded slug | Available when an admitted genuine hook includes it; labelled Hook model |
| Reasoning effort | A per-turn authoritative field, not a global default | UNAVAILABLE: current documented hook schema does not provide it |
| Current context used / capacity | Coherent current session occupancy and declared capacity | UNAVAILABLE: no supported observer of this desktop chat's App Server channel established |
| Free context / percent / colored bar | Derived only from valid coherent used and positive capacity | Omitted until both source measurements exist; known stale/invalid remains explicit |
| Session cumulative tokens | Distinct cumulative counters, excluding double-counted subcategories | UNAVAILABLE through the currently connected plugin transport |
| Automatic compaction threshold | Explicit source threshold, distinct from capacity | UNAVAILABLE |
| Turn elapsed | Host monotonic duration since genuine UserPromptSubmit, transported as a duration | Available only for the admitted observed turn; unobserved time is not operation work |
| Observed work | Native accumulated active observation intervals | Explicitly labelled Observed, never operation elapsed |
| Compaction | Admitted PreCompact / PostCompact for the followed chat | Separate lifecycle; no progress percent or timeout success |
| Link and activity age | Authenticated link presence versus genuine activity update age | Independent; a heartbeat does not refresh model/context provenance |

Official App Server exposes `thread/tokenUsage/updated`, but the installed desktop
App Server uses its own stdio channel. The plugin MCP transport does not forward
that channel. Starting another server, reading transcript text or guessing capacity
from the model name would not establish this chat's telemetry and is not used.
The missing supported observation channel is an explicit integration boundary.
Unsupported optional fields remain unavailable internally and are hidden in the
passive UI, rather than filling the page with unavailable labels.

Compaction remains visible on Home and all panels. Ordinary tool phases cannot
cancel it. A genuine completion may show Context refreshed while context counters
remain unavailable or awaiting a coherent new measurement. Loss of updates,
interruption, an orphan completion and disconnected state remain distinct;
elapsed time alone never proves compaction completed.

Current technical values also require the exact ACTIVITY revision, not merely
the same chat. ACTIVITY and TECH may arrive separately: during that gap, model,
context and turn duration are UNAVAILABLE. A previously known same-chat Active
compaction remains explicitly STALE until coherent metadata arrives. A different
chat cannot borrow its lifecycle. An orphan boundary says `Compaction not fully
observed`, covering both a missing start and a missing completion. A genuine
native permission request retains `Waiting for you` / `Approve on your PC`,
its notification and the frozen project row while the amber compaction indicator
remains visible. Native approvals still happen only on the PC.

The source lease is 120 seconds since the last genuine lifecycle hook. The
current hook source has start/end boundaries, without an ongoing confirmation
signal; repeated broker replies do not renew this lease. A compaction lasting
longer than that remains STALE, never completed by timeout. A future supported
ongoing observer must provide a distinct confirmation age before a long-running
operation can remain current beyond that lease. The explicit Studio synthetic
long-compaction scenario exercises presentation, not actual hook reachability.

### Desk and Away

The existing Make it yours screen includes a `Usage` row, toggling `Desk` and
`Away` through the settings owner. Its five 34-pixel rows begin at y=190 with a
38-pixel step; the last ends at y=376, before the compact footer at y=390. The
40-pixel title begins at y=146 and ends before the controls. These rectangles
remain inside the central square; the separate lateral Back target is intact.

`cody_usage` is a strict persisted uint32 boundary value (Desk=0, Away=1).
Only an absent key receives a logged Desk default followed by exact readback.
Storage/type/value/readback failures publish independent mode validity and the
original error; the row shows N/A rather than a guessed mode. Explicit mode
changes serialize with other preferences and preserve sound, wake, rotation,
clock, preferred name and their independent provenance. Consumers use only the
canonical bounded options projection, with no NVS or sensor read in rendering.

The shared `CODY_companion_mode.hpp` policy calms artistic breathing and outline
motion, and suppresses optional idle accessory scenes on awake passive Home and
panel routes. Setup, semantic question controls and manually selected Debug
expressions retain their existing behavior. Activity headlines, important sounds,
the compaction indicator, tap/shake, source freshness and service decisions stay
with their existing owners. Studio provides a clearly SIMULATED preview toggle
using that same native helper and resets it before live or Debug/event views.

Away makes no radio, CPU frequency, voice, standby or new transport change.
The regular gesture service cadence remains intact; lowering it without
separating canonical motion sampling could miss deliberate shake peaks. Reduced
accessory motion is implemented; CPU time, energy use and walking rejection are
not measured by source review. Reachability continues to follow the actual link.

The options projection is guarded at 256 bytes in the render path. Mode fields
add one enum, one validity flag and one esp_err_t to the canonical preferences;
padding and compiled structure size still require the next authorized build.
One additional preference control belongs to the existing persistent content
owner. Host Away state is one boolean; no task stack was increased. Previously
recorded boot stack headroom and compiled frames predate these changes and do not
validate the new nested render/projection path.

`CODY_scene` resolves immutable definitions by `SceneId`, prepares cloud/title and
an optional bounded content slot, advances fixed-capacity state and plans old/new
damage. Authored background, title visibility, entrance placement/timing and text
cadence are explicit; unsupported schema/IDs fail rather than select a fallback.
Entrance uses smoothstep before snapshot-driven response is composed.

Entrance and 64-bit `SceneFrame::text_elapsed_us` are separate active clocks;
pause/standby advances neither. Retitling or replacing progress restarts text
without replaying the cloud entrance. Title/body/footer and look captions use
captured typed prefixes, normally 45 ms/character after their preceding text.
Long-lived Saving/error/hint text anchors a new local sequence to the current
clock. Look scroll/save does not restart the preview entrance or row clocks.

Content callbacks borrow prepared immutable state and only the slot/tile
intersection, including original stride/capacity/byte order. Bounds stay below the
title and inside the circle, including QR quiet zones. No per-frame allocation,
hardware read, display transaction or retained tile is permitted. `revision`
changes for visual mutations; removing content dirties old bounds. Captured
`visible_characters` counts only displayed/clipped prefixes. `text_generation`
changes on entry/replacement/scroll, not ordinary prefix progress, so newly exposed
old labels prime a silent audio baseline. Commit state only after full retirement.

`set_scene_title()` restarts only text; `scene_content_characters()` prepares bounded
prefixes. Look 11 calls `update_look_text(scene, control_revision, changed)` once
after scene advancement, then captures content before drawing; it preserves an
already-true `changed` flag and does not attach mutable state on every tile.

## Optional audio

Boot cues start with original procedural `audio-ready` (0.18 s), followed by the
explicit `audio.eyes_open_cue: boot-rise` in `assets/cody/runtime.yaml`. BootRise
is a 1.8-second rising chime: 79,380 mono PCM16 bytes at 22,050 Hz. On 2026-10-10
this replaced the earlier chime-plus-murmur greeting; historical vocal auditions
remain separate and are not embedded. Source/rendered hash mismatch or stale
authoring fails preparation rather than selecting another sound.
See [boot and Codex action authoring](../../assets/cody/audio/codex-actions/README.md)
for transformations and owner-confirmed commercial-use licence provenance.
Preparation validates the
flash PCM; completed boot requires a verified canonical clock before applying
the quiet-hours policy. An unavailable/stale clock admits one pending boot request,
not a playback completion. The existing companion worker checks only that pending
request's canonical clock at most once per second, including static/paused routes.
A temporary 4 KiB PSRAM audio worker rechecks clock policy and active power state
before the real PCM operations, then releases itself after completion, failure or
a definitive quiet-hours skip. The request may remain unfulfilled through standby;
completed/skipped/failed requests never become ordinary software-wake sounds. Clock
waiting has no timeout that fabricates completion, and startup keeps an optional
audio admission/playback failure visible without aborting visual startup.
The base assets use 22,050 Hz and authored volume 100; the
optional voice-AFE feature has its separate matched-rate configuration. Shared
transport configuration is authoritative: a conflicting generic playback rate
returns the real error rather than reconfiguring an open microphone unnoticed.

`CODY_codex_audio.hpp` selects typed Codex action cues from immutable mono
16 kHz PCM. The current cue set, durations and byte totals are owned by the
[authoring manifest](../../assets/cody/audio/codex-actions/README.md), within
the existing audio-owner limit. The same optional personality-audio worker and one-slot mailbox own
playback; there is no new worker, I2S owner or event replay queue. Independent
channel generations cancel hidden Codex presentation and hidden personality
symbols without cancelling each other; standby invalidates both. Codex starts
suspended and its presenter enables only visible companion transitions.
Working/Reading/Editing/Running requests have a 1.5-second activity rate limit.
Other notices skip that limit but never preempt a cue already in flight.
Busy, expired, disabled and quiet-hours dispositions remain explicit, while
actual failures return their original error and publish through the canonical
snapshot. `Dispatched` does not mean playback completed. A response-ready cue
means the assistant finished its response, without asserting task success.
The reserved Reading asset requires an actual observed event before use.

The optional keyboard microclick is an original rounded noise impulse: 320 mono
PCM16 samples at 16 kHz / 20 ms, 640 flash bytes. Completed-frame character deltas
send bounded notifications only; rendering performs no codec I/O or audio wait.
The audio owner coalesces at most 20 taps/second, drops busy or expired
notifications (150 ms), and never replays a backlog. Entry, replacement, scroll
and wake reset the silent baseline; unchanged frames cannot create taps.

It preserves actual codec volume and the already shared transport clock, converts
only this cue to the active rate, and does not reconfigure the microphone. Zero
volume, quiet hours, hidden/parked scenes and standby suppress output. Unavailable
trusted time suppresses keyboard microclicks only when both routines and quiet-hours policy are
enabled; disabling either allows cues without that clock. Unavailable policy
settings still suppress with their real error. Audio/speaker-excluded profiles
skip preparation while preserving visual startup. Runtime playback failure is
logged/published without stopping this optional presentation effect.

The authored fixed audio RAM budget is approximately 6.5 KiB: 4,096-byte task
stack + 2,400-byte transport scratch + small fixed state, not measured linked-image
RAM. The stack and PCM scratch now use external BSS in PSRAM, as does the
separate 4-KiB personality-audio stack; task controls/mailboxes and codec DMA
remain internal. Speaker loudness/gentleness remains a listening check. The separate ten
[archive cue candidates](../../assets/cody/audio/gentle-sfx/README.md) exist only
as private `artifacts/audio/cody-gentle-sfx/` previews/audition/manifest. They are
not firmware or automatic cues. Perpetual commercial-use licences were confirmed
by the owner on 2026-10-04; perceptual review is pending.

## Windows preview

The [simulator guide](../../simulator/windows/README.md#host-audio-and-inspection-ownership)
owns the host audio DLL, PC playback, inspection typography and browser links.
These are synthetic host capabilities with separate execution evidence.

## Device diagnostics

Debug FPS uses completed frames over real elapsed time. CPU1/core0 and CPU2/core1
come from canonical idle/IPC measurements with their own timestamps; invalid or
stale data shows unavailable, never an estimated/clamped value. Measurement
windows begin after startup at 04 or in the ordinary main companion view when
retained pairing skips that first-run greeting. The automatic Idle clock greeting
and a manually selected sleeping face in the main view remain eligible. The
five compact rows below the routine title show FPS/CPU, IPv4, free internal
RAM/PSRAM, SD presence/access and free space. The upper overlay uses a quieter
date, larger `HH:MM` without seconds and consistent battery, Wi-Fi and Bluetooth
icons. Because this board has no card-detect pin, the SD row distinguishes a
verified card response from `SD UNCONFIRMED`. Personality icons keep their
independent anchor above the cloud. The CPU values
represent total busy time per core, including tasks other than Cody. No extra
sensor polling is introduced. The HUD preference is session-scoped under the
display lock and gated by scene `readout_visible`. Authored scenes admit the HUD
unless a visible bottom action occupies its area; Release excludes it.

Power is dedicated to standby/wake and deliberate shutdown in Debug and Release.
A short click toggles software standby; a valid three-second continuous hold
presents `Good night` with `Release Power` directly below it, retires the complete
Sleep presentation, and requests the
power owner's gradual fade and coordinated PMU shutdown. Screenshot and restart
no longer consume PWR; BOOT diagnostics remain unchanged. The old Power-screenshot
setter returns `ESP_ERR_NOT_SUPPORTED`, and its getter reports disabled.
Other screenshot diagnostics retain the existing SD owner and real failures.

The shutdown presentation blocks competing touch/scene/voice overlays, suspends
character/typewriter audio, and waits for any existing screenshot operation with
a bounded deadline. It acknowledges only complete retired frames, including a
400-ms greeting dwell after retirement. The passive `Release Power` instruction
shares that retired frame and asks the user to release the admitted hold before
the configured 10-second emergency hardware cutoff can interrupt owner drains. A failed shutdown restores a visible
`Shutdown failed` caption with the original hexadecimal error when display
recovery succeeds; a subsequent touch dismisses it. This is source implementation,
not evidence that power physically disappeared. See the
[power transaction and hardware limitations](../bsp/services/power-management.md#power-key-farewell-and-pmu-shutdown).

The CST9217 path retains 8-bit panel commands, 2-ms address-write settling/configuration
before IRQ registration and real zero-contact release reports. ISR work is only
timestamp/readiness signalling. Bounded acquired reports are dispatched in order
after complete frame retirement, preserving acquisition timestamps; invalid
marker/count/transport failure stops the mandatory consumer. When no report is
pending, the result is `ESP_ERR_NOT_FOUND`, invalid with timestamp zero, not a
fabricated release.
Rotation waits for outstanding frame/contact ownership; drawing and touch share
one cardinal basis without mirroring or a second transform.

`CONFIG_LUNASHELL_CODY_START_AUTO_ROTATION=y` explicitly enables the shared policy
after strict saved-setting restoration and persists `auto_rotate` only if off.
This is a requested startup policy, not an absent-key default or corruption repair;
disable it to honor a saved off preference. Cody adds no second IMU acquisition loop.

Debug disables automatic light sleep to preserve native USB Serial/JTAG diagnosis;
Release/wake-on-word follow canonical platform policy. SD diagnostics observe the
storage owner and never alter saved logging preferences. For log capture, rotation
and readback use [offline USB/SD logging](../bsp/services/storage-and-logging.md); those device
operations still require approval. The intermittent mandatory display/touch stop
has not been resolved by short successful captures.
## One-way voice input source increment

The 2026-10-11 firmware source adds an explicit voice-input event for **Sophia**
and an acquired double tap on the visible character. A single character tap waits
420 ms before opening Activity; technical-panel taps still advance immediately.
Movement, stale/broken touch, rotation and explicit controls disqualify a double
tap. The existing power owner handles standby wake; speech inference and enhanced
mono PCM remain owned by the sole existing ESP-SR AFE.
Wake-word admission skips its detection block and begins with the next real AFE
block. No wake-word cache is prepended; wake-word tail exclusion and preservation
of an immediately following command still need physical observation.

Voice input has presentation priority while retaining the interrupted route and
its real question deadlines. The character, geometry evaluation and normal scene
animation are suspended behind a procedural ribbon with a 30-FPS scheduling
target in a 384 x 48 logical-pixel band. Four filled colored layers use fixed-point
interpolation and native RGB565 alpha composition. A display-owned 784-byte knot
buffer avoids per-frame allocation, video assets, SD access and runtime trigonometry.
The visual gain follows valid, fresh microphone levels already published by the
AFE owner; unavailable levels decay the artwork without inventing measurements.
After the first full retirement, only the ribbon transfers at animation cadence;
the identity and timer bands refresh at 5 Hz, and instructions remain static.
Actual frame rate and energy cost require target measurements. One measured,
fading/scrolling row cycles model, catalog cost band, thinking effort, branch,
then project; unavailable fields disappear. No numeric price or bill is invented.
The bottom timer shows elapsed and remaining seconds within the sixty-second
budget, starting at `00s elapsed / 60s left`. Recording keeps all instructions
visible: `Tap once to send`, `Pause over 2s to send`, and
`Shake or double tap` / `to discard`. A single completed tap waits 350 ms for
double-tap arbitration; a second held contact postpones that decision. Double tap
or deliberate shake requests discard. Session and orientation guards reject late
gestures, and only a fully retired view admits input. Cancellation remains possible
while sending before END, but cannot revoke END already transmitted or a turn
accepted by Codex; no late gesture claims that remote audio was discarded.
End of speech requires more than two seconds of successfully acquired 16-kHz
VAD nonspeech PCM after speech; scheduling gaps do not count as silence. There is an eight-second
no-speech deadline and a 60-second maximum utterance. The overall capture/delivery
budget is 90 seconds. New triggers never start a second simultaneous capture.
Explicit sleep and microphone pauses stop this input even when standby retains
the recognition microphone. If END may already have left the device, the result
is `Unconfirmed`, requiring review on the PC rather than claiming cancellation.

The component-owned PSRAM ring is 160,000 PCM16 samples: 320,000 bytes, or ten
seconds of **unacknowledged** 16 kHz mono audio. It is not an utterance-duration
limit. Full buffers fail explicitly without overwriting unheard words. Neither SD
nor internal flash stores audio on the device. Canonical `voice.input` contains
typed phase/trigger, actual counters, original event times and errors; no consumer
reads the microphone. The CCP worker borrows the transport with a 5-ms cadence
only while connected input needs service (at least one RTOS tick). Idle and
disconnected input use the normal 100-ms cadence; the last 100 ms before an ACK
or session deadline return to the fast cadence to preserve timeout granularity.

The dedicated Wi-Fi channel is UDP 42119, separate from CCP metadata. It uses
AES-256-GCM, fresh per-utterance keys derived from the paired link and current
authenticated discovery nonce, sequence numbers and independent data/ACK nonce
domains. A block stays in the ring until its authenticated acknowledgement.
Four DATA packets may be in flight with cumulative acknowledgement of the
contiguous prefix. Retransmission repeats identical ciphertext; START/END and
delivery are distinct. Temporary CCP loss consumes the ten-second ACK budget,
preserving buffered PCM and resuming only with the same authenticated key and IP.
Transport failure after END transmission remains Unconfirmed.
The PC binds START to the exact genuinely followed chat, rejects a changed chat
before submission and never selects a different destination on retry.

Plugin 0.6.5 receives bounded PCM, writes a private WAV under its runtime
`artifacts/voice` and passes `localAudio` to the exact native Codex chat. The
installed CLI's generated schema confirms `localAudio`; account/model support,
desktop endpoint availability, native approval routing and actual delivery remain
unverified. The explicit loopback `CODY_CODEX_AUDIO_ENDPOINT` must identify the
intended desktop/app-server host; the implementation does not launch a second
host or guess a chat. A periodic read-only native probe rejects START early if
the endpoint or followed chat is unavailable; readiness expires after ten seconds.
This does not certify audio/model capability. Steering uses the current turn in
the frozen chat at submission; native conflicts preserve the WAV for inspection.
Recording IDs, returned native turn IDs, artifact paths and uncertainty are
available through audio status; ambiguous input is never automatically retried. Native approvals
and questions are never answered by the device or plugin.

This does not yet establish working device dictation. The 2026-10-12 recording
increment passed the selected Debug wake-word/activity build/size, real owner and
receiver host tests, native raster/gesture checks and individual compiled stack
frames. It was not flashed over the ongoing battery experiment. Native desktop
integration and measured stack/CPU/PSRAM headroom remain in `VOICE-01` in the root TODO. The atlases retain
their planned audio flow until end-to-end verification establishes its behavior.
