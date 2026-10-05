/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_clock.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
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
std::atomic<esp_err_t> s_initialization_result{ESP_ERR_INVALID_STATE};
struct StartupCue {
    const int16_t *samples = nullptr;
    size_t sample_count = 0;
    uint32_t sample_rate_hz = 0;
    int volume_percent = 0;
};
StartupCue s_startup_cue;
uint8_t s_speaker_channel_count;
uint8_t s_microphone_channel_count;
bool s_speaker_open;
bool s_microphone_open;
std::atomic_bool s_standby;
std::atomic_bool s_audio_state_poisoned;
StaticSemaphore_t s_audio_mutex_control;
SemaphoreHandle_t s_audio_mutex;

constexpr uint32_t TYPEWRITER_INTERVAL_US = 50000;
constexpr uint32_t TYPEWRITER_MAX_AGE_US = 150000;
constexpr uint32_t TYPEWRITER_EVENT = 1U;
constexpr uint32_t TYPEWRITER_STACK_BYTES = 4096;
constexpr uint32_t TYPEWRITER_MAX_TRANSPORT_RATE_HZ = 48000;
constexpr size_t TYPEWRITER_MAX_TRANSPORT_SAMPLES = 1200;
struct TypewriterCue {
    const int16_t *samples = nullptr;
    size_t sample_count = 0;
    uint32_t sample_rate_hz = 0;
};
TypewriterCue s_typewriter_cue;
StaticTask_t s_typewriter_task_control;
// Application workers copy PCM through the codec; DMA buffers and TCBs stay internal.
EXT_RAM_BSS_ATTR StackType_t s_typewriter_task_stack[TYPEWRITER_STACK_BYTES / sizeof(StackType_t)];
std::atomic<TaskHandle_t> s_typewriter_task;
struct TypewriterEvent {
    uint32_t requested_at_us = 0;
    uint32_t generation = 0;
    uint32_t sequence = 0;
};
struct TypewriterMailbox {
    bool pending = false;
    bool suspended = false;
    uint32_t generation = 0;
    uint32_t sequence = 0;
    TypewriterEvent latest;
};
TypewriterMailbox s_typewriter_mailbox;
portMUX_TYPE s_typewriter_mailbox_mux = portMUX_INITIALIZER_UNLOCKED;
// No parameters: cancels any symbol dispatch before the codec changes lifecycle ownership.
void invalidate_personality_events();
std::atomic<esp_err_t> s_typewriter_last_error{ESP_OK};
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
EXT_RAM_BSS_ATTR std::array<int16_t, TYPEWRITER_MAX_TRANSPORT_SAMPLES> s_typewriter_transport_samples;
#endif

// Clear an old scene's pending cue without waiting for the transport owner.
void invalidate_typewriter_events()
{
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    s_typewriter_mailbox.pending = false;
    ++s_typewriter_mailbox.generation;
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
}

#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
// No payload is consumed by this scheduling hint; the claim below remains authoritative.
bool has_pending_typewriter_event()
{
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    const bool pending = s_typewriter_mailbox.pending;
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
    return pending;
}

// event: Receives one coherent latest payload only when a pending cue is claimed.
bool claim_typewriter_event(TypewriterEvent &event)
{
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    const bool claimed = s_typewriter_mailbox.pending;
    if (claimed) {
        event = s_typewriter_mailbox.latest;
        s_typewriter_mailbox.pending = false;
    }
    const bool allowed = claimed && !s_typewriter_mailbox.suspended &&
        !s_standby.load(std::memory_order_acquire);
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
    return allowed;
}
#endif

// event: Failed notification's exact payload; a newer producer must keep its cue.
void drop_typewriter_event(const TypewriterEvent &event)
{
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    if (s_typewriter_mailbox.pending && s_typewriter_mailbox.latest.sequence == event.sequence) {
        s_typewriter_mailbox.pending = false;
    }
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
}

