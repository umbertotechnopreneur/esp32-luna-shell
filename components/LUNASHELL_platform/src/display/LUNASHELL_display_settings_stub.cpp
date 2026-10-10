/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 * Original file notice:
 *
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * VBWR E */


#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

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

// No parameters: a profile without a display cannot provide a dark frame gate.
extern "C" esp_err_t LUNASHELL_display_prepare_dark_frame(void)
{
    LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown,
        ESP_ERR_NOT_SUPPORTED);
    ESP_LOGE("LUNASHELL_display_settings", "Dark-frame preparation unavailable (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
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
