# Startup and canonical logger

The logger is installed as the first application action. Its callback never
touches LVGL or the filesystem.

```mermaid
flowchart LR
    A["ESP_LOG call"] --> B["Serial vprintf"]
    A --> C["Bounded RAM ring"]
    A --> D["Non-blocking queue"]
    C --> E["Boot UI polls snapshot"]
    D --> F["Low-priority SD writer"]
    F --> G["/sdcard/qsee/logs/qsee.log"]
```

The SD writer starts with the logger but does not consume queued lines until a
card has mounted successfully. Queue overflow drops only the SD copy; serial
and the newest RAM lines remain available.

## Boot sequence

```mermaid
sequenceDiagram
    participant Main
    participant Log as Canonical logger
    participant BSP
    participant UI as Boot console
    participant LFS as Internal LittleFS
    participant HW as Hardware probes
    participant SD as Removable SD
    participant Snap as DeviceSnapshotService
    participant Super as Brookesia System Super

    Main->>Log: install vprintf tee
    Main->>BSP: display + CST9217
    Main->>UI: show QSee.ai and live log
    Main->>LFS: mount staged resources without format
    Main->>HW: PMU, RTC, IMU, GPS, audio
    Main->>SD: start mount retries without format
    SD-->>Log: enable mirror only if mounted
    Main->>HW: Wi-Fi, BLE, boot scan
    HW-->>Log: one canonical status per subsystem
    Main->>Snap: explicit post-radio refresh
    Main->>UI: mark ready
    Main->>UI: hide boot console
    Main->>Super: init and start
    Super->>Super: install registered QSee.ai provider
    Super->>Super: start QSee.ai System Core app
```

All optional hardware probes are fail-open. A missing SD card, GPS fix, IMU, or
radio does not prevent the UI from becoming usable.

## Brookesia application boundary

System Super owns the system lifecycle and services. System Core discovers the
registered QSee.ai provider, creates its native `IApp`, and drives its
start/stop/action/timer callbacks. GUI LVGL loads the app document from
`/littlefs/ai.qsee.device-console/res/root.json` and renders its
`qsee_content` screen flow.

The JSON package contains view structure, layout, styles, and action names.
The native app binds values through the GUI interface. It reads hardware
telemetry only from `DeviceSnapshotService`; the Logs screen reads only the
canonical logger ring. Search uses the System Super keyboard service, and SD
formatting uses the System Super message-dialog service.

## Storage separation

`/littlefs` is an internal flash volume containing staged Brookesia and QSee.ai
application resources. It is mounted with formatting disabled. `/sdcard` is
the removable FAT volume used for the mirrored canonical log and user data.
Neither volume is used as a fallback for the other.

Ordinary SD detection and mount retries never format the card. A format attempt
can be armed only when `DeviceSnapshotService` reports a responding card whose
FAT mount requires formatting and the user accepts the destructive
confirmation. Cancelling the dialog performs no storage write.
