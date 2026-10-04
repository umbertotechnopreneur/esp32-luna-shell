/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

// Boot-time audio cue playback for the startup flow.
// Plays the startup sound after the audio path becomes available.

#include "LUNASHELL_boot.h"
#include "LUNASHELL_liveness.h"
#include "LUNASHELL_platform.h"

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

extern const uint8_t LUNASHELL_boot_wav_start[] asm("_binary_LUNASHELL_boot_wav_start");
extern const uint8_t LUNASHELL_boot_wav_end[] asm("_binary_LUNASHELL_boot_wav_end");

static const char *TAG = "LUNASHELL_audio";
static const size_t BOOT_WAV_HEADER_BYTES = 44U;
static const uint32_t BOOT_AUDIO_SAMPLE_RATE_HZ = 22050U;
static const int BOOT_AUDIO_VOLUME_PERCENT = 100;
// Includes the audio layer's 5-second mutex deadline, codec settle delays, and
// scheduling margin in addition to the asset's exact playback duration.
static const uint32_t BOOT_AUDIO_COMPLETION_GRACE_MS = 8000U;
static const uint32_t BOOT_AUDIO_WDT_MARGIN_MS = 1000U;
static StaticSemaphore_t s_audio_done_control;
static SemaphoreHandle_t s_audio_done;
static volatile esp_err_t s_audio_result;
static atomic_bool s_audio_started;
enum {
    BOOT_AUDIO_CLEANUP_UNCLAIMED = 0,
    BOOT_AUDIO_CLEANUP_WAITER,
    BOOT_AUDIO_CLEANUP_WORKER,
};
static atomic_int s_audio_cleanup_owner;

static void finish_audio_task(void)
{
    int expected = BOOT_AUDIO_CLEANUP_UNCLAIMED;
    const bool worker_owns_cleanup = atomic_compare_exchange_strong(
        &s_audio_cleanup_owner,
        &expected,
        BOOT_AUDIO_CLEANUP_WORKER
    ) || expected == BOOT_AUDIO_CLEANUP_WORKER;
    if (worker_owns_cleanup) {
        vTaskDeleteWithCaps(NULL);
        ESP_LOGE(TAG, "Boot SFX worker returned after self-delete");
        abort();
    }

    // The waiter won cleanup ownership after receiving the completion signal.
    // Remain in a known state until it reclaims this temporary internal stack.
    vTaskSuspend(NULL);
    ESP_LOGE(TAG, "Boot SFX worker resumed after completion");
    abort();
}

static void signal_audio_complete(void)
{
    if (xSemaphoreGive(s_audio_done) != pdTRUE) {
        ESP_LOGE(TAG, "Boot SFX completion signal failed");
        abort();
    }
}

static void complete_audio_task(esp_err_t result)
{
    s_audio_result = result;
    const esp_err_t watch_result = LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_BOOT_AUDIO,
        result
    );
    if (result == ESP_OK && watch_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Boot SFX watchdog cleanup failed (%s)",
            esp_err_to_name(watch_result)
        );
        abort();
    }
    signal_audio_complete();
    finish_audio_task();
}

static void audio_task(void *arg)
{
    (void)arg;
    const size_t wav_size = (size_t)(LUNASHELL_boot_wav_end - LUNASHELL_boot_wav_start);
    if (wav_size <= BOOT_WAV_HEADER_BYTES) {
        LUNASHELL_hardware_report_speaker(false, ESP_ERR_INVALID_SIZE);
        ESP_LOGW(TAG, "Boot SFX asset is invalid");
        complete_audio_task(ESP_ERR_INVALID_SIZE);
    }

    const esp_err_t result = LUNASHELL_audio_play_pcm16(
        (const int16_t *)(LUNASHELL_boot_wav_start + BOOT_WAV_HEADER_BYTES),
        (wav_size - BOOT_WAV_HEADER_BYTES) / sizeof(int16_t),
        BOOT_AUDIO_SAMPLE_RATE_HZ,
        BOOT_AUDIO_VOLUME_PERCENT
    );

    if (result == ESP_OK) {
        LUNASHELL_hardware_report_speaker(true, ESP_OK);
        ESP_LOGI(TAG, "Boot SFX audible route completed: 22.05kHz mono PCM16 at volume %d", BOOT_AUDIO_VOLUME_PERCENT);
    } else {
        LUNASHELL_hardware_report_speaker(false, result);
        ESP_LOGW(TAG, "Boot SFX playback failed (%s)", esp_err_to_name(result));
    }
    complete_audio_task(result);
}

