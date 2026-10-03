# Hardware inventory: consumption and sleep wakeup

Target: **Waveshare ESP32-S3-Touch-AMOLED-1.75 GPS**, ESP32-S3R8.
Datasheet and schematic review: **2026-10-04**. No electrical measurements,
firmware builds, tests, flashing or serial access were performed for this review.

This root inventory owns the consumption references below. See the
[pin and firmware integration inventory](docs/hardware/hardware-inventory.md)
and [machine-readable component inventory](hardware/inventory.json).
`machine-environment-inventory.md` describes the development PC and toolchain.

## How to read the ranges

- **Typical** values describe the manufacturer's stated operating conditions;
  they are not guaranteed minimum/maximum bounds for our assembled device.
- **Maximum** and **peak** keep their original meanings. A peak during radio
  transmission or acquisition is not the average current.
- Currents belong to the listed supply pin/rail. Do not add currents from
  different voltages to predict battery current. For a power budget use
  `P = V × I`, converter efficiency, duty cycles and PMU overhead.
- A disabled firmware feature can still have an electrically powered chip.
  `UNAVAILABLE` means the identified device or its relevant specification is
  missing; it does not mean zero consumption.

## ESP32-S3, radios and memory

Reference: [Espressif v2.2][esp32], tables 1-1, 5-7 to 5-10, pp. 13, 66-68;
radio conditions: 3.3 V, 25 °C. Values exclude board loads.

| Component / mode | Datasheet current | Applicability |
| --- | --- | --- |
| CPU, radios gated, 80-240 MHz | **22.0-107.9 mA typical** | Workload/clocks vary; flash/PSRAM extra. |
| Wi-Fi RX | **88-91 mA peak** | HT20/HT40; CPU idle, peripherals off. |
| Wi-Fi TX | **283-340 mA peak** | Listed rates/powers; 100% duty. |
| BLE RX / TX | **93 / 116-335 mA peak** | TX -15 to +21 dBm. |
| Light-sleep | **240 µA typical** | VDD_SPI/Wi-Fi off; GPIOs high-impedance; excludes PSRAM. |
| Deep-sleep | **7-8 µA typical** | RTC memory retained; RTC peripherals off/on. |
| Running ULP | **170 / 190 µA typical** | FSM/RISC-V; whole-mode current. |
| Periodic ULP example | **18 µA typical** | Internal touch, 1% duty; not CST9217. |
| Hardware shutdown | **1 µA typical** | CHIP_PU low. |
| R8 8 MB octal PSRAM | **140 µA typical Light-sleep addition**, 3.3 V | Active/Deep-sleep range unavailable. |
| W25Q128JVSIQ 16 MB external flash | **8/10/12 mA typical read at 50/80/104 MHz**, maxima **15/18/20 mA**; program/erase **20 mA typical, 25 mA max** | Schematic U2; values from Winbond Rev F. |
| Flash standby / power-down | **10 µA typical, 60 µA max** / **1 µA typical, 20 µA max** | CS high and inputs at rails. Power-down requires the appropriate memory state. |

Wi-Fi and BLE are inside the ESP32. Their rows describe alternative ESP32 RF
operating conditions; do not sum them with the CPU baseline as independent chips.
Ideal Light-sleep MCU/PSRAM subtotal: **380 µA**, calculated from the two rows;
this is not a board-current prediction.
Flash values: [Winbond W25Q128JV Rev F][flash], section 9.4, printed p. 62.
Release permits automatic Light-sleep; it does not establish these datasheet
power-domain conditions on our firmware or enter Deep-sleep automatically.

## Sensors, display, audio and power

