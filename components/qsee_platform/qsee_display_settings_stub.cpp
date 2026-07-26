/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

extern "C" esp_err_t qsee_display_set_brightness_percent(uint32_t percent)
{
    (void)percent;
    qsee::DeviceSnapshotService::instance().reportDisplayBrightness(
        0,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_display_get_brightness_percent(uint32_t *percent)
{
    if (percent != nullptr) {
        *percent = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_display_apply_saved_brightness(void)
{
    qsee::DeviceSnapshotService::instance().reportDisplayBrightness(
        0,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_display_set_power_mode(qsee_display_power_mode_t mode)
{
    (void)mode;
    qsee::DeviceSnapshotService::instance().reportDisplayPowerMode(
        qsee::UiHardwareSnapshot::DisplayPowerMode::Unknown,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}
