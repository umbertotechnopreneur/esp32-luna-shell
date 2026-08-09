# Planned work

This file tracks deferred work that is worth retaining but is not a current
priority. Promote an item to `TODO.md` when it becomes active.

## Power management

- [ ] Revalidate at least five consecutive AXP2101 PKEY standby/wake cycles on
  the physical device with serial evidence.
- [ ] Measure active, screen-off, and Device Standby current with USB data
  disconnected, first with Wi-Fi/BLE off and then with each radio enabled.
- [ ] Evaluate `WIFI_PS_MAX_MODEM` against the current `WIFI_PS_MIN_MODEM`
  policy using measured current, latency, and connection reliability.
- [ ] Evaluate a 40 MHz minimum CPU frequency and CPU-domain Light-sleep power
  down using latency, internal-memory, stability, and current measurements.
- [ ] In a future PCB revision, route the AXP2101/TCA9554 interrupt to an ESP32-S3
  RTC GPIO so PWR can wake asynchronously without the 100 ms software poll.
