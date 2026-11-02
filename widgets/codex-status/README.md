# OpenAI Usage

Standalone Codex account-usage widget in `widgets/codex-status` within the
ESP32 Luna Shell / Cody Companion repository. The application keeps its
**OpenAI Usage** name, executable identities and private settings locations.
It runs independently of the firmware and Cody plugin; no physical device is
required.

Two native desktop clients share the same backend:

- **Windows:** C# / WinUI 3, a single compact desktop-acrylic strip and optional always-on-top.
- **Linux:** Python / GTK 4 / libadwaita, one compact native window surface. Follows the GNOME 50 design, with compatible theme colors for libadwaita 1.5+ on Ubuntu 24.04 and newer.
- **Shared:** C# / .NET 10 account reader and analytics. Both clients use the same calculations and settings schema.

The UI is compact-only: allowance percentage, today's tokens, remaining credits and a four-unit mechanical countdown. There is no full dashboard, mode switch, minimize button or maximize button. Preferences and About open in separate native windows. Windows Preferences is titled **OpenAI Usage · Preferences** and presents its form directly on a flat acrylic surface. Its owned window stays above the widget and is hidden from the taskbar and Alt+Tab. Both clients follow the system theme; Windows uses a transparent XAML surface over native desktop acrylic with a system-policy fallback, while GTK uses its native opaque background.

The shared generated icon is used by the executable, window/header, About, Start/app-menu entries and packages. Metrics use monospace typography. Text gradients use the system accent and adjust their lightness to the system theme: native WinUI gradient brushes on Windows and native Pango/Cairo snapshots on GTK. Toolbar controls show only their icons at rest.

Both interfaces are native. No HTML, JavaScript UI, WebView or WebGPU renderer is used. Windows App SDK brings three WebView2 binaries transitively; they are present in the package but are not instantiated by this app. Their disk presence is distinct from a browser runtime running in memory.

## Memory protection and diagnostics

A separate watchdog starts before the widget begins account polling. The limit is **1,000,000,000 bytes** for the widget and the children it starts; unrelated Codex sessions are excluded. Over the limit, the watchdog records the event, stops the owned group and displays a warning. It does not restart the app automatically.

- Windows uses a kill-on-close Job Object and polls aggregate peak private commitment every 500 ms. The guardian is outside the job so it can stop a frozen UI and display the notice. Unexpected guardian exit also closes the job and terminates its members.
- Linux uses a separate Python process and polls aggregate RSS through `/proc` every 500 ms. It tracks descendants and creation identities, including previously observed orphaned children; reused PIDs and the guardian are excluded. RSS can count shared pages in multiple processes, so this is a conservative measure rather than the Windows private-commit metric. Native Linux execution remains unverified here.
- The widget refuses to start without the initial watchdog handshake. The memory limit is not an antivirus scan or an installed system service.
- Errors, connection-state changes and guard events are logged locally under the state directory's `logs` folder. JSONL logs rotate at 512 KiB and keep one backup; a small `watchdog.json` heartbeat records the monitored memory and limit. Raw account payloads, credentials and conversation contents are not logged.
- The Codex executable is resolved from process/user PATH and standard installation links, including WinGet. A stale Explorer PATH no longer prevents a locally installed Codex CLI from being found.

Token quantities use decimal prefixes **K, M, G, T, P, E** (powers of 1,000). The account reader retains its history and analytics internally; the UI presents only the compact summary.

## Tray and compact widget

