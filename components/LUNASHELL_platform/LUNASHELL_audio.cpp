/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
#define LUNASHELL_AUDIO_HAS_OUTPUT 1
#else
#define LUNASHELL_AUDIO_HAS_OUTPUT 0
#endif

namespace {

const char *TAG = "LUNASHELL_audio";
constexpr uint32_t AUDIO_LOCK_TIMEOUT_MS = 5000;
constexpr uint32_t VOICE_SAMPLE_RATE_HZ = 16000;
constexpr size_t VOICE_REFERENCE_CHUNK_SAMPLES = VOICE_SAMPLE_RATE_HZ / 50U;
esp_codec_dev_handle_t s_speaker;
esp_codec_dev_handle_t s_microphone;
uint32_t s_speaker_sample_rate_hz;
uint32_t s_microphone_sample_rate_hz;
std::atomic_bool s_initialized;
bool s_initialization_attempted;
esp_err_t s_initialization_result = ESP_ERR_INVALID_STATE;
uint8_t s_speaker_channel_count;
uint8_t s_microphone_channel_count;
bool s_speaker_open;
bool s_microphone_open;
std::atomic_bool s_standby;
std::atomic_bool s_audio_state_poisoned;
StaticSemaphore_t s_audio_mutex_control;
SemaphoreHandle_t s_audio_mutex;

struct AudioStreamState {
    bool speaker_open;
    uint32_t speaker_sample_rate_hz;
    int speaker_volume;
    bool speaker_muted;
    bool microphone_open;
    uint32_t microphone_sample_rate_hz;
    uint8_t microphone_channel_count;
    float microphone_gain_db;
    bool microphone_muted;
};
AudioStreamState s_pre_standby_stream_state;
bool s_pre_standby_stream_state_valid;

SemaphoreHandle_t audio_mutex()
{
    if (s_audio_mutex == nullptr) {
        s_audio_mutex = xSemaphoreCreateMutexStatic(&s_audio_mutex_control);
    }
    return s_audio_mutex;
}

void audio_unlock(SemaphoreHandle_t mutex)
{
    if (xSemaphoreGive(mutex) != pdTRUE) {
        ESP_LOGE(TAG, "Audio mutex release failed");
        std::abort();
    }
}

// Preserve the first exact codec error while closing every owned stream.
esp_err_t close_streams()
{
    esp_err_t error = ESP_OK;
    if (s_speaker_open) {
        const int result = esp_codec_dev_close(s_speaker);
        if (result == ESP_CODEC_DEV_OK) {
            s_speaker_open = false;
            s_speaker_sample_rate_hz = 0;
            s_speaker_channel_count = 0;
        } else {
            ESP_LOGE(TAG, "Closing speaker stream failed (%d)", result);
            error = static_cast<esp_err_t>(result);
        }
    }
    if (s_microphone_open) {
        const int result = esp_codec_dev_close(s_microphone);
        if (result == ESP_CODEC_DEV_OK) {
            s_microphone_open = false;
            s_microphone_sample_rate_hz = 0;
            s_microphone_channel_count = 0;
        } else {
            ESP_LOGE(TAG, "Closing microphone stream failed (%d)", result);
            if (error == ESP_OK) {
                error = static_cast<esp_err_t>(result);
            }
        }
    }
    return error;
}

// Configure speaker transport only while the base audio route is active.
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
esp_err_t configure_speaker_stream(uint32_t sample_rate_hz)
{
#if !CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    (void)sample_rate_hz;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_standby.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "Speaker stream configuration rejected during standby");
        return ESP_ERR_INVALID_STATE;
    }
    if (sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_speaker_open && s_speaker_sample_rate_hz == sample_rate_hz) {
        return ESP_OK;
    }
    if (s_speaker_open) {
        const int close_result = esp_codec_dev_close(s_speaker);
        if (close_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "Closing speaker stream before reconfiguration failed: %d",
                close_result
            );
            return static_cast<esp_err_t>(close_result);
        }
    }
    s_speaker_open = false;
    esp_codec_dev_sample_info_t sample = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = sample_rate_hz,
        .mclk_multiple = 0,
    };
    const int result = esp_codec_dev_open(s_speaker, &sample);
    s_speaker_open = result == ESP_CODEC_DEV_OK;
    if (result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Speaker stream configuration failed: rate=%lu result=%d",
                 static_cast<unsigned long>(sample_rate_hz), result);
        return static_cast<esp_err_t>(result);
    }
    s_speaker_sample_rate_hz = sample_rate_hz;
    s_speaker_channel_count = 1;
    return ESP_OK;
