# Waveshare ESP32-S3 AMOLED board comparison

Hardware specifications come from the supplied schematics, PCB sheets, dimension
drawings and Waveshare documentation.

## Board selection

The default target is the **1.75 GPS** with 16 MB flash. For the **1.75C**,
select `-Board waveshare-esp32s3-touch-amoled-1.75c`. This selects the
32 MB partition table, GPIO assignments, PMU rails and available peripherals.
The **1.43C** has an 8 MB flash and needs a separate BSP selection.
See the [device console](../../../scripts/README.md).

The original prototype is established by
[`hardware/inventory.json`](../../../hardware/inventory.json), the
[current board inventory](waveshare-esp32s3-touch-amoled-1.75/hardware.md),
[`catalog.json`](../../../config/sdkconfig/v1/catalog.json),
[16 MB board defaults](../../../config/sdkconfig/v1/boards/waveshare-esp32s3-touch-amoled-1.75.defaults)
and the [current BSP pin definitions](../../../components/esp32_s3_touch_amoled_1_75/include/bsp/esp32_s3_touch_amoled_1_75.h).
These identify the documented prototype. The 1.75C selection adds its own
[defaults](../../../config/sdkconfig/v1/boards/waveshare-esp32s3-touch-amoled-1.75c.defaults)
and isolated build state; neither configuration proves physical behavior.

## Hardware comparison

| Area | 1.75 GPS | 1.75C | 1.43C |
| --- | --- | --- | --- |
| Processor | ESP32-S3R8, dual LX7 up to 240 MHz | ESP32-S3R8, same architecture | ESP32-S3-PICO-1-N8R8, same CPU architecture |
| Flash / PSRAM | 16 MB / 8 MB | 32 MB / 8 MB | 8 MB / 8 MB |
| Display | Round 1.75-inch AMOLED, 466 x 466, CO5300/QSPI | Same advertised display specifications; exact panel assembly equivalence unconfirmed | Round 1.43-inch AMOLED, 466 x 466, advertised CO5300/QSPI; demo-driver discrepancy below |
| Touch | CST9217 | CST9217 | CST820 |
| Audio | ES8311 + ES7210, dual microphones, NS4150B amplifier | Same chip families; audio clock pin changes | Same chip families; I2C/I2S pins and audio power control change |
| Power | AXP2101 PMU with battery telemetry and controlled rails | AXP2101 retained; board wiring and button path change | ETA6098 charger, TPS63020 buck-boost and discrete power circuit; no AXP2101 |
| Motion | QMI8658 accelerometer + gyroscope | QMI8658 retained; interrupt routing changes | No IMU in the supplied schematic |
| GNSS | LC76G in the documented GPS variant | No GNSS in the supplied schematic | No GNSS in the supplied schematic |
| Removable storage | microSD slot | No microSD slot in schematic/PCB | No microSD slot in schematic/PCB |
| External RTC | PCF85063 | Not present in supplied schematic; wiki introduction mentions an RTC, so confirm the delivered PCB | No external RTC in the supplied schematic |
| I/O expander | TCA9554 | Not present in the supplied schematic | Not present in the supplied schematic |
| Radio | 2.4 GHz Wi-Fi and BLE | Same radio classes | Same radio classes |
| Enclosure | Existing prototype arrangement | Enclosed product, aluminum alloy; drawing: 55.00 mm diameter, 15.05 mm thick | Enclosed product; drawing: maximum 50.20 mm diameter, body 48.00 mm, 14.50 mm thick |

Waveshare's current [1.75C documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C)
and [1.43C documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.43C)
support the advertised specifications. Absence and wiring statements above
refer specifically to the supplied schematics and PCB sheets, not a teardown.
The 1.75C wiki's RTC claim conflicts with those drawings: do not enable the
prototype's PCF85063 owner on that basis. A PMU rail named `VCC-RTC` does not
establish the presence of an external RTC chip.

