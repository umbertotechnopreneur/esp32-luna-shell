# Purchased Waveshare AMOLED boards: compatibility assessment

Reviewed 2026-10-15 using the supplied reference folders, repository source,
the complete three-page schematic/PCB PDFs, product dimension drawings and live
official Waveshare documentation. This is a static assessment. No firmware
checks, configure, build, size, simulator, serial, flash or device tests were run.

## Result

The current repository targets **ESP32-S3-Touch-AMOLED-1.75, GPS variant,
without the C suffix**. The purchased **1.75C is a different board**, even
though its display has the same advertised size, resolution and controller
family. A casing does not establish electrical or firmware equivalence.

The purchased **1.43C is also AMOLED**. It preserves the 466 x 466 logical
canvas and the ESP32-S3 processor architecture, but changes the pinout, touch,
power hardware and flash capacity, and omits the current board's motion sensor.
Both purchases need explicit board support; neither is covered by the current
firmware profile simply because its screen is round.

The current target is established by
[`hardware/inventory.json`](../../../hardware/inventory.json), the
[current board inventory](waveshare-esp32s3-touch-amoled-1.75/hardware.md),
[`catalog.json`](../../../config/sdkconfig/v1/catalog.json),
[16 MB board defaults](../../../config/sdkconfig/v1/boards/waveshare-esp32s3-touch-amoled-1.75.defaults)
and the [current BSP pin definitions](../../../components/esp32_s3_touch_amoled_1_75/include/bsp/esp32_s3_touch_amoled_1_75.h).
These identify the software target and documented prototype; they do not
identify or electrically validate the units that will arrive.

## Hardware comparison

| Area | Current 1.75 GPS prototype | Purchased 1.75C | Purchased 1.43C |
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

The wiki lists 1.75C SKUs 33691 with battery and 33692 without battery;
1.43C SKU 34383 includes a battery and 34384 (`-EN`) does not. The request
establishes that both purchased products have cases, but not their exact battery
variant or capacity. Check the order and battery label when received. Store
pages returned HTTP 403 during this review, so captured prices are not treated
as a fresh quotation or evidence of stock, taxes or shipping.

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

**The 1.75C is the closest target.** Most rendering, Wi-Fi/BLE, CCP, application
logic and audio codec work can be reused in principle. A separate board profile
must select the new reset/MCLK pins, PMU rails, PWR handling, IMU interrupt and
actual peripherals. The existing 16 MB partition layouts fit within 32 MB as
an address range, but the profile must describe the fitted flash and review its
layout. Extra capacity does not establish that the existing binary can boot.

The official [1.75C repository](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C)
advertises ESP-IDF 6.0.2 examples. The supplied display demo selects
`waveshare/esp32_s3_touch_amoled_1_75c` `^3.0.0`. The pinned official Brookesia
tree already contains the board definition
`hal/brookesia_hal_boards/boards/waveshare/esp32_s3_touch_amoled_1_75c/`;
that is a public integration starting point, not evidence that Luna currently
selects it. The archived vendor Brookesia demo includes the old Phone framework;
it remains reference material and must not replace the project's Brookesia 0.8
System Super/Core architecture. Cody must retain its native 2D path without
adding LVGL or its adapter dependency.

**Flash is the first design gate for 1.43C.** Both current tables,
[`partitions.csv`](../../../partitions.csv) and
[`wake-word.csv`](../../../config/partitions/wake-word.csv), end at `0x1000000`
(16 MiB); an 8 MiB device ends at `0x800000`. They cannot be reused. The current
wake-word table has two `0x6f0000` application slots, a 1 MiB speech-model
partition and a 1 MiB LittleFS partition. The last recorded Release application
in [done.md](../../../done.md) is 7,151,328 bytes, already larger than half of
the new board's entire flash. That historical size is not a new build result.
The current-sized application cannot support two such internal OTA slots on
1.43C. Review feature/asset size and OTA requirements before choosing a smaller
layout; a single application slot may be possible, but full-feature fit is not
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

**A 466 x 466 canvas helps reuse the UI, but does not validate the panel.**
The 1.43C vendor page names CO5300, while its supplied ESP-IDF source actually
selects `esp_lcd_sh8601`, issues SH8601 initialization calls and sets an X gap of
8 pixels. The current board documents a 6-pixel controller gap. Resolve the
controller/initialization discrepancy against the actual panel revision and
public driver documentation before selecting the driver; do not add a guessed
compatibility shim. CST820 needs its own supported touch path and a verified
transform shared with rendering. The nominal screen-size ratio is
`1.43 / 1.75 = 0.817`: unchanged pixel controls are about 18 percent smaller
physically. Review complete text, touch comfort, QR readability, bezel clipping
and all four orientations on the delivered device. The
[existing circular geometry rules](waveshare-esp32s3-touch-amoled-1.75/display-geometry.md)
remain the starting logical layout, not a physical calibration result.

The supplied 1.43C examples are under `esp-idf-v5.5.3/`; their minimum IDF
manifest is not proof of IDF 6.0.2 compatibility. Derive new support from the
documented hardware and public ESP-IDF interfaces while retaining IDF 6.0.2
and Brookesia 0.8. Do not add an IDF 5.x or old-framework compatibility path.

## Proposed delivery sequence and remaining evidence

1. Add typed physical-board identities for the existing GPS prototype, 1.75C
   and 1.43C, separate from Cody/Luna/display-only UI selection. Extend the
   current board catalog/parser, defaults, BSP selection and dependency locks;
   `scripts/common/board_config.py` currently accepts only the old 1.75 ID.
2. Port 1.75C first using its public board support, reviewed pins and capability
   set, preserving native Cody and the existing prototype profile.
3. Resolve the 1.43C display-controller evidence, then decide its flash/OTA/
   speech budget and implement its display, CST820, audio and ADC/power owners.
4. Adapt consumers to the actual hardware capabilities, retaining canonical
   snapshots and explicit unavailable/error states. Do not infer missing data
   or bypass startup failures.
5. After implementation is complete, obtain the separately required command-
   specific authorizations for checks, builds and physical bring-up through
   `scripts/device.ps1`. Confirm board revision, battery, panel transform,
   audio, power, wake behavior and stack headroom on the received hardware.

This request imported references and produced the assessment; it did not
implement those profiles or establish runtime compatibility. The proposed work
is retained as **HARDWARE-PORT-01** in the root
[deferred register](../../../planned-todo.md).

## Local primary evidence

- [1.75C schematic and PCB sheets](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/hardware-docs/schematic.pdf).
- [1.43C schematic and PCB sheets](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/hardware-docs/schematic.pdf).
- [1.75C dimensions](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/ESP32-S3-Touch-AMOLED-1.75C-details-size.jpg).
- [1.43C dimensions](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/images-wiki/ESP32-S3-Touch-AMOLED-1.43C-ProductSize-966473b8ae463f203b9f1b3455b82970.webp).
- [1.43C pin definitions](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/software/esp-idf-v5.5.3/01_ADC_Test/components/port_bsp/bsp_config.h).
- [1.43C display, touch and ADC example](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/software/esp-idf-v5.5.3/01_ADC_Test/components/port_bsp/esp32_s3_touch_amoled_1.43c.cpp).
- [1.43C audio pins](../../../hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.43C/software/esp-idf-v5.5.3/01_ADC_Test/components/externlib/codec_board/board_cfg.txt).