#endif
}
#endif

// Configure microphone transport only while the base audio route is active.
#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
esp_err_t configure_microphone_stream(uint32_t sample_rate_hz, uint8_t channel_count)
{
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    (void)sample_rate_hz;
    (void)channel_count;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_standby.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "Microphone stream configuration rejected during standby");
        return ESP_ERR_INVALID_STATE;
    }
    if (sample_rate_hz == 0 || channel_count == 0 || channel_count > 2 ||
        channel_count != LUNASHELL_audio_input_channel_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_microphone_open &&
        s_microphone_sample_rate_hz == sample_rate_hz &&
        s_microphone_channel_count == channel_count) {
        return ESP_OK;
    }
    if (s_microphone_open) {
        const int close_result = esp_codec_dev_close(s_microphone);
        if (close_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "Closing microphone stream before reconfiguration failed: %d",
                close_result
            );
            return static_cast<esp_err_t>(close_result);
        }
    }
    s_microphone_open = false;
    esp_codec_dev_sample_info_t sample = {
        .bits_per_sample = 16,
        .channel = channel_count,
        .channel_mask = 0,
        .sample_rate = sample_rate_hz,
        .mclk_multiple = 0,
    };
    const int result = esp_codec_dev_open(s_microphone, &sample);
    s_microphone_open = result == ESP_CODEC_DEV_OK;
    if (result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(
            TAG,
            "Microphone stream configuration failed: rate=%lu channels=%u result=%d",
            static_cast<unsigned long>(sample_rate_hz),
            static_cast<unsigned>(channel_count),
            result
        );
        return static_cast<esp_err_t>(result);
    }
    s_microphone_sample_rate_hz = sample_rate_hz;
    s_microphone_channel_count = channel_count;
    return ESP_OK;
#endif
}
#endif

// Capture the active codec policy before a reversible standby transition.
esp_err_t capture_stream_state(AudioStreamState *state)
{
    if (state == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *state = {
        .speaker_open = s_speaker_open,
        .speaker_sample_rate_hz = s_speaker_sample_rate_hz,
        .speaker_volume = 0,
        .speaker_muted = true,
        .microphone_open = s_microphone_open,
        .microphone_sample_rate_hz = s_microphone_sample_rate_hz,
        .microphone_channel_count = s_microphone_channel_count,
        .microphone_gain_db = 0.0F,
        .microphone_muted = false,
    };
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (state->speaker_open) {
        int result = esp_codec_dev_get_out_vol(s_speaker, &state->speaker_volume);
        if (result == ESP_CODEC_DEV_OK) {
            result = esp_codec_dev_get_out_mute(s_speaker, &state->speaker_muted);
        }
        if (result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Capturing speaker policy failed: codec_result=%d", result);
            return static_cast<esp_err_t>(result);
        }
    }
#endif
#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    if (state->microphone_open) {
        int result =
            esp_codec_dev_get_in_gain(s_microphone, &state->microphone_gain_db);
        if (result == ESP_CODEC_DEV_OK) {
            result = esp_codec_dev_get_in_mute(
                s_microphone,
                &state->microphone_muted
            );
        }
        if (result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Capturing microphone policy failed: codec_result=%d", result);
            return static_cast<esp_err_t>(result);
        }
    }
#endif
    return ESP_OK;
}

