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
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_snapshot_aging.hpp"
#include "LUNASHELL_snapshot_internal.hpp"
#include "LUNASHELL_acquisition_internal.hpp"
#include "LUNASHELL_acquisition_pmu.hpp"
#include "LUNASHELL_acquisition_rtc.hpp"
#include "LUNASHELL_acquisition_imu.hpp"
#include "LUNASHELL_activity_internal.hpp"
#include "LUNASHELL_acquisition_gnss.hpp"
#include "LUNASHELL_acquisition_audio.hpp"
#include "LUNASHELL_acquisition_system.hpp"
#include "LUNASHELL_distance_service.hpp"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_snapshot_cadence.hpp"
#include "LUNASHELL_task_affinity.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY || \
    CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH || \
    CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS || \
    CONFIG_LUNASHELL_BUILD_ENABLE_RTC || \
    CONFIG_LUNASHELL_BUILD_ENABLE_IMU || \
    CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
#include "bsp/esp-bsp.h"
#endif
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

namespace imu = LUNASHELL::snapshot_detail::imu;
namespace rtc = LUNASHELL::snapshot_detail::rtc;
namespace gnss = LUNASHELL::snapshot_detail::gnss;
namespace audio = LUNASHELL::snapshot_detail::audio;
namespace system_acquisition = LUNASHELL::snapshot_detail::system_acquisition;
using LUNASHELL::snapshot_detail::ensure_refresh_mutex;
using LUNASHELL::snapshot_detail::give_refresh_mutex;
using LUNASHELL::snapshot_detail::record_first_error;
using LUNASHELL::snapshot_detail::sample_publication_result;
using LUNASHELL::snapshot_detail::high_frequency_sample_phase;
using LUNASHELL::snapshot_detail::copy_orientation_snapshot;
using LUNASHELL::snapshot_detail::process_orientation_sample;
using ImuRawSample = imu::RawSample;
using RtcLatchObservation = rtc::LatchObservation;
namespace acquisition_pmu = LUNASHELL::acquisition_pmu;

using LUNASHELL::snapshot_detail::health_for;
using LUNASHELL::snapshot_detail::set_component;
using LUNASHELL::snapshot_detail::mark_sample_success;
using LUNASHELL::snapshot_detail::mark_sample_failure;
using LUNASHELL::snapshot_detail::log_snapshot_publication_failure;
using LUNASHELL::snapshot_detail::kDiagnosticPeriodMinMs;
using LUNASHELL::snapshot_detail::kDiagnosticPeriodMaxMs;
using LUNASHELL::snapshot_detail::kImuSmoothingMaxPercent;
using LUNASHELL::snapshot_detail::SnapshotPublicationPolicy;

const char *TAG = "LUNASHELL_snapshot";
constexpr char kDiagnosticPeriodSetting[] = "diag_ms";
constexpr char kImuSmoothingSetting[] = "imu_smooth";
constexpr char kAutoRotationSetting[] = "auto_rotate";
constexpr char kLegFlipSetting[] = "leg_flip";
constexpr char kDisplayRotationSetting[] = "display_rot";
constexpr uint32_t kDiagnosticPeriodDefaultMs = 300;
constexpr uint32_t kImuSmoothingDefaultPercent = 70;
constexpr uint32_t kOrientationSamplePeriodMs = 100;
constexpr uint32_t kOrientationApplyRetryBackoffMs = 1000;
constexpr uint32_t kUiMotionSamplePeriodMs = 100;
constexpr uint32_t kUiMotionStandbySamplePeriodMs = 1000;
constexpr int64_t kOrientationStableUs = 300000;
constexpr float kOrientationMagnitudeMin = 7.0F;
constexpr float kOrientationMagnitudeMax = 12.5F;
constexpr float kOrientationPlanarEnter = 5.5F;
constexpr float kOrientationPlanarExit = 4.5F;
constexpr float kOrientationDominance = 1.25F;
constexpr float kOrientationFilterAlpha = 0.35F;
constexpr float kOrientationGyroDeadbandDps = 4.0F;
constexpr float kOrientationGyroSnapDegrees = 50.0F;
constexpr float kOrientationGyroMaxStepSeconds = 0.25F;
constexpr uint32_t kPeriodicSummaryIntervalMs = 30000U;
// Snapshot ownership is held only while copying or committing the canonical
// value. Reaching this deadline is therefore an invariant failure, not a slow
// hardware sample, and must never leave UI/HTTP/radio callers blocked forever.
constexpr uint32_t kSnapshotMutexTimeoutMs = 1000U;
constexpr TickType_t kSnapshotMutexTimeoutTicks =
    pdMS_TO_TICKS(kSnapshotMutexTimeoutMs);
bool s_initialized;
bool s_initialization_attempted;
esp_err_t s_initialization_result = ESP_ERR_INVALID_STATE;
bool s_diagnostic_monitor_started;
std::atomic<TaskHandle_t> s_diagnostic_monitor_task_handle{nullptr};
std::atomic_bool s_diagnostic_imu_active{false};
std::atomic_bool s_diagnostic_microphone_active{false};
std::atomic_bool s_diagnostic_microphone_spectrum_active{false};
std::atomic_bool s_ui_motion_monitor_active{false};
std::atomic_bool s_ui_motion_monitor_standby{false};
// Device Standby preserves the user's requested diagnostics and rotation
// preference, but must not leave their high-rate UI sampling active.
std::atomic_bool s_ui_sampling_power_suspended{false};
std::atomic_bool s_ui_sampling_orientation_reset_pending{false};
std::atomic_bool s_diagnostic_stack_report_pending{false};
std::atomic<uint32_t> s_diagnostic_period_ms{kDiagnosticPeriodDefaultMs};
std::atomic_bool s_diagnostic_period_valid{false};
std::atomic_bool s_auto_rotation_enabled{false};
std::atomic_bool s_leg_flip_enabled{false};
std::atomic_bool s_rotation_retry_blocked{false};
std::atomic_bool s_rotation_failure_latched{false};
std::atomic<int64_t> s_rotation_retry_after_us{0};
int64_t s_last_committed_diagnostic_imu_us;
int64_t s_last_committed_diagnostic_microphone_us;
int64_t s_last_full_consumed_diagnostic_imu_us;
int64_t s_last_full_consumed_diagnostic_microphone_us;
TickType_t s_last_periodic_summary_tick;
std::mutex s_diagnostic_settings_mutex;

struct OrientationState {
    bool observed_enabled = false;
    bool filter_ready = false;
    bool planar_active = false;
    bool gravity_valid = false;
    bool candidate_valid = false;
    float gravity_mps2[3] = {};
    float gyro_physical_degrees = 0.0F;
    int64_t gyro_sampled_at_us = 0;
    uint16_t candidate_physical_degrees = 0;
    int64_t candidate_since_us = 0;
    uint16_t physical_degrees = 0;
    uint16_t desired_rendering_degrees = 0;
    uint16_t applied_rendering_degrees = 0;
    bool persisted_rendering_valid = false;
    uint16_t persisted_rendering_degrees = 0;
    uint32_t change_sequence = 0;
    int64_t sampled_at_us = 0;
    int64_t changed_at_us = 0;
    int64_t last_rotation_error_log_us = 0;
    LUNASHELL::ComponentSnapshot component;
};

OrientationState s_orientation;

// Keep default field values: IDF initializes PSRAM before running C++ constructors.
// Only task-context snapshot owners access this storage; locks and ISR latches stay internal.
alignas(LUNASHELL::DeviceSnapshot) EXT_RAM_BSS_ATTR
uint8_t s_snapshot_storage[sizeof(LUNASHELL::DeviceSnapshot)]{};
LUNASHELL::DeviceSnapshot &s_snapshot = *new (s_snapshot_storage) LUNASHELL::DeviceSnapshot{};
// Full acquisition owns both buffers under s_refresh_mutex, including summary logging.
// Their size follows the canonical schema without consuming the caller's internal stack.
struct FullRefreshScratch {
    LUNASHELL::DeviceSnapshot stage;
    LUNASHELL::DeviceSnapshot summary;
};
alignas(FullRefreshScratch) EXT_RAM_BSS_ATTR
uint8_t s_full_refresh_storage[sizeof(FullRefreshScratch)]{};
FullRefreshScratch &s_full_refresh = *new (s_full_refresh_storage) FullRefreshScratch{};
StaticSemaphore_t s_snapshot_mutex_control;
SemaphoreHandle_t s_snapshot_mutex;
std::atomic<uint32_t> s_touch_activity_sequence{0};
std::atomic<int64_t> s_touch_last_activity_at_us{0};
std::atomic<TaskHandle_t> s_snapshot_stage_owner{nullptr};
std::atomic<LUNASHELL::DeviceSnapshot *> s_snapshot_stage{nullptr};
bool s_snapshot_stage_dirty;
StaticSemaphore_t s_refresh_mutex_control;
SemaphoreHandle_t s_refresh_mutex;

LUNASHELL::DeviceSnapshot *s_diagnostic_stage_snapshot;
// Keep the large diagnostic staging snapshot in PSRAM for the task lifetime.
esp_err_t ensure_diagnostic_stage_snapshot()
{
    if (s_diagnostic_stage_snapshot != nullptr) {
        return ESP_OK;
    }

    s_diagnostic_stage_snapshot =
        static_cast<LUNASHELL::DeviceSnapshot *>(heap_caps_calloc(
            1,
            sizeof(LUNASHELL::DeviceSnapshot),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ));
    if (s_diagnostic_stage_snapshot == nullptr) {
        ESP_LOGE(
            TAG,
            "Diagnostic staging allocation failed: %u PSRAM bytes required",
            static_cast<unsigned>(sizeof(LUNASHELL::DeviceSnapshot))
        );
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

SemaphoreHandle_t ensure_snapshot_mutex()
{
    if (s_snapshot_mutex == nullptr) {
        s_snapshot_mutex =
            xSemaphoreCreateMutexStatic(&s_snapshot_mutex_control);
    }
    return s_snapshot_mutex;
}


template <typename Update>
esp_err_t update_snapshot(
    Update update, SnapshotPublicationPolicy policy = SnapshotPublicationPolicy::Required);


void advance_snapshot_sequence()
{
    ++s_snapshot.sequence;
}

LUNASHELL::DeviceSnapshot *current_task_snapshot_stage()
{
    const TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (current_task == nullptr ||
        s_snapshot_stage_owner.load(std::memory_order_acquire) !=
            current_task) {
        return nullptr;
    }
    return s_snapshot_stage.load(std::memory_order_acquire);
}

// update: Synchronous stage or canonical mutation; never retained after this call.
// policy: Optional logging publishes errors without invoking a fatal synchronization handler.
// Exception: Returns actual optional mutex errors; required publication keeps its invariant.
template <typename Update>
esp_err_t update_snapshot(Update update, SnapshotPublicationPolicy policy)
{
    if (LUNASHELL::DeviceSnapshot *stage = current_task_snapshot_stage();
        stage != nullptr) {
        update(*stage);
        s_snapshot_stage_dirty = true;
        return ESP_OK;
    }

    SemaphoreHandle_t mutex = ensure_snapshot_mutex();
    if (mutex == nullptr) {
        ESP_LOGE(TAG, "DeviceSnapshot update failed: mutex allocation failed");
        if (policy == SnapshotPublicationPolicy::Optional) {
            return ESP_ERR_NO_MEM;
        }
        std::abort();
    }
    const TickType_t wait_ticks = policy == SnapshotPublicationPolicy::Optional
        ? 0 : kSnapshotMutexTimeoutTicks;
    if (xSemaphoreTake(mutex, wait_ticks) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot update failed: mutex take timed out after %lu ms",
            static_cast<unsigned long>(wait_ticks * portTICK_PERIOD_MS)
        );
        if (policy == SnapshotPublicationPolicy::Optional) {
            return ESP_ERR_TIMEOUT;
        }
        std::abort();
    }

    update(s_snapshot);
    advance_snapshot_sequence();
    if (xSemaphoreGive(mutex) != pdTRUE) {
        ESP_LOGE(TAG, "DeviceSnapshot update failed: mutex release rejected");
        if (policy == SnapshotPublicationPolicy::Optional) {
            return ESP_FAIL;
        }
        std::abort();
    }
    return ESP_OK;
}


esp_err_t take_snapshot_mutex(TickType_t wait_ticks, const char *operation)
{
    SemaphoreHandle_t mutex = ensure_snapshot_mutex();
    if (mutex == nullptr) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot %s failed: mutex allocation failed",
            operation
        );
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mutex, wait_ticks) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot %s failed: mutex take timed out",
            operation
        );
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t give_snapshot_mutex(const char *operation)
{
    if (s_snapshot_mutex == nullptr ||
        xSemaphoreGive(s_snapshot_mutex) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot %s failed: mutex release rejected",
            operation
        );
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t begin_snapshot_stage(
    LUNASHELL::DeviceSnapshot &stage,
    TickType_t wait_ticks,
    const char *operation
)
{
    if (s_snapshot_stage_owner.load(std::memory_order_acquire) != nullptr) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot %s failed: another stage is active",
            operation
        );
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t lock_error =
        take_snapshot_mutex(wait_ticks, operation);
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    stage = s_snapshot;
    const esp_err_t release_error = give_snapshot_mutex(operation);
    if (release_error != ESP_OK) {
        return release_error;
    }

    const TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (current_task == nullptr) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot %s failed: current task is unavailable",
            operation
        );
        return ESP_ERR_INVALID_STATE;
    }
    s_snapshot_stage_dirty = false;
    s_snapshot_stage.store(&stage, std::memory_order_release);
    s_snapshot_stage_owner.store(current_task, std::memory_order_release);
    return ESP_OK;
}

bool stage_is_newer(
    const LUNASHELL::ComponentSnapshot &staged,
    const LUNASHELL::ComponentSnapshot &current
)
{
    return staged.last_attempt_at_us > 0 &&
           staged.last_attempt_at_us >= current.last_attempt_at_us;
}

void merge_staged_rtc(
    LUNASHELL::RtcSnapshot &current,
    const LUNASHELL::RtcSnapshot &staged
)
{
    if (stage_is_newer(staged, current)) {
        static_cast<LUNASHELL::ComponentSnapshot &>(current) =
            static_cast<const LUNASHELL::ComponentSnapshot &>(staged);
        current.clock_integrity = staged.clock_integrity;
        current.year = staged.year;
        current.month = staged.month;
        current.day = staged.day;
        current.hour = staged.hour;
        current.minute = staged.minute;
        current.second = staged.second;
    }
    if (stage_is_newer(staged.system_time, current.system_time)) {
        current.system_time = staged.system_time;
        current.system_time_valid = staged.system_time_valid;
        current.synchronized = staged.synchronized;
        strlcpy(current.source, staged.source, sizeof(current.source));
        strlcpy(
            current.timezone,
            staged.timezone,
            sizeof(current.timezone)
        );
    }
}

void merge_staged_microphone(
    LUNASHELL::AudioSnapshot &current,
    const LUNASHELL::AudioSnapshot &staged
)
{
    if (stage_is_newer(staged.microphone, current.microphone)) {
        current.microphone = staged.microphone;
        current.channel_count = staged.channel_count;
        current.sample_rate_hz = staged.sample_rate_hz;
        current.microphone_mean_level = staged.microphone_mean_level;
        current.microphone_peak_level = staged.microphone_peak_level;
        std::memcpy(
            current.channel_mean_level,
            staged.channel_mean_level,
            sizeof(current.channel_mean_level)
        );
        std::memcpy(
            current.channel_peak_level,
            staged.channel_peak_level,
            sizeof(current.channel_peak_level)
        );
    }
    if (stage_is_newer(staged.spectrum, current.spectrum)) {
        current.spectrum = staged.spectrum;
        std::memcpy(
            current.channel_spectrum,
            staged.channel_spectrum,
            sizeof(current.channel_spectrum)
        );
        current.spectrum_sequence = staged.spectrum_sequence;
    }
}

enum class SnapshotStageKind {
    FullRefresh,
    Diagnostic,
};

// current: Canonical snapshot protected by the snapshot mutex.
// staged: Measurements acquired by the current stage owner with original timestamps.
// kind: Acquisition domain that determines which fields this stage may publish.
void merge_snapshot_stage(
    LUNASHELL::DeviceSnapshot &current,
    const LUNASHELL::DeviceSnapshot &staged,
    SnapshotStageKind kind
)
{
    if (kind == SnapshotStageKind::FullRefresh) {
        if (stage_is_newer(staged.battery, current.battery)) {
            current.battery = staged.battery;
        }
        merge_staged_rtc(current.rtc, staged.rtc);
        if (stage_is_newer(staged.gps, current.gps)) {
            current.gps = staged.gps;
        }
        if (stage_is_newer(staged.memory, current.memory)) {
            current.memory = staged.memory;
        }
        if (stage_is_newer(staged.processor, current.processor)) {
            current.processor = staged.processor;
        }
        if (stage_is_newer(staged.storage.logger_capture, current.storage.logger_capture)) {
            current.storage.logger_capture = staged.storage.logger_capture;
        }
    }
    if (stage_is_newer(staged.imu, current.imu)) {
        current.imu = staged.imu;
    }
    if (stage_is_newer(
            staged.ui.orientation,
            current.ui.orientation
        )) {
        current.ui.orientation = staged.ui.orientation;
    }
    if (stage_is_newer(staged.sensor_fusion, current.sensor_fusion)) {
        current.sensor_fusion = staged.sensor_fusion;
    }
    merge_staged_microphone(current.audio, staged.audio);
    if (staged.activity.inferred_at_us > current.activity.inferred_at_us) {
        current.activity = staged.activity;
    }
    current.last_update_attempt_at_us =
        staged.last_update_attempt_at_us;
    current.last_update_complete = staged.last_update_complete;
    current.last_update_error = staged.last_update_error;
    strlcpy(current.reason, staged.reason, sizeof(current.reason));
}