| Component | Active / working reference | Low-power reference | Conditions and board limits | Source |
| --- | --- | --- | --- | --- |
| CST9217 touch | **1.3 mA typical**, 100 Hz | Monitor **12 µA typical**, 30 Hz; sleep **2 µA typical** | 2.8 V, 25 °C; PCB supply 3.3 V. Monitor detects touch; sleep needs a wake command or reset and cannot provide touch wake. | [Hynitron datasheet][touch], §9.1 / §10.2, pp. 4, 6-7 |
| QMI8658 IMU, accelerometer + gyro | **750-1031 µA typical**, 31.25-8000 Hz; at 500 Hz **757-758 µA** | Current firmware's 31.25 Hz six-axis profile: **750 µA typical reference** | VDD = VDDIO = 1.8 V, 25 °C; PCB supplies 3.3 V. This is not a measured 3.3 V result. | [QST Rev 0.6][imu], tables 14-16, printed pp. 19-20 |
| QMI8658 accelerometer only | High-resolution **132-182 µA typical**, 31.25-1000 Hz | Low-power **30/35/42/55 µA typical** at 3/11/21/128 Hz; power-down **20 µA typical** | Same 1.8 V reference. Wake-on-Motion uses accelerometer only; power-down has no motion sensing. | [QST Rev 0.6][imu], tables 14 and 31, pp. 19, 42 |
| LC76G **AB** GNSS | VCC: acquisition/tracking **36 mA typical, 62 mA peak**; ALP **13 mA typical, 43 mA peak** | V_BCKP in Backup: **13 µA typical, 53 µA peak** | 3.3 V, room temperature. Active V_BCKP adds **125-130 µA typical**, up to 191 µA peak. GPS power modes require specific transitions; see below. | [Quectel V1.2][gnss], table 14, printed p. 47 |
| PCF85063ATL RTC | I2C active, 400 kHz: **18 µA typical, 50 µA max** | Clock running, bus idle: **0.22-0.47 µA typical**, maxima **0.45-0.60 µA**, 25-85 °C | 3.3 V, CLKOUT off, specified quartz/load. VCC-RTC remains a separate supply. | [NXP Rev 7][rtc], table 41, p. 39 |
| TCA9554 I/O expander | **15 µA typical** at 3.6 V, 400 kHz, unloaded input ports | All inputs high: **1.1 µA typical, 1.8 µA max**; all inputs low: **0.30 mA typical, 0.60 mA max** at 3.6 V | PCB 3.3 V; internal input pull-ups, external pull-ups and output loads matter. Low inputs can dominate the chip's idle current. | [TI Rev E][expander], §6.5, p. 7 |
| AXP2101 PMU / charger / regulators | Whole enabled-PMU overhead **UNAVAILABLE** under our rail/ADC/charger settings | **40 µA typical IBAT**, no VBUS, BATFET disabled, only RTCLDO on | This is PMU shutdown overhead, not Device Standby overhead or total board current. Charging and supplied loads are separate. | [X-Powers V1.0][pmu], table 5-5, p. 10 |
| CO5300 display driver | Complete active-panel range **UNAVAILABLE** | IC deep standby: **2 µA VDDI + 1 µA VCI typical**; DSI Sleep In: **85-110 µA VDDI + 25 µA VCI typical** | 25 °C; driver-only reference. DSI conditions do not establish our QSPI current; DISP OFF does not prove deep standby. | [Chipone V0.01][display], §6.2, p. 103 |
| 1.75-inch AMOLED emitting panel/module | **UNAVAILABLE** for the exact Waveshare assembly | Module sleep current **UNAVAILABLE** | Requires exact panel power specification or measurements with stated brightness, image, refresh and supply. CO5300 values exclude the emitting panel/module budget. | [Waveshare hardware resources][resources] |
| ES8311 output codec | **8 mA typical** | Datasheet lists **0 µA typical**, no maximum | DVDD/PVDD 1.8 V, AVDD 3.3 V. PCB digital and analog rails are 3.3 V. Do not treat the rounded zero or codec close as proven zero board current. | [Everest Rev 10.0][codec], DC characteristics, p. 12 |
| ES7210 four-channel input ADC | **24-63 mW typical**, Fs 16 kHz | **10 µA typical**, no maximum | Digital/I/O 1.8 V, analog 1.8/3.3 V. PCB rails 3.3 V; 22.05 kHz runtime differs. 63 mW / 3.3 V = **19.1 mA equivalent input** only, not a specified board current. | [Everest Rev 22.0][adc], DC characteristics, p. 8 |
| NS4150B speaker amplifier | **3 mA typical** quiescent, unloaded; playback adds speaker power | CTRL low: **0.1 µA typical, 10 µA max** | 3.6 V, 25 °C; PCB 3.3 V. 90% efficiency specified at 0.6 W/8 Ω/3.6 V; not a constant for all volume levels. | [Nsiway V1.1][amplifier], §7, p. 3 |
| Speaker | Passive load; playback range **UNAVAILABLE** | No separate powered standby circuit | Exact impedance/rating not identified in the schematic. Its electrical power is delivered by NS4150B; do not count it twice. | [Waveshare schematic][schematic], H3 |
| Dual analog microphones, MIC1/MIC2 | **UNAVAILABLE**, part numbers not published in schematic | **UNAVAILABLE** | Powered by ES7210 MICBIAS12. Include microphone/bias loads when measuring audio rails. | [Waveshare schematic][schematic], MIC1/MIC2 |
| User-added 8 GB microSD | **UNAVAILABLE**, manufacturer/model not recorded | **UNAVAILABLE** | Capacity does not identify power requirements. SDMMC deinitialization does not remove VCC3V3. | [Local integration inventory](docs/hardware/hardware-inventory.md) |
| Optional two TOF400F UART modules | **40 mA max each; 80 mA max pair** | Module standby **UNAVAILABLE** | Module supply 3.0-5.0 V; wired to 3.3 V. UART suspension does not cut VIN; exclude if absent. | [TOF400F module datasheet][tof], §2.2, p. 2 |
| Antennas, quartz, connectors, passives, BSS138 and USB TVS | No standalone active-current sum available | Leakage / static pull-up paths **UNAVAILABLE as an aggregate** | Add converter/termination and leakage losses once. A low line through a 3.3 V / 2.2 kΩ pull-up draws **1.5 mA while low** (calculation from schematic), not continuously. | [Waveshare schematic][schematic] |
| Battery | Supplies loads; cell self-discharge **UNAVAILABLE** | Capacity/chemistry-specific self-discharge **UNAVAILABLE** | No verified cell model/capacity for a runtime estimate. Firmware charge settings are not device consumption. | [Local integration inventory](docs/hardware/hardware-inventory.md) |

