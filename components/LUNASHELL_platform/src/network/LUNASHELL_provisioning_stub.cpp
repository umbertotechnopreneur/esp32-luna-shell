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

extern "C" esp_err_t LUNASHELL_provisioning_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_provisioning_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_provisioning_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_provisioning_deinit_for_ble(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" bool LUNASHELL_provisioning_initialized(void)
{
    return false;
}

extern "C" bool LUNASHELL_provisioning_active(void)
{
    return false;
}

extern "C" LUNASHELL_provisioning_state_t LUNASHELL_provisioning_state(void)
{
    return LUNASHELL_PROVISIONING_STATE_UNAVAILABLE;
}

extern "C" LUNASHELL_provisioning_failure_reason_t
LUNASHELL_provisioning_failure_reason(void)
{
    return LUNASHELL_PROVISIONING_FAILURE_UNKNOWN;
}

extern "C" esp_err_t LUNASHELL_provisioning_connection_summary(
    char *buffer,
    size_t buffer_size
)
{
    if (buffer == nullptr || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = '\0';
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" const char *LUNASHELL_provisioning_service_name(void)
{
    return "";
}

extern "C" const char *LUNASHELL_provisioning_qr_payload(void)
{
    return "";
}

extern "C" const char *LUNASHELL_provisioning_pop(void)
{
    return "";
}

// buffer: Caller-owned destination, cleared when available.
// buffer_size: Destination capacity including a null byte.
// session_id: Unused identity in a profile without provisioning.
extern "C" esp_err_t LUNASHELL_provisioning_copy_qr(
    char *buffer, size_t buffer_size, uint32_t session_id)
{
    (void)session_id;
    if (buffer == nullptr || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer[0] = '\0';
    return ESP_ERR_NOT_SUPPORTED;
}
