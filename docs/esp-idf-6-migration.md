# ESP-IDF 6 and ESP-Brookesia 0.8 migration

This branch moves the firmware to ESP-IDF 6.0.2 and the official
ESP-Brookesia 0.8 architecture. Earlier ESP-IDF and UI-framework targets are
not supported by this checkout.

The migration is direct: application code targets System Super, System Core,
and GUI LVGL JSON APIs only. It does not add an AI companion, OpenAI
integration, memories, avatars, private branding, or unrelated product
behavior.

## Toolchain entrypoint

Every ESP-IDF action on Windows runs through `scripts/device.ps1`. The script
selects and validates the toolchain, applies project defaults, scopes port
handling, and preserves one reproducible command surface. Its `capture` action
also owns bounded serial capture, waits for exclusive port access, and removes
stale monitor/capture processes scoped to that port. Do not bypass it.

Its default toolchain is:

| Tool | Selected version |
| --- | --- |
| ESP-IDF | 6.0.2 |
| Python | 3.13.7 |
| CMake | 4.0.3 |
| Ninja | 1.12.1 |
| Xtensa ESP32-S3 GCC | 15.2.0, Espressif build `esp-15.2.0_20251204` |
| OpenOCD manifest | `v0.12.0-esp32-20260424` |

Explicit `-IdfPath`, `-ToolsPath`, and `-PythonExe` parameters override the
defaults. The dedicated `LUNASHELL_IDF_PATH`, `LUNASHELL_IDF_TOOLS_PATH`, and
`LUNASHELL_IDF_PYTHON` environment variables are secondary overrides. The selected
versions are printed before an action starts.

Initialize the official framework source, select the target, reconfigure,
build, and inspect size with:

```powershell
git submodule update --init --recursive
pwsh -NoProfile -File .\scripts\device.ps1 set-target -BuildDir build-idf6
pwsh -NoProfile -File .\scripts\device.ps1 reconfigure -BuildDir build-idf6
pwsh -NoProfile -File .\scripts\device.ps1 build -BuildDir build-idf6
pwsh -NoProfile -File .\scripts\device.ps1 size -BuildDir build-idf6
```

Canonical configuration belongs in `sdkconfig.defaults`; generated
configuration and build directories remain untracked.

## Official ESP-Brookesia source

The complete upstream monorepo is the Git submodule
`third_party/esp-brookesia`, pinned to:

```text
5361118b9953800e4fe710e57e5314fccec9cbcf
```

The project manifests select components from that pinned tree through local
`override_path` entries. Keeping the official monorepo layout preserves the
relative component relationships used by the upstream CMake and manifest
files. No copied framework source lives under the project component tree.

The selected architecture is:

- System Super for the system lifecycle, services, keyboard, and dialogs;
- System Core for app discovery, native `IApp` lifecycle, actions, and timers;
- GUI LVGL for rendering declarative JSON application resources;
- LVGL 9 only as the GUI backend below the Brookesia interfaces.

The provider is registered as the application provider. Its UI package is
staged into the internal LittleFS image. App startup parses only
`res/root.json`, which contains the persistent background, chrome, Welcome
screen, and empty content target. Other routes are independent
`res/screen_*.json` documents loaded and mounted on demand, with the previous
route document unloaded before the next one is activated.

## Principal resolved dependencies

`dependencies.lock` uses schema 3 and records the ESP-IDF 6.0.2 dependency
graph.

| Component | Resolved version |
| --- | --- |
| `espressif/brookesia_system_super` | 0.8.1 |
| `espressif/brookesia_system_core` | 0.8.1 |
| `espressif/brookesia_gui_lvgl` | 0.8.1 |
| `espressif/brookesia_hal_adaptor` | 0.8.2 |
| `espressif/brookesia_service_helper` | 0.8.2 |
| `joltwallet/littlefs` | 1.20.4 |
| `lvgl/lvgl` | 9.5.0 |
| `espressif/esp_lvgl_adapter` | 0.6.2 |
| `espressif/network_provisioning` | 1.2.4 |
| `espressif/cjson` | 1.7.19~2 |
| `espressif/esp-boost` | 0.6.0 |
| `espressif/esp_codec_dev` | 1.5.11 |
| `espressif/usb` | 1.5.0 |

## Framework and API changes

- The native UI is a native System Core `IApp` installed by a registered app
  provider and started by System Super.
- Screen structure, layout, styling, action names, and transitions are edited
  in `components/LUNASHELL_app/gui/root.json`.
  `scripts/generate_LUNASHELL_gui_documents.py` produces the staged bootstrap and
  one lazy runtime document for each non-Welcome content screen.
- The current package contains Welcome, Logs, Hardware and Hardware Detail,
  Audio Scope, Audio FFT and Audio Detail, IMU, Settings, About, and Pairing.
  Settings exposes diagnostic cadence, IMU smoothing, SD-log mirroring,
  persisted brightness, and the explicit Wi-Fi/BLE start action.
