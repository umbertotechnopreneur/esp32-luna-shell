/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"

extern "C" esp_err_t LUNASHELL_ble_init(void)
{
    LUNASHELL::DeviceSnapshotService::instance().reportBle(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

// No parameters: radio-less profiles cannot reserve BLE controller DMA.
extern "C" esp_err_t LUNASHELL_radios_reserve_startup_dma(void)
{
    LUNASHELL::DeviceSnapshotService::instance().reportBle(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_radios_init(void)
{
    LUNASHELL::WifiSnapshot wifi{};
    wifi.last_error = ESP_ERR_NOT_SUPPORTED;
    wifi.health = LUNASHELL::DeviceHealth::Missing;
    LUNASHELL::DeviceSnapshotService::instance().reportWifi(wifi);
    LUNASHELL::DeviceSnapshotService::instance().reportBle(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_radios_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_radios_start_pairing(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// restore_telemetry: Unused in a profile without Bluetooth.
extern "C" esp_err_t LUNASHELL_radios_end_pairing(bool restore_telemetry)
{
    (void)restore_telemetry;
    return ESP_ERR_NOT_SUPPORTED;
}
