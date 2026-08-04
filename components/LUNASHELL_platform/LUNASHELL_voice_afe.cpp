/*
 * Dedicated ESP-SR voice front end. Diagnostic audio never feeds this worker.
 */

#include "LUNASHELL_platform.h"

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
constexpr size_t kFeedTaskStackWords = 4096;
constexpr size_t kFetchTaskStackWords = 3072;
constexpr TickType_t kReferenceLockTimeout = pdMS_TO_TICKS(20);
constexpr TickType_t kTaskCancellationTimeout = pdMS_TO_TICKS(1000);

#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_ES7210_DUAL
constexpr char kAfeInputFormat[] = "MMNR";
constexpr uint8_t kMicrophoneChannels = 2;
#else
constexpr char kAfeInputFormat[] = "MR";
constexpr uint8_t kMicrophoneChannels = 1;
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
std::atomic_bool s_start_cancelled{false};
std::atomic_bool s_feed_start_cancelled_ack{false};
std::atomic_bool s_standby_requested{false};
bool s_power_standby_requested;
bool s_performance_suspended;
bool s_diagnostic_suspended;
std::mutex s_standby_mutex;
std::atomic_bool s_feed_paused{false};
std::atomic<TaskHandle_t> s_feed_task_handle{nullptr};
std::atomic_bool s_initialization_attempted{false};
std::atomic<esp_err_t> s_initialization_result{ESP_ERR_INVALID_STATE};

SemaphoreHandle_t reference_mutex()
{
    if (s_reference_mutex == nullptr) {
        s_reference_mutex = xSemaphoreCreateMutexStatic(&s_reference_mutex_control);
    }
    return s_reference_mutex;
}

// Wake the feed worker while the standby lock protects its published handle.
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

