# ESP32 Luna Shell Windows device simulator

This desktop target mirrors the round 466 x 466 UI contract and application
flow. It does not emulate the Waveshare electrical hardware or ESP-IDF drivers.
All values originate in a deterministic device-twin snapshot and remain
visibly labelled `SYNTHETIC`; simulator state never enters firmware snapshots.

## Cody cloud-rig PoC

The unified `scripts/device.ps1 simulator` action opens
`cody_simulator.py`: a local cloud with named normalized points, cubic handles,
independent facial sliders, elapsed-time breathing/blinking, rotation and point
inspection. Independent contour controls change cloud fullness, wave amplitude,
speed and phase spread, with a separate pause control. Its screen selector
includes all three original boot artworks.
The Face tab also exposes mouth width, nose size, cheek size and cheek opacity;
sliders and example presets interpolate smoothly before breathing/blinking.
The Embarrassed example makes the cheeks more prominent using the same controls.
`assets/cody/cody.yaml` configures identity, entities, geometry and indexed style;
the shared palette has 256 slots. No fake-device HTTP server is started.

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 simulator -PythonExecutable 'E:\ESP32 Cody Companion\artifacts\python\simulator\Scripts\python.exe'
```

Use `-Screen boot-sleep|boot-waking|boot-ready|rig`, `-Rotation 180`,
`-SafeGuide`, or `-OutputPath .\artifacts\simulator\preview.png`. Select a Python
interpreter with Pillow and PyYAML using `-PythonExecutable`, or prepare the
local `artifacts/python/simulator` environment with the packaging action.
Cody options cannot be mixed with device-twin scenarios.
The legacy packaged EXE remains a Luna target; Cody currently launches from source.
See `docs/architecture/cody-rig-poc.md` for controls, limits and validation scope.

The debug view can show stable point names attached to evaluated positions,
the current contour and the undeformed reference outline. Name filters keep
anchors, contour points, one entity, one selected point or paged point groups
readable. Desktop annotations sit outside the device circle and stay upright
when the device view rotates; they are not additional firmware UI. Selecting
a point reports its normalized and logical canvas coordinates.

The desktop window is resizable. A uniform viewport transform keeps the round
preview, point labels and mouse input aligned at every size and rotation.
Controls move below the preview in narrow windows and stay scrollable when
vertical space is limited. Label pages adapt to viewport height without shrinking
the font; shortened names remain complete in the point inspector. Native rendering
and captures remain 466 x 466. Slider and checkbox construction share helpers.

The source palette remains 256 entries. Partial cheek opacity composites in
RGB565 precision, so the framebuffer can include derived intermediate colors.

Contour/label, responsive layout and facial-control changes have static source review only. Historical host evidence
in the architecture document belongs to the preceding rig checkpoint; no
simulator, build or device check was run for this extension.

## Minimal board shell preview

The reviewed `ui-bootstrap` board profile has a separate preview that reads
`config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml` through
the build validator and renders the shell's `package_shell/res/root.json`.
It shows the front-facing idle mascot on a dark gradient with a face assembled
from separate features. Mouse clicks inside the 320 × 320 surface increment the
synthetic touch counter in the console; the preview remains visibly labelled
`SYNTHETIC`.

```powershell
.\scripts\device.ps1 simulator-luna -ToolArguments '--board-config'
.\scripts\device.ps1 simulator-luna -ToolArguments '--board-config','--dump-board-shell'
.\scripts\device.ps1 simulator-luna -ToolArguments '--board-config','--shell-taps','2'
```

The unified menu exposes Cody and Luna simulator actions. Pass `--board-config`
through `-ToolArguments` to select this minimal board shell; it cannot be mixed
with full-device scenarios, pages, or radio options. The simulator needs Pillow
and PyYAML; `device.ps1 package-simulator` prepares the environment and packages the
YAML, shell JSON, mascot preview, and validator into the executable. This is a host preview of
the UI contract, not ESP-IDF or physical display/touch emulation.

## Deterministic scenarios

Scenario fixtures live in `simulator/windows/scenarios`. Each JSON file uses
the `LUNASHELL.simulator.scenario.v1` schema, applies typed dotted-path patches
to the canonical synthetic snapshot, and can add strictly ordered virtual-time
events. Invalid field types, impossible component states, duplicate keys, and
incoherent sensor validity fail fast when the fixture is loaded.

The built-in catalog is:

| Scenario | State exercised |
| --- | --- |
| `healthy` | Ready baseline with current telemetry and stopped radios |
| `battery-low` | Valid low-battery warning |
| `battery-charging` | USB-powered charging state |
| `battery-unavailable` | Explicit PMIC read failure without numeric placeholders |
| `pmic-no-cell` | Valid PMIC observation with no battery cell present |
| `gps-searching` | Receiver active without a valid fix |
| `boot-partial` | Partial startup followed by a deterministic ready transition |
| `memory-pressure` | Low-memory state followed by recovery |
| `sd-unformatted` | Verified card requiring one authorized format action |
| `sd-card-lifecycle` | Mounted card removal and insertion timeline |
| `stale-recovery` | Current, stale, failed, and recovered battery samples |
| `standby-rollback` | Standby transition and failed-wake rollback |
| `tof-one-missing` | One unavailable distance sensor followed by recovery |
| `wifi-lifecycle` | Start, connect, lease loss, and reconnection timeline |

The scenario clock advances only when requested. Replaying the same fixture,
offsets, and actions produces the same immutable generations and JSON output.
Semantic actions enforce device preconditions: for example, SD formatting
requires a present verified format-required card and consumes its authorization
once, while TOF calibration requires two current sensors.

Run from the project root:

```powershell
.\scripts\device.ps1 simulator-luna
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','sd-unformatted'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','gps-searching'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','battery-low','--screen','battery'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','battery-charging','--screen','battery'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','battery-unavailable','--screen','battery'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','battery'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','logs'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','settings'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','gps'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','gps-searching','--screen','gps'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','storage'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','pairing-intro'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','leg-accel'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','leg-motion','--capture','.\artifacts\leg-motion-preview.png'
```

The Leg Edition preview has `leg-accel`, `leg-gyro`, `leg-combo`,
`leg-motion`, and `leg-settings` screens. Its IMU values and graph are
visibly labelled synthetic; it previews the layout and navigation geometry,
not the firmware renderer or sensor behavior.

### Headless device-twin CLI

The Python entry point can validate or inspect the same model without creating
a Tk window:

```powershell
.\scripts\device.ps1 simulator-luna -ToolArguments '--list-scenarios'
.\scripts\device.ps1 simulator-luna -ToolArguments '--validate-scenarios'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','healthy','--at-ms','2500','--dump-snapshot'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','healthy','--action','start:wireless','--advance-ms','1000','--dump-api','/api/v1/status'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario-file','.\path\custom.json','--dump-api','/api/v1/info','--output','artifacts/simulator/device-info.json'
```

`--scenario-file` loads an explicit v1 JSON fixture; `--at-ms` chooses its
initial virtual-clock offset. `--action` applies a semantic action and
`--advance-ms` moves the clock before `--dump-snapshot` or `--dump-api` emits
JSON. `--output` writes that JSON to a file instead of standard output. Use
`--list-scenarios` for the live catalog and `--validate-scenarios` to validate
every built-in fixture.

Run the full hardware-free contract package from the repository root:

```powershell
.\scripts\device.ps1 check -Check simulator
```

It validates every fixture and timeline, deterministic clock replay, immutable
generations, semantic wireless/SD/TOF actions, public-info redaction, and the
loopback HTTP transport. It returns a nonzero exit code on any failure.

Capture the current synthetic home screen without touching a device:

```powershell
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','healthy','--capture','artifacts/simulator/healthy.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','healthy','--screen','battery','--capture','artifacts/simulator/battery-healthy.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','battery-low','--screen','battery','--capture','artifacts/simulator/battery-low.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','battery-charging','--screen','battery','--capture','artifacts/simulator/battery-charging.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','battery-unavailable','--screen','battery','--capture','artifacts/simulator/battery-unavailable.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','healthy','--screen','logs','--capture','artifacts/simulator/logs.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','healthy','--screen','settings','--capture','artifacts/simulator/settings.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','healthy','--screen','gps','--capture','artifacts/simulator/gps-fix.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','gps-searching','--screen','gps','--capture','artifacts/simulator/gps-searching.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','healthy','--screen','storage','--capture','artifacts/simulator/storage.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--scenario','sd-unformatted','--screen','storage','--capture','artifacts/simulator/storage-format-required.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','pairing-app-qr','--capture','artifacts/simulator/pairing-app-qr.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','pairing-device-qr','--capture','artifacts/simulator/pairing-device-qr.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','pairing-success','--capture','artifacts/simulator/pairing-success.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','pairing-failed','--capture','artifacts/simulator/pairing-failed.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--idle-state','command-hidden','--capture','artifacts/simulator/command-hidden.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','settings','--idle-state','fullscreen','--safe-guide','--capture','artifacts/simulator/settings-fullscreen.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--idle-state','dimmed','--capture','artifacts/simulator/dimmed.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','chrome-settings','--capture','artifacts/simulator/chrome-settings.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','power-settings','--capture','artifacts/simulator/power-settings.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','hardware','--capture','artifacts/simulator/hardware.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','tof','--capture','artifacts/simulator/tof.png'
.\scripts\device.ps1 simulator-luna -ToolArguments '--screen','tof-settings','--capture','artifacts/simulator/tof-settings.png'
```

Controls:

- mouse click: synthetic touch
- `B`: simulate a short `BOOT` press. From `active` it toggles Fullscreen;
  from an automatic hidden/dim/off state it wakes to `active` first, and
  `standby` ignores it.
- `P`: toggle the synthetic `PWR` standby/awake state and cancel Fullscreen
- `Enter`: confirm
- `Esc`: back/cancel
- `C`: open confirmation
- `R`: simulate the three-second `BOOT` soft reset and cancel Fullscreen
- `F2`: show or hide the dynamic content-viewport guide without changing page

The capture-only `-IdleState` option accepts `active`, `command-hidden`,
`chrome-hidden`, `fullscreen`, `dimmed`, `off`, and `standby`. It provides
deterministic visual checks for the power-management stages. A synthetic touch
restores an automatically hidden stage, but Fullscreen remains locked while
page hit targets stay active. Standby ignores touch and short `BOOT`; it wakes
only through synthetic `PWR`.

The viewport follows the firmware insets. With both arcs visible it is
320 x 320 at `y=73`; releasing only the lower Command Arc grows it to
320 x 393; hiding both arcs or entering Fullscreen grows it to 320 x 466 at
`y=0`. The `F2` guide shows the current bounds and the original safe square.
The Settings demo also shifts upward and reveals additional Display & Motion
controls as that viewport grows.

The persistent upper status ribbon and lower adaptive command arc reproduce the
firmware geometry, visual hierarchy, and semantic color roles. The Windows
renderer uses antialiased radial atmosphere, gradients, and restrained bloom so
captures remain representative of the AMOLED design rather than raw Tk canvas
primitives. The displayed values remain deterministic simulator-only data.

The first Welcome command opens the Battery detail page. Its large AXP2101
battery graphic, percentage, threshold bands, voltages, USB state, system rail,
PMIC temperature, charger phase/status flags, configured charge policy, and
sample health are all deterministic and visibly marked `SYNTHETIC`; no value
comes from a device or serial port. The `healthy`, `battery-low`,
`battery-charging`, and
`battery-unavailable` scenarios exercise the normal, critical, charging, and
error presentations.

Logs now lives in the Settings command arc. Open Settings and choose the left
Logs command to inspect the synthetic RAM-ring preview; `Esc` returns to
Settings. Battery returns to Welcome, or to Hardware when it was opened from
the AXP2101 component row.

Settings exposes GPS, Wireless, and SD management only in its lower command
arc, leaving the scrolling body for status and configuration data. GPS is green with a fix,
amber while searching, neutral when unavailable, and red on an explicit
failure. Wireless is amber while stopped and green while running; selecting
its central command switches between synthetic Start and Stop states. Use
`--wireless-running` through `device.ps1 simulator-luna -ToolArguments` to capture the active state
directly. The GPS page mirrors
the firmware layout with receiver state, fix validity, coordinates, satellite
count, HDOP, altitude, speed, course, GNSS UTC, fix quality, and NMEA queue
state. Every simulator value is visibly marked `SYNTHETIC`; the
`gps-searching` scenario keeps fix-dependent numeric fields at `NO FIX`.

Build the standalone Windows executable:

```powershell
.\scripts\commands\package-simulator.ps1
```

The packaging action creates `artifacts/python/simulator` and writes
its generated build files and executable below
`artifacts/simulator/windows/`, including
`artifacts/simulator/windows/dist/ESP32LunaShellDeviceSimulator.exe`.

## Loopback fake HTTP device

Start a deterministic HTTP device for client and agent integration tests:

```powershell
pwsh -NoProfile -File .\simulator\windows\serve-fake-device.ps1
pwsh -NoProfile -File .\simulator\windows\serve-fake-device.ps1 -Scenario stale-recovery -StartAtMs 2000 -Port 18081
```

The server defaults to `http://127.0.0.1:18080` and rejects non-loopback bind
addresses. It uses only Python's standard library, never opens a serial port,
and adds `X-LUNASHELL-Synthetic: true` plus `Cache-Control: no-store` to every
response. Its fixed test-only Bearer key is intentionally public:

