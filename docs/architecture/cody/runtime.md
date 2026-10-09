# Cody runtime and lifecycle

The display worker owns scene/rig state under the shared display lock. Hardware,
settings, radios, power and audio retain their platform owners. A frame borrows
immutable state until every display transaction retires; partial writes never
become the next displayed-state reference. See [rendering](rendering.md) for the
damage/DMA contract and [onboarding](onboarding.md) for route gates.

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
from the canonical saved-profile/link state; see [onboarding](onboarding.md).
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
[low-power management](../low-power-management.md) and
[release deep sleep](../release-deep-sleep.md).

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
See [boot and Codex action authoring](../../../assets/cody/audio/codex-actions/README.md)
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

`CODY_codex_audio.hpp` selects ten typed Codex action cues from 90,240 bytes of
immutable mono 16 kHz PCM. Each lasts 210--350 ms, within the existing owner
limit. The same optional personality-audio worker and one-slot mailbox own
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
The audio owner coalesces at at most 20 taps/second, drops busy or expired
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
[archive cue candidates](../../../assets/cody/audio/gentle-sfx/README.md) exist only
as private `artifacts/audio/cody-gentle-sfx/` previews/audition/manifest. They are
not firmware or automatic cues. Perpetual commercial-use licences were confirmed
by the owner on 2026-10-04; perceptual review is pending.

## Windows fake audio output

The Windows interactive preview has a host-only fake output owner. The public
`device.ps1 simulator` route first compiles `cody_fake_audio.dll` below
`artifacts/build-cody-simulator-audio/`; the explicit generator is
`device.ps1 generate -Generator cody-simulator-audio`. An installed Windows C++
toolchain is required. No toolchain is installed automatically.

The DLL links the unchanged Cody personality selector, whistle and typewriter
modules to fake `LUNASHELL_audio_*` output functions. Python copies their actual
PCM into WAV memory and uses the existing Windows `winsound` playback backend
on one bounded worker. Presented symbols, the visible whistle generation,
new greeting glyphs and pointer presses request sounds automatically. Boot
artwork routes and manual previews use the existing generated boot WAVs.
The PC sound toggle cancels pending clips; optional admission/playback failures
remain visible in the simulator status and host log without stopping the UI.
This is one output voice, with no mixer. PC playback is labelled synthetic and
never published into firmware snapshots. Capture-only routes do not initialize
the audio owner or play sound. DLL compilation does not establish audible output;
interactive playback remains a separate authorized observation.

Cody Studio uses Consolas for host controls and inspection labels. The four
inspector tabs share a seven-character label width and identical padding in
selected/unselected states; the Clam selected-tab padding/expansion defaults
are explicitly overridden. Character scene typography retains its authored
device assets. Palette text provenance is normalized both by the generator
and the Windows painter; indexed coverage and PCM binary hashes remain exact.

The studio header bundles the approved VibeWare floppy PNG unchanged from
`Obsidian/20_Execution/Projects/VibeWare/Branding/vibeware-logo.png`; the host
copy, original MIT notice and provenance are under
`simulator/windows/assets/branding/`. The buttons open the Cody GitHub project,
`https://umbertogiacobbi.biz` and the published VibeWare `MANIFESTO.md` on GitHub
only on a user click. The planned website manifesto is not used as a published
destination. Browser launch failures remain in the simulator status/console.

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

The Debug Screenshot touch control is hidden and non-interactive. One short PWR
press while Cody is active waits 500 ms for a second click, then starts a
session-enabled 3, 2, 1 countdown. Two separately acquired short clicks within
that window request a software restart instead, preserving settings and using
the existing bounded log drain. Failed acquisitions cancel the pending gesture;
batched clicks without individual timing are rejected. Screenshot admission,
shutter worker and button-triggered software restarts are Debug only. The
session flag defaults on and can be changed through the Cody runtime API. Each
number lasts one second after its frame retires. Cody clears the overlay in a
completed frame before requesting the capture, so the numeral is absent from
the saved image. The existing on-demand 466×466 RGB565 PSRAM buffer (434,312
bytes) transfers to the SD owner, which writes an atomic top-down 24-bit BMP
under `LUNASHELL/screenshots/`. The overlay reports SAVING, SAVED, or FAILED
from the actual asynchronous result. An original 200-ms shutter cue is queued
to a separate audio worker after the capture frame; quiet hours suppress it.
Absent or unhealthy SD storage returns its real error and never causes formatting.

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
and readback use [offline USB/SD logging](../offline-logging-usb.md); those device
operations still require approval. The intermittent mandatory display/touch stop
has not been resolved by short successful captures.
