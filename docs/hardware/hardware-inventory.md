# Waveshare ESP32-S3-Touch-AMOLED-1.75 hardware inventory

Target: GPS variant of the Waveshare ESP32-S3-Touch-AMOLED-1.75.

| Area | Device / capability | Firmware integration |
| --- | --- | --- |
| MCU | ESP32-S3R8, dual core up to 240 MHz | ESP-IDF 5.5.4, 8 MB octal PSRAM, 16 MB flash |
| Display | 1.75-inch round AMOLED, 466 x 466, CO5300, QSPI | Official Waveshare BSP + LVGL 9 |
| Touch | CST9217, two-point capacitive touch | Initialized by `bsp_display_start()` |
| Power | AXP2101 PMU at I2C `0x34` | Probed; battery presence, charging state, percent, and voltage logged |
| IMU | QMI8658 at I2C `0x6B` or `0x6A` | Probed and initialized for accelerometer + gyroscope |
| GPS / GNSS | LC76G GPS variant, external ceramic antenna | I2C transport at write `0x50`, read `0x54`; non-blocking, no-fix is allowed |
| Storage | User-added 8 GB microSD | SDMMC 1-bit; mount only, never auto-format |
| Audio out | ES8311 codec + speaker amplifier | Embedded 22.05 kHz mono PCM16 boot SFX |
| Audio in | Dual microphones / ES7210 path | Delayed 22.05 kHz mono PCM16 initialization and bounded level sample |
| RTC | PCF85063 | Initialized and sampled with clock-integrity reporting |
| I/O expander | TCA9554 at I2C `0x20` | Managed by the official BSP |
| Wi-Fi | ESP32-S3 2.4 GHz 802.11 b/g/n | STA driver started with RAM-only config; no credentials embedded |
| Bluetooth | Bluetooth LE | NimBLE peripheral advertises as `QSEE-xxxx` |

## Shared bus and pin ownership

The official BSP owns the board buses. Application modules obtain the existing
I2C master handle through `bsp_i2c_get_handle()` and do not initialize a second
I2C driver.

| Signal | GPIO |
| --- | ---: |
| I2C SDA | 15 |
| I2C SCL | 14 |
| Display CS / PCLK / D0-D3 / reset | 12 / 38 / 4,5,6,7 / 39 |
| Touch reset / interrupt | 40 / 11 |
| SDMMC CLK / CMD / D0 | 2 / 1 / 3 |
| I2S BCLK / MCLK / WS / DOUT / DIN | 9 / 42 / 45 / 8 / 10 |
| Speaker amplifier enable | 46 |

## SD preparation

The 8 GB card is currently considered unformatted. Format it as FAT32 with an
MBR partition before device validation. Runtime mount failures are logged and
shown on the boot console; they never trigger an implicit format.

Expected runtime path:

```text
/sdcard/qsee/logs/qsee.log
```

The logger rotates the file at 2 MB to `qsee.log.1`.

An SDMMC OCR timeout happens before FAT parsing. It means that the card did not
complete protocol initialization, so it should be investigated separately from
the still-required FAT32 formatting.

## Runtime inventory evidence

The physical board on `COM12` has confirmed PMU, RTC, IMU, display, touch,
Wi-Fi, BLE advertising, ES8311 speaker playback, and ES7210 initialization.
The LVGL profile uses one 10-row internal DMA-capable buffer; the 2026-07-25
validation had no bitmap or SPI DMA-allocation failures. The LC76G acknowledges
its I2C write/read addresses, while its first NMEA transaction currently
reports a degraded state. The 8 GB SD card currently times out during SDMMC
initialization.

The platform snapshot also records native ESP-IDF heap and PSRAM metrics,
largest allocatable blocks, chip core count, and FreeRTOS task/stack state for
each core.
