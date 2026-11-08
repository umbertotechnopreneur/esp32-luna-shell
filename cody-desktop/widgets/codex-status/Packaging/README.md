# Local installers

Source/package target: **0.4.1**. Architecture: Windows x64 / Ubuntu amd64. Packages are generated on demand and removed by an explicit clean.

## Windows

Open `../artifacts/installers/OpenAIUsage-0.4.1-windows-x64.msi`, then launch **OpenAI Usage** from Start.

- Per-user installation in `%LOCALAPPDATA%/Programs/OpenAI Usage`.
- Includes the app's private .NET runtime and Windows App SDK components.
- Uses standard Windows Installer progress, Add/Remove Programs and major-upgrade handling.
- Does not add autostart or a background service. It does not install or sign in to Codex.
- Private settings/history remain in `%LOCALAPPDATA%/OpenAIUsage` after uninstall.
- **OpenAI Usage · Preferences** is a flat native acrylic form in a window owned by the widget. It stays above its owner and is excluded from the taskbar and Alt+Tab; the form is rendered directly on the window surface.

## Ubuntu

For Ubuntu 24.04 or newer on amd64, install from the directory containing the `.deb`:

```sh
sudo apt install ./openai-usage_0.4.1-1_amd64.deb
```

Launch **OpenAI Usage** from the application menu or run `openai-usage`.

- The package includes a source-built self-contained Linux backend under `/usr/lib/openai-usage`.
- APT resolves declared GTK 4.14+, libadwaita 1.5+, Python GI/Cairo and system-library dependencies from configured repositories.
- Install the Codex CLI separately from its official distribution and run `codex login` yourself.
- Remove with `sudo apt remove openai-usage`. Personal state under `~/.local/state/openai-usage` is preserved.
- Native Ubuntu installation/runtime testing has not been performed in this Windows environment.

## Rebuild

From the project root in PowerShell:

```powershell
$imports = & .\Packaging\Prepare-Packaging.ps1
& .\Packaging\Build-Installers.ps1 -Imports $imports
```

Preparation downloads official NuGet runtime/tool imports and explicitly scans their archives and extracted files with Defender before use. It returns a verification object kept only in the current PowerShell execution. Pass it directly to the builder; never export it to disk. No scan report, antivirus history, hash inventory or persistent verification manifest is written.

Building verifies the transient import state, forces **clean source rebuilds**, checks the compiled application version, runs behavioral checks and validates both packages. Builds and installer execution do not start antivirus scans. Successful packaging removes temporary imported dependencies, staging payloads, bin/obj and Python caches, retaining only the current MSI/DEB until installation or hand-off. It does not keep a release history or a dependency cache for later runs. No old application binary is reused.

To remove everything generated, including the current installer pair:

```powershell
& .\Packaging\Clean-BuildOutputs.ps1
```

Use `-WhatIf` to inspect that cleanup first. The builder uses `-KeepDeliverables` only while the current packages are needed for installation or hand-off. A later build requires a fresh explicit preparation session.

The WiX compiler is staged temporarily as version 5.0.2; no global compiler installation or WiX 7 license settings are modified. Windows installers use deterministic component identities and a stable upgrade code. Debian packages use the documented ar/ustar format and include file checksums, dependency metadata and Unix file modes.

The packages are local unsigned builds. Report package creation, Windows installation and native Ubuntu execution separately in the current task; do not retain historical validation or scan files. No native Ubuntu installation is claimed here.
