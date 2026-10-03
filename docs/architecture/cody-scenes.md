# Reusable Cody scenes

The standalone Cody renderer now supports an immutable scene definition with
the existing animated cloud above one large monospace title and optional
caller-owned content below it. This is not an LVGL screen, a new sensor owner,
or a second animation/render task. English is the only supported product language.

## First-boot greeting

The first scene follows page 05, screen 04 of `cody-planning-atlas-v1.pdf`:

- Enter `SceneId::Greeting` 2,000 ms after the completed **Ready** boot frame,
  not two seconds after power-on. The existing worker handles the deadline.
- Fade the Ready bitmap to black over 400 ms, then fade in animated Cody over
  2,000 ms. Intro durations are explicit scene fields; zero skips that phase.
  Motion, face and contour keep their shared active clock during the cloud fade.
- Place Cody at `(233, 225)` with scale `130`, versus the demo's `(233, 235)`
  and scale `145`: ten pixels higher and about ten percent smaller.
- Select a happy squint, raised cheeks and an open smile. The same response
  composer still supplies blinking, breathing, motion and valid snapshot input.
- Reveal `Hi, I'm Cody!` in white, one character every 70 ms. The full line
  appears after 910 ms following the completed intro; pause/standby advances
  neither intro nor typewriter. Text, content, HUD and touch targets remain
  hidden during the intro.
- Hide demo navigation. Hidden controls have no touch target; the greeting is
  a root scene with no previous route. Eye interaction still uses the evaluated,
  optionally deformed face geometry. Admit the existing measured FPS/CPU readout
  below the title; its visibility remains a session preference.

The title's prepared bounds are `[115, 350) x [348, 388)`, inside the central
safe square. All rendering and input retain the existing logical canvas and
cardinal rotation owner. Pairing, QR routes and onboarding progression are
not implemented by this change.

The generic scene frame owns a typed intro phase and a bounded phase clock.
The boot fade adjusts at most 64 palette entries on the worker stack; the cloud
fade multiplies RGB565 in the existing owned raster tile, respecting its byte
order. Neither changes panel brightness or allocates a framebuffer. Opacity
changes invalidate both old and new artwork bounds even if geometry is static.
The Windows mirror follows the same phases; it remains explicitly synthetic.

## Settings and failure behavior

`CODY_settings.cpp` uses the existing `UserSettingsService` and its internal-
stack NVS executor. It does not write flash from the renderer's PSRAM worker.
The boolean key is `cody_firstboot` in the existing user-settings namespace.

Only `ESP_ERR_NVS_NOT_FOUND` selects and persists the logged default `true`.
A read/type/storage error or a persisted boolean other than zero/one remains
an error; there is no repair, migration, erase or substituted success.

`CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT=y` is enabled in the Cody profile for
the current iteration. It makes the **effective** flag true without rewriting
a valid persisted false. The real key is still read and validated first.
Disable this build option later to honor the stored preference alone.
`persist_first_boot()` is an explicit preference write for future startup;
finishing the typewriter never clears the key automatically.

### Measured readout and shared auto-rotation

`CONFIG_LUNASHELL_CODY_DIAGNOSTICS_READOUT=y` selects the initial FPS/CPU HUD
preference. `CODY_companion_set_debug_readout_enabled(bool)` and
`CODY_companion_get_debug_readout_enabled(bool *)` change/read it at runtime
under the existing display lock. The flag is session-scoped, not an NVS write;
scene `readout_visible` must also admit the layer. Hiding it invalidates its old
bounds, so old digits are erased. Its real snapshot validity/unavailable states
and completed-frame timing remain unchanged. This adds no framebuffer or task.

`CONFIG_LUNASHELL_CODY_START_AUTO_ROTATION=y` explicitly enables Luna's existing
cardinal orientation policy after saved display settings have passed strict
restoration. It uses `LUNASHELL_display_set_auto_rotation_enabled(true)`,
persisting the existing `auto_rotate` preference only if it is currently off.
This is a requested startup policy, not a missing-key default or repair of
invalid settings. Disable the policy to honor a saved off preference across
boots. Runtime changes continue through the same Luna setter/getter.

The existing acquisition owner, orientation filtering/hysteresis, panel/touch
rotation transaction, content revision and Cody redraw path are reused for
0/90/180/270 degrees. Cody adds no IMU reads, sensor loop, frame cache or LVGL
dependency. Physical rotation, touch alignment and standby still need device
observation; enabling the policy alone does not prove those behaviors.

### Boot cue clock and loudness