esp_err_t commit_snapshot_stage(
    LUNASHELL::DeviceSnapshot &stage,
    SnapshotStageKind kind,
    TickType_t wait_ticks,
    const char *operation,
    LUNASHELL::DeviceSnapshot *committed_copy
)
{
    const TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (current_task == nullptr ||
        s_snapshot_stage_owner.load(std::memory_order_acquire) !=
            current_task ||
        s_snapshot_stage.load(std::memory_order_acquire) != &stage) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot %s failed: stage ownership mismatch",
            operation
        );
        return ESP_ERR_INVALID_STATE;
    }

    const bool stage_dirty = s_snapshot_stage_dirty;
    s_snapshot_stage_owner.store(nullptr, std::memory_order_release);
    s_snapshot_stage.store(nullptr, std::memory_order_release);
    s_snapshot_stage_dirty = false;
    if (!stage_dirty) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot %s failed: stage contains no updates",
            operation
        );
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_error =
        take_snapshot_mutex(wait_ticks, operation);
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    merge_snapshot_stage(s_snapshot, stage, kind);
    advance_snapshot_sequence();
    if (committed_copy != nullptr) {
        *committed_copy = s_snapshot;
    }
    return give_snapshot_mutex(operation);
}

bool component_has_current_data(const LUNASHELL::ComponentSnapshot &component)
{
    return component.health == LUNASHELL::DeviceHealth::Ready &&
           component.last_error == ESP_OK &&
           component.data_valid &&
           !component.data_stale;
}

bool is_new_successful_sample(
    const LUNASHELL::ComponentSnapshot &component,
    int64_t previous_success_at_us,
    esp_err_t sample_error
)
{
    return sample_error == ESP_OK &&
           component_has_current_data(component) &&
           component.last_attempt_at_us == component.last_success_at_us &&
           component.last_success_at_us > previous_success_at_us;
}

bool can_reuse_diagnostic_sample(
    const LUNASHELL::ComponentSnapshot &component,
    int64_t committed_diagnostic_at_us,
    int64_t consumed_diagnostic_at_us,
    int64_t now_us,
    uint32_t diagnostic_reuse_limit_ms
)
{
    if (diagnostic_reuse_limit_ms == 0U ||
        !component_has_current_data(component) ||
        committed_diagnostic_at_us <= consumed_diagnostic_at_us ||
        component.last_attempt_at_us != component.last_success_at_us ||
        component.last_success_at_us != committed_diagnostic_at_us ||
        now_us < committed_diagnostic_at_us) {
        return false;
    }
    const int64_t reuse_limit_us =
        static_cast<int64_t>(diagnostic_reuse_limit_ms) * 1000LL;
    return now_us - committed_diagnostic_at_us < reuse_limit_us;
}


void notify_hardware_monitor()
{
    TaskHandle_t task = s_diagnostic_monitor_task_handle.load(
        std::memory_order_acquire
    );
    if (task != nullptr) {
        xTaskNotifyGive(task);
    }
}

const char *component_summary_state(
    const LUNASHELL::ComponentSnapshot &component
)
{
    if (component_has_current_data(component)) {
        return "ready";
    }
    if (component.health == LUNASHELL::DeviceHealth::Missing) {
        return "missing";
    }
    if (component.last_error == ESP_ERR_NOT_SUPPORTED) {
        return "unavailable";
    }
    if (component.health == LUNASHELL::DeviceHealth::Unknown &&
        component.last_error == ESP_ERR_INVALID_STATE &&
        !component.data_valid &&
        !component.data_stale &&
        component.last_attempt_at_us == 0 &&
        component.last_success_at_us == 0) {
        return "standby";
    }
    if (component.last_error != ESP_OK ||
        component.health == LUNASHELL::DeviceHealth::Degraded) {
        return "error";
    }
    if (component.data_stale) {
        return "stale";
    }
    return "unavailable";
}

const char *gps_summary_state(const LUNASHELL::GpsSnapshot &gps)
{
    if (component_has_current_data(gps)) {
        return gps.has_fix ? "fix" : "no-fix";
    }
    return component_summary_state(gps);
}

const char *radio_summary_state(const LUNASHELL::ComponentSnapshot &radio)
{
    if (radio.health == LUNASHELL::DeviceHealth::Unknown &&
        radio.last_error == ESP_ERR_INVALID_STATE &&
        !radio.data_valid &&
        !radio.data_stale &&
        radio.last_attempt_at_us == 0 &&
        radio.last_success_at_us == 0) {
        return "off-policy";
    }
    return component_summary_state(radio);
}

esp_err_t publish_update_result(const char *reason, esp_err_t error)
{
    const char *safe_reason =
        reason != nullptr && reason[0] != '\0' ? reason : "manual";
    const int64_t attempted_at_us = esp_timer_get_time();
    return update_snapshot(
        [safe_reason, error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
            snapshot.last_update_attempt_at_us = attempted_at_us;
            snapshot.last_update_complete = error == ESP_OK;
            snapshot.last_update_error = error;
            strlcpy(snapshot.reason, safe_reason, sizeof(snapshot.reason));
        }
    );
}


void publish_diagnostic_period(esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportDiagnosticPeriod(
        s_diagnostic_period_ms.load(std::memory_order_relaxed),
        error
    );
}

// error: Original setting admission/publication result for the IMU owner.
void publish_imu_smoothing(esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportImuSmoothing(
        imu::smoothing_percent(),
        error
    );
}

void publish_diagnostic_monitor(esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportDiagnosticMonitor(
        s_diagnostic_imu_active.load(std::memory_order_relaxed),
        s_diagnostic_microphone_active.load(std::memory_order_relaxed),
        s_diagnostic_microphone_spectrum_active.load(
            std::memory_order_relaxed
        ),
        error
    );
}

esp_err_t ensure_user_settings()
{
    auto &settings = LUNASHELL::UserSettingsService::instance();
    return settings.isInitialized() ? ESP_OK : settings.initialize();
}

esp_err_t load_or_create_uint32_setting(
    LUNASHELL::UserSettingsService &settings,
    const char *key,
    const char *label,
    uint32_t first_boot_default,
    uint32_t minimum,
    uint32_t maximum,
    uint32_t *value
)
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t persisted = 0;
    esp_err_t error = settings.getUInt32(key, &persisted);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(
            TAG,
            "%s: first boot key '%s' absent; persisting explicit default %lu",
            label,
            key,
            static_cast<unsigned long>(first_boot_default)
        );
        error = settings.setUInt32(key, first_boot_default);
        if (error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "%s: persisting first-boot default failed (%s)",
                label,
                esp_err_to_name(error)
            );
            return error;
        }
        *value = first_boot_default;
        return ESP_OK;
    }
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "%s: reading persisted key '%s' failed (%s)",
            label,
            key,
            esp_err_to_name(error)
        );
        return error;
    }
    if (persisted < minimum || persisted > maximum) {
        ESP_LOGE(
            TAG,
            "%s: persisted value %lu outside [%lu, %lu]; explicit reset required",
            label,
            static_cast<unsigned long>(persisted),
            static_cast<unsigned long>(minimum),
            static_cast<unsigned long>(maximum)
        );
        return ESP_ERR_INVALID_ARG;
    }

    *value = persisted;
    return ESP_OK;
}

// Load a boolean setting without inventing a value after first boot.
esp_err_t load_or_create_bool_setting(
    LUNASHELL::UserSettingsService &settings,
    const char *key,
    const char *label,
    bool first_boot_default,
    bool *value
)
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    bool persisted = false;
    esp_err_t error = settings.getBool(key, &persisted);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(
            TAG,
            "%s: first boot key '%s' absent; persisting explicit default %s",
            label,
            key,
            first_boot_default ? "true" : "false"
        );
        error = settings.setBool(key, first_boot_default);
        if (error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "%s: persisting first-boot default failed (%s)",
                label,
                esp_err_to_name(error)
            );
            return error;
        }
        *value = first_boot_default;
        return ESP_OK;
    }
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "%s: reading persisted key '%s' failed (%s)",
            label,
            key,
            esp_err_to_name(error)
        );
        return error;
    }

    *value = persisted;
    return ESP_OK;
}

// Return the inverse cardinal turn that keeps content upright.
uint16_t rendering_rotation_for_physical(uint16_t physical_degrees)
{
    switch (physical_degrees) {
    case 90:
        return 270;
    case 180:
        return 180;
    case 270:
        return 90;
    case 0:
        return 0;
    default:
        ESP_LOGE(
            TAG,
            "Non-cardinal physical orientation rejected: %u",
            static_cast<unsigned>(physical_degrees)
        );
        std::abort();
    }
}

uint16_t leg_rendering_rotation_for_physical(uint16_t physical_degrees)
{
    const unsigned base = rendering_rotation_for_physical(physical_degrees);
    return static_cast<uint16_t>(
        (base + (s_leg_flip_enabled.load(std::memory_order_relaxed)
                     ? 180U : 0U)) % 360U
    );
}

// Keep an integrated gyro angle within one physical revolution.
float normalize_orientation_degrees(float degrees)
{
    while (degrees >= 360.0F) {
        degrees -= 360.0F;
    }
    while (degrees < 0.0F) {
        degrees += 360.0F;
    }
    return degrees;
}

// Measure the shortest signed turn from a cardinal reference to an angle.
float signed_orientation_delta(float angle, uint16_t reference_degrees)
{
    float delta = angle - static_cast<float>(reference_degrees);
    while (delta > 180.0F) {
        delta -= 360.0F;
    }
    while (delta <= -180.0F) {
        delta += 360.0F;
    }
    return delta;
}

// Publish orientation changes without exposing BSP state to consumers.
esp_err_t publish_orientation_state()
{
    return update_snapshot([](LUNASHELL::DeviceSnapshot &snapshot) {
        copy_orientation_snapshot(snapshot.ui.orientation);
    });
}

// Publish one new physical cardinal and its inverse render rotation.
void commit_cardinal_orientation(
    uint16_t physical_degrees,
    int64_t sampled_at_us,
    const char *source
)
{
    s_orientation.candidate_valid = false;
    s_orientation.physical_degrees = physical_degrees % 360U;
    s_orientation.desired_rendering_degrees =
        leg_rendering_rotation_for_physical(s_orientation.physical_degrees);
    ++s_orientation.change_sequence;
    s_orientation.changed_at_us = sampled_at_us;
    ESP_LOGI(
        TAG,
        "Cardinal orientation stable: source=%s physical=%u rendering=%u",
        source,
        static_cast<unsigned>(s_orientation.physical_degrees),
        static_cast<unsigned>(s_orientation.desired_rendering_degrees)
    );
}

// Track flat-table turns from gyro Z and emit only cardinal steps.
// data: Same normalized raw sample already acquired by the IMU owner.
// sampled_at_us: Original attempt timestamp used for gyro continuity.
// gravity_controls_orientation: Whether this sample already admits measured gravity orientation.
void process_gyro_cardinal_sample(
    const ImuRawSample &data,
    int64_t sampled_at_us,
    bool gravity_controls_orientation
)
{
    const int64_t previous_sample_us = s_orientation.gyro_sampled_at_us;
    s_orientation.gyro_sampled_at_us = sampled_at_us;
    if (previous_sample_us == 0 || sampled_at_us <= previous_sample_us) {
        s_orientation.gyro_physical_degrees =
            static_cast<float>(s_orientation.physical_degrees);
        return;
    }

    if (gravity_controls_orientation) {
        s_orientation.gyro_physical_degrees =
            static_cast<float>(s_orientation.physical_degrees);
        return;
    }

    float step_seconds =
        static_cast<float>(sampled_at_us - previous_sample_us) / 1000000.0F;
    step_seconds = std::min(step_seconds, kOrientationGyroMaxStepSeconds);
    const float gyro_z_dps =
        imu::corrected_gyro_z(data);
    if (std::fabs(gyro_z_dps) < kOrientationGyroDeadbandDps) {
        return;
    }

    s_orientation.gyro_physical_degrees = normalize_orientation_degrees(
        s_orientation.gyro_physical_degrees + gyro_z_dps * step_seconds
    );
    const float delta = signed_orientation_delta(
        s_orientation.gyro_physical_degrees,
        s_orientation.physical_degrees
    );
    uint16_t next_physical_degrees = s_orientation.physical_degrees;
    if (delta >= kOrientationGyroSnapDegrees) {
        next_physical_degrees =
            static_cast<uint16_t>((next_physical_degrees + 90U) % 360U);
    } else if (delta <= -kOrientationGyroSnapDegrees) {
        next_physical_degrees =
            static_cast<uint16_t>((next_physical_degrees + 270U) % 360U);
    } else {
        return;
    }

    commit_cardinal_orientation(
        next_physical_degrees,
        sampled_at_us,
        "gyro"
    );
}

// Reset filtering and dwell whenever the persisted runtime preference changes.
bool observe_auto_rotation_state(bool enabled)
{
    if (s_orientation.observed_enabled == enabled) {
        return false;
    }

    s_orientation.observed_enabled = enabled;
    s_orientation.filter_ready = false;
    s_orientation.planar_active = false;
    s_orientation.gravity_valid = false;
    s_orientation.candidate_valid = false;
    s_orientation.candidate_since_us = 0;
    s_orientation.gyro_physical_degrees =
        static_cast<float>(s_orientation.physical_degrees);
    s_orientation.gyro_sampled_at_us = 0;
    const int64_t changed_at_us = esp_timer_get_time();
    if (!enabled) {
        s_orientation.desired_rendering_degrees =
            s_leg_flip_enabled.load(std::memory_order_relaxed) ? 180 : 0;
        s_orientation.sampled_at_us = changed_at_us;
        if (!s_rotation_retry_blocked.load(std::memory_order_acquire) &&
            s_rotation_retry_after_us.load(std::memory_order_acquire) != 0 &&
            s_orientation.desired_rendering_degrees ==
                s_orientation.applied_rendering_degrees) {
            // Cancelling a retryable request is a real reconciliation because
            // the BSP rollback already restored a consistent panel/touch map.
            s_rotation_retry_after_us.store(0, std::memory_order_release);
            s_rotation_failure_latched.store(false, std::memory_order_release);
        }
        if (!s_rotation_failure_latched.load(std::memory_order_acquire)) {
            mark_sample_success(s_orientation.component, changed_at_us);
        }
    }
    return true;
}

// A long standby gap invalidates the filter's time continuity without changing
// either the persisted preference or the already-applied panel orientation.
void reset_auto_rotation_filter_after_power_resume()
{
    s_orientation.filter_ready = false;
    s_orientation.planar_active = false;
    s_orientation.gravity_valid = false;
    s_orientation.candidate_valid = false;
    s_orientation.candidate_since_us = 0;
    s_orientation.gyro_physical_degrees =
        static_cast<float>(s_orientation.physical_degrees);
    s_orientation.gyro_sampled_at_us = 0;
}

// Accept only rotations that the panel and touch transforms implement exactly.
bool is_cardinal_rotation(uint32_t degrees)
{
    return degrees == 0U || degrees == 90U ||
           degrees == 180U || degrees == 270U;
}

// Convert only a previously validated cardinal degree value.
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
// degrees: Previously validated cardinal panel rotation in degrees.
bsp_display_rotation_t bsp_rotation_for_degrees(uint16_t degrees)
{
    switch (degrees) {
    case 0:
        return BSP_DISPLAY_ROTATE_0;
    case 90:
        return BSP_DISPLAY_ROTATE_90;
    case 180:
        return BSP_DISPLAY_ROTATE_180;
    case 270:
        return BSP_DISPLAY_ROTATE_270;
    default:
        ESP_LOGE(
            TAG,
            "Non-cardinal render rotation rejected: %u",
            static_cast<unsigned>(degrees)
        );
        std::abort();
    }
}
#endif

// rendering_degrees: Applied cardinal angle, persisted only when it changes.
// Exception: Returns original storage/readback errors without claiming persistence.
esp_err_t persist_display_rotation(uint16_t rendering_degrees)
{
    if (!is_cardinal_rotation(rendering_degrees)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_orientation.persisted_rendering_valid &&
        s_orientation.persisted_rendering_degrees == rendering_degrees) {
        return ESP_OK;
    }

    auto &settings = LUNASHELL::UserSettingsService::instance();
    const esp_err_t init_error = settings.isInitialized()
        ? ESP_OK
        : settings.initialize();
    if (init_error != ESP_OK) {
        return init_error;
    }
    esp_err_t error =
        settings.setUInt32(kDisplayRotationSetting, rendering_degrees);
    uint32_t readback = UINT32_MAX;
    if (error == ESP_OK) {
        error = settings.getUInt32(kDisplayRotationSetting, &readback);
    }
    if (error == ESP_OK && readback != rendering_degrees) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Display rotation persistence/readback failed (%s)", esp_err_to_name(error));
    }
    if (error == ESP_OK) {
        s_orientation.persisted_rendering_valid = true;
        s_orientation.persisted_rendering_degrees = rendering_degrees;
        ESP_LOGI(
            TAG,
            "Saved cardinal display rotation: %u deg",
            static_cast<unsigned>(rendering_degrees)
        );
    }
    return error;
}

