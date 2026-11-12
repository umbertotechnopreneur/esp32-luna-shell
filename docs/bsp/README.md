<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

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

The shared BSP supports the Waveshare 1.75 GPS (16 MB) and 1.75C (32 MB).
They share a 466 × 466 canvas and the CO5300/CST9217 drivers. Board selection
determines flash layout, reset and audio clock pins, motion interrupt routing
and available peripherals. The [board comparison](boards/purchased-amoled-comparison.md)
also documents the 1.43C hardware.

The [1.75 GPS hardware inventory](boards/waveshare-esp32s3-touch-amoled-1.75/hardware.md),
[display geometry](boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md)
and [power reference](boards/waveshare-esp32s3-touch-amoled-1.75/power-reference.md)
describe the original board.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