Waveshare lists 1.75C SKU 33691 and 1.43C SKU 34383 with batteries.
The corresponding battery-free variants are 33692 and 34384 (`-EN`).
Check the cell label for the capacity of a particular unit.

## Pin differences that prevent reusing the current binary

| Signal | Current 1.75 | 1.75C | 1.43C |
| --- | --- | --- | --- |
| I2C SDA / SCL | 15 / 14 | 15 / 14 | 47 / 48 |
| Display CS / clock | 12 / 38 | 12 / 38 | 15 / 14 |
| Display D0 / D1 / D2 / D3 | 4 / 5 / 6 / 7 | 4 / 5 / 6 / 7 | 9 / 10 / 11 / 12 |
| Display reset | 39 | 1 | 13 |
| Touch reset / interrupt | 40 / 11 | 2 / 11 | 16 / 17 |
| Audio MCLK | 42 | 16 | 38 |
| Audio BCLK / WS | 9 / 45 | 9 / 45 | 39 / 40 |
| Audio output / input | 8 / 10 | 8 / 10 | 41 / 42 |
| Amplifier enable | 46 | 46 | 46 |
| Additional audio supply enable | Existing PMU-controlled supply | Review AXP2101 rail configuration | GPIO18 (`CODEC_EN`) |
| IMU direct interrupt | INT2 on GPIO21 | INT1 on GPIO21 in supplied schematic | No IMU |
| Battery acquisition | AXP2101 over I2C | AXP2101 over I2C | GPIO4 / ADC1 channel 3, divider; charger STAT GPIO7 |

For 1.75C, schematic `SYS_OUT` reaches GPIO3; PWR and PMU interrupt handling
must be reviewed against that circuit instead of retaining the old TCA9554
assumptions. Keeping GPIO21 does not preserve the IMU interrupt identity:
the wake-on-motion configuration must use the wired interrupt output.

For 1.43C, the public PWR description is automatic power-on when connected,
short press to power on and long press to power off. Its circuit is different
from an AXP2101 register-controlled shutdown. Do not promise the current
software shutdown, wake or USB/battery policy without a supported route and
physical evidence. Battery voltage and a charge-status pin do not provide the
prototype's fuel-gauge percentage, current or reliable battery-presence data.

## Software reuse and the main porting constraints

**The 1.75C is the closest target.** Rendering, Wi-Fi/BLE, CCP, application
logic and codec services share the existing implementation. Its physical
selection owns the reset/MCLK pins, PMU rails, PWR handling, IMU interrupt and
peripheral exclusions. Both OTA slots fit entirely in the lower 16 MiB of the
1.75C flash. The wake-word layout also places the complete 1 MiB Sophia model
partition in that range so ESP-SR can map it through the 24-bit flash cache.
LittleFS reserves the complete upper 16 MiB at `0x1000000` for files and resources,
including the previously unallocated 15 MiB. Both OTA slots remain unchanged.
This is a clean layout with no compatibility or migration path for the old
1 MiB filesystem at `0x1f00000`. Installing it requires the new partition table
and a filesystem initialized for the new extent. Changing the source table alone
changes no device data. See the [base layout](../../../config/partitions/amoled-1.75c.csv)
and [wake-word layout](../../../config/partitions/amoled-1.75c-wake-word.csv).

The official [1.75C repository](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C)
advertises ESP-IDF 6.0.2 examples. The supplied display demo selects
`waveshare/esp32_s3_touch_amoled_1_75c` `^3.0.0`. The pinned official Brookesia
tree already contains the board definition
`hal/brookesia_hal_boards/boards/waveshare/esp32_s3_touch_amoled_1_75c/`;
that is a public hardware reference. This repository extends its shared BSP
instead of importing that application/HAL path. The archived vendor Brookesia demo includes the old Phone framework;
it remains reference material and must not replace the project's Brookesia 0.8
System Super/Core architecture. Cody must retain its native 2D path without
adding LVGL or its adapter dependency.