// Commit the desired panel angle while allowing touch activity to defer it.
// No parameters: reconcile the orientation owner's pending panel/touch rotation.
// Exception: Returns original display, persistence or publication failures.
esp_err_t apply_pending_display_rotation()
{
#if !CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    ESP_LOGE(TAG, "Display rotation rejected: display disabled by build profile");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_orientation.desired_rendering_degrees ==
        s_orientation.applied_rendering_degrees) {
        if (!s_rotation_retry_blocked.load(std::memory_order_acquire) &&
            s_rotation_retry_after_us.load(std::memory_order_acquire) != 0) {
            s_rotation_retry_after_us.store(0, std::memory_order_release);
            s_rotation_failure_latched.store(false, std::memory_order_release);
            mark_sample_success(
                s_orientation.component,
                esp_timer_get_time()
            );
            return publish_orientation_state();
        }
        return ESP_OK;
    }

    const esp_err_t result = bsp_display_rotation_set(
        bsp_rotation_for_degrees(s_orientation.desired_rendering_degrees)
    );
    if (result == ESP_OK) {
        s_rotation_retry_blocked.store(false, std::memory_order_release);
        s_rotation_retry_after_us.store(0, std::memory_order_release);
        s_orientation.applied_rendering_degrees =
            s_orientation.desired_rendering_degrees;
        const esp_err_t persist_error = persist_display_rotation(
            s_orientation.applied_rendering_degrees
        );
        if (persist_error != ESP_OK) {
            s_rotation_failure_latched.store(true, std::memory_order_release);
            mark_sample_failure(
                s_orientation.component,
                persist_error,
                esp_timer_get_time()
            );
            const esp_err_t publication_error = publish_orientation_state();
            if (publication_error != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Display rotation persistence failure publication failed (%s)",
                    esp_err_to_name(publication_error)
                );
            }
            ESP_LOGE(
                TAG,
                "Cardinal display rotation persistence failed (%s)",
                esp_err_to_name(persist_error)
            );
            return persist_error;
        }
        s_rotation_failure_latched.store(false, std::memory_order_release);
        mark_sample_success(
            s_orientation.component,
            esp_timer_get_time()
        );
        return publish_orientation_state();
    }

    if (result == BSP_DISPLAY_ERR_ROTATION_INCONSISTENT &&
        !bsp_display_rotation_is_consistent()) {
        s_rotation_retry_blocked.store(true, std::memory_order_release);
        s_rotation_failure_latched.store(true, std::memory_order_release);
        s_rotation_retry_after_us.store(0, std::memory_order_release);
        mark_sample_failure(
            s_orientation.component,
            result,
            esp_timer_get_time()
        );
        const esp_err_t publication_error = publish_orientation_state();
        if (publication_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Terminal display rotation failure could not be published (%s)",
                esp_err_to_name(publication_error)
            );
        }
        ESP_LOGE(
            TAG,
            "Cardinal display rotation retries blocked: panel/touch state is "
            "inconsistent"
        );
        return result;
    }

    const int64_t now = esp_timer_get_time();
    s_rotation_failure_latched.store(true, std::memory_order_release);
    s_rotation_retry_after_us.store(
        now + static_cast<int64_t>(kOrientationApplyRetryBackoffMs) * 1000LL,
        std::memory_order_release
    );
    mark_sample_failure(s_orientation.component, result, now);
    const esp_err_t publication_error = publish_orientation_state();
    if (publication_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Deferred display rotation failure publication failed (%s)",
            esp_err_to_name(publication_error)
        );
    }
    if (now - s_orientation.last_rotation_error_log_us >= 5000000) {
        s_orientation.last_rotation_error_log_us = now;
        ESP_LOGW(
            TAG,
            "Cardinal display rotation pending (%s)",
            esp_err_to_name(result)
        );
    }
    return result;
#endif
}

// arg: Unused task context; the owner controls stream leases and serialized acquisition.
void diagnostic_monitor_task(void *arg)
{
    (void)arg;
    configASSERT(s_diagnostic_stage_snapshot != nullptr);
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
    ESP_LOGI(TAG, "Activity acquisition owner started: core=%d priority=%u",
             xPortGetCoreID(), static_cast<unsigned>(uxTaskPriorityGet(nullptr)));
#endif
    TickType_t last_diagnostic_sample = 0;
    TickType_t last_ui_motion_sample = 0;
    TickType_t last_activity_sample = 0;
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
    int64_t previous_activity_attempt_us = 0;
    int64_t last_activity_timing_log_us = 0;
#endif

    for (;;) {
        const bool ui_sampling_power_suspended =
            s_ui_sampling_power_suspended.load(std::memory_order_relaxed);
        const bool diagnostic_imu_requested =
            s_diagnostic_imu_active.load(std::memory_order_relaxed);
        const bool diagnostic_microphone_requested =
            s_diagnostic_microphone_active.load(std::memory_order_relaxed);
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
        const bool diagnostic_microphone_spectrum_requested =
            s_diagnostic_microphone_spectrum_active.load(
                std::memory_order_relaxed
            );
#endif
        const bool diagnostic_imu_active =
            !ui_sampling_power_suspended && diagnostic_imu_requested;
        const bool diagnostic_microphone_active =
            !ui_sampling_power_suspended &&
            diagnostic_microphone_requested;
        const bool monitor_active =
            diagnostic_imu_active || diagnostic_microphone_active;
        const bool ui_motion_active =
            s_ui_motion_monitor_active.load(std::memory_order_relaxed);
        const bool ui_motion_standby =
            s_ui_motion_monitor_standby.load(std::memory_order_relaxed);
        const bool activity_active =
            LUNASHELL::snapshot_detail::activity::requested() &&
            !ui_sampling_power_suspended && !ui_motion_standby && imu::ready();
        LUNASHELL::snapshot_detail::activity::set_active(activity_active);
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
        if (!activity_active) {
            previous_activity_attempt_us = 0;
        }
#endif
        const bool auto_rotation_requested =
            s_auto_rotation_enabled.load(std::memory_order_relaxed);
        const bool auto_rotation_active =
            auto_rotation_requested && !ui_sampling_power_suspended &&
            !s_rotation_retry_blocked.load(std::memory_order_acquire);
        // Observe the persisted preference even while its sampling is gated.
        // Treating standby as a preference change would reset the requested
        // orientation to zero and lose the user's setting.
        if (observe_auto_rotation_state(auto_rotation_requested)) {
            log_snapshot_publication_failure(
                "Auto-rotation state",
                publish_orientation_state()
            );
        }

        const TickType_t now = xTaskGetTickCount();
        const bool activity_due = activity_active &&
            (last_activity_sample == 0 || now - last_activity_sample >= pdMS_TO_TICKS(LUNASHELL::snapshot_detail::activity::kPeriodMs));
        const TickType_t diagnostic_period = pdMS_TO_TICKS(
            s_diagnostic_period_ms.load(std::memory_order_relaxed)
        );
        const bool diagnostic_due =
            monitor_active &&
            (last_diagnostic_sample == 0 ||
             now - last_diagnostic_sample >= diagnostic_period);
        const uint32_t ui_motion_period_ms = ui_motion_standby
            ? kUiMotionStandbySamplePeriodMs
            : kUiMotionSamplePeriodMs;
        const TickType_t ui_motion_period =
            pdMS_TO_TICKS(ui_motion_period_ms);
        const bool ui_motion_due =
            ui_motion_active &&
            imu::ready() &&
            (last_ui_motion_sample == 0 ||
             now - last_ui_motion_sample >= ui_motion_period);
        const bool orientation_due =
            auto_rotation_active && imu::ready();
        const bool diagnostic_imu_due =
            diagnostic_due && diagnostic_imu_active;
        const bool diagnostic_microphone_due =
            diagnostic_due && diagnostic_microphone_active;
        const bool imu_sample_due =
            diagnostic_imu_due || ui_motion_due || orientation_due || activity_due;
        const bool sampling_due =
            imu_sample_due || diagnostic_microphone_due;
        bool effective_diagnostic_due = diagnostic_due;
        bool effective_diagnostic_imu_due = diagnostic_imu_due;
        bool effective_diagnostic_microphone_due =
            diagnostic_microphone_due;
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
        bool effective_diagnostic_microphone_spectrum_due =
            diagnostic_microphone_due &&
            diagnostic_microphone_spectrum_requested;
#endif
        bool effective_imu_sample_due = imu_sample_due;

        if (sampling_due) {
            TickType_t activity_attempt_tick = now;
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
            const int64_t refresh_wait_started_us = esp_timer_get_time();
            int64_t refresh_wait_us = 0;
            int64_t stage_wait_us = 0;
            int64_t activity_gap_us = 0;
            int64_t imu_read_us = 0;
#endif
            esp_err_t aggregate = ESP_OK;
            bool diagnostic_result_published = false;
            int64_t committed_imu_sample_at_us = 0;
            int64_t committed_microphone_sample_at_us = 0;
            LUNASHELL::DeviceSnapshot &staged_snapshot =
                *s_diagnostic_stage_snapshot;
            SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
            if (refresh_mutex == nullptr) {
                aggregate = ESP_ERR_NO_MEM;
                ESP_LOGE(
                    TAG,
                    "Hardware monitor sample failed: refresh mutex allocation failed"
                );
            } else if (
                xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(250)) != pdTRUE
            ) {
                aggregate = ESP_ERR_TIMEOUT;
                ESP_LOGW(
                    TAG,
                    "Hardware monitor sample rejected: refresh mutex take timed out"
                );
            } else {
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
                refresh_wait_us = esp_timer_get_time() - refresh_wait_started_us;
#endif
                // Stream selection and standby UI sampling can both change
                // while this task waits for the refresh lock. Re-check all of
                // them under the same lock so either transition is a real
                // quiescence barrier.
                const bool effective_ui_sampling_power_suspended =
                    s_ui_sampling_power_suspended.load(
                        std::memory_order_acquire
                    );
                const bool effective_auto_rotation_active =
                    s_auto_rotation_enabled.load(std::memory_order_acquire) &&
                    !effective_ui_sampling_power_suspended &&
                    !s_rotation_retry_blocked.load(std::memory_order_acquire);
                effective_diagnostic_imu_due =
                    !effective_ui_sampling_power_suspended &&
                    diagnostic_due &&
                    s_diagnostic_imu_active.load(std::memory_order_acquire);
                effective_diagnostic_microphone_due =
                    !effective_ui_sampling_power_suspended &&
                    diagnostic_due &&
                    s_diagnostic_microphone_active.load(
                        std::memory_order_acquire
                    );
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
                effective_diagnostic_microphone_spectrum_due =
                    effective_diagnostic_microphone_due &&
                    s_diagnostic_microphone_spectrum_active.load(
                        std::memory_order_acquire
                    );
#endif
                effective_diagnostic_due =
                    effective_diagnostic_imu_due ||
                    effective_diagnostic_microphone_due;
                effective_imu_sample_due =
                    effective_diagnostic_imu_due ||
                    ui_motion_due ||
                    effective_auto_rotation_active ||
                    (activity_due && !effective_ui_sampling_power_suspended &&
                     !s_ui_motion_monitor_standby.load(std::memory_order_acquire));
                const bool effective_sampling_due =
                    effective_imu_sample_due ||
                    effective_diagnostic_microphone_due;
                if (effective_sampling_due) {
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
                    const int64_t stage_wait_started_us = esp_timer_get_time();
#endif
                    const esp_err_t snapshot_lock_error =
                        begin_snapshot_stage(
                            staged_snapshot,
                            pdMS_TO_TICKS(250),
                            "hardware monitor stage"
                        );
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
                    stage_wait_us = esp_timer_get_time() - stage_wait_started_us;
#endif
                    record_first_error(snapshot_lock_error, &aggregate);
                    if (snapshot_lock_error == ESP_OK) {
                        const int64_t previous_imu_success_at_us =
                            staged_snapshot.imu.last_success_at_us;
                        const int64_t previous_microphone_success_at_us =
                            staged_snapshot.audio.microphone
                                .last_success_at_us;
                        esp_err_t imu_sample_error = ESP_OK;
                        esp_err_t microphone_sample_error = ESP_OK;
                        if (effective_imu_sample_due) {
                            const bool reset_orientation_filter =
                                effective_auto_rotation_active &&
                                s_ui_sampling_orientation_reset_pending.load(
                                    std::memory_order_acquire
                                );
                            if (reset_orientation_filter) {
                                reset_auto_rotation_filter_after_power_resume();
                            }
                            const bool effective_activity_due = activity_due &&
                                LUNASHELL::snapshot_detail::activity::requested() &&
                                !effective_ui_sampling_power_suspended &&
                                !s_ui_motion_monitor_standby.load(std::memory_order_acquire);
                            // The lease starts at acquisition, after owner/stage locks.
                            // Using loop entry here would shorten the next real interval
                            // by lock wait time and make the model discard that sample.
                            if (effective_activity_due) {
                                activity_attempt_tick = xTaskGetTickCount();
                            }
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
                            const int64_t imu_read_started_us = esp_timer_get_time();
                            if (effective_activity_due) {
                                activity_gap_us = previous_activity_attempt_us == 0 ? 0 :
                                    imu_read_started_us - previous_activity_attempt_us;
                                previous_activity_attempt_us = imu_read_started_us;
                            }
#endif
                            imu_sample_error = imu::sample(
                                effective_diagnostic_imu_due
                                    ? "diagnostic-ui"
                                    : ui_motion_due
                                        ? "adaptive-ui"
                                        : "auto-rotation",
                                effective_diagnostic_imu_due,
                                effective_auto_rotation_active,
                                effective_activity_due
                            );
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
                            imu_read_us = esp_timer_get_time() - imu_read_started_us;
#endif
                            if (reset_orientation_filter &&
                                imu_sample_error == ESP_OK) {
                                s_ui_sampling_orientation_reset_pending.store(
                                    false,
                                    std::memory_order_release
                                );
                            }
                            record_first_error(
                                imu_sample_error,
                                &aggregate
                            );
                        }
                        if (effective_diagnostic_due) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
                            if (effective_diagnostic_microphone_due) {
                                microphone_sample_error =
                                    audio::sample_microphone(
                                        "diagnostic-ui",
                                        effective_diagnostic_microphone_spectrum_due
                                    );
                                record_first_error(
                                    microphone_sample_error,
                                    &aggregate
                                );
                            }
#endif
                            record_first_error(
                                publish_update_result(
                                    "diagnostic-ui",
                                    aggregate
                                ),
                                &aggregate
                            );
                        }
                        const esp_err_t commit_error =
                            commit_snapshot_stage(
                                staged_snapshot,
                                SnapshotStageKind::Diagnostic,
                                pdMS_TO_TICKS(250),
                                "hardware monitor commit",
                                nullptr
                            );
                        record_first_error(
                            commit_error,
                            &aggregate
                        );
                        diagnostic_result_published =
                            effective_diagnostic_due &&
                            commit_error == ESP_OK;
                        if (commit_error == ESP_OK) {
                            if (effective_diagnostic_imu_due &&
                                is_new_successful_sample(
                                    staged_snapshot.imu,
                                    previous_imu_success_at_us,
                                    imu_sample_error
                                )) {
                                committed_imu_sample_at_us =
                                    staged_snapshot.imu.last_success_at_us;
                            }
                            if (effective_diagnostic_microphone_due &&
                                is_new_successful_sample(
                                    staged_snapshot.audio.microphone,
                                    previous_microphone_success_at_us,
                                    microphone_sample_error
                                )) {
                                committed_microphone_sample_at_us =
                                    staged_snapshot.audio.microphone
                                        .last_success_at_us;
                            }
                        }
                    }
                }
                if (committed_imu_sample_at_us > 0) {
                    s_last_committed_diagnostic_imu_us =
                        committed_imu_sample_at_us;
                }
                if (committed_microphone_sample_at_us > 0) {
                    s_last_committed_diagnostic_microphone_us =
                        committed_microphone_sample_at_us;
                }
                record_first_error(
                    give_refresh_mutex(
                        refresh_mutex,
                        "Hardware monitor sample"
                    ),
                    &aggregate
                );
            }

#if CONFIG_LUNASHELL_ACTIVITY_MODEL
            // Report only delayed scheduled acquisitions, outside the refresh
            // barrier and at most once a second; these are measured costs, not
            // sensor replacements or synthetic timestamps.
            const int64_t timing_log_at_us = esp_timer_get_time();
            if (activity_gap_us > 75000 && timing_log_at_us - last_activity_timing_log_us >= 1000000) {
                last_activity_timing_log_us = timing_log_at_us;
                ESP_LOGW(TAG, "Activity timing core=%d gap=%lld refresh_wait=%lld stage_wait=%lld imu_call=%lld us",
                         xPortGetCoreID(), activity_gap_us, refresh_wait_us, stage_wait_us, imu_read_us);
            }
#endif
            if (effective_diagnostic_due) {
                last_diagnostic_sample = now;
                if (!diagnostic_result_published) {
                    record_first_error(
                        publish_update_result(
                            "diagnostic-ui",
                            aggregate
                        ),
                        &aggregate
                    );
                }
                if (aggregate != ESP_OK) {
                    ESP_LOGW(
                        TAG,
                        "Diagnostic sample failed (%s)",
                        esp_err_to_name(aggregate)
                    );
                }
                if (s_diagnostic_stack_report_pending.exchange(
                        false,
                        std::memory_order_relaxed
                    )) {
                    ESP_LOGI(
                        TAG,
                        "Hardware monitor stack high-water mark: %lu bytes",
                        static_cast<unsigned long>(
                            uxTaskGetStackHighWaterMark(nullptr) *
                            sizeof(StackType_t)
                        )
                    );
                }
            }
            if (ui_motion_due) {
                last_ui_motion_sample = now;
            }
            if (activity_due) {
                last_activity_sample = activity_attempt_tick;
            }
        } else {
            if (!monitor_active) {
                last_diagnostic_sample = 0;
            }
            if (!ui_motion_active) {
                last_ui_motion_sample = 0;
            }
        }

        const int64_t rotation_attempt_check_us = esp_timer_get_time();
        const int64_t rotation_retry_after_us =
            s_rotation_retry_after_us.load(std::memory_order_acquire);
        const bool rotation_retry_due =
            rotation_retry_after_us == 0 ||
            rotation_attempt_check_us >= rotation_retry_after_us;
        if (rotation_retry_due &&
            !s_ui_sampling_power_suspended.load(std::memory_order_acquire) &&
            !s_ui_sampling_orientation_reset_pending.load(
                std::memory_order_acquire
            ) &&
            !s_rotation_retry_blocked.load(std::memory_order_acquire) &&
            s_orientation.desired_rendering_degrees !=
                s_orientation.applied_rendering_degrees) {
            SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
            if (refresh_mutex != nullptr &&
                xSemaphoreTake(
                    refresh_mutex,
                    pdMS_TO_TICKS(250)
                ) == pdTRUE) {
                const int64_t locked_retry_after_us =
                    s_rotation_retry_after_us.load(std::memory_order_acquire);
                if ((locked_retry_after_us == 0 ||
                     esp_timer_get_time() >= locked_retry_after_us) &&
                    !s_ui_sampling_power_suspended.load(
                        std::memory_order_acquire
                    ) &&
                    !s_ui_sampling_orientation_reset_pending.load(
                        std::memory_order_acquire
                    ) &&
                    !s_rotation_retry_blocked.load(
                        std::memory_order_acquire
                    ) &&
                    s_orientation.desired_rendering_degrees !=
                        s_orientation.applied_rendering_degrees) {
                    (void)apply_pending_display_rotation();
                }
                (void)give_refresh_mutex(
                    refresh_mutex,
                    "Cardinal display rotation"
                );
            }
        }

        const int64_t rotation_wait_started_us = esp_timer_get_time();
        const int64_t pending_retry_after_us =
            s_rotation_retry_after_us.load(std::memory_order_acquire);
        const bool rotation_apply_pending =
            !s_ui_sampling_power_suspended.load(std::memory_order_acquire) &&
            !s_ui_sampling_orientation_reset_pending.load(
                std::memory_order_acquire
            ) &&
            !s_rotation_retry_blocked.load(std::memory_order_acquire) &&
            s_orientation.desired_rendering_degrees !=
                s_orientation.applied_rendering_degrees;
        uint32_t rotation_wait_ms = kOrientationSamplePeriodMs;
        if (rotation_apply_pending && !auto_rotation_active &&
            pending_retry_after_us > rotation_wait_started_us) {
            const int64_t remaining_us =
                pending_retry_after_us - rotation_wait_started_us;
            rotation_wait_ms = static_cast<uint32_t>(
                (remaining_us + 999LL) / 1000LL
            );
        }
        const bool wait_indefinitely =
            !auto_rotation_active &&
            !rotation_apply_pending &&
            !ui_motion_active &&
            !monitor_active && !activity_active;
        TickType_t wait_ticks = wait_indefinitely
            ? portMAX_DELAY
            : pdMS_TO_TICKS(
                auto_rotation_active || rotation_apply_pending
                    ? rotation_wait_ms
                    : ui_motion_active
                        ? ui_motion_period_ms
                        : s_diagnostic_period_ms.load(
                            std::memory_order_relaxed
                        )
            );
        if (!wait_indefinitely && wait_ticks == 0) {
            wait_ticks = 1;
        }
        if (activity_active) {
            // Wake at the remaining owner deadline, not another whole period
            // after acquisition. A faster UI wake must not push the 50 ms lease
            // to its next UI tick; acquisition/commit time also counts as elapsed.
            const TickType_t activity_period =
                pdMS_TO_TICKS(LUNASHELL::snapshot_detail::activity::kPeriodMs);
            const TickType_t elapsed = xTaskGetTickCount() - last_activity_sample;
            const TickType_t remaining = last_activity_sample == 0 || elapsed >= activity_period
                ? 0 : activity_period - elapsed;
            if (wait_ticks > remaining) {
                wait_ticks = remaining;
            }
        }
        // Configuration setters notify this task, so an idle monitor has no
        // polling wakeup and any newly requested consumer starts immediately.
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks);
    }
}