- C++ handles lifecycle, single-active-document routing, and data binding
  through the System Core and GUI interfaces. It does not build application
  screens with direct LVGL calls.
- UI hardware telemetry comes only from `DeviceSnapshotService`. The Logs
  screen reads the canonical logger ring. Numeric sensor fields are rendered
  only when their component sample is valid and within its freshness TTL;
  failed or expired reads expose the original error or `STALE` rather than a
  zero-filled or old substitute. Full refreshes commit transactionally as one
  generation, and each asynchronous subsystem report commits a new generation.
- Full-device refresh remains explicit. The opt-in diagnostic monitor is the
  only periodic sensor sampler and reads only IMU and microphone at its
  configured cadence; mounted-SD status and radio state have their own
  polling/event paths.
- Search uses the System Super keyboard service. SD formatting uses its
  destructive message-dialog service and calls the storage abstraction only
  after explicit confirmation.
- The removed in-tree `wifi_provisioning` component was replaced by the
  official `espressif/network_provisioning` component. Includes, manager types,
  security types, callbacks, events, scheme helpers, and the provisioned-state
  query now use the `network_prov_*` API.
- `espressif/cjson` is declared by the framework dependency graph. The old
  CMake dependency on ESP-IDF's internal `json` component is gone.
- Power diagnostics use the ESP-IDF 6
  `esp_sleep_get_wakeup_causes()` bitmap API.
- Obsolete or duplicate Kconfig defaults were removed. Provisioning Security 1
  remains enabled, and the current BLE-only flow intentionally operates
  without a proof-of-possession value.
- Wi-Fi and BLE are no longer allocated during boot. The pre-UI snapshot is
  created first, and `LUNASHELL_radios_init()` runs only from Settings.
- The firmware has no ESP-IDF 5.x or Brookesia 0.6/0.7 compatibility path.
  Partial initialization is returned as failure, invalid persisted settings
  are rejected, and only genuinely absent first-boot keys receive documented,
  logged, persisted defaults.
- The Waveshare LVGL port uses two 50-line buffers in PSRAM. The boot screen is
  a minimal artwork/status frame with no live log, RichText, or shadow layer;
  it is repainted black immediately before System Super takes over.
- The I2S standard-mode, I2C master, SDMMC, ESP-NETIF SNTP, NVS, and FATFS
  implementations already use the current driver families.

The provisioning and cJSON changes follow Espressif's official
[provisioning migration guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/migration-guides/release-6.x/6.0/provisioning.html)
and
[protocol component migration guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/migration-guides/release-6.x/6.0/protocols.html).

## Storage boundary

The partition table contains an internal `littlefs_data` partition for staged
System Super and application resources. The official Brookesia HAL adaptor
mounts it at `/littlefs` and exposes its NVS key-value backend.
Mount-time formatting is disabled.

This internal volume is separate from the removable FAT SD volume at
`/sdcard`. The firmware owns the removable-card lifecycle; the HAL adaptor's
SD backend is deliberately disabled. The card stores user data and the mirrored
canonical log and is never a fallback location for application resources. The
board has
no card-detect pin, so the UI reports card/transport unconfirmed, ready, or
format-required through `DeviceSnapshotService`; an SDMMC failure is not
presented as physical absence.

Ordinary SD mounting never formats the card, and there is no format-on-mount
configuration. A format attempt is permitted only after FatFs reports
exactly `FR_NO_FILESYSTEM` and the user explicitly accepts the destructive
System Super confirmation. That single-use authorization is bound to every
observed CID field plus capacity and is consumed by the immediate attempt,
including a card mismatch, missing card, transport failure, or no-longer-needed
format. Mounted-card status is polled; failure invalidates and unmounts the
volume before transient retries resume. A verified `format-required` state
pauses probes until explicit authorization. Internal, destructive, poisoned,
or incompletely cleaned failures stop retries for the current boot and expose
their cleanup stage and error in the UI.

## Validation boundary

The host-side acceptance sequence is:

1. clean target selection and reconfiguration;
2. one complete firmware build;
3. a second incremental build;
4. the project size report;
5. JSON syntax, bootstrap inventory, lazy-document ownership, and asset
   reference validation for the staged application package;
6. the BLE provisioning/System Core verifier, including the one-active-route
   load/mount/unload contract;
7. syntax and hidden-window smoke checks for the Windows simulator;
8. its existing executable packaging flow;
9. `git diff --check` and a scan for stale framework references.

Run all ESP-IDF items in that sequence through `scripts/device.ps1`. Exact
artifact sizes and hashes belong in the final branch handoff because later
source edits can change them.

This migration document makes no physical-device claim. Flash, serial monitor,
erase, recovery, COM-port access, display, touch, BLE provisioning, Wi-Fi, TLS
endpoints, audio, sleep/wake behavior, GPS, and removable-SD operation remain
checks for a later hardware session.
The dated `COM12` observations in the hardware inventory describe an earlier
firmware/hardware session only and are not validation evidence for this commit.
