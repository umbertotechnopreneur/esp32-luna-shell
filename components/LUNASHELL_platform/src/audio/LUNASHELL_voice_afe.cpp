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
 *
 * VBWR E */

/*
 * Dedicated ESP-SR voice front end. Diagnostic audio never feeds this worker.
 */

#include "LUNASHELL_platform.h"
#if !CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
#error "Disabled wake-word builds must exclude this translation unit"
#endif
#if !CONFIG_SPIRAM_BOOT_INIT || !CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
#error "Wake-word buffers and static worker stacks require initialized PSRAM and external-stack support"
#endif
#include "LUNASHELL_task_affinity.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_voice_input.hpp"
#include "LUNASHELL_snapshot_internal.hpp"
#include "esp_timer.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
#include "model_path.h"
#include "esp_partition.h"
#endif

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "esp_afe_config.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_aec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "LUNASHELL_voice_afe";
constexpr uint32_t kVoiceSampleRateHz = 16000;
constexpr size_t kReferenceRingFrames = 8000;
// ESP-IDF task stack sizes are bytes, including the static task API.
constexpr size_t kFeedTaskStackBytes = 4096;
constexpr size_t kFetchTaskStackBytes = 3072;
constexpr TickType_t kReferenceLockTimeout = pdMS_TO_TICKS(20);
constexpr TickType_t kTaskCancellationTimeout = pdMS_TO_TICKS(1000);

#if CONFIG_LUNASHELL_BUILD_VOICE_MICROPHONE_CHANNELS == 2
constexpr uint8_t kMicrophoneChannels = 2;
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
constexpr char kAfeInputFormat[] = "MMNR";
#else
constexpr char kAfeInputFormat[] = "MM";
#endif
#elif CONFIG_LUNASHELL_BUILD_VOICE_MICROPHONE_CHANNELS == 1
constexpr uint8_t kMicrophoneChannels = 1;
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
constexpr char kAfeInputFormat[] = "MR";
#else
constexpr char kAfeInputFormat[] = "M";
#endif
#else
#error "Voice capture requires an explicit supported microphone channel count"
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
constexpr char kWakeModelName[] = "wn9_sophia_tts_v1";
struct ReviewedWakeModel {
    const char *name;
    const char *word;
};
constexpr ReviewedWakeModel kReviewedWakeModels[] = {
    {kWakeModelName, "Sophia"},
    {"wn9_jarvis_tts", "Jarvis"},
    {"wn9_computer_tts", "Computer"},
};
constexpr int kReviewedWakeModelCount = 3;
constexpr int kActiveWakeModelApiIndex = 1;
constexpr int kSophiaWordApiIndex = 1;
constexpr int64_t kDetectionCooldownUs = 1500000;
srmodel_list_t *s_models;
char *s_wake_model_name;
#endif

#if CONFIG_LUNASHELL_BUILD_VOICE_AFE_SR_PROFILE_HIGH_PERF
constexpr afe_mode_t kAfeMode = AFE_MODE_HIGH_PERF;
constexpr aec_mode_t kAecMode = AEC_MODE_SR_HIGH_PERF;
#else
constexpr afe_mode_t kAfeMode = AFE_MODE_LOW_COST;
constexpr aec_mode_t kAecMode = AEC_MODE_SR_LOW_COST;
#endif

const esp_afe_sr_iface_t *s_afe_handle;
esp_afe_sr_data_t *s_afe_data;
int16_t *s_reference_ring;
size_t s_reference_read;
size_t s_reference_write;
size_t s_reference_count;
StaticSemaphore_t s_reference_mutex_control;
SemaphoreHandle_t s_reference_mutex;
StaticTask_t s_feed_task_control;
StaticTask_t s_fetch_task_control;
StackType_t *s_feed_task_stack;
StackType_t *s_fetch_task_stack;
std::atomic_bool s_running{false};
std::atomic_bool s_start_released{false};
std::atomic_bool s_fetch_paused{false};
std::atomic_bool s_start_cancelled{false};
std::atomic_bool s_feed_start_cancelled_ack{false};
std::atomic_bool s_standby_requested{false};
bool s_power_standby_requested;
bool s_performance_suspended;
bool s_diagnostic_suspended;
std::mutex s_standby_mutex;
std::mutex s_publication_mutex;
std::atomic_bool s_feed_paused{false};
std::atomic<TaskHandle_t> s_feed_task_handle{nullptr};
std::atomic_bool s_initialization_attempted{false};
std::atomic<esp_err_t> s_initialization_result{ESP_ERR_INVALID_STATE};

// state: Actual AFE lifecycle state.
// error: Original operation error, retained in the canonical snapshot.
// detected_at_us: Confirmed detection time; zero publishes status only.
void publish_voice(LUNASHELL::VoiceState state, esp_err_t error, int64_t detected_at_us = 0)
{
    // A terminal failure wins over a fetch result already in flight.
    std::lock_guard<std::mutex> lock(s_publication_mutex);
    if (state == LUNASHELL::VoiceState::Listening &&
        s_initialization_result.load(std::memory_order_acquire) != ESP_OK) {
        return;
    }
    if (state == LUNASHELL::VoiceState::Failed) {
        s_initialization_result.store(error, std::memory_order_release);
        s_running.store(false, std::memory_order_release);
    }
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportVoice(
        state, error, detected_at_us);
    LUNASHELL::snapshot_detail::log_snapshot_publication_failure("voice AFE", publication_error);
}

