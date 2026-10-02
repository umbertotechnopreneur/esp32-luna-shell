# ESP32 Luna Shell Windows device simulator

This desktop target mirrors the round 466 x 466 UI contract and application
flow. It does not emulate the Waveshare electrical hardware or ESP-IDF drivers.
All values originate in a deterministic device-twin snapshot and remain
visibly labelled `SYNTHETIC`; simulator state never enters firmware snapshots.

## Cody cloud-rig PoC

The `esp32-cody-companion` worktree adds `-Cody` to this same launcher. It opens
`cody_simulator.py`: a local cloud with named normalized points, cubic handles,
independent facial sliders, elapsed-time breathing/blinking, rotation and point
inspection. Independent contour controls change cloud fullness, wave amplitude,
speed and phase spread, with a separate pause control. Its screen selector
includes all three original boot artworks.
`assets/cody/cody.yaml` configures identity, entities, geometry and indexed style;
the shared palette has 256 slots. No fake-device HTTP server is started.

```powershell
pwsh -NoProfile -File .\simulator\windows\run-simulator.ps1 -Cody -PythonExecutable 'E:\ESP32 Luna Shell\.venv-simulator\Scripts\python.exe'
```

Use `-CodyScreen boot-sleep|boot-waking|boot-ready|rig`, `-CodyRotation 180`,
`-SafeGuide`, or `-Capture .\artifacts\cody-poc\preview.png`. The explicit Python
path reuses the existing Luna environment; a local `.venv-simulator` with Pillow
and PyYAML also works. Cody options cannot be mixed with device-twin scenarios.
The legacy packaged EXE remains a Luna target; Cody currently launches from source.
See `docs/architecture/cody-rig-poc.md` for controls, limits and validation scope.

The debug view can show stable point names attached to evaluated positions,
the current contour and the undeformed reference outline. Name filters keep
anchors, contour points, one entity, one selected point or paged point groups
readable. Desktop annotations sit outside the device circle and stay upright
when the device view rotates; they are not additional firmware UI. Selecting
a point reports its normalized and logical canvas coordinates.

Contour/label changes have static source review only. Historical host evidence
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
pwsh -NoProfile -File .\simulator\windows\run-simulator.ps1 -BoardConfig
pwsh -NoProfile -File .\simulator\windows\run-simulator.ps1 -BoardConfig -DumpBoardShell
pwsh -NoProfile -File .\simulator\windows\run-simulator.ps1 -BoardConfig -ShellTaps 2
```

The interactive launcher offers `U` to switch between the full device twin and
this minimal board shell. `-BoardConfig` cannot be combined with full-device
scenarios, pages, or radio options. The simulator environment needs Pillow
and PyYAML; `build-exe.ps1` installs both in `.venv-simulator` and packages the
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
.\simulator\windows\run-simulator.ps1
.\simulator\windows\run-simulator.ps1 -Scenario sd-unformatted
.\simulator\windows\run-simulator.ps1 -Scenario gps-searching
.\simulator\windows\run-simulator.ps1 -Scenario battery-low -Screen battery
.\simulator\windows\run-simulator.ps1 -Scenario battery-charging -Screen battery
.\simulator\windows\run-simulator.ps1 -Scenario battery-unavailable -Screen battery
.\simulator\windows\run-simulator.ps1 -Screen battery
.\simulator\windows\run-simulator.ps1 -Screen logs
.\simulator\windows\run-simulator.ps1 -Screen settings
.\simulator\windows\run-simulator.ps1 -Screen gps
.\simulator\windows\run-simulator.ps1 -Scenario gps-searching -Screen gps
.\simulator\windows\run-simulator.ps1 -Screen storage
.\simulator\windows\run-simulator.ps1 -Screen pairing-intro
.\simulator\windows\run-simulator.ps1 -Screen leg-accel
.\simulator\windows\run-simulator.ps1 -Screen leg-motion -Capture .\artifacts\leg-motion-preview.png
```

The Leg Edition preview has `leg-accel`, `leg-gyro`, `leg-combo`,
`leg-motion`, and `leg-settings` screens. Its IMU values and graph are
visibly labelled synthetic; it previews the layout and navigation geometry,
not the firmware renderer or sensor behavior.

### Headless device-twin CLI

The Python entry point can validate or inspect the same model without creating
a Tk window:

