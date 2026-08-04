/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_boot.h"
#include "LUNASHELL_platform.h"

esp_err_t LUNASHELL_boot_audio_play(void)
{
    LUNASHELL_hardware_report_speaker(false, ESP_ERR_NOT_SUPPORTED);
    return ESP_ERR_NOT_SUPPORTED;
}
