#include "LUNASHELL_activity_internal.hpp"
#include "LUNASHELL_snapshot_internal.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>

#if CONFIG_LUNASHELL_ACTIVITY_MODEL
#include "dl_model_base.hpp"
#include "activity_preprocessing.hpp"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <new>

extern const uint8_t activity_model_start[] asm("_binary_activity_model_start");
namespace {
constexpr const char *TAG = "LUNASHELL_activity";
constexpr size_t kSamples = 80;
struct Window {
    float standardized_axes[kSamples * 3] = {};
    int64_t timestamps[kSamples] = {};
};
// 4800 bytes of owner storage in PSRAM. Only normal tasks access these windows;
// no ISR, DMA or cache-disabled flash callback may borrow their memory. The mutex
// stays internal, and inference exclusively owns its window after the locked copy.
EXT_RAM_BSS_ATTR Window s_collecting;
EXT_RAM_BSS_ATTR Window s_pending;
EXT_RAM_BSS_ATTR Window s_inference;
std::mutex s_mutex;
TaskHandle_t s_task = nullptr;
size_t s_count = 0;
bool s_active = false;
bool s_pending_ready = false;
bool s_calibrated = false;
uint32_t s_generation = 0;
uint32_t s_pending_generation = 0;
uint32_t s_sequence = 0;
LUNASHELL::ActivityClass s_previous_class = LUNASHELL::ActivityClass::Unknown;
uint32_t s_stability = 0;
std::atomic_bool s_model_failed{false};

// rejection: Precise reason the current observer has no usable result.
// error: Original operation failure or the specific unavailable-state error.
// time_us: Timestamp of the failure or owner transition.
void publish_rejection(LUNASHELL::ActivityRejection rejection, esp_err_t error, int64_t time_us)
{
    s_stability = 0;
    s_previous_class = LUNASHELL::ActivityClass::Unknown;
    const esp_err_t publication = LUNASHELL::snapshot_detail::update_snapshot(
        [rejection, error, time_us](LUNASHELL::DeviceSnapshot &snapshot) {
            snapshot.activity.scores_valid = false;
            snapshot.activity.activity = LUNASHELL::ActivityClass::Unknown;
            snapshot.activity.rejection = rejection;
            snapshot.activity.error = error;
            snapshot.activity.stable_windows = 0;
            snapshot.activity.inferred_at_us = time_us;
        });
    LUNASHELL::snapshot_detail::log_snapshot_publication_failure("Activity rejection", publication);
}

// model: Component-owned engine constructed through the official loading constructor.
// input: Borrowed model input tensor output.
// output: Borrowed model output tensor output.
// Exception: Returns an unavailable or incompatible tensor contract; ESP-DL logs its loader cause.
esp_err_t inspect_model(dl::Model &model, dl::TensorBase *&input, dl::TensorBase *&output)
{
    auto &inputs = model.get_inputs();
    auto &outputs = model.get_outputs();
    if (inputs.size() != 1 || outputs.size() != 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    input = inputs.begin()->second;
    output = outputs.begin()->second;
    if (input == nullptr || output == nullptr || input->data == nullptr || output->data == nullptr ||
        input->get_size() != 240 || output->get_size() != 6 ||
        input->shape != std::vector<int>({1, 80, 3, 1})) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

// unused: The worker exclusively owns model lifetime and inference scratch.
void worker(void *unused)
{
    (void)unused;
    // ESP-DL's default constructor leaves ModelContext null; only the loading
    // constructor initializes it. Zero internal preference uses PSRAM tensors.
    auto *model = new (std::nothrow) dl::Model(
        reinterpret_cast<const char *>(activity_model_start), fbs::MODEL_LOCATION_IN_FLASH_RODATA,
        0, dl::MEMORY_MANAGER_GREEDY, nullptr, false);
    dl::TensorBase *input = nullptr;
    dl::TensorBase *output = nullptr;
    const esp_err_t model_error = model == nullptr ? ESP_ERR_NO_MEM : inspect_model(*model, input, output);
    if (model_error != ESP_OK) {
        ESP_LOGE(TAG, "Model initialization failed (%s)", esp_err_to_name(model_error));
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_model_failed.store(true, std::memory_order_release);
            s_pending_ready = false;
            publish_rejection(LUNASHELL::ActivityRejection::ModelError, model_error, esp_timer_get_time());
        }
        // The worker alone owns the partially loaded model. Release its tensors
        // and loader outside the acquisition mutex; no new samples are admitted.
        delete model;
        // WithCaps self-deletion allocates a temporary internal cleanup task in
        // IDF. Park this existing worker on the failure path instead of requiring
        // another allocation while reporting a possible memory failure.
        for (;;) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (!s_pending_ready || !s_active) {
                continue;
            }
            s_inference = s_pending;
            generation = s_pending_generation;
            s_pending_ready = false;
        }
        LUNASHELL::ActivitySnapshot result;
        result.window_start_us = s_inference.timestamps[0];
        result.window_end_us = s_inference.timestamps[kSamples - 1];
        result.error = model_error;
        const int64_t started = esp_timer_get_time();
        if (result.error == ESP_OK &&
            !input->assign({1, 80, 3, 1}, s_inference.standardized_axes, 0, dl::DATA_TYPE_FLOAT)) {
            result.error = ESP_ERR_INVALID_ARG;
        }
        if (result.error == ESP_OK) {
            model->run(dl::RUNTIME_MODE_SINGLE_CORE);
            // TensorBase owns no extra data here; output is copied into the bounded result.
            dl::TensorBase scores({1, 6}, result.scores, 0, dl::DATA_TYPE_FLOAT, false);
            if (!scores.assign(output)) {
                result.error = ESP_ERR_INVALID_RESPONSE;
            }
        }
        result.inferred_at_us = esp_timer_get_time();
        result.inference_duration_us = static_cast<uint32_t>(result.inferred_at_us - started);
        size_t best = 0;
        float sum = 0;
        if (result.error == ESP_OK) {
            for (size_t index = 0; index < 6; ++index) {
                const float score = result.scores[index];
                if (!std::isfinite(score) || score < 0 || score > 1.01F) {
                    result.error = ESP_ERR_INVALID_RESPONSE;
                    break;
                }
                sum += score;
                if (score > result.scores[best]) {
                    best = index;
                }
            }
            if (sum < 0.95F || sum > 1.05F) {
                result.error = ESP_ERR_INVALID_RESPONSE;
            }
        }
        // Fixed typed mapping preserves upstream class identity; indices are tensor storage only.
        constexpr LUNASHELL::ActivityClass labels[] = {
            LUNASHELL::ActivityClass::Downstairs, LUNASHELL::ActivityClass::Jogging,
            LUNASHELL::ActivityClass::Sitting, LUNASHELL::ActivityClass::Standing,
            LUNASHELL::ActivityClass::Upstairs, LUNASHELL::ActivityClass::Walking};
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (!s_active || generation != s_generation) {
                continue;
            }
            result.scores_valid = result.error == ESP_OK;
            result.rejection = result.scores_valid ? LUNASHELL::ActivityRejection::None : LUNASHELL::ActivityRejection::ModelError;
            result.activity = result.scores_valid ? labels[best] : LUNASHELL::ActivityClass::Unknown;
            if (!result.scores_valid) {
                s_stability = 0;
            } else if (result.activity != s_previous_class) {
                s_stability = 1;
            } else if (s_stability != std::numeric_limits<uint32_t>::max()) {
                ++s_stability;
            }
            s_previous_class = result.activity;
            result.stable_windows = s_stability;
            result.sequence = ++s_sequence;
            const esp_err_t publication = LUNASHELL::snapshot_detail::update_snapshot(
                [&result](LUNASHELL::DeviceSnapshot &snapshot) { snapshot.activity = result; });
            LUNASHELL::snapshot_detail::log_snapshot_publication_failure("Activity inference", publication);
        }
        if (result.error != ESP_OK) {
            ESP_LOGE(TAG, "Inference rejected (%s)", esp_err_to_name(result.error));
        } else {
            constexpr const char *names[] = {"downstairs", "jogging", "sitting", "standing", "upstairs", "walking"};
            ESP_LOGI(TAG, "HAR experimental class=%s scores=[%.4f %.4f %.4f %.4f %.4f %.4f] window=%lld..%lld us inference=%lu us domain=UNVALIDATED",
                names[best], result.scores[0], result.scores[1], result.scores[2],
                result.scores[3], result.scores[4], result.scores[5], result.window_start_us, result.window_end_us,
                static_cast<unsigned long>(result.inference_duration_us));
        }
    }
}
} // namespace
#endif

