/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

#include "qsee_boot.h"
#include "qsee_platform.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const uint8_t qsee_boot_wav_start[] asm("_binary_qsee_boot_wav_start");
extern const uint8_t qsee_boot_wav_end[] asm("_binary_qsee_boot_wav_end");

static const char *TAG = "qsee_audio";
static StaticTask_t s_audio_task_control;
static StackType_t s_audio_task_stack[3072];

static void audio_task(void *arg)
{
    (void)arg;
    const size_t wav_size = (size_t)(qsee_boot_wav_end - qsee_boot_wav_start);
    if (wav_size <= 44U) {
        qsee_hardware_report_speaker(false, ESP_ERR_INVALID_SIZE);
        ESP_LOGW(TAG, "Boot SFX asset is invalid");
        vTaskDelete(NULL);
        return;
    }

    const esp_err_t result = qsee_audio_play_pcm16(
        (const int16_t *)(qsee_boot_wav_start + 44),
        (wav_size - 44U) / sizeof(int16_t),
        22050,
        90
    );

    if (result == ESP_OK) {
        qsee_hardware_report_speaker(true, ESP_OK);
        ESP_LOGI(TAG, "Boot SFX audible route completed: 22.05kHz mono PCM16 at volume 90");
    } else {
        qsee_hardware_report_speaker(false, result);
        ESP_LOGW(TAG, "Boot SFX playback failed (%s)", esp_err_to_name(result));
    }
    vTaskDelete(NULL);
}

esp_err_t qsee_boot_audio_play(void)
{
    TaskHandle_t task = xTaskCreateStatic(
        audio_task,
        "qsee_boot_sfx",
        sizeof(s_audio_task_stack) / sizeof(s_audio_task_stack[0]),
        NULL,
        3,
        s_audio_task_stack,
        &s_audio_task_control
    );
    return task != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}
