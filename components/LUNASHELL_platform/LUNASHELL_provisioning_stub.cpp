/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

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
