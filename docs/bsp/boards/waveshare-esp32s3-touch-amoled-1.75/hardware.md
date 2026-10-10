# Waveshare ESP32-S3-Touch-AMOLED-1.75 hardware inventory

Target: GPS variant of the Waveshare ESP32-S3-Touch-AMOLED-1.75.

The board [hardware consumption and sleep-wakeup reference](power-reference.md)
records datasheet ranges, supply conditions, source revisions and unavailable
values. Its 2026-10-04 schematic review is source evidence, not a device test.

| Area | Device / capability | Firmware integration |
| --- | --- | --- |
| MCU | ESP32-S3R8, dual core up to 240 MHz | ESP-IDF 6.0.2, 8 MB octal PSRAM, 16 MB flash |
| Display | 1.75-inch round AMOLED, 466 x 466, CO5300, QSPI | Waveshare BSP; Brookesia/LVGL UI or standalone renderer according to profile |
| Touch | CST9217, two-point capacitive touch | Shared BSP owner; LVGL or standalone IRQ input according to profile |
| Power | AXP2101 PMU at I2C `0x34` | Probed; battery presence, charging state, percent, and voltage logged |
| IMU | QMI8658 at I2C `0x6B` or `0x6A` | Probed and initialized for accelerometer + gyroscope |
| GPS / GNSS | LC76G GPS variant, external ceramic antenna | Command-gated I2C bridge at control `0x50`, read `0x54`; bounded NMEA chunks, no-fix is allowed |
| Storage | User-added 8 GB microSD | SDMMC 1-bit; mount only, never auto-format |
| Flash | W25Q128JVSIQ, schematic U2 | 16 MB external flash; exact fitted marking not inspected in this review |
| Audio out | ES8311 codec + NS4150B amplifier, schematic U7 | Embedded 22.05 kHz mono PCM16 boot SFX; amplifier CTRL on GPIO46 |
| Audio in | Dual microphones / ES7210 path | Shared 22.05 kHz mono PCM16 initialization and bounded level sample |
| RTC | PCF85063 | Initialized and sampled with clock-integrity reporting |
| I/O expander | TCA9554 at I2C `0x20` | Managed by the official BSP |
| Wi-Fi | ESP32-S3 2.4 GHz 802.11 b/g/n | STA driver started with RAM-only config; no credentials embedded |
| Bluetooth | Bluetooth LE | NimBLE peripheral advertises as `LUNASHELL-xxxx` |

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
| IMU INT2 | 21 (direct ESP RTC-capable GPIO; Release WoM wake implemented in source, hardware verification pending) |
| SDMMC CLK / CMD / D0 | 2 / 1 / 3 |
| I2S BCLK / MCLK / WS / DOUT / DIN | 9 / 42 / 45 / 8 / 10 |
| Speaker amplifier enable | 46 |

The published schematic routes PMU IRQ to TCA9554 P5, RTC INT to P3 and IMU
INT1 to P6. TCA9554 INT has a pull-up but no ESP connection. PWR is AXP2101
PWRON; GPIO0 is BOOT. Touch GPIO11 and IMU INT2 GPIO21 are direct candidate
Deep-sleep wake routes. See the root census for conditions and implementation
status. Common VCC3V3 supplies GNSS VCC, touch, IMU, SD and the amplifier;
VCC-RTC supplies RTC/GNSS backup, and A3V3 supplies the audio ADC/codec analog
path. Firmware exclusion does not prove an electrically disabled device.

## SD preparation

This board has no SD card-detect pin. A failed initialization therefore means
card/transport unconfirmed and must not be presented as confirmed physical
absence. Prepare a disposable validation card as FAT32 with an MBR partition.
Runtime mount failures are logged and never trigger an implicit format.

Expected runtime path:

```text
/sdcard/LUNASHELL/logs/LUNASHELL.log
```

The logger rotates the file at 2 MB to `LUNASHELL.log.1`.

An SDMMC OCR timeout happens before FAT parsing. It means that protocol
initialization did not complete, so card presence and transport remain
unconfirmed and no filesystem or formatting conclusion is valid.

## Historical runtime inventory evidence

An earlier 2026-07-25 session used a physical board then assigned to `COM12`.
That older firmware run observed PMU, RTC, calibrated IMU, display, touch,
Wi-Fi, BLE provisioning advertising, ES8311 speaker playback, and ES7210
capture. It also returned and parsed two bounded LC76G NMEA chunks; the indoor
pass had no fix. With the card intentionally removed for that session, the
SDMMC probe timed out and scheduled a non-blocking retry.

These observations predate the current tree and do not validate this commit,
its binaries, or its present runtime behavior. Current validation must produce
new build identity and serial evidence.

The platform snapshot also records native ESP-IDF heap and PSRAM metrics,
largest allocatable blocks, chip core count, and FreeRTOS task/stack state for
each core.

## RTC and wake limits

On the ESP32-S3-Touch-AMOLED-1.75, U4 is PCF85063ATL with a 32.768-kHz crystal,
I2C address 0x51 on SDA15/SCL14. VDD is VCC-RTC from AXP2101, not an independent
coin cell. CLKOUT is unconnected and CLKOE is held low. Retention across removed
USB/battery or PMU-off conditions is not established by chip specifications and
still needs measurement on this board.

RTC INT goes to EXIO3/TCA9554 P3. The TCA9554 INT node has a pull-up but no ESP32
GPIO connection on the current PCB, so a PCF85063 alarm cannot directly wake
ESP32 deep sleep. ESP32 software timer wake is a different mechanism; normal
release sleep currently programs no schedule deadline. An RTC-to-free-GPIO wire
would be an explicit hardware change, not an existing firmware capability.

The chip has one alarm comparator (second/minute/hour/day/weekday), not month/year
or multiple alarms. Its 8-bit reload timer is limited to 255 seconds at 1 Hz or
4 hours 15 minutes at one tick/minute. Typical 0.22-µA chip current at 3.3 V/25°C with
clock output disabled is not total-board current or demonstrated time accuracy.
Alarm/countdown registers, reminders/snooze UI, alarm snapshots and an automatic
wake scheduler are not implemented. A future scheduler needs persisted UTC
deadlines, explicit wall-clock-change/error policy, active monotonic timing and
a supported wake source; it must not be inferred from the routine greeting.

Hardware detail is supported by the
[Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf),
[board hardware reference](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
and [NXP PCF85063A datasheet](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf).
RTC retention, SNTP recovery, clock fault handling, timezone/DST transitions and
physical cue gating still require explicitly approved device observation. No
network-duty-cycle or battery-life improvement is established by this design.
