/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

// Boot-time audio cue playback for the startup flow.
// Plays the startup sound after the audio path becomes available.

#include "qsee_boot.h"
#include "qsee_platform.h"

#include <stddef.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

extern const uint8_t qsee_boot_wav_start[] asm("_binary_qsee_boot_wav_start");
extern const uint8_t qsee_boot_wav_end[] asm("_binary_qsee_boot_wav_end");

static const char *TAG = "qsee_audio";
static StaticSemaphore_t s_audio_done_control;
static SemaphoreHandle_t s_audio_done;
static volatile esp_err_t s_audio_result;
static atomic_bool s_audio_started;

static void signal_audio_complete(void)
{
    if (xSemaphoreGive(s_audio_done) != pdTRUE) {
        ESP_LOGE(TAG, "Boot SFX completion signal failed");
        abort();
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    const size_t wav_size = (size_t)(qsee_boot_wav_end - qsee_boot_wav_start);
    if (wav_size <= 44U) {
        qsee_hardware_report_speaker(false, ESP_ERR_INVALID_SIZE);
        ESP_LOGW(TAG, "Boot SFX asset is invalid");
        s_audio_result = ESP_ERR_INVALID_SIZE;
        signal_audio_complete();
        vTaskSuspend(NULL);
        ESP_LOGE(TAG, "Boot SFX worker resumed after completion");
        abort();
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
    signal_audio_complete();
    vTaskSuspend(NULL);
    ESP_LOGE(TAG, "Boot SFX worker resumed after completion");
    abort();
}

esp_err_t qsee_boot_audio_play(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_audio_started, &expected, true)) {
        ESP_LOGE(TAG, "Boot SFX playback was already started");
        return ESP_ERR_INVALID_STATE;
    }

    s_audio_done = xSemaphoreCreateBinaryStatic(&s_audio_done_control);
    if (s_audio_done == NULL) {
        atomic_store(&s_audio_started, false);
        ESP_LOGE(TAG, "Creating boot SFX completion signal failed");
        return ESP_ERR_NO_MEM;
    }

    TaskHandle_t task = NULL;
    if (xTaskCreateWithCaps(
        audio_task,
        "qsee_boot_sfx",
        3072,
        NULL,
        3,
        &task,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    ) != pdPASS) {
        atomic_store(&s_audio_started, false);
        ESP_LOGE(TAG, "Creating boot SFX task failed");
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_audio_done, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Waiting for boot SFX completion failed");
        vTaskDeleteWithCaps(task);
        return ESP_FAIL;
    }
    const esp_err_t result = s_audio_result;
    // Delete from the waiting task so the temporary internal stack and TCB are
    // reclaimed before the remaining boot stages can start allocating.
    vTaskDeleteWithCaps(task);
    return result;
}