```text
LUNASHELL-SIMULATOR-ONLY-NOT-A-SECRET
```

`GET /api/v1/health` is public. These read-only firmware-contract routes
require `Authorization: Bearer LUNASHELL-SIMULATOR-ONLY-NOT-A-SECRET`:

- `GET /api/v1/capabilities`
- `GET /api/v1/info`
- `GET /api/v1/diagnostics`
- `GET /api/v1/rtc`
- `GET /api/v1/gps`
- `GET /api/v1/status`

The separate loopback-only control plane is not part of the firmware API:

- `GET /__simulator/list` lists fixtures and the active scenario.
- `GET /__simulator/state` returns the current immutable generation.
- `POST /__simulator/load` accepts `{"scenario":"healthy","start_at_ms":0}`.
- `POST /__simulator/advance` accepts `{"milliseconds":25}`.
- `POST /__simulator/action` accepts
  `{"action":"start:wireless","params":{}}`.

Data routes use exact Bearer matching and a deterministic-compatible limit of
20 failed authentications in 60 seconds. The health route remains available
while data access is rate-limited, matching its liveness purpose.

## Audio scope demo

After the synthetic boot completes, click the Audio command. The page is a
high-refresh preview of the two-microphone layout. Its left command opens one
FFT series (channel 1 for a single-channel source, otherwise the two-channel
mean); its right command opens the separate two-channel spectrum. Firmware
shows the second command only when two channels are available. Press `Esc` to
return. The simulator bars are deliberately synthetic and visibly labelled;
CPU utilization is shown as unavailable rather than invented.

The firmware audio snapshot now carries two interleaved ES7210 channel levels,
while the firmware IMU page consumes calibrated QMI8658 acceleration,
gyroscope, temperature, bias, and calibration-sample fields. Home CPU bars use
real idle-task runtime on hardware; simulator CPU bars remain visibly labelled,
deterministic synthetic values.

The Distance and Distance Settings demos mirror the dual-TOF page, IMU gear
navigation, robust-window controls, 500–1500 mm reference slider, and
calibration action. Every simulated distance and configuration value is
explicitly labelled `SYNTHETIC`; the simulator never opens a serial port.