// Restore stream formats and codec policy after standby or a failed transition.
esp_err_t restore_stream_state(const AudioStreamState &state)
{
    esp_err_t error = close_streams();
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (error == ESP_OK && state.speaker_open) {
        error = configure_speaker_stream(state.speaker_sample_rate_hz);
    }
    if (error == ESP_OK && state.speaker_open) {
        const int volume_result =
            esp_codec_dev_set_out_vol(s_speaker, state.speaker_volume);
        const int mute_result =
            esp_codec_dev_set_out_mute(s_speaker, state.speaker_muted);
        if (volume_result != ESP_CODEC_DEV_OK || mute_result != ESP_CODEC_DEV_OK) {
            const int result = volume_result != ESP_CODEC_DEV_OK
                ? volume_result
                : mute_result;
            ESP_LOGE(
                TAG,
                "Restoring speaker policy failed: volume=%d mute=%d",
                volume_result,
                mute_result
            );
            error = static_cast<esp_err_t>(result);
        }
    }
#endif
#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    if (error == ESP_OK && state.microphone_open) {
        error = configure_microphone_stream(
            state.microphone_sample_rate_hz,
            state.microphone_channel_count
        );
    }
    if (error == ESP_OK && state.microphone_open) {
        const int gain_result =
            esp_codec_dev_set_in_gain(s_microphone, state.microphone_gain_db);
        const int mute_result =
            esp_codec_dev_set_in_mute(s_microphone, state.microphone_muted);
        if (gain_result != ESP_CODEC_DEV_OK || mute_result != ESP_CODEC_DEV_OK) {
            const int result = gain_result != ESP_CODEC_DEV_OK
                ? gain_result
                : mute_result;
            ESP_LOGE(
                TAG,
                "Restoring microphone policy failed: gain=%d mute=%d",
                gain_result,
                mute_result
            );
            error = static_cast<esp_err_t>(result);
        }
    }
#endif
    if (error != ESP_OK) {
        const esp_err_t cleanup_error = close_streams();
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Audio stream rollback cleanup failed (%s)",
                esp_err_to_name(cleanup_error)
            );
        }
    }
    return error;
}

// Prepare the 16 kHz duplex route while the caller owns the audio mutex.
esp_err_t prepare_voice_route_locked()
{
    if (s_audio_state_poisoned.load(std::memory_order_acquire) ||
        s_standby.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "Voice route preparation rejected by audio lifecycle state");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = ESP_OK;
    const uint8_t microphone_channels = LUNASHELL_audio_input_channel_count();
    const bool route_ready =
        (!LUNASHELL_AUDIO_HAS_OUTPUT ||
         (s_speaker_open &&
          s_speaker_sample_rate_hz == VOICE_SAMPLE_RATE_HZ)) &&
        (microphone_channels == 0 ||
         (s_microphone_open &&
          s_microphone_sample_rate_hz == VOICE_SAMPLE_RATE_HZ &&
          s_microphone_channel_count == microphone_channels));
    if (route_ready) {
        return ESP_OK;
    }

    error = close_streams();
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (error == ESP_OK) {
        error = configure_speaker_stream(VOICE_SAMPLE_RATE_HZ);
    }
    if (error == ESP_OK) {
        const int volume_result = esp_codec_dev_set_out_vol(s_speaker, 90);
        const int mute_result = esp_codec_dev_set_out_mute(s_speaker, true);
        if (volume_result != ESP_CODEC_DEV_OK || mute_result != ESP_CODEC_DEV_OK) {
            const int result = volume_result != ESP_CODEC_DEV_OK
                ? volume_result
                : mute_result;
            ESP_LOGE(
                TAG,
                "Voice speaker idle policy failed: volume=%d mute=%d",
                volume_result,
                mute_result
            );
            error = static_cast<esp_err_t>(result);
        }
    }
#endif
#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    if (error == ESP_OK) {
        error = configure_microphone_stream(
            VOICE_SAMPLE_RATE_HZ,
            microphone_channels
        );
    }
    if (error == ESP_OK) {
        const int gain_result = esp_codec_dev_set_in_gain(s_microphone, 24.0F);
        if (gain_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "Voice microphone gain policy failed: result=%d",
                gain_result
            );
            error = static_cast<esp_err_t>(gain_result);
        }
    }
