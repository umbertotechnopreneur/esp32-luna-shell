# BSP and shared services

This area documents the physical-device boundary shared by Cody and Luna Shell.
The minimum graphical baseline is display plus touch, with the required buses,
memory and internal state. It can run without LVGL, Brookesia GUI, radios,
sensors, audio, removable SD or an application-specific screen.

## Separate the selections

| Selection | Owns |
| --- | --- |
| Board | Espressif target, pinout, display/touch drivers, buses, memory and supported capabilities |
| Interface | Standalone display/touch, native Cody, or Luna Shell with LVGL/Brookesia |
| Optional services | Power/battery, sensors, clock, audio, network and removable storage |
| Build mode | Debug or explicitly selected Release |

The [toolchain and profiles](toolchain-and-profiles.md) describe actual supported
combinations. A service may be admitted without adding a UI page. An interface
must consume a published capability or error rather than assume a particular chip.

## Core contracts

| Guide | Responsibility |
| --- | --- |
| [Display and touch](core/display-touch-runtime.md) | Mandatory startup, orientation, frames and transfer ownership |
| [Hardware API](core/hardware-api.md) | Acquisition/service ownership, bounded operations and failure semantics |
| [Canonical snapshot](core/device-snapshot.md) | Published state, validity, original timestamps and consumer projections |

The common contracts must distinguish board capabilities from product policy.
The current 466 x 466 circle, CO5300 and CST9217 are board-specific details.
Cody controls live in [Cody layout](../cody/layout.md); LVGL layout lives in
[Luna Shell](../luna-shell/README.md).

## Optional services

| Service | Guide |
| --- | --- |
| Storage and logging | [Storage ownership and USB readback](services/storage-and-logging.md) |
| Power | [Acquisition, standby and wake](services/power-management.md) |
| Time | [Trusted clock, preferences and RTC](services/time.md) |
| Network setup | [Wi-Fi/BLE provisioning](services/connectivity-pairing.md) |
| HTTP diagnostics | [Read-only API](services/http-device-api.md) |
| Wake word | [Explicit recognition feature](services/wake-on-word.md) |
| TTS | [Explicit speech feature](services/text-to-speech.md) |

## Boards

The implemented adapter is [Waveshare ESP32-S3 Touch AMOLED 1.75](boards/waveshare-esp32s3-touch-amoled-1.75/hardware.md).
Its [display geometry](boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md)
and [power references](boards/waveshare-esp32s3-touch-amoled-1.75/power-reference.md)
retain their physical and evidence limits.

Future boards stay within the Espressif ecosystem, including devices with greater
compute capacity. Each needs an explicit adapter, capability description, pinout,
reviewed dependencies and board-specific evidence. Changing a YAML name does not
add support. The documentation split adds no new adapter or compatibility path.

[Workstation inventory](workstation-inventory.md) is a dated historical snapshot;
the selected project toolchain is defined by the repository configuration.