The touch and amplifier references are manufacturer-authored datasheets hosted
by third parties. Their locations and revisions are explicit; they are not
third-party estimates. The TOF400F module sheet does not identify its author;
treat its 40 mA maximum as a module-document reference pending supplier/revision
confirmation, rather than a verified specification for the user's two units.

## Deep-sleep and ULP: how this PCB can wake

**ULP is a small coprocessor that can run during Deep-sleep**, not another sleep
mode below it. Light-sleep resumes execution; Deep-sleep wakes through a boot
path and needs runtime reinitialization. The external touchscreen is not the
ESP32's internal capacitive touch wake peripheral. ESP32-S3 RTC-capable GPIOs
0-21 support external sleep wakeup. See [ESP-IDF 6.0 sleep documentation][sleep]
(6.0-series reference; this repository targets 6.0.2).

| Requested wake source | Schematic route | What is possible | Current firmware |
| --- | --- | --- | --- |
| BOOT button | GPIO0 to GND | Direct RTC GPIO wake | Explicit sleep helper enables EXT0 low; optional timer. |
| Touch | CST9217 TP_INT → GPIO11 | External GPIO wake while touch scanning remains powered; monitor mode is the low-current candidate | Touch IRQ is used by input; not configured as a Deep-sleep source. |
| IMU movement | QMI8658 INT2 → GPIO21; INT1 → TCA9554 P6 | Configure accelerometer-only WoM on INT2, polarity, threshold and latch handling, then RTC GPIO wake | Software motion sampling; no configured hardware WoM/Deep-sleep route. |
| PWR short press | AXP2101 PWRON; AXP_IRQ → TCA9554 P5 | No direct ESP RTC wake on the published PCB: TCA9554 INT is only pulled high, not wired to ESP | PMU polling wakes software Device Standby; polling stops in Deep-sleep. |
| RTC alarm | PCF85063 INT → TCA9554 P3 | No direct RTC-alarm GPIO wake through the unwired expander INT | RTC values are acquired; use ESP sleep timer for a direct timed wake candidate. |
| PWR after PMU shutdown | Hardware PMU key / PWROK → ESP CHIP_PU | AXP2101 power-on sequence can restore power and boot; touch/IMU cannot wake with their common rail off | Separate product behavior requiring an explicit PMU shutdown/startup implementation; not Device Standby. |

