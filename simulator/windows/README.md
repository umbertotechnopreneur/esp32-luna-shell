# Windows simulators

Host previews of the 466 x 466 circular UI and authored behavior, not electrical
hardware or ESP-IDF driver emulation. All device-twin/stimulus values are visibly
`SYNTHETIC` and never enter firmware snapshots. Host timing and Pillow output do
not establish native pixels, FPS, CPU load or audible quality.

## Cody

The desktop inspector uses Tkinter/ttk with the local Cody Studio dark theme:
navy surfaces, teal accents, Segoe UI text, thin sliders and switch controls.
The fixed header keeps animation, rig inspection, scene and rotation accessible.
Face, Shape, Inputs and Inspect each retain a separate scroll position, and
expression presets wrap into a grid instead of overflowing the sidebar. Narrow
windows stack the inspector below the preview. The initial view hides geometry
overlays and opens Face, so the character and automatic caress experiment can
be tried without first clearing the diagnostic view. Stable point names remain
available under Inspect. These changes affect desktop widgets only.

The first Studio UI at `679fbda2` opened after an authorized restart; the user
reported blurry Windows scaling and about 100 ms host ticks, then closed it.
The subsequent DPI/performance corrections have static source review only.
Windows startup now explicitly requires Per Monitor v2 awareness before Tk,
then sets point scaling from the actual window DPI. Custom indicators, sidebar
dimensions, wrapping and spacing use that startup scale. Window dimensions are
bounded by the monitor's physical work area; compact side-by-side layout is used
when the stacked minimum cannot fit. The character's native
466 x 466 geometry and pointer mapping stay independent of UI scaling. Moving
between monitors with different DPI still needs a separate Tk scaling transition.

The exact four-sample coverage accumulator buckets edges by scanline and emits
disjoint coverage runs. Opaque RGB565 interiors use Pillow's C-backed copy; only
partial pixels use the original native channel blend. Visible inspector text
updates at most 10 Hz, status at 5 Hz; model, gesture and cue clocks still advance
each tick. The footer reports observed host FPS separately from the target.
The desktop frame limit defaults to30Hz (25Hz for the jelly study), with explicit
15/25/30/60 choices in the inspector. This limits host redraw demand without
changing firmware cadence or elapsed-time personality/gesture rules.
No improvement in FPS or CPU use has been measured yet.

A focused regression check is available through the public CLI, after explicit
authorization. It opens no Tk window and runs no benchmark:

```powershell
.\scripts\device.ps1 check -Check cody-preview-raster
```

It compares final RGB pixels against the established per-pixel compositor across
clipped/concave/fractional polygons and four opacities, plus analytic coverage,
winding reversal and an independent per-pixel coverage oracle on 32 x 32 masks.
Native firmware performance remains unmeasured.

The Face panel offers one manga-symbol selector in the main `rig` presentation.
The semantic `sleep` and `sleeping` presets select Sleep automatically; blinking
or closing the eye sliders does not. Editing a face slider clears the selected
preset identity. Greeting and boot artwork disable the symbol; Micro always
suppresses it. Device-symbol choices remain explicitly synthetic host previews.

The single-target channel uses a 300-ms stable request, 400-ms smooth fade-in,
2,500-ms minimum full-opacity hold and 600-ms smooth fade-out. Intermediate
requests are overwritten without a queue. A semantic Sleep preset starts its
fade without the stability delay; a manually selected Sleep symbol in an ordinary
pose retains that delay. Entering the Sleep preset releases any competing symbol
without its minimum hold, completing the old symbol's fade-out before Sleep's
fade-in. Leaving the semantic Sleep preset also releases it without waiting for
the minimum hold. Pause freezes the channel and following time. The interactive
Sleep preset can show the full transition; firmware power timing is unchanged
and may turn the panel off before a competing-symbol transition completes.

The simulator reads its stable IDs, priorities, gesture/mood rules, anchor
profiles and timing directly from validated `assets/cody/personality.yaml`.
Firmware receives generated immutable headers from the same source. The center follows
the evaluated Nose anchor plus its configured local offset using a 360-ms finite
history window; scale uses a 400-ms window. Seventeen fixed 25-ms geometry buckets
hold duration-weighted target integrals. Older geometry is discarded completely,
so a stationary target is reached within 400 ms of active time. Position lag is
capped at four pixels radially; deformation is limited to two pixels at the
largest base extent. Pause freezes these windows. This geometric history does
not queue symbol requests. Symbols use the generated RGB565 palette/coverage data and the same
integer sampling and opacity multiplication as native Cody. Pillow compositing
remains a host preview rather than pixel-level hardware proof.

