# Toolchain, boards and firmware profiles

The supported SDK is ESP-IDF 6.0.2. Luna selects official ESP-Brookesia 0.8
System Super/Core and GUI LVGL packages. Native display/touch and Cody do not
admit LVGL or GUI adapters. No earlier-SDK or removed-framework compatibility
path is part of this checkout.

## Configuration ownership

| Input | Responsibility |
| --- | --- |
| `config/sdkconfig/v1/catalog.json` | Legal composition of base, board, product, profile, feature and mode layers |
| `config/boards/` | Current board YAML and supported service/driver choices |
| `config/dependency-locks/v1/` | Reviewed dependency graph for each legal variant |
| `scripts/common/board_config.py` | Validated module-to-component roots before Kconfig |
| `scripts/device.ps1` | Public configuration, build, size, generators, checks and hardware operations |

Each variant has isolated generated sdkconfig and build state under `artifacts/`.
Normal dependency resolution must agree with the reviewed canonical lock.
Changing board or service composition requires explicit configuration; an absent
lock requires the public update-lock workflow, review and a focused checkpoint.
The YAML hash belongs to the fingerprint and cannot silently reuse prior state.

## Board and interface

The implemented physical adapter is Waveshare ESP32-S3 Touch AMOLED 1.75.
Its display/touch baseline selects neither Luna nor optional sensors/radios.
`LUNASHELL_BUILD_ENABLE_DISPLAY` and `LUNASHELL_BUILD_ENABLE_TOUCH` select devices;
`LUNASHELL_BUILD_ENABLE_UI` selects Luna. Touch requires display and the shared
logical orientation. Headless selects neither. Profiles admit component roots
before Kconfig and reject inconsistent choices.

The YAML service modes are `service` and `off`. Battery requires PMU; speaker
and microphone routes remain independent. A service need not create a UI page.
Another required component may still admit a dependency transitively when a
particular board module is off. Driver exclusion and electrical shutdown are
different facts. Luna's UI-minimal composition is in
[configuration](../luna-shell/configuration.md).

Future Espressif boards need explicit adapters, pinouts, capabilities and locks.
Changing the board name alone is rejected. Selected interfaces consume the board
capabilities; they do not define the physical-device identity.

## Selected toolchain and framework

The wrapper owns `-IdfPath`, `-ToolsPath`, `-PythonExe` and the corresponding
`LUNASHELL_IDF_PATH`, `LUNASHELL_IDF_TOOLS_PATH`, `LUNASHELL_IDF_PYTHON` overrides.
It prints the actual selected versions before an operation. The dated
[workstation inventory](workstation-inventory.md) is historical host context.

The official upstream framework is the `third_party/esp-brookesia` Git submodule.
Project manifests use local `override_path` entries into its official layout;
the checked-in gitlink and per-variant locks define exact revisions. Brookesia's
application/provider/package responsibilities are in
[Luna Shell](../luna-shell/README.md). Physical/network drivers use ESP-IDF 6 APIs,
including `network_prov_*` and the wake-cause bitmap API.

## Debug and Release

Every standalone firmware CLI invocation defaults to Debug. Release requires
literal `-BuildMode Release` on that invocation; old binaries, directories,
environment variables and previous commands do not select it. The menu keeps
an explicit Session settings selection only for that menu session.
Usage and all commands are in the [console guide](../../scripts/README.md).

The versioned catalog declares mode defaults, applied after profile and board
layers. Generated sdkconfigs and default build directories receive `-debug`
or `-release` suffixes, for example:

| Mode | Generated configuration | Build directory |
| --- | --- | --- |
| Debug | `artifacts/sdkconfig/v1/cody-companion-debug` | `artifacts/build-cody-companion-debug` |
| Release | `artifacts/sdkconfig/v1/cody-companion-release` | `artifacts/build-cody-companion-release` |

The hardware component graph and canonical dependency lock are shared between
modes of the same profile. The working lock, binaries, staged UI assets, CMake
state and generated configuration stay separate. Old unsuffixed artifacts are
neither migrated nor selected automatically. Use a dedicated explicit
`-BuildDir` for each mode when overriding the default.

The defaults fingerprint includes the mode and its defaults file. Before build,
flash or size, the engine checks the generated mode, logging levels, sleep
instrumentation and disabled USB SOF hook. A missing/mismatched configuration
fails with a `set-target` command preserving the requested mode. CMake also
rejects a command/configuration mode mismatch; removing `-BuildMode Release`
cannot accidentally reuse a Release build.

### Mode behavior

