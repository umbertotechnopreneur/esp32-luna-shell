# ESP32 Luna Shell / Cody Companion

Firmware for the Waveshare ESP32-S3 Touch AMOLED 1.75: a 466 x 466 circular
display, touch, 16 MB flash and 8 MB PSRAM. Two explicit profiles are supported
on ESP-IDF 6.0.2:

- **Cody Companion** (default): standalone character renderer, snapshot-driven
  motion, pearl jelly, touch response and first-setup pairing. No LVGL dependency.
- **Luna Shell**: full diagnostic UI using ESP-Brookesia 0.8 System Core and
  declarative GUI documents. Dependencies are admitted by the selected profile.

There are no legacy framework compatibility paths. English is the only supported
Cody language; its fonts contain bounded printable ASCII.

## Start here

- [Documentation map](docs/README.md): architecture, hardware and screen atlases.
- [Development console](scripts/README.md): commands, outputs, locks and serial logs.
- [Cody architecture](docs/architecture/cody/README.md): runtime, onboarding,
  rendering and clock contracts.
- [Engineering rules](AGENTS.md): approval boundaries and implementation invariants.
- [Active work](TODO.md), [deferred work](planned-todo.md), [completed work](done.md)
  and [recurring lessons](lessons.md).

## Git starting points

The annotated local tags below freeze the existing variant branches as source
starting points. They do not establish new build or device validation.

| Tag | Source commit | Existing variant branch |
| --- | --- | --- |
| `base-display-touch` | `3283a194` | `codex/base-display-touch` |
| `luna-shell` | `369e5026` | `codex/luna-shell` |
| `cody` | `9d2fe8c5` | `codex/cody`, also `main` when tagged |

Onboarding work starts on `codex/cody-onboarding` from `cody`. Keep the tags
fixed; make focused checkpoint commits on the feature branch and integrate
reviewed work into `main`. Creating these local refs does not publish them;
push requires separate authorization. The older variant branches are historical
source baselines, not alternative SDK compatibility targets.

## Development

Use PowerShell from this checkout. `scripts/device.ps1` is the sole public entry
point for firmware, simulators, generators and checks. Opening its menu, `help`
or `list` does not start those workflows or access hardware.

```powershell
.\scripts\device.ps1 help
.\scripts\device.ps1 list
.\scripts\device.ps1
```

Firmware commands default to `Debug` and the `cody-companion` profile. Select
Release explicitly on every corresponding build/flash command. Limit builds to
eight jobs. Examples below are operations to request, not automatic prerequisites:

```powershell
.\scripts\device.ps1 build -Jobs 8
.\scripts\device.ps1 build -BuildMode Release -Jobs 8
.\scripts\device.ps1 flash -Port COM10 -BuildMode Release -Jobs 8
.\scripts\device.ps1 simulator
.\scripts\device.ps1 check -Check cody-native
```

Confirm the actual serial port before an authorized device operation. Build,
test, simulator and hardware workflows require the approvals described in
`AGENTS.md`; implementing a change does not implicitly authorize validation.
Do not invoke ESP-IDF's Python entry point directly.

## Runtime contracts

- Sensors and telemetry have one acquisition owner: `DeviceSnapshotService`.
  Consumers use its validity, real errors and original timestamps; they never
  read sensors directly or fabricate fresh values.
- Errors retain their original identity. Optional orchestration may continue
  only with the failure still visible in structured state.
- Missing settings receive logged first-boot defaults. Corrupt or failed reads
  are errors, not permission to reset, clamp or migrate settings.
- Internal NVS/LittleFS and removable SD have separate ownership. SD formatting
  requires explicit, single-use authorization; it is never an automatic repair.
- Display, clipping and touch share one logical coordinate system through all
  four rotations. Read the [circular layout contract](docs/architecture/circular-display-layout.md)
  before changing geometry.

Cody's cold boot uses Sleeping, Waking and Ready before routing to first-setup
greeting or Idle. The first-setup flow is 04 Hello → 05 Let's pair → 06 installation
QR → 07 pairing with the session QR → 08 failure or 09 online. Software standby
retains the current scene; a deep-sleep reset does not retain RAM route state.
See the Cody guides for exact timings, audio policy and backend success gates.

Continued setup currently follows 09 Online -> 10 Make it yours -> 36 Settings
-> 11 skin selection -> 12 Codex pairing -> 13 setup confirmed -> 16 Idle for
ten active seconds -> 14 paired-PC search -> 15 CCP ready. Screen 10 lists
preference categories; interactive preference editing remains deferred.
Screen 11 already selects and persists a named skin, rather than a flat color.

[Cody's Codex Plugin](plugins/cody-codex-plugin/README.md) supplies the PC-side
discovery, display-code pairing and authenticated liveness tools. Screen 15
confirms the paired plugin's liveness. Chat selection, messages and Codex task
events require the separate session/event transport recorded in
[deferred work](planned-todo.md). The Wi-Fi connection alone establishes none
of those outcomes. See [onboarding](docs/architecture/cody/onboarding.md) for
the implemented confirmation gates and outstanding physical validation.

## Source ownership

| Location | Responsibility |
| --- | --- |
| `main/` | Startup and selected-profile composition |
| [CODY_companion](components/CODY_companion/README.md) | Character runtime, scenes, model, rendering and diagnostics |
| [CODY2D](components/CODY2D/README.md) | Standalone 2D primitives, paths, text and controls |
| [LUNASHELL_platform](components/LUNASHELL_platform/README.md) | Snapshots, display, power, audio, connectivity and storage owners |
| [LUNASHELL_app](components/LUNASHELL_app/README.md) | Luna documents, navigation, chrome and shell |
| [LUNASHELL_boot](components/LUNASHELL_boot/README.md) | Boot UI and sound |
| [LUNASHELL_logger](components/LUNASHELL_logger/README.md) | Persistent logging and USB download |
| [LUNASHELL_tof400f](components/LUNASHELL_tof400f/README.md) | Distance sensor transport and protocol |
| `assets/`, `config/`, component `gui/` | Authored resources, configuration and generated firmware inputs |
| `scripts/` | Workflow implementations, generators and explicit checks |

Owned component implementations live under `src/`, grouped by responsibility.
Public headers remain in `include/`; component IDs, manifests and configuration
stay at the component root. Each implementation folder has a short ownership
README. Vendor, BSP and managed dependencies are not reorganized as owned code.

Disposable output belongs in `artifacts/`. Build/flash clean transient outputs
but retain the selected firmware build, configuration and host caches. Versioned
asset outputs are source inputs, not disposable build artifacts.

Implementation, host validation and physical-device verification are separate
claims. Outstanding evidence and release gates belong in `TODO.md`, not in
historical command transcripts repeated throughout documentation.
