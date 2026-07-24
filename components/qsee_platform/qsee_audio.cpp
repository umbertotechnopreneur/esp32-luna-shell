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

#include "qsee_platform.h"

#include <algorithm>
#include <atomic>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

const char *TAG = "qsee_audio";
constexpr uint32_t AUDIO_LOCK_TIMEOUT_MS = 5000;
esp_codec_dev_handle_t s_speaker;
esp_codec_dev_handle_t s_microphone;
uint32_t s_sample_rate_hz;
std::atomic_bool s_initialized;
StaticSemaphore_t s_audio_mutex_control;
SemaphoreHandle_t s_audio_mutex;

SemaphoreHandle_t audio_mutex()
{
    if (s_audio_mutex == nullptr) {
        s_audio_mutex = xSemaphoreCreateMutexStatic(&s_audio_mutex_control);
    }
    return s_audio_mutex;
}

esp_err_t configure_streams(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_sample_rate_hz == sample_rate_hz) {
        return ESP_OK;
    }

    if (s_sample_rate_hz != 0) {
        (void)esp_codec_dev_close(s_speaker);
        (void)esp_codec_dev_close(s_microphone);
    }
    esp_codec_dev_sample_info_t sample = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = sample_rate_hz,
        .mclk_multiple = 0,
    };
    int result = esp_codec_dev_open(s_speaker, &sample);
    if (result == ESP_CODEC_DEV_OK) {
        result = esp_codec_dev_open(s_microphone, &sample);
    }
    if (result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Codec stream configuration failed: rate=%lu result=%d",
                 static_cast<unsigned long>(sample_rate_hz), result);
        return ESP_FAIL;
    }
    s_sample_rate_hz = sample_rate_hz;
    return ESP_OK;
}

}  // namespace

extern "C" esp_err_t qsee_audio_init(void)
{
    // Initialization is immutable after success; avoid waiting behind playback.
    if (s_initialized.load(std::memory_order_acquire)) {
        return ESP_OK;
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_initialized.load(std::memory_order_relaxed)) {
        xSemaphoreGive(mutex);
        return ESP_OK;
    }

    s_speaker = bsp_audio_codec_speaker_init();
    s_microphone = bsp_audio_codec_microphone_init();
    esp_err_t error =
        s_speaker != nullptr && s_microphone != nullptr ? configure_streams(22050) : ESP_ERR_NOT_FOUND;
    if (error == ESP_OK) {
        const int volume_result = esp_codec_dev_set_out_vol(s_speaker, 90);
        const int mute_result = esp_codec_dev_set_out_mute(s_speaker, true);
        const int gain_result = esp_codec_dev_set_in_gain(s_microphone, 24.0F);
        if (volume_result != ESP_CODEC_DEV_OK ||
            mute_result != ESP_CODEC_DEV_OK ||
            gain_result != ESP_CODEC_DEV_OK) {
            error = ESP_FAIL;
        }
    }
    s_initialized.store(error == ESP_OK, std::memory_order_release);
    xSemaphoreGive(mutex);

    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Audio route ready: ES8311 out + ES7210 in, mono PCM16 22.05kHz");
    } else {
        ESP_LOGE(TAG, "Audio route initialization failed (%s)", esp_err_to_name(error));
    }
    return error;
}

extern "C" esp_err_t qsee_audio_play_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    int volume_percent
)
{
    if (samples == nullptr || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = qsee_audio_init();
    if (error != ESP_OK) {
        return error;
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    error = configure_streams(sample_rate_hz);
    const int volume = std::clamp(volume_percent, 0, 100);
    if (error == ESP_OK &&
        esp_codec_dev_set_out_vol(s_speaker, volume) != ESP_CODEC_DEV_OK) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK &&
        esp_codec_dev_set_out_mute(s_speaker, false) != ESP_CODEC_DEV_OK) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        // The PA is enabled by the official BSP codec interface; allow its output to settle.
        vTaskDelay(pdMS_TO_TICKS(100));
        const int result = esp_codec_dev_write(
            s_speaker,
            const_cast<int16_t *>(samples),
            sample_count * sizeof(int16_t)
        );
        error = result == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    (void)esp_codec_dev_set_out_mute(s_speaker, true);
    xSemaphoreGive(mutex);

    ESP_LOGI(
        TAG,
        "Playback complete: rate=%lu samples=%u volume=%d result=%s",
        static_cast<unsigned long>(sample_rate_hz),
        static_cast<unsigned>(sample_count),
        volume,
        esp_err_to_name(error)
    );
    return error;
}

extern "C" esp_err_t qsee_audio_capture_pcm16(
    int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    float gain_db
)
{
    if (samples == nullptr || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = qsee_audio_init();
    if (error != ESP_OK) {
        return error;
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    error = configure_streams(sample_rate_hz);
    if (error == ESP_OK &&
        esp_codec_dev_set_in_gain(s_microphone, gain_db) != ESP_CODEC_DEV_OK) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        const int result = esp_codec_dev_read(
            s_microphone,
            samples,
            sample_count * sizeof(int16_t)
        );
        error = result == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
    }
    xSemaphoreGive(mutex);
    return error;
}
