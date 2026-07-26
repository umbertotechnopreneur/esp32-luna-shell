/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "qsee_platform.h"

extern "C" esp_err_t qsee_provisioning_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_provisioning_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_provisioning_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_provisioning_deinit_for_ble(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" bool qsee_provisioning_initialized(void)
{
    return false;
}

extern "C" bool qsee_provisioning_active(void)
{
    return false;
}

extern "C" qsee_provisioning_state_t qsee_provisioning_state(void)
{
    return QSEE_PROVISIONING_STATE_UNAVAILABLE;
}

extern "C" esp_err_t qsee_provisioning_connection_summary(
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

extern "C" const char *qsee_provisioning_service_name(void)
{
    return "";
}

extern "C" const char *qsee_provisioning_qr_payload(void)
{
    return "";
}

extern "C" const char *qsee_provisioning_pop(void)
{
    return "";
}