#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
// generation: Epoch captured with the retired-frame event, never read from a newer payload.
bool typewriter_event_current(uint32_t generation)
{
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    const bool current = !s_typewriter_mailbox.suspended &&
        generation == s_typewriter_mailbox.generation;
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
    return current;
}
#endif

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
// sample_rate_hz: Requested transport clock, matching any open peer microphone.
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
        ESP_LOGE(TAG, "Speaker stream rejected: zero sample rate (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (s_microphone_open && sample_rate_hz != s_microphone_sample_rate_hz) {
        // TX and RX share a clock. Do not let the codec accept PCM against
        // a peer transport that never changed its real sample rate.
        ESP_LOGE(TAG, "Speaker clock conflict: requested=%lu microphone=%lu (%s)",
            static_cast<unsigned long>(sample_rate_hz),
            static_cast<unsigned long>(s_microphone_sample_rate_hz), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
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
// sample_rate_hz: Requested transport clock, matching any open peer speaker.
// channel_count: Exact channel count owned by the configured ES7210 input.
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
        ESP_LOGE(TAG, "Microphone stream rejected: rate=%lu channels=%u (%s)",
            static_cast<unsigned long>(sample_rate_hz), channel_count, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (s_speaker_open && sample_rate_hz != s_speaker_sample_rate_hz) {
        ESP_LOGE(TAG, "Microphone clock conflict: requested=%lu speaker=%lu (%s)",
            static_cast<unsigned long>(sample_rate_hz),
            static_cast<unsigned long>(s_speaker_sample_rate_hz), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
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
// samples: Immutable flash-owned mono PCM; this owner does not allocate or copy it.
// sample_count: Exact clip length in samples.
// sample_rate_hz: Selected codec sample rate.
// volume_percent: Explicit codec output volume.
extern "C" esp_err_t LUNASHELL_audio_set_startup_cue(const int16_t *samples,
    size_t sample_count, uint32_t sample_rate_hz, int volume_percent)
{
    if (samples == nullptr || sample_count == 0 || sample_rate_hz < 50 ||
        volume_percent < 0 || volume_percent > 100) {
        ESP_LOGE(TAG, "Startup audio cue rejected: invalid PCM or format");
        return ESP_ERR_INVALID_ARG;
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (mutex == nullptr) {
        ESP_LOGE(TAG, "Startup cue mutex creation failed");
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Startup cue registration timed out");
        return ESP_ERR_TIMEOUT;
    }
    if (s_initialization_attempted || s_startup_cue.samples != nullptr) {
        audio_unlock(mutex);
        ESP_LOGE(TAG, "Startup cue registration rejected after configuration/init");
        return ESP_ERR_INVALID_STATE;
    }
    s_startup_cue = {samples, sample_count, sample_rate_hz, volume_percent};
    audio_unlock(mutex);
    return ESP_OK;
}

// Initialize the existing board codec route once; the registered cue has the same owner.
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
        return s_initialization_result.load(std::memory_order_acquire);
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
        return s_initialization_result.load(std::memory_order_acquire);
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
        if (s_startup_cue.samples != nullptr) {
            // The route is initialized and the mutex released before calling the
            // existing playback API; its initialization fast path cannot recurse.
            error = LUNASHELL_audio_play_pcm16(s_startup_cue.samples,
                s_startup_cue.sample_count, s_startup_cue.sample_rate_hz,
                s_startup_cue.volume_percent);
            s_initialization_result.store(error, std::memory_order_release);
            if (error != ESP_OK) {
                ESP_LOGE(TAG, "Audio-ready cue failed: %s", esp_err_to_name(error));
            }
        }
    } else {
        ESP_LOGE(TAG, "Audio route initialization failed (%s)", esp_err_to_name(error));
    }
    return error;
}

namespace {
// samples: Immutable PCM at the actual transport sample rate.
// sample_count: Exact count, already validated by the public owner.
// sample_rate_hz: Actual clock shared with the microphone when open.
// volume_percent: Observed or explicitly selected codec output policy.
// cue_id: Stable application sound identity, published only after successful writes.
// Exception: Returns original AFE, codec write, or snapshot publication failures.
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
esp_err_t write_speaker_pcm_locked(const int16_t *samples, size_t sample_count,
                                  uint32_t sample_rate_hz, int volume_percent, uint32_t cue_id = 0)
{
    size_t offset = 0;
    while (offset < sample_count) {
        const bool voice_afe_active = LUNASHELL_voice_afe_running();
        const size_t chunk_limit = voice_afe_active ? VOICE_REFERENCE_CHUNK_SAMPLES : sample_rate_hz / 50U;
        const size_t chunk_samples = std::min(chunk_limit, sample_count - offset);
        if (voice_afe_active) {
            const esp_err_t reference_error = LUNASHELL_voice_afe_push_speaker_pcm16(
                samples + offset, chunk_samples, sample_rate_hz);
            if (reference_error != ESP_OK) {
                ESP_LOGE(TAG, "AEC speaker reference enqueue failed (%s)", esp_err_to_name(reference_error));
                return reference_error;
            }
        }
        const int write_result = esp_codec_dev_write(s_speaker,
            const_cast<int16_t *>(samples + offset), chunk_samples * sizeof(int16_t));
        if (write_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Speaker PCM write failed: codec_result=%d", write_result);
            return static_cast<esp_err_t>(write_result);
        }
        uint32_t absolute_sum = 0;
        for (size_t index = 0; index < chunk_samples; ++index) {
            const int32_t sample = samples[offset + index];
            absolute_sum += static_cast<uint32_t>(sample < 0 ? -sample : sample);
        }
        const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
            true, absolute_sum / chunk_samples, sample_rate_hz, volume_percent, ESP_OK, cue_id);
        if (publication_error != ESP_OK) {
            ESP_LOGE(TAG, "Speaker playback publication failed (%s)", esp_err_to_name(publication_error));
            return publication_error;
        }
        offset += chunk_samples;
    }
    return ESP_OK;
}
#endif

// error: Original failure; a secondary publication failure cannot replace it.
// sample_rate_hz: Requested rate, diagnostic only on a failed publication.
// volume_percent: Requested volume, diagnostic only on failure.
void publish_rejected_playback(esp_err_t error, uint32_t sample_rate_hz, int volume_percent)
{
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
        false, 0, sample_rate_hz, volume_percent, error);
    if (publication_error != ESP_OK) {
        ESP_LOGE(TAG, "Rejected playback publication failed: %s", esp_err_to_name(publication_error));
    }
}

// operation: Rejected playback stage retained in the original error log.
// error: Original failure; a secondary publication failure cannot replace it.
// sample_rate_hz: Requested rate, diagnostic only on a failed publication.
// volume_percent: Requested volume, diagnostic only on failure.
esp_err_t reject_playback(const char *operation, esp_err_t error,
                         uint32_t sample_rate_hz, int volume_percent)
{
    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
    publish_rejected_playback(error, sample_rate_hz, volume_percent);
    return error;
}
} // namespace

// samples: Borrowed immutable mono PCM16; storage remains owned by the caller.
// sample_count: Exact number of samples to write.
// sample_rate_hz: Actual requested output sample rate.
// volume_percent: Codec output volume in 0..100.
// Exception: Returns original standby, codec, format or snapshot publication errors.
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
    return reject_playback("Disabled playback route", ESP_ERR_NOT_SUPPORTED, sample_rate_hz, volume_percent);
#else
    if (samples == nullptr || sample_count == 0 || sample_rate_hz < 50) {
        ESP_LOGE(TAG, "PCM playback rejected: samples=%p count=%u rate=%lu (%s)",
            samples, static_cast<unsigned>(sample_count), static_cast<unsigned long>(sample_rate_hz),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return reject_playback("Playback PCM format", ESP_ERR_INVALID_ARG, sample_rate_hz, volume_percent);
    }
    if (volume_percent < 0 || volume_percent > 100) {
        ESP_LOGE(
            TAG,
            "Playback volume %d outside [0, 100]",
            volume_percent
        );
        return reject_playback("Playback volume", ESP_ERR_INVALID_ARG, sample_rate_hz, volume_percent);
    }
    if (LUNASHELL_voice_afe_running() && sample_rate_hz != VOICE_SAMPLE_RATE_HZ) {
        ESP_LOGE(
            TAG,
            "Voice AFE owns 16 kHz capture; playback rate %lu is unsupported",
            static_cast<unsigned long>(sample_rate_hz)
        );
        return reject_playback("Playback AFE format", ESP_ERR_NOT_SUPPORTED, sample_rate_hz, volume_percent);
    }
    esp_err_t error = LUNASHELL_audio_init();
    if (error != ESP_OK) {
        return reject_playback("Playback initialization", error, sample_rate_hz, volume_percent);
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (mutex == nullptr) {
        return reject_playback("Playback mutex allocation", ESP_ERR_NO_MEM, sample_rate_hz, volume_percent);
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return reject_playback("Playback mutex acquisition", ESP_ERR_TIMEOUT, sample_rate_hz, volume_percent);
    }

    // Standby may have won this mutex after the initialization fast path returned.
    // Playback cannot reopen a suspended or indeterminate route under that owner.
    if (s_standby.load(std::memory_order_acquire) ||
        s_audio_state_poisoned.load(std::memory_order_acquire)) {
        audio_unlock(mutex);
        return reject_playback("Playback lifecycle ownership", ESP_ERR_INVALID_STATE,
            sample_rate_hz, volume_percent);
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
            error = static_cast<esp_err_t>(volume_result);
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
            error = static_cast<esp_err_t>(unmute_result);
        }
    }
    if (error == ESP_OK) {
        // The PA is enabled by the official BSP codec interface; allow its output to settle.
        vTaskDelay(pdMS_TO_TICKS(100));
        error = write_speaker_pcm_locked(samples, sample_count, sample_rate_hz, volume_percent);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    const int mute_result = esp_codec_dev_set_out_mute(s_speaker, true);
    if (mute_result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Muting speaker after playback failed (%d)", mute_result);
        if (error == ESP_OK) {
            error = static_cast<esp_err_t>(mute_result);
        }
    }
    audio_unlock(mutex);

    const esp_err_t publication_error =
        LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
            false, 0, sample_rate_hz, volume_percent, error);
    if (publication_error != ESP_OK) {
        ESP_LOGE(TAG, "Playback final publication failed: %s", esp_err_to_name(publication_error));
        if (error == ESP_OK) {
            error = publication_error;
        }
    }

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
// standby: Clears unplayed taps while the existing owner quiesces/restores its transport.
// Exception: Returns original codec, transport, or lifecycle rollback errors.
extern "C" esp_err_t LUNASHELL_audio_set_standby(bool standby)
{
    invalidate_typewriter_events();
    invalidate_personality_events();
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

namespace {
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
// played: True only after the real PCM write and mute restoration both succeed.
// sample_rate_hz: Receives the current transport clock; no microphone is reconfigured.
// volume_percent: Receives the existing codec volume, which this cue never overwrites.
// generation: Scene admission generation captured when this event was dequeued.
// Exception: Returns exact codec, AFE or snapshot failures; the caller owns the audio mutex.
esp_err_t play_typewriter_tap_locked(bool &played, uint32_t &sample_rate_hz,
                                    int &volume_percent, uint32_t generation)
{
    played = false;
    if (s_standby.load(std::memory_order_acquire) || !typewriter_event_current(generation)) {
        return ESP_OK; // An explicitly suppressed cue was never submitted to the codec.
    }
    if (s_audio_state_poisoned.load(std::memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    sample_rate_hz = s_microphone_open ? s_microphone_sample_rate_hz :
        s_speaker_open ? s_speaker_sample_rate_hz : s_typewriter_cue.sample_rate_hz;
    if (sample_rate_hz < 8000U || sample_rate_hz > TYPEWRITER_MAX_TRANSPORT_RATE_HZ) {
        ESP_LOGE(TAG, "Typewriter transport rate rejected: %lu", static_cast<unsigned long>(sample_rate_hz));
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t error = configure_speaker_stream(sample_rate_hz);
    if (error != ESP_OK) {
        return error;
    }
    bool previous_mute = true;
    if (error == ESP_OK) {
        const int volume_result = esp_codec_dev_get_out_vol(s_speaker, &volume_percent);
        const int mute_result = volume_result == ESP_CODEC_DEV_OK
            ? esp_codec_dev_get_out_mute(s_speaker, &previous_mute) : volume_result;
        if (volume_result != ESP_CODEC_DEV_OK || mute_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Reading typewriter codec policy failed: volume=%d mute=%d", volume_result, mute_result);
            return static_cast<esp_err_t>(volume_result != ESP_CODEC_DEV_OK ? volume_result : mute_result);
        }
        if (volume_percent < 0 || volume_percent > 100) {
            ESP_LOGE(TAG, "Typewriter codec volume rejected: %d", volume_percent);
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (volume_percent == 0) {
            return ESP_OK; // Existing silence is respected without a codec write or success report.
        }
    }
    const size_t output_samples = s_typewriter_cue.sample_count * sample_rate_hz / s_typewriter_cue.sample_rate_hz;
    if (output_samples == 0 || output_samples > s_typewriter_transport_samples.size()) {
        return ESP_ERR_INVALID_SIZE;
    }
    // The peer mic clock stays untouched. Resample this tiny original impulse into
    // bounded owner scratch instead of trying to switch the duplex I2S clock.
    for (size_t index = 0; index < output_samples; ++index) {
        const uint32_t source_position = static_cast<uint32_t>(index) * s_typewriter_cue.sample_rate_hz;
        const size_t source_index = source_position / sample_rate_hz;
        const uint32_t fraction = source_position % sample_rate_hz;
        const size_t next_index = std::min(source_index + 1U, s_typewriter_cue.sample_count - 1U);
        const int32_t first = s_typewriter_cue.samples[source_index];
        const int32_t next = s_typewriter_cue.samples[next_index];
        s_typewriter_transport_samples[index] = static_cast<int16_t>(
            (first * static_cast<int32_t>(sample_rate_hz - fraction) + next * static_cast<int32_t>(fraction)) /
            static_cast<int32_t>(sample_rate_hz));
    }
    if (error == ESP_OK) {
        const int unmute_result = esp_codec_dev_set_out_mute(s_speaker, false);
        if (unmute_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Unmuting speaker for typewriter tap failed (%d)", unmute_result);
            error = static_cast<esp_err_t>(unmute_result);
        }
    }
    if (error == ESP_OK) {
        error = write_speaker_pcm_locked(s_typewriter_transport_samples.data(), output_samples,
            sample_rate_hz, volume_percent);
    }
    if (error == ESP_OK) {
        // Codec writes can enqueue DMA before the last sample reaches the amp.
        // Drain this 15..25 ms cue before restoring mute, only in the audio worker.
        const uint32_t clip_ms = static_cast<uint32_t>((output_samples * 1000U + sample_rate_hz - 1U) / sample_rate_hz);
        vTaskDelay(pdMS_TO_TICKS(clip_ms) + 1U);
    }
    const int mute_result = esp_codec_dev_set_out_mute(s_speaker, previous_mute);
    if (mute_result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Restoring speaker mute after typewriter tap failed (%d)", mute_result);
        if (error == ESP_OK) {
            error = static_cast<esp_err_t>(mute_result);
        }
    }
    played = error == ESP_OK;
    return error;
}

// error: Original asynchronous error, retained for the next caller and canonical snapshot.
// sample_rate_hz: Attempted transport rate, diagnostic only after a rejection.
// volume_percent: Observed codec volume, or zero before policy could be read.
// Coalesce duplicate log lines, never the canonical publication of a real failed operation.
void retain_typewriter_failure(esp_err_t error, uint32_t sample_rate_hz, int volume_percent)
{
    if (s_typewriter_last_error.exchange(error, std::memory_order_acq_rel) != error) {
        ESP_LOGE(TAG, "Typewriter playback failed: %s", esp_err_to_name(error));
    }
    publish_rejected_playback(error, sample_rate_hz, volume_percent);
}

// context: Unused; this static worker shares the existing codec/mutex owner.
// Exception: Asynchronous errors are logged, published and returned by later admissions.
void typewriter_worker(void *context)
{
    (void)context;
    uint32_t last_play_at_us = 0;
    bool have_played = false;
    for (;;) {
        uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);
        if ((events & TYPEWRITER_EVENT) == 0 || !has_pending_typewriter_event()) {
            continue;
        }
        const uint32_t now_us = static_cast<uint32_t>(esp_timer_get_time());
        const uint32_t since_play_us = now_us - last_play_at_us;
        if (have_played && since_play_us < TYPEWRITER_INTERVAL_US) {
            const uint32_t delay_ms = (TYPEWRITER_INTERVAL_US - since_play_us + 999U) / 1000U;
            // One extra tick avoids a short floor-rounded wait at lower tick rates.
            vTaskDelay(pdMS_TO_TICKS(delay_ms) + 1U);
        }
        TypewriterEvent event;
        if (!claim_typewriter_event(event)) {
            continue;
        }
        const uint32_t observed_us = static_cast<uint32_t>(esp_timer_get_time());
        if (observed_us - event.requested_at_us > TYPEWRITER_MAX_AGE_US) {
            continue; // Expired taps never catch up after another audio operation.
        }
        LUNASHELL::ClockSnapshot clock;
        esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
        if (error != ESP_OK) {
            retain_typewriter_failure(error, s_typewriter_cue.sample_rate_hz, 0);
            continue;
        }
        if (LUNASHELL::clock_cue_policy(clock) != LUNASHELL::ClockCuePolicy::Allowed) {
            continue; // Canonical quiet/invalid-time policy deliberately suppresses the cue.
        }
        error = LUNASHELL_audio_init();
        if (error != ESP_OK) {
            retain_typewriter_failure(error, s_typewriter_cue.sample_rate_hz, 0);
            continue;
        }
        SemaphoreHandle_t mutex = audio_mutex();
        if (xSemaphoreTake(mutex, 0) != pdTRUE) {
            continue; // Meaningful playback/capture owns the route; no tap backlog is created.
        }
        bool played = false;
        uint32_t sample_rate_hz = s_typewriter_cue.sample_rate_hz;
        int volume_percent = 0;
        const uint32_t started_at_us = static_cast<uint32_t>(esp_timer_get_time());
        error = play_typewriter_tap_locked(played, sample_rate_hz, volume_percent, event.generation);
        if (error != ESP_OK) {
            retain_typewriter_failure(error, sample_rate_hz, volume_percent);
            audio_unlock(mutex);
            continue;
        }
        if (played) {
            last_play_at_us = started_at_us;
            have_played = true;
            const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
                false, 0, sample_rate_hz, volume_percent, ESP_OK);
            if (publication_error != ESP_OK) {
                retain_typewriter_failure(publication_error, sample_rate_hz, volume_percent);
            } else {
                s_typewriter_last_error.store(ESP_OK, std::memory_order_release);
            }
        }
        audio_unlock(mutex);
    }
}
#endif
} // namespace

// samples: Immutable original PCM; lifetime must match the firmware owner.
// sample_count: Bounded 15..25 ms cue payload, 240..400 mono samples.
// sample_rate_hz: Exactly 16000 Hz; transport conversion happens only in the owner.
// Exception: Returns actual preparation errors; does not initialize or play the codec.
extern "C" esp_err_t LUNASHELL_audio_prepare_typewriter(const int16_t *samples,
    size_t sample_count, uint32_t sample_rate_hz)
{
#if !CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    ESP_LOGE(TAG, "Typewriter audio is unavailable for the selected output route");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (samples == nullptr || sample_count < 240U || sample_count > 400U || sample_rate_hz != 16000U) {
        ESP_LOGE(TAG, "Typewriter original PCM registration rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Typewriter preparation mutex timed out");
        return ESP_ERR_TIMEOUT;
    }
    if (s_typewriter_task.load(std::memory_order_acquire) != nullptr) {
        const bool same_cue = s_typewriter_cue.samples == samples &&
            s_typewriter_cue.sample_count == sample_count && s_typewriter_cue.sample_rate_hz == sample_rate_hz;
        audio_unlock(mutex);
        if (!same_cue) {
            ESP_LOGE(TAG, "Typewriter cue cannot replace its lifetime-owned PCM");
            return ESP_ERR_INVALID_STATE;
        }
        return s_typewriter_last_error.load(std::memory_order_acquire);
    }
    s_typewriter_cue = {samples, sample_count, sample_rate_hz};
    const TaskHandle_t worker = xTaskCreateStatic(typewriter_worker, "audio_typewriter",
        TYPEWRITER_STACK_BYTES, nullptr, 2, s_typewriter_task_stack, &s_typewriter_task_control);
    s_typewriter_task.store(worker, std::memory_order_release);
    audio_unlock(mutex);
    if (worker == nullptr) {
        ESP_LOGE(TAG, "Typewriter static worker creation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Original typewriter cue registered: samples=%u bytes=%u rate=%lu; admission capped at 20/s",
        static_cast<unsigned>(sample_count), static_cast<unsigned>(sample_count * sizeof(int16_t)),
        static_cast<unsigned long>(sample_rate_hz));
    return ESP_OK;
#endif
}

// newly_visible_characters: New glyphs from the caller's complete displayed-frame delta.
// Exception: Returns admission or retained asynchronous failure; never waits for audio I/O.
extern "C" esp_err_t LUNASHELL_audio_notify_typewriter(uint32_t newly_visible_characters)
{
    if (newly_visible_characters == 0) {
        return ESP_OK; // No playback was requested for this unchanged frame.
    }
    const TaskHandle_t worker = s_typewriter_task.load(std::memory_order_acquire);
    const uint32_t requested_at_us = static_cast<uint32_t>(esp_timer_get_time());
    TypewriterEvent event;
    bool admitted = false;
    bool owner_unavailable = false;
    // Publish pending, timestamp and epoch together. No scheduler or codec call
    // belongs in this bounded critical section shared with the consumer claim.
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    if (!s_typewriter_mailbox.suspended && !s_standby.load(std::memory_order_acquire)) {
        owner_unavailable = worker == nullptr;
        if (!owner_unavailable) {
            ++s_typewriter_mailbox.sequence;
            event = {requested_at_us, s_typewriter_mailbox.generation, s_typewriter_mailbox.sequence};
            s_typewriter_mailbox.latest = event;
            s_typewriter_mailbox.pending = true;
            admitted = true;
        }
    }
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
    if (owner_unavailable) {
        ESP_LOGE(TAG, "Typewriter notification rejected before owner preparation");
        return ESP_ERR_INVALID_STATE;
    }
    if (!admitted) {
        return ESP_OK; // An explicitly suspended/standby cue was not admitted.
    }
    if (xTaskNotify(worker, TYPEWRITER_EVENT, eSetBits) != pdPASS) {
        drop_typewriter_event(event);
        ESP_LOGE(TAG, "Typewriter notification failed (%s)", esp_err_to_name(ESP_FAIL));
        return ESP_FAIL;
    }
    return s_typewriter_last_error.load(std::memory_order_acquire);
}

// suspended: Changes only cue admission; the canonical standby API owns codec quiescence.
// Exception: Returns exact owner-preparation errors; no allocation, blocking lock, or codec I/O occurs.
extern "C" esp_err_t LUNASHELL_audio_set_typewriter_suspended(bool suspended)
{
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    s_typewriter_mailbox.suspended = suspended;
    s_typewriter_mailbox.pending = false;
    ++s_typewriter_mailbox.generation;
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
    if (s_typewriter_task.load(std::memory_order_acquire) == nullptr) {
        ESP_LOGE(TAG, "Typewriter suspension rejected before owner preparation");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

namespace {
// Transport resource limits are independent of product authoring policy.
constexpr uint32_t PERSONALITY_STACK_BYTES = 4096;
struct PersonalityAudioEvent {
    const int16_t *samples = nullptr;
    size_t sample_count = 0;
    uint32_t sample_rate_hz = 16000;
    uint32_t cue_id = 0;
    uint32_t generation = 0;
    int64_t requested_at_us = 0;
    int64_t request_expiry_us = 0;
};
struct PersonalityAudioMailbox {
    PersonalityAudioEvent event;
    lunashell_audio_cue_status_t status{LUNASHELL_CUE_IDLE, ESP_OK, 0, 0};
    lunashell_personality_audio_policy_t policy{};
    bool policy_configured = false;
    uint32_t generation = 0;
    bool pending = false;
    bool in_flight = false;
    bool suspended = false;
    bool have_played = false;
    int64_t last_started_us = 0;
};
PersonalityAudioMailbox s_personality_mailbox;
portMUX_TYPE s_personality_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<TaskHandle_t> s_personality_task{nullptr};
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
StaticTask_t s_personality_task_control;
EXT_RAM_BSS_ATTR StackType_t s_personality_task_stack[PERSONALITY_STACK_BYTES / sizeof(StackType_t)];
#endif

// No parameters: expire dispatched symbols before standby touches the transport.
void invalidate_personality_events()
{
    portENTER_CRITICAL(&s_personality_mux);
    ++s_personality_mailbox.generation;
    portEXIT_CRITICAL(&s_personality_mux);
}

// error: Real preparation/admission/playback failure, retained without inventing completion.
// sample_rate_hz: Actual attempted transport rate, or authored rate before transport admission.
// volume_percent: Observed codec volume, or zero when codec policy was unavailable.
esp_err_t retain_personality_failure(esp_err_t error, uint32_t sample_rate_hz, int volume_percent)
{
    portENTER_CRITICAL(&s_personality_mux);
    s_personality_mailbox.status.state = LUNASHELL_CUE_FAILED;
    s_personality_mailbox.status.last_error = error;
    portEXIT_CRITICAL(&s_personality_mux);
    ESP_LOGE(TAG, "Personality audio operation failed (%s)", esp_err_to_name(error));
    publish_rejected_playback(error, sample_rate_hz, volume_percent);
    return error;
}

#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
// generation: Dispatch ownership token invalidated by scene hide or standby.
bool personality_event_current(uint32_t generation)
{
    portENTER_CRITICAL(&s_personality_mux);
    const bool current = s_personality_mailbox.policy.enabled && !s_personality_mailbox.suspended &&
        generation == s_personality_mailbox.generation;
    portEXIT_CRITICAL(&s_personality_mux);
    return current && !s_standby.load(std::memory_order_acquire);
}

// event: One immutable admitted symbol sound; audio mutex is already held.
// outcome: Receives Completed or an explicit suppression outcome.
// sample_rate_hz: Receives the unchanged duplex transport clock.
// volume_percent: Receives the current codec volume; no volume setting is overwritten.
// Exception: Returns actual codec, reference or snapshot failures.
esp_err_t play_personality_locked(const PersonalityAudioEvent &event,
    lunashell_audio_cue_state_t &outcome, uint32_t &sample_rate_hz, int &volume_percent)
{
    outcome = LUNASHELL_CUE_SUPPRESSED;
    if (!personality_event_current(event.generation)) {
        return ESP_OK;
    }
    if (!s_initialized.load(std::memory_order_acquire) || s_audio_state_poisoned.load(std::memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    sample_rate_hz = s_microphone_open ? s_microphone_sample_rate_hz : s_speaker_sample_rate_hz;
    if (sample_rate_hz < 8000 || sample_rate_hz > 48000) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t error = configure_speaker_stream(sample_rate_hz);
    if (error != ESP_OK) {
        return error;
    }
    bool previous_mute = true;
    const int volume_result = esp_codec_dev_get_out_vol(s_speaker, &volume_percent);
    if (volume_result != ESP_CODEC_DEV_OK) {
        return static_cast<esp_err_t>(volume_result);
    }
    const int mute_result = esp_codec_dev_get_out_mute(s_speaker, &previous_mute);
    if (mute_result != ESP_CODEC_DEV_OK) {
        return static_cast<esp_err_t>(mute_result);
    }
    if (volume_percent < 0 || volume_percent > 100) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (volume_percent == 0 || !personality_event_current(event.generation)) {
        return ESP_OK;
    }
    if (esp_timer_get_time() - event.requested_at_us > event.request_expiry_us) {
        outcome = LUNASHELL_CUE_EXPIRED;
        return ESP_OK;
    }
    const int unmute_result = esp_codec_dev_set_out_mute(s_speaker, false);
    error = static_cast<esp_err_t>(unmute_result);
    const size_t output_samples = event.sample_count * sample_rate_hz / event.sample_rate_hz;
    size_t offset = 0;
    bool submitted = false;
    bool cancelled = false;
    while (error == ESP_OK && offset < output_samples) {
        if (!personality_event_current(event.generation)) {
            cancelled = true;
            break;
        }
        // The codec mutex makes the existing typewriter scratch exclusive here too.
        // Resampling in bounded chunks keeps the current microphone clock unchanged.
        const size_t chunk_count = std::min<size_t>(320, output_samples - offset);
        for (size_t index = 0; index < chunk_count; ++index) {
            const uint32_t position = static_cast<uint32_t>(offset + index) * event.sample_rate_hz;
            const size_t source = position / sample_rate_hz;
            const uint32_t fraction = position % sample_rate_hz;
            const size_t next = std::min(source + 1, event.sample_count - 1);
            s_typewriter_transport_samples[index] = static_cast<int16_t>(
                (static_cast<int32_t>(event.samples[source]) * static_cast<int32_t>(sample_rate_hz - fraction) +
                 static_cast<int32_t>(event.samples[next]) * static_cast<int32_t>(fraction)) /
                static_cast<int32_t>(sample_rate_hz));
        }
        if (!submitted) {
            const int64_t started_at_us = esp_timer_get_time();
            portENTER_CRITICAL(&s_personality_mux);
            if (s_personality_mailbox.status.last_error == ESP_OK) {
                s_personality_mailbox.status.state = LUNASHELL_CUE_PLAYING;
            }
            s_personality_mailbox.have_played = true;
            s_personality_mailbox.last_started_us = started_at_us;
            portEXIT_CRITICAL(&s_personality_mux);
        }
        error = write_speaker_pcm_locked(s_typewriter_transport_samples.data(), chunk_count,
            sample_rate_hz, volume_percent, event.cue_id);
        submitted = true;
        offset += chunk_count;
    }
    if (error == ESP_OK && submitted && !cancelled) {
        // Wait only on the audio owner: the final DMA samples must retire before restoring mute.
        const uint32_t clip_ms = static_cast<uint32_t>((output_samples * 1000 + sample_rate_hz - 1) / sample_rate_hz);
        vTaskDelay(pdMS_TO_TICKS(clip_ms) + 1);
    }
    const int restore_result = esp_codec_dev_set_out_mute(s_speaker, previous_mute);
    if (restore_result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Personality mute restoration failed (%d)", restore_result);
        if (error == ESP_OK) {
            error = static_cast<esp_err_t>(restore_result);
        }
    }
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
        false, 0, sample_rate_hz, volume_percent, error, event.cue_id);
    if (publication_error != ESP_OK) {
        ESP_LOGE(TAG, "Personality playback completion publication failed (%s)", esp_err_to_name(publication_error));
        if (error == ESP_OK) {
            error = publication_error;
        }
    }
    if (error == ESP_OK && submitted && !cancelled) {
        outcome = LUNASHELL_CUE_COMPLETED;
    }
    return error;
}

// event: One dispatched immutable symbol cue; it never waits behind another sound.
// outcome: Receives a meaningful suppression/admission/completion state.
// Exception: Returns genuine snapshot/codec errors and publishes them through the existing owner.
esp_err_t handle_personality_event(const PersonalityAudioEvent &event, lunashell_audio_cue_state_t &outcome)
{
    outcome = LUNASHELL_CUE_SUPPRESSED;
    if (!personality_event_current(event.generation)) {
        return ESP_OK;
    }
    if (esp_timer_get_time() - event.requested_at_us > event.request_expiry_us) {
        outcome = LUNASHELL_CUE_EXPIRED;
        return ESP_OK;
    }
    LUNASHELL::ClockSnapshot clock;
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
    if (error != ESP_OK) {
        return retain_personality_failure(error, event.sample_rate_hz, 0);
    }
    if (LUNASHELL::clock_cue_policy(clock) != LUNASHELL::ClockCuePolicy::Allowed) {
        return ESP_OK;
    }
    SemaphoreHandle_t mutex = audio_mutex();
    if (mutex == nullptr) {
        return retain_personality_failure(ESP_ERR_NO_MEM, event.sample_rate_hz, 0);
    }
    if (xSemaphoreTake(mutex, 0) != pdTRUE) {
        outcome = LUNASHELL_CUE_DROPPED_BUSY;
        return ESP_OK;
    }
    if (esp_timer_get_time() - event.requested_at_us > event.request_expiry_us) {
        audio_unlock(mutex);
        outcome = LUNASHELL_CUE_EXPIRED;
        return ESP_OK;
    }
    uint32_t sample_rate_hz = event.sample_rate_hz;
    int volume_percent = 0;
    error = play_personality_locked(event, outcome, sample_rate_hz, volume_percent);
    if (error != ESP_OK) {
        retain_personality_failure(error, sample_rate_hz, volume_percent);
    }
    audio_unlock(mutex);
    return error;
}

// context: Unused; this sole worker owns asynchronous symbol playback within the audio owner.
void personality_audio_worker(void *context)
{
    (void)context;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        PersonalityAudioEvent event;
        portENTER_CRITICAL(&s_personality_mux);
        const bool pending = s_personality_mailbox.pending;
        if (pending) {
            event = s_personality_mailbox.event;
            s_personality_mailbox.pending = false;
        }
        portEXIT_CRITICAL(&s_personality_mux);
        if (!pending) {
            continue;
        }
        lunashell_audio_cue_state_t outcome;
        const esp_err_t error = handle_personality_event(event, outcome);
        portENTER_CRITICAL(&s_personality_mux);
        if (error != ESP_OK) {
            s_personality_mailbox.status.state = LUNASHELL_CUE_FAILED;
            s_personality_mailbox.status.last_error = error;
        } else if (s_personality_mailbox.status.last_error == ESP_OK) {
            s_personality_mailbox.status.state = outcome;
        } else {
            s_personality_mailbox.status.state = LUNASHELL_CUE_FAILED;
        }
        s_personality_mailbox.in_flight = false;
        portEXIT_CRITICAL(&s_personality_mux);
    }
}
#endif
} // namespace

// policy: Build-authored immutable admission settings, copied before worker creation.
// Exception: Returns and retains invalid or changed policy and original preparation failures.
extern "C" esp_err_t LUNASHELL_audio_prepare_personality(const lunashell_personality_audio_policy_t *policy)
{
    if (policy == nullptr || policy->cooldown_us < 0 || policy->request_expiry_us <= 0) {
        ESP_LOGE(TAG, "Personality preparation policy rejected: present=%u cooldown=%lld expiry=%lld",
            policy != nullptr, static_cast<long long>(policy == nullptr ? 0 : policy->cooldown_us),
            static_cast<long long>(policy == nullptr ? 0 : policy->request_expiry_us));
        return retain_personality_failure(ESP_ERR_INVALID_ARG, 16000, 0);
    }
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    lunashell_audio_cue_status_t retained;
    const esp_err_t retained_error = LUNASHELL_audio_get_personality_status(&retained);
    if (retained_error != ESP_OK) {
        return retained_error;
    }
    portENTER_CRITICAL(&s_personality_mux);
    const bool already_configured = s_personality_mailbox.policy_configured;
    const bool same_policy = !already_configured ||
        (s_personality_mailbox.policy.enabled == policy->enabled &&
         s_personality_mailbox.policy.cooldown_us == policy->cooldown_us &&
         s_personality_mailbox.policy.request_expiry_us == policy->request_expiry_us);
    if (!already_configured) {
        s_personality_mailbox.policy = *policy;
        s_personality_mailbox.policy_configured = true;
        if (!policy->enabled) {
            s_personality_mailbox.status.state = LUNASHELL_CUE_SUPPRESSED;
        }
    }
    portEXIT_CRITICAL(&s_personality_mux);
    if (!same_policy) {
        ESP_LOGE(TAG, "Personality preparation cannot replace immutable admission policy");
        return retain_personality_failure(ESP_ERR_INVALID_STATE, 16000, 0);
    }
    if (already_configured) {
        if (s_personality_task.load(std::memory_order_acquire) == nullptr) {
            ESP_LOGE(TAG, "Personality preparation already claimed without a ready worker");
            return retain_personality_failure(ESP_ERR_INVALID_STATE, 16000, 0);
        }
        return LUNASHELL_audio_get_personality_status(&retained);
    }
    const TaskHandle_t worker = xTaskCreateStatic(personality_audio_worker, "audio_personality",
        PERSONALITY_STACK_BYTES, nullptr, 2, s_personality_task_stack, &s_personality_task_control);
    if (worker == nullptr) {
        return retain_personality_failure(ESP_ERR_NO_MEM, 16000, 0);
    }
    s_personality_task.store(worker, std::memory_order_release);
    return LUNASHELL_audio_get_personality_status(&retained);
#else
    return retain_personality_failure(ESP_ERR_NOT_SUPPORTED, 16000, 0);
#endif
}

// samples: Immutable flash-owned original PCM, retained through dispatch and playback.
// sample_count: Exact authored length, limited to 350 ms of mono PCM16.
// sample_rate_hz: Exact authored format, 16000 Hz.
// cue_id: Stable application symbol identity preserved through the worker.
// status: Receives the request disposition without mistaking admission for completion.
// Exception: Returns genuine argument/admission or retained asynchronous errors.
extern "C" esp_err_t LUNASHELL_audio_notify_personality(const int16_t *samples, size_t sample_count,
    uint32_t sample_rate_hz, uint32_t cue_id, lunashell_audio_cue_status_t *status)
{
    if (status == nullptr || samples == nullptr || sample_count == 0 || sample_count > 5600 ||
        sample_rate_hz != 16000 || cue_id == 0) {
        const esp_err_t error = retain_personality_failure(ESP_ERR_INVALID_ARG, sample_rate_hz, 0);
        if (status != nullptr) {
            portENTER_CRITICAL(&s_personality_mux);
            *status = s_personality_mailbox.status;
            portEXIT_CRITICAL(&s_personality_mux);
        }
        return error;
    }
    const TaskHandle_t worker = s_personality_task.load(std::memory_order_acquire);
    if (worker == nullptr) {
        const esp_err_t retained_error = LUNASHELL_audio_get_personality_status(status);
        if (retained_error != ESP_OK) {
            return retained_error;
        }
        const esp_err_t error = retain_personality_failure(ESP_ERR_INVALID_STATE, sample_rate_hz, 0);
        const esp_err_t observation_error = LUNASHELL_audio_get_personality_status(status);
        if (observation_error != error) {
            ESP_LOGE(TAG, "Personality preparation failure observation changed (%s)", esp_err_to_name(observation_error));
        }
        return error;
    }
    const int64_t now_us = esp_timer_get_time();
    bool dispatched = false;
    portENTER_CRITICAL(&s_personality_mux);
    *status = s_personality_mailbox.status;
    status->cue_id = cue_id;
    if (status->last_error != ESP_OK) {
        // Retained failures remain visible until an explicit new preparation/recovery design exists.
    } else if (!s_personality_mailbox.policy.enabled || s_personality_mailbox.suspended ||
               s_standby.load(std::memory_order_acquire)) {
        status->state = LUNASHELL_CUE_SUPPRESSED;
    } else if (s_personality_mailbox.in_flight) {
        status->state = LUNASHELL_CUE_DROPPED_BUSY;
    } else if (s_personality_mailbox.have_played &&
               now_us - s_personality_mailbox.last_started_us < s_personality_mailbox.policy.cooldown_us) {
        status->state = LUNASHELL_CUE_DROPPED_COOLDOWN;
    } else {
        ++s_personality_mailbox.status.request_sequence;
        s_personality_mailbox.status.cue_id = cue_id;
        s_personality_mailbox.status.state = LUNASHELL_CUE_DISPATCHED;
        s_personality_mailbox.event = {samples, sample_count, sample_rate_hz, cue_id,
            s_personality_mailbox.generation, now_us, s_personality_mailbox.policy.request_expiry_us};
        s_personality_mailbox.pending = true;
        s_personality_mailbox.in_flight = true;
        *status = s_personality_mailbox.status;
        dispatched = true;
    }
    portEXIT_CRITICAL(&s_personality_mux);
    if (dispatched && xTaskNotifyGive(worker) != pdPASS) {
        portENTER_CRITICAL(&s_personality_mux);
        s_personality_mailbox.pending = false;
        s_personality_mailbox.in_flight = false;
        portEXIT_CRITICAL(&s_personality_mux);
        const esp_err_t error = retain_personality_failure(ESP_FAIL, sample_rate_hz, 0);
        const esp_err_t retained_error = LUNASHELL_audio_get_personality_status(status);
        if (retained_error != error) {
            ESP_LOGE(TAG, "Personality notification failure observation changed (%s)", esp_err_to_name(retained_error));
        }
        return error;
    }
    return status->last_error;
}

// suspended: Invalidates the single dispatch without queuing it for resume.
// Exception: Returns any retained owner error.
extern "C" esp_err_t LUNASHELL_audio_set_personality_suspended(bool suspended)
{
    portENTER_CRITICAL(&s_personality_mux);
    if (s_personality_mailbox.suspended != suspended) {
        s_personality_mailbox.suspended = suspended;
        ++s_personality_mailbox.generation;
    }
    const esp_err_t error = s_personality_mailbox.status.last_error;
    portEXIT_CRITICAL(&s_personality_mux);
    return error;
}

// status: Receives a coherent outcome and its retained real error.
// Exception: Returns rejected output pointer or the original retained owner failure.
extern "C" esp_err_t LUNASHELL_audio_get_personality_status(lunashell_audio_cue_status_t *status)
{
    if (status == nullptr) {
        ESP_LOGE(TAG, "Personality status pointer rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_personality_mux);
    *status = s_personality_mailbox.status;
    portEXIT_CRITICAL(&s_personality_mux);
    return status->last_error;
}
