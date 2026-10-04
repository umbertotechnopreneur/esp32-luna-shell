# Cody runtime and lifecycle

The display worker owns scene/rig state under the shared display lock. Hardware,
settings, radios, power and audio retain their platform owners. A frame borrows
immutable state until every display transaction retires; partial writes never
become the next displayed-state reference. See [rendering](rendering.md) for the
damage/DMA contract and [onboarding](onboarding.md) for route gates.

## Startup and expression sequencing

Cold boot or software reset follows Sleeping 01 → Waking 02 → Ready 03. The animated
character fades in over 2 seconds, then Waking and Ready receive 4 seconds each.
Real startup checkpoints gate advancement: a service wait does not consume the
next phase's active duration. `umbertogiacobbi.biz` appears at y=348 and fades out
during Ready's last 300 ms; the startup phases do not show the Debug HUD.

Effective first setup enters Hello 04 with Happy and `Hi, I'm Cody!` at
`(233,225,130)`. Its 13 characters use 70 ms each; after the complete text has
actually retired to the panel it remains readable for one active second before
05. There is no second bitmap intro. Otherwise the runtime proceeds to idle and
the applicable [clock routine](clock.md), not the network introduction.

`cody_firstboot` is a validated persisted boolean. Only a genuinely absent key
logs and persists the default `true`; invalid type/value or storage failure stops
that operation. `CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT=y` currently makes the
effective flag true without rewriting a valid saved false. Completing the text
does not clear the key; `persist_first_boot()` is an explicit preference write.

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

Software wake first renders Sleeping behind the dark panel, exposes it through
the shared 800-ms panel fade, then gives Waking and Ready 4 seconds each. It does
not multiply a second 2-second character fade over the panel fade. Route, pending
operation, debug/pause state and scene entrance/text clocks stay in RAM; hidden
routes do not update during the lifecycle wake sequence. Before showing the
retained route, reconcile the canonical pairing/Wi-Fi state. A BLE session closed
while asleep must not resurrect its old QR. Rebase the IMU response baseline.

Cold boot/reset still uses normal startup and first-boot policy. Actual deep sleep
resets RAM and starts Wake 35B → Idle 16 rather than cold-boot branding/introduction;
there is no persisted route-resume contract. The power implementation and
profile-specific silicon-sleep policy remain in
[low-power management](../low-power-management.md) and
[release deep sleep](../release-deep-sleep.md).

Every authored scene supplies `standby_override_seconds`: zero inherits the saved
user timeout, a positive value replaces it only for the active scene. Invitation
05, installation 06 and pairing 07 use 60 seconds; the unused scan definition does
too. Hello 04, error 08, online 09 and look 11 inherit. Scene entry supplies a fresh
inactivity baseline and touch refreshes it. Departure/parking clears the override.
This never writes NVS or extends the backend's five-minute pairing deadline.

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

Boot cues are original procedural `audio-ready` (0.18 s) and `eyes-open` (1.3 s),
authored in `assets/cody/poc.yaml` and stored in flash. Preparation validates the
flash PCM; both playback operations are deferred until completed boot reads the
trusted-clock policy. The base assets use 22,050 Hz and authored volume 100; the
optional voice-AFE feature has its separate matched-rate configuration. Shared
transport configuration is authoritative: a conflicting generic playback rate
returns the real error rather than reconfiguring an open microphone unnoticed.

The optional keyboard microclick is an original rounded noise impulse: 320 mono
PCM16 samples at 16 kHz / 20 ms, 640 flash bytes. Completed-frame character deltas
send bounded notifications only; rendering performs no codec I/O or audio wait.
The audio owner coalesces at at most 20 taps/second, drops busy or expired
notifications (150 ms), and never replays a backlog. Entry, replacement, scroll
and wake reset the silent baseline; unchanged frames cannot create taps.

It preserves actual codec volume and the already shared transport clock, converts
only this cue to the active rate, and does not reconfigure the microphone. Zero
volume, quiet hours, hidden/parked scenes and standby suppress output. Unavailable
trusted time suppresses only when both routines and quiet-hours policy are
enabled; disabling either allows cues without that clock. Unavailable policy
settings still suppress with their real error. Audio/speaker-excluded profiles
skip preparation while preserving visual startup. Runtime playback failure is
logged/published without stopping this optional presentation effect.

The authored fixed audio RAM budget is approximately 6.5 KiB: 4,096-byte task
stack + 2,400-byte transport scratch + small fixed state, not measured linked-image
RAM. Speaker loudness/gentleness remains a listening check. The separate ten
[archive cue candidates](../../../assets/cody/audio/gentle-sfx/README.md) exist only
as private `artifacts/audio/cody-gentle-sfx/` previews/audition/manifest. They are
not firmware or automatic cues; redistribution rights and perceptual review are
unconfirmed.

## Device diagnostics

Debug FPS uses completed frames over real elapsed time. CPU1/core0 and CPU2/core1
come from canonical idle/IPC measurements with their own timestamps; invalid or
stale data shows unavailable, never an estimated/clamped value. Measurement
windows begin after startup at 04. The HUD preference is session-scoped under the
display lock, also gated by scene `readout_visible`; Release excludes it.

Only the Debug Screenshot button is currently exposed: upper-center `(233,47)`,
44-pixel circular target, revealed by touch, 5-second inactivity, 330-ms fades.
A reveal-only touch cannot trigger it. Pause/Skeleton are hidden/non-interactive;
route navigation and battery/date/radio chrome have separate ownership. A
screenshot allocates an on-demand 466×466 RGB565 PSRAM buffer (434,312 bytes),
then the SD owner writes an atomic top-down 24-bit BMP under
`LUNASHELL/screenshots/`. This is not a persistent renderer framebuffer; absent
or unhealthy storage returns its real error and never causes formatting.

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
