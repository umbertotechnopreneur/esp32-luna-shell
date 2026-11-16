# Desktop simulators

Cody's desktop tools explore the 466 x 466 circular interface on Windows and
Linux. **SIMULATED** and **SYNTHETIC** identify host inputs and previews, never
physical device observations. The directory name `windows/` also contains the
cross-platform Cody code; Luna packaging remains a separate Windows workflow.

## Choose the right mode

Run commands from the repository root using [the public console](../../../scripts/README.md).
Execution follows [AGENTS.md](../../../AGENTS.md).

| Task | Public command | Behavior and guide |
| --- | --- | --- |
| Explore the firmware UI interactively | `pwsh -NoProfile -File ./scripts/device.ps1 simulator` | Prepares the native device runtime and opens [live native frames](native.md). |
| Inspect a particular screen with controlled inputs | `pwsh -NoProfile -File ./scripts/device.ps1 simulator -StudioCase P04` | Opens [controlled Studio](studio.md); requires a current runtime, with no implicit rebuild. |
| Capture a controlled screen without a window | `pwsh -NoProfile -File ./scripts/device.ps1 simulator -StudioCase P04 -StudioCapture` | Saves native pixels and capture identity under `artifacts/simulator/studio/`. |
| Exercise the simulated CCP endpoint | `pwsh -NoProfile -File ./scripts/device.ps1 simulator -Ccp` | Opens the [legacy CCP preview](preview.md#connect-the-simulator-to-ccp) with real loopback transport. |
| Explore artwork or materials | `pwsh -NoProfile -File ./scripts/device.ps1 simulator -Screen greeting` | Opens the [artwork preview](preview.md), with its own controls. |
| Explore Luna diagnostic pages | `pwsh -NoProfile -File ./scripts/device.ps1 simulator-luna` | Opens the separate [Luna preview](preview.md#luna-and-leg-previews). |

`-StudioCase`, `-StudioScenario` and `-StudioCapture` select controlled Studio.
`-Screen`, `-Rotation`, `-AtMs`, `-SafeGuide`, `-OutputPath` and `-Ccp` select the
legacy preview route; they must not be mixed with Studio flags. Use
`-DeviceScenario` for a live native scenario. These scenario formats are
[not interchangeable](architecture.md#scenario-formats).

## Prerequisites

Cody Studio runs from source on Windows and Linux with PowerShell 7, a native C++20
compiler, Python with Tk and Pillow ImageTk, and PyYAML. The native runtime also
requires the official `espressif/qrcode` source dependency, version `0.1.0~2`,
restored under `cody-device/managed_components/espressif__qrcode` by the project's
ESP-IDF dependency setup. Its manifest and component hash are already pinned in
the companion's versioned dependency files; managed sources and built libraries
are not committed. A source-only export needs this prerequisite before running
the graphics generator. The legacy illustrated QR previews use optional
ReportLab tooling; when it is absent those previews show QR unavailable.
On Ubuntu, the host packages include `g++`, `python3-tk`, `python3-pil.imagetk`
and `python3-yaml`. Use a graphical
session for the interactive window. The native Boot replay uses ALSA `libasound`
on Linux or WinMM on Windows; the legacy preview uses `aplay` from `alsa-utils`
on Linux. The live native command prepares the device runtime before launch. Explicit
legacy interactive previews prepare their graphics and host audio drivers;
controlled Studio requires a separately prepared current device runtime.
Static captures do not start audio.

Prepare the controlled Studio runtime explicitly before first use or after a
source change, with authorization for the generator command:

```powershell
pwsh -NoProfile -File ./scripts/device.ps1 generate -Generator cody-simulator-graphics -ToolArguments '--device-runtime'
```

## Find the details

| Guide | Owns |
| --- | --- |
| [Live native device](native.md) | First boot, synthetic Wi-Fi, input keys, snapshot editor and F12 captures |
| [Controlled Studio](studio.md) | Catalog, scenarios, timeline, audio audition, baselines, comparisons and atlas export |
| [Preview tools](preview.md) | Legacy CCP/artwork controls, Muse, material studies and Luna |
| [Architecture](architecture.md) | Python/C++ ownership, source map, scenario formats and artifact lifetimes |
| [Shared contracts and checks](checks.md) | Native bindings, Luna fixtures, HTTP simulation and verification commands |

[done.md](../../../done.md) holds the latest validation summary;
[TODO.md](../../../TODO.md) holds open work and acceptance gates. A host capture
or successful test does not establish physical display, audio, power or timing
behavior. Generated artifacts are retained; cleanup is manual and requires an
explicit request.

---

## This is VibeWare

[Manifesto](https://umbertogiacobbi.biz/vibeware/manifesto?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=simulator-manifesto) ·
[Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) ·
[VibeWare on GitHub](https://github.com/umbertotechnopreneur/VibeWare) ·
[Umberto Giacobbi](https://umbertogiacobbi.biz/?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=simulator-author) ·
[Terms](https://umbertogiacobbi.biz/terms/?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=simulator-terms) ·
[Privacy](https://umbertogiacobbi.biz/privacy/?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=simulator-privacy)

<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">
