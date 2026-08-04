/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

extern "C" esp_err_t qsee_ble_init(void)
{
    qsee::DeviceSnapshotService::instance().reportBle(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_radios_init(void)
{
    qsee::WifiSnapshot wifi{};
    wifi.last_error = ESP_ERR_NOT_SUPPORTED;
    wifi.health = qsee::DeviceHealth::Missing;
    qsee::DeviceSnapshotService::instance().reportWifi(wifi);
    qsee::DeviceSnapshotService::instance().reportBle(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_radios_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}
