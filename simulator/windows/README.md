# Windows simulators

Host previews of the 466 x 466 circular UI and authored behavior, not electrical
hardware or ESP-IDF driver emulation. All device-twin/stimulus values are visibly
`SYNTHETIC` and never enter firmware snapshots. Host timing and Pillow output do
not establish native pixels, FPS, CPU load or audible quality.

## Cody

The Face panel offers one manga-symbol selector. It previews the same generated
fixed-palette index/coverage assets and delayed pose follow as native Cody;
device-symbol choices are explicitly synthetic host previews. Micro suppresses
the symbol. The feature source is implemented, but this increment has not been
executed in the simulator. See [personality symbols](../../docs/architecture/cody/personality-icons.md).

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
