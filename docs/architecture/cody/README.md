# Cody standalone companion

Cody is the front-facing, English-only character runtime for the
ESP32-S3-Touch-AMOLED-1.75. It uses the shared Luna hardware, snapshot, settings,
power and connectivity owners, but renders through `CODY2D` without LVGL or the
Brookesia GUI. The supported platform is ESP-IDF 6.0.2 / ESP-Brookesia 0.8;
there is no legacy-framework compatibility path.

## Read by responsibility

| Guide | Current contract |
| --- | --- |
| [Runtime](runtime.md) | Startup, scenes, active clocks, standby, optional audio and device diagnostics |
| [Idle scenes](idle-scenes.md) | Visible mouth breathing, random yawn/whistle/greeting accents and audio ownership |
| [Onboarding](onboarding.md) | Screens 04–15, pairing/QR ownership, skin selector 11 and CCP confirmation |
| [Rendering](rendering.md) | Authored rig, five jelly skins, snapshot response, damage/DMA ownership and memory |
| [Clock](clock.md) | Trusted time, routines/quiet policy, RTC drift diagnostics and board limitations |

These are current architecture contracts, not a history of builds or flashes.
The original navigation/planning atlases and their addenda remain the visual
references; their PDFs and supplied artwork are not rewritten by these guides.
Screen IDs, expression IDs and authored entity IDs have distinct typed namespaces.
Do not infer a route or an expression from a catalog position.

## Profile and hardware boundary

The `cody-companion` catalog layers the standalone profile with connectivity
features. Display/CO5300 and touch/CST9217 are mandatory. It admits Wi-Fi,
Bluetooth provisioning, power/battery, IMU, PCF85063 RTC, ES8311 speaker,
ES7210 microphone and removable SD. GNSS and distance are excluded; disabling
GNSS firmware does not physically disconnect the GPS power rail. Voice AFE is a
separate optional feature, not a prerequisite for visual startup.

Pearl jelly, face-follow, force-first-boot and the shared auto-rotation startup
policy are currently selected. The HTTP API is compiled but defaults to disabled;
BLE provisioning does not start automatically. Core graphics/fonts/audio belong
in internal flash, not on SD. The board's 16 MB flash capacity is separate from
application-partition, PSRAM, internal-stack and DMA budgets.

Use the canonical platform guides for detail rather than introducing Cody owners:

- [Board/profile configuration](../board-build-configuration.md),
  [circular geometry](../circular-display-layout.md) and
  [display/touch runtime](../display-touch-runtime.md).
- [Device snapshots](../device-snapshot.md),
  [startup/logger](../startup-and-logger.md) and
  [offline USB/SD logging](../offline-logging-usb.md).
- [Power management](../low-power-management.md),
  [deep sleep](../release-deep-sleep.md) and [wake-on-word](../wake-on-word.md).
- [Connectivity/pairing](../connectivity-pairing.md) and
  [HTTP API](../http-device-api.md).

## Evidence and change discipline

Consumers read only canonical published values, validity, health, original errors
and timestamps. Missing settings may select a logged, persisted default; corrupt
or failed reads never authorize silent repair. Optional effects may be bypassed
only at a documented boundary while retaining the real failure.

Use `scripts/device.ps1` as the sole public entrypoint. Tests, validation scans,
build/size, simulator execution and physical device operations require the user's
explicit approval; implementation approval is not validation approval. Source or
asset preparation, host simulation and successful flashing do not establish the
latest device appearance, audible quality, timing or stability.

The intermittent display/touch consumer stop remains unresolved. Current device
handoff also needs long-run DMA/heap/stack observation, physical cardinal rotation
and touch alignment, pairing/standby races, RTC retention and actual speaker
listening. Topic-specific limitations are retained in the linked guides. Historical
command transcripts and revision measurements remain in Git history, not here.
