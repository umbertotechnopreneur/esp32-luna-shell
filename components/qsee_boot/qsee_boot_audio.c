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

// Boot-time audio cue playback for the QSee startup flow.
// Plays the startup sound after the audio path becomes available.

#include "qsee_boot.h"
#include "qsee_platform.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

extern const uint8_t qsee_boot_wav_start[] asm("_binary_qsee_boot_wav_start");
extern const uint8_t qsee_boot_wav_end[] asm("_binary_qsee_boot_wav_end");

static const char *TAG = "qsee_audio";
static StaticTask_t s_audio_task_control;
static StackType_t s_audio_task_stack[3072];
static StaticSemaphore_t s_audio_done_control;
static SemaphoreHandle_t s_audio_done;
static volatile esp_err_t s_audio_result;

static void audio_task(void *arg)
{
    (void)arg;
    const size_t wav_size = (size_t)(qsee_boot_wav_end - qsee_boot_wav_start);
    if (wav_size <= 44U) {
        qsee_hardware_report_speaker(false, ESP_ERR_INVALID_SIZE);
        ESP_LOGW(TAG, "Boot SFX asset is invalid");
        s_audio_result = ESP_ERR_INVALID_SIZE;
        xSemaphoreGive(s_audio_done);
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
    s_audio_result = result;
    xSemaphoreGive(s_audio_done);
    vTaskDelete(NULL);
}

esp_err_t qsee_boot_audio_play(void)
{
    s_audio_done = xSemaphoreCreateBinaryStatic(&s_audio_done_control);
    if (s_audio_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    TaskHandle_t task = xTaskCreateStatic(
        audio_task,
        "qsee_boot_sfx",
        sizeof(s_audio_task_stack) / sizeof(s_audio_task_stack[0]),
        NULL,
        3,
        s_audio_task_stack,
        &s_audio_task_control
    );
    if (task == NULL) {
        return ESP_ERR_NO_MEM;
    }

    (void)xSemaphoreTake(s_audio_done, portMAX_DELAY);
    return s_audio_result;
}
