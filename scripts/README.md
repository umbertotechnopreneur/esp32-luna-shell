# Development console

Use `scripts/device.ps1` from PowerShell at the repository root for firmware,
simulators, generators and checks. The menu, `help` and `list` do not load
ESP-IDF, start a simulator or access serial hardware. Execution authorization is
defined in [AGENTS.md](../AGENTS.md).

## Firmware prerequisites on Windows and Ubuntu

Install a current PowerShell 7 (`pwsh`) release using Microsoft's instructions for [Windows](https://learn.microsoft.com/en-us/powershell/scripting/install/install-powershell-on-windows)
or [Ubuntu](https://learn.microsoft.com/en-us/powershell/scripting/install/install-ubuntu).
PowerShell is the prerequisite for starting the console itself.

Run the preflight from the checkout on either host.

```powershell
pwsh -NoProfile -File ./scripts/device.ps1 preflight
```

Preflight checks installed prerequisites and installs the missing ones in
sequence, including the pinned **ESP-IDF 6.0.2** toolchain for ESP32-S3 and its
**Python 3.13.x** environment. Installing missing tools requires Internet access.
Preflight stops at the first failed step and reports the step and cause. Earlier
successful steps stay installed, so rerun the same command after fixing the
failure. Preflight prepares the host outside the checkout. First-time imports
into the repository still require the targeted Defender scan in [AGENTS.md](../AGENTS.md).
Firmware configuration, build and serial operations are separate console commands.

On Windows, preflight keeps an existing `E:\Espressif` installation. For a
new setup, it uses the system drive's `Espressif` directory, usually
`C:\Espressif`. It uses WinGet for missing Git and Python 3.13, then installs
the pinned SDK from Espressif's repository with its official tools utility.
Open a new terminal after WinGet changes the Windows PATH.

On Ubuntu booted from USB, keep the checkout and tools on writable Linux storage
with space-free paths, such as `~/src/esp32-luna-shell`. Preflight can prompt
for `sudo` to install missing Ubuntu packages. It keeps the default SDK and
tools under `~/.local/share/luna-shell/esp-idf/`. Subsequent console commands
can use that installation directly. If Ubuntu has no usable Python 3.13 package,
preflight can compile the verified CPython 3.13.18 source into the user's home
directory. It adds the current account to `dialout` when needed and reports
when a new login is required for USB access. If you already have an ESP-IDF 6.0.2 EIM
installation, you can activate it in Bash before starting `pwsh`. See
[Espressif's activation instructions](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/get-started/linux-macos-start-project.html#activate-the-environment).

For a custom installation on either host, pass `-IdfPath`, `-ToolsPath` and
`-PythonExe` or set the corresponding `LUNASHELL_IDF_*` overrides. The
[toolchain guide](../docs/bsp/toolchain-and-profiles.md#selected-toolchain-and-framework)
lists the exact names.

## Choose an operation

```powershell
.\scripts\device.ps1 help
.\scripts\device.ps1 list
.\scripts\device.ps1 build -Jobs 8
.\scripts\device.ps1 build -BuildMode Release -Jobs 8
.\scripts\device.ps1 flash -Port COM10 -BuildMode Release -Jobs 8
.\scripts\device.ps1 simulator
.\scripts\device.ps1 simulator -Screen boot-waking -Rotation 180
.\scripts\device.ps1 simulator -Ccp
.\scripts\device.ps1 check -Check cody-native
.\scripts\device.ps1 generate -Generator cody-rig
.\scripts\device.ps1 size -SizeOutputFormat json2
.\scripts\device.ps1 endurance-read -Port COM10
.\scripts\device.ps1 clean-artifacts
```

These are command examples, not permission to execute them. Each invocation
defaults to `Debug` and `cody-companion`. Select `-BuildMode Release` on each
corresponding command; the menu keeps a selection for its session.
`-BoardConfig` owns its profile and cannot combine with `-Profile`/`-Feature`.
Read [toolchain and profiles](../docs/bsp/toolchain-and-profiles.md) for composition,
configuration guards and dependency-lock review. Confirm device identity and
port before authorized serial operations.

Physical board selection is independent of the interface, feature and build mode.
The default remains `waveshare-esp32s3-touch-amoled-1.75` (GPS, 16 MB).
Select the enclosed 32 MB board explicitly with
`-Board waveshare-esp32s3-touch-amoled-1.75c` on **every** configure/build/flash
command. Its generated configuration and build directory are isolated by the
`-1.75c` suffix; the old board's build state is not reused.

```powershell
.\scripts\device.ps1 set-target -Board waveshare-esp32s3-touch-amoled-1.75c -Profile cody-companion -Feature wake-word -BuildMode Debug
.\scripts\device.ps1 flash -Board waveshare-esp32s3-touch-amoled-1.75c -Profile cody-companion -Feature wake-word -BuildMode Debug -Port COM8 -Jobs 8
```

On Ubuntu, use the same selectors after preflight. Plug in Cody and
use `ports` to identify its path. Prefer the device's `/dev/serial/by-id/` alias
when available and replace `/dev/ttyACM0` below with the selected path. A USB
serial bridge may appear as `/dev/ttyUSB0`. Sign out and back in if preflight
reports that the current session lacks `dialout`. Close any monitor using the
port before flashing. See the
[ESP-IDF serial permissions guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/get-started/linux-macos-start-project.html#permission-denied-issue).

```bash
pwsh -NoProfile -File ./scripts/device.ps1 ports
pwsh -NoProfile -File ./scripts/device.ps1 set-target -Board waveshare-esp32s3-touch-amoled-1.75c -Profile cody-companion -Feature wake-word -BuildMode Debug
pwsh -NoProfile -File ./scripts/device.ps1 build -Board waveshare-esp32s3-touch-amoled-1.75c -Profile cody-companion -Feature wake-word -BuildMode Debug -Jobs 8
pwsh -NoProfile -File ./scripts/device.ps1 flash -Board waveshare-esp32s3-touch-amoled-1.75c -Profile cody-companion -Feature wake-word -BuildMode Debug -Port /dev/ttyACM0 -Jobs 8
```

Use `build` when you want binaries alone. `flash` includes the build step.
Linux uses a `-linux` suffix for default build and generated configuration
paths, keeping Windows CMake state separate. Use separate custom `-BuildDir`
paths for each host too.

Both 1.75C OTA slots and the optional Sophia model fit entirely below `0x1000000`
(16 MiB), within the firmware's 24-bit flash cache mapping range. The upper
16 MiB, starting at `0x1000000`, are reserved for LittleFS files and resources.

The 1.75C uses its own reset/MCLK wiring and IMU INT1 wake output.
Its PMU owner verifies the DCDC1/ALDO1 rails and uses the vendor's 4.10 V
charge target. SD, GNSS, PCF85063 RTC and TCA9554 acquisition are unavailable on
this board. System time can synchronize over SNTP without an external RTC.
The `tts-sd`, distance and Leg Edition compositions are rejected on 1.75C.
The shared BSP keeps Cody/display-only rendering native, without LVGL.
Physical acceptance is tracked in [TODO.md](../TODO.md); a build or flash does
not establish display, touch, audio, battery or wake behavior.

The `cody-companion` profile now builds a native device panel with counters,
connection/pairing controls and options. Its default `-Feature none` omits the
character renderer, character boot audio, microphone capture, Sophia and activity
models; speaker notifications remain enabled. IMU orientation and the existing
power, radio and storage owners remain available.

Codex voice handoff is still opt-in through `-Feature wake-word`. It restores the
dual microphones, ESP-SR and Sophia model with their existing partition and
dependency lock. This feature retains continuous standby listening and its
software-standby power cost. Use the same explicit feature selection for
configuration, build, flash and size. Cody's TTS and activity-classification
compositions are rejected on this branch.

```powershell
.\scripts\device.ps1 set-target -Profile cody-companion -Feature wake-word -BuildMode Release
.\scripts\device.ps1 build -Profile cody-companion -Feature wake-word -BuildMode Release -Jobs 8
.\scripts\device.ps1 flash -Profile cody-companion -Feature wake-word -BuildMode Release -Port COM10 -Jobs 8
.\scripts\device.ps1 size -Profile cody-companion -Feature wake-word -BuildMode Release -SizeOutputFormat json2
```

The first `set-target` prepares that mode's isolated build with the reviewed
canonical dependency lock. Repeat it when the public CLI requests configuration;
ordinary subsequent builds are incremental. `endurance-read` does not need these
profile selectors because it reads the connected firmware's canonical record.

Use `list` for current check and generator identifiers. Additional arguments
are literal string values, never shell code:

```powershell
.\scripts\device.ps1 check -Check cody-alpha -ToolArguments @('--previews')
.\scripts\device.ps1 check -Check changed-secrets -ToolArguments @('--base-ref=origin/main')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'battery', '--capture', 'battery.png')
```

The secret check with `--base-ref` includes local checkpoints without fetching;
without that option it inspects dirty/untracked files only. Menu arguments use
a JSON array of strings. CLI errors propagate to the caller; the menu retains
the failed result before offering another operation.

## Simulator and CCP

`simulator -Ccp` opens Cody Studio with its CCP panel and loopback CCP1 endpoint
at `127.0.0.1:42117`. Discovery includes it only through explicit
`include_simulator:true`; the default targets physical devices. Host network,
pairing and activity remain labelled `SIMULATED` and do not reproduce every
native onboarding gesture. Protected state survives transient cleanup in
`artifacts/cody-simulator-state/` on Windows and
`artifacts/cody-simulator-state-linux/` on Linux.

See the [simulator guide](../simulator/windows/README.md) for controls and limits
and the [CCP guide](../plugins/cody-codex-plugin/README.md) for installation/trust.

## Optional speech assets

Ordinary firmware leaves TTS disabled. `-Feature tts-sd` selects English,
Italian, Mandarin and Cantonese on microSD; `-Feature tts-flash` embeds English.
Both remain available for `full` with isolated configuration, build output and
dependencies. The Cody device panel rejects these speech-synthesis selections;
its optional Codex voice input uses `-Feature wake-word` instead.

`tts-sd` enables the diagnostic `CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN`: after
mandatory startup, it queues one fixed Italian phrase dated 5 October 2026.
`tts-flash` starts silently; the diagnostic flag can be disabled separately.
Synthesis uses core 0 and Cody/LVGL rendering uses core 1. Scenes do not request
speech automatically. The boot diagnostic requires a verified SD package,
active power and nonzero saved volume; it bypasses clock/quiet-hours policy.

The explicit `tts-assets` generator requires `-ToolArguments @('--backend', 'sd')`
or `@('--backend', 'flash')`. It writes under `artifacts/`, never prepares a
physical card or flashes firmware, and ordinary builds do not invoke it.
See [TTS](../docs/bsp/services/text-to-speech.md) for API, packages, memory and
outstanding evidence.

## Serial capture and stored logs

```powershell
.\scripts\device.ps1 capture -Port COM10 -DurationSeconds 45 -ResetBeforeCapture -OutputPath artifacts/serial/boot.log
.\scripts\device.ps1 sd-log -Port COM10 -StoredLog current
.\scripts\device.ps1 sd-log -Port COM10 -StoredLog previous
```

`-ResetBeforeCapture` explicitly requests a reset. Stored-log download does not
reset or format: it requires current firmware, an awake device and mounted SD.
Close other monitors first. The default download path is
`artifacts/serial/sd-log-<name>.log`; verified blocks precede replacement of the
local file. `-WaitSeconds` bounds each USB response.
[Storage and logging](../docs/bsp/services/storage-and-logging.md) owns the protocol.

`endurance-read` reads one CRC-checked canonical battery journal projection in
Debug or Release, independent of INFO log output and SD availability. It writes
`artifacts/serial/endurance.json` and its 128-byte `.wire` evidence. It never
resets, flashes, samples sensors or writes device storage. A recorder failure
returns a command error after retaining the verified JSON for inspection;
`source_current=false` is valid when USB powers the device. Use `-OutputPath`
for another artifact destination and `-WaitSeconds` for the response deadline.
The firmware must include the explicit reader; see
[battery runtime recording](../docs/bsp/services/power-management.md#battery-only-runtime-recording).

## PDF authoring

The documentation index links published PDF artifacts; authoring operations
remain explicit generators through this console:

```powershell
.\scripts\device.ps1 generate -Generator cody-connection-atlas
.\scripts\device.ps1 generate -Generator cody-pairing-flyer
.\scripts\device.ps1 generate -Generator cody-planning-atlas
.\scripts\device.ps1 generate -Generator cody-navigation-atlas
```

`cody-connection-atlas` authors the single CCP Communication Atlas, including
server/client ownership, pairing, protocol messages, native audio and its final
retained Security corrections. The A5 flyer remains a separate four-page guide.
Historical flow/setup/screen/security/translation generators retain reproduction
safeguards and are not the current authoring path for these documents.

Maintain the current behavior in the PDF body and keep code history in Git.
When needed, the final register preserves the dates, page and screen IDs, and
evidence for errata, addenda and security corrections. See [AGENTS.md](../AGENTS.md).
The pairing flyer remains exactly four A5 pages with current instructions;
it has no appended register or technical appendix. English conversion uses
the mappings in `assets/cody/docs/translations/`, preserves originals under
`artifacts/docs/`, skips unchanged output and rejects unexpected source edits.
It does not invent translations beyond those mappings. Documentation moves
do not revise existing PDF contents.

## Host tools and output ownership

`-PythonExecutable` chooses host Python; `-PythonExe` chooses ESP-IDF Python.
Host discovery prefers `artifacts/python/simulator/Scripts/python.exe`, then
PATH. Cody tools require Pillow and PyYAML; opening a preview does not install
them. `package-simulator` explicitly prepares/packages Luna; Cody runs from source.

| Path | Responsibility |
| --- | --- |
| `device.ps1` | Public dispatcher |
| `commands/` | Firmware, menu, packaging and banner maintenance |
| `common/` | Shared routing, console/help, artifact policy and source contracts |
| `generators/` | Authored-resource transformations |
| `checks/`, `tests/` | Explicit runners and native fixtures |

CMake may invoke internal generator/check inputs; manual operations use the
dispatcher. Bundled MenuManager has no runtime dependency on `E:\tools`.

Disposable output belongs under this checkout's `artifacts/`, including explicit
`-BuildDir`, `-LogPath`, `-OutputPath` and `-SizeOutputFile`. Relative public paths
resolve from the checkout; simple Python capture names go to `artifacts/simulator/`.
Before authorized build/flash/combo/recover, shared cleanup removes transient
logs, serial captures, reports, previews, check output and temporary files.
It preserves firmware builds, locks, board configuration/overlays, Python caches
and simulator protected state. `fullclean` affects only the selected firmware
build. Writers hold exclusive artifact locks; cleanup bounds targets and rejects
reparse points. Close active simulators/monitors before another writer.

Versioned `assets/**/generated` and downloaded `managed_components/` are inputs.
Default outputs are `artifacts/logs/<action>.log`, `artifacts/serial/capture.log`
and `artifacts/reports/size.<format>`.