Routes: [published board schematic][schematic], sheet 1, AXP2101, I/O Extensions,
RTC, 6/9-Axis and connector sections. WoM interrupt selection/polarity:
[QMI8658C §9 and table 33][imu]. An electrically suitable route is not proof of
working firmware wakeup. IRQ retention, touch monitor support in the actual
panel firmware, sensor identity/revision and boot restoration still need work.

For touch plus motion, align both interrupt active levels before using EXT1.
Keep the selected sensors powered, quiesce other owners, clear old interrupt
conditions, then arm wakeup. After wake, reinitialize owners and publish only
fresh successful acquisitions through `DeviceSnapshotService`.

PWR could be addressed by a future IRQ-to-RTC-GPIO PCB link, or a new ULP
acquisition path that polls the PMU on the shared RTC-capable I2C pins. The
latter needs reviewed bus ownership and a measured duty-cycle budget; ordinary
FreeRTOS polling cannot run in Deep-sleep. These are design candidates.

## What matters for the complete device

The current six-axis standby rate reduction has little sensor-current benefit
in the reference table. Disabling the gyro and selecting accelerometer-only
WoM is the meaningful low-current candidate; retained UI/boot state and wake
latency must then be designed explicitly.

LC76GABMD VCC shares VCC3V3, while V_BCKP is on VCC-RTC; STANDBY is unconnected.
The documented lowest-power sequence requires `$PAIR650`, then VCC removal
with backup retained. Quectel also documents an AB-only command entry while
VCC stays present, with RESET_N needed to exit. That note does not establish
the full module current on both powered rails. Current `$PAIR003` behavior
is not evidence of the 13 µA Backup reference. See [Quectel §3.3.4][gnss].

All wake sensors, GNSS, SD and amplifier share the principal 3.3 V rail; audio
ADC/codec use A3V3. Turning off the common rail also removes touch/IMU wake.
The PMU 40 µA shutdown figure therefore cannot simply be combined with the
touch/IMU monitoring figures as a realizable board state.

**Total board standby/Deep-sleep current and battery life remain UNAVAILABLE.**
The panel, microphones, SD identity, enabled-PMU overhead and exact rail states
prevent a defensible complete range. A useful future measurement records USB
versus battery supply, charge state, image/brightness, radios, GNSS state,
inserted card, attached TOFs, wake-source configuration and firmware identity.
No physical measurement is authorized or performed by this documentation task.

[esp32]: https://documentation.espressif.com/esp32_s3_datasheet_en.pdf
[flash]: https://www.winbond.com/resource-files/w25q128jv%20revf%2003272018%20plus.pdf
[touch]: https://github.com/ScripTerasu/cst92xx-touch-driver/blob/4470eaaf6b4a53b4417df06eb04bbab799bacdac/docs/CST9217.pdf
[imu]: https://files.waveshare.com/wiki/common/QMI8658C.pdf
[gnss]: https://quectel.com/content/uploads/2024/02/Quectel_LC76G_Series_Hardware_Design_V1.2.pdf
[rtc]: https://files.waveshare.com/wiki/common/PCF85063A.pdf
[expander]: https://www.ti.com/lit/ds/symlink/tca9554.pdf
[pmu]: https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf
[display]: https://files.waveshare.com/wiki/common/Co5300_Datasheet.pdf
[codec]: https://files.waveshare.com/wiki/common/ES8311.DS.pdf
[adc]: https://files.waveshare.com/wiki/common/ES7210-datasheet.pdf
[amplifier]: https://storage.googleapis.com/graviton-electric-symbols/document_assets/lcsc/2209161630_Shenzhen-Nsiway-Tech-NS4150B_C189961.pdf
[tof]: https://raw.githubusercontent.com/andreas40239/TOF400F/main/TOF400F%20DATASHEET_20210108.pdf
[schematic]: https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf
[resources]: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/Resources-And-Documents
[sleep]: https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/system/sleep_modes.html
