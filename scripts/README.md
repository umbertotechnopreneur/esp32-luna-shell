# QSee.ai development scripts

This folder contains the developer entrypoints for building, flashing, testing,
and maintaining the QSee.ai firmware project. If you are new to the repository,
start here: the scripts hide most of the ESP-IDF and device-specific command
line complexity behind one PowerShell entrypoint.

## What this repository is

QSee.ai is a firmware project for the Waveshare ESP32-S3-Touch-AMOLED-1.75
GPS variant. The main application lives in the firmware source tree, while this
`scripts/` folder provides the operational tooling needed to:

- build the firmware;
- flash the target board;
- open a serial monitor or capture logs;
- generate or verify bundled assets;
- manage repository-specific maintenance tasks;
- inspect and persist local workstation settings.

## Main folders

- `main/` contains the primary firmware application entrypoints and runtime
	logic.
- `components/` contains reusable firmware components and application modules.
- `managed_components/` contains third-party or IDF-managed dependencies.
- `assets/` contains source material used to generate runtime assets such as
	display content, branding, and sound effects.
- `docs/` contains architecture notes, UI guidelines, hardware documentation,
	and other project references.
- `hardware/` contains machine-readable hardware inventory data.
- `simulator/` contains the Windows-side simulator and related support code.
- `scripts/` contains the automation layer documented in this file.
- `build*/` folders are generated build outputs for different configurations
	and should not be edited manually.
- `runtime-logs/` stores captured logs and other run-time artifacts.

## How to run it

`device.ps1` is the canonical PowerShell entrypoint for firmware, serial,
asset, verification, source-banner, and local-settings workflows.

Run it without parameters to open the interactive menu:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1
```

Every menu operation also has a prompt-free CLI form:

```powershell
# Firmware and serial
pwsh -NoProfile -File .\scripts\device.ps1 build -BuildDir build-brookesia
pwsh -NoProfile -File .\scripts\device.ps1 flash -Port COM12 -BuildDir build-brookesia -Baud 921600
pwsh -NoProfile -File .\scripts\device.ps1 monitor -Port COM12 -BuildDir build-brookesia
pwsh -NoProfile -File .\scripts\device.ps1 capture -Port COM12 -DurationSeconds 30

# Assets and static verification
pwsh -NoProfile -File .\scripts\device.ps1 assets
pwsh -NoProfile -File .\scripts\device.ps1 boot-assets
pwsh -NoProfile -File .\scripts\device.ps1 sfx
pwsh -NoProfile -File .\scripts\device.ps1 pairing

# Repository maintenance
pwsh -NoProfile -File .\scripts\device.ps1 source-banner
pwsh -NoProfile -File .\scripts\device.ps1 source-banner -Apply
pwsh -NoProfile -File .\scripts\device.ps1 source-banner -Remove -Apply
```

Serial actions never close an existing monitor by default. If one is detected,
the command reports its PID and stops. Close it yourself, or explicitly allow
the script to close matching monitor processes for that run:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 flash -Port COM12 -StopStaleMonitors
```

## Required environment

This repository expects a Windows development machine with:

- PowerShell 7 (`pwsh`);
- Python;
- ESP-IDF configured through `IDF_PATH` and related tools;
- access to the board's serial port for flashing and monitoring;
- FFmpeg and other helper tools when running asset workflows.

The checked-in defaults live in `settings.defaults.json`. The `settings`
action can inspect them or save a workstation-local override under the ignored
`scripts/logs/settings.json` path:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 settings
pwsh -NoProfile -File .\scripts\device.ps1 settings -Port COM12 -BuildDir build-brookesia -SaveSettings
pwsh -NoProfile -File .\scripts\device.ps1 settings -IdfPath E:\Espressif\.espressif\v5.5.4\esp-idf -AssetPython python -SaveSettings
pwsh -NoProfile -File .\scripts\device.ps1 settings -ResetSettings
```

The reset form is handled before parsing the local file, so it can recover from
an invalid or partially written `scripts/logs/settings.json`.

Environment overrides are available for:

- `QSEE_PORT`, `QSEE_BAUD`, `QSEE_BUILD_DIR`, `QSEE_WAIT_SECONDS`,
  `QSEE_DURATION_SECONDS`, and `QSEE_JOBS`;
- `IDF_PATH`, `IDF_TOOLS_PATH`, and `IDF_PYTHON_ENV_PATH`;
- `OPENOCD_SCRIPTS`, `ESP_ROM_ELF_DIR`, and `QSEE_ASSET_PYTHON`.

The complete precedence is explicit CLI, environment, local settings, then
checked-in defaults.

## What the scripts do

Every run performs action-specific preflight checks, writes a timestamped log
under `scripts/logs/`, and prints a shared header and completion footer. Use
`-NoBanner` for minimal automation output and `-NoProgress` when progress
rendering is undesirable.

`device.ps1` is the main orchestration layer. It handles the common workflows,
chooses the correct build directory, validates the target setup, and routes to
the underlying implementation helpers.

## Compatibility shims

The historical PowerShell names remain as thin shims:

- `capture-serial.ps1`
- `generate-device-assets.ps1`
- `generate-sfx.ps1`
- `add-source-banner.ps1`

Without parameters they open the shared menu, consistently with the canonical
entrypoint. Add `-NonInteractive` to request their historical default action,
or pass an existing action parameter such as `-Apply`, `-Remove`, `-Port`, or
`-ConfigPath`. A shim forwards only parameters supplied by the caller, so the
same environment and local-setting precedence remains in effect.

The Python files are implementation helpers and remain directly callable with
their existing arguments.

## Quick start

If you only want to get the project running, a practical first path is:

1. Open a PowerShell 7 terminal in the repository root.
2. Verify the ESP-IDF environment is configured.
3. Run `pwsh -NoProfile -File .\scripts\device.ps1`.
4. Use `build` first, then `flash`, then `monitor`.

If you are unsure which build folder to use, check the root `README.md` for
the recommended firmware setup and target configuration.