**Flash is the first design gate for 1.43C.** Both current tables,
[`partitions.csv`](../../../partitions.csv) and
[`wake-word.csv`](../../../config/partitions/wake-word.csv), end at `0x1000000`
(16 MiB); an 8 MiB device ends at `0x800000`. They cannot be reused. The current
wake-word table has two `0x6f0000` application slots, a 1 MiB speech-model
partition and a 1 MiB LittleFS partition. Two existing application slots alone
exceed the 1.43C's entire 8 MiB flash. Review feature/asset size and OTA
requirements before choosing a smaller layout; a single application slot may be possible, but full-feature fit is not
established, especially with speech models and internal assets.

**Power and missing sensors change behavior on 1.43C.** Add a board-owned ADC
and charger-status acquisition path, publish only successfully acquired samples
through `DeviceSnapshotService`, and expose unavailable fields explicitly.
The vendor ADC example returns zero on a failed read; that behavior must not be
copied into Luna. No IMU means no deliberate-shake Home reset, motion wake,
inertial activity observer or IMU-driven animation. Keep existing tap navigation
and design any additional replacement gesture explicitly. Neither new board
should start GNSS/SD/RTC/expander owners whose hardware is absent. Configure those
capabilities explicitly; never hide a failed initialization as success.

**1.43C display and touch.** Waveshare identifies the panel as CO5300.
Its supplied demo uses the SH8601 QSPI driver with a board-specific command
table, sets MADCTL to `0xC0` and applies an 8-pixel X gap for drawing. The
1.75 GPS uses a 6-pixel gap. The CST820 reports touch on I²C address `0x15`.
Verify that touch and drawing agree at all four orientations on the physical
panel. Each screen has 466 × 466 pixels, so the same control on a 1.43-inch
panel is about 18 percent smaller in millimetres. Check text and QR readability,
hit targets and bezel clearance using the
[shared circular geometry](waveshare-esp32s3-touch-amoled-1.75/display-geometry.md).

The supplied 1.43C examples are under `esp-idf-v5.5.3/`; their minimum IDF
manifest is not proof of IDF 6.0.2 compatibility. Derive new support from the
documented hardware and public ESP-IDF interfaces while retaining IDF 6.0.2
and Brookesia 0.8. Do not add an IDF 5.x or old-framework compatibility path.

## Remaining evidence and deferred support

The [active work](../../../TODO.md) owns 1.75C physical acceptance: delivered
board revision/RTC population, panel/touch transform, audio, battery telemetry,
PWR/standby/wake and stack headroom. These remain separate from compilation and
flash success. Missing hardware remains explicitly unavailable in snapshots.

The unsupported 1.43C remains **HARDWARE-PORT-01** in the
[deferred register](../../../planned-todo.md). Resolve its panel driver, 8 MB
OTA/speech budget, CST820, audio wiring and ADC/charger ownership before activation.

## Local primary evidence

- [1.75C schematic and PCB sheets](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/hardware-docs/schematic.pdf).
- [1.43C schematic and PCB sheets](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/hardware-docs/schematic.pdf).
- [1.75C dimensions](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/ESP32-S3-Touch-AMOLED-1.75C-details-size.jpg).
- [1.43C dimensions](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/images-wiki/ESP32-S3-Touch-AMOLED-1.43C-ProductSize-966473b8ae463f203b9f1b3455b82970.webp).
- [1.43C pin definitions](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/software/esp-idf-v5.5.3/01_ADC_Test/components/port_bsp/bsp_config.h).
- [1.43C display, touch and ADC example](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/software/esp-idf-v5.5.3/01_ADC_Test/components/port_bsp/esp32_s3_touch_amoled_1.43c.cpp).
- [1.43C audio pins](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/software/esp-idf-v5.5.3/01_ADC_Test/components/externlib/codec_board/board_cfg.txt).