// Feed ESP-SR continuously while active and block without polling in standby.
void voice_feed_task(void *arg)
{
    (void)arg;
    while (!s_running.load(std::memory_order_acquire)) {
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
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (xSemaphoreTake(reference_mutex(), kReferenceLockTimeout) != pdTRUE) {
            ESP_LOGE(kTag, "AEC reference ring lock timed out");
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        for (int frame = 0; frame < feed_frames; ++frame) {
            const int16_t reference = s_reference_count > 0
                ? s_reference_ring[s_reference_read]
                : 0;
            if (s_reference_count > 0) {
                s_reference_read = (s_reference_read + 1U) % kReferenceRingFrames;
                --s_reference_count;
            }
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_ES7210_DUAL
            feed[frame * feed_channels] = microphones[frame * kMicrophoneChannels];
            feed[frame * feed_channels + 1] =
                microphones[frame * kMicrophoneChannels + 1];
            feed[frame * feed_channels + 2] = 0;
            feed[frame * feed_channels + 3] = reference;
#else
            feed[frame * feed_channels] = microphones[frame];
            feed[frame * feed_channels + 1] = reference;
#endif
        }
        if (xSemaphoreGive(reference_mutex()) != pdTRUE) {
            ESP_LOGE(kTag, "AEC reference ring unlock failed");
            std::abort();
        }
        const int feed_result = s_afe_handle->feed(s_afe_data, feed);
        if (feed_result < 0) {
            ESP_LOGE(kTag, "AFE feed rejected a %d-frame input block", feed_frames);
            s_running.store(false, std::memory_order_release);
        }
    }

    free(microphones);
    free(feed);
    s_feed_paused.store(false, std::memory_order_release);
    finish_feed_task();
}

// Fetch AFE results and release a feed worker blocked during a terminal failure.
void voice_fetch_task(void *arg)
{
    (void)arg;
    while (!s_running.load(std::memory_order_acquire)) {
        if (s_start_cancelled.load(std::memory_order_acquire)) {
            vTaskDelete(nullptr);
            return;
        }
        vTaskDelay(1);
    }
    while (s_running.load(std::memory_order_acquire)) {
        afe_fetch_result_t *result = s_afe_handle->fetch(s_afe_data);
        if (result == nullptr) {
            ESP_LOGE(kTag, "AFE fetch returned null");
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
            ESP_LOGW(
                kTag,
                "AFE fetch reported %s",
                esp_err_to_name(result->ret_value)
            );
        }
        // WakeNet, VAD, and command recognition intentionally attach here later.
    }
    vTaskDelete(nullptr);
}

// Transition the AFE and its audio route without releasing capture prematurely.
esp_err_t apply_afe_standby_locked(bool standby)
{
    if (!s_running.load(std::memory_order_acquire)) {
        s_standby_requested.store(standby, std::memory_order_release);
        return ESP_OK;
    }

    constexpr TickType_t kPauseTimeout = pdMS_TO_TICKS(1000);
    if (standby) {
        s_standby_requested.store(true, std::memory_order_release);
        const TickType_t started_at = xTaskGetTickCount();
        while (!s_feed_paused.load(std::memory_order_acquire)) {
            if (!s_running.load(std::memory_order_acquire)) {
                ESP_LOGE(kTag, "AFE feed stopped while a pause was pending");
                s_standby_requested.store(false, std::memory_order_release);
                return ESP_ERR_INVALID_STATE;
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
        ESP_LOGI(kTag, "AFE capture loop suspended");
        return ESP_OK;
    }

    // Keep capture claimed until the 16 kHz route is fully restored so a
    // diagnostic request cannot reopen the codec at 22.05 kHz in between.
    const esp_err_t audio_error = LUNASHELL_audio_set_standby(false);
    if (audio_error != ESP_OK) {
        return audio_error;
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
    while (s_feed_paused.load(std::memory_order_acquire)) {
        if (!s_running.load(std::memory_order_acquire)) {
            ESP_LOGE(kTag, "AFE feed stopped while a resume was pending");
            s_standby_requested.store(true, std::memory_order_release);
            const esp_err_t rollback_error = LUNASHELL_audio_set_standby(true);
            if (rollback_error != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "AFE audio rollback failed after feed stopped (%s)",
                    esp_err_to_name(rollback_error)
                );
            }
            return ESP_ERR_INVALID_STATE;
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
        ESP_LOGE(kTag, "AFE feed stopped during resume completion");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(kTag, "AFE capture loop resumed");
    return ESP_OK;
}

}  // namespace

// Build the AFE and publish worker handles before opening the startup gate.
extern "C" esp_err_t LUNASHELL_voice_afe_init(void)
{
    if (s_running.load(std::memory_order_acquire)) {
        return ESP_OK;
    }
    if (s_initialization_attempted.exchange(true, std::memory_order_acq_rel)) {
        return s_initialization_result.load(std::memory_order_acquire);
    }
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

    afe_config_t *config = afe_config_init(
        kAfeInputFormat,
        nullptr,
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
    // This first integration is AEC-only. WebRTC NS/VAD defaults need no
    // model partition; WakeNet and command models attach in a later feature.
    config->aec_init = true;
    config->aec_mode = kAecMode;
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
        kFeedTaskStackWords * sizeof(StackType_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    s_fetch_task_stack = static_cast<StackType_t *>(heap_caps_aligned_alloc(
        16,
        kFetchTaskStackWords * sizeof(StackType_t),
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
    TaskHandle_t feed_task = xTaskCreateStatic(
        voice_feed_task,
        "LUNASHELL_afe_feed",
        kFeedTaskStackWords,
        nullptr,
        4,
        s_feed_task_stack,
        &s_feed_task_control
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

    TaskHandle_t fetch_task = xTaskCreateStatic(
        voice_fetch_task,
        "LUNASHELL_afe_fetch",
        kFetchTaskStackWords,
        nullptr,
        4,
        s_fetch_task_stack,
        &s_fetch_task_control
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

    s_running.store(true, std::memory_order_release);
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
    s_initialization_result.store(ESP_OK, std::memory_order_release);
    ESP_LOGI(
        kTag,
        "ESP-SR AFE ready: 16kHz format=%s type=SR profile=%s",
        kAfeInputFormat,
        kAfeMode == AFE_MODE_HIGH_PERF ? "high-perf" : "low-cost"
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
    return error;
}

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
        return error;
    }
    ESP_LOGI(
        kTag,
        "AFE Performance Mode suppression: %s",
        suspended ? "active" : "inactive"
    );
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_voice_afe_set_diagnostic_suspended(bool suspended)
{
    std::lock_guard<std::mutex> lock(s_standby_mutex);
    if (s_diagnostic_suspended == suspended) {
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
