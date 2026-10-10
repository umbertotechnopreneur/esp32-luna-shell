# Development console

Use `scripts/device.ps1` from PowerShell at the repository root for firmware,
simulators, generators and checks. Menu, `help` and `list` are inert: they do
not load ESP-IDF, start a simulator or access serial hardware until an operation
is selected. Execution authorization is defined in [AGENTS.md](../AGENTS.md).

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
.\scripts\device.ps1 clean-artifacts
```

These are command examples, not permission to execute them. Each invocation
defaults to `Debug` and `cody-companion`. Select `-BuildMode Release` on each
corresponding command; the menu keeps a selection for its session.
`-BoardConfig` owns its profile and cannot combine with `-Profile`/`-Feature`.
Read [toolchain and profiles](../docs/bsp/toolchain-and-profiles.md) for composition,
configuration guards and dependency-lock review. Confirm device identity and
port before authorized serial operations.

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
native onboarding gesture. Protected state in `artifacts/cody-simulator-state/`
survives transient cleanup.

See the [simulator guide](../simulator/windows/README.md) for controls and limits
and the [CCP guide](../plugins/cody-codex-plugin/README.md) for installation/trust.

## Optional speech assets

Ordinary firmware leaves TTS disabled. `-Feature tts-sd` selects English,
Italian, Mandarin and Cantonese on microSD; `-Feature tts-flash` embeds English.
Both apply to `cody-companion` and `full` with isolated configuration, build
output and dependencies. The menu exposes the same selection.

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

## PDF authoring

The documentation index links published PDF artifacts; authoring operations
remain explicit generators through this console:

```powershell
.\scripts\device.ps1 generate -Generator cody-connection-atlas
.\scripts\device.ps1 generate -Generator cody-pairing-flyer
.\scripts\device.ps1 generate -Generator cody-security-atlases
.\scripts\device.ps1 generate -Generator cody-pdf-english
```

PDF revisions preserve original dates, page/screen IDs and evidence, and use
the shared final revision register required by [AGENTS.md](../AGENTS.md).
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
