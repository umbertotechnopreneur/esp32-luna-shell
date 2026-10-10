#include "CODY_boot_cues.h"
#include "runtime_settings.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

extern "C" {
extern const uint8_t audio_ready_start[] asm("_binary_cody_audio_ready_start");
extern const uint8_t audio_ready_end[] asm("_binary_cody_audio_ready_end");
extern const uint8_t eyes_open_start[] asm("_binary_cody_eyes_open_start");
extern const uint8_t eyes_open_end[] asm("_binary_cody_eyes_open_end");
}
namespace {
constexpr char kTag[] = "CODY_boot_cues";
int64_t s_boot_started_us = 0;
int64_t s_stage_started_us = 0;
constexpr uint32_t kDeferredWorkerStackBytes = 4096;
constexpr int64_t kPendingObservationIntervalUs = 1000000;
int64_t s_pending_observation_due_us = 0;
enum class BootCueState : uint8_t {
    NotRequested,
    WaitingClock,
    Queued,
    Playing,
    Completed,
    Suppressed,
    Failed,
};
std::atomic<BootCueState> s_boot_cue_state{BootCueState::NotRequested};
std::atomic<TaskHandle_t> s_deferred_worker{nullptr};
std::atomic<uint32_t> s_boot_user_sounds_revision{0};

// No parameters: the one boot request keeps its original admission token across clock waits.
bool boot_sounds_allowed()
{
    return LUNASHELL_audio_user_sounds_enabled() &&
        LUNASHELL_audio_user_sounds_revision() == s_boot_user_sounds_revision.load(std::memory_order_acquire);
}

// first: Aligned flash-owned little-endian PCM16 data.
// last: Linker end symbol for exactly that asset.
// samples: Receives borrowed samples without decoding or heap allocation.
// count: Receives the exact mono sample count.
// Exception: Returns the actual invalid bounds/alignment error without modifying outputs.
esp_err_t pcm_view(const uint8_t *first, const uint8_t *last,
                   const int16_t *&samples, size_t &count)
{
    const uintptr_t start = reinterpret_cast<uintptr_t>(first);
    const uintptr_t end = reinterpret_cast<uintptr_t>(last);
    if (end <= start || start % alignof(int16_t) != 0 || (end - start) % 2 != 0) {
        ESP_LOGE(kTag, "Boot PCM bounds/alignment rejected: start=%p end=%p alignment=%u (%s)",
            static_cast<const void *>(first), static_cast<const void *>(last),
            static_cast<unsigned>(alignof(int16_t)), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    samples = reinterpret_cast<const int16_t *>(first);
    count = (end - start) / sizeof(int16_t);
    return ESP_OK;
}

// completed: True only when both PCM clips complete under the original Sounds revision.
// Exception: Returns the original asset/codec/playback error.
esp_err_t play_boot_pcm(bool &completed)
{
    completed = false;
    if (!boot_sounds_allowed()) {
        return ESP_OK;
    }
    const uint32_t sounds_revision = s_boot_user_sounds_revision.load(std::memory_order_acquire);
    bool clip_completed = false;
    const int16_t *samples = nullptr;
    size_t count = 0;
    esp_err_t error = pcm_view(audio_ready_start, audio_ready_end, samples, count);
    if (error == ESP_OK) {
        error = LUNASHELL_audio_play_user_cue_pcm16(samples, count,
            CODY::RuntimeData::sample_rate_hz, CODY::RuntimeData::ready_volume_percent,
            sounds_revision, &clip_completed);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Audio-ready cue failed: %s", esp_err_to_name(error));
        return error;
    }
    if (!clip_completed || !boot_sounds_allowed()) {
        return ESP_OK;
    }
    error = pcm_view(eyes_open_start, eyes_open_end, samples, count);
    if (error == ESP_OK) {
        error = LUNASHELL_audio_play_user_cue_pcm16(samples, count,
            CODY::RuntimeData::sample_rate_hz, CODY::RuntimeData::eyes_open_volume_percent,
            sounds_revision, &clip_completed);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Eyes-open cue failed: %s", esp_err_to_name(error));
    }
    completed = error == ESP_OK && clip_completed && boot_sounds_allowed();
    return error;
}

// operation: Deferred admission or prerequisite that actually failed.
// error: Original failure; snapshot publication cannot replace it.
void publish_deferred_failure(const char *operation, esp_err_t error)
{
    s_boot_cue_state.store(BootCueState::Failed, std::memory_order_release);
    ESP_LOGE(kTag, "Deferred boot cue %s failed: %s", operation, esp_err_to_name(error));
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
        false, 0, CODY::RuntimeData::sample_rate_hz,
        CODY::RuntimeData::ready_volume_percent, error);
    if (publication_error != ESP_OK) {
        ESP_LOGE(kTag, "Deferred boot cue failure publication failed: %s",
            esp_err_to_name(publication_error));
    }
}

// No parameters: wakes the sole pending owner to apply clock policy or an invalidated Sounds token.
// Real admission failures are published; no playback completion is inferred from notification.
void queue_pending_boot_observation()
{
    BootCueState expected = BootCueState::WaitingClock;
    if (!s_boot_cue_state.compare_exchange_strong(expected, BootCueState::Queued,
            std::memory_order_acq_rel)) {
        return;
    }
    const TaskHandle_t worker = s_deferred_worker.load(std::memory_order_acquire);
    if (worker == nullptr) {
        publish_deferred_failure("worker ownership", ESP_ERR_INVALID_STATE);
        return;
    }
    if (xTaskNotifyGive(worker) != pdPASS) {
        publish_deferred_failure("clock/preference notification", ESP_FAIL);
        s_deferred_worker.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(worker);
    }
}

// context: Unused; this temporary worker owns the single deferred boot request.
// Exception: Asynchronous errors are published; Sounds cancellation remains Suppressed.
void deferred_boot_worker(void *context)
{
    (void)context;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_boot_cue_state.load(std::memory_order_acquire) != BootCueState::Queued) {
            continue;
        }
        if (!boot_sounds_allowed()) {
            s_boot_cue_state.store(BootCueState::Suppressed, std::memory_order_release);
            ESP_LOGI(kTag, "Deferred boot cues suppressed: Sounds preference invalidated this request");
            break;
        }
        LUNASHELL::ClockSnapshot clock;
        const esp_err_t clock_error =
            LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
        if (clock_error != ESP_OK) {
            publish_deferred_failure("clock observation", clock_error);
            break;
        }
        const auto policy = LUNASHELL::clock_cue_policy(clock);
        if (policy == LUNASHELL::ClockCuePolicy::QuietHours) {
            s_boot_cue_state.store(BootCueState::Suppressed, std::memory_order_release);
            ESP_LOGI(kTag, "Deferred boot cues deliberately suppressed: verified quiet hours");
            break;
        }
        if (policy != LUNASHELL::ClockCuePolicy::Allowed) {
            s_boot_cue_state.store(BootCueState::WaitingClock, std::memory_order_release);
            continue;
        }
        LUNASHELL::ConnectivityObservation connectivity;
        const esp_err_t power_error =
            LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(connectivity);
        if (power_error != ESP_OK) {
            publish_deferred_failure("power observation", power_error);
            break;
        }
        if (connectivity.operating_state != LUNASHELL::DeviceOperatingState::Active) {
            // An unfulfilled boot request survives standby without creating a wake cue.
            s_boot_cue_state.store(BootCueState::WaitingClock, std::memory_order_release);
            continue;
        }
        s_boot_cue_state.store(BootCueState::Playing, std::memory_order_release);
        const int64_t started_us = esp_timer_get_time();
        bool completed = false;
        const esp_err_t playback_error = play_boot_pcm(completed);
        if (playback_error != ESP_OK) {
            publish_deferred_failure("playback", playback_error);
        } else {
            s_boot_cue_state.store(completed ? BootCueState::Completed : BootCueState::Suppressed,
                std::memory_order_release);
        }
        ESP_LOGI(kTag, "Deferred boot PCM operations ended: completed=%u result=%s duration_us=%lld",
            static_cast<unsigned>(completed), esp_err_to_name(playback_error),
            static_cast<long long>(esp_timer_get_time() - started_us));
        break;
    }
    s_deferred_worker.store(nullptr, std::memory_order_release);
    vTaskDeleteWithCaps(nullptr);
}
} // namespace