| Area | Debug | Release |
| --- | --- | --- |
| Application logging | INFO default, compiled maximum VERBOSE | WARN default and compiled maximum WARN |
| Bootloader logging | INFO | WARN |
| Boot identity | Debug notice | One Release notice warning about the USB sleep limitation |
| Automatic Light-sleep | Disabled for USB debugging | Enabled when power management is present and workloads permit it |
| Inactivity sleep | CPU/USB remain awake in software standby | Cody always uses software standby for PWR wake; other profiles retain software standby for Codex or voice listening, otherwise Deep-sleep with touch wake |
| Screen inactivity and fade | Runtime `idle_sleep_s`, default 60 s; 800-ms fade | Same timeout and fades |
| Standby motion observation | 10-Hz host snapshots | Software observation in Cody/retained-service standby; accelerometer-only hardware WoM without host polling during Deep-sleep |
| CPU frequency | Existing 80--240 MHz dynamic policy | Same 80--240 MHz policy |
| Sleep residency diagnostics | Callback counters available | Debug-only callback instrumentation disabled |
| Repeated post-wake trace reports | Available | Disabled; retained failure trace remains available |
| Cody frame diagnostics | Authored execution timings, frame distributions and FPS/CPU HUD | No frame-distribution/raster profiling collection; explicitly opened diagnostic screens still show the canonical readout |
| Cody device diagnostics | Character with canonical CPU/free RAM/PSRAM, FPS, IP and SD readout, plus local Memory details | The same overview, Memory page and Back; no build-mode choice is required |
| Cody PWR | Short click enters/wakes standby; valid continuous 3-second hold requests farewell and PMU shutdown | Same dedicated power controls; no PWR screenshot or software reset |
| BOOT software reset | 3-second BOOT hold | Excluded; ordinary BOOT controls remain |
| Cody static/parked worker | Existing 50 ms/200 ms waits; paused HUD refresh | 1000 ms functional gate checks; touch IRQ wakes immediately; an explicit diagnostic view retains live refresh |
| Active animation | Existing selected 40 ms or 33 ms target | Same target |
| Compiler optimization | Performance optimization (`-O2`); existing activity/control-plane size policy remains | Same optimization for comparable rendering behavior |

Both modes keep the same OTA/storage geometry, compiler policy and task budgets.
Debug preserves the VERBOSE log ceiling, assertions and UART core dumps. Sprite
storage omits only exactly transparent pixels outside each immutable bounding
rectangle; logical geometry, full reference assets and rendered pixels remain.
Existing activity/control-plane `-Os` overrides apply in both modes. Build-mode
configuration checks reject a stale optimization selection.

Release keeps real errors, canonical snapshot health/validity/timestamps,
frame ownership/deadline checks, watchdogs, persistence semantics, requested
active sensor cadences and structured input errors. Driver-owned
frame safety and published measurements are still present. User-requested
diagnostic services are functional features and are not silently disabled.
Performance Mode restores the compiled log default when it ends, so it cannot
turn Release INFO logging back on.

The battery-endurance NVS journal remains active in Release. Its periodic
`Endurance` readback messages are INFO and are suppressed by the compiled WARN
maximum; passive serial capture alone cannot retrieve those records from a
Release image. Use `scripts/device.ps1 endurance-read -Port COM10` for an explicit
CRC-checked canonical readback without raising normal logging. The command
preserves recorder failures and unknown termination; it does not refresh sensors
or write device storage. See [battery-only runtime recording](services/power-management.md#battery-only-runtime-recording)
for the persistence contract and readback procedure.

Cody keeps software standby in both modes so the PMU PWR key can wake it; that
key has no ESP Deep-sleep wake route. Other touch-enabled Release profiles enter
Deep-sleep after confirmed standby only when the power owner retains neither
the Codex radio association nor wake-word standby listening. Those services keep
software standby without attempting sleep and restoring Active due to an expected
policy rejection. During Deep-sleep, touch GPIO11 and accelerometer-only IMU
INT2/GPIO21 provide physical wake; PWR polling stops and the normal inactivity
timer adds no periodic wake.
Debug keeps CPU/USB awake and retains 10-Hz software motion observation.
See [power lifecycle](services/power-management.md) for runtime settings,
IRQ/revision checks, rollback, startup and shared-rail limits. Whole-board
current and battery life remain unmeasured. Native USB Serial/JTAG disconnects
during Deep-sleep. `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` stays disabled in both modes:
the known ESP-IDF 6.0.2 SOF-hook cache-error problem has not been repaired by
this change. Wake/reconnect may be necessary before a later explicitly
authorized serial/flash operation.

## Artifacts and evidence

Each actual public build captures the compiling computer's local date and time
(`dd/MM/yy HH:mm`), its exact operating-system timezone ID and its local hostname.
`FirmwareBuildMetadata` stores this bounded identity as immutable firmware data;
it does not read device clock settings or sensors. The build writes its generated
header and `firmware-build-metadata.json` audit record under the selected build's
`generated/` directory. Configuration, size reports and serial capture do not
replace the stamp. Flash first builds, so its embedded stamp belongs to that build.

Timezone and hostname fields accept at most 63 printable ASCII characters.
Unavailable or unsupported values retain their explicit state for the display;
they are never truncated, transliterated or replaced with a guessed timezone.
Acquisition and write errors stop compilation. The public `build-modes` check uses
synthetic inputs to cover local-date rollover, bounded identity, literal escaping
and capture renewal without probing the current computer.

Before authorized build/flash/combo/recover, the shared helper cleans transient
artifacts while preserving firmware, reviewed locks, generated configuration,
Python caches and simulator pairing state. Fullclean affects only the selected
build; managed/versioned inputs are not runtime cleanup targets.

Clean/incremental builds, size, simulator checks, source/dependency checks and
physical operations follow [AGENTS.md](../../AGENTS.md). Their actual commands,
dates, profiles and outcomes belong in [done.md](../../done.md); current gates
belong in [TODO.md](../../TODO.md). A successful build does not prove boot,
display/touch, service behavior or current consumption on a physical device.