- The close button asks whether to exit, minimize to the notification area or cancel. Polling and memory protection continue while hidden.
- Left-click the tray icon to reopen. Right-click offers Open, About, Settings, the owner's website, the VibeWare manifesto and Exit. Website actions open the browser only when explicitly clicked.
- The widget opens directly as a compact horizontal strip with visible metric labels and inline actions. Drag the logo, values, countdown or empty data area to move it; buttons remain interactive. Windows uses native caption regions that follow layout and DPI. GTK uses a compositor move gesture that excludes buttons. There is no larger layout to switch to.
- Windows uses a native Shell notification icon and menu; no Windows Forms or browser UI is added. Toolbar glyphs retain theme-aware foregrounds even when pressed/selected. The taskbar/shortcut use an explicit app identity.
- Ubuntu uses the native StatusNotifierItem/DBusMenu protocol. The desktop must provide a compatible tray host (as supplied by Ubuntu's indicator integration). If absent, the app leaves its window accessible and disables the hide-to-tray option. No desktop extension is installed automatically. Native GNOME tray execution is not verified in this environment.

## Installers

Installer targets for **0.4.1**, generated on demand from the current source. These paths exist only until cleanup:

- `artifacts/installers/OpenAIUsage-0.4.1-windows-x64.msi`: per-user Windows x64 installation under `%LOCALAPPDATA%/Programs/OpenAI Usage`, Start menu shortcut and standard Windows uninstall/upgrade support. Includes .NET and Windows App SDK runtime files. No background service or autostart entry.
- `artifacts/installers/openai-usage_0.4.1-1_amd64.deb`: Ubuntu amd64 package with a self-contained Linux .NET backend, launcher and desktop entry. GTK/Python/system-library prerequisites are declared as APT dependencies. Targets Ubuntu 24.04+; runtime execution on Ubuntu is not verified here.

Both require a separately installed Codex CLI and your own ChatGPT sign-in. The installers contain no credentials and do not install or sign in to Codex. Local packages are unsigned. Package validation, Windows installation/startup and native GTK runtime validation are separate checks; report their results in the current task rather than retaining a historical report.

```sh
sudo apt install ./openai-usage_0.4.1-1_amd64.deb
```

On Windows, open the MSI to install, then launch **OpenAI Usage** from Start. Use Settings > Apps to uninstall; private observation history is outside the installation directory. The installer builder forces clean source rebuilds and checks the compiled version. Successful packaging keeps only the current MSI/DEB until installation or hand-off, while removing imported dependencies, staging payloads, bin/obj and Python caches. Run `Packaging/Clean-BuildOutputs.ps1` to remove all generated material, including the latest installers. No release history, antivirus report, scan hash inventory or persistent import cache is kept.

## Real data

The widget starts a private stdio `codex app-server`, initializes it and issues only account reads using the existing Codex ChatGPT sign-in. It does not read credential files, invoke models, start agent turns, send messages, redeem resets or purchase credits.

| Shared data | Source and behavior |
| --- | --- |
| Allowance and reset | `account/rateLimits/read`; windows are identified by actual duration, not primary/secondary position. Weekly Codex is preferred; a previously configured window remains honored. |
| Credit balance | Returned balance, unlimited state or unavailable. Credits are distinct from earned full-reset vouchers. |
| Token chart, daily average and previous week | `account/usage/read` daily buckets; available remotely without waiting for local collection. Missing dates are unavailable, not zero. |
| Plan | `account/read`, retained by the shared backend. |
| Quota history | Local percentage observations, isolated by hashed account identity, window and plan. The account API does not provide historical daily quota percentages. |
| Autonomy | Remaining percentage points divided by observed daily percentage-point consumption. Tokens are never assumed equivalent to quota percentage. |
| Credit value | Optional user-entered purchase price per credit and currency. No default conversion, subscription valuation or redeemable cash value. |

Account data refreshes every 60 seconds; the countdown updates every second. Requests time out, failed refreshes clear old account data, and stale/reset-pending states are visible. Authentication and schema failures show unavailable states, never sample numbers. API-key-only accounts do not expose the subscription allowance.

Forecasts use complete local calendar days within the preceding seven days, disclosing partial coverage. Gaps over three minutes, unobserved resets, decreases in used percentage or plan changes invalidate that day's quota total. Closing the widget or sleeping the computer can prevent a complete observation. There is no background service or historical quota backfill. Zero consumption has no finite exhaustion estimate. If the reset comes first, the widget reports that the current pace should reach reset.

Token buckets expose dates without a documented timezone. Labels are preserved rather than rebucketed; current local and UTC dates are conservatively excluded. The backend carries the ending date and this limitation. Service-reported tokens are not a universal remaining-token budget.

## Windows

Source-built Debug output, available only before cleanup:

```powershell
& 'E:\ESP32 Luna Shell\widgets\codex-status\artifacts\windows-x64\OpenAIUsage.Windows.exe'
```

Requires Windows x64, .NET 10 runtime and an installed Codex CLI signed in with ChatGPT. Windows App SDK components are included. Keep the output folder intact; this is an unpackaged app, not an installer. Run `codex login` yourself when needed, or set the native Codex executable path in Settings.

Settings include Codex path, optional credit purchase price/currency, reporting timezone and reduced motion. The pin button controls always-on-top. System animation preferences are respected; there is no mechanical sound.

## Linux / GNOME

Portable source-built output: `artifacts/linux`. Its backend DLL is platform-independent, not a repackaged Windows application.

Prerequisites for the framework-dependent source output: .NET 10 runtime; Codex CLI signed in with ChatGPT; Python 3; PyGObject; GTK 4.14+; libadwaita 1.5+; Pycairo; PangoCairo introspection; and a graphical Linux session. The `.deb` includes its private .NET runtime. Typical Debian-family UI package names are `python3-gi`, `python3-gi-cairo`, `python3-cairo`, `gir1.2-gtk-4.0` and `gir1.2-adw-1`. Obtain prerequisites from official sources and follow the first-import policy below. Nothing is installed automatically by repository build scripts.

```sh
sh /path/to/artifacts/linux/Linux/run.sh
```

In a source checkout, build `Backend/OpenAIUsage.Backend.csproj` using the .NET 10 SDK, then run `sh Linux/run.sh`. `OPENAI_USAGE_BACKEND` can point to another source-built backend DLL. The optional `.desktop` template assumes you have created an `openai-usage` launcher on PATH.

Native GTK rendering, GNOME theme transitions, animation and input require
separate validation on a Linux desktop. Python syntax and shared-backend checks
do not replace a GNOME smoke test.

## Build and verify

The repository's [working agreements](../../AGENTS.md) govern this widget too.
Dependency preparation, builds, tests, packaging and installation each need the
applicable explicit authorization; the commands below are instructions, not
authorization to run them. `Build.ps1` and `Packaging/Build-Installers.ps1` also
run tests, which require fresh authorization for each test or suite.

Use the pinned SDK in `global.json`, Windows SDK tooling and Python. Import preparation is an explicit separate step in the same PowerShell execution:

```powershell
Set-Location 'E:\ESP32 Luna Shell\widgets\codex-status'
$imports = & .\Prepare-Imports.ps1
& .\Build.ps1 -Imports $imports -Configuration Debug
```

Preparation downloads official dependencies and explicitly scans the imported archives and extracted files. It returns an in-memory verification object for the current build; do not serialize or export it. Build scripts validate this transient object and the unchanged staged files before using them. Builds and tests never start antivirus scans. Cleanup removes generated outputs and local dependency caches; a later build starts a fresh preparation session.

For self-contained Release installers:

```powershell
$imports = & .\Packaging\Prepare-Packaging.ps1
& .\Packaging\Build-Installers.ps1 -Imports $imports
```

The packaging workflow forces clean source rebuilds, checks the compiled version, runs behavioral checks and validates the MSI/DEB packages. WiX 5.0.2 is obtained from official NuGet as a temporary tool dependency; no global tool installation or WiX 7 license settings are changed. Success cleanup uses `-KeepDeliverables` to retain only the current MSI/DEB until installation or hand-off. A default explicit clean removes those too:

```powershell
& .\Packaging\Clean-BuildOutputs.ps1
```

No build/release history or antivirus evidence files are retained. The dependency-free C# checks cover forecast arithmetic, reset ordering, exhausted/zero/missing usage, incomplete coverage, monitoring gaps, plan changes, DST, current-day exclusion, token totals, duplicate dates, quota selection, credits, purchase valuation and account-isolated atomic persistence. Native GTK behavior still requires a GNOME smoke test.

## Local state

- Windows: `%LOCALAPPDATA%/OpenAIUsage`.
- Linux: `$XDG_STATE_HOME/openai-usage`, otherwise `~/.local/state/openai-usage`.
- Settings and up to 16 days of quota observations stay outside the repository. Account identities are hashed. Emails, credentials and conversation contents are not persisted by the widget. Remote token buckets stay in memory.
- A locked or corrupt history file is reported as unavailable rather than silently overwritten or treated as zero usage.
- Windows records unexpected application exceptions in a local `startup-error.log` for troubleshooting. It is not uploaded or placed in the repository.

## First external import

Before first introducing an external library or prebuilt binary, run one explicit targeted Microsoft Defender scan of the new archive **and extracted import**. Keep it staged until clean completion. Check the result transiently for the current preparation/build session; do not write scan reports, inventories, verification manifests or historical logs. Reuse the confirmed result only for the same unchanged staged files in that session. An unavailable, disabled, failed or unfinished scan is not clean. Never scan whole disks, installed SDK/runtime trees or generated build output as part of this workflow. Cleanup removes the temporary imports and their session state.

## Official references

- [Codex App Server account and usage methods](https://learn.chatgpt.com/docs/app-server)
- [OpenAI pricing and usage concepts](https://learn.chatgpt.com/docs/pricing)
- [WinUI unpackaged distribution](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/unpackage-winui-app)
- [GNOME 50 developer notes](https://release.gnome.org/50/developers/)
- [PyGObject GTK 4 introduction](https://pygobject.gnome.org/tutorials/gtk4/introduction.html)
- [Adwaita appearance and system preferences](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/styles-and-appearance.html)

Mockups and ImageGen prompts are design source assets. Their numbers are illustrative, not account telemetry.