The native status rectangle `(106,37)-(362,58)` is permanently reserved, along
with Back. A symbol that cannot fit its authored position, the body's clearance
or the circle reports `BLOCKED_BY_LAYOUT` in the host status line. It is hidden
without shifting or shrinking abruptly; the next admitted presentation fades in
again. The 2026-10-05 focused suite passed 122 native/Python controller parity
frames, and the interactive Rig window opened using the versioned pearl boot
artwork. Gesture feel, audio playback and hardware behavior remain unverified.
Disable Debug geometry and select symbol None to try automatic caress hearts.
See [personality symbols](../../docs/architecture/cody/personality-icons.md).

```powershell
.\scripts\device.ps1 simulator
.\scripts\device.ps1 simulator -Screen boot-waking -Rotation 180
.\scripts\device.ps1 simulator -Screen greeting -CloudJellyStudy -AtMs 1500 -OutputPath artifacts/simulator/greeting.png
```

The resizable inspector shares authored rig/response data, named point selection,
face and contour controls, pause, cardinal rotation and synthetic motion/touch.
One current viewport transform aligns preview and mouse input; resizing does not
advance animation time. Desktop annotations are inspector UI, not firmware text.

`-CloudJellyStudy` previews the native pearl approach; face-follow starts enabled.
`-CloudMaterialStudy` and `-CloudPngMeshStudy` are separate host-only appearance studies.
They are mutually exclusive material choices, not alternate firmware fallbacks.
PNG is preferable to GIF for judging subtle colors. Fixed synthetic GIF timelines
are not recorded sensor replay or rendering benchmarks.

Cody launches from source and needs Python with Pillow/PyYAML. See
[the console](../../scripts/README.md) for interpreter resolution, permitted output
paths and packaging. Current contracts:
[runtime](../../docs/architecture/cody/runtime.md),
[rendering](../../docs/architecture/cody/rendering.md).

## Luna and Leg previews

```powershell
.\scripts\device.ps1 simulator-luna -ToolArguments @('--scenario', 'battery-low', '--screen', 'battery')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'settings', '--capture', 'artifacts/simulator/settings.png')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--board-config')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'leg-motion')
.\scripts\device.ps1 package-simulator
```

Luna previews diagnostic pages, status/command arcs, power stages and semantic
device-twin actions. `--board-config` selects the minimal shell using the reviewed
board YAML and staged shell document; it cannot be mixed with full-device
scenarios/pages/radio options. Leg has acceleration, gyro, combined, motion and
settings views; values remain synthetic.

The packaged executable remains the Luna target, written under
`artifacts/simulator/windows/dist/`. Packaging explicitly prepares the local host
environment; launching a preview does not silently install it.

Controls: mouse = synthetic touch; `B` = short BOOT/fullscreen; `P` = PWR standby
toggle; `R` = BOOT soft reset; Enter = confirm; Esc = Back; `C` = confirmation;
F2 = viewport guide. Standby ignores touch/short BOOT and wakes through synthetic
PWR. Fullscreen stays locked until explicitly exited.

The initial central viewport is 320 x 320 at y=73; hiding only the lower arc
grows it to 320 x 393, and hiding both/fullscreen to 320 x 466 at y=0.
`--idle-state` supports active, command-hidden, chrome-hidden, fullscreen,
dimmed, off and standby captures.

## Deterministic fixtures

`scenarios/` uses `LUNASHELL.simulator.scenario.v1`: typed snapshot patches and
strictly ordered virtual-time events. Invalid types, duplicate keys, impossible
states and incoherent validity fail. Semantic actions retain preconditions:
formatting needs one-shot authorization; distance calibration needs current inputs.

```powershell
.\scripts\device.ps1 simulator-luna -ToolArguments @('--list-scenarios')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--scenario', 'healthy', '--at-ms', '2500', '--dump-snapshot')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--scenario', 'healthy', '--action', 'start:wireless', '--advance-ms', '1000', '--dump-api', '/api/v1/status')
```

`--scenario-file` selects an explicit fixture; `--output` writes JSON under
`artifacts/`. Replay advances only when requested and retains immutable
generations. The live scenario catalog replaces a duplicated list here.

## Loopback HTTP fixtures

`fake_device_server.py` and its local wrapper implement the internal test server:
loopback-only, normally port 18080, no GUI/serial/hardware, synthetic/no-cache
headers. They are fixture implementation, not a physical-device API or a second
public workflow. The intentionally public test token is
`LUNASHELL-SIMULATOR-ONLY-NOT-A-SECRET`.

Health is public. Capabilities/info/diagnostics/RTC/GPS/status GET routes require
exact Bearer matching; twenty failed authentications in sixty seconds rate-limit
data access without hiding health. The separate `/__simulator/` control plane
lists fixtures/state and accepts load/advance/action requests; it is not firmware
API behavior. See [HTTP contracts](../../docs/architecture/http-device-api.md).

Simulator execution and checks still need explicit approval under
[AGENTS](../../AGENTS.md). The `simulator` check covers fixtures, replay, actions,
redaction and loopback transport; Cody has separate check identifiers. No command
in this README has been executed as part of the documentation cleanup.
