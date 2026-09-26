/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"

extern "C" esp_err_t LUNASHELL_power_manager_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" void LUNASHELL_power_manager_note_activity(void)
{
}

extern "C" esp_err_t LUNASHELL_power_manager_set_leg_mode(
    bool active,
    bool wake_on_motion
)
{
    (void)active;
    (void)wake_on_motion;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_power_manager_set_leg_sounds_enabled(bool enabled)
{
    (void)enabled;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" void LUNASHELL_power_manager_note_ui_standby_result(
    uint32_t transition_sequence,
    esp_err_t result
)
{
    (void)transition_sequence;
    (void)result;
}

extern "C" esp_err_t LUNASHELL_power_manager_set_timeout(
    LUNASHELL_power_timeout_t timeout,
    uint32_t seconds
)
{
    (void)timeout;
    (void)seconds;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_power_manager_get_timeout(
    LUNASHELL_power_timeout_t timeout,
    uint32_t *seconds
)
{
    (void)timeout;
    if (seconds != nullptr) {
        *seconds = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_power_enter_light_sleep(uint64_t wake_after_us)
{
    (void)wake_after_us;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_power_enter_deep_sleep(uint64_t wake_after_us)
{
    (void)wake_after_us;
    return ESP_ERR_NOT_SUPPORTED;
}
