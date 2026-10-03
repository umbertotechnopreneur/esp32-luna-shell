# Reusable Cody scenes

The standalone Cody renderer now supports an immutable scene definition with
the existing animated cloud above one large monospace title and optional
caller-owned content below it. This is not an LVGL screen, a new sensor owner,
or a second animation/render task. English is the only supported product language.

## First-boot greeting

The first scene follows page 05, screen 04 of `cody-planning-atlas-v1.pdf`:

- Enter `SceneId::Greeting` 2,000 ms after the completed **Ready** boot frame,
  not two seconds after power-on. The existing worker handles the deadline.
- Place Cody at `(233, 225)` with scale `130`, versus the demo's `(233, 235)`
  and scale `145`: ten pixels higher and about ten percent smaller.
- Select a happy squint, raised cheeks and an open smile. The same response
  composer still supplies blinking, breathing, motion and valid snapshot input.
- Reveal `Hi, I'm Cody!` in white, one character every 70 ms. The full line
  appears after 910 ms of active scene time; pause/standby does not advance it.
- Hide demo navigation and the FPS/CPU readout. Hidden controls have no touch
  target; the greeting is a root scene with no previous route. Eye interaction
  still uses the evaluated, optionally deformed face geometry.

The title's prepared bounds are `[115, 350) x [348, 388)`, inside the central
safe square. All rendering and input retain the existing logical canvas and
cardinal rotation owner. Pairing, QR routes and onboarding progression are
not implemented by this change.

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