The two original synthesized Cody cues now use mono PCM16 at **22,050 Hz**,
matching the already-open ES8311/ES7210 shared I2S clock, at codec volume **100**.
`assets/cody/poc.yaml` remains the source; regeneration updates both embedded
PCM and host WAV files. The audio owner rejects a stream rate that conflicts
with an open peer before changing transport state, retaining the real error.
It does not resample, silently substitute a rate, or suppress codec failures.
The separately proposed archive SFX are not integrated into this release.

## Ownership and reuse

Authoring lives in `assets/cody/scenes/scenes.json`; generated definitions are
immutable and addressed by `SceneId`, not their list position. Names and IDs
must be unique. Current schema/IDs are accepted explicitly; unknown fields,
characters and old schemas fail rather than select a compatibility path.

`CODY_scene.hpp` provides these small, separate responsibilities:

- `find_scene()` resolves a stable identity.
- `begin_scene()` checks the shared rig pose, font and circle-aware layout,
  then prepares a text line. It publishes the frame only on success.
- `advance_scene()` advances the fixed-size typewriter state with active time.
- `set_scene_content()` accepts an optional `SceneElementId::Content` slot.
- `plan_scene_damage()` adds only changed glyph spans and changed content to
  the existing old/new cloud damage plan.
- `draw_scene_rows()` composites into the existing borrowed RGB565 tile.

The optional content callback borrows immutable state until the entire frame
retires. Its nonempty bounds must lie below the title and wholly inside the
circle, including any QR quiet zone. The compositor lends only the intersection
of its slot and the current tile, preserving stride, capacity and byte order.
It must not allocate per frame, own a display transaction, sample hardware,
or retain the borrowed tile. Increment `revision` when its visual state changes;
old and new bounds are both dirtied when content changes or is removed.
Clear the slot by passing a default `SceneContent{}`.

The cloud, title and content are redrawn in that order within each dirty tile.
Reference geometry and scene state become the next displayed-frame reference
only after complete successful display retirement, never after a partial write.

## Memory and frame cost

The supported board profile configures 16 MB of flash. That is not the internal
RAM/DMA budget, and an application partition is not the entire flash capacity.
The new title font stores only 95 printable ASCII characters (32 through 126):
19 x 40 coverage pixels, 18-pixel monospace advance, four coverage levels packed
into two bits. Its coverage is exactly **19,000 flash bytes**, plus small constant
metadata. No Unicode ranges, translations, TTF file or runtime font engine are
linked into firmware. Glyph overhangs are preserved rather than cropped.

Scene state is fixed-size. There is no new framebuffer, tile, PSRAM allocation,
heap-owning text object, task or periodic sensor read. Text is counted and
prepared once; rasterization visits only glyphs intersecting a dirty tile.
After the typewriter finishes, title-only invalidations stop. Cloud damage can
still cause an intersecting text/content region to be recomposited correctly.

At identical deformation, scaling 145 to 130 reduces cloud pixel area to
`(130 / 145)^2`, about 80.4% of the original. This reduces pixel sampling and
potential transfer area, not every fixed geometry/snapshot/display cost.
It is a geometric estimate, **not** a measured 19.6% CPU improvement or proof
of a sustained frame rate. A smaller animated cloud leaves more rendering
budget for mostly static content; timing remains to be measured on hardware.

## Shared assets and Windows preview

The host preview uses the same authored scene and packed glyph coverage.
Its acceleration/gyro and other inputs remain visibly synthetic and never
enter firmware snapshots. Select `greeting` in the existing screen selector.
The pearl preview remains an explicit renderer selection:

```powershell
.\scripts\device.ps1 simulator -Screen greeting -CloudJellyStudy
.\scripts\device.ps1 simulator -Screen greeting -CloudJellyStudy -AtMs 1000 -OutputPath artifacts/simulator/cody-greeting.png
```

These are future preview commands, **not commands run for this implementation**.
The fixed GIF motion experiment remains a `rig`-only timeline; it cannot silently
capture the wrong scene when `greeting` is selected.

Regenerate source assets through the sole repository entrypoint and an explicit
local JetBrains Mono source font (Pillow and PyYAML are required):

```powershell
.\scripts\device.ps1 generate -Generator cody-scenes -ToolArguments @('--font-path', 'C:\absolute\path\JetBrainsMono-Regular.ttf')
```