static esp_err_t boot_audio_timeout_ticks(TickType_t *timeout_ticks)
{
    if (timeout_ticks == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t wav_size =
        (size_t)(LUNASHELL_boot_wav_end - LUNASHELL_boot_wav_start);
    if (wav_size <= BOOT_WAV_HEADER_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t sample_count =
        (wav_size - BOOT_WAV_HEADER_BYTES) / sizeof(int16_t);
    const uint64_t playback_ms =
        ((uint64_t)sample_count * 1000ULL +
         BOOT_AUDIO_SAMPLE_RATE_HZ - 1ULL) /
        BOOT_AUDIO_SAMPLE_RATE_HZ;
    const uint64_t timeout_ms_64 =
        playback_ms + BOOT_AUDIO_COMPLETION_GRACE_MS;
    const uint64_t watchdog_timeout_ms =
        (uint64_t)CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000ULL;
    if (timeout_ms_64 > UINT32_MAX ||
        timeout_ms_64 + BOOT_AUDIO_WDT_MARGIN_MS > watchdog_timeout_ms) {
        ESP_LOGE(
            TAG,
            "Boot SFX deadline rejected: requested=%llu ms watchdog=%llu ms "
            "required-margin=%lu ms",
            (unsigned long long)timeout_ms_64,
            (unsigned long long)watchdog_timeout_ms,
            (unsigned long)BOOT_AUDIO_WDT_MARGIN_MS
        );
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t timeout_ms = (uint32_t)timeout_ms_64;
    *timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    ESP_LOGI(
        TAG,
        "Boot SFX completion deadline: %lu ms (%lu samples)",
        (unsigned long)timeout_ms,
        (unsigned long)sample_count
    );
    return ESP_OK;
}

static esp_err_t finish_boot_audio_watch(esp_err_t result)
{
    return LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_BOOT_AUDIO,
        result
    );
}

esp_err_t LUNASHELL_boot_audio_play(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_audio_started, &expected, true)) {
        ESP_LOGE(TAG, "Boot SFX playback was already started");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_BOOT_AUDIO
    );
    if (watch_error != ESP_OK) {
        atomic_store(&s_audio_started, false);
        ESP_LOGE(
            TAG,
            "Boot SFX liveness watch failed (%s)",
            esp_err_to_name(watch_error)
        );
        return watch_error;
    }

    TickType_t timeout_ticks = 0;
    const esp_err_t timeout_result =
        boot_audio_timeout_ticks(&timeout_ticks);
    if (timeout_result != ESP_OK || timeout_ticks == 0U) {
        atomic_store(&s_audio_started, false);
        const esp_err_t error = timeout_result != ESP_OK
                                    ? timeout_result
                                    : ESP_ERR_INVALID_SIZE;
        LUNASHELL_hardware_report_speaker(false, error);
        ESP_LOGE(
            TAG,
            "Boot SFX deadline calculation failed (%s)",
            esp_err_to_name(error)
        );
        return finish_boot_audio_watch(error);
    }

    s_audio_done = xSemaphoreCreateBinaryStatic(&s_audio_done_control);
    if (s_audio_done == NULL) {
        atomic_store(&s_audio_started, false);
        ESP_LOGE(TAG, "Creating boot SFX completion signal failed");
        return finish_boot_audio_watch(ESP_ERR_NO_MEM);
    }
    atomic_store(&s_audio_cleanup_owner, BOOT_AUDIO_CLEANUP_UNCLAIMED);

    TaskHandle_t task = NULL;
    if (xTaskCreateWithCaps(
        audio_task,
        "LUNASHELL_boot_sfx",
        3072,
        NULL,
        3,
        &task,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    ) != pdPASS) {
        atomic_store(&s_audio_started, false);
        ESP_LOGE(TAG, "Creating boot SFX task failed");
        return finish_boot_audio_watch(ESP_ERR_NO_MEM);
    }

    if (xSemaphoreTake(s_audio_done, timeout_ticks) != pdTRUE) {
        // Delegate task cleanup atomically. The worker always owns the
        // still-armed watch: a late completion closes it, while a genuinely
        // stuck audio path is reset by the Task WDT.
        int cleanup_expected = BOOT_AUDIO_CLEANUP_UNCLAIMED;
        (void)atomic_compare_exchange_strong(
            &s_audio_cleanup_owner,
            &cleanup_expected,
            BOOT_AUDIO_CLEANUP_WORKER
        );
        LUNASHELL_hardware_report_speaker(false, ESP_ERR_TIMEOUT);
        ESP_LOGE(
            TAG,
            "Boot SFX completion timed out; worker owns deferred cleanup and watchdog"
        );
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = s_audio_result;
    int cleanup_expected = BOOT_AUDIO_CLEANUP_UNCLAIMED;
    if (atomic_compare_exchange_strong(
            &s_audio_cleanup_owner,
            &cleanup_expected,
            BOOT_AUDIO_CLEANUP_WAITER
        )) {
        // Delete from the waiting task so the temporary internal stack and TCB
        // are reclaimed before the remaining boot stages allocate.
        vTaskDeleteWithCaps(task);
    }
    // The worker closes the watch before publishing completion. Cleanup task
    // ownership is independent and negotiated only to reclaim the stack.
    return result;
}