void log_summary(const LUNASHELL::DeviceSnapshot &snapshot)
{
    ESP_LOGI(
        TAG,
        "Snapshot #%lu (%s): PMU=%s RTC=%s IMU=%s TOF1=%s TOF2=%s DIST=%s GPS=%s MIC=%s SPK=%s SD=%s DISPLAY=%s TOUCH=%s WIFI=%s BLE=%s MEM=%s CPU=%s",
        static_cast<unsigned long>(snapshot.sequence),
        snapshot.reason,
        component_summary_state(snapshot.battery),
        component_summary_state(snapshot.rtc),
        component_summary_state(snapshot.imu),
        component_summary_state(snapshot.distance.sensors[0]),
        component_summary_state(snapshot.distance.sensors[1]),
        component_summary_state(snapshot.distance),
        gps_summary_state(snapshot.gps),
        component_summary_state(snapshot.audio.microphone),
        component_summary_state(snapshot.audio.speaker),
        component_summary_state(snapshot.storage),
        component_summary_state(snapshot.ui.display),
        component_summary_state(snapshot.ui.touch),
        radio_summary_state(snapshot.radios.wifi),
        radio_summary_state(snapshot.radios.ble),
        component_summary_state(snapshot.memory),
        component_summary_state(snapshot.processor)
    );
    if (component_has_current_data(snapshot.memory)) {
        ESP_LOGI(
            TAG,
            "Flash: firmware=%luB partition=%luB capacity=%luB",
            static_cast<unsigned long>(
                snapshot.memory.firmware_image_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.app_partition_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.flash_capacity_bytes
            )
        );
        ESP_LOGI(
            TAG,
            "Memory: heap free=%luB total=%luB min=%luB allocation-failures=%lu largest-failed=%luB caps=0x%08lx",
            static_cast<unsigned long>(snapshot.memory.heap_free_bytes),
            static_cast<unsigned long>(snapshot.memory.heap_total_bytes),
            static_cast<unsigned long>(
                snapshot.memory.heap_minimum_free_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.allocation_failure_count
            ),
            static_cast<unsigned long>(
                snapshot.memory.largest_failed_allocation_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.failed_allocation_caps_seen_mask
            )
        );
        ESP_LOGI(
            TAG,
            "Internal RAM: free=%luB total=%luB min=%luB largest=%luB blocks=%lu/%lu pressure=%s",
            static_cast<unsigned long>(snapshot.memory.internal_free_bytes),
            static_cast<unsigned long>(
                snapshot.memory.internal_heap_total_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.internal_minimum_free_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.internal_largest_block_bytes
            ),
            static_cast<unsigned long>(snapshot.memory.internal_free_blocks),
            static_cast<unsigned long>(snapshot.memory.internal_total_blocks),
            snapshot.memory.internal_memory_pressure ? "high" : "normal"
        );
        ESP_LOGI(
            TAG,
            "PSRAM: used=%luB free=%luB total=%luB physical=%luB min=%luB largest=%luB blocks=%lu/%lu",
            static_cast<unsigned long>(snapshot.memory.psram_used_bytes),
            static_cast<unsigned long>(snapshot.memory.psram_free_bytes),
            static_cast<unsigned long>(
                snapshot.memory.psram_heap_total_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.psram_capacity_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.psram_minimum_free_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.psram_largest_block_bytes
            ),
            static_cast<unsigned long>(
                snapshot.memory.psram_free_blocks
            ),
            static_cast<unsigned long>(
                snapshot.memory.psram_total_blocks
            )
        );
    }
    if (component_has_current_data(snapshot.processor)) {
        ESP_LOGI(
            TAG,
            "Processor: cores=%u sampled-on=%u",
            snapshot.processor.core_count,
            snapshot.processor.sampling_core
        );
        const uint8_t cores_to_log =
            snapshot.processor.core_count < 2
                ? snapshot.processor.core_count
                : 2;
        for (uint8_t core = 0; core < cores_to_log; ++core) {
            if (!snapshot.processor.cores[core].available) {
                continue;
            }
            if (snapshot.processor.cores[core].utilization_available) {
                ESP_LOGI(
                    TAG,
                    "Core %u: utilization=%u%%",
                    core,
                    snapshot.processor.cores[core].utilization_percent
                );
            } else {
                ESP_LOGI(TAG, "Core %u: utilization=measuring", core);
            }
        }
    }
    if (component_has_current_data(snapshot.rtc)) {
        ESP_LOGI(
            TAG,
            "RTC: UTC=%04u-%02u-%02u %02u:%02u:%02u",
            snapshot.rtc.year,
            snapshot.rtc.month,
            snapshot.rtc.day,
            snapshot.rtc.hour,
            snapshot.rtc.minute,
            snapshot.rtc.second
        );
    }
    if (component_has_current_data(snapshot.rtc.system_time)) {
        ESP_LOGI(
            TAG,
            "System time: source=%s synchronized=%s timezone=%s",
            snapshot.rtc.source,
            snapshot.rtc.synchronized ? "yes" : "no",
            snapshot.rtc.timezone
        );
    }
    if (component_has_current_data(snapshot.gps)) {
        if (snapshot.gps.has_fix) {
            ESP_LOGI(
                TAG,
                "GPS: fix=yes satellites=%u",
                static_cast<unsigned>(snapshot.gps.satellites)
            );
        } else {
            ESP_LOGI(TAG, "GPS: fix=no");
        }
    }
    if (component_has_current_data(snapshot.input.boot_button)) {
        ESP_LOGI(
            TAG,
            "BOOT input: state=%s presses=%lu",
            snapshot.input.boot_pressed ? "pressed" : "released",
            static_cast<unsigned long>(snapshot.input.boot_press_count)
        );
    }
    if (component_has_current_data(snapshot.input.power_button)) {
        ESP_LOGI(
            TAG,
            "Power input: state=%s short=%lu long=%lu",
            snapshot.input.power_pressed ? "pressed" : "released",
            static_cast<unsigned long>(
                snapshot.input.power_short_press_count
            ),
            static_cast<unsigned long>(
                snapshot.input.power_long_press_count
            )
        );
    }
    if (component_has_current_data(snapshot.imu.calibration)) {
        ESP_LOGI(
            TAG,
            "IMU calibration: calibrated=%s stationary=%s samples=%u",
            snapshot.imu.calibrated ? "yes" : "no",
            snapshot.imu.calibration_stationary ? "yes" : "no",
            snapshot.imu.calibration_samples
        );
    }
    if (component_has_current_data(snapshot.storage)) {
        ESP_LOGI(
            TAG,
            "SD: mounted=%s card-response=%s outcome=%u attempts=%lu format-attempted=%s",
            snapshot.storage.mounted ? "yes" : "no",
            snapshot.storage.card_response_verified
                ? "verified"
                : "unconfirmed",
            static_cast<unsigned>(snapshot.storage.mount_outcome),
            static_cast<unsigned long>(
                snapshot.storage.mount_attempt_count
            ),
            snapshot.storage.format_attempted ? "yes" : "no"
        );
    }
}

}  // namespace

namespace LUNASHELL::snapshot_detail {
// No parameters: create the owner-held static refresh barrier once.
// Exception: Returns null if FreeRTOS cannot admit the mutex.
SemaphoreHandle_t ensure_refresh_mutex()
{
    if (s_refresh_mutex == nullptr) {
        s_refresh_mutex = xSemaphoreCreateMutexStatic(&s_refresh_mutex_control);
    }
    return s_refresh_mutex;
}

// mutex: Refresh barrier currently held by the acquisition task.
// operation: Identity used when ownership cannot be released.
// Exception: A rejected release aborts because acquisition ownership is uncertain.
esp_err_t give_refresh_mutex(
    SemaphoreHandle_t mutex,
    const char *operation
)
{
    if (mutex == nullptr || xSemaphoreGive(mutex) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "%s failed: refresh mutex release rejected",
            operation
        );
        std::abort();
    }
    return ESP_OK;
}

// error: Original operation result.
// aggregate: Borrowed caller result; preserve its first failure.
void record_first_error(esp_err_t error, esp_err_t *aggregate)
{
    if (aggregate != nullptr && *aggregate == ESP_OK && error != ESP_OK) {
        *aggregate = error;
    }
}

// component: Hardware owner whose canonical publication was attempted.
// phase: Original acquisition identity.
// sample_error: Original operation failure, which takes precedence.
// publication_error: Result of publishing the same observation.
esp_err_t sample_publication_result(
    const char *component,
    const char *phase,
    esp_err_t sample_error,
    esp_err_t publication_error
)
{
    if (publication_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "%s: %s snapshot publication failed (%s)",
            component,
            phase != nullptr ? phase : "unknown",
            esp_err_to_name(publication_error)
        );
    }
    return sample_error != ESP_OK ? sample_error : publication_error;
}

// phase: Existing acquisition cadence identity selecting quiet diagnostic output.
bool high_frequency_sample_phase(const char *phase)
{
    return phase != nullptr &&
           (std::strcmp(phase, "diagnostic-ui") == 0 ||
            std::strcmp(phase, "periodic-full") == 0);
}

// snapshot: Bounded destination borrowing the existing orientation owner state.
void copy_orientation_snapshot(LUNASHELL::OrientationSnapshot &snapshot)
{
    static_cast<LUNASHELL::ComponentSnapshot &>(snapshot) =
        s_orientation.component;
    snapshot.auto_rotation_enabled =
        s_auto_rotation_enabled.load(std::memory_order_relaxed);
    snapshot.gravity_valid = s_orientation.gravity_valid;
    snapshot.rotation_pending =
        s_orientation.desired_rendering_degrees !=
        s_orientation.applied_rendering_degrees;
    snapshot.physical_orientation_degrees =
        s_orientation.physical_degrees;
    snapshot.rendering_rotation_degrees =
        s_orientation.applied_rendering_degrees;
    snapshot.change_sequence = s_orientation.change_sequence;
    snapshot.sampled_at_us = s_orientation.sampled_at_us;
    snapshot.changed_at_us = s_orientation.changed_at_us;
}

// data: Same normalized raw IMU acquisition; no hardware is read here.
// sampled_at_us: Original hardware attempt timestamp.
void process_orientation_sample(
    const ImuRawSample &data,
    int64_t sampled_at_us
)
{
    const float raw[3] = {data.accelX, data.accelY, data.accelZ};
    const float magnitude = std::sqrt(
        raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]
    );
    s_orientation.sampled_at_us = sampled_at_us;
    if (!s_rotation_retry_blocked.load(std::memory_order_acquire) &&
        s_rotation_retry_after_us.load(std::memory_order_acquire) != 0 &&
        s_orientation.desired_rendering_degrees ==
            s_orientation.applied_rendering_degrees) {
        s_rotation_retry_after_us.store(0, std::memory_order_release);
        s_rotation_failure_latched.store(false, std::memory_order_release);
    }
    // A valid IMU sample does not prove that a failed panel/touch rotation or
    // its persistence succeeded. Retain that structured failure until the
    // requested rotation is reconciled or a later transaction commits.
    if (!s_rotation_failure_latched.load(std::memory_order_acquire)) {
        mark_sample_success(s_orientation.component, sampled_at_us);
    }

    if (magnitude < kOrientationMagnitudeMin ||
        magnitude > kOrientationMagnitudeMax) {
        s_orientation.gravity_valid = false;
        s_orientation.candidate_valid = false;
        process_gyro_cardinal_sample(data, sampled_at_us, false);
        return;
    }

    if (!s_orientation.filter_ready) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            s_orientation.gravity_mps2[axis] = raw[axis];
        }
        s_orientation.filter_ready = true;
    } else {
        for (unsigned axis = 0; axis < 3; ++axis) {
            s_orientation.gravity_mps2[axis] =
                s_orientation.gravity_mps2[axis] *
                    (1.0F - kOrientationFilterAlpha) +
                raw[axis] * kOrientationFilterAlpha;
        }
    }

    const float accel_x = s_orientation.gravity_mps2[0];
    const float accel_y = s_orientation.gravity_mps2[1];
    const float planar = std::sqrt(accel_x * accel_x + accel_y * accel_y);
    if (s_orientation.planar_active) {
        if (planar < kOrientationPlanarExit) {
            s_orientation.planar_active = false;
        }
    } else if (planar >= kOrientationPlanarEnter) {
        s_orientation.planar_active = true;
    }

    if (!s_orientation.planar_active) {
        s_orientation.gravity_valid = false;
        s_orientation.candidate_valid = false;
        process_gyro_cardinal_sample(data, sampled_at_us, false);
        return;
    }

    const float abs_x = std::fabs(accel_x);
    const float abs_y = std::fabs(accel_y);
    const float major = abs_x >= abs_y ? abs_x : abs_y;
    const float minor = abs_x >= abs_y ? abs_y : abs_x;
    if (major < minor * kOrientationDominance) {
        s_orientation.gravity_valid = false;
        s_orientation.candidate_valid = false;
        process_gyro_cardinal_sample(data, sampled_at_us, false);
        return;
    }

    uint16_t physical_degrees = 0;
    if (abs_x >= abs_y) {
        physical_degrees = accel_x >= 0.0F ? 0 : 180;
    } else {
        physical_degrees = accel_y < 0.0F ? 90 : 270;
    }
    s_orientation.gravity_valid = true;
    process_gyro_cardinal_sample(data, sampled_at_us, true);

    if (physical_degrees == s_orientation.physical_degrees &&
        s_orientation.desired_rendering_degrees ==
            leg_rendering_rotation_for_physical(physical_degrees)) {
        s_orientation.candidate_valid = false;
        return;
    }

    if (!s_orientation.candidate_valid ||
        s_orientation.candidate_physical_degrees != physical_degrees) {
        s_orientation.candidate_valid = true;
        s_orientation.candidate_physical_degrees = physical_degrees;
        s_orientation.candidate_since_us = sampled_at_us;
        return;
    }

    if (sampled_at_us - s_orientation.candidate_since_us <
        kOrientationStableUs) {
        return;
    }

    commit_cardinal_orientation(
        physical_degrees,
        sampled_at_us,
        "gravity"
    );
    s_orientation.gyro_physical_degrees =
        static_cast<float>(physical_degrees);
}

// No parameters: observe completed acquisition initialization.
bool hardware_initialized()
{
    return s_initialized;
}