#endif
    if (error != ESP_OK) {
        const esp_err_t cleanup_error = close_streams();
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Voice route cleanup failed (%s)",
                esp_err_to_name(cleanup_error)
            );
        }
    }
    return error;
}

}  // namespace

extern "C" uint8_t LUNASHELL_audio_input_channel_count(void)
{
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_ES7210_DUAL
    return 2;
#elif CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_ES7210_MONO
    return 1;
#else
    return 0;
#endif
}

// Initialize base audio unless standby owns or poisoned the shared route.
extern "C" esp_err_t LUNASHELL_audio_init(void)
{
    if (s_audio_state_poisoned.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "Audio initialization rejected: state is poisoned");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_standby.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "Audio initialization rejected during standby");
        return ESP_ERR_INVALID_STATE;
    }
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
        audio_unlock(mutex);
        return ESP_OK;
    }
    if (s_standby.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "Audio initialization cancelled by standby");
        audio_unlock(mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_initialization_attempted) {
        const esp_err_t cached_result = s_initialization_result;
        audio_unlock(mutex);
        return cached_result;
    }
    s_initialization_attempted = true;

#if !CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311 && CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    ESP_LOGI(TAG, "Audio route disabled: no selected input or output device");
    s_initialization_result = ESP_ERR_NOT_SUPPORTED;
    audio_unlock(mutex);
    return ESP_ERR_NOT_SUPPORTED;
#endif

    const esp_err_t bsp_init_error =
        bsp_audio_codec_init(&s_speaker, &s_microphone);
    const bool bsp_route_owned = bsp_init_error == ESP_OK;
    esp_err_t error = bsp_init_error;
    if (bsp_init_error != ESP_OK &&
        (s_speaker != nullptr || s_microphone != nullptr)) {
        ESP_LOGE(
            TAG,
            "BSP audio initialization violated its failure contract: "
            "speaker=%p microphone=%p error=%s",
            s_speaker,
            s_microphone,
            esp_err_to_name(bsp_init_error)
        );
        std::abort();
    }
    if (error == ESP_OK &&
        ((LUNASHELL_AUDIO_HAS_OUTPUT && s_speaker == nullptr) ||
         (LUNASHELL_audio_input_channel_count() > 0 && s_microphone == nullptr))) {
        ESP_LOGE(TAG, "BSP audio initialization returned incomplete handles");
        error = ESP_ERR_INVALID_STATE;
    }
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (error == ESP_OK) {
        error = configure_speaker_stream(22050);
    }
    if (error == ESP_OK) {
        const int volume_result = esp_codec_dev_set_out_vol(s_speaker, 90);
        const int mute_result = esp_codec_dev_set_out_mute(s_speaker, true);
        if (volume_result != ESP_CODEC_DEV_OK || mute_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "Speaker codec policy failed: volume=%d mute=%d",
                volume_result,
                mute_result
            );
            error = ESP_FAIL;
        }
    }
#endif
#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    if (error == ESP_OK) {
        error = configure_microphone_stream(22050, LUNASHELL_audio_input_channel_count());
    }
    if (error == ESP_OK) {
        const int gain_result = esp_codec_dev_set_in_gain(s_microphone, 24.0F);
        if (gain_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Microphone codec gain policy failed: result=%d", gain_result);
            error = ESP_FAIL;
        }
    }
#endif
    if (error != ESP_OK) {
        const esp_err_t cleanup_error = close_streams();
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Audio initialization cleanup failed; stream ownership is "
                "indeterminate (%s)",
                esp_err_to_name(cleanup_error)
            );
            std::abort();
        }
        if (bsp_route_owned) {
            const esp_err_t bsp_cleanup_error =
                bsp_audio_codec_deinit();
            if (bsp_cleanup_error != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "BSP audio ownership cleanup failed; route ownership is "
                    "indeterminate (%s)",
                    esp_err_to_name(bsp_cleanup_error)
                );
                std::abort();
            }
            s_speaker = nullptr;
            s_microphone = nullptr;
        }
    }
    s_initialization_result = error;
    s_initialized.store(error == ESP_OK, std::memory_order_release);
    audio_unlock(mutex);

    if (error == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Audio route ready: speaker=%s microphone-channels=%u",
            LUNASHELL_AUDIO_HAS_OUTPUT ? "ES8311" : "disabled",
            static_cast<unsigned>(LUNASHELL_audio_input_channel_count())
        );
    } else {
        ESP_LOGE(TAG, "Audio route initialization failed (%s)", esp_err_to_name(error));
    }
    return error;
}

