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
    participant SD
    participant BSP
    participant UI as Boot console
    participant HW as Hardware probes
    participant B as ESP-Brookesia

    Main->>Log: install vprintf tee
    Main->>SD: mount without format
    SD-->>Log: enable mirror if mounted
    Main->>BSP: display + CST9217
    Main->>UI: show QSee.ai and live log
    Main->>HW: PMU, IMU, GPS, Wi-Fi, BLE
    HW-->>Log: one canonical status per subsystem
    Main->>UI: mark ready
    UI-->>Main: touch
    Main->>B: launch Phone and QSee.ai app
```

All optional hardware probes are fail-open. A missing SD card, GPS fix, IMU, or
radio does not prevent the UI from becoming usable.
