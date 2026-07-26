/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "qsee_boot.h"
#include "qsee_platform.h"

esp_err_t qsee_boot_audio_play(void)
{
    qsee_hardware_report_speaker(false, ESP_ERR_NOT_SUPPORTED);
    return ESP_ERR_NOT_SUPPORTED;
}
