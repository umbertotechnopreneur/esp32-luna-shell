# Development console

`scripts/device.ps1` is the sole public CLI/menu for firmware, simulators,
generators and checks. Run from the checkout in PowerShell. The menu, `help` and
`list` are inert: no ESP-IDF load, simulator, checks or serial probing starts
until an operation is selected. Help and labels are English.

## Commands

```powershell
.\scripts\device.ps1 help
.\scripts\device.ps1 list
.\scripts\device.ps1 build -Jobs 8
.\scripts\device.ps1 build -BuildMode Release -Jobs 8
.\scripts\device.ps1 flash -Port COM10 -BuildMode Release -Jobs 8
.\scripts\device.ps1 simulator
.\scripts\device.ps1 simulator -Screen boot-waking -Rotation 180
.\scripts\device.ps1 check -Check cody-native
.\scripts\device.ps1 generate -Generator cody-rig
.\scripts\device.ps1 size -SizeOutputFormat json2
.\scripts\device.ps1 clean-artifacts
```

These are usage examples, not automatically authorized operations. Follow
[AGENTS.md](../AGENTS.md), including fresh command-specific test approval and
explicit device-operation permission. Confirm the actual serial port first.

Firmware defaults to `Debug` and `cody-companion` on each invocation. Release
requires `-BuildMode Release` on each corresponding command; a menu session can
select its own mode. `-BoardConfig` owns its profile and cannot be combined with
`-Profile`/`-Feature`. Configuration and reviewed dependency-lock gates still apply.
See [build modes](../docs/architecture/firmware-build-modes.md).

`list` exposes current check/generator identifiers. Extra arguments are literal,
never evaluated shell code:

```powershell
.\scripts\device.ps1 check -Check cody-poc -ToolArguments @('--previews')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'battery', '--capture', 'battery.png')
```

The menu accepts extra arguments as a JSON string array. CLI failures propagate
to the caller; the menu preserves the failure before offering another selection.

## Serial and stored logs

```powershell
.\scripts\device.ps1 capture -Port COM10 -DurationSeconds 45 -ResetBeforeCapture -OutputPath artifacts/serial/boot.log
.\scripts\device.ps1 sd-log -Port COM10 -StoredLog current
.\scripts\device.ps1 sd-log -Port COM10 -StoredLog previous
```

Capture can explicitly reset first. Stored-log download does not reset or format:
it requires updated firmware, an awake device and a mounted SD. Close monitors
first. Downloads default to `artifacts/serial/sd-log-<name>.log`; blocks are checked
before replacing local output. `-WaitSeconds` bounds each USB response.
See [logging ownership](../docs/architecture/offline-logging-usb.md).

## Host tools

`-PythonExecutable` selects Python host tools; `-PythonExe` selects the ESP-IDF
interpreter. Host resolution checks `artifacts/python/simulator/Scripts/python.exe`
then PATH. Cody host tools require Pillow and PyYAML. Launching a simulator never
installs its environment automatically. `package-simulator` explicitly prepares
and packages the inherited Luna simulator; Cody is source-based.

## Ownership and outputs

| Location | Responsibility |
| --- | --- |
| `device.ps1` | Public dispatcher |
| `commands/` | Firmware engine, menus, packaging and banner maintenance |
| `common/` | Shared routing, console/help, artifact policy and source contracts |
| `generators/` | Authored-resource transformations |
| `checks/`, `tests/` | Explicit check runners and native fixtures |

CMake may invoke internal generator/check inputs directly; public manual
workflows still go through the dispatcher. The owned MenuManager copy has no
runtime dependency on `E:\tools`. Do not add a second public capture/menu alias.

All disposable output stays inside this checkout's `artifacts/`. Explicit
`-BuildDir`, `-LogPath`, `-OutputPath` and `-SizeOutputFile` must stay there too;
relative public paths are checkout-relative. Bare Python capture names default
to `artifacts/simulator/`.

Build/flash/combo/recover clean transient logs, serial captures, reports, previews,
check output and temporary files before starting. They retain firmware build
trees, dependency locks, configuration/board overlays and Python caches.
`fullclean` explicitly cleans the selected firmware build. Writers hold an
exclusive artifact lock; close an active simulator/monitor before another writer.
Cleanup validates targets and rejects reparse points.

Versioned `assets/**/generated` and downloaded `managed_components/` are source
inputs, not disposable build output. Logs default to `artifacts/logs/<action>.log`,
captures to `artifacts/serial/capture.log` and size to `artifacts/reports/size.<format>`.