```powershell
python -B .\simulator\windows\lunashell_simulator.py --list-scenarios
python -B .\simulator\windows\lunashell_simulator.py --validate-scenarios
python -B .\simulator\windows\lunashell_simulator.py --scenario healthy --at-ms 2500 --dump-snapshot
python -B .\simulator\windows\lunashell_simulator.py --scenario healthy --action start:wireless --advance-ms 1000 --dump-api /api/v1/status
python -B .\simulator\windows\lunashell_simulator.py --scenario-file .\path\custom.json --dump-api /api/v1/info --output .\tmp\device-info.json
```

`--scenario-file` loads an explicit v1 JSON fixture; `--at-ms` chooses its
initial virtual-clock offset. `--action` applies a semantic action and
`--advance-ms` moves the clock before `--dump-snapshot` or `--dump-api` emits
JSON. `--output` writes that JSON to a file instead of standard output. Use
`--list-scenarios` for the live catalog and `--validate-scenarios` to validate
every built-in fixture.

Run the full hardware-free contract package from the repository root:

```powershell
python -B .\scripts\verify_simulator.py
```

It validates every fixture and timeline, deterministic clock replay, immutable
generations, semantic wireless/SD/TOF actions, public-info redaction, and the
loopback HTTP transport. It returns a nonzero exit code on any failure.

Capture the current synthetic home screen without touching a device:

```powershell
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Capture .\simulator\windows\healthy.png
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Screen battery -Capture .\simulator\windows\battery-healthy.png
.\simulator\windows\run-simulator.ps1 -Scenario battery-low -Screen battery -Capture .\simulator\windows\battery-low.png
.\simulator\windows\run-simulator.ps1 -Scenario battery-charging -Screen battery -Capture .\simulator\windows\battery-charging.png
.\simulator\windows\run-simulator.ps1 -Scenario battery-unavailable -Screen battery -Capture .\simulator\windows\battery-unavailable.png
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Screen logs -Capture .\simulator\windows\logs.png
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Screen settings -Capture .\simulator\windows\settings.png
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Screen gps -Capture .\simulator\windows\gps-fix.png
.\simulator\windows\run-simulator.ps1 -Scenario gps-searching -Screen gps -Capture .\simulator\windows\gps-searching.png
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Screen storage -Capture .\simulator\windows\storage.png
.\simulator\windows\run-simulator.ps1 -Scenario sd-unformatted -Screen storage -Capture .\simulator\windows\storage-format-required.png
.\simulator\windows\run-simulator.ps1 -Screen pairing-app-qr -Capture .\simulator\windows\pairing-app-qr.png
.\simulator\windows\run-simulator.ps1 -Screen pairing-device-qr -Capture .\simulator\windows\pairing-device-qr.png
.\simulator\windows\run-simulator.ps1 -Screen pairing-success -Capture .\simulator\windows\pairing-success.png
.\simulator\windows\run-simulator.ps1 -Screen pairing-failed -Capture .\simulator\windows\pairing-failed.png
.\simulator\windows\run-simulator.ps1 -IdleState command-hidden -Capture .\simulator\windows\command-hidden.png
.\simulator\windows\run-simulator.ps1 -Screen settings -IdleState fullscreen -SafeGuide -Capture .\simulator\windows\settings-fullscreen.png
.\simulator\windows\run-simulator.ps1 -IdleState dimmed -Capture .\simulator\windows\dimmed.png
.\simulator\windows\run-simulator.ps1 -Screen chrome-settings -Capture .\simulator\windows\chrome-settings.png
.\simulator\windows\run-simulator.ps1 -Screen power-settings -Capture .\simulator\windows\power-settings.png
.\simulator\windows\run-simulator.ps1 -Screen hardware -Capture .\simulator\windows\hardware.png
.\simulator\windows\run-simulator.ps1 -Screen tof -Capture .\simulator\windows\tof.png
.\simulator\windows\run-simulator.ps1 -Screen tof-settings -Capture .\simulator\windows\tof-settings.png
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
`-WirelessRunning` with `run-simulator.ps1` to capture the active state
directly. The GPS page mirrors
the firmware layout with receiver state, fix validity, coordinates, satellite
count, HDOP, altitude, speed, course, GNSS UTC, fix quality, and NMEA queue
state. Every simulator value is visibly marked `SYNTHETIC`; the
`gps-searching` scenario keeps fix-dependent numeric fields at `NO FIX`.

Build the standalone Windows executable:

```powershell
.\simulator\windows\build-exe.ps1
```

The build script creates an isolated `.venv-simulator` environment and writes
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