// No parameters: observes pending clock/Sounds policy without waiting for audio.
// Exception: Actual observation/admission failures are logged and published, not retried as success.
extern "C" void CODY_boot_cues_observe_pending(void)
{
    if (s_boot_cue_state.load(std::memory_order_acquire) != BootCueState::WaitingClock) {
        return;
    }
    if (!boot_sounds_allowed()) {
        queue_pending_boot_observation();
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if (now_us < s_pending_observation_due_us) {
        return;
    }
    s_pending_observation_due_us = now_us + kPendingObservationIntervalUs;
    LUNASHELL::ClockSnapshot clock;
    const esp_err_t observation_error =
        LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
    if (observation_error != ESP_OK) {
        publish_deferred_failure("pending clock observation", observation_error);
        const TaskHandle_t worker = s_deferred_worker.exchange(nullptr, std::memory_order_acq_rel);
        if (worker != nullptr) {
            vTaskDeleteWithCaps(worker);
        }
        return;
    }
    // Disabled quiet hours explicitly permit boot audio before clock synchronization.
    const auto policy = LUNASHELL::clock_cue_policy(clock);
    if (policy != LUNASHELL::ClockCuePolicy::Allowed &&
        policy != LUNASHELL::ClockCuePolicy::QuietHours) {
        return;
    }
    queue_pending_boot_observation();
}

// No parameters: validate flash-owned boot sound; defer playback until trusted time is published.
extern "C" esp_err_t CODY_boot_cues_prepare_boot(void)
{
    if (s_boot_started_us != 0) {
        ESP_LOGE(kTag, "Duplicate boot cue preparation: %s", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_boot_started_us = esp_timer_get_time();
    s_stage_started_us = s_boot_started_us;
    const int16_t *samples = nullptr;
    size_t count = 0;
    esp_err_t error = pcm_view(audio_ready_start, audio_ready_end, samples, count);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Audio-ready cue preparation failed: %s", esp_err_to_name(error));
    }
    // Codec initialization happens before the time service. Playing there would
    // bypass quiet hours; Cody's existing completed-boot stage owns both cues now.
    return error;
}

// stage: Real completed startup operation, logged to console before the optional SD mirror.
extern "C" void CODY_boot_cues_boot_checkpoint(const char *stage)
{
    const int64_t now_us = esp_timer_get_time();
    if (CODY::RuntimeData::execution_timings) {
        ESP_LOGI(kTag, "TIMING boot stage=\"%s\" stage_us=%lld elapsed_us=%lld", stage,
            static_cast<long long>(now_us - s_stage_started_us),
            static_cast<long long>(now_us - s_boot_started_us));
    }
    s_stage_started_us = now_us;
}

// No parameters: report published SD availability without changing saved log policy.
// Returns the original canonical snapshot error if this observation fails.
extern "C" esp_err_t CODY_boot_cues_configure_diagnostics(void)
{
    LUNASHELL::MotionPowerObservation observation;
    const esp_err_t read_error =
        LUNASHELL::DeviceSnapshotService::instance().readMotionPowerObservation(observation);
    if (read_error != ESP_OK) {
        ESP_LOGE(kTag, "Diagnostics snapshot read failed: %s", esp_err_to_name(read_error));
        return read_error;
    }
    // Timing diagnostics are independent of the user's saved SD preference.
    // In particular, Release must not persistently turn logging off at boot.
    ESP_LOGI(kTag, "Diagnostics timings=%u; published SD availability=%u; saved storage log policy retained",
        static_cast<unsigned>(CODY::RuntimeData::execution_timings),
        static_cast<unsigned>(observation.storage_available));
    return ESP_OK; // The snapshot read completed; no filesystem operation was attempted.
}

// No parameters: play once under the configured quiet policy, or admit a bounded pending request.
// Exception: Returns original clock, worker admission, asset or playback errors.
extern "C" esp_err_t CODY_boot_cues_play_eyes_open(void)
{
    BootCueState expected = BootCueState::NotRequested;
    if (!s_boot_cue_state.compare_exchange_strong(expected, BootCueState::Playing,
            std::memory_order_acq_rel)) {
        ESP_LOGE(kTag, "Duplicate completed-boot cue request: %s", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_boot_user_sounds_revision.store(LUNASHELL_audio_user_sounds_revision(), std::memory_order_release);
    if (!boot_sounds_allowed()) {
        s_boot_cue_state.store(BootCueState::Suppressed, std::memory_order_release);
        ESP_LOGI(kTag, "Boot cues deliberately suppressed: Sounds Off; saved codec volume unchanged");
        CODY_boot_cues_boot_checkpoint("boot cues suppressed by Sounds preference");
        return ESP_OK;
    }
    LUNASHELL::ClockSnapshot clock;
    const esp_err_t observation_error =
        LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
    if (observation_error != ESP_OK) {
        publish_deferred_failure("initial clock observation", observation_error);
        return observation_error;
    }
    const auto policy = LUNASHELL::clock_cue_policy(clock);
    if (policy != LUNASHELL::ClockCuePolicy::Allowed &&
        policy != LUNASHELL::ClockCuePolicy::QuietHours) {
        TaskHandle_t worker = nullptr;
        if (xTaskCreateWithCaps(deferred_boot_worker, "CODY_boot_audio", kDeferredWorkerStackBytes,
                nullptr, 2, &worker, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            publish_deferred_failure("worker allocation", ESP_ERR_NO_MEM);
            return ESP_ERR_NO_MEM;
        }
        s_deferred_worker.store(worker, std::memory_order_release);
        s_boot_cue_state.store(BootCueState::WaitingClock, std::memory_order_release);
        ESP_LOGI(kTag, "Boot cues pending verified canonical time: clock=%s preferences=%u; no PCM submitted",
            esp_err_to_name(clock.last_error), static_cast<unsigned>(clock.preferences_valid));
        CODY_boot_cues_boot_checkpoint("boot cues admitted pending verified time");
        return ESP_OK; // One request was admitted; only the worker can report its playback result.
    }
    if (policy != LUNASHELL::ClockCuePolicy::Allowed) {
        s_boot_cue_state.store(BootCueState::Suppressed, std::memory_order_release);
        const char *reason = policy == LUNASHELL::ClockCuePolicy::QuietHours ? "quiet hours" :
            policy == LUNASHELL::ClockCuePolicy::TimeUnavailable ? "time unavailable/stale" : "settings unavailable";
        ESP_LOGI(kTag, "Boot cues deliberately suppressed: %s; saved codec volume unchanged", reason);
        CODY_boot_cues_boot_checkpoint("boot cues suppressed by routine policy");
        return ESP_OK;
    }
    bool completed = false;
    const esp_err_t error = play_boot_pcm(completed);
    BootCueState result = BootCueState::Suppressed;
    if (error != ESP_OK) {
        result = BootCueState::Failed;
    } else if (completed) {
        result = BootCueState::Completed;
    }
    s_boot_cue_state.store(result, std::memory_order_release);
    CODY_boot_cues_boot_checkpoint("eyes-open audio cue");
    return error;
}
