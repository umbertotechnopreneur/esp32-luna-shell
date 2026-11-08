#include "LUNASHELL_acquisition_i2c.hpp"

#include <cstring>

namespace LUNASHELL::snapshot_detail::acquisition_i2c {
// bus: Shared BSP bus retained by its owner.
// address: Seven-bit chip address.
// frequency_hz: Device transaction frequency.
// device: Component-owned admitted handle.
// Exception: Returns the original ESP-IDF admission error.
esp_err_t add_i2c_device(
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t frequency_hz,
    i2c_master_dev_handle_t *device
)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = frequency_hz,
        .scl_wait_us = 20000,
        .flags = {},
    };
    return i2c_master_bus_add_device(bus, &config, device);
}

// device: Component-owned I2C handle.
// reg: First register to acquire.
// data: Caller-owned output buffer.
// length: Requested byte count.
// Exception: Returns the original ESP-IDF read error.
esp_err_t read_register(
    i2c_master_dev_handle_t device,
    uint8_t reg,
    uint8_t *data,
    size_t length
)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, length, kTimeoutMs);
}

// device: Component-owned I2C handle.
// reg: Register receiving the byte.
// value: Exact chip-owner value.
// Exception: Returns the original ESP-IDF write error.
esp_err_t write_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t payload[] = {reg, value};
    return i2c_master_transmit(device, payload, sizeof(payload), kTimeoutMs);
}

// device: Component-owned I2C handle.
// reg: First register receiving the payload.
// data: Borrowed caller payload.
// length: Payload count within the existing fifteen-byte bound.
// Exception: Returns rejected admission or the original ESP-IDF write error.
esp_err_t write_registers(
    i2c_master_dev_handle_t device,
    uint8_t reg,
    const uint8_t *data,
    size_t length
)
{
    if (device == nullptr || data == nullptr || length == 0 || length > 15) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t payload[16] = {reg};
    std::memcpy(payload + 1, data, length);
    return i2c_master_transmit(device, payload, length + 1, kTimeoutMs);
}

}  // namespace LUNASHELL::snapshot_detail::acquisition_i2c