// snapshot: Borrowed canonical/staged destination for the same failed IMU observation.
// error: Original acquisition failure.
// attempted_at_us: Original hardware attempt timestamp.
void invalidate_orientation_sample(DeviceSnapshot &snapshot, esp_err_t error,
                                   int64_t attempted_at_us)
{
    s_orientation.gravity_valid = false;
    s_orientation.candidate_valid = false;
    s_orientation.sampled_at_us = attempted_at_us;
    mark_sample_failure(s_orientation.component, error, attempted_at_us);
    copy_orientation_snapshot(snapshot.ui.orientation);
}

// mutation: Synchronous update executed by the canonical snapshot owner.
// context: Borrowed caller state; never stored beyond this call.
// policy: Explicit required/optional publication selected by the component owner.
// Exception: Returns actual callback or optional mutex failures.
esp_err_t apply_snapshot_mutation(
    SnapshotMutation mutation, void *context, SnapshotPublicationPolicy policy)
{
    if (mutation == nullptr || context == nullptr) {
        ESP_LOGE(TAG, "Snapshot mutation rejected: callback=%s context=%p (%s)",
                 mutation != nullptr ? "present" : "null", context, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    // Keep staging and lock ownership here. Reporters never acquire their own sensor cache.
    return ::update_snapshot(
        [=](DeviceSnapshot &snapshot) { mutation(snapshot, context); }, policy);
}
}  // namespace LUNASHELL::snapshot_detail

namespace LUNASHELL {

DeviceSnapshotService &DeviceSnapshotService::instance()
{
    static DeviceSnapshotService service;
    return service;
}

// No parameters: initialize owners/drivers only; orchestrators explicitly acquire the first snapshot.
// Exception: Returns original critical-stage or recorded optional-component failures.
esp_err_t DeviceSnapshotService::initialize()
{
    const esp_err_t telemetry_error = LUNASHELL_memory_telemetry_init();
    if (telemetry_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Allocation-failure monitor initialization failed (%s)",
            esp_err_to_name(telemetry_error)
        );
        return telemetry_error;
    }
    if (s_initialization_attempted) {
        return s_initialization_result;
    }
    if (ensure_snapshot_mutex() == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (ensure_refresh_mutex() == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t pmu_synchronization_error = acquisition_pmu::initialize_synchronization();
    if (pmu_synchronization_error != ESP_OK) {
        return pmu_synchronization_error;
    }
    auto &settings = UserSettingsService::instance();
    const esp_err_t settings_error = settings.initialize();
    if (settings_error != ESP_OK) {
        publish_diagnostic_period(settings_error);
        publish_imu_smoothing(settings_error);
        ESP_LOGE(
            TAG,
            "Diagnostic settings store initialization failed (%s)",
            esp_err_to_name(settings_error)
        );
        return settings_error;
    }

    uint32_t saved_period_ms = 0;
    esp_err_t error = load_or_create_uint32_setting(
        settings,
        kDiagnosticPeriodSetting,
        "Diagnostic sample period",
        kDiagnosticPeriodDefaultMs,
        kDiagnosticPeriodMinMs,
        kDiagnosticPeriodMaxMs,
        &saved_period_ms
    );
    if (error != ESP_OK) {
        s_diagnostic_period_valid.store(false, std::memory_order_relaxed);
        publish_diagnostic_period(error);
        return error;
    }
    s_diagnostic_period_ms.store(saved_period_ms, std::memory_order_relaxed);
    s_diagnostic_period_valid.store(true, std::memory_order_relaxed);
    publish_diagnostic_period(ESP_OK);

    uint32_t saved_smoothing = 0;
    error = load_or_create_uint32_setting(
        settings,
        kImuSmoothingSetting,
        "IMU input smoothing",
        kImuSmoothingDefaultPercent,
        0,
        kImuSmoothingMaxPercent,
        &saved_smoothing
    );
    if (error != ESP_OK) {
        imu::set_smoothing(imu::smoothing_percent(), false);
        publish_imu_smoothing(error);
        return error;
    }
    imu::set_smoothing(saved_smoothing, true);
    publish_imu_smoothing(ESP_OK);

    i2c_master_bus_handle_t bus = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS || \
    CONFIG_LUNASHELL_BUILD_ENABLE_RTC || \
    CONFIG_LUNASHELL_BUILD_ENABLE_IMU || \
    CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
    error = bsp_i2c_get_handle(&bus);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Shared BSP I2C initialization failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    if (bus == nullptr) {
        ESP_LOGE(TAG, "Shared BSP I2C returned a null handle");
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "Shared BSP I2C: SDA=%d SCL=%d", BSP_I2C_SDA, BSP_I2C_SCL);
#else
    ESP_LOGI(TAG, "Shared BSP I2C: not required by build profile");
#endif

    s_initialization_attempted = true;
    esp_err_t aggregate = ESP_OK;
    const auto record_optional_chip = [&aggregate](
                                          const char *chip,
                                          esp_err_t chip_error
                                      ) {
        if (chip_error == ESP_OK || chip_error == ESP_ERR_NOT_SUPPORTED) {
            return;
        }
        ESP_LOGW(
            TAG,
            "%s remains unavailable; continuing with the remaining hardware (%s)",
            chip,
            esp_err_to_name(chip_error)
        );
        record_first_error(chip_error, &aggregate);
    };

    error = acquisition_pmu::initialize(bus);
    record_optional_chip("PMU", error);

    error = rtc::initialize(bus);
    record_optional_chip("RTC", error);

    error = imu::initialize(bus);
    record_optional_chip("IMU", error);

    error = gnss::initialize(bus);
    record_optional_chip("GNSS", error);

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    error = LUNASHELL::distanceServiceInit();
    record_optional_chip("dual TOF400F distance", error);
#else
    update_snapshot([](DeviceSnapshot &snapshot) {
        set_component(snapshot.distance, false, ESP_ERR_NOT_SUPPORTED);
        set_component(
            snapshot.distance.settings,
            false,
            ESP_ERR_NOT_SUPPORTED
        );
        set_component(
            snapshot.distance.calibration,
            false,
            ESP_ERR_NOT_SUPPORTED
        );
        for (TofSensorSnapshot &sensor : snapshot.distance.sensors) {
            set_component(sensor, false, ESP_ERR_NOT_SUPPORTED);
        }
    });
    ESP_LOGI(TAG, "Dual TOF400F distance: disabled by build profile");
#endif

    error = audio::initialize();
    record_optional_chip("audio codecs", error);
    if (error == ESP_OK && audio::microphone_ready()) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
        error = audio::sample_microphone("initial", false);
        record_optional_chip("microphones", error);
#endif
    }

    s_initialized = true;
    error = LUNASHELL_hardware_set_diagnostic_streams(false, false, false);
    if (error != ESP_OK) {
        s_initialization_result = error;
        return error;
    }
    // An unset RTC is a failed clock observation, not a failed chip startup.
    // Keep acquisition separate so the orchestrator can reach the first NTP sync
    // with unavailable time, without converting a sample error into ESP_OK here.
    s_initialization_result = aggregate;
    if (aggregate != ESP_OK) {
        return aggregate;
    }
    ESP_LOGI(
        TAG,
        "Selected hardware owners initialized; initial snapshot is an explicit startup stage"
    );
    return ESP_OK;
}

// reason: Caller-owned diagnostic label for this serialized acquisition.
// diagnostic_reuse_limit_ms: Bounded original-sample reuse window; zero disables reuse.
// Exception: Returns acquisition/commit errors; optional logger faults remain in their component.
esp_err_t DeviceSnapshotService::refresh(
    const char *reason,
    uint32_t diagnostic_reuse_limit_ms
)
{
    if (!s_initialized) {
        const esp_err_t publish_error =
            publish_update_result(reason, ESP_ERR_INVALID_STATE);
        if (publish_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Hardware refresh rejection could not be published (%s)",
                esp_err_to_name(publish_error)
            );
        }
        ESP_LOGE(TAG, "Hardware refresh rejected: service is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        const esp_err_t publish_error =
            publish_update_result(reason, ESP_ERR_NO_MEM);
        if (publish_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Hardware refresh allocation failure could not be published (%s)",
                esp_err_to_name(publish_error)
            );
        }
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Hardware refresh rejected: another refresh is still running");
        const esp_err_t publish_error =
            publish_update_result(reason, ESP_ERR_TIMEOUT);
        if (publish_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Hardware refresh timeout could not be published (%s)",
                esp_err_to_name(publish_error)
            );
        }
        return ESP_ERR_TIMEOUT;
    }
    const char *phase = reason != nullptr && reason[0] != '\0' ? reason : "manual";
    esp_err_t aggregate = ESP_OK;
    DeviceSnapshot &staged_snapshot = s_full_refresh.stage;
    DeviceSnapshot &summary = s_full_refresh.summary;
    bool summary_available = false;
    int64_t reused_diagnostic_imu_at_us = 0;
    int64_t reused_diagnostic_microphone_at_us = 0;
    RtcLatchObservation rtc_latch_observation;
    const bool diagnostic_reuse_allowed =
        diagnostic_reuse_limit_ms > 0U &&
        std::strcmp(phase, "periodic-full") == 0;

    const esp_err_t snapshot_lock_error = begin_snapshot_stage(
        staged_snapshot,
        pdMS_TO_TICKS(5000),
        "refresh stage"
    );
    record_first_error(snapshot_lock_error, &aggregate);
    if (snapshot_lock_error == ESP_OK) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
        if (acquisition_pmu::ready()) {
            record_first_error(acquisition_pmu::sample(phase), &aggregate);
        }
#endif
        if (rtc::ready()) {
            record_first_error(
                rtc::sample(phase, &rtc_latch_observation),
                &aggregate
            );
        }
        if (imu::ready()) {
            const int64_t committed_diagnostic_imu_at_us =
                s_last_committed_diagnostic_imu_us;
            const int64_t consumed_diagnostic_imu_at_us =
                s_last_full_consumed_diagnostic_imu_us;
            if (diagnostic_reuse_allowed &&
                s_diagnostic_imu_active.load(std::memory_order_acquire) &&
                can_reuse_diagnostic_sample(
                    staged_snapshot.imu,
                    committed_diagnostic_imu_at_us,
                    consumed_diagnostic_imu_at_us,
                    esp_timer_get_time(),
                    diagnostic_reuse_limit_ms
                )) {
                reused_diagnostic_imu_at_us =
                    committed_diagnostic_imu_at_us;
            } else {
                record_first_error(imu::sample(phase), &aggregate);
            }
        }
#if CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
        if (gnss::ready()) {
            record_first_error(gnss::sample(phase), &aggregate);
        }
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
        if (audio::microphone_ready()) {
            const int64_t committed_diagnostic_microphone_at_us =
                s_last_committed_diagnostic_microphone_us;
            const int64_t consumed_diagnostic_microphone_at_us =
                s_last_full_consumed_diagnostic_microphone_us;
            if (diagnostic_reuse_allowed &&
                s_diagnostic_microphone_active.load(
                    std::memory_order_acquire
                ) &&
                can_reuse_diagnostic_sample(
                    staged_snapshot.audio.microphone,
                    committed_diagnostic_microphone_at_us,
                    consumed_diagnostic_microphone_at_us,
                    esp_timer_get_time(),
                    diagnostic_reuse_limit_ms
                )) {
                reused_diagnostic_microphone_at_us =
                    committed_diagnostic_microphone_at_us;
            } else {
                record_first_error(
                    audio::sample_microphone(phase, false),
                    &aggregate
                );
            }
        }
#endif
        record_first_error(system_acquisition::sample(), &aggregate);
        // Logging is optional. Its own component returns and publishes real
        // errors without turning sensor acquisition or startup into a failure.
        const esp_err_t logger_capture_error = refreshLoggerCapture();
        if (logger_capture_error != ESP_OK) {
            ESP_LOGE(TAG, "Optional logger capture observation failed (%s)",
                esp_err_to_name(logger_capture_error));
        }
        record_first_error(publish_update_result(phase, aggregate), &aggregate);
        const esp_err_t commit_error = commit_snapshot_stage(
            staged_snapshot,
            SnapshotStageKind::FullRefresh,
            pdMS_TO_TICKS(5000),
            "refresh commit",
            &summary
        );
        record_first_error(commit_error, &aggregate);
        summary_available = commit_error == ESP_OK;
        if (summary_available) {
            rtc::apply_committed_observation(rtc_latch_observation);
            if (reused_diagnostic_imu_at_us > 0) {
                s_last_full_consumed_diagnostic_imu_us =
                    reused_diagnostic_imu_at_us;
            }
            if (reused_diagnostic_microphone_at_us > 0) {
                s_last_full_consumed_diagnostic_microphone_us =
                    reused_diagnostic_microphone_at_us;
            }
            apply_measured_sample_aging(summary, esp_timer_get_time());
        }
    }
    if (!summary_available) {
        record_first_error(
            publish_update_result(phase, aggregate),
            &aggregate
        );
    }
    // The time owner observes the system clock only after the staged hardware
    // commit. It must not race the stage merge or create a second sensor sampler.
    if (LUNASHELL_time_is_initialized()) {
        const esp_err_t clock_error = LUNASHELL_time_refresh_snapshot();
        record_first_error(clock_error, &aggregate);
        if (clock_error != ESP_OK) {
            record_first_error(publish_update_result(phase, aggregate), &aggregate);
        }
    }
    bool should_log_summary =
        summary_available && std::strcmp(phase, "diagnostic-ui") != 0;
    if (should_log_summary &&
        std::strcmp(phase, "periodic-full") == 0) {
        const TickType_t now_tick = xTaskGetTickCount();
        const TickType_t interval_ticks =
            pdMS_TO_TICKS(kPeriodicSummaryIntervalMs);
        should_log_summary =
            s_last_periodic_summary_tick == 0U ||
            now_tick - s_last_periodic_summary_tick >= interval_ticks;
        if (should_log_summary) {
            s_last_periodic_summary_tick = now_tick;
        }
    }
    // The canonical lock is already released. Keep only the refresh ownership
    // while logging, so another refresh cannot overwrite the shared summary.
    if (should_log_summary) {
        log_summary(summary);
    }
    record_first_error(
        give_refresh_mutex(refresh_mutex, "Hardware refresh"),
        &aggregate
    );
    return aggregate;
}

TouchActivityObservation
DeviceSnapshotService::touchActivityObservation() const noexcept
{
    TouchActivityObservation observation;
    observation.sequence =
        s_touch_activity_sequence.load(std::memory_order_acquire);
    observation.last_activity_at_us =
        s_touch_last_activity_at_us.load(std::memory_order_relaxed);
    return observation;
}

// destination: Caller-owned full snapshot buffer, never allocated on this function's stack.
// Exception: Returns original lock/release errors; destination is unusable on failure.
esp_err_t DeviceSnapshotService::readSnapshot(DeviceSnapshot &destination) const
{
    const esp_err_t lock_error =
        take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "read");
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    destination = s_snapshot;
    const esp_err_t release_error = give_snapshot_mutex("read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    apply_measured_sample_aging(destination, esp_timer_get_time());
    const TouchActivityObservation touch_activity =
        touchActivityObservation();
    destination.ui.touch_activity_sequence = touch_activity.sequence;
    destination.ui.touch_last_activity_at_us = touch_activity.last_activity_at_us;
    return ESP_OK;
}

// destination: Bounded observer result copied under the canonical lock.
// Exception: Returns the original canonical lock/release error.
esp_err_t DeviceSnapshotService::readActivity(ActivitySnapshot &destination) const
{
    const esp_err_t lock_error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "activity read");
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    destination = s_snapshot.activity;
    const esp_err_t release_error = give_snapshot_mutex("activity read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    const int64_t observed_at_us = esp_timer_get_time();
    if (destination.scores_valid &&
        (observed_at_us < destination.window_end_us || observed_at_us - destination.window_end_us > 6000000)) {
        destination.scores_valid = false;
        destination.activity = ActivityClass::Unknown;
        destination.rejection = ActivityRejection::MissingSample;
        destination.error = ESP_ERR_TIMEOUT;
        destination.stable_windows = 0;
    }
    return ESP_OK;
}

// destination: Bounded persisted endurance observation, never a synthesized measurement.
// Exception: Returns the original canonical lock/release error.
esp_err_t DeviceSnapshotService::readBatteryEndurance(BatteryEnduranceSnapshot &destination) const
{
    const esp_err_t error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "endurance read");
    if (error != ESP_OK) {
        return error;
    }
    const BatteryEnduranceSnapshot copy = s_snapshot.battery_endurance;
    const esp_err_t release_error = give_snapshot_mutex("endurance read");
    if (release_error == ESP_OK) {
        destination = copy;
    }
    return release_error;
}

// report: Owner status including retained durable records on a failed write.
// Exception: Returns the original canonical publication error.
esp_err_t DeviceSnapshotService::reportBatteryEndurance(const BatteryEnduranceSnapshot &report)
{
    return update_snapshot([&report](DeviceSnapshot &snapshot) {
        snapshot.battery_endurance = report;
    }, SnapshotPublicationPolicy::Optional);
}

// destination: Aged canonical battery projection; no driver access is performed.
// Exception: Returns the original canonical lock/release error.
esp_err_t DeviceSnapshotService::readBatteryObservation(BatterySnapshot &destination) const
{
    const esp_err_t error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "battery observation");
    if (error != ESP_OK) {
        return error;
    }
    BatterySnapshot copy = s_snapshot.battery;
    const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
    const esp_err_t release_error = give_snapshot_mutex("battery observation");
    if (release_error == ESP_OK) {
        age_measured_sample(copy, esp_timer_get_time(), ttl_us);
        destination = copy;
    }
    return release_error;
}

