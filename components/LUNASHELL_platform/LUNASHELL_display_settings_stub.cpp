/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"

extern "C" esp_err_t LUNASHELL_display_set_brightness_percent(uint32_t percent)
{
    (void)percent;
    LUNASHELL::DeviceSnapshotService::instance().reportDisplayBrightness(
        0,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_display_get_brightness_percent(uint32_t *percent)
{
    if (percent != nullptr) {
        *percent = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_display_apply_saved_brightness(void)
{
    LUNASHELL::DeviceSnapshotService::instance().reportDisplayBrightness(
        0,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_display_set_power_mode(LUNASHELL_display_power_mode_t mode)
{
    (void)mode;
    LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown,
        ESP_ERR_NOT_SUPPORTED
    );
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_display_fade_to_power_mode(
    LUNASHELL_display_power_mode_t mode,
    uint32_t duration_ms
)
{
    (void)duration_ms;
    return LUNASHELL_display_set_power_mode(mode);
}
