/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "qsee_platform.h"

extern "C" esp_err_t qsee_power_inputs_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_power_enter_light_sleep(uint64_t wake_after_us)
{
    (void)wake_after_us;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_power_enter_deep_sleep(uint64_t wake_after_us)
{
    (void)wake_after_us;
    return ESP_ERR_NOT_SUPPORTED;
}