// No parameters: one PMU-owner acquisition reuses the existing refresh serialization.
// Exception: Returns original acquisition/lock errors without replacing failed samples.
esp_err_t DeviceSnapshotService::refreshBatteryForEndurance()
{
    SemaphoreHandle_t mutex = ensure_refresh_mutex();
    if (mutex == nullptr) {
        ESP_LOGE(TAG, "Endurance PMU refresh mutex unavailable (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGE(TAG, "Endurance PMU refresh lock failed (%s)", esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t error = acquisition_pmu::ready()
        ? acquisition_pmu::sample("endurance") : ESP_ERR_INVALID_STATE;
    record_first_error(give_refresh_mutex(mutex, "endurance PMU refresh"), &error);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Endurance PMU owner sample failed (%s)", esp_err_to_name(error));
    }
    return error;
}

// No parameters: legacy by-value consumers retain their explicit fatal read contract.
// Exception: Aborts on canonical read failure; fallible consumers must call readSnapshot().
DeviceSnapshot DeviceSnapshotService::snapshot() const
{
    DeviceSnapshot copy;
    const esp_err_t error = readSnapshot(copy);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "DeviceSnapshot legacy read failed (%s)", esp_err_to_name(error));
        std::abort();
    }
    return copy;
}

// observation: Original touch service readiness and acquisition error, without hardware reads.
// Exception: Returns the original lock/release failure without changing the destination.
esp_err_t DeviceSnapshotService::readTouchHealthObservation(ComponentSnapshot &observation) const
{
    const esp_err_t error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "touch health read");
    if (error != ESP_OK) {
        return error;
    }
    const ComponentSnapshot copy = s_snapshot.ui.touch;
    const esp_err_t release_error = give_snapshot_mutex("touch health read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    observation = copy;
    return ESP_OK;
}

// observation: Bounded canonical settings and orientation; no settings or sensor reads.
// Exception: Returns the original lock/release error without assigning a partial result.
esp_err_t DeviceSnapshotService::readCodyOptionsObservation(CodyOptionsObservation &observation) const
{
    const esp_err_t error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "Cody options read");
    if (error != ESP_OK) {
        return error;
    }
    const CodyOptionsObservation copy{s_snapshot.cody_preferences, s_snapshot.ui.orientation};
    const esp_err_t release_error = give_snapshot_mutex("Cody options read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    observation = copy;
    return ESP_OK;
}

// observation: Bounded canonical event, copied only after acquiring the snapshot mutex.
// Exception: Returns the original lock/release failure, never an invented empty event.
esp_err_t DeviceSnapshotService::readCodexEventObservation(CodexEventSnapshot &observation) const
{
    const esp_err_t error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "Codex event read");
    if (error != ESP_OK) {
        return error;
    }
    const CodexEventSnapshot copy = s_snapshot.codex_event;
    const esp_err_t release_error = give_snapshot_mutex("Codex event read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    observation = copy;
    return ESP_OK;
}

// observation: Bounded canonical telemetry; no network, filesystem or sensor reads.
// Exception: Returns the original mutex or immutable image-identity error; output is unusable on failure.
esp_err_t DeviceSnapshotService::readCodexTechnicalObservation(CodexTechnicalObservation &observation) const
{
    const auto *application = esp_app_get_description();
    if (application == nullptr) {
        ESP_LOGE(TAG, "Firmware identity unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const size_t version_length = strnlen(application->version, sizeof(application->version));
    if (version_length == 0 || version_length == sizeof(application->version)) {
        ESP_LOGE(TAG, "Firmware version has no bounded identity (%s)", esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        return ESP_ERR_INVALID_RESPONSE;
    }
    for (size_t index = 0; index < version_length; ++index) {
        if (application->version[index] < 32 || application->version[index] > 126) {
            ESP_LOGE(TAG, "Firmware version contains an invalid byte at %u (%s)",
                static_cast<unsigned>(index), esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    const esp_err_t error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "Codex technical read");
    if (error != ESP_OK) {
        return error;
    }
    // The caller owns a persistent buffer; avoid a nested link-plus-telemetry stack copy.
    observation.link = s_snapshot.radios.codex_link;
    observation.technical = s_snapshot.codex_technical;
    std::memcpy(observation.firmware_version, application->version, version_length + 1);
    const esp_err_t release_error = give_snapshot_mutex("Codex technical read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    age_codex_technical(observation.technical, esp_timer_get_time());
    if (observation.link.state != CodexLinkState::Connected) {
        observation.technical.data_stale = observation.technical.received_at_us > 0;
        observation.technical.model_stale = observation.technical.model_valid;
        observation.technical.cost_stale = observation.technical.cost_band != CodexCostBand::Unknown;
        observation.technical.context_stale = observation.technical.context_valid;
        if (observation.technical.compaction == CodexCompactionState::Active) {
            observation.technical.compaction = CodexCompactionState::Stale;
        }
    }
    return ESP_OK;
}

// observation: Coherent projection for the connectivity worker; no driver reads.
esp_err_t DeviceSnapshotService::readConnectivityObservation(ConnectivityObservation &observation) const
{
    const esp_err_t lock_error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "connectivity read");
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    const ConnectivityObservation copy{
        .radios = s_snapshot.radios,
        .operating_state = s_snapshot.power.state,
    };
    const esp_err_t release_error = give_snapshot_mutex("connectivity read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    observation = copy;
    return ESP_OK;
}

// observation: Coherent power inputs; no driver reads or independent sensor cache.
// Exception: Returns the original lock/release error without publishing a partial copy.
esp_err_t DeviceSnapshotService::readPowerControlObservation(
    PowerControlObservation &observation
) const
{
    const esp_err_t lock_error =
        take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "power control read");
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    PowerControlObservation copy{
        .wifi = s_snapshot.radios.wifi,
        .ble = s_snapshot.radios.ble,
        .input = s_snapshot.input,
        .imu = s_snapshot.imu,
        .voice = s_snapshot.voice,
    };
    const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
    const esp_err_t release_error = give_snapshot_mutex("power control read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    // Match snapshot() aging exactly. Buttons retain their acquisition time;
    // the power policy applies its stricter PKEY freshness requirement itself.
    const int64_t now_us = esp_timer_get_time();
    age_measured_sample(copy.imu, now_us, ttl_us);
    if (copy.voice.state == VoiceState::Listening) {
        age_measured_sample(copy.voice, now_us, 1000000);
    }
    observation = copy;
    return ESP_OK;
}

// observation: Applied brightness and mode, read afresh after any power transition.
// Exception: Returns the original lock/release error without publishing a partial copy.
esp_err_t DeviceSnapshotService::readDisplayPolicyObservation(
    DisplayPolicyObservation &observation
) const
{
    const esp_err_t lock_error =
        take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "display policy read");
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    const DisplayPolicyObservation copy{
        .brightness = s_snapshot.ui.brightness,
        .display_power_mode = s_snapshot.ui.display_power_mode,
        .brightness_percent = s_snapshot.ui.brightness_percent,
        .configured_brightness_percent = s_snapshot.ui.configured_brightness_percent,
        .display_power_mode_state = s_snapshot.ui.display_power_mode_state,
    };
    const esp_err_t release_error = give_snapshot_mutex("display policy read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    observation = copy;
    return ESP_OK;
}

// observation: Receives coherent battery, clock, Wi-Fi and BLE for informational chrome.
// The renderer never samples the PMU, system clock, Wi-Fi or BLE driver through this path.
esp_err_t DeviceSnapshotService::readDeviceStatusObservation(
    DeviceStatusObservation &observation
) const
{
    DeviceStatusObservation copy;
    const esp_err_t lock_error =
        take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "device status read");
    if (lock_error != ESP_OK) {
        ESP_LOGE(TAG, "Device status observation lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    copy.battery = s_snapshot.battery;
    copy.clock = s_snapshot.clock;
    copy.wifi = s_snapshot.radios.wifi;
    copy.ble = s_snapshot.radios.ble;
    const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
    const esp_err_t release_error = give_snapshot_mutex("device status read");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "Device status observation release failed (%s)", esp_err_to_name(release_error));
        return release_error;
    }
    const int64_t now_us = esp_timer_get_time();
    age_measured_sample(copy.battery, now_us, ttl_us);
    age_measured_sample(copy.clock, now_us, ttl_us);
    observation = copy;
    return ESP_OK;
}

// frame: Receives the last completed display operation, never a new driver read.
// Its measured flag, original error and completion timestamp travel with the values.
esp_err_t DeviceSnapshotService::readDisplayFrameObservation(DisplayFrameSnapshot &frame) const
{
    const esp_err_t lock_error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "display frame snapshot read");
    if (lock_error != ESP_OK) {
        ESP_LOGE(TAG, "Display frame snapshot read failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayFrameSnapshot copy = s_snapshot.ui.rendering;
    const esp_err_t release_error = give_snapshot_mutex("display frame snapshot read");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "Display frame snapshot release failed (%s)", esp_err_to_name(release_error));
        return release_error;
    }
    frame = copy;
    return ESP_OK;
}

// processor: Receives measured CPU state from the canonical snapshot, aged at read time.
// The rendering task does not call FreeRTOS counters or allocate a full device snapshot.
esp_err_t DeviceSnapshotService::readProcessorObservation(ProcessorSnapshot &processor) const
{
    const esp_err_t lock_error =
        take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "processor snapshot read");
    if (lock_error != ESP_OK) {
        ESP_LOGE(TAG, "Processor snapshot read failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    ProcessorSnapshot copy = s_snapshot.processor;
    const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
    const esp_err_t release_error = give_snapshot_mutex("processor snapshot read");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "Processor snapshot release failed (%s)", esp_err_to_name(release_error));
        return release_error;
    }
    age_measured_sample(copy, esp_timer_get_time(), ttl_us);
    processor = copy;
    return ESP_OK;
}

// observation: Bounded BLE wire fields, copied from one canonical snapshot without hardware reads.
// Exception: Returns the original lock/release error and leaves the caller's output untouched.
esp_err_t DeviceSnapshotService::readBleTelemetryObservation(BleTelemetryObservation &observation) const
{
    BleTelemetryObservation copy;
    const esp_err_t lock_error =
        take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "BLE telemetry read");
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    copy.sequence = s_snapshot.sequence;
    copy.battery = s_snapshot.battery;
    copy.gps = s_snapshot.gps;
    copy.imu = s_snapshot.imu;
    copy.storage = s_snapshot.storage;
    copy.wifi = s_snapshot.radios.wifi;
    copy.memory = s_snapshot.memory;
    copy.battery_level_percent = s_snapshot.battery.level_percent;
    copy.battery_level_percent_valid = s_snapshot.battery.level_percent_valid;
    copy.usb_present = s_snapshot.battery.usb_present;
    copy.gps_has_fix = s_snapshot.gps.has_fix;
    copy.gps_satellites = s_snapshot.gps.satellites;
    copy.imu_calibrated = s_snapshot.imu.calibrated;
    copy.storage_card_response_verified = s_snapshot.storage.card_response_verified;
    static_assert(sizeof(copy.wifi_ssid) == sizeof(s_snapshot.radios.wifi.ssid));
    static_assert(sizeof(copy.wifi_ip_address) == sizeof(s_snapshot.radios.wifi.ip_address));
    std::memcpy(copy.wifi_ssid, s_snapshot.radios.wifi.ssid, sizeof(copy.wifi_ssid));
    std::memcpy(copy.wifi_ip_address, s_snapshot.radios.wifi.ip_address, sizeof(copy.wifi_ip_address));
    copy.heap_free_bytes = s_snapshot.memory.heap_free_bytes;
    copy.psram_free_bytes = s_snapshot.memory.psram_free_bytes;
    const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
    const esp_err_t release_error = give_snapshot_mutex("BLE telemetry read");
    if (release_error != ESP_OK) {
        return release_error;
    }
    // Preserve snapshot() freshness exactly: Wi-Fi and card-response status are
    // event-owned; only these four measured components were aged for BLE output.
    const int64_t now_us = esp_timer_get_time();
    age_measured_sample(copy.battery, now_us, ttl_us);
    age_measured_sample(copy.gps, now_us, ttl_us);
    age_measured_sample(copy.imu, now_us, ttl_us);
    age_measured_sample(copy.memory, now_us, ttl_us);
    observation = copy;
    return ESP_OK;
}

// observation: Coherent diagnostics projection, with original measurement timestamps.
// Exception: Returns the original snapshot lock or release error without publishing a partial copy.
esp_err_t DeviceSnapshotService::readDiagnosticsObservation(
    DiagnosticsObservation &observation
) const
{
    DiagnosticsObservation copy;
    const esp_err_t lock_error =
        take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "diagnostics snapshot read");
    if (lock_error != ESP_OK) {
        ESP_LOGE(TAG, "Diagnostics snapshot read failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    copy.wifi = s_snapshot.radios.wifi;
    copy.memory = s_snapshot.memory;
    copy.processor = s_snapshot.processor;
    copy.storage = s_snapshot.storage;
    const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
    const esp_err_t release_error = give_snapshot_mutex("diagnostics snapshot read");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "Diagnostics snapshot release failed (%s)", esp_err_to_name(release_error));
        return release_error;
    }
    const int64_t now_us = esp_timer_get_time();
    age_measured_sample(copy.memory, now_us, ttl_us);
    age_measured_sample(copy.processor, now_us, ttl_us);
    // Wi-Fi is event-owned rather than a periodically acquired numeric sample.
    // FAT space is acquired every 60 seconds, independently of sensor cadence.
    age_measured_sample(copy.storage, now_us, 90LL * 1000000LL);
    observation = copy;
    return ESP_OK;
}

// observation: Receives a coherent, aged projection of the single canonical snapshot.
// The renderer must never hold a second measurement cache or trigger a driver read.
esp_err_t DeviceSnapshotService::readMotionPowerObservation(
    MotionPowerObservation &observation
) const
{
    MotionPowerObservation copy;
    const esp_err_t lock_error =
        take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "motion/power snapshot read");
    if (lock_error != ESP_OK) {
        ESP_LOGE(TAG, "Motion/power snapshot read failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    copy.battery = s_snapshot.battery;
    copy.imu = s_snapshot.imu;
    copy.clock = s_snapshot.clock;
    copy.audio = s_snapshot.audio;
    copy.voice = s_snapshot.voice;
    copy.character_interaction = s_snapshot.character_interaction;
    copy.character_touch = s_snapshot.character_touch;
    copy.storage_available = s_snapshot.storage.mounted && !s_snapshot.storage.poisoned &&
        s_snapshot.storage.card_response_verified && s_snapshot.storage.last_error == ESP_OK;
    copy.rendering_rotation_degrees = s_snapshot.ui.orientation.rendering_rotation_degrees;
    const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
    const esp_err_t release_error = give_snapshot_mutex("motion/power snapshot read");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "Motion/power snapshot release failed (%s)", esp_err_to_name(release_error));
        return release_error;
    }
    const int64_t now_us = esp_timer_get_time();
    age_measured_sample(copy.battery, now_us, ttl_us);
    age_measured_sample(copy.imu, now_us, ttl_us);
    age_measured_sample(copy.clock, now_us, ttl_us);
    if (copy.voice.state == VoiceState::Listening) {
        age_measured_sample(copy.voice, now_us, 1000000);
    }
    age_measured_sample(copy.audio.microphone, now_us, ttl_us);
    age_measured_sample(copy.audio.playback, now_us, ttl_us);
    age_measured_sample(copy.character_interaction, now_us, ttl_us);
    age_measured_sample(copy.character_touch, now_us, ttl_us);
    // Match the full snapshot: the touch producer publishes its activity atomically.
    copy.touch_last_activity_at_us = touchActivityObservation().last_activity_at_us;
    observation = copy;
    return ESP_OK;
}

// clock: Aged projection of the canonical owner; never read or extrapolate RTC here.
esp_err_t DeviceSnapshotService::readClockObservation(ClockSnapshot &clock) const
{
    const esp_err_t lock_error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "clock read");
    if (lock_error != ESP_OK) {
        ESP_LOGE(TAG, "Clock observation lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    ClockSnapshot copy = s_snapshot.clock;
    const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
    const esp_err_t release_error = give_snapshot_mutex("clock read");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "Clock observation release failed (%s)", esp_err_to_name(release_error));
        return release_error;
    }
    age_measured_sample(copy, esp_timer_get_time(), ttl_us);
    clock = copy;
    return ESP_OK;
}

// voice: Receives the canonical AFE state; this method never acquires audio.
esp_err_t DeviceSnapshotService::readVoiceObservation(VoiceSnapshot &voice) const
{
    const esp_err_t lock_error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "voice snapshot read");
    if (lock_error != ESP_OK) {
        ESP_LOGE(TAG, "Voice snapshot read failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    VoiceSnapshot copy = s_snapshot.voice;
    const esp_err_t release_error = give_snapshot_mutex("voice snapshot read");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "Voice snapshot release failed (%s)", esp_err_to_name(release_error));
        return release_error;
    }
    // Service heartbeats are independent of configurable sensor diagnostic cadence.
    if (copy.state == VoiceState::Listening) {
        age_measured_sample(copy, esp_timer_get_time(), 1000000);
    }
    voice = copy;
    return ESP_OK;
}