Generation writes versioned source assets, not build output. The source font
is not copied into firmware; its hash, glyph hash and scene-source hash are
recorded in the manifest. Scene-source hashing normalizes LF/CRLF so a Windows
Git checkout does not manufacture asset drift. Stale or corrupt preview assets
are rejected.
The derived coverage is named **Cody Title Mono** and distributed with the
[upstream SIL Open Font License](https://github.com/JetBrains/JetBrainsMono/blob/master/OFL.txt)
in `assets/cody/scenes/OFL.txt`.

## Authorized build and flash (2026-10-03)

After implementation checkpoint `1df7ee0a`, the user explicitly requested
device delivery. The Cody profile was reconfigured through `device.ps1
set-target`, then `device.ps1 flash -Profile cody-companion -BuildDir
artifacts/build-cody-companion -Port COM10 -Baud 460800 -Jobs 8` compiled
and flashed successfully with ESP-IDF 6.0.2. The application written at
`0x10000` was **2,055,376 bytes**. Esptool verified the written hash and
completed the hardware RTS reset. The generated configuration enables pearl
jelly, face-follow and the forced first-boot greeting on the 16 MB board.
The command evidence is in `artifacts/logs/flash.log` until transient cleanup.

No simulator execution, test suite, separate size report, serial runtime
capture or physical display/motion observation ran for this revision.
Successful flashing does not establish the greeting's appearance, measured
CPU/FPS, stability, rotation or standby behavior. Those checks still require
fresh authorization for the specific validation or physical operation.

## Authorized 45-second capture (2026-10-04)

After the user reported a static screen and inaudible cues, one explicitly
authorized COM10 capture with reset observed this preceding firmware for
45 seconds. The greeting entered at approximately 6.2 seconds uptime; rendering
continued through frame 749 at 44.9 seconds, typically 19-20 FPS with measured
core 0 usage around 81-83% and core 1 at 0%. No panic or renderer terminal
failure appeared in that window. The reported freeze was **not reproduced**,
not resolved. The HUD was hidden by the scene definition, despite live timing
reports, and the restored rotation preference was off.

The capture also records the real I2S error: playback requested 16,000 Hz while
the open microphone used 22,050 Hz. Playback nevertheless reported completion;
that did not establish audible output. The implementation above aligns cue
rates and rejects conflicting requests rather than trusting that partial
success. Loudness and physical sound remain unobserved after the change.

The capture is preserved under
`artifacts/build-release-evidence/cody-greeting-freeze.log`, outside transient
cleanup. No automated tests or simulator checks were run for this iteration.

## HUD/audio/rotation checkpoint build (2026-10-04)

Checkpoint `6f42f84d` compiled successfully through the authorized
`device.ps1 flash` workflow with ESP-IDF 6.0.2 and eight compiler jobs.
The application is **2,074,320 bytes**. The resulting configuration enables
the diagnostic readout, shared auto-rotation startup policy, pearl renderer,
face-follow and forced first boot; the Luna UI remains excluded.

The flash stage did **not** run: COM10 was no longer present. The dispatcher
port listing showed COM10/COM11 and the older USB ports as `Unknown` and not
openable, with no connected USB serial replacement. Reconnect the intended
device before retrying; do not select a Bluetooth or another product's port.
The preceding firmware therefore remains the last successfully flashed image.
Build/port evidence is preserved as `greeting-hud-rotation-build.log` and
`greeting-disconnected-ports.log` under `artifacts/build-release-evidence/`.
No new serial capture, tests, simulator checks or physical behavior observations
were performed after this build.

### Subsequent delivery and live capture

The later explicit retry successfully flashed checkpoint `b663f2ac` to COM10:
2,074,320 application bytes at `0x10000`, verified hash and hardware RTS reset.
The successful command log is preserved in
`artifacts/build-release-evidence/greeting-hud-rotation-flash-success.log`.
A separately authorized 45-second capture without reset then showed continued
rendering through frame 731 and successful 0/90/0-degree orientation changes.
No panic or repeated reset occurred in that window; the capture began during
startup and does not contain a preceding failure/reset reason. Evidence:
`artifacts/build-release-evidence/cody-reboot-loop-live.log`.
This does not establish long-run stability or fix the earlier intermittent stop.

## Intro/touch deployment (2026-10-04)

At the user's explicit request, checkpoint `d922d75a` compiled and flashed
successfully through `device.ps1 flash -Profile cody-companion -BuildDir
artifacts/build-cody-companion -Port COM10 -Baud 460800 -Jobs 8`.
The application is **2,081,600 bytes** at `0x10000`, on the 16 MB board;
esptool verified the written hash and completed the RTS hardware reset.
This includes the scene intro, authored X/Y swap and canonical elastic touch
response, plus preceding review-P2 fix `db2c7815`.
Evidence: `artifacts/build-release-evidence/cody-intro-touch-flash.log`.
No tests, simulator execution, separate size report or post-flash capture ran.
Appearance, motion directions, touch response and long-run behavior remain
physical observations, not conclusions from successful compilation/flashing.
