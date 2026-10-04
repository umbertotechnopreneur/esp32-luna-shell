# Documentation map

Architecture guides own behavioral contracts. Folder READMEs explain code
ownership; Git and `done.md` retain history. Open verification belongs in `TODO.md`.

## Cody

- [Overview](architecture/cody/README.md)
- [Runtime, scenes and debugging](architecture/cody/runtime.md)
- [Onboarding and pairing](architecture/cody/onboarding.md)
- [Rig, rendering and performance](architecture/cody/rendering.md)
- [Clock and RTC](architecture/cody/clock.md)
- [Planning atlas](cody/cody-planning-atlas-v1.pdf) and
  [navigation atlas](cody/cody-navigation-atlas-v1.pdf)
- [Pairing addendum](planning/2026-10-03-pairing-atlas-addendum.md) and
  [appearance addendum](planning/2026-10-04-cody-look-atlas-addendum.md)

## Platform and Luna

| Area | Contract |
| --- | --- |
| Geometry | [Circular layout](architecture/circular-display-layout.md), [round-display guidance](ui/round-display-guidelines.md) |
| Display/touch | [Runtime ownership](architecture/display-touch-runtime.md) |
| Startup/logs | [Startup](architecture/startup-and-logger.md), [SD/USB logs](architecture/offline-logging-usb.md) |
| Hardware data | [Snapshot](architecture/device-snapshot.md), [hardware API](architecture/hardware-api.md) |
| Power | [Low-power policy](architecture/low-power-management.md), [release deep sleep](architecture/release-deep-sleep.md) |
| Connectivity | [Pairing](architecture/connectivity-pairing.md), [HTTP API](architecture/http-device-api.md) |
| Audio | [Wake-on-word](architecture/wake-on-word.md) |
| Build selection | [Board configuration](architecture/board-build-configuration.md), [Debug/Release](architecture/firmware-build-modes.md) |
| Release | [Open-source checklist](release/open-source-pre-release-checklist.md) |

## Hardware and development

- [Verified hardware inventory](hardware/hardware-inventory.md)
- [Dual-UART ToF wiring](hardware/tof400f-dual-uart.md)
- [ESP-IDF 6 migration constraints](esp-idf-6-migration.md)
- [Public development console](../scripts/README.md)
- [Repository rules](../AGENTS.md) and [open work](../TODO.md)