// active: Confirmed codec playback state, not an estimate from elapsed clip time.
// mean_level: Absolute PCM16 mean, valid only after a successful codec write.
// sample_rate_hz: Actual playback sample rate.
// volume_percent: Selected codec volume; invalid values are rejected without repair.
// error: Original codec/playback failure, retained by the canonical snapshot.
// cue_id: Stable application sound identity supplied by the write owner.
// Exception: Returns actual publication errors; failed playback remains explicitly invalid.
esp_err_t DeviceSnapshotService::reportPlayback(bool active, uint32_t mean_level,
                                               uint32_t sample_rate_hz,
                                               int volume_percent, esp_err_t error, uint32_t cue_id)
{
    if (error == ESP_OK && (sample_rate_hz == 0 || mean_level > 32768U ||
                           volume_percent < 0 || volume_percent > 100)) {
        ESP_LOGE(TAG, "Playback publication rejected: rate=%lu mean=%lu volume=%d",
                 static_cast<unsigned long>(sample_rate_hz),
                 static_cast<unsigned long>(mean_level), volume_percent);
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t lock_error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "playback publication");
    if (lock_error != ESP_OK) {
        ESP_LOGE(TAG, "Playback publication lock failed: %s", esp_err_to_name(lock_error));
        return lock_error;
    }
    PlaybackSnapshot &playback = s_snapshot.audio.playback;
    playback.cue_id = cue_id;
    const int64_t now_us = esp_timer_get_time();
    if (error == ESP_OK) {
        mark_sample_success(playback, now_us);
        playback.state = active ? PlaybackState::Playing : PlaybackState::Idle;
        playback.level_valid = active;
        if (active) {
            playback.mean_level = mean_level;
        }
        playback.sample_rate_hz = sample_rate_hz;
        playback.volume_percent = static_cast<uint8_t>(volume_percent);
    } else {
        mark_sample_failure(playback, error, now_us);
        playback.state = PlaybackState::Failed;
        playback.level_valid = false;
        ESP_LOGE(TAG, "Playback failed: %s", esp_err_to_name(error));
    }
    const esp_err_t release_error = give_snapshot_mutex("playback publication");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "Playback publication unlock failed: %s", esp_err_to_name(release_error));
    }
    return release_error;
}

// sample: Real acquired coordinates and the gesture's original coordinate basis.
// sampled_at_us: Original hardware report time, never generated by animation.
esp_err_t DeviceSnapshotService::reportCharacterTouch(const CharacterTouchSnapshot &sample,
                                                   int64_t sampled_at_us)
{
    const auto rotation = sample.rendering_rotation_degrees;
    if (sample.origin_x >= 466 || sample.origin_y >= 466 || sample.x >= 466 || sample.y >= 466 ||
        (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) ||
        sample.gesture_started_at_us <= 0 || sample.gesture_started_at_us > sampled_at_us ||
        sampled_at_us > esp_timer_get_time()) {
        ESP_LOGE(TAG, "Body touch coordinates/timestamp rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "body touch publication");
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Body touch snapshot lock failed (%s)", esp_err_to_name(error));
        return error;
    }
    auto &published = s_snapshot.character_touch;
    if (sampled_at_us <= published.last_success_at_us) {
        error = ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "Duplicate/reversed body touch rejected (%s)", esp_err_to_name(error));
    } else {
        published.pressed = sample.pressed;
        published.origin_x = sample.origin_x;
        published.origin_y = sample.origin_y;
        published.x = sample.x;
        published.y = sample.y;
        published.rendering_rotation_degrees = rotation;
        published.gesture_started_at_us = sample.gesture_started_at_us;
        mark_sample_success(published, sampled_at_us);
    }
    const esp_err_t release_error = give_snapshot_mutex("body touch publication");
    return error != ESP_OK ? error : release_error;
}

// kind: Acquired touch target, not a synthetic firmware input.
// sampled_at_us: Original report timestamp; the model consumes the latest event once.
esp_err_t DeviceSnapshotService::reportCharacterInteraction(CharacterInteractionSnapshot::Kind kind,
                                                          int64_t sampled_at_us)
{
    using Kind = CharacterInteractionSnapshot::Kind;
    const int64_t now_us = esp_timer_get_time();
    if ((kind != Kind::EyeLeftTap && kind != Kind::EyeRightTap) ||
        sampled_at_us <= 0 || sampled_at_us > now_us) {
        ESP_LOGE(TAG, "Character interaction rejected: kind=%u sample=%lldus now=%lldus (%s)",
            static_cast<unsigned>(kind), static_cast<long long>(sampled_at_us),
            static_cast<long long>(now_us), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "character interaction");
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Character interaction lock failed: %s", esp_err_to_name(error));
        return error;
    }
    auto &interaction = s_snapshot.character_interaction;
    if (sampled_at_us <= interaction.last_success_at_us) {
        error = ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "Duplicate/reversed character interaction: %s", esp_err_to_name(error));
    } else {
        interaction.kind = kind;
        mark_sample_success(interaction, sampled_at_us);
    }
    const esp_err_t release_error = give_snapshot_mutex("character interaction");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "Character interaction unlock failed: %s", esp_err_to_name(release_error));
        if (error == ESP_OK) {
            error = release_error;
        }
    }
    return error;
}

// geometry: Output owned by the distance service; failed sampling leaves it invalid.
// Exception: Returns original admission, IMU acquisition or projection failures.
esp_err_t DeviceSnapshotService::sampleDistanceGeometry(
    DistanceGeometrySnapshot *geometry
)
{
    if (geometry == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *geometry = {};
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!imu::ready()) {
        return ESP_ERR_NOT_FOUND;
    }

    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t error = imu::sample("distance-geometry", false);
    if (error == ESP_OK) {
        error = take_snapshot_mutex(
            pdMS_TO_TICKS(250),
            "distance geometry"
        );
        if (error == ESP_OK) {
            geometry->imu = s_snapshot.imu;
            geometry->sensor_fusion = s_snapshot.sensor_fusion;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                geometry->acceleration_mps2[axis] =
                    s_snapshot.imu.acceleration_mps2[axis];
            }
            geometry->imu_calibrated = s_snapshot.imu.calibrated;
            geometry->gravity_dominant =
                s_snapshot.sensor_fusion.gravity_dominant;
            geometry->stationary =
                s_snapshot.sensor_fusion.stationary;
            const int64_t now_us = esp_timer_get_time();
            const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
            age_measured_sample(geometry->imu, now_us, ttl_us);
            age_measured_sample(geometry->sensor_fusion, now_us, ttl_us);
            const esp_err_t release_error =
                give_snapshot_mutex("distance geometry");
            if (release_error != ESP_OK) {
                std::abort();
            }
        }
    }

    give_refresh_mutex(refresh_mutex, "Distance geometry sample");
    if (error != ESP_OK) {
        *geometry = {};
    }
    return error;
}

void DeviceSnapshotService::reportTouchActivity()
{
    const int64_t activity_at_us = esp_timer_get_time();
    s_touch_last_activity_at_us.store(
        activity_at_us,
        std::memory_order_relaxed
    );
    s_touch_activity_sequence.fetch_add(
        1,
        std::memory_order_release
    );
}

}  // namespace LUNASHELL


