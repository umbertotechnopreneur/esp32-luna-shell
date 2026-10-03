# Cody development console

Run `pwsh -NoProfile -File .\scripts\device.ps1` from the checkout to open the
interactive menu. Opening the menu/help does not load ESP-IDF, start a simulator,
run checks or probe a serial port. Select an operation to run it explicitly.
All labels and command help are English.

Firmware mode defaults to `Debug` on every CLI invocation. Release requires
explicit `-BuildMode Release`, including for later flash/size/configuration
commands. Session settings can explicitly select Release for the current menu
session; the next menu starts in Debug. See the
[mode policy and deferred validation](../docs/architecture/firmware-build-modes.md).

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 help
pwsh -NoProfile -File .\scripts\device.ps1 simulator
pwsh -NoProfile -File .\scripts\device.ps1 simulator -Screen boot-waking -Rotation 180
pwsh -NoProfile -File .\scripts\device.ps1 boot-screens
pwsh -NoProfile -File .\scripts\device.ps1 check -Check cody-poc
pwsh -NoProfile -File .\scripts\device.ps1 check -Check device-console
pwsh -NoProfile -File .\scripts\device.ps1 generate -Generator cody-rig
pwsh -NoProfile -File .\scripts\device.ps1 build -Jobs 8
pwsh -NoProfile -File .\scripts\device.ps1 set-target -BuildMode Release
pwsh -NoProfile -File .\scripts\device.ps1 build -BuildMode Release -Jobs 8
pwsh -NoProfile -File .\scripts\device.ps1 flash -Port COM8 -Jobs 8
pwsh -NoProfile -File .\scripts\device.ps1 capture -Port COM8 -DurationSeconds 30
pwsh -NoProfile -File .\scripts\device.ps1 size -SizeOutputFormat json2
pwsh -NoProfile -File .\scripts\device.ps1 clean-artifacts
```

`list` prints all available checks and generators. `-Check`/`-Generator` select
their exact displayed identifier; the registry comes from the files in the
corresponding directory. Use `-ToolArguments` for literal tool-specific tokens.
In a PowerShell session, arrays preserve argument boundaries:

```powershell
.\scripts\device.ps1 check -Check cody-poc -ToolArguments @('--previews')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'battery', '--capture', 'battery.png')
```

The menu offers the same catalog and accepts extra tokens as a JSON string array,
for example `["--previews"]`. Tokens are never evaluated as shell code. A failed
command remains a visible failure; the interactive menu then offers another
selection. CLI failures propagate as failures to the calling process.

Use `-PythonExecutable` for Python host tools (Pillow and PyYAML for Cody),
and `-PythonExe` for the ESP-IDF interpreter. PowerShell tools receive their own
interpreter options through `-ToolArguments`. The host resolver first checks
`artifacts/python/simulator/Scripts/python.exe`, then Python on PATH. No host
environment is installed automatically by launching Cody. `package-simulator`
explicitly prepares the local environment and packages the inherited **Luna**
simulator EXE; Cody remains a source-based PoC.

The default firmware profile remains `cody-companion`. `-BoardConfig` selects its
own profile and cannot be combined with `-Profile`/`-Feature`. Existing reviewed
dependency-lock and SDKCONFIG gates remain mandatory. The currently outstanding
Cody dependency-lock review is tracked in `TODO.md`; adding this menu does not
resolve dependencies or prove that the firmware builds.

## Ownership and layout

| Location | Purpose |
| --- | --- |
| `device.ps1` | Sole public console and CLI dispatcher |
| `commands/` | Firmware engine, menu, executable packaging and banner maintenance |
| `common/` | Shared parameter routing, artifact policy and Python contracts |
| `common/MenuManager/` | Owned menu module copied from `E:\tools\scripts\modules\MenuManager` |
| `generators/` | Canonical asset generators and build-time resource transformations |
| `checks/` | Explicit checks and their budget data |
| `tests/` | Native test fixtures used by checks |

There is no separate serial-capture alias or simulator PowerShell menu. CMake
uses internal generator/check files directly as build inputs; their public manual
workflows belong to `device.ps1`. The shared menu has no runtime dependency on
`E:\tools`.

## Output and cleanup

All disposable project output belongs under `artifacts/`:

| Location | Contents | Before build/flash |
| --- | --- | --- |
| `build-*-debug`, `build-*-release`, or explicitly selected `-BuildDir` | Mode-specific firmware build, binaries, working dependency lock | Retained |
| `sdkconfig/`, `board-config/` | Generated configuration and board overlays | Retained |
| `python/` | Host environment and package cache | Retained |
| `logs/` | Current command logs | Replaced |
| `serial/` | Captured device output | Removed |
| `reports/`, `simulator/`, `checks/`, `cody-poc/`, `tmp/` | Reports, previews, host checks and temporary files | Removed |

`build`, `flash`, `combo` and `recover` clean transient artifacts **before**
starting the operation. The build required for flashing survives, as does
incremental compilation state. `fullclean` explicitly cleans the selected
firmware build. All command writers hold an exclusive artifact lock; close a
running simulator/monitor before starting another command in the same checkout.
Cleanup checks every removal target and rejects reparse points before deleting.

Explicit `-BuildDir`, `-LogPath`, `-OutputPath` and `-SizeOutputFile` must remain
inside this checkout's `artifacts/`. Relative public output paths are relative
to the checkout. The Python simulator's own bare capture names default to
`artifacts/simulator/`. Command logs use `logs/<action>.log`; serial captures
default to `serial/capture.log`, and size reports to `reports/size.<format>`.
Source assets under `assets/**/generated` are versioned firmware inputs, not
disposable artifacts. ESP-IDF's `managed_components/` is its downloaded dependency
source tree, not a build-output directory.

Only the requested `clean-artifacts` operation was executed, in addition to static
source review. The prepared console checks, existing checks, simulator, packaging
and firmware builds have not been run.
