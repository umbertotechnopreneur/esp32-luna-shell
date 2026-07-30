# Dual TOF400F UART wiring

This device configuration uses two **TOF400F VL53L1X 4 m** modules in their
factory UART/Modbus mode. No mode jumper, address change, I2C wiring or UART0
header pins are required.

## Wiring

| ESP32-S3 Touch AMOLED 1.75 header | TOF400F module 1 | TOF400F module 2 |
| --- | --- | --- |
| `3V3` | `VIN` | `VIN` |
| `GND` | `GND` | `GND` |
| `GPIO16` | `RX` | `RX` |
| `GPIO17` | `TX` | — |
| `GPIO18` | — | `TX` |

The shared line is safe because it drives the two module **RX inputs**. The two
module **TX outputs must never be connected together**; they terminate on the
two independent ESP32 receive pins. Module `SDA` and `SCL` remain disconnected.
Trust the module pin labels over wire colours if a supplied harness differs.
The data-sheet harness uses red=`VIN`, black=`GND`, white=module `RX`,
green=module `TX`, yellow=`SDA`, and blue=`SCL`.

UART0 `U0RXD`/`U0TXD` remains reserved for the firmware console. The driver
uses UART1 with TX GPIO16/RX GPIO17 and UART2 as RX-only on GPIO18.

Power both modules from `3V3`, not `VBUS`, so their UART high level remains
compatible with the ESP32-S3. The pair can draw up to approximately 80 mA.

## Board check before connecting

The Waveshare schematic shows optional zero-ohm links `R15`/`R16` between
GPIO17/GPIO18 and the LC76G UART. Confirm those links are not populated on the
actual board before attaching the two TOF transmitters. The firmware uses the
LC76G through its I2C bridge, but populated links would still create an
electrical conflict on the external GPIO header.

## Protocol owned by the driver

- 115200 baud, 8 data bits, no parity, 1 stop bit;
- Modbus RTU address 1;
- distance register `0x0010`;
- long-range register `0x0004 = 1`;
- autonomous serial output disabled with `0x0005 = 0`;
- CRC-16/MODBUS validation on every reply.

Both modules may retain address 1 because a request is transmitted once and
their replies arrive on physically separate receive lines.

## Synchronized measurement contract

The acquisition worker timestamps the shared request immediately after TX
completes, then keeps the independent parse timestamp from each receiver. A
pair enters calibration or runtime filtering only when:

- the two receiver timestamps differ by no more than 50 ms;
- the fresh IMU and fusion states use the same source timestamp, with at most
  50 ms allowed between them; and
- the request, both receiver timestamps, IMU and fusion all fit in one 500 ms
  acquisition envelope.

After a valid TOF pair, the worker samples only the IMU/fusion geometry path
under the canonical hardware refresh lock. It does not copy or reuse the full
device snapshot. A missing frame, stale/failed geometry sample, excessive
skew, unsupported posture or corrected-distance error rejects the complete
pair and clears both filter windows. Calibration follows the same pair rule:
both accepted counts advance together, and either-side failure resets both
calibration windows.

One-point calibration records are versioned and CRC-protected. Version 2 also
stores the sign of the calibrated IMU Z axis. Runtime accepts only that same
mounting sign, so turning the assembly upside down cannot produce a plausible
distance from `abs(z)`. A recognized version-1 record is not migrated or
overwritten; it is reported as calibration required. Read, header and CRC
failures remain real storage errors.

## Memory and recovery behavior

The four 500-sample windows live in PSRAM. Estimator scratch space and the
worker itself use the dedicated PSRAM task stack, whose minimum observed free
bytes are published in the distance snapshot. Each sensor also publishes
frame, CRC, protocol, timeout and UART counters.

When the pair is absent or stops responding, no prior or synthetic distance is
reused. The service clears both windows and increases its polling delay through
1, 2, 4, 8, 16 and 30 seconds, capped at 30 seconds. The driver independently
marks a repeatedly failing pair unconfigured and retries its verified
configuration after its own recovery guard. Successful acquisition resets the
service delay. If worker creation fails, both UARTs are explicitly deinitialized
and any cleanup error is published and logged.

An active calibration aborts after three consecutive transport failures and
publishes the real transport error. Independently, its wall-clock budget is
twice the requested sample count times the configured sample interval, plus a
30-second margin. Deadline expiry publishes `ESP_ERR_TIMEOUT`. Either terminal
path restores `Ready` when a prior valid calibration record exists, or enters
`Failed` otherwise, so sample-count, interval and reference setters are no
longer held behind a stale `Running` state.

References:

- [TOF400F module data sheet](https://makerselectronics.com/wp-content/uploads/2025/08/20210224033328TOF400F.pdf)
- [Waveshare board wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)
- [Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf)