// Allocate and start the shared diagnostics/orientation task once.
// No parameters: uses the canonical shared acquisition state.
// Exception: Returns snapshot allocation or task creation failure.
static esp_err_t ensure_hardware_monitor_task_locked()
{
    if (s_diagnostic_monitor_started) {
        return ESP_OK;
    }

    const esp_err_t allocation_error =
        ensure_diagnostic_stage_snapshot();
    if (allocation_error != ESP_OK) {
        return allocation_error;
    }
    s_diagnostic_imu_active.store(false, std::memory_order_relaxed);
    s_diagnostic_microphone_active.store(false, std::memory_order_relaxed);
    s_diagnostic_microphone_spectrum_active.store(
        false,
        std::memory_order_relaxed
    );
    s_ui_motion_monitor_active.store(false, std::memory_order_relaxed);
    s_ui_motion_monitor_standby.store(false, std::memory_order_relaxed);
    TaskHandle_t task = nullptr;
    s_diagnostic_monitor_started =
        xTaskCreatePinnedToCoreWithCaps(
            diagnostic_monitor_task,
            "LUNASHELL_diag_monitor",
            3072,
            nullptr,
            3,
            &task,
#if CONFIG_LUNASHELL_ACTIVITY_MODEL
            LUNASHELL::task_affinity::kActivityAcquisitionCoreId,
#else
            tskNO_AFFINITY,
#endif
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) == pdPASS;
    if (!s_diagnostic_monitor_started) {
        s_diagnostic_monitor_task_handle.store(
            nullptr,
            std::memory_order_release
        );
        ESP_LOGE(TAG, "Hardware monitor task creation failed");
        return ESP_ERR_NO_MEM;
    }
    s_diagnostic_monitor_task_handle.store(task, std::memory_order_release);
    ESP_LOGI(TAG, "Hardware diagnostics/orientation monitor ready");
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_hardware_init(void)
{
    return LUNASHELL::DeviceSnapshotService::instance().initialize();
}

extern "C" esp_err_t LUNASHELL_hardware_refresh(const char *reason)
{
    return LUNASHELL::DeviceSnapshotService::instance().refresh(reason);
}

// imu_enabled: Requested high-rate IMU stream, admitted only when its owner is ready.
// microphone_enabled: Requested high-rate microphone stream.
// microphone_spectrum_enabled: Spectrum lease requiring the same microphone stream.
// Exception: Returns original admission, task or canonical publication failure.
extern "C" esp_err_t LUNASHELL_hardware_set_diagnostic_streams(
    bool imu_enabled,
    bool microphone_enabled,
    bool microphone_spectrum_enabled
)
{
    if (microphone_spectrum_enabled && !microphone_enabled) {
        publish_diagnostic_monitor(ESP_ERR_INVALID_ARG);
        ESP_LOGE(
            TAG,
            "Diagnostic streams rejected: spectrum requires microphone capture"
        );
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        publish_diagnostic_monitor(ESP_ERR_INVALID_STATE);
        ESP_LOGE(
            TAG,
            "Diagnostic streams rejected: hardware is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }
    if ((imu_enabled && !imu::ready()) ||
        (microphone_enabled && !audio::microphone_ready())) {
        publish_diagnostic_monitor(ESP_ERR_INVALID_STATE);
        ESP_LOGE(
            TAG,
            "Diagnostic streams rejected: requested hardware is unavailable "
            "(IMU=%s microphone=%s)",
            !imu_enabled ? "off" : imu::ready() ? "ready" : "unavailable",
            !microphone_enabled
                ? "off"
                : audio::microphone_ready() ? "ready" : "unavailable"
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (((imu_enabled || microphone_enabled) &&
         !s_diagnostic_period_valid.load(std::memory_order_relaxed)) ||
        (imu_enabled &&
         !imu::smoothing_valid())) {
        publish_diagnostic_monitor(ESP_ERR_INVALID_STATE);
        ESP_LOGE(
            TAG,
            "Diagnostic streams rejected: persisted settings are invalid"
        );
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(s_diagnostic_settings_mutex);
    const esp_err_t task_error =
        ensure_hardware_monitor_task_locked();
    if (task_error != ESP_OK) {
        publish_diagnostic_monitor(task_error);
        return task_error;
    }

    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        publish_diagnostic_monitor(ESP_ERR_NO_MEM);
        ESP_LOGE(
            TAG,
            "Diagnostic streams rejected: refresh mutex allocation failed"
        );
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        publish_diagnostic_monitor(ESP_ERR_TIMEOUT);
        ESP_LOGE(
            TAG,
            "Diagnostic streams rejected: sampler quiescence timed out"
        );
        return ESP_ERR_TIMEOUT;
    }

    if (imu_enabled || microphone_enabled) {
        s_diagnostic_stack_report_pending.store(true, std::memory_order_relaxed);
    }
    s_diagnostic_imu_active.store(imu_enabled, std::memory_order_release);
    s_diagnostic_microphone_active.store(
        microphone_enabled,
        std::memory_order_release
    );
    s_diagnostic_microphone_spectrum_active.store(
        microphone_spectrum_enabled,
        std::memory_order_release
    );
    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex,
        "Diagnostic stream selection"
    );
    notify_hardware_monitor();
    if (release_error != ESP_OK) {
        publish_diagnostic_monitor(release_error);
        return release_error;
    }

    publish_diagnostic_monitor(ESP_OK);
    ESP_LOGI(
        TAG,
        "Diagnostic streams: IMU=%s microphone=%s spectrum=%s",
        imu_enabled ? "active" : "standby",
        microphone_enabled ? "active" : "standby",
        microphone_spectrum_enabled ? "active" : "standby"
    );
    return ESP_OK;
}

// enabled: Requested motion acquisition lease for adaptive UI.
// Exception: Returns original readiness, settings, task or publication failure.
extern "C" esp_err_t LUNASHELL_hardware_set_ui_motion_monitor(bool enabled)
{
    if (!s_initialized || !imu::ready()) {
        ESP_LOGE(
            TAG,
            "Adaptive UI motion monitor rejected: hardware or IMU is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (enabled &&
        !imu::smoothing_valid()) {
        ESP_LOGE(
            TAG,
            "Adaptive UI motion monitor rejected: IMU smoothing is invalid"
        );
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(s_diagnostic_settings_mutex);
    const esp_err_t task_error = ensure_hardware_monitor_task_locked();
    if (task_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Adaptive UI motion monitor task start failed (%s)",
            esp_err_to_name(task_error)
        );
        return task_error;
    }

    s_ui_motion_monitor_active.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        s_ui_motion_monitor_standby.store(false, std::memory_order_relaxed);
    }
    notify_hardware_monitor();
    ESP_LOGI(
        TAG,
        "Adaptive UI motion monitor: %s",
        enabled ? "active" : "standby"
    );
    return ESP_OK;
}

// Keep motion wake available in Device Standby without retaining the active
// UI cadence. The IMU profile itself is selected separately by the power owner.
// standby: Select the preserved motion lease's slower cadence.
// Exception: Returns invalid state if hardware or the motion lease is unavailable.
extern "C" esp_err_t LUNASHELL_hardware_set_ui_motion_monitor_standby(bool standby)
{
    if (!s_initialized || !imu::ready()) {
        ESP_LOGE(
            TAG,
            "Adaptive UI motion standby rejected: hardware or IMU is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_ui_motion_monitor_active.load(std::memory_order_acquire)) {
        ESP_LOGE(
            TAG,
            "Adaptive UI motion standby rejected: motion monitor is inactive"
        );
        return ESP_ERR_INVALID_STATE;
    }

    s_ui_motion_monitor_standby.store(standby, std::memory_order_release);
    notify_hardware_monitor();
    ESP_LOGI(
        TAG,
        "Adaptive UI motion cadence: %s (%lu ms)",
        standby ? "standby" : "active",
        static_cast<unsigned long>(
            standby
                ? kUiMotionStandbySamplePeriodMs
                : kUiMotionSamplePeriodMs
        )
    );
    return ESP_OK;
}

// Preserve requested diagnostic streams and auto-rotation across Device
// Standby while making their sampler quiescent at the shared refresh barrier.
extern "C" esp_err_t LUNASHELL_hardware_set_ui_sampling_power_suspended(
    bool suspended
)
{
    if (!s_initialized) {
        ESP_LOGE(
            TAG,
            "UI sampling standby transition rejected: hardware is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(s_diagnostic_settings_mutex);
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        ESP_LOGE(
            TAG,
            "UI sampling standby transition failed: refresh mutex allocation failed"
        );
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "UI sampling standby transition timed out waiting for sampler quiescence"
        );
        return ESP_ERR_TIMEOUT;
    }

    const bool was_suspended = s_ui_sampling_power_suspended.exchange(
        suspended,
        std::memory_order_acq_rel
    );
    if (suspended) {
        s_ui_sampling_orientation_reset_pending.store(
            false,
            std::memory_order_release
        );
    } else if (was_suspended) {
        // Keep a pre-standby rotation pending until a new active IMU sample
        // rebuilds the filter, rather than applying stale orientation data.
        s_ui_sampling_orientation_reset_pending.store(
            true,
            std::memory_order_release
        );
    }

    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex,
        "UI sampling standby transition"
    );
    notify_hardware_monitor();
    if (release_error != ESP_OK) {
        return release_error;
    }

    ESP_LOGI(
        TAG,
        "UI diagnostics and auto-rotation sampling: %s%s",
        suspended ? "standby" : "active",
        !suspended && was_suspended
            ? "; awaiting a fresh orientation sample"
            : ""
    );
    return ESP_OK;
}

// Send the LC76G subsystem power command while all snapshot I2C work is
// quiesced. PAIR003 leaves the I2C command path available; PAIR002 restores
// GNSS DSP, RF, PE, and clock before the snapshot scheduler is resumed.
// suspended: Exact requested GNSS power state, delegated to its acquisition owner.
// Exception: Returns the owner's original transition/acknowledgement failure.
extern "C" esp_err_t LUNASHELL_hardware_set_gps_power_suspended(bool suspended)
{
    return gnss::set_power_suspended(suspended);
}

// Change rotation state only after the shared monitor and snapshot are ready.
// enabled: Whether measured IMU samples should drive cardinal panel rotation.
// Exception: Returns original admission, refresh barrier or publication failure.
static esp_err_t set_auto_rotation_runtime(bool enabled)
{
#if !CONFIG_LUNASHELL_BUILD_ENABLE_IMU && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (enabled) {
        ESP_LOGE(TAG, "Auto-rotation rejected: IMU disabled by build profile");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!s_initialized) {
        ESP_LOGE(TAG, "Display rotation rejected: hardware is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        ESP_LOGE(TAG, "Display rotation refresh lock allocation failed (%s)",
                 esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Display rotation refresh lock timed out (%s)",
                 esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }
    s_auto_rotation_enabled.store(false, std::memory_order_relaxed);
    const esp_err_t result = apply_pending_display_rotation();
    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex, "Standalone cardinal rotation update"
    );
    return result != ESP_OK ? result : release_error;
#else
    if (!s_initialized || !imu::ready()) {
        ESP_LOGE(
            TAG,
            "Auto-rotation rejected: hardware or IMU is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(s_diagnostic_settings_mutex);
    const esp_err_t task_error =
        ensure_hardware_monitor_task_locked();
    if (task_error != ESP_OK) {
        return task_error;
    }

    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_auto_rotation_enabled.store(enabled, std::memory_order_relaxed);
    (void)observe_auto_rotation_state(enabled);
    const esp_err_t publication_error = publish_orientation_state();
    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex,
        "Auto-rotation runtime update"
    );
    notify_hardware_monitor();
    if (publication_error != ESP_OK) {
        return publication_error;
    }
    return release_error;
#endif
}

// enabled: Explicit auto-rotation choice, persisted and read back before runtime admission.
// Exception: Returns the original storage/readback/runtime error.
extern "C" esp_err_t LUNASHELL_display_set_auto_rotation_enabled(bool enabled)
{
    const esp_err_t init_error = ensure_user_settings();
    if (init_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Auto-rotation settings initialization failed (%s)",
            esp_err_to_name(init_error)
        );
        return init_error;
    }

    auto &settings = LUNASHELL::UserSettingsService::instance();
    const esp_err_t persist_error =
        settings.setBool(kAutoRotationSetting, enabled);
    if (persist_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Auto-rotation preference persistence failed (%s)",
            esp_err_to_name(persist_error)
        );
        return persist_error;
    }

    bool stored = !enabled;
    esp_err_t readback_error = settings.getBool(kAutoRotationSetting, &stored);
    if (readback_error == ESP_OK && stored != enabled) {
        readback_error = ESP_ERR_INVALID_RESPONSE;
    }
    if (readback_error != ESP_OK) {
        ESP_LOGE(TAG, "Auto-rotation preference readback failed (%s)", esp_err_to_name(readback_error));
        return readback_error;
    }
    const esp_err_t runtime_error = set_auto_rotation_runtime(enabled);
    if (runtime_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Persisted auto-rotation could not be applied (%s)",
            esp_err_to_name(runtime_error)
        );
        return runtime_error;
    }
    ESP_LOGI(
        TAG,
        "Cardinal display auto-rotation: %s",
        enabled ? "on" : "off"
    );
    return ESP_OK;
}

extern "C" bool LUNASHELL_display_get_auto_rotation_enabled(void)
{
    return s_auto_rotation_enabled.load(std::memory_order_relaxed);
}

extern "C" bool LUNASHELL_display_get_leg_flip_enabled(void)
{
    return s_leg_flip_enabled.load(std::memory_order_relaxed);
}

// enabled: Whether the Leg Edition applies an additional half-turn to its display.
extern "C" esp_err_t LUNASHELL_display_set_leg_flip_enabled(bool enabled)
{
    const esp_err_t init_error = ensure_user_settings();
    if (init_error != ESP_OK) {
        ESP_LOGE(TAG, "Leg flip settings unavailable (%s)",
                 esp_err_to_name(init_error));
        return init_error;
    }
    if (!s_initialized || !imu::ready()) {
        ESP_LOGE(TAG, "Leg flip rejected: display/IMU not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Leg flip rejected: sampler mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    auto &settings = LUNASHELL::UserSettingsService::instance();
    const esp_err_t save_error = settings.setBool(kLegFlipSetting, enabled);
    if (save_error != ESP_OK) {
        ESP_LOGE(TAG, "Leg flip persistence failed (%s)",
                 esp_err_to_name(save_error));
        const esp_err_t release_error = give_refresh_mutex(
            refresh_mutex, "Leg flip setting failure"
        );
        return release_error != ESP_OK ? release_error : save_error;
    }
    s_leg_flip_enabled.store(enabled, std::memory_order_relaxed);
    const uint16_t desired =
        s_auto_rotation_enabled.load(std::memory_order_relaxed)
            ? leg_rendering_rotation_for_physical(s_orientation.physical_degrees)
            : static_cast<uint16_t>(enabled ? 180U : 0U);
    if (s_orientation.desired_rendering_degrees != desired) {
        s_orientation.desired_rendering_degrees = desired;
        s_orientation.changed_at_us = esp_timer_get_time();
        ++s_orientation.change_sequence;
    }
    const esp_err_t publication_error = publish_orientation_state();
    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex, "Leg flip setting"
    );
    notify_hardware_monitor();
    if (publication_error != ESP_OK) {
        ESP_LOGE(TAG, "Leg flip publication failed (%s)",
                 esp_err_to_name(publication_error));
        return publication_error;
    }
    return release_error;
}

// degrees: Exact panel and touch rotation, restricted to 0, 90, 180, or 270 degrees.
extern "C" esp_err_t LUNASHELL_display_set_rotation_degrees(uint32_t degrees)
{
    if (!is_cardinal_rotation(degrees)) {
        ESP_LOGE(TAG, "Manual display rotation %lu rejected (%s)",
                 static_cast<unsigned long>(degrees), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
#if !CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    ESP_LOGE(TAG, "Manual display rotation %lu rejected: display disabled (%s)",
             static_cast<unsigned long>(degrees), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_initialized || !bsp_display_is_ready()) {
        ESP_LOGE(TAG, "Manual display rotation %lu rejected: hardware is not ready (%s)",
                 static_cast<unsigned long>(degrees), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (!bsp_display_rotation_is_consistent()) {
        ESP_LOGE(TAG, "Manual display rotation %lu rejected: panel/touch mapping is inconsistent (%s)",
                 static_cast<unsigned long>(degrees), esp_err_to_name(BSP_DISPLAY_ERR_ROTATION_INCONSISTENT));
        return BSP_DISPLAY_ERR_ROTATION_INCONSISTENT;
    }
    const esp_err_t init_error = ensure_user_settings();
    if (init_error != ESP_OK) {
        return init_error;
    }
    auto &settings = LUNASHELL::UserSettingsService::instance();
    bool automatic_requested = false;
    esp_err_t result = load_or_create_bool_setting(
        settings, kAutoRotationSetting, "Cardinal display auto-rotation",
        false, &automatic_requested
    );
    if (result != ESP_OK) {
        return result;
    }
    if (automatic_requested || s_auto_rotation_enabled.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "Manual display rotation %lu rejected: auto-rotation is enabled (%s)",
                 static_cast<unsigned long>(degrees), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t stored_degrees = 0;
    result = load_or_create_uint32_setting(
        settings, kDisplayRotationSetting, "Cardinal display rotation",
        0, 0, 270, &stored_degrees
    );
    if (result != ESP_OK) {
        return result;
    }
    if (!is_cardinal_rotation(stored_degrees)) {
        ESP_LOGE(TAG, "Persisted display rotation %lu rejected; explicit reset required (%s)",
                 static_cast<unsigned long>(stored_degrees), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        ESP_LOGE(TAG, "Manual display rotation refresh lock allocation failed (%s)",
                 esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Manual display rotation refresh lock timed out (%s)",
                 esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }
    const uint16_t previous_degrees = s_orientation.applied_rendering_degrees;
    s_orientation.persisted_rendering_valid = true;
    s_orientation.persisted_rendering_degrees = static_cast<uint16_t>(stored_degrees);
    s_orientation.desired_rendering_degrees = static_cast<uint16_t>(degrees);
    s_orientation.changed_at_us = esp_timer_get_time();
    ++s_orientation.change_sequence;
    result = apply_pending_display_rotation();
    if (result == ESP_OK) {
        result = persist_display_rotation(static_cast<uint16_t>(degrees));
    }
    if (result != ESP_OK) {
        const bool applied_without_persistence =
            s_orientation.applied_rendering_degrees == degrees &&
            s_orientation.persisted_rendering_degrees != degrees;
        if (applied_without_persistence) {
            const esp_err_t rollback_error = bsp_display_rotation_set(
                bsp_rotation_for_degrees(previous_degrees)
            );
            if (rollback_error == ESP_OK) {
                s_orientation.applied_rendering_degrees = previous_degrees;
            } else {
                ESP_LOGE(TAG, "Manual display rotation rollback failed: requested=%lu rollback=%s primary=%s",
                         static_cast<unsigned long>(degrees), esp_err_to_name(rollback_error),
                         esp_err_to_name(result));
                s_rotation_retry_blocked.store(true, std::memory_order_release);
                result = rollback_error;
            }
        }
        s_orientation.desired_rendering_degrees = s_orientation.applied_rendering_degrees;
        s_rotation_retry_after_us.store(0, std::memory_order_release);
        s_rotation_failure_latched.store(true, std::memory_order_release);
        mark_sample_failure(s_orientation.component, result, esp_timer_get_time());
        ESP_LOGE(TAG, "Manual display rotation %lu failed (%s)",
                 static_cast<unsigned long>(degrees), esp_err_to_name(result));
    } else {
        s_rotation_failure_latched.store(false, std::memory_order_release);
        mark_sample_success(s_orientation.component, esp_timer_get_time());
        ESP_LOGI(TAG, "Manual panel and touch rotation persisted: %lu deg",
                 static_cast<unsigned long>(degrees));
    }
    const esp_err_t publication_error = publish_orientation_state();
    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex, "Manual cardinal rotation update"
    );
    if (publication_error != ESP_OK) {
        ESP_LOGE(TAG, "Manual rotation publication failed (%s)", esp_err_to_name(publication_error));
    }
    return result != ESP_OK ? result :
           publication_error != ESP_OK ? publication_error : release_error;
#endif
}

// Restore both saved values exactly; invalid persisted data requires an explicit reset.
extern "C" esp_err_t LUNASHELL_display_apply_saved_auto_rotation(void)
{
#if !CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    ESP_LOGE(
        TAG,
        "Saved display rotation restore rejected: display disabled by build profile"
    );
    return ESP_ERR_NOT_SUPPORTED;
#else
    const esp_err_t init_error = ensure_user_settings();
    if (init_error != ESP_OK) {
        return init_error;
    }
    auto &settings = LUNASHELL::UserSettingsService::instance();

    bool enabled = false;
    esp_err_t error = load_or_create_bool_setting(
        settings,
        kAutoRotationSetting,
        "Cardinal display auto-rotation",
        false,
        &enabled
    );
    if (error != ESP_OK) {
        return error;
    }

#if CONFIG_LUNASHELL_BUILD_PROFILE_LEG_EDITION
    bool leg_flip_enabled = false;
    error = load_or_create_bool_setting(
        settings, kLegFlipSetting, "Leg display flip", false,
        &leg_flip_enabled
    );
    if (error != ESP_OK) {
        return error;
    }
    s_leg_flip_enabled.store(leg_flip_enabled, std::memory_order_relaxed);
#endif

    uint32_t saved_rendering_degrees = 0;
    error = load_or_create_uint32_setting(
        settings,
        kDisplayRotationSetting,
        "Cardinal display rotation",
        0,
        0,
        270,
        &saved_rendering_degrees
    );
    if (error != ESP_OK) {
        return error;
    }
    if (!is_cardinal_rotation(saved_rendering_degrees)) {
        ESP_LOGE(
            TAG,
            "Persisted display rotation is non-cardinal: %lu",
            static_cast<unsigned long>(saved_rendering_degrees)
        );
        return ESP_ERR_INVALID_ARG;
    }

    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const uint16_t applied_degrees = static_cast<uint16_t>(
        static_cast<unsigned>(bsp_display_rotation_get()) * 90U
    );
    s_orientation.persisted_rendering_valid = true;
    s_orientation.persisted_rendering_degrees =
        static_cast<uint16_t>(saved_rendering_degrees);
    s_rotation_retry_blocked.store(false, std::memory_order_release);
    s_rotation_failure_latched.store(false, std::memory_order_release);
    s_rotation_retry_after_us.store(0, std::memory_order_release);
    s_orientation.applied_rendering_degrees = applied_degrees;
    s_orientation.physical_degrees = rendering_rotation_for_physical(
        static_cast<uint16_t>(
            (saved_rendering_degrees +
             (s_leg_flip_enabled.load(std::memory_order_relaxed) ? 180U : 0U)) % 360U
        )
    );
    s_orientation.desired_rendering_degrees =
        static_cast<uint16_t>(saved_rendering_degrees);
    s_orientation.gyro_physical_degrees =
        static_cast<float>(s_orientation.physical_degrees);
    s_orientation.gyro_sampled_at_us = 0;
    s_orientation.candidate_valid = false;
    const int64_t restored_at_us = esp_timer_get_time();
    s_orientation.sampled_at_us = restored_at_us;
    if (s_orientation.desired_rendering_degrees !=
        s_orientation.applied_rendering_degrees) {
        ++s_orientation.change_sequence;
        s_orientation.changed_at_us = restored_at_us;
    }
    mark_sample_success(s_orientation.component, restored_at_us);
    const esp_err_t publication_error = publish_orientation_state();
    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex,
        "Saved cardinal rotation restore"
    );
    if (publication_error != ESP_OK) {
        return publication_error;
    }
    if (release_error != ESP_OK) {
        return release_error;
    }

    ESP_LOGI(
        TAG,
        "Restored display state: auto-rotation=%s rendering=%lu deg",
        enabled ? "on" : "off",
        static_cast<unsigned long>(saved_rendering_degrees)
    );
    return set_auto_rotation_runtime(enabled);
#endif
}

extern "C" esp_err_t LUNASHELL_hardware_set_diagnostic_period_ms(uint32_t period_ms)
{
    if (period_ms < kDiagnosticPeriodMinMs ||
        period_ms > kDiagnosticPeriodMaxMs) {
        publish_diagnostic_period(ESP_ERR_INVALID_ARG);
        ESP_LOGE(
            TAG,
            "Diagnostic sample period %lu outside [%lu, %lu]",
            static_cast<unsigned long>(period_ms),
            static_cast<unsigned long>(kDiagnosticPeriodMinMs),
            static_cast<unsigned long>(kDiagnosticPeriodMaxMs)
        );
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_diagnostic_settings_mutex);
    const esp_err_t init_error = ensure_user_settings();
    if (init_error != ESP_OK) {
        publish_diagnostic_period(init_error);
        ESP_LOGE(
            TAG,
            "Diagnostic sample period settings initialization failed (%s)",
            esp_err_to_name(init_error)
        );
        return init_error;
    }
    auto &settings = LUNASHELL::UserSettingsService::instance();
    const esp_err_t persist_error =
        settings.setUInt32(kDiagnosticPeriodSetting, period_ms);
    if (persist_error != ESP_OK) {
        publish_diagnostic_period(persist_error);
        ESP_LOGE(
            TAG,
            "Diagnostic sample period persistence failed (%s)",
            esp_err_to_name(persist_error)
        );
        return persist_error;
    }

    s_diagnostic_period_ms.store(period_ms, std::memory_order_relaxed);
    s_diagnostic_period_valid.store(true, std::memory_order_relaxed);
    notify_hardware_monitor();
    publish_diagnostic_period(ESP_OK);
    ESP_LOGI(TAG, "Diagnostic sample period: %lu ms",
             static_cast<unsigned long>(period_ms));
    return ESP_OK;
}

extern "C" uint32_t LUNASHELL_hardware_get_diagnostic_period_ms(void)
{
    return s_diagnostic_period_ms.load(std::memory_order_relaxed);
}

// percent: Explicit smoothing choice, persisted before the component admits it.
// Exception: Returns original admission, persistence or refresh barrier failure.
extern "C" esp_err_t LUNASHELL_hardware_set_imu_smoothing_percent(uint32_t percent)
{
    if (percent > kImuSmoothingMaxPercent) {
        publish_imu_smoothing(ESP_ERR_INVALID_ARG);
        ESP_LOGE(
            TAG,
            "IMU smoothing %lu outside [0, %lu]",
            static_cast<unsigned long>(percent),
            static_cast<unsigned long>(kImuSmoothingMaxPercent)
        );
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_diagnostic_settings_mutex);
    const esp_err_t init_error = ensure_user_settings();
    if (init_error != ESP_OK) {
        publish_imu_smoothing(init_error);
        ESP_LOGE(
            TAG,
            "IMU smoothing settings initialization failed (%s)",
            esp_err_to_name(init_error)
        );
        return init_error;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        publish_imu_smoothing(ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        publish_imu_smoothing(ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    auto &settings = LUNASHELL::UserSettingsService::instance();
    const esp_err_t persist_error =
        settings.setUInt32(kImuSmoothingSetting, percent);
    if (persist_error != ESP_OK) {
        (void)give_refresh_mutex(
            refresh_mutex,
            "IMU smoothing update"
        );
        publish_imu_smoothing(persist_error);
        ESP_LOGE(
            TAG,
            "IMU smoothing persistence failed (%s)",
            esp_err_to_name(persist_error)
        );
        return persist_error;
    }

    imu::set_smoothing(percent, true);
    imu::reset_filter();
    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex,
        "IMU smoothing update"
    );
    if (release_error != ESP_OK) {
        publish_imu_smoothing(release_error);
        return release_error;
    }

    publish_imu_smoothing(ESP_OK);
    ESP_LOGI(TAG, "IMU input smoothing: %lu%%",
             static_cast<unsigned long>(percent));
    return ESP_OK;
}

// No parameters: return the value admitted by the component after persistence.
extern "C" uint32_t LUNASHELL_hardware_get_imu_smoothing_percent(void)
{
    return imu::smoothing_percent();
}

extern "C" void LUNASHELL_hardware_report_speaker(bool ready, esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportSpeaker(ready, error);
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
// observation: Coherent canonical policy; a failure never publishes a partial view.
// Exception: Returns original snapshot lock or release failure.
esp_err_t LUNASHELL::DeviceSnapshotService::readSpeechObservation(SpeechObservation &observation) const
{
    SpeechObservation copy;
    esp_err_t error = take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "speech policy read");
    if (error != ESP_OK) return error;
    copy.clock = s_snapshot.clock;
    copy.storage = s_snapshot.storage;
    copy.operating_state = s_snapshot.power.state;
    const int64_t ttl_us = measured_sample_ttl_us(s_snapshot);
    error = give_snapshot_mutex("speech policy read");
    if (error != ESP_OK) return error;
    age_measured_sample(copy.clock, esp_timer_get_time(), ttl_us);
    age_measured_sample(copy.storage, esp_timer_get_time(), ttl_us);
    observation = copy;
    return ESP_OK;
}
#endif