// samples: Real interleaved capture block; unused only when capture failed.
// frames: Number of acquired PCM frames per channel.
// sampled_at_us: Time the hardware read completed.
// error: Original capture error.
void publish_microphones(const int16_t *samples, size_t frames, int64_t sampled_at_us,
                         esp_err_t error)
{
    LUNASHELL::AudioSnapshot audio;
    audio.channel_count = kMicrophoneChannels;
    audio.sample_rate_hz = kVoiceSampleRateHz;
    if (error == ESP_OK) {
        uint64_t combined_sum = 0;
        for (uint8_t channel = 0; channel < kMicrophoneChannels; ++channel) {
            uint64_t sum = 0;
            uint32_t peak = 0;
            for (size_t frame = 0; frame < frames; ++frame) {
                const int32_t sample = samples[frame * kMicrophoneChannels + channel];
                const uint32_t magnitude = static_cast<uint32_t>(sample < 0 ? -sample : sample);
                sum += magnitude;
                if (magnitude > peak) {
                    peak = magnitude;
                }
            }
            audio.channel_mean_level[channel] = static_cast<uint32_t>(sum / frames);
            audio.channel_peak_level[channel] = peak;
            combined_sum += sum;
            if (peak > audio.microphone_peak_level) {
                audio.microphone_peak_level = peak;
            }
        }
        audio.microphone_mean_level = static_cast<uint32_t>(combined_sum / (frames * kMicrophoneChannels));
    }
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportVoiceMicrophones(
        audio, sampled_at_us, error);
    LUNASHELL::snapshot_detail::log_snapshot_publication_failure("voice microphones", publication_error);
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
// No parameters: loads the reviewed inventory while selecting only Sophia for inference.
// Exception: Returns a missing-partition, model-load or inventory error.
esp_err_t load_wake_model()
{
    if (esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "model") == nullptr) {
        ESP_LOGE(kTag, "Wake model partition missing (%s)", esp_err_to_name(ESP_ERR_NOT_FOUND));
        return ESP_ERR_NOT_FOUND;
    }
    s_models = esp_srmodel_init("model");
    if (s_models == nullptr) {
        ESP_LOGE(kTag, "Wake model load failed (%s)", esp_err_to_name(ESP_FAIL));
        return ESP_FAIL;
    }
    if (s_models->num != kReviewedWakeModelCount || s_models->model_name == nullptr) {
        ESP_LOGE(kTag, "Wake model inventory rejected: expected %d models, found %d (%s)",
                 kReviewedWakeModelCount, s_models->num, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        return ESP_ERR_INVALID_RESPONSE;
    }
    // Model-list order is a storage detail; each expected identity must occur once.
    for (const ReviewedWakeModel &expected : kReviewedWakeModels) {
        int matches = 0;
        char *matched_name = nullptr;
        for (int index = 0; index < s_models->num; ++index) {
            char *candidate = s_models->model_name[index];
            if (candidate != nullptr && std::strcmp(candidate, expected.name) == 0) {
                matched_name = candidate;
                ++matches;
            }
        }
        if (matches != 1) {
            ESP_LOGE(kTag, "Wake model identity rejected: %s matches=%d (%s)",
                     expected.name, matches, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
        // Unlike the borrowed model name, ESP-SR allocates this metadata string.
        char *words = esp_srmodel_get_wake_words(s_models, matched_name);
        if (words == nullptr || std::strcmp(words, expected.word) != 0) {
            ESP_LOGE(kTag, "Wake word identity rejected: model=%s word=%s (%s)",
                     expected.name, words != nullptr ? words : "unavailable",
                     esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            free(words);
            return ESP_ERR_INVALID_RESPONSE;
        }
        free(words);
        if (std::strcmp(expected.name, kWakeModelName) == 0) {
            s_wake_model_name = matched_name;
        }
    }
    return ESP_OK;
}
#endif

// Publish failed optional startup and release models only after AFE teardown.
struct InitializationPublication final {
    ~InitializationPublication()
    {
        const esp_err_t error = s_initialization_result.load(std::memory_order_acquire);
        if (error != ESP_OK) {
            publish_voice(LUNASHELL::VoiceState::Failed, error);
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
            if (s_models != nullptr && s_afe_data == nullptr &&
                s_feed_task_handle.load(std::memory_order_acquire) == nullptr) {
                esp_srmodel_deinit(s_models);
                s_models = nullptr;
                s_wake_model_name = nullptr;
            }
#endif
        }
    }
};

SemaphoreHandle_t reference_mutex()
{
    if (s_reference_mutex == nullptr) {
        s_reference_mutex = xSemaphoreCreateMutexStatic(&s_reference_mutex_control);
    }
    return s_reference_mutex;
}

// operation: Name of the lifecycle transition needing a feed notification.
// The standby lock protects the published handle until notification completes.
esp_err_t notify_feed_task_locked(const char *operation)
{
    TaskHandle_t feed_task = s_feed_task_handle.load(std::memory_order_acquire);
    if (feed_task == nullptr) {
        ESP_LOGE(kTag, "AFE feed %s notification rejected: task is unavailable", operation);
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskNotifyGive(feed_task) != pdPASS) {
        ESP_LOGE(kTag, "AFE feed %s notification failed", operation);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Remove the published handle under the standby lock before a live worker exits.
void finish_feed_task()
{
    {
        std::lock_guard<std::mutex> lock(s_standby_mutex);
        s_feed_task_handle.store(nullptr, std::memory_order_release);
    }
    vTaskDelete(nullptr);
}

// arg: Unused FreeRTOS task context.
// The only voice capture owner also publishes real microphone levels.
void voice_feed_task(void *arg)
{
    (void)arg;
    while (!s_start_released.load(std::memory_order_acquire)) {
        if (s_start_cancelled.load(std::memory_order_acquire)) {
            s_feed_start_cancelled_ack.store(true, std::memory_order_release);
            // Initialization owns deletion after observing this parked state.
            vTaskSuspend(nullptr);
            vTaskDelete(nullptr);
            return;
        }
        vTaskDelay(1);
    }
    const int feed_frames = s_afe_handle->get_feed_chunksize(s_afe_data);
    const int feed_channels = s_afe_handle->get_feed_channel_num(s_afe_data);
    if (feed_frames <= 0 ||
        feed_channels != static_cast<int>(sizeof(kAfeInputFormat) - 1U)) {
        ESP_LOGE(
            kTag,
            "AFE feed contract rejected: frames=%d channels=%d format=%s",
            feed_frames,
            feed_channels,
            kAfeInputFormat
        );
        publish_voice(LUNASHELL::VoiceState::Failed, ESP_ERR_INVALID_RESPONSE);
        s_running.store(false, std::memory_order_release);
        finish_feed_task();
        return;
    }

    const size_t microphone_samples =
        static_cast<size_t>(feed_frames) * kMicrophoneChannels;
    const size_t feed_samples =
        static_cast<size_t>(feed_frames) * static_cast<size_t>(feed_channels);
    auto *microphones = static_cast<int16_t *>(heap_caps_aligned_alloc(
        16,
        microphone_samples * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    auto *feed = static_cast<int16_t *>(heap_caps_aligned_alloc(
        16,
        feed_samples * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    if (microphones == nullptr || feed == nullptr) {
        ESP_LOGE(kTag, "AFE feed allocation failed");
        free(microphones);
        free(feed);
        s_running.store(false, std::memory_order_release);
        publish_voice(LUNASHELL::VoiceState::Failed, ESP_ERR_NO_MEM);
        finish_feed_task();
        return;
    }

    while (s_running.load(std::memory_order_acquire)) {
        if (s_standby_requested.load(std::memory_order_acquire)) {
            s_feed_paused.store(true, std::memory_order_release);
            while (s_running.load(std::memory_order_acquire) &&
                   s_standby_requested.load(std::memory_order_acquire)) {
                (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            }
            s_feed_paused.store(false, std::memory_order_release);
            continue;
        }
        const esp_err_t capture_error = LUNASHELL_audio_capture_voice_pcm16(
            microphones,
            static_cast<size_t>(feed_frames),
            kMicrophoneChannels
        );
        if (capture_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "16 kHz microphone capture failed (%s)",
                esp_err_to_name(capture_error)
            );
            publish_microphones(nullptr, 0, esp_timer_get_time(), capture_error);
            publish_voice(LUNASHELL::VoiceState::Failed, capture_error);
            s_running.store(false, std::memory_order_release);
            break;
        }
        publish_microphones(microphones, static_cast<size_t>(feed_frames), esp_timer_get_time(), ESP_OK);

        if (xSemaphoreTake(reference_mutex(), kReferenceLockTimeout) != pdTRUE) {
            ESP_LOGE(kTag, "AEC reference ring lock timed out");
            publish_voice(LUNASHELL::VoiceState::Failed, ESP_ERR_TIMEOUT);
            s_running.store(false, std::memory_order_release);
            break;
        }
        for (int frame = 0; frame < feed_frames; ++frame) {
            const int16_t reference = s_reference_count > 0
                ? s_reference_ring[s_reference_read]
                : 0;
            if (s_reference_count > 0) {
                s_reference_read = (s_reference_read + 1U) % kReferenceRingFrames;
                --s_reference_count;
            }
            uint8_t microphone_channel = 0;
            for (int channel = 0; channel < feed_channels; ++channel) {
                const char role = kAfeInputFormat[channel];
                if (role == 'M') {
                    feed[frame * feed_channels + channel] =
                        microphones[frame * kMicrophoneChannels + microphone_channel];
                    ++microphone_channel;
                } else {
                    // N is an intentionally unused AFE slot. R is actual playback
                    // reference, or silence when no speaker samples were written.
                    feed[frame * feed_channels + channel] = role == 'R' ? reference : 0;
                }
            }
        }
        if (xSemaphoreGive(reference_mutex()) != pdTRUE) {
            ESP_LOGE(kTag, "AEC reference ring unlock failed");
            std::abort();
        }
        const int feed_result = s_afe_handle->feed(s_afe_data, feed);
        if (feed_result < 0) {
            ESP_LOGE(kTag, "AFE feed rejected a %d-frame input block (%s)",
                     feed_frames, esp_err_to_name(ESP_FAIL));
            publish_voice(LUNASHELL::VoiceState::Failed, ESP_FAIL);
            s_running.store(false, std::memory_order_release);
        }
    }

    free(microphones);
    free(feed);
    s_feed_paused.store(false, std::memory_order_release);
    finish_feed_task();
}

// arg: Unused FreeRTOS task context.
// Consume inference results; never read hardware from this consumer.
void voice_fetch_task(void *arg)
{
    (void)arg;
    while (!s_start_released.load(std::memory_order_acquire)) {
        if (s_start_cancelled.load(std::memory_order_acquire)) {
            vTaskDelete(nullptr);
            return;
        }
        vTaskDelay(1);
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    int64_t last_detection_at_us = 0;
#endif
    while (s_running.load(std::memory_order_acquire)) {
        if (s_standby_requested.load(std::memory_order_acquire)) {
            s_fetch_paused.store(true, std::memory_order_release);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        s_fetch_paused.store(false, std::memory_order_release);
        afe_fetch_result_t *result = s_afe_handle->fetch_with_delay(s_afe_data, pdMS_TO_TICKS(200));
        // A suspended feed has no results. Pausing must never invent a failure
        // or turn a buffered detection into a fresh standby wake.
        if (s_standby_requested.load(std::memory_order_acquire) ||
            !s_running.load(std::memory_order_acquire)) {
            continue;
        }
        if (result == nullptr) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
            LUNASHELL::feedVoiceInput(nullptr, 0, false, ESP_FAIL);
#endif
            ESP_LOGE(kTag, "AFE fetch returned null (%s)", esp_err_to_name(ESP_FAIL));
            publish_voice(LUNASHELL::VoiceState::Failed, ESP_FAIL);
            s_running.store(false, std::memory_order_release);
            esp_err_t notification_error = ESP_OK;
            {
                // Serialize against handle withdrawal in finish_feed_task().
                std::lock_guard<std::mutex> lock(s_standby_mutex);
                notification_error = notify_feed_task_locked("fetch-failure");
            }
            if (notification_error != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "AFE feed wake after fetch failure failed (%s)",
                    esp_err_to_name(notification_error)
                );
            }
            break;
        }
        if (result->ret_value != ESP_OK) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
            LUNASHELL::feedVoiceInput(nullptr, 0, false, result->ret_value);
#endif
            ESP_LOGE(kTag, "AFE fetch failed (%s)", esp_err_to_name(result->ret_value));
            publish_voice(LUNASHELL::VoiceState::Failed, result->ret_value);
            s_running.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> lock(s_standby_mutex);
            const esp_err_t notification_error = notify_feed_task_locked("inference-failure");
            if (notification_error != ESP_OK) {
                ESP_LOGE(kTag, "Feed failure notification failed (%s)", esp_err_to_name(notification_error));
            }
            break;
        }
        int64_t detected_at_us = 0;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
        if (result->wakeup_state == WAKENET_DETECTED) {
            if (result->wakenet_model_index != kActiveWakeModelApiIndex ||
                result->wake_word_index != kSophiaWordApiIndex) {
                ESP_LOGE(kTag, "Wake identity rejected: model=%d word=%d (%s)",
                         result->wakenet_model_index, result->wake_word_index,
                         esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
                publish_voice(LUNASHELL::VoiceState::Failed, ESP_ERR_INVALID_RESPONSE);
                s_running.store(false, std::memory_order_release);
                break;
            }
            const int64_t now_us = esp_timer_get_time();
            if (last_detection_at_us == 0 || now_us - last_detection_at_us >= kDetectionCooldownUs) {
                detected_at_us = now_us;
                last_detection_at_us = now_us;
                ESP_LOGI(kTag, "Wake word detected: Sophia model=%s", kWakeModelName);
            }
        }
#endif
        publish_voice(LUNASHELL::VoiceState::Listening, ESP_OK, detected_at_us);
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        bool capture_started_here = false;
        if (detected_at_us != 0) {
            // The explicit event is handled even while Cody is already awake.
            const esp_err_t error = LUNASHELL::requestVoiceInput(LUNASHELL::VoiceInputTrigger::WakeWord);
            capture_started_here = error == ESP_OK;
            if (error != ESP_OK) ESP_LOGE(kTag, "Wake-word capture admission failed (%s)", esp_err_to_name(error));
        }
        // Detection is the admission event, not the user's first audio packet.
        // Start with the next acquired block; do not prepend a wake-word cache.
        if (capture_started_here) {
            continue;
        }
        if (result->data_size <= 0 || (result->data_size % int(sizeof(int16_t))) != 0) {
            LUNASHELL::feedVoiceInput(nullptr, 0, false, ESP_ERR_INVALID_SIZE);
        } else {
            LUNASHELL::feedVoiceInput(result->data, size_t(result->data_size) / sizeof(int16_t),
                result->vad_state == VAD_SPEECH, ESP_OK);
        }
#endif
    }
    s_fetch_paused.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(s_standby_mutex);
        if (s_feed_task_handle.load(std::memory_order_acquire) != nullptr) {
            const esp_err_t notification_error = notify_feed_task_locked("fetch-exit");
            if (notification_error != ESP_OK) {
                ESP_LOGE(kTag, "Feed exit notification failed (%s)", esp_err_to_name(notification_error));
            }
        }
    }
    vTaskDelete(nullptr);
}

// standby: Whether all microphone acquisition must pause.
// Keep the capture lease until the audio route has completed its transition.
esp_err_t apply_afe_standby_locked(bool standby)
{
    if (!s_running.load(std::memory_order_acquire)) {
        s_standby_requested.store(standby, std::memory_order_release);
        const esp_err_t error = s_initialization_result.load(std::memory_order_acquire);
        if (s_initialization_attempted.load(std::memory_order_acquire) && error != ESP_OK) {
            ESP_LOGE(kTag, "AFE transition rejected after failure (%s)", esp_err_to_name(error));
            return error;
        }
        return ESP_OK;
    }
    if (s_standby_requested.load(std::memory_order_acquire) == standby &&
        s_feed_paused.load(std::memory_order_acquire) == standby &&
        s_fetch_paused.load(std::memory_order_acquire) == standby) {
        return ESP_OK;
    }

    constexpr TickType_t kPauseTimeout = pdMS_TO_TICKS(1000);
    if (standby) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        const esp_err_t input_error = LUNASHELL::pauseVoiceInput();
        if (input_error != ESP_OK) {
            ESP_LOGE(kTag, "AFE voice input pause failed (%s)", esp_err_to_name(input_error));
            return input_error;
        }
#endif
        s_standby_requested.store(true, std::memory_order_release);
        const TickType_t started_at = xTaskGetTickCount();
        while (!s_feed_paused.load(std::memory_order_acquire) ||
               !s_fetch_paused.load(std::memory_order_acquire)) {
            if (!s_running.load(std::memory_order_acquire)) {
                const esp_err_t stopped_error = s_initialization_result.load(std::memory_order_acquire);
                ESP_LOGE(kTag, "AFE feed stopped while a pause was pending (%s)", esp_err_to_name(stopped_error));
                s_standby_requested.store(false, std::memory_order_release);
                return stopped_error != ESP_OK ? stopped_error : ESP_ERR_INVALID_STATE;
            }
            if (xTaskGetTickCount() - started_at >= kPauseTimeout) {
                ESP_LOGE(kTag, "AFE feed pause timed out");
                s_standby_requested.store(false, std::memory_order_release);
                const esp_err_t notification_error =
                    notify_feed_task_locked("pause-timeout");
                if (notification_error != ESP_OK) {
                    ESP_LOGE(
                        kTag,
                        "AFE pause-timeout recovery notification failed (%s)",
                        esp_err_to_name(notification_error)
                    );
                }
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(1);
        }
        const esp_err_t audio_error = LUNASHELL_audio_set_standby(true);
        if (audio_error != ESP_OK) {
            s_standby_requested.store(false, std::memory_order_release);
            const esp_err_t notification_error =
                notify_feed_task_locked("audio-suspend-rollback");
            if (notification_error != ESP_OK) {
                s_standby_requested.store(true, std::memory_order_release);
                ESP_LOGE(
                    kTag,
                    "AFE feed rollback notification failed after audio suspend error (%s)",
                    esp_err_to_name(notification_error)
                );
            }
            return audio_error;
        }
        publish_microphones(nullptr, 0, esp_timer_get_time(), ESP_ERR_NOT_ALLOWED);
        publish_voice(LUNASHELL::VoiceState::Paused, ESP_OK);
        ESP_LOGI(kTag, "AFE capture loop suspended");
        return ESP_OK;
    }

    // Keep capture claimed until the 16 kHz route is fully restored so a
    // diagnostic request cannot reopen the codec at 22.05 kHz in between.
    const esp_err_t audio_error = LUNASHELL_audio_set_standby(false);
    if (audio_error != ESP_OK) {
        return audio_error;
    }
    // Discard inference buffers from before the pause before capture resumes.
    if (s_afe_handle->reset_buffer(s_afe_data) < 0) {
        ESP_LOGE(kTag, "AFE buffer reset failed (%s)", esp_err_to_name(ESP_FAIL));
        publish_voice(LUNASHELL::VoiceState::Failed, ESP_FAIL);
        return ESP_FAIL;
    }
    if (xSemaphoreTake(reference_mutex(), kReferenceLockTimeout) != pdTRUE) {
        ESP_LOGE(kTag, "AEC reference reset lock timed out (%s)", esp_err_to_name(ESP_ERR_TIMEOUT));
        publish_voice(LUNASHELL::VoiceState::Failed, ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }
    s_reference_read = 0;
    s_reference_write = 0;
    s_reference_count = 0;
    if (xSemaphoreGive(reference_mutex()) != pdTRUE) {
        ESP_LOGE(kTag, "AEC reference reset unlock failed");
        std::abort();
    }
    s_standby_requested.store(false, std::memory_order_release);
    const esp_err_t notification_error = notify_feed_task_locked("resume");
    if (notification_error != ESP_OK) {
        s_standby_requested.store(true, std::memory_order_release);
        const esp_err_t rollback_error = LUNASHELL_audio_set_standby(true);
        if (rollback_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "AFE audio rollback failed after feed notification error (%s)",
                esp_err_to_name(rollback_error)
            );
        }
        return notification_error;
    }
    const TickType_t started_at = xTaskGetTickCount();
    while (s_feed_paused.load(std::memory_order_acquire) ||
           s_fetch_paused.load(std::memory_order_acquire)) {
        if (!s_running.load(std::memory_order_acquire)) {
            const esp_err_t stopped_error = s_initialization_result.load(std::memory_order_acquire);
            ESP_LOGE(kTag, "AFE feed stopped while a resume was pending (%s)", esp_err_to_name(stopped_error));
            s_standby_requested.store(true, std::memory_order_release);
            const esp_err_t rollback_error = LUNASHELL_audio_set_standby(true);
            if (rollback_error != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "AFE audio rollback failed after feed stopped (%s)",
                    esp_err_to_name(rollback_error)
                );
            }
            return stopped_error != ESP_OK ? stopped_error : ESP_ERR_INVALID_STATE;
        }
        if (xTaskGetTickCount() - started_at >= kPauseTimeout) {
            ESP_LOGE(kTag, "AFE feed resume timed out");
            s_standby_requested.store(true, std::memory_order_release);
            const esp_err_t rollback_error = LUNASHELL_audio_set_standby(true);
            if (rollback_error != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "AFE audio rollback failed after feed resume timeout (%s)",
                    esp_err_to_name(rollback_error)
                );
            }
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
    if (!s_running.load(std::memory_order_acquire)) {
        const esp_err_t stopped_error = s_initialization_result.load(std::memory_order_acquire);
        ESP_LOGE(kTag, "AFE feed stopped during resume completion (%s)", esp_err_to_name(stopped_error));
        return stopped_error != ESP_OK ? stopped_error : ESP_ERR_INVALID_STATE;
    }
    publish_voice(LUNASHELL::VoiceState::Listening, ESP_OK);
    ESP_LOGI(kTag, "AFE capture loop resumed");
    return ESP_OK;
}

}  // namespace

// Build the AFE and publish worker handles before opening the startup gate.
// No parameters: starts the optional wake-word owner on the voice core.
// Exception: Returns the original model, audio, allocation or task-start failure.
extern "C" esp_err_t LUNASHELL_voice_afe_init(void)
{
    if (s_running.load(std::memory_order_acquire)) {
        return s_initialization_result.load(std::memory_order_acquire);
    }
    if (s_initialization_attempted.exchange(true, std::memory_order_acq_rel)) {
        return s_initialization_result.load(std::memory_order_acquire);
    }
    InitializationPublication publication;
    publish_voice(LUNASHELL::VoiceState::Starting, ESP_OK);
    if (LUNASHELL_audio_input_channel_count() != kMicrophoneChannels) {
        ESP_LOGE(kTag, "Selected microphone topology does not match AFE layout");
        s_initialization_result.store(ESP_ERR_INVALID_STATE, std::memory_order_release);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t route_error = LUNASHELL_audio_prepare_voice_route();
    if (route_error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "16 kHz voice route preparation failed (%s)",
            esp_err_to_name(route_error)
        );
        s_initialization_result.store(route_error, std::memory_order_release);
        return route_error;
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    const esp_err_t model_error = load_wake_model();
    if (model_error != ESP_OK) {
        s_initialization_result.store(model_error, std::memory_order_release);
        return model_error;
    }
#endif
    s_reference_ring = static_cast<int16_t *>(heap_caps_aligned_alloc(
        16,
        kReferenceRingFrames * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    if (s_reference_ring == nullptr) {
        ESP_LOGE(kTag, "AEC reference ring allocation failed");
        s_initialization_result.store(ESP_ERR_NO_MEM, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    std::memset(s_reference_ring, 0, kReferenceRingFrames * sizeof(int16_t));
    // Publish the mutex before playback and capture can race to initialize it.
    if (reference_mutex() == nullptr) {
        ESP_LOGE(kTag, "AEC reference mutex creation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        free(s_reference_ring);
        s_reference_ring = nullptr;
        s_initialization_result.store(ESP_ERR_NO_MEM, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }

    afe_config_t *config = afe_config_init(
        kAfeInputFormat,
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
        s_models,
#else
        nullptr,
#endif
        AFE_TYPE_SR,
        kAfeMode
    );
    if (config == nullptr) {
        ESP_LOGE(kTag, "AFE configuration creation failed");
        free(s_reference_ring);
        s_reference_ring = nullptr;
        s_initialization_result.store(ESP_ERR_NO_MEM, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    // A speaker reference exists only when an output adapter is enabled.
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    config->aec_init = true;
#else
    config->aec_init = false;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    config->wakenet_init = true;
    config->wakenet_model_name = s_wake_model_name;
    config->wakenet_model_name_2 = nullptr;
    config->wakenet_mode = kMicrophoneChannels == 2 ? DET_MODE_2CH_90 : DET_MODE_90;
#else
    config->wakenet_init = false;
#endif
    config->aec_mode = kAecMode;
    // ESP-SR creates its own processing task; pinning only feed/fetch is insufficient.
    config->afe_perferred_core = LUNASHELL::task_affinity::kVoiceCoreId;
    // Use the vendor's most external-memory-oriented mode. ESP-SR still owns
    // internal-only inference resources; never relocate its opaque structures.
    config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    s_afe_handle = esp_afe_handle_from_config(config);
    s_afe_data = s_afe_handle != nullptr
        ? s_afe_handle->create_from_config(config)
        : nullptr;
    afe_config_free(config);
    if (s_afe_data == nullptr) {
        ESP_LOGE(kTag, "AFE instance creation failed for format=%s", kAfeInputFormat);
        free(s_reference_ring);
        s_reference_ring = nullptr;
        s_initialization_result.store(ESP_FAIL, std::memory_order_release);
        return ESP_FAIL;
    }

    // Both workers wait behind this gate so partial task creation cannot use
    // an AFE instance that startup subsequently tears down.
    s_running.store(false, std::memory_order_release);
    s_start_cancelled.store(false, std::memory_order_release);
    s_feed_start_cancelled_ack.store(false, std::memory_order_release);
    s_feed_task_handle.store(nullptr, std::memory_order_release);
    s_feed_task_stack = static_cast<StackType_t *>(heap_caps_aligned_alloc(
        16,
        kFeedTaskStackBytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    s_fetch_task_stack = static_cast<StackType_t *>(heap_caps_aligned_alloc(
        16,
        kFetchTaskStackBytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    if (s_feed_task_stack == nullptr || s_fetch_task_stack == nullptr) {
        ESP_LOGE(
            kTag,
            "AFE task stack allocation failed: feed=%p fetch=%p",
            s_feed_task_stack,
            s_fetch_task_stack
        );
        free(s_feed_task_stack);
        free(s_fetch_task_stack);
        s_feed_task_stack = nullptr;
        s_fetch_task_stack = nullptr;
        s_afe_handle->destroy(s_afe_data);
        s_afe_data = nullptr;
        s_afe_handle = nullptr;
        free(s_reference_ring);
        s_reference_ring = nullptr;
        s_initialization_result.store(ESP_ERR_NO_MEM, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    TaskHandle_t feed_task = xTaskCreateStaticPinnedToCore(
        voice_feed_task,
        "LUNASHELL_afe_feed",
        kFeedTaskStackBytes,
        nullptr,
        4,
        s_feed_task_stack,
        &s_feed_task_control,
        LUNASHELL::task_affinity::kVoiceCoreId
    );
    if (feed_task == nullptr) {
        ESP_LOGE(kTag, "AFE feed task creation failed");
        free(s_feed_task_stack);
        free(s_fetch_task_stack);
        s_feed_task_stack = nullptr;
        s_fetch_task_stack = nullptr;
        s_afe_handle->destroy(s_afe_data);
        s_afe_data = nullptr;
        s_afe_handle = nullptr;
        free(s_reference_ring);
        s_reference_ring = nullptr;
        s_initialization_result.store(ESP_ERR_NO_MEM, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    s_feed_task_handle.store(feed_task, std::memory_order_release);

    TaskHandle_t fetch_task = xTaskCreateStaticPinnedToCore(
        voice_fetch_task,
        "LUNASHELL_afe_fetch",
        kFetchTaskStackBytes,
        nullptr,
        4,
        s_fetch_task_stack,
        &s_fetch_task_control,
        LUNASHELL::task_affinity::kVoiceCoreId
    );
    if (fetch_task == nullptr) {
        ESP_LOGE(kTag, "AFE fetch task creation failed after feed task startup");
        s_start_cancelled.store(true, std::memory_order_release);
        const TickType_t cancellation_started_at = xTaskGetTickCount();
        while (!s_feed_start_cancelled_ack.load(std::memory_order_acquire)) {
            if (xTaskGetTickCount() - cancellation_started_at >=
                kTaskCancellationTimeout) {
                ESP_LOGE(
                    kTag,
                    "AFE feed startup cancellation timed out; live task resources retained"
                );
                s_initialization_result.store(
                    ESP_ERR_TIMEOUT,
                    std::memory_order_release
                );
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(1);
        }
        // The feed task acknowledged cancellation and parked before its static
        // stack and AFE dependencies are released.
        vTaskSuspend(feed_task);
        vTaskDelete(feed_task);
        s_feed_task_handle.store(nullptr, std::memory_order_release);
        s_afe_handle->destroy(s_afe_data);
        s_afe_data = nullptr;
        s_afe_handle = nullptr;
        free(s_reference_ring);
        s_reference_ring = nullptr;
        free(s_feed_task_stack);
        free(s_fetch_task_stack);
        s_feed_task_stack = nullptr;
        s_fetch_task_stack = nullptr;
        s_initialization_result.store(ESP_ERR_NO_MEM, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }

    s_initialization_result.store(ESP_OK, std::memory_order_release);
    s_running.store(true, std::memory_order_release);
    s_start_released.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(s_standby_mutex);
        const bool standby =
            s_power_standby_requested ||
            s_performance_suspended ||
            s_diagnostic_suspended;
        const esp_err_t standby_error = standby
            ? apply_afe_standby_locked(true)
            : ESP_OK;
        if (standby_error != ESP_OK) {
            s_initialization_result.store(
                standby_error,
                std::memory_order_release
            );
            ESP_LOGE(
                kTag,
                "AFE started but standby handoff failed (%s)",
                esp_err_to_name(standby_error)
            );
            return standby_error;
        }
    }
    const esp_err_t startup_result = s_initialization_result.load(std::memory_order_acquire);
    if (startup_result != ESP_OK) {
        return startup_result;
    }
    ESP_LOGI(
        kTag,
        "ESP-SR AFE ready: 16kHz format=%s type=SR profile=%s feed/fetch/inference-core=%d",
        kAfeInputFormat,
        kAfeMode == AFE_MODE_HIGH_PERF ? "high-perf" : "low-cost",
        LUNASHELL::task_affinity::kVoiceCoreId
    );
    return ESP_OK;
}

extern "C" bool LUNASHELL_voice_afe_running(void)
{
    return s_running.load(std::memory_order_acquire);
}

extern "C" bool LUNASHELL_voice_afe_capture_active(void)
{
    return s_running.load(std::memory_order_acquire) &&
           !s_standby_requested.load(std::memory_order_acquire);
}

// standby: Requested power-policy pause; software wake-word standby keeps this false.
extern "C" esp_err_t LUNASHELL_voice_afe_set_standby(bool standby)
{
    std::lock_guard<std::mutex> lock(s_standby_mutex);
    const bool previous = s_power_standby_requested;
    s_power_standby_requested = standby;
    const esp_err_t error = apply_afe_standby_locked(
        s_power_standby_requested ||
        s_performance_suspended ||
        s_diagnostic_suspended
    );
    if (error != ESP_OK) {
        s_power_standby_requested = previous;
        const esp_err_t rollback_error = apply_afe_standby_locked(
            s_power_standby_requested ||
            s_performance_suspended ||
            s_diagnostic_suspended
        );
        if (rollback_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "AFE power-standby rollback failed (%s)",
                esp_err_to_name(rollback_error)
            );
        }
    }
    if (error != ESP_OK) {
        publish_voice(LUNASHELL::VoiceState::Failed, error);
    }
    return error;
}

// suspended: Independent Performance Mode request to pause recognition.
extern "C" esp_err_t LUNASHELL_voice_afe_set_performance_suspended(bool suspended)
{
    std::lock_guard<std::mutex> lock(s_standby_mutex);
    const bool previous = s_performance_suspended;
    s_performance_suspended = suspended;
    const esp_err_t error = apply_afe_standby_locked(
        s_power_standby_requested ||
        s_performance_suspended ||
        s_diagnostic_suspended
    );
    if (error != ESP_OK) {
        s_performance_suspended = previous;
        const esp_err_t rollback_error = apply_afe_standby_locked(
            s_power_standby_requested ||
            s_performance_suspended ||
            s_diagnostic_suspended
        );
        if (rollback_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "AFE Performance Mode rollback failed (%s)",
                esp_err_to_name(rollback_error)
            );
        }
        publish_voice(LUNASHELL::VoiceState::Failed, error);
        return error;
    }
    ESP_LOGI(
        kTag,
        "AFE Performance Mode suppression: %s",
        suspended ? "active" : "inactive"
    );
    return ESP_OK;
}

// suspended: Exclusive audio diagnostic lease, which pauses recognition.
extern "C" esp_err_t LUNASHELL_voice_afe_set_diagnostic_suspended(bool suspended)
{
    std::lock_guard<std::mutex> lock(s_standby_mutex);
    if (s_diagnostic_suspended == suspended) {
        const esp_err_t error = s_initialization_result.load(std::memory_order_acquire);
        if (s_initialization_attempted.load(std::memory_order_acquire) && error != ESP_OK) {
            ESP_LOGE(kTag, "AFE diagnostic handoff rejected after failure (%s)", esp_err_to_name(error));
            return error;
        }
        return ESP_OK;
    }

    const bool previous = s_diagnostic_suspended;
    s_diagnostic_suspended = suspended;
    const esp_err_t error = apply_afe_standby_locked(
        s_power_standby_requested ||
        s_performance_suspended ||
        s_diagnostic_suspended
    );
    if (error != ESP_OK) {
        s_diagnostic_suspended = previous;
        const esp_err_t rollback_error = apply_afe_standby_locked(
            s_power_standby_requested ||
            s_performance_suspended ||
            s_diagnostic_suspended
        );
        if (rollback_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "AFE diagnostic-capture rollback failed (%s)",
                esp_err_to_name(rollback_error)
            );
        }
        publish_voice(LUNASHELL::VoiceState::Failed, error);
        return error;
    }
    ESP_LOGI(
        kTag,
        "AFE diagnostic microphone handoff %s",
        suspended ? "active" : "released"
    );
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_voice_afe_push_speaker_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz
)
{
    if (samples == nullptr || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_running.load(std::memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sample_rate_hz != kVoiceSampleRateHz) {
        ESP_LOGE(
            kTag,
            "AEC reference rejected: expected 16 kHz, received %lu Hz",
            static_cast<unsigned long>(sample_rate_hz)
        );
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(reference_mutex(), kReferenceLockTimeout) != pdTRUE) {
        ESP_LOGE(kTag, "AEC reference enqueue lock timed out");
        return ESP_ERR_TIMEOUT;
    }
    if (sample_count > kReferenceRingFrames - s_reference_count) {
        xSemaphoreGive(reference_mutex());
        ESP_LOGE(
            kTag,
            "AEC reference ring overflow: queued=%u available=%u",
            static_cast<unsigned>(sample_count),
            static_cast<unsigned>(kReferenceRingFrames - s_reference_count)
        );
        return ESP_ERR_NO_MEM;
    }
    for (size_t index = 0; index < sample_count; ++index) {
        s_reference_ring[s_reference_write] = samples[index];
        s_reference_write = (s_reference_write + 1U) % kReferenceRingFrames;
        ++s_reference_count;
    }
    if (xSemaphoreGive(reference_mutex()) != pdTRUE) {
        ESP_LOGE(kTag, "AEC reference enqueue unlock failed");
        std::abort();
    }
    return ESP_OK;
}
