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
#include "esp_log.h"

// enabled: Unsupported motion policy in a build without the power owner.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_power_manager_set_shake_wake_enabled(bool enabled)
{
    (void)enabled;
    return ESP_ERR_NOT_SUPPORTED;
}

// callback: Unsupported display lifecycle registration for a profile without power inputs.
extern "C" esp_err_t LUNASHELL_power_manager_register_display_lifecycle(
    LUNASHELL_display_lifecycle_callback_t callback)
{
    (void)callback;
    return ESP_ERR_NOT_SUPPORTED;
}

// prepare: Excluded profile cannot register an offline farewell owner.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_power_manager_register_offline_farewell(esp_err_t (*prepare)(void))
{
    (void)prepare;
    return ESP_ERR_NOT_SUPPORTED;
}

// prepare: Excluded profile cannot register a farewell owner.
// report_error: Excluded profile cannot restore a failure presentation.
// Exception: Returns ESP_ERR_NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_power_manager_register_shutdown_callbacks(
    esp_err_t (*prepare)(void), esp_err_t (*report_error)(esp_err_t))
{
    (void)prepare;
    (void)report_error;
    return ESP_ERR_NOT_SUPPORTED;
}

// seal: Excluded profile cannot coordinate speech shutdown.
// Exception: Returns ESP_ERR_NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_power_manager_register_speech_shutdown(esp_err_t (*seal)(bool))
{
    (void)seal;
    return ESP_ERR_NOT_SUPPORTED;
}

// No parameters: this profile has no power transition owner or retained trace.
extern "C" void LUNASHELL_power_manager_report_boot_trace(void)
{
    ESP_LOGW("LUNASHELL_power", "Previous standby trace unavailable: power owner excluded");
}

extern "C" esp_err_t LUNASHELL_power_manager_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" void LUNASHELL_power_manager_note_activity(void)
{
}

// No parameters: excluded power profile has no active countdown policy.
extern "C" void LUNASHELL_power_manager_note_power_activity(void)
{
}

// No parameters: excluded power profile has no notification wake owner.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_power_manager_note_codex_notification(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// No parameters: excluded power profile cannot admit a Codex restart.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_power_manager_request_codex_restart(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// No parameters: excluded power profile cannot perform a factory reset.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_power_manager_request_codex_factory_reset(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// seconds: Unsupported transient override in a profile without the power owner.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED for this excluded feature.
extern "C" esp_err_t LUNASHELL_power_manager_set_scene_standby_override(uint32_t seconds)
{
    (void)seconds;
    return ESP_ERR_NOT_SUPPORTED;
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