extern "C" esp_err_t LUNASHELL_audio_play_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    int volume_percent
)
{
#if !CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    (void)volume_percent;
    LUNASHELL_hardware_report_speaker(false, ESP_ERR_NOT_SUPPORTED);
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (samples == nullptr || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (volume_percent < 0 || volume_percent > 100) {
        ESP_LOGE(
            TAG,
            "Playback volume %d outside [0, 100]",
            volume_percent
        );
        return ESP_ERR_INVALID_ARG;
    }
    if (LUNASHELL_voice_afe_running() && sample_rate_hz != VOICE_SAMPLE_RATE_HZ) {
        ESP_LOGE(
            TAG,
            "Voice AFE owns 16 kHz capture; playback rate %lu is unsupported",
            static_cast<unsigned long>(sample_rate_hz)
        );
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t error = LUNASHELL_audio_init();
    if (error != ESP_OK) {
        return error;
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    error = configure_speaker_stream(sample_rate_hz);
    if (error == ESP_OK) {
        const int volume_result =
            esp_codec_dev_set_out_vol(s_speaker, volume_percent);
        if (volume_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "Setting playback volume failed: requested=%d codec_result=%d",
                volume_percent,
                volume_result
            );
            error = ESP_FAIL;
        }
    }
    if (error == ESP_OK) {
        const int unmute_result =
            esp_codec_dev_set_out_mute(s_speaker, false);
        if (unmute_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "Unmuting speaker failed: codec_result=%d",
                unmute_result
            );
            error = ESP_FAIL;
        }
    }
    if (error == ESP_OK) {
        // The PA is enabled by the official BSP codec interface; allow its output to settle.
        vTaskDelay(pdMS_TO_TICKS(100));
        size_t offset = 0;
        while (offset < sample_count && error == ESP_OK) {
            const bool voice_afe_active = LUNASHELL_voice_afe_running();
            const size_t chunk_samples = voice_afe_active
                ? std::min(VOICE_REFERENCE_CHUNK_SAMPLES, sample_count - offset)
                : sample_count - offset;
            if (voice_afe_active) {
                const esp_err_t reference_error = LUNASHELL_voice_afe_push_speaker_pcm16(
                    samples + offset,
                    chunk_samples,
                    sample_rate_hz
                );
                if (reference_error != ESP_OK) {
                    ESP_LOGE(
                        TAG,
                        "AEC speaker reference enqueue failed (%s)",
                        esp_err_to_name(reference_error)
                    );
                    error = reference_error;
                    break;
                }
            }
            const int result = esp_codec_dev_write(
                s_speaker,
                const_cast<int16_t *>(samples + offset),
                chunk_samples * sizeof(int16_t)
            );
            if (result != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "Speaker PCM write failed: codec_result=%d", result);
                error = ESP_FAIL;
                break;
            }
            offset += chunk_samples;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    const int mute_result = esp_codec_dev_set_out_mute(s_speaker, true);
    if (mute_result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Muting speaker after playback failed (%d)", mute_result);
        if (error == ESP_OK) {
            error = ESP_FAIL;
        }
    }
    audio_unlock(mutex);

    ESP_LOGI(
        TAG,
        "Playback complete: rate=%lu samples=%u volume=%d result=%s",
        static_cast<unsigned long>(sample_rate_hz),
        static_cast<unsigned>(sample_count),
        volume_percent,
        esp_err_to_name(error)
    );
    return error;
#endif
}

extern "C" esp_err_t LUNASHELL_audio_capture_pcm16(
    int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    float gain_db
)
{
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    (void)gain_db;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (samples == nullptr || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (LUNASHELL_audio_input_channel_count() != 1) {
        ESP_LOGE(TAG, "Mono capture is unavailable for the selected microphone topology");
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t error = LUNASHELL_audio_init();
    if (error != ESP_OK) {
        return error;
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    error = configure_microphone_stream(sample_rate_hz, 1);
    if (error == ESP_OK) {
        const int gain_result =
            esp_codec_dev_set_in_gain(s_microphone, gain_db);
        if (gain_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "Setting microphone gain failed: requested=%.2f codec_result=%d",
                static_cast<double>(gain_db),
                gain_result
            );
            error = ESP_FAIL;
        }
    }
    if (error == ESP_OK) {
        const int result = esp_codec_dev_read(
            s_microphone,
            samples,
            sample_count * sizeof(int16_t)
        );
        if (result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "Microphone PCM read failed: codec_result=%d",
                result
            );
            error = ESP_FAIL;
        }
    }
    audio_unlock(mutex);
    return error;
#endif
}

extern "C" esp_err_t LUNASHELL_audio_capture_pcm16_channels(
    int16_t *samples,
    size_t frame_count,
    uint8_t channel_count,
    uint32_t sample_rate_hz,
    float gain_db
)
{
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    (void)samples;
    (void)frame_count;
    (void)channel_count;
    (void)sample_rate_hz;
    (void)gain_db;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (samples == nullptr || frame_count == 0 || channel_count == 0 || channel_count > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = LUNASHELL_audio_init();
    if (error != ESP_OK) {
        return error;
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    error = configure_microphone_stream(sample_rate_hz, channel_count);
    if (error == ESP_OK) {
        const int gain_result =
            esp_codec_dev_set_in_gain(s_microphone, gain_db);
        if (gain_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "Setting %u-channel microphone gain failed: "
                "requested=%.2f codec_result=%d",
                static_cast<unsigned>(channel_count),
                static_cast<double>(gain_db),
                gain_result
            );
            error = ESP_FAIL;
        }
    }
    if (error == ESP_OK) {
        const int result = esp_codec_dev_read(
            s_microphone,
            samples,
            frame_count * channel_count * sizeof(int16_t)
        );
        if (result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(
                TAG,
                "%u-channel microphone PCM read failed: codec_result=%d",
                static_cast<unsigned>(channel_count),
                result
            );
            error = ESP_FAIL;
        }
    }
    audio_unlock(mutex);
    return error;
#endif
}

extern "C" esp_err_t LUNASHELL_audio_capture_voice_pcm16(
    int16_t *samples,
    size_t frame_count,
    uint8_t channel_count
)
{
    return LUNASHELL_audio_capture_pcm16_channels(
        samples,
        frame_count,
        channel_count,
        VOICE_SAMPLE_RATE_HZ,
        24.0F
    );
}

// Reconfigure the active base route for the AFE-owned 16 kHz stream.
extern "C" esp_err_t LUNASHELL_audio_prepare_voice_route(void)
{
    esp_err_t error = LUNASHELL_audio_init();
    if (error != ESP_OK) {
        return error;
    }

    SemaphoreHandle_t mutex = audio_mutex();
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    error = prepare_voice_route_locked();
    audio_unlock(mutex);

    if (error == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Voice duplex route ready: rate=%lu microphone-channels=%u",
            static_cast<unsigned long>(VOICE_SAMPLE_RATE_HZ),
            static_cast<unsigned>(LUNASHELL_audio_input_channel_count())
        );
    } else {
        ESP_LOGE(
            TAG,
            "Voice duplex route preparation failed (%s)",
            esp_err_to_name(error)
        );
    }
    return error;
}

// Suspend or restore codec streams and I2S as one reversible transition.
extern "C" esp_err_t LUNASHELL_audio_set_standby(bool standby)
{
    SemaphoreHandle_t mutex = audio_mutex();
    if (mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_audio_state_poisoned.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "Audio standby transition rejected: state is poisoned");
        audio_unlock(mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (standby == s_standby.load(std::memory_order_acquire)) {
        audio_unlock(mutex);
        return ESP_OK;
    }
    if (!s_initialized.load(std::memory_order_acquire)) {
        s_pre_standby_stream_state_valid = false;
        s_standby.store(standby, std::memory_order_release);
        audio_unlock(mutex);
        return ESP_OK;
    }

    esp_err_t error = ESP_OK;
    if (standby) {
        AudioStreamState previous = {};
        error = capture_stream_state(&previous);
        const bool state_captured = error == ESP_OK;
        if (error == ESP_OK) {
            error = close_streams();
        }
        if (error == ESP_OK) {
            error = bsp_audio_set_transport_standby(true, false, false);
        }
        if (error != ESP_OK) {
            const esp_err_t transport_rollback_error =
                bsp_audio_set_transport_standby(
                    false,
                    state_captured && previous.speaker_open,
                    state_captured && previous.microphone_open
                );
            const esp_err_t stream_rollback_error =
                state_captured && transport_rollback_error == ESP_OK
                    ? restore_stream_state(previous)
                    : transport_rollback_error == ESP_OK
                        ? ESP_OK
                        : transport_rollback_error;
            if (transport_rollback_error != ESP_OK ||
                stream_rollback_error != ESP_OK) {
                s_audio_state_poisoned.store(true, std::memory_order_release);
                ESP_LOGE(
                    TAG,
                    "Audio standby rollback failed: transport=%s streams=%s",
                    esp_err_to_name(transport_rollback_error),
                    esp_err_to_name(stream_rollback_error)
                );
            }
            audio_unlock(mutex);
            ESP_LOGE(TAG, "Audio standby failed (%s)", esp_err_to_name(error));
            return error;
        }
        s_pre_standby_stream_state = previous;
        s_pre_standby_stream_state_valid = true;
        s_standby.store(true, std::memory_order_release);
        audio_unlock(mutex);
        ESP_LOGI(TAG, "Audio codec streams and I2S transport suspended");
        return ESP_OK;
    }

    const bool voice_route_requested = LUNASHELL_voice_afe_running();
    const bool resume_speaker = voice_route_requested
        ? LUNASHELL_AUDIO_HAS_OUTPUT
        : s_pre_standby_stream_state_valid &&
              s_pre_standby_stream_state.speaker_open;
    const bool resume_microphone = voice_route_requested
        ? LUNASHELL_audio_input_channel_count() > 0
        : s_pre_standby_stream_state_valid &&
              s_pre_standby_stream_state.microphone_open;
    error = bsp_audio_set_transport_standby(
        false,
        resume_speaker,
        resume_microphone
    );
    if (error == ESP_OK) {
        s_standby.store(false, std::memory_order_release);
    }
    if (error == ESP_OK && voice_route_requested) {
        error = prepare_voice_route_locked();
    } else if (error == ESP_OK && s_pre_standby_stream_state_valid) {
        error = restore_stream_state(s_pre_standby_stream_state);
    }
    if (error != ESP_OK &&
        !s_standby.load(std::memory_order_acquire)) {
        const esp_err_t stream_rollback_error = close_streams();
        const esp_err_t transport_rollback_error =
            stream_rollback_error == ESP_OK
                ? bsp_audio_set_transport_standby(true, false, false)
                : stream_rollback_error;
        if (stream_rollback_error == ESP_OK &&
            transport_rollback_error == ESP_OK) {
            s_standby.store(true, std::memory_order_release);
        } else {
            s_audio_state_poisoned.store(true, std::memory_order_release);
            ESP_LOGE(
                TAG,
                "Audio resume rollback failed: streams=%s transport=%s",
                esp_err_to_name(stream_rollback_error),
                esp_err_to_name(transport_rollback_error)
            );
        }
    }
    if (error == ESP_OK) {
        s_pre_standby_stream_state_valid = false;
    }
    audio_unlock(mutex);
    if (error == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Audio I2S transport resumed: route=%s",
            voice_route_requested ? "voice" : "restored"
        );
    } else {
        ESP_LOGE(
            TAG,
            "Audio resume failed (%s)",
            esp_err_to_name(error)
        );
    }
    return error;
}
