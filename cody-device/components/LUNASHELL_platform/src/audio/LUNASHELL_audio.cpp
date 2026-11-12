/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 * Original file notice:
 *
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * VBWR E */


#include "LUNASHELL_platform.h"
#include "LUNASHELL_audio_mixer.hpp"
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
#include "LUNASHELL_audio_stream.hpp"
#include "LUNASHELL_audio_resampler.hpp"
#endif
#include "LUNASHELL_audio_controls.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_clock.hpp"
#include "LUNASHELL_audio_sequence.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <cmath>
#include <mutex>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
#include "LUNASHELL_voice_capture_guard.hpp"
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS || CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
#include "driver/i2s_common.h"
#endif

#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
#define LUNASHELL_AUDIO_HAS_OUTPUT 1
#else
#define LUNASHELL_AUDIO_HAS_OUTPUT 0
#endif

namespace {

const char *TAG = "LUNASHELL_audio";
constexpr uint32_t AUDIO_LOCK_TIMEOUT_MS = 5000;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
constexpr uint32_t VOICE_SAMPLE_RATE_HZ = 16000;
constexpr size_t VOICE_REFERENCE_CHUNK_SAMPLES = VOICE_SAMPLE_RATE_HZ / 50U;
#endif
esp_codec_dev_handle_t s_speaker;
esp_codec_dev_handle_t s_microphone;
uint32_t s_speaker_sample_rate_hz;
// Zero means there is no fully configured output stream to admit a producer.
std::atomic<uint32_t> s_output_transport_rate{0};
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
std::atomic_bool s_standby_transition;
std::atomic<uint32_t> s_audio_lifecycle_generation{0};
// Bit zero is admission; the other bits change on every actual preference transition.
// One atomic word keeps request revisions coherent before any worker exists.
std::atomic<uint32_t> s_user_sounds_state{1U};
// Opted in by Cody startup only; ESP_OK means its initial saved policy is ready.
std::atomic_bool s_cody_output_policy_armed{false};
std::atomic<esp_err_t> s_cody_output_policy_error{ESP_OK};
std::atomic_bool s_audio_state_poisoned;
// The owner retains driver-accepted policy; the settings owner persists it.
// esp_codec_dev 1.5.11 getters expose driver state, not physical register readback.
std::atomic<uint8_t> s_user_output_volume{90};
std::atomic<uint8_t> s_user_input_gain{24};
StaticSemaphore_t s_audio_mutex_control;
SemaphoreHandle_t s_audio_mutex;

constexpr uint32_t TYPEWRITER_INTERVAL_US = 50000;
constexpr uint32_t TYPEWRITER_MAX_AGE_US = 150000;
constexpr uint32_t TYPEWRITER_EVENT = 1U;
constexpr uint32_t TYPEWRITER_STACK_BYTES = 4096;
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
enum class TypewriterEventKind : uint8_t { Glyph, Touch };
struct TypewriterEvent {
    uint32_t requested_at_us = 0;
    uint32_t generation = 0;
    uint32_t sequence = 0;
    TypewriterEventKind kind = TypewriterEventKind::Glyph;
};
struct TypewriterMailbox {
    bool pending = false;
    bool suspended = false;
    uint32_t generation = 0;
    uint32_t sequence = 0;
    uint32_t touch_generation = 0;
    TypewriterEvent latest;
};
TypewriterMailbox s_typewriter_mailbox;
portMUX_TYPE s_typewriter_mailbox_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> s_feedback_glyph_generation{0};
std::atomic<uint32_t> s_feedback_touch_generation{0};
// No parameters: cancels any symbol dispatch before the codec changes lifecycle ownership.
void invalidate_personality_events();
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
// No parameters: identifies the mixer task to reject blocking self-submission.
bool on_cue_worker();
esp_err_t ensure_cue_worker();
#endif

// No parameters: preserve Luna's existing policy until Cody explicitly opts in.
esp_err_t cody_output_policy_error()
{
    return s_cody_output_policy_armed.load(std::memory_order_acquire)
        ? s_cody_output_policy_error.load(std::memory_order_acquire) : ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
// revision: Opaque Sounds gate token captured by one logical cue request.
bool user_sound_request_current(uint32_t revision)
{
    return (revision & 1U) != 0U && s_user_sounds_state.load(std::memory_order_acquire) == revision;
}
#endif
std::atomic<esp_err_t> s_typewriter_last_error{ESP_OK};
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
std::array<int16_t, 400> s_typewriter_touch_samples{};
#endif

// No parameters: cancels scene and touch cues before the transport enters standby.
void invalidate_typewriter_events()
{
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    s_typewriter_mailbox.pending = false;
    ++s_typewriter_mailbox.generation;
    ++s_typewriter_mailbox.touch_generation;
    s_feedback_glyph_generation.store(s_typewriter_mailbox.generation, std::memory_order_release);
    s_feedback_touch_generation.store(s_typewriter_mailbox.touch_generation, std::memory_order_release);
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
    const bool allowed = claimed &&
        (event.kind == TypewriterEventKind::Touch || !s_typewriter_mailbox.suspended) &&
        !s_standby.load(std::memory_order_acquire) && LUNASHELL_audio_user_sounds_enabled();
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
// event: Immutable scene or touch epoch; Sounds Off invalidates both kinds.
bool typewriter_event_current(const TypewriterEvent &event)
{
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    const bool current = event.kind == TypewriterEventKind::Touch
        ? event.generation == s_typewriter_mailbox.touch_generation
        : !s_typewriter_mailbox.suspended && event.generation == s_typewriter_mailbox.generation;
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
    return current && LUNASHELL_audio_user_sounds_enabled();
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

// No parameters: closes owned streams only after the capture owner acknowledges pause.
// Exception: Returns ownership rejection or the first exact codec close error.
esp_err_t close_streams()
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    if (LUNASHELL_voice_afe_capture_active()) {
        ESP_LOGE(TAG, "Closing duplex streams rejected: AFE capture is active");
        return ESP_ERR_NOT_ALLOWED;
    }
#endif
    esp_err_t error = ESP_OK;
    if (s_speaker_open) {
        const int result = esp_codec_dev_close(s_speaker);
        if (result == ESP_CODEC_DEV_OK) {
            s_speaker_open = false;
            s_speaker_sample_rate_hz = 0;
            s_speaker_channel_count = 0;
            s_output_transport_rate.store(0, std::memory_order_release);
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
    s_output_transport_rate.store(0, std::memory_order_release);
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
    const int volume_error = esp_codec_dev_set_out_vol(s_speaker, s_user_output_volume.load(std::memory_order_acquire));
    if (volume_error != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Restoring user output volume after open failed: %d", volume_error);
        const int close_error = esp_codec_dev_close(s_speaker);
        if (close_error == ESP_CODEC_DEV_OK) s_speaker_open = false;
        else { s_audio_state_poisoned.store(true, std::memory_order_release); ESP_LOGE(TAG, "Speaker close after volume failure: %d", close_error); }
        return static_cast<esp_err_t>(volume_error);
    }
    s_output_transport_rate.store(sample_rate_hz, std::memory_order_release);
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
    const int gain_error = esp_codec_dev_set_in_gain(s_microphone, static_cast<float>(s_user_input_gain.load(std::memory_order_acquire)));
    if (gain_error != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Restoring user input gain after open failed: %d", gain_error);
        const int close_error = esp_codec_dev_close(s_microphone);
        if (close_error == ESP_CODEC_DEV_OK) s_microphone_open = false;
        else { s_audio_state_poisoned.store(true, std::memory_order_release); ESP_LOGE(TAG, "Microphone close after gain failure: %d", close_error); }
        return static_cast<esp_err_t>(gain_error);
    }
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

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
// No parameters: prepare the 16 kHz route while the caller owns the audio mutex.
// Exception: Returns the original lifecycle, stream or codec policy error.
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
        const int volume_result = esp_codec_dev_set_out_vol(s_speaker, s_user_output_volume.load(std::memory_order_acquire));
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
        const int gain_result = esp_codec_dev_set_in_gain(s_microphone, static_cast<float>(s_user_input_gain.load(std::memory_order_acquire)));
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

#endif

}  // namespace

// No parameters: Cody calls this once before any of its startup cues are admitted.
extern "C" void LUNASHELL_audio_begin_cody_output_policy(void)
{
    s_cody_output_policy_error.store(ESP_ERR_NOT_FINISHED, std::memory_order_relaxed);
    s_cody_output_policy_armed.store(true, std::memory_order_release);
    invalidate_typewriter_events();
    invalidate_personality_events();
}

// error: Original initial output-policy result, updated after snapshot publication.
// Once open, this startup gate does not change during later playback or settings edits.
extern "C" void LUNASHELL_audio_update_cody_output_policy(esp_err_t error)
{
    if (!s_cody_output_policy_armed.load(std::memory_order_acquire) ||
        s_cody_output_policy_error.load(std::memory_order_acquire) == ESP_OK) return;
    s_cody_output_policy_error.store(error, std::memory_order_release);
}

// enabled: Explicit Sounds admission preference; persistence belongs to the settings owner.
// No worker, codec or volume operation is attempted, including before audio initialization.
extern "C" esp_err_t LUNASHELL_audio_set_user_sounds_enabled(bool enabled)
{
    uint32_t observed = s_user_sounds_state.load(std::memory_order_acquire);
    for (;;) {
        if (((observed & 1U) != 0U) == enabled) {
            return ESP_OK;
        }
        const uint32_t revision = (observed + 2U) & ~1U;
        const uint32_t desired = revision | (enabled ? 1U : 0U);
        if (s_user_sounds_state.compare_exchange_weak(observed, desired, std::memory_order_acq_rel)) {
            if (!enabled) {
                invalidate_typewriter_events();
                invalidate_personality_events();
            }
            ESP_LOGI(TAG, "User sounds admission %s; codec volume and microphone unchanged", enabled ? "enabled" : "disabled");
            return ESP_OK;
        }
    }
}

// No parameters: exposes only the existing audio owner's atomic preference gate.
extern "C" bool LUNASHELL_audio_user_sounds_enabled(void)
{
    return (s_user_sounds_state.load(std::memory_order_acquire) & 1U) != 0U;
}

// No parameters: opaque request revision, changed even when an Off/On pair ends enabled.
extern "C" uint32_t LUNASHELL_audio_user_sounds_revision(void)
{
    return s_user_sounds_state.load(std::memory_order_acquire);
}

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

// No parameters: initializes the board codec once; registered user cues share the Sounds gate.
// Exception: Returns original initialization, lifecycle or admitted startup-cue failures.
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
        const int volume_result = esp_codec_dev_set_out_vol(s_speaker, s_user_output_volume.load(std::memory_order_acquire));
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
        const int gain_result = esp_codec_dev_set_in_gain(s_microphone, static_cast<float>(s_user_input_gain.load(std::memory_order_acquire)));
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
            bool completed = false;
            error = LUNASHELL_audio_play_user_cue_pcm16(s_startup_cue.samples,
                s_startup_cue.sample_count, s_startup_cue.sample_rate_hz,
                s_startup_cue.volume_percent, LUNASHELL_audio_user_sounds_revision(), &completed);
            s_initialization_result.store(error, std::memory_order_release);
            if (error != ESP_OK) {
                ESP_LOGE(TAG, "Audio-ready cue failed: %s", esp_err_to_name(error));
            } else if (!completed) {
                ESP_LOGI(TAG, "Audio-ready cue suppressed by Sounds preference; no completion claimed");
            }
        }
    } else {
        ESP_LOGE(TAG, "Audio route initialization failed (%s)", esp_err_to_name(error));
    }
    return error;
}

namespace {
// input: True selects ES7210 gain; false selects ES8311 output volume.
// write: True applies and verifies the requested value; false reads the current codec.
// requested: Validated target value, used only for writes.
// applied: Receives the driver's reported value on success; no hardware readback is claimed.
// Exception: Returns busy, lifecycle, codec, readback or rollback failure.
esp_err_t access_user_audio_control(bool input, bool write, uint8_t requested, uint8_t *applied)
{
    if (applied == nullptr || (write && ((!input && requested > 100) ||
        (input && (requested > 30 || requested % 3 != 0))))) {
        ESP_LOGE(TAG, "User audio control arguments rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    if (input) return ESP_ERR_NOT_SUPPORTED;
#endif
#if !CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (!input) return ESP_ERR_NOT_SUPPORTED;
#endif
    esp_err_t error = LUNASHELL_audio_init();
    if (error != ESP_OK) return error;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    // Do not delay the AFE feed behind a playback/codec lock. The settings
    // worker may retry after the current RX buffer has retired.
    std::unique_lock<std::mutex> voice_guard(LUNASHELL::voice_capture_guard(), std::defer_lock);
    if (input && !voice_guard.try_lock()) return ESP_ERR_NOT_FINISHED;
#endif
    SemaphoreHandle_t mutex = audio_mutex();
    if (mutex == nullptr) return ESP_ERR_NO_MEM;
    // Settings must not delay startup or a live capture/playback owner. The
    // caller retains the busy result and can retry when the route is available.
    if (xSemaphoreTake(mutex, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;
    if (s_standby.load(std::memory_order_acquire) || s_audio_state_poisoned.load(std::memory_order_acquire) ||
        (input ? !s_microphone_open : !s_speaker_open)) {
        audio_unlock(mutex);
        return ESP_ERR_INVALID_STATE;
    }
    int previous = -1;
    int current = -1;
    int result = ESP_CODEC_DEV_OK;
    if (input) {
#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
        float gain = -1;
        result = esp_codec_dev_get_in_gain(s_microphone, &gain);
        if (result == ESP_CODEC_DEV_OK && std::isfinite(gain) && gain >= 0 && gain <= 30 &&
            std::fabs(gain - std::round(gain)) < 0.01f && static_cast<int>(gain) % 3 == 0) previous = static_cast<int>(gain);
#endif
    } else {
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
        result = esp_codec_dev_get_out_vol(s_speaker, &previous);
#endif
    }
    if (result != ESP_CODEC_DEV_OK) error = static_cast<esp_err_t>(result);
    else if (previous < 0 || previous > (input ? 30 : 100)) error = ESP_ERR_INVALID_RESPONSE;
    current = previous;
    if (error == ESP_OK && write) {
        if (input) {
#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
            result = esp_codec_dev_set_in_gain(s_microphone, static_cast<float>(requested));
            float gain = -1;
            if (result == ESP_CODEC_DEV_OK) result = esp_codec_dev_get_in_gain(s_microphone, &gain);
            current = std::isfinite(gain) && gain >= 0 && gain <= 30 && std::fabs(gain - std::round(gain)) < 0.01f
                ? static_cast<int>(gain) : -1;
#endif
        } else {
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
            result = esp_codec_dev_set_out_vol(s_speaker, requested);
            if (result == ESP_CODEC_DEV_OK) result = esp_codec_dev_get_out_vol(s_speaker, &current);
#endif
        }
        if (result != ESP_CODEC_DEV_OK) error = static_cast<esp_err_t>(result);
        else if (current != requested) error = ESP_ERR_INVALID_RESPONSE;
        if (error != ESP_OK) {
            int rollback = ESP_CODEC_DEV_OK;
            if (input) {
#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
                rollback = esp_codec_dev_set_in_gain(s_microphone, static_cast<float>(previous));
                float restored = -1;
                if (rollback == ESP_CODEC_DEV_OK) rollback = esp_codec_dev_get_in_gain(s_microphone, &restored);
                if (rollback == ESP_CODEC_DEV_OK && (!std::isfinite(restored) ||
                    std::fabs(restored - static_cast<float>(previous)) >= 0.01f)) rollback = ESP_FAIL;
#endif
            } else {
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
                rollback = esp_codec_dev_set_out_vol(s_speaker, previous);
                int restored = -1;
                if (rollback == ESP_CODEC_DEV_OK) rollback = esp_codec_dev_get_out_vol(s_speaker, &restored);
                if (rollback == ESP_CODEC_DEV_OK && restored != previous) rollback = ESP_FAIL;
#endif
            }
            if (rollback != ESP_CODEC_DEV_OK) {
                s_audio_state_poisoned.store(true, std::memory_order_release);
                ESP_LOGE(TAG, "Audio control rollback failed: input=%u error=%d", input, rollback);
            }
        } else if (input) {
            s_user_input_gain.store(requested, std::memory_order_release);
        } else {
            s_user_output_volume.store(requested, std::memory_order_release);
        }
    }
    audio_unlock(mutex);
    if (error == ESP_OK) *applied = static_cast<uint8_t>(current);
    else ESP_LOGE(TAG, "User audio control %s failed: input=%u (%s)", write ? "write" : "read", input, esp_err_to_name(error));
    return error;
}
} // namespace

// percent: Exact 0..100 output selection.
// applied: Receives the driver's reported value only after the request succeeds.
// Exception: Returns original argument, busy, lifecycle or codec failure.
extern "C" esp_err_t LUNASHELL_audio_set_user_output_volume(uint8_t percent, uint8_t *applied)
{
    return access_user_audio_control(false, true, percent, applied);
}

// applied: Receives the codec driver's current volume selection.
// Exception: Returns original argument, busy, lifecycle or codec failure.
extern "C" esp_err_t LUNASHELL_audio_read_user_output_volume(uint8_t *applied)
{
    return access_user_audio_control(false, false, 0, applied);
}

// gain_db: ES7210 gain from zero to 30 dB in exact 3-dB steps.
// applied: Receives the driver's reported value only after the request succeeds.
// Exception: Returns original argument, busy, lifecycle or codec failure.
extern "C" esp_err_t LUNASHELL_audio_set_user_input_gain(uint8_t gain_db, uint8_t *applied)
{
    return access_user_audio_control(true, true, gain_db, applied);
}

// applied: Receives the driver-reported shared input gain.
// Exception: Returns original argument, busy, lifecycle or codec failure.
extern "C" esp_err_t LUNASHELL_audio_read_user_input_gain(uint8_t *applied)
{
    return access_user_audio_control(true, false, 0, applied);
}

namespace {
// samples: Immutable PCM at the actual transport sample rate.
// sample_count: Exact count, already validated by the public owner.
// sample_rate_hz: Actual clock shared with the microphone when open.
// volume_percent: Observed or explicitly selected codec output policy.
// cue_id: Stable application sound identity, published only after successful writes.
// user_sounds_revision: Optional application-cue token; generic PCM/voice writes leave it null.
// completed: Optional exact completion result; false if the Sounds request was invalidated.
// Exception: Returns original AFE, codec write, or snapshot publication failures.
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
esp_err_t write_speaker_pcm_locked(const int16_t *samples, size_t sample_count,
                                  uint32_t sample_rate_hz, int volume_percent, uint32_t cue_id = 0,
                                  const uint32_t *user_sounds_revision = nullptr, bool *completed = nullptr)
{
    if (completed != nullptr) {
        *completed = false;
    }
    size_t offset = 0;
    while (offset < sample_count) {
        // All PCM paths, including speech and feedback, hold the codec mutex here.
        const esp_err_t policy_error = cody_output_policy_error();
        if (policy_error != ESP_OK) return policy_error;
        if (user_sounds_revision != nullptr && !user_sound_request_current(*user_sounds_revision)) {
            return ESP_OK; // Intentional cancellation is separate from real operation errors.
        }
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
        const bool voice_afe_active = LUNASHELL_voice_afe_running();
        const size_t chunk_limit = voice_afe_active ? VOICE_REFERENCE_CHUNK_SAMPLES : sample_rate_hz / 50U;
#else
        const size_t chunk_limit = sample_rate_hz / 50U;
#endif
        const size_t chunk_samples = std::min(chunk_limit, sample_count - offset);
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
        if (voice_afe_active) {
            const esp_err_t reference_error = LUNASHELL_voice_afe_push_speaker_pcm16(
                samples + offset, chunk_samples, sample_rate_hz);
            if (reference_error != ESP_OK) {
                ESP_LOGE(TAG, "AEC speaker reference enqueue failed (%s)", esp_err_to_name(reference_error));
                return reference_error;
            }
        }
#endif
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
    if (completed != nullptr) {
        *completed = true;
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
// sample_count: Exact authored source length.
// sample_rate_hz: Authored sample rate; voice playback uses the fixed duplex clock.
// volume_percent: Codec output volume in 0..100.
// user_sounds_revision: Optional cancellation token for an application cue; null is generic PCM.
// completed: Receives false for Sounds cancellation, independent of the real error return.
// Exception: Returns original standby, codec, format or snapshot publication errors.
static esp_err_t play_pcm16_owned(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    int volume_percent,
    const uint32_t *user_sounds_revision,
    bool &completed
)
{
    completed = false;
#if !CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    (void)samples; (void)sample_count; (void)user_sounds_revision;
    LUNASHELL_hardware_report_speaker(false, ESP_ERR_NOT_SUPPORTED);
    return reject_playback("Disabled playback route", ESP_ERR_NOT_SUPPORTED, sample_rate_hz, volume_percent);
#else
    if (on_cue_worker()) return reject_playback("Playback reentrancy", ESP_ERR_INVALID_STATE, sample_rate_hz, volume_percent);
    if (samples == nullptr || sample_count == 0 || sample_count > UINT32_MAX ||
        sample_rate_hz < 8000 || sample_rate_hz > 48000 || volume_percent < 0 || volume_percent > 100)
        return reject_playback("Playback format", ESP_ERR_INVALID_ARG, sample_rate_hz, volume_percent);
    if (user_sounds_revision != nullptr && !user_sound_request_current(*user_sounds_revision)) return ESP_OK;
    auto error = cody_output_policy_error();
    if (error == ESP_OK) error = LUNASHELL_audio_init();
    if (error != ESP_OK) return reject_playback("Playback admission", error, sample_rate_hz, volume_percent);
    const unsigned master = s_user_output_volume.load(std::memory_order_acquire);
    if (master == 0 || volume_percent == 0) {
        return user_sounds_revision != nullptr ? ESP_OK :
            reject_playback("Playback muted", ESP_ERR_INVALID_STATE, sample_rate_hz, volume_percent);
    }
    // The shared hardware volume remains at the user's master; relative source gain
    // approximates a quieter requested percentage without changing other voices.
    const auto relative_gain = static_cast<uint16_t>(std::min<unsigned>(1000U,
        static_cast<unsigned>(volume_percent) * 1000U / master));
    constexpr uint32_t kBlockingPcmCue = 0x50434d01U;
    const lunashell_audio_request_t request{samples, sample_count, sample_rate_hz, kBlockingPcmCue,
        user_sounds_revision == nullptr ? LUNASHELL_AUDIO_SYSTEM : LUNASHELL_AUDIO_USER_PCM,
        LUNASHELL_CODEX_CUE_NOTICE, 1, 0, LUNASHELL_AUDIO_WAVE_PCM, 0, relative_gain,
        0, 0, 0, user_sounds_revision == nullptr ? 0U : *user_sounds_revision, true, 0, 0};
    lunashell_audio_cue_status_t status{};
    error = LUNASHELL_audio_submit(&request, &status);
    if (error != ESP_OK) return error;
    if (status.state == LUNASHELL_CUE_SUPPRESSED)
        return user_sounds_revision != nullptr ? ESP_OK :
            reject_playback("Playback suppressed", ESP_ERR_INVALID_STATE, sample_rate_hz, volume_percent);
    if (status.state != LUNASHELL_CUE_DISPATCHED || status.request_sequence == 0)
        return reject_playback("Playback mixer busy", ESP_ERR_NOT_FINISHED, sample_rate_hz, volume_percent);
    // A pinned terminal record owns the borrowed source until the worker has
    // finished reading it. On timeout request cancellation, then keep waiting
    // for that exact terminal acknowledgement before allowing the caller to reuse PCM.
    const int64_t deadline = esp_timer_get_time() +
        static_cast<int64_t>((static_cast<uint64_t>(sample_count) * 1000000ULL) / sample_rate_hz) + 5000000LL;
    bool timed_out = false;
    bool observation_error_logged = false;
    for (;;) {
        const auto observation_error = LUNASHELL_audio_get_cue_status(status.request_sequence, kBlockingPcmCue, &status);
        if (observation_error == ESP_OK && status.state != LUNASHELL_CUE_DISPATCHED &&
            status.state != LUNASHELL_CUE_PLAYING) break;
        if (observation_error != ESP_OK && !observation_error_logged) {
            ESP_LOGE(TAG, "Pinned playback status unavailable (%s)", esp_err_to_name(observation_error));
            observation_error_logged = true;
        }
        if (!timed_out && esp_timer_get_time() >= deadline) {
            timed_out = true;
            const auto cancel_error = LUNASHELL_audio_cancel_cue(status.request_sequence, kBlockingPcmCue);
            if (cancel_error != ESP_OK) ESP_LOGE(TAG, "Playback cancellation failed (%s)", esp_err_to_name(cancel_error));
        }
        vTaskDelay(pdMS_TO_TICKS(10) + 1U);
    }
    const auto acknowledgement = LUNASHELL_audio_ack_cue(status.request_sequence, kBlockingPcmCue);
    if (acknowledgement != ESP_OK) return acknowledgement;
    if (timed_out) return ESP_ERR_TIMEOUT;
    if (status.last_error != ESP_OK) return status.last_error;
    if (status.state != LUNASHELL_CUE_COMPLETED && user_sounds_revision == nullptr) return ESP_ERR_INVALID_STATE;
    completed = status.state == LUNASHELL_CUE_COMPLETED &&
        (user_sounds_revision == nullptr || user_sound_request_current(*user_sounds_revision));
    return ESP_OK;
#endif
}

// samples: Borrowed mono PCM16 for explicit capture/diagnostic playback.
// sample_count: Exact source sample count.
// sample_rate_hz: Authored source rate.
// volume_percent: Explicit playback volume.
// Exception: Returns original codec, format or publication errors.
extern "C" esp_err_t LUNASHELL_audio_play_pcm16(const int16_t *samples, size_t sample_count,
    uint32_t sample_rate_hz, int volume_percent)
{
    bool completed = false;
    return play_pcm16_owned(samples, sample_count, sample_rate_hz, volume_percent, nullptr, completed);
}

// samples: Immutable mono PCM16 for a user-governed cue.
// sample_count: Exact source sample count.
// sample_rate_hz: Authored source rate.
// volume_percent: Explicit playback volume if the request is admitted.
// user_sounds_revision: Opaque token for this logical request, retained across boot clips.
// completed: Receives true only after all PCM and cleanup succeed.
// Exception: Returns original argument, codec or publication errors; cancellation remains explicit.
extern "C" esp_err_t LUNASHELL_audio_play_user_cue_pcm16(const int16_t *samples, size_t sample_count,
    uint32_t sample_rate_hz, int volume_percent, uint32_t user_sounds_revision, bool *completed)
{
    if (completed == nullptr) {
        return reject_playback("User cue completion destination", ESP_ERR_INVALID_ARG, sample_rate_hz, volume_percent);
    }
    return play_pcm16_owned(samples, sample_count, sample_rate_hz, volume_percent,
        &user_sounds_revision, *completed);
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
    if (samples == nullptr || sample_count == 0 || sample_count > INT_MAX / sizeof(int16_t)) {
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
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    std::unique_lock<std::mutex> voice_guard(LUNASHELL::voice_capture_guard(), std::try_to_lock);
    if (!voice_guard.owns_lock() || LUNASHELL_voice_afe_capture_active()) return ESP_ERR_NOT_FINISHED;
#endif
    SemaphoreHandle_t mutex = audio_mutex();
    if (mutex == nullptr) return ESP_ERR_NO_MEM;
    // Never hold the AFE capture guard while waiting behind a long speaker cue.
    if (xSemaphoreTake(mutex, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;

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
            error = static_cast<esp_err_t>(gain_result);
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
            error = static_cast<esp_err_t>(result);
        }
    }
    if (s_microphone_open) {
        const int restore_result = esp_codec_dev_set_in_gain(s_microphone, static_cast<float>(s_user_input_gain.load(std::memory_order_acquire)));
        if (restore_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Restoring user gain after mono capture failed (%d)", restore_result);
            s_audio_state_poisoned.store(true, std::memory_order_release);
            if (error == ESP_OK) error = static_cast<esp_err_t>(restore_result);
        }
    }
    audio_unlock(mutex);
    return error;
#endif
}

// samples: Caller-owned destination for diagnostic interleaved PCM16.
// frame_count: Number of frames per channel requested by the diagnostic owner.
// channel_count: Enabled diagnostic microphone topology.
// sample_rate_hz: Requested diagnostic transport clock.
// gain_db: Explicit diagnostic codec gain when use_user_gain is false.
// use_user_gain: Selects the current shared gain inside the codec lock.
// Exception: Returns original admission, initialization, mutex or codec failures.
static esp_err_t capture_pcm16_channels_owned(
    int16_t *samples,
    size_t frame_count,
    uint8_t channel_count,
    uint32_t sample_rate_hz,
    float gain_db,
    bool use_user_gain
)
{
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    (void)samples;
    (void)frame_count;
    (void)channel_count;
    (void)sample_rate_hz;
    (void)gain_db;
    (void)use_user_gain;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (samples == nullptr || frame_count == 0 || channel_count == 0 || channel_count > 2 ||
        frame_count > INT_MAX / sizeof(int16_t) / channel_count) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    if (LUNASHELL_voice_afe_capture_active()) {
        ESP_LOGE(TAG, "Diagnostic PCM capture rejected: AFE owns the microphone");
        return ESP_ERR_NOT_ALLOWED;
    }
#endif
    esp_err_t error = LUNASHELL_audio_init();
    if (error != ESP_OK) {
        return error;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    std::unique_lock<std::mutex> voice_guard(LUNASHELL::voice_capture_guard(), std::try_to_lock);
    if (!voice_guard.owns_lock() || LUNASHELL_voice_afe_capture_active()) return ESP_ERR_NOT_FINISHED;
#endif
    SemaphoreHandle_t mutex = audio_mutex();
    if (mutex == nullptr) return ESP_ERR_NO_MEM;
    // The AFE may start while this diagnostic request is admitted.
    if (xSemaphoreTake(mutex, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;
    if (use_user_gain) gain_db = static_cast<float>(s_user_input_gain.load(std::memory_order_acquire));
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
            error = static_cast<esp_err_t>(gain_result);
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
            error = static_cast<esp_err_t>(result);
        }
    }
    if (s_microphone_open) {
        const int restore_result = esp_codec_dev_set_in_gain(s_microphone, static_cast<float>(s_user_input_gain.load(std::memory_order_acquire)));
        if (restore_result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Restoring user gain after diagnostic capture failed (%d)", restore_result);
            s_audio_state_poisoned.store(true, std::memory_order_release);
            if (error == ESP_OK) error = static_cast<esp_err_t>(restore_result);
        }
    }
    audio_unlock(mutex);
    return error;
#endif
}

// samples: Caller-owned destination for explicit diagnostic PCM.
// frame_count: Number of frames per channel.
// channel_count: Configured input topology.
// sample_rate_hz: Requested transport rate.
// gain_db: Explicit diagnostic gain, restored to user policy afterwards.
// Exception: Returns original validation, capture or restoration failure.
extern "C" esp_err_t LUNASHELL_audio_capture_pcm16_channels(int16_t *samples, size_t frame_count,
    uint8_t channel_count, uint32_t sample_rate_hz, float gain_db)
{
    return capture_pcm16_channels_owned(samples, frame_count, channel_count, sample_rate_hz, gain_db, false);
}

// samples: Caller-owned destination for user-policy microphone levels.
// frame_count: Number of frames per channel.
// channel_count: Configured input topology.
// sample_rate_hz: Requested transport rate.
// Exception: Returns original validation, capture or restoration failure.
extern "C" esp_err_t LUNASHELL_audio_capture_user_levels_pcm16(int16_t *samples, size_t frame_count,
    uint8_t channel_count, uint32_t sample_rate_hz)
{
    return capture_pcm16_channels_owned(samples, frame_count, channel_count, sample_rate_hz, 0, true);
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
// samples: Caller-owned destination for real interleaved microphone PCM16.
// frame_count: Number of frames per channel to read.
// channel_count: Enabled acquisition topology, validated by the audio owner.
// Exception: Returns real format, lifecycle or codec capture errors without substituting samples.
extern "C" esp_err_t LUNASHELL_audio_capture_voice_pcm16(
    int16_t *samples,
    size_t frame_count,
    uint8_t channel_count
)
{
    if (samples == nullptr || frame_count == 0 || channel_count != LUNASHELL_audio_input_channel_count() ||
        channel_count == 0 || channel_count > 2 || frame_count > INT_MAX / sizeof(int16_t) / channel_count) {
        ESP_LOGE(TAG, "Voice capture format rejected: frames=%u channels=%u", static_cast<unsigned>(frame_count), channel_count);
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized.load(std::memory_order_acquire) || s_standby.load(std::memory_order_acquire) ||
        s_audio_state_poisoned.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "Voice capture rejected by audio lifecycle state");
        return ESP_ERR_INVALID_STATE;
    }
    // AFE is the sole RX owner. Its acknowledged pause precedes codec closure;
    // route/gain preparation precedes feed startup. TX uses its own codec/I2S
    // direction, so holding the speaker mutex here would starve inference during
    // prerecorded clips and their DMA retirement waits.
    const int result = esp_codec_dev_read(s_microphone, samples, frame_count * channel_count * sizeof(int16_t));
    if (result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Voice microphone PCM read failed: codec_result=%d", result);
    }
    return static_cast<esp_err_t>(result);
}

// Reconfigure the active base route for the AFE-owned 16 kHz stream.
// Exception: Returns the original initialization, mutex or route failure.
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

#endif

// Suspend or restore codec streams and I2S as one reversible transition.
// standby: Clears unplayed taps while the existing owner quiesces/restores its transport.
// Exception: Returns original codec, transport, or lifecycle rollback errors.
extern "C" esp_err_t LUNASHELL_audio_set_standby(bool standby)
{
    bool expected = false;
    if (!s_standby_transition.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return ESP_ERR_NOT_FINISHED;
    }
    struct TransitionRelease {
        ~TransitionRelease() { s_standby_transition.store(false, std::memory_order_release); }
    } release;
    s_audio_lifecycle_generation.fetch_add(1, std::memory_order_acq_rel);
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

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    const bool voice_route_requested = LUNASHELL_voice_afe_running();
    const bool resume_speaker = voice_route_requested
        ? LUNASHELL_AUDIO_HAS_OUTPUT
        : s_pre_standby_stream_state_valid &&
              s_pre_standby_stream_state.speaker_open;
    const bool resume_microphone = voice_route_requested
        ? LUNASHELL_audio_input_channel_count() > 0
        : s_pre_standby_stream_state_valid &&
              s_pre_standby_stream_state.microphone_open;
#else
    const bool resume_speaker = s_pre_standby_stream_state_valid &&
        s_pre_standby_stream_state.speaker_open;
    const bool resume_microphone = s_pre_standby_stream_state_valid &&
        s_pre_standby_stream_state.microphone_open;
#endif
    error = bsp_audio_set_transport_standby(
        false,
        resume_speaker,
        resume_microphone
    );
    if (error == ESP_OK) {
        s_standby.store(false, std::memory_order_release);
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    if (error == ESP_OK && voice_route_requested) {
        error = prepare_voice_route_locked();
    } else
#endif
    if (error == ESP_OK && s_pre_standby_stream_state_valid) {
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
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
            voice_route_requested ? "voice" : "restored"
#else
            "restored"
#endif
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
        if (!typewriter_event_current(event)) continue;
        esp_err_t error = LUNASHELL_audio_init();
        if (error != ESP_OK) {
            retain_typewriter_failure(error, s_typewriter_cue.sample_rate_hz, 0);
            continue;
        }
        const int64_t now = esp_timer_get_time();
        const uint32_t age = static_cast<uint32_t>(now) - event.requested_at_us;
        if (age > TYPEWRITER_MAX_AGE_US) continue;
        const bool touch = event.kind == TypewriterEventKind::Touch;
        constexpr uint32_t kGlyphFeedbackCue = 0x46544201U;
        constexpr uint32_t kTouchFeedbackCue = 0x46544202U;
        const lunashell_audio_request_t request{
            touch ? s_typewriter_touch_samples.data() : s_typewriter_cue.samples,
            s_typewriter_cue.sample_count, s_typewriter_cue.sample_rate_hz,
            touch ? kTouchFeedbackCue : kGlyphFeedbackCue,
            touch ? LUNASHELL_AUDIO_FEEDBACK_TOUCH : LUNASHELL_AUDIO_FEEDBACK_GLYPH,
            LUNASHELL_CODEX_CUE_NOTICE, 1, 0, LUNASHELL_AUDIO_WAVE_PCM, 0, 1000,
            0, 0, 0, 0, false, event.generation, now - age};
        lunashell_audio_cue_status_t status{};
        error = LUNASHELL_audio_submit(&request, &status);
        if (error != ESP_OK) {
            retain_typewriter_failure(error, s_typewriter_cue.sample_rate_hz, 0);
        } else if (status.state == LUNASHELL_CUE_DISPATCHED) {
            last_play_at_us = observed_us;
            have_played = true;
        }
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
    for (size_t index = 0; index < sample_count; ++index) {
        const int32_t amplified = static_cast<int32_t>(samples[index]) * 8;
        s_typewriter_touch_samples[index] = static_cast<int16_t>(std::clamp<int32_t>(amplified, -32768, 32767));
    }
    const esp_err_t mixer_error = ensure_cue_worker();
    if (mixer_error != ESP_OK) {
        audio_unlock(mutex);
        return mixer_error;
    }
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

namespace {
// kind: Distinguishes scene glyph progress from a physical touch contact edge.
// Exception: Returns admission or retained asynchronous failure; never waits for audio I/O.
esp_err_t notify_short_feedback(TypewriterEventKind kind)
{
    if (!LUNASHELL_audio_user_sounds_enabled()) return ESP_OK;
    const esp_err_t policy_error = cody_output_policy_error();
    if (policy_error != ESP_OK) return policy_error;
    const esp_err_t previous_error = s_typewriter_last_error.exchange(ESP_OK, std::memory_order_acq_rel);
    if (previous_error != ESP_OK) return previous_error;
    const TaskHandle_t worker = s_typewriter_task.load(std::memory_order_acquire);
    const uint32_t requested_at_us = static_cast<uint32_t>(esp_timer_get_time());
    TypewriterEvent event;
    bool admitted = false;
    bool owner_unavailable = false;
    // Publish pending, timestamp and epoch together. No scheduler or codec call
    // belongs in this bounded critical section shared with the consumer claim.
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    if ((kind == TypewriterEventKind::Touch || !s_typewriter_mailbox.suspended) &&
        !s_standby.load(std::memory_order_acquire) && LUNASHELL_audio_user_sounds_enabled()) {
        owner_unavailable = worker == nullptr;
        // A glyph update cannot overwrite a waiting physical contact click.
        const bool touch_waiting = s_typewriter_mailbox.pending &&
            s_typewriter_mailbox.latest.kind == TypewriterEventKind::Touch;
        if (!owner_unavailable && (kind == TypewriterEventKind::Touch || !touch_waiting)) {
            ++s_typewriter_mailbox.sequence;
            const uint32_t generation = kind == TypewriterEventKind::Touch
                ? s_typewriter_mailbox.touch_generation : s_typewriter_mailbox.generation;
            event = {requested_at_us, generation, s_typewriter_mailbox.sequence, kind};
            s_typewriter_mailbox.latest = event;
            s_typewriter_mailbox.pending = true;
            admitted = true;
        }
    }
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
    if (owner_unavailable) {
        ESP_LOGE(TAG, "Short feedback rejected before owner preparation: kind=%u", static_cast<unsigned>(kind));
        return ESP_ERR_INVALID_STATE;
    }
    if (!admitted) {
        return ESP_OK;
    }
    if (xTaskNotify(worker, TYPEWRITER_EVENT, eSetBits) != pdPASS) {
        drop_typewriter_event(event);
        ESP_LOGE(TAG, "Typewriter notification failed (%s)", esp_err_to_name(ESP_FAIL));
        return ESP_FAIL;
    }
    return ESP_OK;
}
} // namespace

// newly_visible_characters: New glyphs from the caller's complete displayed-frame delta.
// Exception: Returns admission or retained asynchronous failure without codec I/O.
extern "C" esp_err_t LUNASHELL_audio_notify_typewriter(uint32_t newly_visible_characters)
{
    if (newly_visible_characters == 0) {
        return ESP_OK;
    }
    return notify_short_feedback(TypewriterEventKind::Glyph);
}

// No parameters: sends one physical contact click independently of scene text visibility.
// Exception: Returns the original admission or retained asynchronous playback error.
extern "C" esp_err_t LUNASHELL_audio_notify_touch()
{
    return notify_short_feedback(TypewriterEventKind::Touch);
}

// suspended: Changes only cue admission; the canonical standby API owns codec quiescence.
// Exception: Returns exact owner-preparation errors; no allocation, blocking lock, or codec I/O occurs.
extern "C" esp_err_t LUNASHELL_audio_set_typewriter_suspended(bool suspended)
{
    portENTER_CRITICAL(&s_typewriter_mailbox_mux);
    s_typewriter_mailbox.suspended = suspended;
    if (s_typewriter_mailbox.latest.kind != TypewriterEventKind::Touch) {
        s_typewriter_mailbox.pending = false;
    }
    ++s_typewriter_mailbox.generation;
    s_feedback_glyph_generation.store(s_typewriter_mailbox.generation, std::memory_order_release);
    portEXIT_CRITICAL(&s_typewriter_mailbox_mux);
    if (s_typewriter_task.load(std::memory_order_acquire) == nullptr) {
        ESP_LOGE(TAG, "Typewriter suspension rejected before owner preparation");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

namespace {
constexpr uint32_t PERSONALITY_STACK_BYTES = 8192;
constexpr int64_t CODEX_ACTIVITY_COOLDOWN_US = 1500000;
constexpr size_t kCueSlots = LUNASHELL::kAudioMixerMaxVoices;
constexpr size_t kCueHistory = 24;
struct CueSlot {
    lunashell_audio_request_t request{};
    lunashell_audio_cue_status_t status{};
    LUNASHELL::AudioMixerVoice voice{};
    uint32_t generation = 0;
    uint32_t lifecycle_generation = 0;
    int64_t requested_at_us = 0;
    int64_t expires_after_us = 0;
    int64_t drain_until_us = 0;
    uint8_t plays_remaining = 0;
    bool used = false;
    bool started = false;
    bool draining = false;
    bool terminal = false;
    bool cancel_requested = false;
};
struct CueOwner {
    std::array<CueSlot, kCueSlots> slots{};
    std::array<lunashell_audio_cue_status_t, kCueHistory> history{};
    size_t history_next = 0;
    uint32_t next_sequence = 0;
    lunashell_personality_audio_policy_t policy{};
    bool policy_configured = false;
    uint32_t generation = 0;
    uint32_t codex_generation = 0;
    bool suspended = false;
    bool codex_suspended = true;
    bool codex_enabled = true;
    bool have_played = false;
    int64_t last_started_us = 0;
    bool codex_have_played = false;
    int64_t codex_last_started_us = 0;
    esp_err_t preparation_error = ESP_OK;
};
CueOwner s_cue_owner;
portMUX_TYPE s_cue_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<TaskHandle_t> s_cue_task{nullptr};
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
// No parameters: observes the single mixer task identity without acquiring its codec lock.
bool on_cue_worker()
{
    const auto task = s_cue_task.load(std::memory_order_acquire);
    return task != nullptr && task == xTaskGetCurrentTaskHandle();
}
#endif
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
StaticTask_t s_cue_task_control;
EXT_RAM_BSS_ATTR StackType_t s_cue_task_stack[PERSONALITY_STACK_BYTES / sizeof(StackType_t)];
std::mutex s_cue_worker_prepare_mutex;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
LUNASHELL::AudioStreamPipe s_audio_stream;
std::atomic<uint32_t> s_audio_stream_generation{0};
std::atomic<uint32_t> s_audio_stream_rate{0};
std::atomic<uint16_t> s_audio_stream_gain{1000};
std::atomic_bool s_audio_stream_reserved{false};

// No parameters: reserve one mixer voice without replacing an in-flight cue.
// Exception: Returns busy when the worker is full or another stream owns the slot.
esp_err_t reserve_audio_stream_slot()
{
    portENTER_CRITICAL(&s_cue_mux);
    size_t occupied = 0;
    for (const auto &slot : s_cue_owner.slots) if (slot.used) ++occupied;
    const bool available = !s_audio_stream_reserved.load(std::memory_order_relaxed) && occupied < kCueSlots;
    if (available) s_audio_stream_reserved.store(true, std::memory_order_release);
    portEXIT_CRITICAL(&s_cue_mux);
    return available ? ESP_OK : ESP_ERR_NOT_FINISHED;
}

// No parameters: release a terminal stream's reservation after worker acknowledgement.
void release_audio_stream_slot()
{
    s_audio_stream_reserved.store(false, std::memory_order_release);
}
#endif
#endif

// No parameters: invalidate admitted cues before codec ownership changes.
void invalidate_personality_events()
{
    portENTER_CRITICAL(&s_cue_mux);
    ++s_cue_owner.generation;
    ++s_cue_owner.codex_generation;
    portEXIT_CRITICAL(&s_cue_mux);
}

// slot: Active request whose source can no longer be read after this call.
// state: Terminal disposition retained in bounded history.
// error: Original playback or policy failure.
void finish_cue_locked(CueSlot &slot, lunashell_audio_cue_state_t state, esp_err_t error)
{
    slot.status.state = state;
    slot.status.last_error = error;
    if (slot.request.channel == LUNASHELL_AUDIO_FEEDBACK_GLYPH ||
        slot.request.channel == LUNASHELL_AUDIO_FEEDBACK_TOUCH) {
        if (state == LUNASHELL_CUE_FAILED) s_typewriter_last_error.store(error, std::memory_order_release);
    }
    if (slot.request.pin_terminal) {
        slot.terminal = true;
        return;
    }
    s_cue_owner.history[s_cue_owner.history_next] = slot.status;
    s_cue_owner.history_next = (s_cue_owner.history_next + 1U) % kCueHistory;
    slot = {};
}

// slot: Owned request, checked with s_cue_mux held.
bool cue_current_locked(const CueSlot &slot)
{
    if (slot.cancel_requested || slot.lifecycle_generation != s_audio_lifecycle_generation.load(std::memory_order_acquire) ||
        s_standby.load(std::memory_order_acquire) ||
        s_standby_transition.load(std::memory_order_acquire)) return false;
    if (slot.request.channel == LUNASHELL_AUDIO_SYSTEM) return true;
    if (slot.request.channel == LUNASHELL_AUDIO_USER_PCM) {
        const auto revision = slot.request.user_sounds_revision;
        return (revision & 1U) != 0U && s_user_sounds_state.load(std::memory_order_acquire) == revision;
    }
    if (slot.request.channel == LUNASHELL_AUDIO_FEEDBACK_GLYPH ||
        slot.request.channel == LUNASHELL_AUDIO_FEEDBACK_TOUCH) {
        const auto generation = slot.request.channel == LUNASHELL_AUDIO_FEEDBACK_TOUCH
            ? s_feedback_touch_generation.load(std::memory_order_acquire)
            : s_feedback_glyph_generation.load(std::memory_order_acquire);
        return generation == slot.request.feedback_generation && LUNASHELL_audio_user_sounds_enabled();
    }
    const bool codex = slot.request.channel == LUNASHELL_AUDIO_CODEX;
    const uint32_t generation = codex ? s_cue_owner.codex_generation : s_cue_owner.generation;
    const bool suspended = codex ? s_cue_owner.codex_suspended : s_cue_owner.suspended;
    return slot.generation == generation && s_cue_owner.policy.enabled && !suspended &&
        (!codex || s_cue_owner.codex_enabled) && LUNASHELL_audio_user_sounds_enabled();
}

#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
// slot: Validated request copied into the output owner's mixer source.
// rate: Active duplex transport clock.
// delay: Silence before this repetition, expressed in transport frames.
// Exception: Returns original mixer initialization error.
esp_err_t start_cue_voice(CueSlot &slot, uint32_t rate, uint32_t delay)
{
    LUNASHELL::AudioVoiceSource source{};
    source.wave = static_cast<LUNASHELL::AudioMixerWave>(slot.request.wave);
    source.pcm = slot.request.samples;
    source.pcm_frames = static_cast<uint32_t>(slot.request.sample_count);
    source.duration_frames = static_cast<uint32_t>(slot.request.sample_count);
    source.source_rate_hz = slot.request.sample_rate_hz;
    source.frequency_hz = slot.request.frequency_hz;
    source.delay_frames = delay;
    source.attack_frames = slot.request.attack_frames;
    source.release_frames = slot.request.release_frames;
    source.noise_seed = slot.request.noise_seed;
    source.gain = slot.request.gain;
    slot.voice = {};
    return LUNASHELL::initialize_audio_voice(slot.voice, source, rate);
}

// error: Original audio transport failure; every occupied slot becomes terminal.
void fail_all_cues(esp_err_t error)
{
    portENTER_CRITICAL(&s_cue_mux);
    for (auto &slot : s_cue_owner.slots) {
        if (slot.used && !slot.terminal) finish_cue_locked(slot, LUNASHELL_CUE_FAILED, error);
    }
    portEXIT_CRITICAL(&s_cue_mux);
}

// rate: Active transport clock used for the conservative I2S tail bound.
// Returns a delay after the final submitted frame before completion is reported.
int64_t cue_drain_us(uint32_t rate)
{
    const i2s_chan_config_t dma = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    const uint64_t frames = static_cast<uint64_t>(dma.dma_desc_num + 1U) * dma.dma_frame_num * 2U;
    return static_cast<int64_t>((frames * 1000000U + rate - 1U) / rate);
}

// rate: Current shared transport rate.
// volume: Codec volume used for the canonical playback observation.
// Exception: Returns the original codec, AEC or snapshot failure.
esp_err_t retire_mixer_tail(uint32_t rate, int volume)
{
    const i2s_chan_config_t dma = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    size_t remaining = static_cast<size_t>(dma.dma_desc_num + 1U) * dma.dma_frame_num * 2U;
    const int16_t silence[240]{};
    while (remaining != 0) {
        const size_t frames = std::min<size_t>(remaining, 240);
        const auto error = write_speaker_pcm_locked(silence, frames, rate, volume);
        if (error != ESP_OK) return error;
        remaining -= frames;
    }
    return ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
struct StreamWorkerState {
    LUNASHELL::AudioStreamBlock block{};
    uint32_t generation = 0;
    size_t offset = 0;
    int64_t drain_until_us = 0;
    esp_err_t failure = ESP_OK;
    bool held = false;
    bool cancelled = false;
    bool mixed_any = false;
};

// state: Worker-owned stream block and terminal retirement deadline.
// now: Current monotonic time used to bound DMA retirement.
// ready: Receives whether a copied block can contribute output immediately.
// Exception: Returns NOT_FINISHED for transient pipe contention or the original pipe error.
esp_err_t service_audio_stream(StreamWorkerState &state, int64_t now, bool &ready)
{
    ready = false;
    const uint32_t generation = s_audio_stream_generation.load(std::memory_order_acquire);
    if (generation == 0) return ESP_OK;
    if (state.generation != generation) {
        if (state.held) return ESP_ERR_INVALID_STATE;
        state = {};
        state.generation = generation;
    }
    if (state.failure != ESP_OK) {
        const auto cancel = LUNASHELL::cancel_audio_stream(s_audio_stream, generation, state.failure);
        if (cancel == ESP_ERR_NOT_FINISHED) return cancel;
        if (cancel != ESP_OK && cancel != ESP_ERR_INVALID_STATE) return cancel;
    }
    LUNASHELL::AudioStreamStatus observed{};
    auto error = LUNASHELL::observe_audio_stream(s_audio_stream, generation, &observed);
    if (error != ESP_OK) return error;
    if (!observed.active) return ESP_OK;
    state.cancelled = observed.cancelled;
    if (state.held && (state.offset == state.block.frames || observed.cancelled)) {
        error = LUNASHELL::complete_audio_stream_block(s_audio_stream, generation, ESP_OK);
        if (error == ESP_ERR_NOT_FINISHED) return error;
        if (error != ESP_OK && (!observed.cancelled || error != observed.error)) return error;
        state.held = false;
        state.offset = 0;
        observed.in_flight = false;
    }
    if (!state.held && !observed.cancelled && observed.queued_blocks != 0) {
        error = LUNASHELL::dequeue_audio_stream(s_audio_stream, generation, state.block);
        if (error != ESP_OK) return error;
        state.held = true;
        state.offset = 0;
        --observed.queued_blocks;
        observed.in_flight = true;
    }
    if (!state.held && !observed.in_flight && observed.queued_blocks == 0 &&
        (observed.input_ended || observed.cancelled) && now >= state.drain_until_us) {
        if (observed.input_ended && !observed.cancelled && !state.mixed_any) {
            state.failure = ESP_ERR_INVALID_RESPONSE;
            return state.failure;
        }
        return LUNASHELL::acknowledge_audio_stream_terminal(s_audio_stream, generation, state.failure);
    }
    ready = state.held && state.offset < state.block.frames;
    return ESP_OK;
}
#endif

// context: Unused; this worker alone renders and writes the shared asynchronous output.
void cue_worker(void *context)
{
    (void)context;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
    StreamWorkerState stream{};
#endif
    for (;;) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
        const TickType_t wait = s_audio_stream_generation.load(std::memory_order_acquire) != 0
            ? pdMS_TO_TICKS(10) + 1 : portMAX_DELAY;
        ulTaskNotifyTake(pdTRUE, wait);
#else
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#endif
        for (;;) {
            bool any = false;
            const int64_t check_time = esp_timer_get_time();
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
            bool stream_ready = false;
            if ((s_standby_transition.load(std::memory_order_acquire) ||
                    s_standby.load(std::memory_order_acquire)) &&
                s_audio_stream_generation.load(std::memory_order_acquire) != 0 && stream.failure == ESP_OK)
                stream.failure = ESP_ERR_INVALID_STATE;
            const auto stream_service_error = service_audio_stream(stream, check_time, stream_ready);
            if (stream_service_error == ESP_ERR_NOT_FINISHED) { vTaskDelay(1); continue; }
            if (stream_service_error != ESP_OK) {
                ESP_LOGE(TAG, "Audio stream service failed (%s)", esp_err_to_name(stream_service_error));
                stream.failure = stream_service_error;
                vTaskDelay(1);
                continue;
            }
            any = stream_ready;
#endif
            portENTER_CRITICAL(&s_cue_mux);
            for (auto &slot : s_cue_owner.slots) {
                if (!slot.used || slot.terminal) continue;
                if (!cue_current_locked(slot)) finish_cue_locked(slot, LUNASHELL_CUE_SUPPRESSED, ESP_OK);
                else if (!slot.started && check_time - slot.requested_at_us > slot.expires_after_us)
                    finish_cue_locked(slot, LUNASHELL_CUE_EXPIRED, ESP_OK);
                else any = true;
            }
            portEXIT_CRITICAL(&s_cue_mux);
            if (!any) break;
            SemaphoreHandle_t mutex = audio_mutex();
            if (mutex == nullptr) {
                fail_all_cues(ESP_ERR_NO_MEM);
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                if (s_audio_stream_generation.load(std::memory_order_acquire) != 0)
                    stream.failure = ESP_ERR_NO_MEM;
#endif
                break;
            }
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) continue;
            esp_err_t error = cody_output_policy_error();
            if (error == ESP_OK && (!s_initialized.load(std::memory_order_acquire) ||
                s_standby.load(std::memory_order_acquire) || s_audio_state_poisoned.load(std::memory_order_acquire)))
                error = ESP_ERR_INVALID_STATE;
            uint32_t rate = s_microphone_open ? s_microphone_sample_rate_hz : s_speaker_sample_rate_hz;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
            if (stream_ready) {
                const uint32_t stream_rate = s_audio_stream_rate.load(std::memory_order_acquire);
                if (s_microphone_open && rate != stream_rate) error = ESP_ERR_INVALID_STATE;
                else rate = stream_rate;
            }
#endif
            if (error == ESP_OK && (rate < 8000 || rate > 48000)) error = ESP_ERR_NOT_SUPPORTED;
            if (error == ESP_OK) error = configure_speaker_stream(rate);
            int volume = 0;
            bool previous_mute = true;
            if (error == ESP_OK) error = static_cast<esp_err_t>(esp_codec_dev_get_out_vol(s_speaker, &volume));
            bool mute_policy_read = false;
            if (error == ESP_OK) {
                error = static_cast<esp_err_t>(esp_codec_dev_get_out_mute(s_speaker, &previous_mute));
                mute_policy_read = error == ESP_OK;
            }
            if (error == ESP_OK && (volume < 0 || volume > 100)) error = ESP_ERR_INVALID_RESPONSE;
            if (error == ESP_OK && volume > 0) error = static_cast<esp_err_t>(esp_codec_dev_set_out_mute(s_speaker, false));
            bool wrote = false;
            bool cancelled_cue = false;
            while (error == ESP_OK) {
                bool has_cue = false;
                portENTER_CRITICAL(&s_cue_mux);
                for (const auto &slot : s_cue_owner.slots) if (slot.used && !slot.terminal &&
                    slot.request.channel != LUNASHELL_AUDIO_SYSTEM &&
                    slot.request.channel != LUNASHELL_AUDIO_USER_PCM) { has_cue = true; break; }
                portEXIT_CRITICAL(&s_cue_mux);
                bool allowed = true;
                esp_err_t cue_clock_error = ESP_OK;
                if (has_cue) {
                    LUNASHELL::ClockSnapshot clock{};
                    cue_clock_error = LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
                    if (cue_clock_error == ESP_OK) {
                        allowed = LUNASHELL::clock_cue_policy(clock) == LUNASHELL::ClockCuePolicy::Allowed;
                    }
                }
                const int64_t now = esp_timer_get_time();
                bool active = false;
                bool remaining = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                // Pipe operations use a zero-time lock. Never wait for a producer while
                // holding the codec: retry after releasing it on lock contention/underrun.
                if ((s_standby_transition.load(std::memory_order_acquire) ||
                        s_standby.load(std::memory_order_acquire)) &&
                    s_audio_stream_generation.load(std::memory_order_acquire) != 0 && stream.failure == ESP_OK) {
                    stream.failure = ESP_ERR_INVALID_STATE;
                    break;
                }
                stream_ready = false;
                const auto stream_error = service_audio_stream(stream, now, stream_ready);
                if (stream_error != ESP_OK && stream_error != ESP_ERR_NOT_FINISHED) stream.failure = stream_error;
                if (stream_ready && s_audio_stream_rate.load(std::memory_order_acquire) != rate) {
                    stream.failure = ESP_ERR_INVALID_STATE;
                    stream_ready = false;
                }
                if (stream_ready && volume == 0) {
                    stream.failure = ESP_ERR_INVALID_STATE;
                    stream_ready = false;
                }
                active = stream_ready;
                remaining = stream_ready;
#endif
                std::array<CueSlot, kCueSlots> current{};
                portENTER_CRITICAL(&s_cue_mux);
                for (size_t index = 0; index < kCueSlots; ++index) {
                    auto &slot = s_cue_owner.slots[index];
                    // A request admitted after an empty observation waits for its own clock check.
                    if (!slot.used || slot.terminal) continue;
                    const bool application = slot.request.channel != LUNASHELL_AUDIO_SYSTEM &&
                        slot.request.channel != LUNASHELL_AUDIO_USER_PCM;
                    if (application && !has_cue) continue;
                    if (application && cue_clock_error != ESP_OK) {
                        cancelled_cue = cancelled_cue || slot.started;
                        finish_cue_locked(slot, LUNASHELL_CUE_FAILED, cue_clock_error);
                        continue;
                    }
                    if (!cue_current_locked(slot) || (application && !allowed) || volume == 0) {
                        cancelled_cue = cancelled_cue || slot.started;
                        finish_cue_locked(slot, LUNASHELL_CUE_SUPPRESSED, ESP_OK);
                        continue;
                    }
                    if (!slot.started && now - slot.requested_at_us > slot.expires_after_us) {
                        finish_cue_locked(slot, LUNASHELL_CUE_EXPIRED, ESP_OK);
                        continue;
                    }
                    if (slot.draining && now >= slot.drain_until_us) {
                        finish_cue_locked(slot, LUNASHELL_CUE_COMPLETED, ESP_OK);
                        continue;
                    }
                    remaining = true;
                    current[index] = slot;
                }
                portEXIT_CRITICAL(&s_cue_mux);
                if (!remaining) break;
                // Each snapshot keeps its PCM lease while rendering. Only this worker retires slots.
                for (auto &slot : current) {
                    if (!slot.used || slot.draining) continue;
                    if (!slot.started) {
                        error = start_cue_voice(slot, rate, 0);
                        if (error != ESP_OK) break;
                        slot.started = true;
                        slot.status.state = LUNASHELL_CUE_PLAYING;
                    }
                    active = true;
                }
                if (error != ESP_OK) break;
                if (!active) { vTaskDelay(pdMS_TO_TICKS(10) + 1); continue; }
                LUNASHELL::AudioMixerVoice voices[kCueSlots]{};
                size_t voice_slots[kCueSlots]{};
                size_t voice_count = 0;
                size_t frames = std::min<size_t>(320, rate / 50U);
                for (size_t index = 0; index < kCueSlots; ++index) {
                    const auto &slot = current[index];
                    if (!slot.used || slot.draining || !slot.started) continue;
                    voices[voice_count] = slot.voice;
                    voice_slots[voice_count++] = index;
                    const uint64_t duration = static_cast<uint64_t>(slot.voice.source.duration_frames) * rate;
                    const uint64_t source_remaining = duration - slot.voice.position;
                    const uint64_t remaining_frames = (source_remaining + slot.voice.source.source_rate_hz - 1U) /
                        slot.voice.source.source_rate_hz + slot.voice.delay_remaining;
                    frames = std::min(frames, static_cast<size_t>(remaining_frames));
                }
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                const bool mixed_stream = stream_ready;
                if (mixed_stream) {
                    if (voice_count == kCueSlots) { stream.failure = ESP_ERR_INVALID_STATE; break; }
                    LUNASHELL::AudioVoiceSource source{};
                    source.pcm = stream.block.pcm.data() + stream.offset;
                    source.pcm_frames = static_cast<uint32_t>(stream.block.frames - stream.offset);
                    source.duration_frames = source.pcm_frames;
                    source.source_rate_hz = rate;
                    source.gain = s_audio_stream_gain.load(std::memory_order_acquire);
                    error = LUNASHELL::initialize_audio_voice(voices[voice_count], source, rate);
                    if (error != ESP_OK) { stream.failure = error; break; }
                    frames = std::min(frames, static_cast<size_t>(source.duration_frames));
                    ++voice_count;
                }
#endif
                int16_t mixed[320]{};
                const size_t cue_voice_count = voice_count
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                    - (mixed_stream ? 1U : 0U)
#endif
                    ;
                uint32_t playback_cue = 0;
                if (voice_count == 1 && cue_voice_count == 1) playback_cue = current[voice_slots[0]].request.cue_id;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                else if (voice_count == 1 && mixed_stream) playback_cue = 0x54545301U;
#endif
                error = LUNASHELL::render_audio_mix(voices, voice_count, rate, mixed, frames);
                if (error == ESP_OK) error = write_speaker_pcm_locked(mixed, frames, rate, volume, playback_cue);
                if (error != ESP_OK) break;
                wrote = true;
                const int64_t submitted_at = esp_timer_get_time();
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                if (mixed_stream) {
                    stream.mixed_any = true;
                    stream.offset += frames;
                    stream.drain_until_us = submitted_at + cue_drain_us(rate);
                }
#endif
                for (size_t voice_index = 0; voice_index < cue_voice_count; ++voice_index) {
                    auto &slot = current[voice_slots[voice_index]];
                    slot.voice = voices[voice_index];
                    if (!slot.voice.active) {
                        if (--slot.plays_remaining != 0) {
                            const uint32_t delay = static_cast<uint32_t>(static_cast<uint64_t>(rate) * slot.request.gap_ms / 1000U);
                            error = start_cue_voice(slot, rate, delay);
                            if (error != ESP_OK) break;
                        } else {
                            slot.draining = true;
                            slot.drain_until_us = submitted_at + cue_drain_us(rate);
                        }
                    }
                }
                if (error != ESP_OK) break;
                portENTER_CRITICAL(&s_cue_mux);
                for (size_t index = 0; index < kCueSlots; ++index) {
                    const auto &snapshot = current[index];
                    auto &slot = s_cue_owner.slots[index];
                    if (!snapshot.used || !slot.used ||
                        snapshot.status.request_sequence != slot.status.request_sequence) continue;
                    if (!slot.started && snapshot.started && slot.request.channel != LUNASHELL_AUDIO_SYSTEM &&
                        slot.request.channel != LUNASHELL_AUDIO_USER_PCM &&
                        slot.request.channel != LUNASHELL_AUDIO_FEEDBACK_GLYPH &&
                        slot.request.channel != LUNASHELL_AUDIO_FEEDBACK_TOUCH) {
                        if (slot.request.channel == LUNASHELL_AUDIO_CODEX) {
                            s_cue_owner.codex_have_played = true;
                            s_cue_owner.codex_last_started_us = now;
                        } else {
                            s_cue_owner.have_played = true;
                            s_cue_owner.last_started_us = now;
                        }
                    }
                    const bool cancel_requested = slot.cancel_requested;
                    slot = snapshot;
                    slot.cancel_requested = slot.cancel_requested || cancel_requested;
                }
                portEXIT_CRITICAL(&s_cue_mux);
            }
            bool flush_tail = cancelled_cue;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
            flush_tail = flush_tail || stream.drain_until_us > esp_timer_get_time();
#endif
            if (wrote && flush_tail && error == ESP_OK) {
                bool silence_output = cancelled_cue;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                silence_output = silence_output || stream.cancelled || stream.failure != ESP_OK;
#endif
                bool other_source = false;
                portENTER_CRITICAL(&s_cue_mux);
                for (const auto &slot : s_cue_owner.slots) {
                    if (slot.used && !slot.terminal && cue_current_locked(slot)) { other_source = true; break; }
                }
                portEXIT_CRITICAL(&s_cue_mux);
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                other_source = other_source || (s_audio_stream_generation.load(std::memory_order_acquire) != 0 &&
                    !stream.cancelled && stream.failure == ESP_OK);
#endif
                silence_output = silence_output && !other_source;
                if (silence_output) error = static_cast<esp_err_t>(esp_codec_dev_set_out_mute(s_speaker, true));
                if (error == ESP_OK) error = retire_mixer_tail(rate, volume);
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                if (error == ESP_OK) stream.drain_until_us = esp_timer_get_time();
#endif
            }
            if (mute_policy_read) {
                const int mute_error = esp_codec_dev_set_out_mute(s_speaker, previous_mute);
                if (mute_error != ESP_CODEC_DEV_OK) {
                    s_audio_state_poisoned.store(true, std::memory_order_release);
                    ESP_LOGE(TAG, "Restoring mixed audio mute failed (%d)", mute_error);
                    const int force_mute = esp_codec_dev_set_out_mute(s_speaker, true);
                    if (force_mute != ESP_CODEC_DEV_OK) ESP_LOGE(TAG, "Mixed audio failure mute failed (%d)", force_mute);
                    if (error == ESP_OK) error = static_cast<esp_err_t>(mute_error);
                }
            }
            if (wrote) {
                const auto publication = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
                    false, 0, rate, volume, error);
                if (error == ESP_OK) error = publication;
            }
            audio_unlock(mutex);
            if (error != ESP_OK) {
                ESP_LOGE(TAG, "Audio mixer worker failed (%s)", esp_err_to_name(error));
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
                if (s_audio_stream_generation.load(std::memory_order_acquire) != 0 && stream.failure == ESP_OK)
                    stream.failure = error;
#endif
                fail_all_cues(error);
                break;
            }
        }
    }
}

// No parameters: create the single mixer worker independently of any cue policy.
// Exception: Returns the original stream initialization or worker allocation error.
esp_err_t ensure_cue_worker()
{
    std::lock_guard<std::mutex> guard(s_cue_worker_prepare_mutex);
    if (s_cue_task.load(std::memory_order_acquire) != nullptr) return ESP_OK;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
    if (s_audio_stream.lock == nullptr) {
        const auto error = LUNASHELL::initialize_audio_stream(s_audio_stream);
        if (error != ESP_OK) return error;
    }
#endif
    const TaskHandle_t task = xTaskCreateStatic(cue_worker, "audio_mixer", PERSONALITY_STACK_BYTES,
        nullptr, 2, s_cue_task_stack, &s_cue_task_control);
    if (task == nullptr) return ESP_ERR_NO_MEM;
    s_cue_task.store(task, std::memory_order_release);
    return ESP_OK;
}

// No parameters: notify the one output owner after PCM, EOF or cancellation.
// Exception: Returns invalid state when no output worker exists.
esp_err_t wake_cue_worker()
{
    const TaskHandle_t task = s_cue_task.load(std::memory_order_acquire);
    if (task == nullptr) return ESP_ERR_INVALID_STATE;
    xTaskNotifyGive(task);
    return ESP_OK;
}
#endif
} // namespace

// policy: Immutable owner admission policy.
// Exception: Returns invalid input, changed policy or worker creation error.
extern "C" esp_err_t LUNASHELL_audio_prepare_personality(const lunashell_personality_audio_policy_t *policy)
{
    if (policy == nullptr || policy->cooldown_us < 0 || policy->request_expiry_us <= 0) return ESP_ERR_INVALID_ARG;
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    portENTER_CRITICAL(&s_cue_mux);
    const bool configured = s_cue_owner.policy_configured;
    const bool same = !configured || (s_cue_owner.policy.enabled == policy->enabled &&
        s_cue_owner.policy.cooldown_us == policy->cooldown_us &&
        s_cue_owner.policy.request_expiry_us == policy->request_expiry_us);
    if (!configured) { s_cue_owner.policy = *policy; s_cue_owner.policy_configured = true; }
    const esp_err_t previous = s_cue_owner.preparation_error;
    portEXIT_CRITICAL(&s_cue_mux);
    if (!same) return ESP_ERR_INVALID_STATE;
    if (previous != ESP_OK) return previous;
    const auto worker_error = ensure_cue_worker();
    if (worker_error != ESP_OK) {
        portENTER_CRITICAL(&s_cue_mux);
        s_cue_owner.preparation_error = worker_error;
        portEXIT_CRITICAL(&s_cue_mux);
        return worker_error;
    }
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// request: Validated PCM or synthesized voice, borrowed until terminal status.
// status: Receives admission result and unique request identity.
// Exception: Returns the original input, policy or owner error.
extern "C" esp_err_t LUNASHELL_audio_submit(const lunashell_audio_request_t *request,
    lunashell_audio_cue_status_t *status)
{
    if (status == nullptr || request == nullptr) return ESP_ERR_INVALID_ARG;
    *status = {LUNASHELL_CUE_FAILED, ESP_ERR_INVALID_ARG, 0, request->cue_id};
    const bool codex = request->channel == LUNASHELL_AUDIO_CODEX;
    const bool boot = request->channel == LUNASHELL_AUDIO_BOOT;
    const bool pet = request->channel == LUNASHELL_AUDIO_PET;
    const bool system = request->channel == LUNASHELL_AUDIO_SYSTEM;
    const bool user_pcm = request->channel == LUNASHELL_AUDIO_USER_PCM;
    const bool explicit_pcm = system || user_pcm;
    const bool feedback = request->channel == LUNASHELL_AUDIO_FEEDBACK_GLYPH ||
        request->channel == LUNASHELL_AUDIO_FEEDBACK_TOUCH;
    const bool channel_valid = request->channel >= LUNASHELL_AUDIO_PERSONALITY && request->channel <= LUNASHELL_AUDIO_FEEDBACK_TOUCH;
    const bool kind_valid = request->kind == LUNASHELL_CODEX_CUE_ACTIVITY || request->kind == LUNASHELL_CODEX_CUE_NOTICE;
    const bool wave_valid = request->wave >= LUNASHELL_AUDIO_WAVE_PCM && request->wave <= LUNASHELL_AUDIO_WAVE_NOISE;
    const bool long_clip = boot || pet || (codex && request->kind == LUNASHELL_CODEX_CUE_NOTICE &&
        request->repetitions == 1);
    // Boot presentation can own one five-second first-run melody; other cues keep their bounds.
    const uint32_t max_clip_ms = boot ? 5000U : long_clip ? 2000U : 350U;
    const uint32_t max_sequence_ms = boot ? 5000U : 2500U;
    const uint64_t clip_ms_units = static_cast<uint64_t>(request->sample_count) * 1000U;
    const uint64_t total_ms_units = clip_ms_units * request->repetitions +
        static_cast<uint64_t>(request->gap_ms) * (request->repetitions > 0 ? request->repetitions - 1U : 0U) *
            request->sample_rate_hz;
    const bool sequence_valid = request->sample_rate_hz >= 8000 && request->sample_rate_hz <= 48000 &&
        request->repetitions >= 1 && request->repetitions <= 5 && request->gap_ms <= 200 &&
        (request->repetitions != 1 || request->gap_ms == 0) &&
        (explicit_pcm || (clip_ms_units <= static_cast<uint64_t>(max_clip_ms) * request->sample_rate_hz &&
            total_ms_units <= static_cast<uint64_t>(max_sequence_ms) * request->sample_rate_hz));
    if (!channel_valid || !kind_valid || !wave_valid || request->cue_id == 0 || request->sample_count == 0 ||
        request->sample_count > UINT32_MAX || request->gain > 1000 || request->attack_frames > request->sample_count ||
        request->release_frames > request->sample_count ||
        (request->wave == LUNASHELL_AUDIO_WAVE_PCM && request->samples == nullptr) ||
        (explicit_pcm && (request->wave != LUNASHELL_AUDIO_WAVE_PCM || request->repetitions != 1)) ||
        (feedback && (request->wave != LUNASHELL_AUDIO_WAVE_PCM || request->sample_rate_hz != 16000 ||
            request->sample_count < 240 || request->sample_count > 400 || request->requested_at_us <= 0)) ||
        (user_pcm && (request->user_sounds_revision & 1U) == 0U) ||
        (request->wave != LUNASHELL_AUDIO_WAVE_PCM && request->wave != LUNASHELL_AUDIO_WAVE_NOISE &&
            (request->frequency_hz == 0 || request->frequency_hz > 4000)) ||
        (codex && (request->cue_id & 0xFFFF0000U) != 0x43430000U) || !sequence_valid ||
        (request->repetitions != 1 && (!codex || request->kind != LUNASHELL_CODEX_CUE_NOTICE))) {
        ESP_LOGE(TAG, "Audio cue format rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (!system && (!LUNASHELL_audio_user_sounds_enabled() || (user_pcm &&
        s_user_sounds_state.load(std::memory_order_acquire) != request->user_sounds_revision))) {
        *status = {LUNASHELL_CUE_SUPPRESSED, ESP_OK, 0, request->cue_id}; return ESP_OK;
    }
    const esp_err_t policy_error = cody_output_policy_error();
    if (policy_error != ESP_OK) { status->last_error = policy_error; return policy_error; }
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (explicit_pcm) {
        const esp_err_t worker_error = ensure_cue_worker();
        if (worker_error != ESP_OK) { status->last_error = worker_error; return worker_error; }
    }
#endif
    const TaskHandle_t task = s_cue_task.load(std::memory_order_acquire);
    if (task == nullptr) { status->last_error = ESP_ERR_INVALID_STATE; return ESP_ERR_INVALID_STATE; }
    const int64_t now = esp_timer_get_time();
    if (feedback && (request->requested_at_us > now || now - request->requested_at_us > TYPEWRITER_MAX_AGE_US)) {
        *status = {LUNASHELL_CUE_EXPIRED, ESP_OK, 0, request->cue_id};
        return ESP_OK;
    }
    bool admitted = false;
    portENTER_CRITICAL(&s_cue_mux);
    const bool enabled = explicit_pcm || feedback || (s_cue_owner.policy.enabled && (!codex || s_cue_owner.codex_enabled));
    const bool suspended = (explicit_pcm || feedback) ? false : codex ? s_cue_owner.codex_suspended : s_cue_owner.suspended;
    const int64_t cooldown = (pet || explicit_pcm || feedback) ? 0 : codex ?
        (request->kind == LUNASHELL_CODEX_CUE_ACTIVITY ? CODEX_ACTIVITY_COOLDOWN_US : 0) : s_cue_owner.policy.cooldown_us;
    const bool played = codex ? s_cue_owner.codex_have_played : s_cue_owner.have_played;
    const int64_t started = codex ? s_cue_owner.codex_last_started_us : s_cue_owner.last_started_us;
    CueSlot *free_slot = nullptr;
    bool pending_same = false;
    size_t occupied = 0;
    for (auto &slot : s_cue_owner.slots) {
        if (!slot.used) { if (free_slot == nullptr) free_slot = &slot; continue; }
        ++occupied;
        const bool slot_codex = slot.request.channel == LUNASHELL_AUDIO_CODEX;
        if (!slot.started && !slot.terminal && cooldown > 0 && slot_codex == codex &&
            slot.request.channel != LUNASHELL_AUDIO_SYSTEM && slot.request.channel != LUNASHELL_AUDIO_USER_PCM &&
            slot.request.channel != LUNASHELL_AUDIO_FEEDBACK_GLYPH && slot.request.channel != LUNASHELL_AUDIO_FEEDBACK_TOUCH)
            pending_same = true;
    }
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311 && CONFIG_LUNASHELL_BUILD_ENABLE_TTS
    if (s_audio_stream_reserved.load(std::memory_order_acquire) && occupied >= kCueSlots - 1U) free_slot = nullptr;
#endif
    if (!enabled || suspended || s_standby.load(std::memory_order_acquire) ||
        s_standby_transition.load(std::memory_order_acquire) || (!system &&
            (!LUNASHELL_audio_user_sounds_enabled() || (user_pcm &&
                s_user_sounds_state.load(std::memory_order_acquire) != request->user_sounds_revision))))
        *status = {LUNASHELL_CUE_SUPPRESSED, ESP_OK, 0, request->cue_id};
    else if (free_slot == nullptr) *status = {LUNASHELL_CUE_DROPPED_BUSY, ESP_OK, 0, request->cue_id};
    else if (pending_same || (played && now - started < cooldown)) *status = {LUNASHELL_CUE_DROPPED_COOLDOWN, ESP_OK, 0, request->cue_id};
    else if (s_cue_owner.next_sequence == UINT32_MAX) status->last_error = ESP_ERR_INVALID_STATE;
    else {
        ++s_cue_owner.next_sequence;
        free_slot->request = *request;
        free_slot->status = {LUNASHELL_CUE_DISPATCHED, ESP_OK, s_cue_owner.next_sequence, request->cue_id};
        free_slot->generation = codex ? s_cue_owner.codex_generation : s_cue_owner.generation;
        free_slot->lifecycle_generation = s_audio_lifecycle_generation.load(std::memory_order_acquire);
        free_slot->requested_at_us = feedback ? request->requested_at_us : now;
        free_slot->expires_after_us = feedback ? TYPEWRITER_MAX_AGE_US :
            explicit_pcm ? INT64_MAX : s_cue_owner.policy.request_expiry_us;
        free_slot->plays_remaining = request->repetitions;
        free_slot->used = true;
        *status = free_slot->status;
        admitted = true;
    }
    portEXIT_CRITICAL(&s_cue_mux);
    // eIncrement notifications to a valid task are unconditional; the worker
    // alone retires slots so a producer cannot release borrowed PCM mid-render.
    if (admitted) xTaskNotifyGive(task);
    return status->last_error;
}

// request_sequence: Unique admitted request sequence.
// cue_id: Exact cue identity submitted with it.
// status: Receives active or bounded-history result.
// Exception: Returns invalid input or ESP_ERR_NOT_FOUND when no record exists.
extern "C" esp_err_t LUNASHELL_audio_get_cue_status(uint32_t request_sequence, uint32_t cue_id,
    lunashell_audio_cue_status_t *status)
{
    if (request_sequence == 0 || cue_id == 0 || status == nullptr) return ESP_ERR_INVALID_ARG;
    bool found = false;
    portENTER_CRITICAL(&s_cue_mux);
    for (const auto &slot : s_cue_owner.slots) if (slot.used && slot.status.request_sequence == request_sequence &&
        slot.status.cue_id == cue_id) { *status = slot.status; found = true; break; }
    if (!found) for (const auto &entry : s_cue_owner.history) if (entry.request_sequence == request_sequence &&
        entry.cue_id == cue_id) { *status = entry; found = true; break; }
    portEXIT_CRITICAL(&s_cue_mux);
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// request_sequence: Exact admitted request identity.
// cue_id: Stable identity associated with that request.
// Exception: Returns not-found for an absent request; terminal observation still owns source release.
extern "C" esp_err_t LUNASHELL_audio_cancel_cue(uint32_t request_sequence, uint32_t cue_id)
{
    if (request_sequence == 0 || cue_id == 0) return ESP_ERR_INVALID_ARG;
    bool found = false;
    portENTER_CRITICAL(&s_cue_mux);
    for (auto &slot : s_cue_owner.slots) {
        if (!slot.used || slot.status.request_sequence != request_sequence || slot.status.cue_id != cue_id) continue;
        if (!slot.terminal) slot.cancel_requested = true;
        found = true;
        break;
    }
    if (!found) for (const auto &entry : s_cue_owner.history) {
        if (entry.request_sequence == request_sequence && entry.cue_id == cue_id) { found = true; break; }
    }
    portEXIT_CRITICAL(&s_cue_mux);
    if (!found) return ESP_ERR_NOT_FOUND;
    const TaskHandle_t task = s_cue_task.load(std::memory_order_acquire);
    if (task != nullptr) xTaskNotifyGive(task);
    return ESP_OK;
}

// request_sequence: Exact pinned request identity after terminal observation.
// cue_id: Stable identity associated with that request.
// Exception: Returns invalid state while the worker owns a slot or not-found for an absent identity.
extern "C" esp_err_t LUNASHELL_audio_ack_cue(uint32_t request_sequence, uint32_t cue_id)
{
    if (request_sequence == 0 || cue_id == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t result = ESP_ERR_NOT_FOUND;
    portENTER_CRITICAL(&s_cue_mux);
    for (auto &slot : s_cue_owner.slots) {
        if (!slot.used || slot.status.request_sequence != request_sequence || slot.status.cue_id != cue_id) continue;
        if (!slot.terminal || !slot.request.pin_terminal) result = ESP_ERR_INVALID_STATE;
        else { slot = {}; result = ESP_OK; }
        break;
    }
    portEXIT_CRITICAL(&s_cue_mux);
    return result;
}

// suspended: Cancels current Codex cues on a presentation transition.
// Exception: Returns the owner's retained preparation failure.
extern "C" esp_err_t LUNASHELL_audio_set_codex_suspended(bool suspended)
{
    portENTER_CRITICAL(&s_cue_mux);
    if (s_cue_owner.codex_suspended != suspended) { s_cue_owner.codex_suspended = suspended; ++s_cue_owner.codex_generation; }
    const auto error = s_cue_owner.preparation_error;
    portEXIT_CRITICAL(&s_cue_mux);
    return error;
}

// enabled: Explicit session sound choice.
// Exception: Returns the owner's retained preparation failure.
extern "C" esp_err_t LUNASHELL_audio_set_codex_enabled(bool enabled)
{
    portENTER_CRITICAL(&s_cue_mux);
    if (s_cue_owner.codex_enabled != enabled) { s_cue_owner.codex_enabled = enabled; ++s_cue_owner.codex_generation; }
    const auto error = s_cue_owner.preparation_error;
    portEXIT_CRITICAL(&s_cue_mux);
    return error;
}

// suspended: Cancels personality and pet requests on a presentation transition.
// Exception: Returns the owner's retained preparation failure.
extern "C" esp_err_t LUNASHELL_audio_set_personality_suspended(bool suspended)
{
    portENTER_CRITICAL(&s_cue_mux);
    if (s_cue_owner.suspended != suspended) { s_cue_owner.suspended = suspended; ++s_cue_owner.generation; }
    const auto error = s_cue_owner.preparation_error;
    portEXIT_CRITICAL(&s_cue_mux);
    return error;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
namespace {
constexpr uint32_t kSpeechWaitMs = 5000;
struct SpeechAudioSession {
    TaskHandle_t owner = nullptr;
    uint32_t request_id = 0;
    uint32_t generation = 0;
    uint32_t transport_rate = 0;
    LUNASHELL::AudioPcmResampler resampler{};
};
SpeechAudioSession s_speech_audio;
std::atomic<TaskHandle_t> s_speech_owner;

// generation: Active stream identity.
// samples: Borrowed converted PCM, copied into the bounded output queue.
// frames: Exact output frames in this block.
// Exception: Returns the original queue or worker-notification error.
esp_err_t enqueue_speech_pcm(uint32_t generation, const int16_t *samples, size_t frames)
{
    if (frames == 0) return ESP_OK;
    const auto error = LUNASHELL::enqueue_audio_stream(s_audio_stream, generation, samples, frames, kSpeechWaitMs);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Speech PCM enqueue failed (%s)", esp_err_to_name(error));
        return error;
    }
    return wake_cue_worker();
}

// generation: Exact active stream identity.
// cancel: Selects cancellation instead of normal end of input.
// cause: Original cancellation reason when cancel is true.
// Exception: Returns original pipe failure or a bounded contention timeout.
esp_err_t close_speech_input(uint32_t generation, bool cancel, esp_err_t cause)
{
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto error = cancel ? LUNASHELL::cancel_audio_stream(s_audio_stream, generation, cause) :
            LUNASHELL::end_audio_stream_input(s_audio_stream, generation);
        if (error != ESP_ERR_NOT_FINISHED) return error;
        vTaskDelay(1);
    }
    ESP_LOGE(TAG, "Speech stream control timed out (%s)", esp_err_to_name(ESP_ERR_TIMEOUT));
    return ESP_ERR_TIMEOUT;
}

// generation: Identity of the just-finished stream.
// Exception: Releases the reservation only after an observed terminal acknowledgement.
esp_err_t retire_speech_stream(uint32_t generation)
{
    const auto terminal = LUNASHELL::wait_audio_stream_terminal(s_audio_stream, generation, kSpeechWaitMs);
    LUNASHELL::AudioStreamStatus status{};
    esp_err_t observed = ESP_ERR_NOT_FINISHED;
    for (unsigned attempt = 0; attempt < 100 && observed == ESP_ERR_NOT_FINISHED; ++attempt) {
        observed = LUNASHELL::observe_audio_stream(s_audio_stream, generation, &status);
        if (observed == ESP_ERR_NOT_FINISHED) vTaskDelay(1);
    }
    if (observed == ESP_OK && !status.active) {
        s_audio_stream_generation.store(0, std::memory_order_release);
        s_audio_stream_rate.store(0, std::memory_order_release);
        release_audio_stream_slot();
    } else {
        ESP_LOGE(TAG, "Speech stream retirement unconfirmed: wait=%s observation=%s",
            esp_err_to_name(terminal), esp_err_to_name(observed));
        return terminal != ESP_OK ? terminal : observed != ESP_OK ? observed : ESP_ERR_TIMEOUT;
    }
    return terminal;
}

// No parameters: releases a previously timed-out stream only after worker acknowledgement.
// Exception: Returns busy, contention or the original pipe observation error.
esp_err_t reap_finished_speech_stream()
{
    const uint32_t generation = s_audio_stream_generation.load(std::memory_order_acquire);
    if (generation == 0) return ESP_OK;
    LUNASHELL::AudioStreamStatus status{};
    const auto error = LUNASHELL::observe_audio_stream(s_audio_stream, generation, &status);
    if (error != ESP_OK) return error;
    if (status.active) return ESP_ERR_NOT_FINISHED;
    s_audio_stream_generation.store(0, std::memory_order_release);
    s_audio_stream_rate.store(0, std::memory_order_release);
    release_audio_stream_slot();
    return ESP_OK;
}
} // namespace

// source_rate_hz: Actual mono PCM clock supplied by the speech engine.
// request_id: Nonzero identity owned by the speech worker.
// Exception: Reserves one mixer voice and returns original format, lifecycle or codec errors.
extern "C" esp_err_t LUNASHELL_audio_begin_speech(uint32_t source_rate_hz, uint32_t request_id)
{
    if (source_rate_hz < 8000 || source_rate_hz > 48000 || request_id == 0)
        return reject_playback("Speech format", ESP_ERR_INVALID_ARG, source_rate_hz, 0);
    TaskHandle_t expected = nullptr;
    const TaskHandle_t owner = xTaskGetCurrentTaskHandle();
    if (!s_speech_owner.compare_exchange_strong(expected, owner))
        return reject_playback("Speech session busy", ESP_ERR_NOT_FINISHED, source_rate_hz, 0);
    auto error = cody_output_policy_error();
    if (error == ESP_OK) error = LUNASHELL_audio_init();
    if (error == ESP_OK) error = ensure_cue_worker();
    if (error == ESP_OK) error = reap_finished_speech_stream();
    bool reserved = false;
    if (error == ESP_OK) {
        error = reserve_audio_stream_slot();
        reserved = error == ESP_OK;
    }
    SpeechAudioSession session{};
    session.owner = owner;
    session.request_id = request_id;
    if (error == ESP_OK) {
        error = cody_output_policy_error();
        if (error == ESP_OK && (s_standby.load() || s_standby_transition.load() ||
            s_audio_state_poisoned.load())) error = ESP_ERR_INVALID_STATE;
        session.transport_rate = s_output_transport_rate.load(std::memory_order_acquire);
        if (error == ESP_OK && (session.transport_rate < 8000 || session.transport_rate > 48000))
            error = ESP_ERR_NOT_FINISHED;
        if (error == ESP_OK && s_user_output_volume.load(std::memory_order_acquire) == 0)
            error = ESP_ERR_INVALID_STATE;
        if (error == ESP_OK) error = LUNASHELL::initialize_audio_resampler(session.resampler,
            source_rate_hz, session.transport_rate);
        if (error == ESP_OK) error = LUNASHELL::begin_audio_stream(s_audio_stream, request_id, &session.generation);
        if (error == ESP_OK) {
            s_speech_audio = session;
            s_audio_stream_rate.store(session.transport_rate, std::memory_order_release);
            s_audio_stream_gain.store(1000, std::memory_order_release);
            s_audio_stream_generation.store(session.generation, std::memory_order_release);
        }
    }
    if (error != ESP_OK) {
        if (reserved) release_audio_stream_slot();
        s_speech_owner.store(nullptr);
        return reject_playback("Speech admission", error, source_rate_hz, 0);
    }
    error = wake_cue_worker();
    if (error != ESP_OK) {
        const auto cancel_error = close_speech_input(session.generation, true, error);
        ESP_LOGE(TAG, "Speech worker notification failed: notify=%s cancel=%s",
            esp_err_to_name(error), esp_err_to_name(cancel_error));
        s_speech_owner.store(nullptr);
        return error;
    }
    return ESP_OK;
}

// samples: Borrowed mono PCM input, consumed or copied before returning.
// sample_count: Exact engine block length.
// request_id: Identity of the calling speech worker's active session.
// Exception: Returns original conversion, stream or worker error.
extern "C" esp_err_t LUNASHELL_audio_write_speech(const int16_t *samples, size_t sample_count, uint32_t request_id)
{
    if (s_speech_owner.load() != xTaskGetCurrentTaskHandle() || samples == nullptr ||
        sample_count == 0 || request_id != s_speech_audio.request_id) return ESP_ERR_INVALID_ARG;
    size_t offset = 0;
    size_t produced = 0;
    do {
        int16_t converted[LUNASHELL::kAudioStreamBlockFrames];
        size_t consumed = 0;
        const auto error = LUNASHELL::resample_audio_pcm(s_speech_audio.resampler, samples + offset,
            sample_count - offset, false, &consumed, converted, LUNASHELL::kAudioStreamBlockFrames, &produced);
        if (error != ESP_OK) return error;
        offset += consumed;
        const auto enqueue_error = enqueue_speech_pcm(s_speech_audio.generation, converted, produced);
        if (enqueue_error != ESP_OK) return enqueue_error;
        if (consumed == 0 && produced == 0 && offset < sample_count) return ESP_ERR_INVALID_STATE;
    } while (offset < sample_count || produced == LUNASHELL::kAudioStreamBlockFrames);
    return ESP_OK;
}

// request_id: Exact identity of the stream owned by the calling speech worker.
// cause: Original synthesis, policy or cancellation error, retained over cleanup failures.
// Exception: Returns the original cause or the real stream/drain failure.
extern "C" esp_err_t LUNASHELL_audio_end_speech(uint32_t request_id, esp_err_t cause)
{
    if (s_speech_owner.load() != xTaskGetCurrentTaskHandle() || request_id != s_speech_audio.request_id)
        return ESP_ERR_INVALID_STATE;
    auto error = cause;
    if (error == ESP_OK) {
        while (!s_speech_audio.resampler.finished) {
            int16_t converted[LUNASHELL::kAudioStreamBlockFrames];
            size_t consumed = 0, produced = 0;
            error = LUNASHELL::resample_audio_pcm(s_speech_audio.resampler, nullptr, 0, true,
                &consumed, converted, LUNASHELL::kAudioStreamBlockFrames, &produced);
            if (error != ESP_OK) break;
            error = enqueue_speech_pcm(s_speech_audio.generation, converted, produced);
            if (error != ESP_OK) break;
        }
    }
    const auto close_error = close_speech_input(s_speech_audio.generation, error != ESP_OK, error);
    if (error == ESP_OK) error = close_error;
    const auto wake_error = wake_cue_worker();
    if (error == ESP_OK) error = wake_error;
    const auto terminal_error = retire_speech_stream(s_speech_audio.generation);
    if (error == ESP_OK) error = terminal_error;
    if (error != ESP_OK) ESP_LOGE(TAG, "Speech stream ended with %s", esp_err_to_name(error));
    s_speech_audio = {};
    s_speech_owner.store(nullptr);
    return error;
}
#endif