namespace LUNASHELL::snapshot_detail::activity {
// No parameters: immutable opt-in plus the owner's actual engine initialization outcome.
bool requested()
{
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
    return !s_model_failed.load(std::memory_order_acquire);
#else
    return false;
#endif
}

// enabled: Active acquisition admission; no radio, voice or power policy is changed here.
void set_active(bool enabled)
{
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
    std::lock_guard<std::mutex> lock(s_mutex);
    if (enabled == s_active) {
        return;
    }
    s_active = enabled;
    s_count = 0;
    s_pending_ready = false;
    s_stability = 0;
    ++s_generation;
    if (!s_model_failed.load(std::memory_order_acquire)) {
        publish_rejection(enabled ? ActivityRejection::Collecting : ActivityRejection::Suspended,
                          ESP_ERR_INVALID_STATE, esp_timer_get_time());
    }
#else
    (void)enabled;
#endif
}

// sample: One canonical acquisition, never a consumer-side read or repeated snapshot.
// error: Original read/publication error, retained without replacement samples.
// sampled_at_us: Original successful measurement or failed-attempt timestamp.
void accept(const ImuSnapshot &sample, esp_err_t error, int64_t sampled_at_us)
{
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_active || s_model_failed.load(std::memory_order_acquire)) {
        return;
    }
    if (error != ESP_OK || !sample.gesture_acceleration_valid) {
        s_count = 0;
        ++s_generation;
        publish_rejection(ActivityRejection::MissingSample,
                          error != ESP_OK ? error : ESP_ERR_INVALID_STATE, sampled_at_us);
        return;
    }
    if (s_task == nullptr && xTaskCreateWithCaps(worker, "activity_inference", 8192, nullptr, 1,
                                               &s_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "Worker allocation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        s_model_failed.store(true, std::memory_order_release);
        publish_rejection(ActivityRejection::ModelError, ESP_ERR_NO_MEM, sampled_at_us);
        return;
    }
    if (s_count != 0) {
        const int64_t delta = sampled_at_us - s_collecting.timestamps[s_count - 1];
        if (delta >= 0 && delta < 45000) {
            return; // Faster shared consumers do not duplicate/interpolate model inputs.
        }
        if (delta < 45000 || delta > 75000 || sample.calibrated != s_calibrated) {
            ESP_LOGW(TAG, "Window rejected: cadence=%lld us count=%u span=%lld us calibration_changed=%u",
                     delta, static_cast<unsigned>(s_count),
                     sampled_at_us - s_collecting.timestamps[0], sample.calibrated != s_calibrated);
            s_count = 0;
            ++s_generation;
            publish_rejection(ActivityRejection::CadenceGap, ESP_ERR_INVALID_RESPONSE, sampled_at_us);
        }
    }
    s_calibrated = sample.calibrated;
    s_collecting.timestamps[s_count] = sampled_at_us;
    for (size_t axis = 0; axis < 3; ++axis) {
        // The training notebook stores 80 X, then 80 Y, then 80 Z before reshaping.
        // The original raw-g demo omitted this scaler/order; do not reproduce that mismatch.
        s_collecting.standardized_axes[axis * kSamples + s_count] =
            (sample.gesture_acceleration_mps2[axis] - LUNASHELL::activity_model::kMeanMps2[axis]) /
            LUNASHELL::activity_model::kScaleMps2[axis];
    }
#if CONFIG_LUNASHELL_ACTIVITY_CAPTURE
    ESP_LOGI(TAG, "ACTIVITY_SAMPLE,%lu,%u,%lld,%.7f,%.7f,%.7f",
        static_cast<unsigned long>(s_generation), static_cast<unsigned>(s_count), sampled_at_us,
        sample.gesture_acceleration_mps2[0], sample.gesture_acceleration_mps2[1], sample.gesture_acceleration_mps2[2]);
#endif
    if (++s_count == kSamples) {
        const int64_t span = sampled_at_us - s_collecting.timestamps[0];
        if (span < 3700000 || span > 4200000) {
            ESP_LOGW(TAG, "Window rejected: 80-sample span=%lld us (%s)", span, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            ++s_generation;
            publish_rejection(ActivityRejection::CadenceGap, ESP_ERR_INVALID_RESPONSE, sampled_at_us);
        } else if (s_pending_ready) {
            ESP_LOGW(TAG, "Window rejected: inference queue busy (%s)", esp_err_to_name(ESP_ERR_TIMEOUT));
            ++s_generation;
            publish_rejection(ActivityRejection::Busy, ESP_ERR_TIMEOUT, sampled_at_us);
        } else {
            s_pending = s_collecting;
            s_pending_generation = s_generation;
            s_pending_ready = true;
            xTaskNotifyGive(s_task);
        }
        s_count = 0;
    }
#else
    (void)sample;
    (void)error;
    (void)sampled_at_us;
#endif
}
} // namespace LUNASHELL::snapshot_detail::activity
