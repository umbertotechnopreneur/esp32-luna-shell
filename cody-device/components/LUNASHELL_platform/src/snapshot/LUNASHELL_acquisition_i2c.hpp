#pragma once

#include "driver/i2c_master.h"

namespace LUNASHELL::snapshot_detail::acquisition_i2c {
inline constexpr int kTimeoutMs = 100;
// bus: Shared BSP bus; ownership remains with the BSP.
// address: Exact seven-bit device address.
// frequency_hz: Chip-specific bus frequency.
// device: Component-owned output handle.
// Exception: Returns the original ESP-IDF device admission error.
esp_err_t add_i2c_device(i2c_master_bus_handle_t bus, uint8_t address,
                         uint32_t frequency_hz, i2c_master_dev_handle_t *device);
// device: Component-owned handle for this transaction.
// reg: Register address sent before the read.
// data: Caller-owned bounded output buffer.
// length: Number of requested bytes.
// Exception: Returns the original ESP-IDF transaction error.
esp_err_t read_register(i2c_master_dev_handle_t device, uint8_t reg,
                        uint8_t *data, size_t length);
// device: Component-owned handle for this transaction.
// reg: Register address receiving the value.
// value: Exact byte requested by the chip owner.
// Exception: Returns the original ESP-IDF transaction error.
esp_err_t write_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value);
// device: Component-owned handle for this transaction.
// reg: First register receiving the contiguous payload.
// data: Caller-owned payload; no staging snapshot is copied.
// length: Payload length, bounded to the existing fifteen-byte limit.
// Exception: Returns invalid admission or the original ESP-IDF write error.
esp_err_t write_registers(i2c_master_dev_handle_t device, uint8_t reg,
                         const uint8_t *data, size_t length);
}  // namespace LUNASHELL::snapshot_detail::acquisition_i2c
