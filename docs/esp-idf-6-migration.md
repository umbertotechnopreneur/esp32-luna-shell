# ESP-IDF 6 migration

This branch moves the upstream firmware from ESP-IDF 5.5.4 to ESP-IDF 6.0.2.
It is an infrastructure migration only: it does not add an AI companion,
OpenAI integration, memories, avatars, private branding, or product behavior.
ESP-IDF 5.5.4 is no longer a supported build target for this checkout.

## Toolchain

The Windows entrypoint is `scripts/device.ps1`. Its default toolchain is:

| Tool | Version used for validation |
| --- | --- |
| ESP-IDF | 6.0.2 |
| Python | 3.13.7 |
| CMake | 4.0.3 |
| Ninja | 1.12.1 |
| Xtensa ESP32-S3 GCC | 15.2.0, Espressif build `esp-15.2.0_20251204` |
| OpenOCD manifest | `v0.12.0-esp32-20260424` |

The script obtains the remaining executable paths from the selected ESP-IDF
installation instead of duplicating tool versions. Explicit `-IdfPath`,
`-ToolsPath`, and `-PythonExe` parameters override the defaults; the dedicated
`QSEE_IDF_PATH`, `QSEE_IDF_TOOLS_PATH`, and `QSEE_IDF_PYTHON` environment
variables are the secondary override. The selected versions are printed before
an action starts.

Build, reconfigure, and inspect size without loading a PowerShell profile:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 reconfigure -BuildDir build-idf6
pwsh -NoProfile -File .\scripts\device.ps1 build -BuildDir build-idf6
pwsh -NoProfile -File .\scripts\device.ps1 size -BuildDir build-idf6
```

## Framework and API changes

- The removed in-tree `wifi_provisioning` component was replaced by the
  official `espressif/network_provisioning` component. Includes, manager types,
  security types, callbacks, events, scheme helpers, and the provisioned-state
  query now use the `network_prov_*` API.
- `espressif/cjson` is declared by the Brookesia component manifest. The old
  CMake dependency on ESP-IDF's internal `json` component was removed.
- Power diagnostics use the ESP-IDF 6
  `esp_sleep_get_wakeup_causes()` bitmap API instead of the deprecated
  single-cause API.
- Obsolete or duplicate Kconfig defaults were removed. Provisioning Security 1
  is explicitly enabled; the existing flow intentionally operates without a
  proof-of-possession value.
- The existing I2S standard-mode, I2C master, SDMMC, ESP-NETIF SNTP, NVS, and
  FATFS implementations already use the current driver families and were not
  rewritten.
- The generated dependency lock uses schema 3 and records ESP-IDF 6.0.2.

The provisioning and cJSON changes follow Espressif's official
[provisioning migration guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/migration-guides/release-6.x/6.0/provisioning.html)
and [protocol component migration guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/migration-guides/release-6.x/6.0/protocols.html).

## Principal resolved dependencies

| Component | Resolved version |
| --- | --- |
| `espressif/network_provisioning` | 1.2.4 |
| `espressif/cjson` | 1.7.19~2 |
| `espressif/esp-boost` | 0.6.0 |
| `espressif/esp_lvgl_adapter` | 0.6.2 |
| `espressif/esp_codec_dev` | 1.5.11 |
| `lvgl/lvgl` | 9.4.0 |
| `espressif/usb` | 1.5.0 |

Both local Brookesia manifests now allow `esp-boost 0.6.*`. That removes the
resolver conflict with the future Brookesia 0.8 dependency line while leaving
the AI agent out of this firmware.

## Brookesia boundary

The current Phone UI remains on the vendored `brookesia_core 0.6.0-beta2`.
Espressif's published
[compatibility matrix](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html)
does not claim IDF 6 support for that historical line, so its status here is
based on this repository's successful compile, not on upstream support.
Replacing the Phone framework with Brookesia 0.8 is a separate product
migration.

A separate dependency-resolution probe with ESP-IDF 6.0.2 confirmed that the
Brookesia 0.8 agent chain selects `esp-boost 0.6.*`; the former `0.3.*` pin
blocked it. The current Registry archives for `brookesia_service_helper`
0.8.1/0.8.2 also contain a monorepo-relative CMake include that is absent from
the standalone package. That upstream packaging problem prevents treating a
future agent configure as validated here and is intentionally not worked
around in the product.

`esp-boost 0.6.0` leaves optional Thread, Chrono, and System libraries disabled
by default. The present feature set compiles without them; a later Brookesia
0.8 integration must enable only the Boost libraries its selected services
actually use.

The disabled legacy AI framework inside the vendored core still contains old
peripheral code. It is not compiled by the current configuration. Enabling it
would require a separate IDF 6 port; it must not be enabled as a shortcut to
the Brookesia 0.8 agent.

## Validation and limits

The ESP-IDF 5.5.4 baseline reconfigured but did not produce firmware because
two tracked source files already contained unresolved conflict markers. The
migration removed those markers by retaining the equivalent intended code;
the baseline failure was not an IDF compatibility finding.

The IDF 6 validation consists of a clean reconfigure, a full firmware build, a
second incremental build, an ESP-IDF size report, syntax and hidden-window
smoke tests for the Windows simulator, and its existing executable packaging
flow. Exact artifact sizes and hashes belong in the branch validation report
because they may change with later source edits.

No flash, serial monitor, erase, recovery, COM-port access, or physical-device
operation is part of this validation. Display, touch, BLE provisioning, Wi-Fi,
TLS endpoints, audio, sleep/wake behavior, GPS, and SD operation therefore
remain runtime checks for a later hardware session.

The SD contract is unchanged: ordinary mounting never formats the card,
`CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL=n` remains set, and formatting requires an
explicit user action and confirmation.
