/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_distance_service.hpp"
#include "LUNASHELL_sensor_fusion.hpp"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_snapshot_cadence.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_bit_defs.h"
#include "esp_chip_info.h"
#include "esp_codec_dev.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "xt_utils.h"

#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
// The Waveshare header defines the standard M_PI macro unconditionally.
#pragma push_macro("M_PI")
#undef M_PI
#include "qmi8658.h"
#pragma pop_macro("M_PI")
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#endif

#define I2C_TIMEOUT_MS 100
#define GPS_MAX_PAYLOAD 4096
#define GPS_MAX_PENDING_BYTES (1024U * 1024U)

#define AXP2101_ADDRESS 0x34

#define PCF85063_ADDRESS 0x51
#define PCF85063_REG_CTRL1 0x00
#define PCF85063_REG_RAM 0x03
#define PCF85063_REG_SECONDS 0x04

#define LC76G_WRITE_ADDRESS 0x50
#define LC76G_READ_ADDRESS 0x54
#define LC76G_DATA_WRITE_ADDRESS 0x58

namespace {

const char *TAG = "LUNASHELL_snapshot";
constexpr char kDiagnosticPeriodSetting[] = "diag_ms";
constexpr char kImuSmoothingSetting[] = "imu_smooth";
constexpr char kAutoRotationSetting[] = "auto_rotate";
constexpr char kDisplayRotationSetting[] = "display_rot";
constexpr uint32_t kDiagnosticPeriodDefaultMs = 300;
constexpr uint32_t kDiagnosticPeriodMinMs = 100;
constexpr uint32_t kDiagnosticPeriodMaxMs = 2000;
constexpr uint32_t kImuSmoothingDefaultPercent = 70;
constexpr uint32_t kImuSmoothingMaxPercent = 90;
constexpr uint32_t kOrientationSamplePeriodMs = 100;
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
constexpr size_t kAudioSpectrumFrameCount = 256;
constexpr float kAudioSpectrumFullScale = 32768.0F;
// The ES7210's normal ambient signal is well below -60 dBFS; keep its measured
// bands visible instead of quantising every quiet, valid capture to zero.
constexpr float kAudioSpectrumFloor = 0.00001F;
constexpr float kAudioSpectrumDisplayFloorDbfs = -90.0F;
constexpr float kAudioSpectrumTwoPi = 6.28318530718F;
constexpr uint16_t kAudioSpectrumBins[LUNASHELL::kAudioSpectrumBandCount] = {
    1, 2, 3, 4, 6, 8, 11, 16,
    23, 32, 45, 64, 90, 110, 120, 127,
};
constexpr int64_t kMinimumMeasuredSampleTtlUs = 5LL * 1000LL * 1000LL;
constexpr int64_t kFullSnapshotTtlNumerator = 6;
constexpr int64_t kFullSnapshotTtlDenominator = 5;
constexpr uint32_t kPeriodicSummaryIntervalMs = 30000U;
// Snapshot ownership is held only while copying or committing the canonical
// value. Reaching this deadline is therefore an invariant failure, not a slow
// hardware sample, and must never leave UI/HTTP/radio callers blocked forever.
constexpr uint32_t kSnapshotMutexTimeoutMs = 1000U;
constexpr TickType_t kSnapshotMutexTimeoutTicks =
    pdMS_TO_TICKS(kSnapshotMutexTimeoutMs);
constexpr uint64_t kPmuIrqReadFailure = 0xFFFFFFULL;
constexpr uint64_t kPmuIrqMask = 0xFFFFFFULL;
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
constexpr uint64_t kPmuRuntimeIrqMask =
    XPOWERS_AXP2101_BAT_INSERT_IRQ |
    XPOWERS_AXP2101_BAT_REMOVE_IRQ |
    XPOWERS_AXP2101_VBUS_INSERT_IRQ |
    XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
    XPOWERS_AXP2101_PKEY_SHORT_IRQ |
    XPOWERS_AXP2101_PKEY_LONG_IRQ |
    XPOWERS_AXP2101_BAT_CHG_DONE_IRQ |
    XPOWERS_AXP2101_BAT_CHG_START_IRQ;
#endif

i2c_master_bus_handle_t s_i2c_bus;
i2c_master_dev_handle_t s_pmu_device;
i2c_master_dev_handle_t s_rtc;
i2c_master_dev_handle_t s_gps_control;
i2c_master_dev_handle_t s_gps_read;
i2c_master_dev_handle_t s_gps_write;
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
qmi8658_dev_t s_imu;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
XPowersPMU s_power;
#endif

bool s_initialized;
bool s_initialization_attempted;
esp_err_t s_initialization_result = ESP_ERR_INVALID_STATE;
bool s_pmu_ready;
std::atomic<uint32_t> s_pkey_short_press_pending{0};
std::atomic<uint32_t> s_pkey_long_press_pending{0};
uint64_t s_pmu_queued_pkey_status;
bool s_rtc_ready;
std::atomic_bool s_rtc_oscillator_stop_latched{false};
std::atomic_bool s_rtc_recovery_readback_required{false};
std::atomic<uint32_t> s_rtc_trusted_write_generation{0};
std::mutex s_rtc_operation_mutex;
bool s_imu_ready;
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
bool s_imu_profile_low_power;
bool s_imu_profile_valid;
esp_err_t s_imu_profile_error = ESP_ERR_INVALID_STATE;
#endif
bool s_imu_calibrated;
bool s_imu_calibration_stationary;
uint16_t s_imu_calibration_samples;
float s_imu_calibration_accel_max_span;
float s_imu_calibration_gyro_max_span;
float s_accel_bias[3];
float s_gyro_bias[3];
float s_accel_filtered[3];
float s_gyro_filtered[3];
bool s_imu_filter_ready;
std::atomic<uint32_t> s_imu_smoothing_percent{kImuSmoothingDefaultPercent};
std::atomic_bool s_imu_smoothing_valid{false};
bool s_gps_ready;
bool s_gps_power_suspended;
bool s_microphone_ready;
bool s_diagnostic_monitor_started;
std::atomic<TaskHandle_t> s_diagnostic_monitor_task_handle{nullptr};
std::atomic_bool s_diagnostic_imu_active{false};
std::atomic_bool s_diagnostic_microphone_active{false};
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
int64_t s_last_committed_diagnostic_imu_us;
int64_t s_last_committed_diagnostic_microphone_us;
int64_t s_last_full_consumed_diagnostic_imu_us;
int64_t s_last_full_consumed_diagnostic_microphone_us;
TickType_t s_last_periodic_summary_tick;
std::mutex s_diagnostic_settings_mutex;
std::mutex s_processor_usage_mutex;

static_assert(sizeof(size_t) == sizeof(uint32_t));
DRAM_ATTR volatile uint32_t s_allocation_failure_count = 0;
DRAM_ATTR volatile uint32_t s_largest_failed_allocation_bytes = 0;
DRAM_ATTR volatile uint32_t s_failed_allocation_caps_seen_mask = 0;
std::atomic_bool s_memory_telemetry_active{false};

FORCE_INLINE_ATTR void allocation_counter_update_max(
    volatile uint32_t *counter,
    uint32_t candidate
)
{
    uint32_t observed = *counter;
    while (observed < candidate) {
        if (xt_utils_compare_and_set(counter, observed, candidate)) {
            return;
        }
        observed = *counter;
    }
}

FORCE_INLINE_ATTR void allocation_counter_or(
    volatile uint32_t *counter,
    uint32_t bits
)
{
    uint32_t observed = *counter;
    while ((observed | bits) != observed) {
        if (xt_utils_compare_and_set(counter, observed, observed | bits)) {
            return;
        }
        observed = *counter;
    }
}

FORCE_INLINE_ATTR void allocation_counter_increment_saturating(
    volatile uint32_t *counter
)
{
    uint32_t observed = *counter;
    while (observed != UINT32_MAX) {
        if (xt_utils_compare_and_set(counter, observed, observed + 1U)) {
            return;
        }
        observed = *counter;
    }
}

void IRAM_ATTR allocation_failed_hook(
    size_t requested_bytes,
    uint32_t caps,
    const char *function_name
)
{
    (void)function_name;

    allocation_counter_update_max(
        &s_largest_failed_allocation_bytes,
        static_cast<uint32_t>(requested_bytes)
    );
    allocation_counter_or(&s_failed_allocation_caps_seen_mask, caps);
    __asm__ __volatile__("memw" ::: "memory");
    allocation_counter_increment_saturating(&s_allocation_failure_count);
}

struct ProcessorUsageState {
    bool initialized = false;
    configRUN_TIME_COUNTER_TYPE sampled_at = 0;
    configRUN_TIME_COUNTER_TYPE idle_runtime[2] = {};
};

ProcessorUsageState s_processor_usage;

struct StaticMemoryMetadata {
    bool ready = false;
    uint32_t flash_capacity_bytes = 0;
    uint32_t app_partition_bytes = 0;
    uint32_t firmware_image_bytes = 0;
};

struct StaticProcessorMetadata {
    bool ready = false;
    uint8_t core_count = 0;
};

struct RtcLatchObservation {
    bool valid = false;
    bool oscillator_stop = false;
    uint32_t trusted_write_generation_at_read = 0;
};

StaticMemoryMetadata s_static_memory_metadata;
StaticProcessorMetadata s_static_processor_metadata;

// Chip-specific adapters return this normalized sample to the shared pipeline.
struct ImuRawSample {
    float accelX;
    float accelY;
    float accelZ;
    float gyroX;
    float gyroY;
    float gyroZ;
    float temperature;
};

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
    int64_t last_error_log_us = 0;
    LUNASHELL::ComponentSnapshot component;
};

OrientationState s_orientation;

LUNASHELL::DeviceSnapshot s_snapshot;
StaticSemaphore_t s_snapshot_mutex_control;
SemaphoreHandle_t s_snapshot_mutex;
std::atomic<uint32_t> s_touch_activity_sequence{0};
std::atomic<int64_t> s_touch_last_activity_at_us{0};
std::atomic<TaskHandle_t> s_snapshot_stage_owner{nullptr};
std::atomic<LUNASHELL::DeviceSnapshot *> s_snapshot_stage{nullptr};
bool s_snapshot_stage_dirty;
StaticSemaphore_t s_refresh_mutex_control;
SemaphoreHandle_t s_refresh_mutex;
StaticSemaphore_t s_pmu_mutex_control;
SemaphoreHandle_t s_pmu_mutex;

LUNASHELL::DeviceSnapshot *s_diagnostic_stage_snapshot;
std::atomic<TaskHandle_t> s_imu_calibration_task{nullptr};
std::atomic_bool s_imu_calibration_request_pending{false};
std::mutex s_imu_calibration_mutex;
StaticTask_t s_imu_calibration_task_control;
StackType_t s_imu_calibration_task_stack[3072];
// The synchronous I2C read copies GNSS bytes into this parser buffer, so the
// application payload can live in PSRAM.
EXT_RAM_BSS_ATTR char s_gps_payload[GPS_MAX_PAYLOAD + 1];
int16_t s_microphone_samples[1024];

struct AudioSpectrumBands {
    uint8_t value[2][LUNASHELL::kAudioSpectrumBandCount] = {};
};

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

SemaphoreHandle_t ensure_refresh_mutex()
{
    if (s_refresh_mutex == nullptr) {
        s_refresh_mutex = xSemaphoreCreateMutexStatic(&s_refresh_mutex_control);
    }
    return s_refresh_mutex;
}

SemaphoreHandle_t ensure_pmu_mutex()
{
    if (s_pmu_mutex == nullptr) {
        s_pmu_mutex = xSemaphoreCreateMutexStatic(&s_pmu_mutex_control);
    }
    return s_pmu_mutex;
}

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

template <typename Update>
esp_err_t update_snapshot(Update update);

void mark_sample_failure(
    LUNASHELL::ComponentSnapshot &component,
    esp_err_t error,
    int64_t sampled_at_us
);

void publish_imu_calibration_failure(
    esp_err_t error,
    int64_t attempted_at_us
)
{
    update_snapshot([error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
        mark_sample_failure(
            snapshot.imu.calibration,
            error,
            attempted_at_us
        );
    });
}

LUNASHELL::DeviceHealth health_for(bool ready, esp_err_t error)
{
    if (ready) {
        return LUNASHELL::DeviceHealth::Ready;
    }
    return error == ESP_ERR_NOT_FOUND ? LUNASHELL::DeviceHealth::Missing : LUNASHELL::DeviceHealth::Degraded;
}

void set_component(LUNASHELL::ComponentSnapshot &component, bool ready, esp_err_t error)
{
    component.health = health_for(ready, error);
    component.last_error = error;
}

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

template <typename Update>
esp_err_t update_snapshot(Update update)
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
        std::abort();
    }
    if (xSemaphoreTake(mutex, kSnapshotMutexTimeoutTicks) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot update failed: mutex take timed out after %lu ms",
            static_cast<unsigned long>(kSnapshotMutexTimeoutMs)
        );
        std::abort();
    }

    update(s_snapshot);
    advance_snapshot_sequence();
    if (xSemaphoreGive(mutex) != pdTRUE) {
        ESP_LOGE(TAG, "DeviceSnapshot update failed: mutex release rejected");
        std::abort();
    }
    return ESP_OK;
}

void mark_sample_success(
    LUNASHELL::ComponentSnapshot &component,
    int64_t attempted_at_us,
    int64_t completed_at_us
)
{
    set_component(component, true, ESP_OK);
    component.data_valid = true;
    component.data_stale = false;
    component.last_attempt_at_us = attempted_at_us;
    component.last_success_at_us = completed_at_us;
}

void mark_sample_success(
    LUNASHELL::ComponentSnapshot &component,
    int64_t sampled_at_us
)
{
    mark_sample_success(component, sampled_at_us, sampled_at_us);
}

void mark_sample_failure(
    LUNASHELL::ComponentSnapshot &component,
    esp_err_t error,
    int64_t attempted_at_us
)
{
    set_component(component, false, error);
    component.data_valid = false;
    component.data_stale = component.last_success_at_us > 0;
    component.last_attempt_at_us = attempted_at_us;
}

void record_first_error(esp_err_t error, esp_err_t *aggregate)
{
    if (aggregate != nullptr && *aggregate == ESP_OK && error != ESP_OK) {
        *aggregate = error;
    }
}

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

void log_snapshot_publication_failure(
    const char *operation,
    esp_err_t error
)
{
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "%s snapshot publication failed (%s)",
            operation,
            esp_err_to_name(error)
        );
        std::abort();
    }
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
    if (!stage_is_newer(staged.microphone, current.microphone)) {
        return;
    }
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
    std::memcpy(
        current.channel_spectrum,
        staged.channel_spectrum,
        sizeof(current.channel_spectrum)
    );
    current.spectrum_sequence = staged.spectrum_sequence;
}

enum class SnapshotStageKind {
    FullRefresh,
    Diagnostic,
};

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

bool high_frequency_sample_phase(const char *phase)
{
    return phase != nullptr &&
           (std::strcmp(phase, "diagnostic-ui") == 0 ||
            std::strcmp(phase, "periodic-full") == 0);
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

void age_measured_sample(
    LUNASHELL::ComponentSnapshot &component,
    int64_t now_us,
    int64_t ttl_us
)
{
    if (!component.data_valid || component.data_stale ||
        component.last_success_at_us <= 0) {
        return;
    }
    if (now_us < component.last_success_at_us ||
        now_us - component.last_success_at_us >= ttl_us) {
        component.data_valid = false;
        component.data_stale = true;
    }
}

int64_t measured_sample_ttl_us(const LUNASHELL::DeviceSnapshot &snapshot)
{
    if (!snapshot.diagnostics.full_snapshot_monitor_active ||
        snapshot.diagnostics.full_snapshot_period.last_error != ESP_OK ||
        !snapshot.diagnostics.full_snapshot_period.data_valid) {
        return kMinimumMeasuredSampleTtlUs;
    }

    const int64_t cadence_ttl_us =
        static_cast<int64_t>(snapshot.diagnostics.full_snapshot_period_ms) *
        1000LL * kFullSnapshotTtlNumerator /
        kFullSnapshotTtlDenominator;
    return std::max(kMinimumMeasuredSampleTtlUs, cadence_ttl_us);
}

void apply_measured_sample_aging(
    LUNASHELL::DeviceSnapshot &snapshot,
    int64_t now_us
)
{
    const int64_t ttl_us = measured_sample_ttl_us(snapshot);
    age_measured_sample(snapshot.battery, now_us, ttl_us);
    age_measured_sample(snapshot.rtc, now_us, ttl_us);
    age_measured_sample(snapshot.imu, now_us, ttl_us);
    age_measured_sample(snapshot.sensor_fusion, now_us, ttl_us);
    age_measured_sample(snapshot.distance, now_us, ttl_us);
    for (LUNASHELL::TofSensorSnapshot &sensor : snapshot.distance.sensors) {
        age_measured_sample(sensor, now_us, ttl_us);
    }
    age_measured_sample(snapshot.gps, now_us, ttl_us);
    age_measured_sample(snapshot.audio.microphone, now_us, ttl_us);
    if (snapshot.ui.orientation.auto_rotation_enabled) {
        age_measured_sample(snapshot.ui.orientation, now_us, ttl_us);
    }
    age_measured_sample(snapshot.memory, now_us, ttl_us);
    age_measured_sample(snapshot.processor, now_us, ttl_us);
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

void reset_imu_filter()
{
    std::memset(s_accel_filtered, 0, sizeof(s_accel_filtered));
    std::memset(s_gyro_filtered, 0, sizeof(s_gyro_filtered));
    s_imu_filter_ready = false;
}

void publish_diagnostic_period(esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportDiagnosticPeriod(
        s_diagnostic_period_ms.load(std::memory_order_relaxed),
        error
    );
}

void publish_imu_smoothing(esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportImuSmoothing(
        s_imu_smoothing_percent.load(std::memory_order_relaxed),
        error
    );
}

void publish_diagnostic_monitor(esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportDiagnosticMonitor(
        s_diagnostic_imu_active.load(std::memory_order_relaxed),
        s_diagnostic_microphone_active.load(std::memory_order_relaxed),
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

// Copy the service state into the only snapshot contract consumed by UI and HTTP.
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
        rendering_rotation_for_physical(s_orientation.physical_degrees);
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
        data.gyroZ - (s_imu_calibrated ? s_gyro_bias[2] : 0.0F);
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
        s_orientation.desired_rendering_degrees = 0;
        s_orientation.sampled_at_us = changed_at_us;
        mark_sample_success(s_orientation.component, changed_at_us);
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

// Classify raw gravity into a stable cardinal physical orientation.
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
    mark_sample_success(s_orientation.component, sampled_at_us);

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
            rendering_rotation_for_physical(physical_degrees)) {
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

// Accept only rotations that the panel and touch transforms implement exactly.
bool is_cardinal_rotation(uint32_t degrees)
{
    return degrees == 0U || degrees == 90U ||
           degrees == 180U || degrees == 270U;
}

// Convert only a previously validated cardinal degree value.
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

// Commit only a newly applied cardinal angle to avoid redundant NVS writes.
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
    const esp_err_t error =
        settings.setUInt32(kDisplayRotationSetting, rendering_degrees);
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
esp_err_t apply_pending_display_rotation()
{
    if (s_orientation.desired_rendering_degrees ==
        s_orientation.applied_rendering_degrees) {
        return ESP_OK;
    }

    const esp_err_t result = bsp_display_rotation_set(
        bsp_rotation_for_degrees(s_orientation.desired_rendering_degrees)
    );
    if (result == ESP_OK) {
        s_orientation.applied_rendering_degrees =
            s_orientation.desired_rendering_degrees;
        const esp_err_t persist_error = persist_display_rotation(
            s_orientation.applied_rendering_degrees
        );
        if (persist_error != ESP_OK) {
            mark_sample_failure(
                s_orientation.component,
                persist_error,
                esp_timer_get_time()
            );
            (void)publish_orientation_state();
            ESP_LOGE(
                TAG,
                "Cardinal display rotation persistence failed (%s)",
                esp_err_to_name(persist_error)
            );
            return persist_error;
        }
        mark_sample_success(
            s_orientation.component,
            esp_timer_get_time()
        );
        return publish_orientation_state();
    }

    const int64_t now = esp_timer_get_time();
    if (now - s_orientation.last_error_log_us >= 5000000) {
        s_orientation.last_error_log_us = now;
        ESP_LOGW(
            TAG,
            "Cardinal display rotation pending (%s)",
            esp_err_to_name(result)
        );
    }
    return result;
}

esp_err_t add_i2c_device(
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t frequency_hz,
    i2c_master_dev_handle_t *device
)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = frequency_hz,
        .scl_wait_us = 20000,
        .flags = {},
    };
    return i2c_master_bus_add_device(bus, &config, device);
}

esp_err_t read_register(
    i2c_master_dev_handle_t device,
    uint8_t reg,
    uint8_t *data,
    size_t length
)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, length, I2C_TIMEOUT_MS);
}

esp_err_t write_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t payload[] = {reg, value};
    return i2c_master_transmit(device, payload, sizeof(payload), I2C_TIMEOUT_MS);
}

esp_err_t write_registers(
    i2c_master_dev_handle_t device,
    uint8_t reg,
    const uint8_t *data,
    size_t length
)
{
    if (device == nullptr || data == nullptr || length == 0 || length > 15) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t payload[16] = {reg};
    std::memcpy(payload + 1, data, length);
    return i2c_master_transmit(device, payload, length + 1, I2C_TIMEOUT_MS);
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
int xpowers_read_register(
    uint8_t device_address,
    uint8_t reg,
    uint8_t *data,
    uint8_t length
)
{
    if (device_address != AXP2101_ADDRESS ||
        s_pmu_device == nullptr || data == nullptr || length == 0U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 callback read rejected: address=0x%02x register=0x%02x length=%u",
            device_address,
            reg,
            static_cast<unsigned>(length)
        );
        return -1;
    }
    const esp_err_t error =
        read_register(s_pmu_device, reg, data, length);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 callback read failed: register=0x%02x length=%u (%s)",
            reg,
            static_cast<unsigned>(length),
            esp_err_to_name(error)
        );
        return -1;
    }
    return 0;
}

int xpowers_write_register(
    uint8_t device_address,
    uint8_t reg,
    uint8_t *data,
    uint8_t length
)
{
    if (device_address != AXP2101_ADDRESS ||
        s_pmu_device == nullptr || data == nullptr || length == 0U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 callback write rejected: address=0x%02x register=0x%02x length=%u",
            device_address,
            reg,
            static_cast<unsigned>(length)
        );
        return -1;
    }
    const esp_err_t error =
        write_registers(s_pmu_device, reg, data, length);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 callback write failed: register=0x%02x length=%u (%s)",
            reg,
            static_cast<unsigned>(length),
            esp_err_to_name(error)
        );
        return -1;
    }
    return 0;
}

esp_err_t read_pmu_registers(
    uint8_t reg,
    uint8_t *data,
    size_t length
)
{
    if (data == nullptr || length == 0 || length > UINT8_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_pmu_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return read_register(s_pmu_device, reg, data, length);
}

esp_err_t enable_and_verify_pmu_battery_detection()
{
    uint8_t value = 0;
    esp_err_t error = read_pmu_registers(
        XPOWERS_AXP2101_BAT_DET_CTRL,
        &value,
        1
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: battery detection control read failed at register=0x%02x (%s)",
            XPOWERS_AXP2101_BAT_DET_CTRL,
            esp_err_to_name(error)
        );
        return error;
    }
    if ((value & BIT(0)) == 0U) {
        error = write_register(
            s_pmu_device,
            XPOWERS_AXP2101_BAT_DET_CTRL,
            static_cast<uint8_t>(value | BIT(0))
        );
        if (error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "PMU AXP2101: battery detection enable failed at register=0x%02x (%s)",
                XPOWERS_AXP2101_BAT_DET_CTRL,
                esp_err_to_name(error)
            );
            return error;
        }
    }

    uint8_t readback = 0;
    error = read_pmu_registers(
        XPOWERS_AXP2101_BAT_DET_CTRL,
        &readback,
        1
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: battery detection enable readback failed at register=0x%02x (%s)",
            XPOWERS_AXP2101_BAT_DET_CTRL,
            esp_err_to_name(error)
        );
        return error;
    }
    if ((readback & BIT(0)) == 0U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: battery detection enable readback mismatch at register=0x%02x value=0x%02x (%s)",
            XPOWERS_AXP2101_BAT_DET_CTRL,
            readback,
            esp_err_to_name(ESP_ERR_INVALID_RESPONSE)
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t read_pmu_irq_status(const char *operation, uint64_t *status)
{
    if (status == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t status_bytes[3] = {};
    // INTSTS1..3 are contiguous; one burst keeps the status coherent and avoids
    // three polling transactions on the shared bus every 100 ms.
    const esp_err_t error = read_pmu_registers(
        XPOWERS_AXP2101_INTSTS1,
        status_bytes,
        sizeof(status_bytes)
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: %s failed at registers=0x%02x..0x%02x (%s)",
            operation,
            XPOWERS_AXP2101_INTSTS1,
            XPOWERS_AXP2101_INTSTS3,
            esp_err_to_name(error)
        );
        return error;
    }
    const uint64_t readback =
        static_cast<uint64_t>(status_bytes[0]) |
        (static_cast<uint64_t>(status_bytes[1]) << 8U) |
        (static_cast<uint64_t>(status_bytes[2]) << 16U);
    if (readback == kPmuIrqReadFailure) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: %s failed (raw=0x%06llx)",
            operation,
            static_cast<unsigned long long>(readback)
        );
        return ESP_FAIL;
    }
    *status = readback;
    return ESP_OK;
}

esp_err_t verify_pmu_irq_enable_mask(uint64_t expected)
{
    uint8_t enabled_bytes[3] = {};
    const esp_err_t error = read_pmu_registers(
        XPOWERS_AXP2101_INTEN1,
        enabled_bytes,
        sizeof(enabled_bytes)
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: runtime IRQ enable readback failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    const uint64_t observed =
        static_cast<uint64_t>(enabled_bytes[0]) |
        (static_cast<uint64_t>(enabled_bytes[1]) << 8U) |
        (static_cast<uint64_t>(enabled_bytes[2]) << 16U);
    if (observed != expected) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: runtime IRQ mask mismatch expected=0x%06llx observed=0x%06llx",
            static_cast<unsigned long long>(expected),
            static_cast<unsigned long long>(observed)
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t give_pmu_mutex(
    SemaphoreHandle_t mutex,
    const char *operation
)
{
    if (mutex == nullptr || xSemaphoreGive(mutex) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "%s failed: PMU mutex release rejected",
            operation
        );
        std::abort();
    }
    return ESP_OK;
}

esp_err_t clear_and_verify_pmu_irq_status(
    const char *operation,
    uint64_t clear_mask = kPmuIrqMask,
    uint64_t *remaining_status = nullptr
)
{
    for (size_t index = 0; index < 3U; ++index) {
        const uint8_t value = static_cast<uint8_t>(
            (clear_mask >> (index * 8U)) & 0xFFU
        );
        if (value == 0U) {
            continue;
        }
        const uint8_t reg = static_cast<uint8_t>(
            XPOWERS_AXP2101_INTSTS1 + index
        );
        const esp_err_t write_error =
            write_register(s_pmu_device, reg, value);
        if (write_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "PMU AXP2101: %s write failed at register=0x%02x mask=0x%02x (%s)",
                operation,
                reg,
                value,
                esp_err_to_name(write_error)
            );
            return write_error;
        }
    }

    uint64_t readback = 0;
    const esp_err_t read_error = read_pmu_irq_status(operation, &readback);
    if (read_error != ESP_OK) {
        return read_error;
    }
    if (remaining_status != nullptr) {
        *remaining_status = readback;
    }
    if ((readback & clear_mask) != 0U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: %s verification failed: clear=0x%06llx observed=0x%06llx",
            operation,
            static_cast<unsigned long long>(clear_mask),
            static_cast<unsigned long long>(readback)
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t collect_pmu_irq_events()
{
    uint64_t irq_status = 0;
    esp_err_t error =
        read_pmu_irq_status("read runtime IRQ status", &irq_status);
    if (error != ESP_OK) {
        return error;
    }

    if (irq_status == 0U) {
        // A previous clear may have succeeded even if its verification read did
        // not; retain queued button counts but release their de-duplication mask.
        s_pmu_queued_pkey_status = 0;
        return ESP_OK;
    }

    const uint64_t pkey_status =
        irq_status &
        (XPOWERS_AXP2101_PKEY_SHORT_IRQ |
         XPOWERS_AXP2101_PKEY_LONG_IRQ);
    const uint64_t new_pkey_status =
        pkey_status & ~s_pmu_queued_pkey_status;
    if ((new_pkey_status & XPOWERS_AXP2101_PKEY_SHORT_IRQ) != 0U) {
        s_pkey_short_press_pending.fetch_add(1, std::memory_order_release);
    }
    if ((new_pkey_status & XPOWERS_AXP2101_PKEY_LONG_IRQ) != 0U) {
        s_pkey_long_press_pending.fetch_add(1, std::memory_order_release);
    }
    // Mark decoded PKEY bits before clearing so a failed clear can be retried
    // without duplicating the already queued physical event.
    s_pmu_queued_pkey_status |= pkey_status;

    ESP_LOGI(
        TAG,
        "PMU AXP2101 events: raw=0x%llx bat+%d bat-%d usb+%d usb-%d key-short=%d key-long=%d charge-start=%d charge-done=%d",
        static_cast<unsigned long long>(irq_status),
        (irq_status & XPOWERS_AXP2101_BAT_INSERT_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_BAT_REMOVE_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_VBUS_INSERT_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_VBUS_REMOVE_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_PKEY_SHORT_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_PKEY_LONG_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_BAT_CHG_START_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_BAT_CHG_DONE_IRQ) != 0
    );

    uint64_t remaining_status = 0;
    error = clear_and_verify_pmu_irq_status(
        "clear runtime IRQ status",
        irq_status,
        &remaining_status
    );
    if (error != ESP_OK) {
        return error;
    }
    s_pmu_queued_pkey_status &= remaining_status;
    return ESP_OK;
}

esp_err_t verify_pmu_precharge_current(xpowers_prechg_t expected)
{
    const xpowers_prechg_t observed = s_power.getPrechargeCurr();
    if (observed == expected) {
        return ESP_OK;
    }
    const unsigned observed_value = static_cast<unsigned>(observed);
    const esp_err_t error =
        observed_value == 0x0FU
            ? ESP_FAIL
            : ESP_ERR_INVALID_RESPONSE;
    ESP_LOGE(
        TAG,
        "PMU AXP2101: precharge-current verification failed: expected=%u observed=%u (%s)",
        static_cast<unsigned>(expected),
        observed_value,
        esp_err_to_name(error)
    );
    return error;
}

esp_err_t verify_pmu_termination_current(
    xpowers_axp2101_chg_iterm_t expected
)
{
    const xpowers_axp2101_chg_iterm_t observed =
        s_power.getChargerTerminationCurr();
    if (observed == expected) {
        return ESP_OK;
    }
    const unsigned observed_value = static_cast<unsigned>(observed);
    const esp_err_t error =
        observed_value == 0x0FU
            ? ESP_FAIL
            : ESP_ERR_INVALID_RESPONSE;
    ESP_LOGE(
        TAG,
        "PMU AXP2101: termination-current verification failed: expected=%u observed=%u (%s)",
        static_cast<unsigned>(expected),
        observed_value,
        esp_err_to_name(error)
    );
    return error;
}
#endif

uint8_t bcd_to_decimal(uint8_t value)
{
    return static_cast<uint8_t>(((value >> 4) * 10U) + (value & 0x0FU));
}

uint8_t decimal_to_bcd(uint8_t value)
{
    return static_cast<uint8_t>(((value / 10U) << 4) | (value % 10U));
}

bool valid_calendar_time(const LUNASHELL_calendar_time_t &time)
{
    if (time.year < 2024 || time.year > 2099 ||
        time.month < 1 || time.month > 12 ||
        time.hour > 23 || time.minute > 59 || time.second > 59) {
        return false;
    }
    static constexpr uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    uint8_t maximum_day = days_per_month[time.month - 1U];
    const bool leap =
        (time.year % 4U == 0U && time.year % 100U != 0U) ||
        time.year % 400U == 0U;
    if (time.month == 2U && leap) {
        maximum_day = 29;
    }
    return time.day >= 1U && time.day <= maximum_day;
}

bool valid_rtc_register_calendar(const LUNASHELL_calendar_time_t &time)
{
    if (time.year < 2000 || time.year > 2099 ||
        time.month < 1 || time.month > 12 ||
        time.hour > 23 || time.minute > 59 || time.second > 59) {
        return false;
    }
    static constexpr uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    uint8_t maximum_day = days_per_month[time.month - 1U];
    const bool leap =
        (time.year % 4U == 0U && time.year % 100U != 0U) ||
        time.year % 400U == 0U;
    if (time.month == 2U && leap) {
        maximum_day = 29;
    }
    return time.day >= 1U && time.day <= maximum_day;
}

bool valid_bcd(uint8_t value)
{
    return (value & 0x0FU) <= 9U && ((value >> 4U) & 0x0FU) <= 9U;
}

bool calendar_fields_equal(
    const LUNASHELL_calendar_time_t &left,
    const LUNASHELL_calendar_time_t &right
)
{
    return left.year == right.year &&
           left.month == right.month &&
           left.day == right.day &&
           left.hour == right.hour &&
           left.minute == right.minute &&
           left.second == right.second;
}

void advance_calendar_one_second(LUNASHELL_calendar_time_t *time)
{
    if (time == nullptr) {
        return;
    }
    if (++time->second <= 59U) {
        return;
    }
    time->second = 0;
    if (++time->minute <= 59U) {
        return;
    }
    time->minute = 0;
    if (++time->hour <= 23U) {
        return;
    }
    time->hour = 0;
    static constexpr uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    uint8_t maximum_day = days_per_month[time->month - 1U];
    const bool leap =
        (time->year % 4U == 0U && time->year % 100U != 0U) ||
        time->year % 400U == 0U;
    if (time->month == 2U && leap) {
        maximum_day = 29U;
    }
    if (++time->day <= maximum_day) {
        return;
    }
    time->day = 1;
    if (++time->month <= 12U) {
        return;
    }
    time->month = 1;
    ++time->year;
}

bool rtc_write_readback_matches(
    const LUNASHELL_calendar_time_t &requested,
    const LUNASHELL_calendar_time_t &observed
)
{
    if (calendar_fields_equal(requested, observed)) {
        return true;
    }
    LUNASHELL_calendar_time_t next = requested;
    advance_calendar_one_second(&next);
    return calendar_fields_equal(next, observed);
}

RtcLatchObservation make_rtc_latch_observation(
    const LUNASHELL_calendar_time_t &time,
    esp_err_t read_error,
    esp_err_t publication_error,
    uint32_t trusted_write_generation_at_read
)
{
    RtcLatchObservation observation;
    if (publication_error != ESP_OK) {
        return observation;
    }
    if (read_error != ESP_ERR_INVALID_STATE ||
        !valid_rtc_register_calendar(time) ||
        time.clock_integrity) {
        return observation;
    }

    observation.valid = true;
    observation.oscillator_stop = true;
    observation.trusted_write_generation_at_read =
        trusted_write_generation_at_read;
    return observation;
}

void apply_rtc_latch_observation_locked(
    const RtcLatchObservation &observation
)
{
    if (!observation.valid) {
        return;
    }
    if (!observation.oscillator_stop) {
        return;
    }

    if (s_rtc_trusted_write_generation.load(std::memory_order_acquire) !=
        observation.trusted_write_generation_at_read) {
        // A trusted write superseded this sample. Do not touch the latch: a
        // newer read may already own a valid oscillator-stop observation.
        return;
    }
    s_rtc_oscillator_stop_latched.store(true, std::memory_order_release);
}

void apply_rtc_latch_observation(const RtcLatchObservation &observation)
{
    // Pair the generation comparison with the latch update. RTC reads and
    // trusted writes use the same mutex, so a stale observation can neither
    // relatch after a write nor clear a newer observation.
    std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);
    apply_rtc_latch_observation_locked(observation);
}

void require_rtc_recovery_readback(
    const RtcLatchObservation &observation
)
{
    if (observation.valid && observation.oscillator_stop) {
        // This is intentionally immediate rather than commit-gated. A valid
        // GNSS fix can write the RTC later in the same staged Full Snapshot;
        // that write must verify recovery even before the persistent latch is
        // applied after snapshot commit.
        s_rtc_recovery_readback_required.store(
            true,
            std::memory_order_release
        );
    }
}

bool finite_imu_sample(const ImuRawSample &data)
{
    return std::isfinite(data.accelX) &&
           std::isfinite(data.accelY) &&
           std::isfinite(data.accelZ) &&
           std::isfinite(data.gyroX) &&
           std::isfinite(data.gyroY) &&
           std::isfinite(data.gyroZ) &&
           std::isfinite(data.temperature);
}

esp_err_t sample_pmu_locked(const char *phase)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
    LUNASHELL::BatterySnapshot sample;
    const int64_t attempted_at_us = esp_timer_get_time();
    esp_err_t error =
        i2c_master_probe(s_i2c_bus, AXP2101_ADDRESS, I2C_TIMEOUT_MS);

    if (error != ESP_OK) {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                snapshot.battery.level_percent_valid = false;
                snapshot.battery.battery_mv_valid = false;
                mark_sample_failure(
                    snapshot.battery,
                    error,
                    attempted_at_us
                );
            }
        );
        ESP_LOGW(TAG, "PMU AXP2101: %s sample failed (%s)", phase, esp_err_to_name(error));
        return sample_publication_result(
            "PMU AXP2101",
            phase,
            error,
            publication_error
        );
    }

    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t battery_adc[2] = {};
    uint8_t vbus_adc[2] = {};
    uint8_t system_adc[2] = {};
    uint8_t temperature_adc[2] = {};
    uint8_t percent = 0;
    error = read_pmu_registers(XPOWERS_AXP2101_STATUS1, &status1, 1);
    if (error == ESP_OK) {
        error = read_pmu_registers(XPOWERS_AXP2101_STATUS2, &status2, 1);
    }

    sample.vbus_good = (status1 & BIT(5)) != 0;
    sample.batfet_enabled = (status1 & BIT(4)) != 0;
    sample.battery_present = (status1 & BIT(3)) != 0;
    sample.battery_active_mode = (status1 & BIT(2)) != 0;
    sample.thermal_regulation_active = (status1 & BIT(1)) != 0;
    sample.input_current_limit_active = (status1 & BIT(0)) != 0;
    sample.usb_present =
        sample.vbus_good && (status2 & BIT(3)) == 0;

    // BAT presence comes from the dedicated detector. Do not touch the cell
    // ADC or percentage registers when no battery is connected: their bytes
    // are not current telemetry and the extra I2C transactions are wasteful.
    if (error == ESP_OK && sample.battery_present) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_ADC_DATA_RELUST0,
            battery_adc,
            sizeof(battery_adc)
        );
    }
    if (error == ESP_OK && sample.battery_present) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_BAT_PERCENT_DATA,
            &percent,
            1
        );
    }
    if (error == ESP_OK) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_ADC_DATA_RELUST4,
            vbus_adc,
            sizeof(vbus_adc)
        );
    }
    if (error == ESP_OK) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_ADC_DATA_RELUST6,
            system_adc,
            sizeof(system_adc)
        );
    }
    if (error == ESP_OK) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_ADC_DATA_RELUST8,
            temperature_adc,
            sizeof(temperature_adc)
        );
    }

    const uint16_t battery_mv = static_cast<uint16_t>(
        ((battery_adc[0] & 0x1FU) << 8U) | battery_adc[1]
    );
    const uint16_t usb_mv = static_cast<uint16_t>(
        ((vbus_adc[0] & 0x3FU) << 8U) | vbus_adc[1]
    );
    const uint16_t system_mv = static_cast<uint16_t>(
        ((system_adc[0] & 0x3FU) << 8U) | system_adc[1]
    );
    const uint16_t temperature_raw = static_cast<uint16_t>(
        ((temperature_adc[0] & 0x3FU) << 8U) | temperature_adc[1]
    );
    const uint8_t charge_state = status2 >> 5U;
    const uint8_t charger_phase = status2 & 0x07U;
    if (error == ESP_OK && charge_state > 2U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: reserved charge state %u in STATUS2=0x%02x",
            static_cast<unsigned>(charge_state),
            status2
        );
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK && charger_phase > 5U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: reserved charger phase %u in STATUS2=0x%02x",
            static_cast<unsigned>(charger_phase),
            status2
        );
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK &&
        ((sample.battery_present &&
          (percent > 100U || battery_mv == 0U)) ||
         (sample.usb_present && usb_mv == 0U) ||
         system_mv == 0U ||
         temperature_raw > 16383U)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK && sample.battery_present) {
        sample.level_percent_valid = true;
        sample.battery_mv_valid = true;
        sample.level_percent = static_cast<int>(percent);
        sample.battery_mv = battery_mv;
    }
    sample.usb_mv = usb_mv;
    sample.system_mv = system_mv;
    sample.die_temperature_c =
        XPOWERS_AXP2101_CONVERSION(temperature_raw);
    if (error == ESP_OK && !std::isfinite(sample.die_temperature_c)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK) {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                snapshot.battery.level_percent_valid = false;
                snapshot.battery.battery_mv_valid = false;
                mark_sample_failure(
                    snapshot.battery,
                    error,
                    attempted_at_us
                );
            }
        );
        ESP_LOGW(
            TAG,
            "PMU AXP2101: %s returned invalid telemetry (%s)",
            phase,
            esp_err_to_name(error)
        );
        return sample_publication_result(
            "PMU AXP2101",
            phase,
            error,
            publication_error
        );
    }

    const char *power_state =
        charge_state == 1U
            ? "charging"
            : (charge_state == 2U ? "discharging" : "standby");
    static constexpr std::array<const char *, 6> kChargerPhases = {
        "trickle",
        "precharge",
        "constant-current",
        "constant-voltage",
        "done",
        "stopped",
    };
    strlcpy(sample.power_state, power_state, sizeof(sample.power_state));
    strlcpy(
        sample.charger_phase,
        kChargerPhases[charger_phase],
        sizeof(sample.charger_phase)
    );

    mark_sample_success(sample, attempted_at_us);
    const esp_err_t publication_error = update_snapshot(
        [&sample](LUNASHELL::DeviceSnapshot &snapshot) {
            snapshot.battery = sample;
        }
    );

    const esp_log_level_t success_log_level =
        high_frequency_sample_phase(phase) ? ESP_LOG_DEBUG : ESP_LOG_INFO;
    if (sample.level_percent_valid && sample.battery_mv_valid) {
        ESP_LOG_LEVEL_LOCAL(
            success_log_level,
            TAG,
            "PMU AXP2101 %s: battery=present cell=%d%% %dmV, state=%s phase=%s, USB=%s good=%d %dmV, system=%dmV, die=%.1fC, BATFET=%d active=%d thermal=%d input-limit=%d",
            phase,
            sample.level_percent,
            sample.battery_mv,
            sample.power_state,
            sample.charger_phase,
            sample.usb_present ? "present" : "absent",
            sample.vbus_good,
            sample.usb_mv,
            sample.system_mv,
            sample.die_temperature_c,
            sample.batfet_enabled,
            sample.battery_active_mode,
            sample.thermal_regulation_active,
            sample.input_current_limit_active
        );
    } else {
        ESP_LOG_LEVEL_LOCAL(
            success_log_level,
            TAG,
            "PMU AXP2101 %s: battery=absent cell=UNAVAILABLE, state=%s phase=%s, USB=%s good=%d %dmV, system=%dmV, die=%.1fC, BATFET=%d active=%d thermal=%d input-limit=%d",
            phase,
            sample.power_state,
            sample.charger_phase,
            sample.usb_present ? "present" : "absent",
            sample.vbus_good,
            sample.usb_mv,
            sample.system_mv,
            sample.die_temperature_c,
            sample.batfet_enabled,
            sample.battery_active_mode,
            sample.thermal_regulation_active,
            sample.input_current_limit_active
        );
    }
    return sample_publication_result(
        "PMU AXP2101",
        phase,
        ESP_OK,
        publication_error
    );
#else
    const esp_err_t error = ESP_ERR_NOT_SUPPORTED;
    const esp_err_t publication_error = update_snapshot(
        [error](LUNASHELL::DeviceSnapshot &snapshot) {
            set_component(snapshot.battery, false, error);
        }
    );
    return sample_publication_result(
        "PMU",
        phase,
        error,
        publication_error
    );
#endif
}

esp_err_t publish_pmu_lock_failure(
    const char *phase,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
            snapshot.battery.level_percent_valid = false;
            snapshot.battery.battery_mv_valid = false;
            mark_sample_failure(
                snapshot.battery,
                error,
                attempted_at_us
            );
        }
    );
    ESP_LOGW(
        TAG,
        "PMU AXP2101: %s sample lock failed (%s)",
        phase,
        esp_err_to_name(error)
    );
    return sample_publication_result(
        "PMU AXP2101",
        phase,
        error,
        publication_error
    );
}

esp_err_t sample_pmu(const char *phase)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
    SemaphoreHandle_t pmu_mutex = ensure_pmu_mutex();
    if (pmu_mutex == nullptr) {
        return publish_pmu_lock_failure(phase, ESP_ERR_NO_MEM);
    }
    if (xSemaphoreTake(pmu_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return publish_pmu_lock_failure(phase, ESP_ERR_TIMEOUT);
    }

    esp_err_t result = sample_pmu_locked(phase);
    const esp_err_t release_error =
        give_pmu_mutex(pmu_mutex, "PMU telemetry sample");
    record_first_error(release_error, &result);
    return result;
#else
    return sample_pmu_locked(phase);
#endif
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
esp_err_t remove_pmu_device()
{
    if (s_pmu_device == nullptr) {
        return ESP_OK;
    }
    const esp_err_t error =
        i2c_master_bus_rm_device(s_pmu_device);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 device cleanup failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    s_pmu_device = nullptr;
    return ESP_OK;
}
#endif

esp_err_t init_pmu(i2c_master_bus_handle_t bus)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
    esp_err_t error = i2c_master_probe(bus, AXP2101_ADDRESS, I2C_TIMEOUT_MS);
    if (error == ESP_OK) {
        error = add_i2c_device(
            bus,
            AXP2101_ADDRESS,
            XPOWERSLIB_I2C_MASTER_SPEED,
            &s_pmu_device
        );
    }
    if (error == ESP_OK &&
        !s_power.begin(
            AXP2101_ADDRESS,
            xpowers_read_register,
            xpowers_write_register
        )) {
        error = ESP_FAIL;
    }
    if (error != ESP_OK) {
        record_first_error(remove_pmu_device(), &error);
        update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
            set_component(snapshot.battery, false, error);
        });
        ESP_LOGW(TAG, "PMU AXP2101: not detected (%s)", esp_err_to_name(error));
        return error;
    }

    const auto require_pmu_write = [](bool applied, const char *operation) {
        if (!applied) {
            ESP_LOGE(TAG, "PMU AXP2101: %s failed", operation);
        }
        return applied;
    };
    if (error == ESP_OK) {
        error = enable_and_verify_pmu_battery_detection();
    }
    if (error == ESP_OK &&
        (!require_pmu_write(
            s_power.enableVbusVoltageMeasure(),
            "enable VBUS voltage measurement"
        ) ||
        !require_pmu_write(
            s_power.enableBattVoltageMeasure(),
            "enable battery voltage measurement"
        ) ||
        !require_pmu_write(
            s_power.enableSystemVoltageMeasure(),
            "enable system voltage measurement"
        ) ||
        !require_pmu_write(
            s_power.enableTemperatureMeasure(),
            "enable die temperature measurement"
        ))) {
        error = ESP_FAIL;
    }
    // The board has no battery NTC on TS; leaving TS measurement enabled can inhibit charging.
    if (error == ESP_OK &&
        !require_pmu_write(
            s_power.disableTSPinMeasure(),
            "disable unused TS measurement"
        )) {
        error = ESP_FAIL;
    }

    if (error == ESP_OK &&
        !require_pmu_write(
            s_power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ),
            "disable existing IRQ mask"
        )) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        error = clear_and_verify_pmu_irq_status(
            "clear initialization IRQ status"
        );
        if (error == ESP_OK) {
            s_pkey_short_press_pending.store(0, std::memory_order_relaxed);
            s_pkey_long_press_pending.store(0, std::memory_order_relaxed);
            s_pmu_queued_pkey_status = 0;
        }
    }
    if (error == ESP_OK &&
        !require_pmu_write(
                s_power.enableIRQ(kPmuRuntimeIrqMask),
                "enable runtime IRQ mask"
            )) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        error = verify_pmu_irq_enable_mask(kPmuRuntimeIrqMask);
    }
    if (error == ESP_OK) {
        s_power.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
        error = verify_pmu_precharge_current(
            XPOWERS_AXP2101_PRECHARGE_50MA
        );
    }
    if (error == ESP_OK &&
        !require_pmu_write(
            s_power.setChargerConstantCurr(
                XPOWERS_AXP2101_CHG_CUR_400MA
            ),
            "set constant charge current"
        )) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        s_power.setChargerTerminationCurr(
            XPOWERS_AXP2101_CHG_ITERM_25MA
        );
        error = verify_pmu_termination_current(
            XPOWERS_AXP2101_CHG_ITERM_25MA
        );
    }
    if (error == ESP_OK &&
        !require_pmu_write(
            s_power.setChargeTargetVoltage(
                XPOWERS_AXP2101_CHG_VOL_4V2
            ),
            "set charge target voltage"
        )) {
        error = ESP_FAIL;
    }
    if (error != ESP_OK) {
        record_first_error(remove_pmu_device(), &error);
        update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
            set_component(snapshot.battery, false, error);
        });
        return error;
    }
    // ADC channels need one conversion interval before their first coherent sample.
    vTaskDelay(pdMS_TO_TICKS(50));

    s_pmu_ready = true;
    update_snapshot([](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.battery, true, ESP_OK);
    });
    ESP_LOGI(
        TAG,
        "PMU AXP2101: battery detector verified, ADCs enabled, TS disabled, charge policy=50/400/25mA target=4.20V, events enabled"
    );
    return ESP_OK;
#else
    (void)bus;
    const esp_err_t error = ESP_ERR_NOT_SUPPORTED;
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.battery, false, error);
    });
    ESP_LOGI(TAG, "PMU: disabled by board hardware selection");
    return error;
#endif
}

esp_err_t init_rtc(i2c_master_bus_handle_t bus)
{
    esp_err_t error = add_i2c_device(bus, PCF85063_ADDRESS, 400000, &s_rtc);
    uint8_t ram_original = 0;
    if (error == ESP_OK) {
        error = read_register(s_rtc, PCF85063_REG_RAM, &ram_original, 1);
    }
    const bool ram_original_valid = error == ESP_OK;
    uint8_t ram_test = ram_original;
    if (error == ESP_OK) {
        error = write_register(s_rtc, PCF85063_REG_RAM, static_cast<uint8_t>(ram_original ^ BIT(7)));
    }
    if (error == ESP_OK) {
        error = read_register(s_rtc, PCF85063_REG_RAM, &ram_test, 1);
    }
    if (ram_original_valid) {
        const esp_err_t restore_error =
            write_register(s_rtc, PCF85063_REG_RAM, ram_original);
        if (restore_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "RTC PCF85063 RAM probe restore failed (%s)",
                esp_err_to_name(restore_error)
            );
            record_first_error(restore_error, &error);
        }
    }
    if (error == ESP_OK && ((ram_test ^ ram_original) & BIT(7)) == 0) {
        error = ESP_ERR_NOT_FOUND;
    }

    uint8_t ctrl1 = 0;
    if (error == ESP_OK) {
        error = read_register(s_rtc, PCF85063_REG_CTRL1, &ctrl1, 1);
    }
    if (error == ESP_OK) {
        ctrl1 &= static_cast<uint8_t>(~(BIT(5) | BIT(1)));
        error = write_register(s_rtc, PCF85063_REG_CTRL1, ctrl1);
    }
    s_rtc_ready = error == ESP_OK;
    if (s_rtc_ready) {
        s_rtc_oscillator_stop_latched.store(
            false,
            std::memory_order_release
        );
        s_rtc_recovery_readback_required.store(
            false,
            std::memory_order_release
        );
    }
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.rtc, error == ESP_OK, error);
    });
    if (!s_rtc_ready) {
        ESP_LOGW(TAG, "RTC PCF85063: initialization failed (%s)", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "RTC PCF85063: detected, 24-hour mode configured");
    return ESP_OK;
}

esp_err_t read_rtc_calendar(LUNASHELL_calendar_time_t *time)
{
    if (time == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_rtc_ready || s_rtc == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw[7] = {};
    esp_err_t error =
        read_register(s_rtc, PCF85063_REG_SECONDS, raw, sizeof(raw));
    if (error != ESP_OK) {
        return error;
    }

    const uint8_t seconds = raw[0] & 0x7FU;
    const uint8_t minutes = raw[1] & 0x7FU;
    const uint8_t hours = raw[2] & 0x3FU;
    const uint8_t day = raw[3] & 0x3FU;
    const uint8_t month = raw[5] & 0x1FU;
    if (!valid_bcd(seconds) || !valid_bcd(minutes) ||
        !valid_bcd(hours) || !valid_bcd(day) ||
        !valid_bcd(month) || !valid_bcd(raw[6])) {
        ESP_LOGE(
            TAG,
            "RTC PCF85063 rejected non-BCD calendar: raw=[%02x %02x %02x %02x %02x %02x %02x]",
            raw[0],
            raw[1],
            raw[2],
            raw[3],
            raw[4],
            raw[5],
            raw[6]
        );
        return ESP_ERR_INVALID_RESPONSE;
    }

    const LUNASHELL_calendar_time_t decoded = {
        .clock_integrity = (raw[0] & BIT(7)) == 0,
        .year = static_cast<uint16_t>(2000U + bcd_to_decimal(raw[6])),
        .month = bcd_to_decimal(month),
        .day = bcd_to_decimal(day),
        .hour = bcd_to_decimal(hours),
        .minute = bcd_to_decimal(minutes),
        .second = bcd_to_decimal(seconds),
    };
    if (!valid_rtc_register_calendar(decoded)) {
        ESP_LOGE(
            TAG,
            "RTC PCF85063 rejected calendar range: decoded=%04u-%02u-%02u %02u:%02u:%02u raw=[%02x %02x %02x %02x %02x %02x %02x]",
            decoded.year,
            decoded.month,
            decoded.day,
            decoded.hour,
            decoded.minute,
            decoded.second,
            raw[0],
            raw[1],
            raw[2],
            raw[3],
            raw[4],
            raw[5],
            raw[6]
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    *time = decoded;
    if (!decoded.clock_integrity) {
        ESP_LOGW(
            TAG,
            "RTC PCF85063 calendar integrity unavailable (oscillator-stop flag set): "
            "decoded=%04u-%02u-%02u %02u:%02u:%02u "
            "raw=[%02x %02x %02x %02x %02x %02x %02x]",
            decoded.year,
            decoded.month,
            decoded.day,
            decoded.hour,
            decoded.minute,
            decoded.second,
            raw[0],
            raw[1],
            raw[2],
            raw[3],
            raw[4],
            raw[5],
            raw[6]
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (!valid_calendar_time(decoded)) {
        ESP_LOGW(
            TAG,
            "RTC PCF85063 calendar is structurally valid but not initialized "
            "to a trusted epoch: decoded=%04u-%02u-%02u %02u:%02u:%02u",
            decoded.year,
            decoded.month,
            decoded.day,
            decoded.hour,
            decoded.minute,
            decoded.second
        );
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t publish_rtc_read(
    const LUNASHELL_calendar_time_t &time,
    esp_err_t error,
    int64_t attempted_at_us
)
{
    if (error != ESP_OK) {
        return update_snapshot(
            [time, error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                if (error == ESP_ERR_INVALID_STATE &&
                    valid_rtc_register_calendar(time)) {
                    snapshot.rtc.clock_integrity = time.clock_integrity;
                    snapshot.rtc.second = time.second;
                    snapshot.rtc.minute = time.minute;
                    snapshot.rtc.hour = time.hour;
                    snapshot.rtc.day = time.day;
                    snapshot.rtc.month = time.month;
                    snapshot.rtc.year = time.year;
                }
                mark_sample_failure(
                    snapshot.rtc,
                    error,
                    attempted_at_us
                );
            }
        );
    }

    LUNASHELL::RtcSnapshot sample;
    sample.clock_integrity = time.clock_integrity;
    sample.second = time.second;
    sample.minute = time.minute;
    sample.hour = time.hour;
    sample.day = time.day;
    sample.month = time.month;
    sample.year = time.year;
    mark_sample_success(sample, attempted_at_us);
    return update_snapshot(
        [&sample](LUNASHELL::DeviceSnapshot &snapshot) {
            const LUNASHELL::ComponentSnapshot system_time =
                snapshot.rtc.system_time;
            const bool system_time_valid =
                snapshot.rtc.system_time_valid;
            const bool synchronized = snapshot.rtc.synchronized;
            char source[sizeof(snapshot.rtc.source)] = {};
            char timezone[sizeof(snapshot.rtc.timezone)] = {};
            strlcpy(source, snapshot.rtc.source, sizeof(source));
            strlcpy(
                timezone,
                snapshot.rtc.timezone,
                sizeof(timezone)
            );
            snapshot.rtc = sample;
            snapshot.rtc.system_time = system_time;
            snapshot.rtc.system_time_valid = system_time_valid;
            snapshot.rtc.synchronized = synchronized;
            strlcpy(
                snapshot.rtc.source,
                source,
                sizeof(snapshot.rtc.source)
            );
            strlcpy(
                snapshot.rtc.timezone,
                timezone,
                sizeof(snapshot.rtc.timezone)
            );
        }
    );
}

esp_err_t sample_rtc(
    const char *phase,
    RtcLatchObservation *latch_observation
)
{
    if (latch_observation != nullptr) {
        *latch_observation = {};
    }
    if (high_frequency_sample_phase(phase) &&
        s_rtc_oscillator_stop_latched.load(std::memory_order_acquire)) {
        // The previous physical read already proved the OS condition. Keep the
        // real degraded result without repeating I2C traffic or pretending a
        // new sample attempt occurred; a trusted write performs the recovery.
        return ESP_ERR_INVALID_STATE;
    }
    // Serialize the complete read/observation sequence with direct reads and
    // trusted write+readback transactions. The GNSS path calls rtc_write later
    // in the same refresh, after this guard has been released.
    std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);
    if (high_frequency_sample_phase(phase) &&
        s_rtc_oscillator_stop_latched.load(std::memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    LUNASHELL_calendar_time_t time = {};
    const uint32_t trusted_write_generation_at_read =
        s_rtc_trusted_write_generation.load(std::memory_order_acquire);
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t error = read_rtc_calendar(&time);
    const esp_err_t publication_error =
        publish_rtc_read(time, error, attempted_at_us);
    const RtcLatchObservation observation = make_rtc_latch_observation(
        time,
        error,
        publication_error,
        trusted_write_generation_at_read
    );
    require_rtc_recovery_readback(observation);
    if (latch_observation != nullptr) {
        *latch_observation = observation;
    }
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "RTC PCF85063: %s sample failed (%s)", phase, esp_err_to_name(error));
        return sample_publication_result(
            "RTC PCF85063",
            phase,
            error,
            publication_error
        );
    }
    if (high_frequency_sample_phase(phase)) {
        ESP_LOGD(
            TAG,
            "RTC PCF85063 %s: %04u-%02u-%02u %02u:%02u:%02u, integrity=%s",
            phase,
            time.year,
            time.month,
            time.day,
            time.hour,
            time.minute,
            time.second,
            time.clock_integrity ? "valid" : "clock-reset"
        );
    } else {
        ESP_LOGI(
            TAG,
            "RTC PCF85063 %s: %04u-%02u-%02u %02u:%02u:%02u, integrity=%s",
            phase,
            time.year,
            time.month,
            time.day,
            time.hour,
            time.minute,
            time.second,
            time.clock_integrity ? "valid" : "clock-reset"
        );
    }
    return sample_publication_result(
        "RTC PCF85063",
        phase,
        ESP_OK,
        publication_error
    );
}

// Reject samples after a failed profile transition instead of publishing partial data.
esp_err_t read_imu_sample(ImuRawSample *sample)
{
    if (sample == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
    if (s_imu_ready && !s_imu_profile_valid) {
        return s_imu_profile_error;
    }
    qmi8658_data_t raw = {};
    const esp_err_t error = qmi8658_read_sensor_data(&s_imu, &raw);
    if (error != ESP_OK) {
        return error;
    }
    *sample = {
        .accelX = raw.accelX,
        .accelY = raw.accelY,
        .accelZ = raw.accelZ,
        .gyroX = raw.gyroX,
        .gyroY = raw.gyroY,
        .gyroZ = raw.gyroZ,
        .temperature = raw.temperature,
    };
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
// Log the exact QMI8658 register operation that rejected a profile transition.
esp_err_t imu_profile_step_result(
    const char *profile,
    const char *operation,
    esp_err_t error
)
{
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "IMU QMI8658 %s profile %s failed (%s)",
            profile,
            operation,
            esp_err_to_name(error)
        );
    }
    return error;
}

// Apply and read back one complete measured QMI8658 sampling profile.
esp_err_t configure_imu_profile(bool low_power)
{
    const char *profile = low_power ? "low-power" : "active";
    const qmi8658_accel_odr_t accel_odr = low_power
        ? QMI8658_ACCEL_ODR_31_25HZ
        : QMI8658_ACCEL_ODR_500HZ;
    const qmi8658_gyro_odr_t gyro_odr = low_power
        ? QMI8658_GYRO_ODR_31_25HZ
        : QMI8658_GYRO_ODR_500HZ;

    esp_err_t error = imu_profile_step_result(
        profile,
        "sensor-disable",
        qmi8658_enable_sensors(&s_imu, QMI8658_DISABLE_ALL)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "accelerometer-range",
        qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_8G)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "accelerometer-ODR",
        qmi8658_set_accel_odr(&s_imu, accel_odr)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "gyroscope-range",
        qmi8658_set_gyro_range(&s_imu, QMI8658_GYRO_RANGE_512DPS)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "gyroscope-ODR",
        qmi8658_set_gyro_odr(&s_imu, gyro_odr)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "low-pass-filter",
        qmi8658_write_register(&s_imu, QMI8658_CTRL5, 0x03)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "sensor-enable",
        qmi8658_enable_sensors(
            &s_imu,
            QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO
        )
    );
    if (error != ESP_OK) {
        return error;
    }

    uint8_t control_registers[6] = {};
    error = imu_profile_step_result(
        profile,
        "control-register-readback",
        qmi8658_read_register(
            &s_imu,
            QMI8658_CTRL2,
            control_registers,
            static_cast<uint8_t>(sizeof(control_registers))
        )
    );
    if (error != ESP_OK) {
        return error;
    }
    const uint8_t expected_ctrl2 =
        (static_cast<uint8_t>(QMI8658_ACCEL_RANGE_8G) << 4U) |
        static_cast<uint8_t>(accel_odr);
    const uint8_t expected_ctrl3 =
        (static_cast<uint8_t>(QMI8658_GYRO_RANGE_512DPS) << 4U) |
        static_cast<uint8_t>(gyro_odr);
    const uint8_t expected_ctrl5 = 0x03;
    const uint8_t expected_ctrl7 =
        QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO;
    if (control_registers[0] != expected_ctrl2 ||
        control_registers[1] != expected_ctrl3 ||
        control_registers[3] != expected_ctrl5 ||
        control_registers[5] != expected_ctrl7) {
        ESP_LOGE(
            TAG,
            "IMU QMI8658 %s profile readback rejected: CTRL2=0x%02x/0x%02x CTRL3=0x%02x/0x%02x CTRL5=0x%02x/0x%02x CTRL7=0x%02x/0x%02x",
            profile,
            control_registers[0],
            expected_ctrl2,
            control_registers[1],
            expected_ctrl3,
            control_registers[3],
            expected_ctrl5,
            control_registers[5],
            expected_ctrl7
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
#endif

// Publish a failed IMU profile transition as unavailable measured telemetry.
void publish_imu_profile_failure(esp_err_t error, int64_t attempted_at_us)
{
    const esp_err_t publication_error = update_snapshot(
        [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
            mark_sample_failure(snapshot.imu, error, attempted_at_us);
        }
    );
    log_snapshot_publication_failure(
        "IMU profile transition",
        publication_error
    );
}

// Initialize the QMI8658 in the canonical active sampling profile.
esp_err_t init_imu(i2c_master_bus_handle_t bus)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
    const uint8_t addresses[] = {QMI8658_ADDRESS_HIGH, QMI8658_ADDRESS_LOW};
    esp_err_t error = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        std::memset(&s_imu, 0, sizeof(s_imu));
        error = qmi8658_init(&s_imu, bus, addresses[i]);
        if (error == ESP_OK) {
            error = configure_imu_profile(false);
        }
        if (error == ESP_OK) {
            qmi8658_set_accel_unit_mps2(&s_imu, true);
            qmi8658_set_gyro_unit_dps(&s_imu, true);
            // Let the first filtered samples settle before calibration.
            vTaskDelay(pdMS_TO_TICKS(150));
            for (unsigned discard = 0; discard < 16; ++discard) {
                ImuRawSample ignored = {};
                error = read_imu_sample(&ignored);
                if (error != ESP_OK || !finite_imu_sample(ignored)) {
                    if (error == ESP_OK) {
                        error = ESP_ERR_INVALID_RESPONSE;
                    }
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
        if (error == ESP_OK) {
            s_imu_ready = true;
            s_imu_profile_low_power = false;
            s_imu_profile_valid = true;
            s_imu_profile_error = ESP_OK;
            ESP_LOGI(
                TAG,
                "IMU QMI8658: ready at 0x%02x; accel=8g/500Hz gyro=512dps/500Hz LPF enabled",
                addresses[i]
            );
            break;
        }
        if (s_imu.dev_handle != nullptr) {
            const esp_err_t remove_error =
                i2c_master_bus_rm_device(s_imu.dev_handle);
            if (remove_error != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "IMU probe cleanup failed at 0x%02x (%s)",
                    addresses[i],
                    esp_err_to_name(remove_error)
                );
                record_first_error(remove_error, &error);
                break;
            }
            s_imu.dev_handle = nullptr;
        }
    }
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.imu, error == ESP_OK, error);
    });
    if (!s_imu_ready) {
        s_imu_profile_valid = false;
        s_imu_profile_error = error;
        ESP_LOGW(TAG, "IMU QMI8658: not detected");
    }
    return error;
#else
    (void)bus;
    const esp_err_t error = ESP_ERR_NOT_SUPPORTED;
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.imu, false, error);
    });
    ESP_LOGI(TAG, "IMU: disabled by board hardware selection");
    return error;
#endif
}

esp_err_t calibrate_imu()
{
    const int64_t calibration_attempted_at_us = esp_timer_get_time();
    if (!s_imu_ready) {
        update_snapshot([calibration_attempted_at_us](
                            LUNASHELL::DeviceSnapshot &snapshot
                        ) {
            mark_sample_failure(
                snapshot.imu.calibration,
                ESP_ERR_INVALID_STATE,
                calibration_attempted_at_us
            );
        });
        return ESP_ERR_INVALID_STATE;
    }
    constexpr uint16_t sample_count = 96;
    constexpr float gravity = 9.80665F;
    float accel_sum[3] = {};
    float gyro_sum[3] = {};
    float accel_min[3] = {1000.0F, 1000.0F, 1000.0F};
    float accel_max[3] = {-1000.0F, -1000.0F, -1000.0F};
    float gyro_min[3] = {10000.0F, 10000.0F, 10000.0F};
    float gyro_max[3] = {-10000.0F, -10000.0F, -10000.0F};

    for (uint16_t index = 0; index < sample_count; ++index) {
        ImuRawSample data = {};
        esp_err_t error = read_imu_sample(&data);
        if (error == ESP_OK && !finite_imu_sample(data)) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
        if (error != ESP_OK) {
            const int64_t attempted_at_us = esp_timer_get_time();
            update_snapshot([error, attempted_at_us,
                             calibration_attempted_at_us](
                                LUNASHELL::DeviceSnapshot &snapshot
                            ) {
                mark_sample_failure(snapshot.imu, error, attempted_at_us);
                mark_sample_failure(
                    snapshot.imu.calibration,
                    error,
                    calibration_attempted_at_us
                );
            });
            ESP_LOGE(
                TAG,
                "IMU calibration sample %u failed (%s)",
                static_cast<unsigned>(index),
                esp_err_to_name(error)
            );
            return error;
        }
        const float acceleration[3] = {data.accelX, data.accelY, data.accelZ};
        const float angular_velocity[3] = {data.gyroX, data.gyroY, data.gyroZ};
        for (unsigned axis = 0; axis < 3; ++axis) {
            accel_sum[axis] += acceleration[axis];
            gyro_sum[axis] += angular_velocity[axis];
            if (acceleration[axis] < accel_min[axis]) {
                accel_min[axis] = acceleration[axis];
            }
            if (acceleration[axis] > accel_max[axis]) {
                accel_max[axis] = acceleration[axis];
            }
            if (angular_velocity[axis] < gyro_min[axis]) {
                gyro_min[axis] = angular_velocity[axis];
            }
            if (angular_velocity[axis] > gyro_max[axis]) {
                gyro_max[axis] = angular_velocity[axis];
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    float accel_mean[3] = {};
    float gyro_mean[3] = {};
    float magnitude_squared = 0.0F;
    float accel_max_span = 0.0F;
    float gyro_max_span = 0.0F;
    unsigned gravity_axis = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        accel_mean[axis] = accel_sum[axis] / sample_count;
        gyro_mean[axis] = gyro_sum[axis] / sample_count;
        magnitude_squared += accel_mean[axis] * accel_mean[axis];
        const float accel_span = accel_max[axis] - accel_min[axis];
        const float gyro_span = gyro_max[axis] - gyro_min[axis];
        if (accel_span > accel_max_span) {
            accel_max_span = accel_span;
        }
        if (gyro_span > gyro_max_span) {
            gyro_max_span = gyro_span;
        }
        const float axis_acceleration =
            accel_mean[axis] < 0.0F ? -accel_mean[axis] : accel_mean[axis];
        const float gravity_acceleration =
            accel_mean[gravity_axis] < 0.0F
                ? -accel_mean[gravity_axis]
                : accel_mean[gravity_axis];
        if (axis_acceleration > gravity_acceleration) {
            gravity_axis = axis;
        }
    }
    const float magnitude = __builtin_sqrtf(magnitude_squared);
    s_imu_calibration_samples = sample_count;
    s_imu_calibration_accel_max_span = accel_max_span;
    s_imu_calibration_gyro_max_span = gyro_max_span;
    s_imu_calibration_stationary =
        magnitude >= 8.0F && magnitude <= 12.0F &&
        accel_max_span <= 0.75F && gyro_max_span <= 8.0F;
    if (!s_imu_calibration_stationary) {
        update_snapshot([calibration_attempted_at_us](
                            LUNASHELL::DeviceSnapshot &snapshot
                        ) {
            snapshot.imu.calibration_stationary =
                s_imu_calibration_stationary;
            snapshot.imu.calibration_samples = s_imu_calibration_samples;
            snapshot.imu.calibration_accel_max_span_mps2 =
                s_imu_calibration_accel_max_span;
            snapshot.imu.calibration_gyro_max_span_dps =
                s_imu_calibration_gyro_max_span;
            mark_sample_failure(
                snapshot.imu.calibration,
                ESP_ERR_INVALID_STATE,
                calibration_attempted_at_us
            );
        });
        ESP_LOGW(
            TAG,
            "IMU calibration deferred: unstable sample set (|a|=%.2f accel-span=%.3f gyro-span=%.3f gyro-mean=[%.2f %.2f %.2f])",
            magnitude,
            accel_max_span,
            gyro_max_span,
            gyro_mean[0],
            gyro_mean[1],
            gyro_mean[2]
        );
        return ESP_ERR_INVALID_STATE;
    }

    for (unsigned axis = 0; axis < 3; ++axis) {
        s_accel_bias[axis] = accel_mean[axis];
        s_gyro_bias[axis] = gyro_mean[axis];
    }
    s_accel_bias[gravity_axis] -= accel_mean[gravity_axis] >= 0.0F ? gravity : -gravity;
    s_imu_calibrated = true;
    // A new bias must start a fresh filtered stream.
    reset_imu_filter();
    update_snapshot([calibration_attempted_at_us](
                        LUNASHELL::DeviceSnapshot &snapshot
                    ) {
        snapshot.imu.calibrated = s_imu_calibrated;
        snapshot.imu.calibration_stationary = s_imu_calibration_stationary;
        snapshot.imu.calibration_samples = s_imu_calibration_samples;
        snapshot.imu.calibration_accel_max_span_mps2 =
            s_imu_calibration_accel_max_span;
        snapshot.imu.calibration_gyro_max_span_dps =
            s_imu_calibration_gyro_max_span;
        for (unsigned axis = 0; axis < 3; ++axis) {
            snapshot.imu.acceleration_bias_mps2[axis] = s_accel_bias[axis];
            snapshot.imu.angular_velocity_bias_dps[axis] = s_gyro_bias[axis];
        }
        mark_sample_success(
            snapshot.imu.calibration,
            calibration_attempted_at_us
        );
    });
    ESP_LOGI(
        TAG,
        "IMU stationary calibration ready: samples=%u accel-span=%.3f gyro-span=%.3f accel-bias=[%.3f %.3f %.3f] gyro-bias=[%.3f %.3f %.3f]",
        sample_count,
        accel_max_span,
        gyro_max_span,
        s_accel_bias[0],
        s_accel_bias[1],
        s_accel_bias[2],
        s_gyro_bias[0],
        s_gyro_bias[1],
        s_gyro_bias[2]
    );
    return ESP_OK;
}

// Publish calibrated motion and optionally classify the same raw sample for rotation.
esp_err_t sample_imu(
    const char *phase,
    bool verbose = true,
    bool track_orientation = false
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
#if !CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    LUNASHELL::DeviceSnapshotService::instance().reportSensorFusion(
        {},
        ESP_ERR_NOT_SUPPORTED,
        attempted_at_us
    );
    const esp_err_t publication_error = update_snapshot(
        [attempted_at_us, track_orientation](LUNASHELL::DeviceSnapshot &snapshot) {
            mark_sample_failure(
                snapshot.imu,
                ESP_ERR_NOT_SUPPORTED,
                attempted_at_us
            );
            if (track_orientation) {
                s_orientation.gravity_valid = false;
                s_orientation.candidate_valid = false;
                s_orientation.sampled_at_us = attempted_at_us;
                mark_sample_failure(
                    s_orientation.component,
                    ESP_ERR_NOT_SUPPORTED,
                    attempted_at_us
                );
                copy_orientation_snapshot(snapshot.ui.orientation);
            }
        }
    );
    return sample_publication_result(
        "IMU QMI8658",
        phase,
        ESP_ERR_NOT_SUPPORTED,
        publication_error
    );
#endif
    if (!s_imu_smoothing_valid.load(std::memory_order_relaxed)) {
        LUNASHELL::DeviceSnapshotService::instance().reportSensorFusion(
            {},
            ESP_ERR_INVALID_STATE,
            attempted_at_us
        );
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us, track_orientation](
                LUNASHELL::DeviceSnapshot &snapshot
            ) {
                mark_sample_failure(
                    snapshot.imu,
                    ESP_ERR_INVALID_STATE,
                    attempted_at_us
                );
                if (track_orientation) {
                    s_orientation.gravity_valid = false;
                    s_orientation.candidate_valid = false;
                    s_orientation.sampled_at_us = attempted_at_us;
                    mark_sample_failure(
                        s_orientation.component,
                        ESP_ERR_INVALID_STATE,
                        attempted_at_us
                    );
                    copy_orientation_snapshot(snapshot.ui.orientation);
                }
            }
        );
        ESP_LOGE(TAG, "IMU QMI8658: smoothing setting is invalid");
        return sample_publication_result(
            "IMU QMI8658",
            phase,
            ESP_ERR_INVALID_STATE,
            publication_error
        );
    }

    LUNASHELL::ImuSnapshot sample;
    ImuRawSample data = {};
    esp_err_t error = read_imu_sample(&data);
    if (error == ESP_OK && !finite_imu_sample(data)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        sample.calibrated = s_imu_calibrated;
        sample.calibration_stationary = s_imu_calibration_stationary;
        sample.calibration_samples = s_imu_calibration_samples;
        sample.calibration_accel_max_span_mps2 = s_imu_calibration_accel_max_span;
        sample.calibration_gyro_max_span_dps = s_imu_calibration_gyro_max_span;
        const float acceleration[] = {data.accelX, data.accelY, data.accelZ};
        const float angular_velocity[] = {data.gyroX, data.gyroY, data.gyroZ};
        const float smoothing =
            static_cast<float>(
                s_imu_smoothing_percent.load(std::memory_order_relaxed)
            ) / 100.0F;
        for (unsigned axis = 0; axis < 3; ++axis) {
            sample.acceleration_bias_mps2[axis] = s_accel_bias[axis];
            sample.angular_velocity_bias_dps[axis] = s_gyro_bias[axis];
            const float acceleration_corrected =
                acceleration[axis] - (s_imu_calibrated ? s_accel_bias[axis] : 0.0F);
            const float angular_velocity_corrected =
                angular_velocity[axis] - (s_imu_calibrated ? s_gyro_bias[axis] : 0.0F);
            if (!s_imu_filter_ready) {
                s_accel_filtered[axis] = acceleration_corrected;
                s_gyro_filtered[axis] = angular_velocity_corrected;
            } else {
                s_accel_filtered[axis] =
                    s_accel_filtered[axis] * smoothing +
                    acceleration_corrected * (1.0F - smoothing);
                s_gyro_filtered[axis] =
                    s_gyro_filtered[axis] * smoothing +
                    angular_velocity_corrected * (1.0F - smoothing);
            }
            sample.acceleration_mps2[axis] = s_accel_filtered[axis];
            sample.angular_velocity_dps[axis] = s_gyro_filtered[axis];
        }
        s_imu_filter_ready = true;
        sample.temperature_c = data.temperature;
        if (track_orientation) {
            process_orientation_sample(data, attempted_at_us);
        }
    }
    if (error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportSensorFusion(
            {},
            error,
            attempted_at_us
        );
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us, track_orientation](
                LUNASHELL::DeviceSnapshot &snapshot
            ) {
                mark_sample_failure(snapshot.imu, error, attempted_at_us);
                if (track_orientation) {
                    s_orientation.gravity_valid = false;
                    s_orientation.candidate_valid = false;
                    s_orientation.sampled_at_us = attempted_at_us;
                    mark_sample_failure(
                        s_orientation.component,
                        error,
                        attempted_at_us
                    );
                    copy_orientation_snapshot(snapshot.ui.orientation);
                }
            }
        );
        const int64_t now = esp_timer_get_time();
        if (verbose || now - s_orientation.last_error_log_us >= 5000000) {
            s_orientation.last_error_log_us = now;
            ESP_LOGW(
                TAG,
                "IMU QMI8658: %s sample failed (%s)",
                phase,
                esp_err_to_name(error)
            );
        }
        return sample_publication_result(
            "IMU QMI8658",
            phase,
            error,
            publication_error
        );
    }
    mark_sample_success(sample, attempted_at_us);
    const esp_err_t publication_error = update_snapshot(
        [&sample, track_orientation](LUNASHELL::DeviceSnapshot &snapshot) {
            const LUNASHELL::ComponentSnapshot calibration =
                snapshot.imu.calibration;
            snapshot.imu = sample;
            snapshot.imu.calibration = calibration;
            if (track_orientation) {
                copy_orientation_snapshot(snapshot.ui.orientation);
            }
        }
    );
    if (verbose) {
        if (high_frequency_sample_phase(phase)) {
            ESP_LOGD(
                TAG,
                "IMU QMI8658 %s: accel=[%.2f %.2f %.2f]m/s2 gyro=[%.2f %.2f %.2f]dps temp=%.1fC",
                phase,
                sample.acceleration_mps2[0],
                sample.acceleration_mps2[1],
                sample.acceleration_mps2[2],
                sample.angular_velocity_dps[0],
                sample.angular_velocity_dps[1],
                sample.angular_velocity_dps[2],
                data.temperature
            );
        } else {
            ESP_LOGI(
                TAG,
                "IMU QMI8658 %s: accel=[%.2f %.2f %.2f]m/s2 gyro=[%.2f %.2f %.2f]dps temp=%.1fC",
                phase,
                sample.acceleration_mps2[0],
                sample.acceleration_mps2[1],
                sample.acceleration_mps2[2],
                sample.angular_velocity_dps[0],
                sample.angular_velocity_dps[1],
                sample.angular_velocity_dps[2],
                data.temperature
            );
        }
    }
    LUNASHELL::SensorFusionResult fusion_result;
    const esp_err_t fusion_error =
        LUNASHELL::SensorFusionService::instance().process(sample, &fusion_result);
    LUNASHELL::DeviceSnapshotService::instance().reportSensorFusion(
        fusion_result,
        fusion_error,
        attempted_at_us
    );
    const esp_err_t sample_result = sample_publication_result(
        "IMU QMI8658",
        phase,
        ESP_OK,
        publication_error
    );
    if (sample_result != ESP_OK) {
        return sample_result;
    }
    if (fusion_error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "IMU sensor-fusion %s failed (%s)",
            phase,
            esp_err_to_name(fusion_error)
        );
        return fusion_error;
    }
    return ESP_OK;
}

void imu_calibration_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_imu_calibration_request_pending.load(std::memory_order_acquire)) {
            continue;
        }

        struct PendingCalibrationReset {
            std::atomic_bool &pending;
            ~PendingCalibrationReset()
            {
                pending.store(false, std::memory_order_release);
            }
        } pending_reset{s_imu_calibration_request_pending};

        const int64_t calibration_attempted_at_us = esp_timer_get_time();
        SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
        if (refresh_mutex == nullptr) {
            ESP_LOGE(
                TAG,
                "IMU calibration worker failed: refresh mutex allocation failed"
            );
            publish_imu_calibration_failure(
                ESP_ERR_NO_MEM,
                calibration_attempted_at_us
            );
            continue;
        }
        if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(
                TAG,
                "IMU calibration rejected: another refresh is still running"
            );
            publish_imu_calibration_failure(
                ESP_ERR_TIMEOUT,
                calibration_attempted_at_us
            );
            continue;
        }

        esp_err_t final_result = calibrate_imu();
        if (final_result == ESP_OK) {
            final_result = sample_imu("post-calibration");
        }
        const esp_err_t release_error = give_refresh_mutex(
            refresh_mutex,
            "IMU calibration"
        );
        record_first_error(release_error, &final_result);
    }
}

TaskHandle_t ensure_imu_calibration_task()
{
    TaskHandle_t task = s_imu_calibration_task.load(std::memory_order_acquire);
    if (task != nullptr) {
        return task;
    }

    task = xTaskCreateStatic(
        imu_calibration_task,
        "LUNASHELL_imu_cal",
        sizeof(s_imu_calibration_task_stack) /
            sizeof(s_imu_calibration_task_stack[0]),
        nullptr,
        3,
        s_imu_calibration_task_stack,
        &s_imu_calibration_task_control
    );
    if (task != nullptr) {
        s_imu_calibration_task.store(task, std::memory_order_release);
    }
    return task;
}

esp_err_t init_gps(i2c_master_bus_handle_t bus)
{
    esp_err_t error = add_i2c_device(bus, LC76G_WRITE_ADDRESS, 100000, &s_gps_control);
    if (error == ESP_OK) {
        error = add_i2c_device(bus, LC76G_READ_ADDRESS, 100000, &s_gps_read);
    }
    if (error == ESP_OK) {
        error = add_i2c_device(
            bus,
            LC76G_DATA_WRITE_ADDRESS,
            100000,
            &s_gps_write
        );
    }
    if (error == ESP_OK) {
        const esp_err_t write_probe = i2c_master_probe(bus, LC76G_WRITE_ADDRESS, I2C_TIMEOUT_MS);
        ESP_LOGI(
            TAG,
            "GPS LC76G control endpoint 0x50: %s; read endpoint 0x54 is command-gated",
            esp_err_to_name(write_probe)
        );
        if (write_probe != ESP_OK) {
            error = ESP_ERR_NOT_FOUND;
        }
    }
    s_gps_ready = error == ESP_OK;
    s_gps_power_suspended = false;
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.gps, error == ESP_OK, error);
    });
    if (!s_gps_ready) {
        ESP_LOGW(TAG, "GPS LC76G: initialization failed (%s)", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(
        TAG,
        "GPS LC76G: I2C bridge configured (control=0x50 read=0x54 data-write=0x58)"
    );
    return ESP_OK;
}

esp_err_t gps_control_write(const uint8_t *data, size_t length)
{
    if (data == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit(s_gps_control, data, length, 1000);
}

// The LC76G I2C bridge requires a quiet phase after a read response before
// accepting the next control frame (including 0xAA531000 for host writes).
constexpr TickType_t kGpsI2cProtocolDelayTicks = pdMS_TO_TICKS(10);

esp_err_t gps_exchange(
    const uint8_t *command,
    size_t command_length,
    uint8_t *data,
    size_t data_length,
    const char *step
)
{
    esp_err_t error = ESP_FAIL;
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        error = gps_control_write(command, command_length);
        if (error == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            error = i2c_master_receive(s_gps_read, data, data_length, 1000);
        }
        if (error == ESP_OK) {
            // Do not begin a new LC76G I2C phase immediately after the 0x54
            // read. The module NACKs the following 0x50 control transaction
            // otherwise, which used to abort transactional Device Standby.
            vTaskDelay(kGpsI2cProtocolDelayTicks);
            return ESP_OK;
        }
        ESP_LOGW(
            TAG,
            "GPS LC76G: %s exchange attempt %u/3 failed (%s)",
            step,
            attempt,
            esp_err_to_name(error)
        );
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return error;
}

bool decimal_digits(const char *value, size_t count)
{
    if (value == nullptr) {
        return false;
    }
    for (size_t index = 0; index < count; ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    return true;
}

int hexadecimal_digit(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

bool validate_and_strip_nmea_checksum(char *sentence, size_t length)
{
    if (sentence == nullptr || length < 5 || sentence[0] != '$') {
        return false;
    }
    char *checksum_marker = static_cast<char *>(
        std::memchr(sentence, '*', length)
    );
    if (checksum_marker == nullptr ||
        checksum_marker + 3 != sentence + length) {
        return false;
    }
    const int high = hexadecimal_digit(checksum_marker[1]);
    const int low = hexadecimal_digit(checksum_marker[2]);
    if (high < 0 || low < 0) {
        return false;
    }
    uint8_t calculated = 0;
    for (char *cursor = sentence + 1; cursor < checksum_marker; ++cursor) {
        calculated ^= static_cast<uint8_t>(*cursor);
    }
    const uint8_t supplied = static_cast<uint8_t>((high << 4) | low);
    if (calculated != supplied) {
        return false;
    }
    *checksum_marker = '\0';
    return true;
}

bool nmea_time_valid(const char *value)
{
    if (value == nullptr || std::strlen(value) < 6 ||
        !decimal_digits(value, 6)) {
        return false;
    }
    const uint8_t hour = static_cast<uint8_t>(
        (value[0] - '0') * 10 + (value[1] - '0')
    );
    const uint8_t minute = static_cast<uint8_t>(
        (value[2] - '0') * 10 + (value[3] - '0')
    );
    const uint8_t second = static_cast<uint8_t>(
        (value[4] - '0') * 10 + (value[5] - '0')
    );
    if (hour > 23U || minute > 59U || second > 59U) {
        return false;
    }
    if (value[6] == '\0') {
        return true;
    }
    if (value[6] != '.' || value[7] == '\0') {
        return false;
    }
    for (const char *cursor = value + 7; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
    }
    return true;
}

bool parse_float_field(const char *value, float *parsed)
{
    if (value == nullptr || value[0] == '\0' || parsed == nullptr) {
        return false;
    }
    char *end = nullptr;
    const float result = std::strtof(value, &end);
    if (end == value || end == nullptr || *end != '\0' ||
        !std::isfinite(result)) {
        return false;
    }
    *parsed = result;
    return true;
}

bool parse_uint8_field(const char *value, uint8_t *parsed)
{
    if (value == nullptr || value[0] == '\0' || parsed == nullptr) {
        return false;
    }
    char *end = nullptr;
    const unsigned long result = std::strtoul(value, &end, 10);
    if (end == value || end == nullptr || *end != '\0' || result > UINT8_MAX) {
        return false;
    }
    *parsed = static_cast<uint8_t>(result);
    return true;
}

bool nmea_coordinate(
    const char *value,
    char hemisphere,
    bool latitude,
    double *coordinate
)
{
    if (value == nullptr || value[0] == '\0' || coordinate == nullptr) {
        return false;
    }
    if ((latitude && hemisphere != 'N' && hemisphere != 'S') ||
        (!latitude && hemisphere != 'E' && hemisphere != 'W')) {
        return false;
    }
    char *end = nullptr;
    const double encoded = std::strtod(value, &end);
    if (end == value || end == nullptr || *end != '\0' ||
        !std::isfinite(encoded) || encoded < 0.0) {
        return false;
    }
    const int degrees = static_cast<int>(encoded / 100.0);
    const double minutes = encoded - degrees * 100.0;
    const int maximum_degrees = latitude ? 90 : 180;
    if (degrees > maximum_degrees || minutes < 0.0 || minutes >= 60.0 ||
        (degrees == maximum_degrees && minutes != 0.0)) {
        return false;
    }
    double result = static_cast<double>(degrees) + minutes / 60.0;
    if (hemisphere == 'S' || hemisphere == 'W') {
        result = -result;
    }
    *coordinate = result;
    return true;
}

enum class NmeaSentenceKind : uint8_t {
    Unsupported,
    Gga,
    Rmc,
};

struct NmeaParseState {
    bool saw_gga = false;
    bool gga_has_fix = false;
    bool saw_rmc = false;
    bool rmc_active = false;
};

struct NmeaPayloadResult {
    size_t valid_sentence_count = 0;
    size_t malformed_supported_sentence_count = 0;
    const char *coherence_error = nullptr;
};

NmeaSentenceKind nmea_sentence_kind(const char *sentence, size_t length)
{
    if (sentence == nullptr || length < 6 || sentence[0] != '$') {
        return NmeaSentenceKind::Unsupported;
    }
    size_t type_end = 1;
    while (type_end < length && sentence[type_end] != ',' &&
           sentence[type_end] != '*') {
        ++type_end;
    }
    if (type_end < 4) {
        return NmeaSentenceKind::Unsupported;
    }
    const char *suffix = sentence + type_end - 3U;
    if (std::memcmp(suffix, "GGA", 3) == 0) {
        return NmeaSentenceKind::Gga;
    }
    if (std::memcmp(suffix, "RMC", 3) == 0) {
        return NmeaSentenceKind::Rmc;
    }
    return NmeaSentenceKind::Unsupported;
}

bool parse_nmea_sentence(
    const char *sentence,
    size_t length,
    LUNASHELL::GpsSnapshot &sample,
    NmeaParseState &state,
    bool *supported
)
{
    const NmeaSentenceKind kind = nmea_sentence_kind(sentence, length);
    if (supported != nullptr) {
        *supported = kind != NmeaSentenceKind::Unsupported;
    }
    if (kind == NmeaSentenceKind::Unsupported) {
        return false;
    }

    char line[192] = {};
    if (length >= sizeof(line)) {
        return false;
    }
    const size_t copy_length = length;
    std::memcpy(line, sentence, copy_length);
    if (!validate_and_strip_nmea_checksum(line, copy_length)) {
        return false;
    }

    char *fields[20] = {line};
    size_t field_count = 1;
    for (size_t index = 0; index < copy_length &&
                           field_count < sizeof(fields) / sizeof(fields[0]);
         ++index) {
        if (line[index] == ',') {
            line[index] = '\0';
            fields[field_count++] = &line[index + 1U];
        }
    }
    if (field_count < 2 || fields[0] == nullptr) {
        return false;
    }

    const size_t type_length = std::strlen(fields[0]);
    const char *type = type_length >= 3 ? fields[0] + type_length - 3 : fields[0];
    if (kind == NmeaSentenceKind::Gga &&
        std::strcmp(type, "GGA") == 0 && field_count >= 10) {
        uint8_t fix_quality = 0;
        uint8_t satellites = 0;
        if (!nmea_time_valid(fields[1]) ||
            !parse_uint8_field(fields[6], &fix_quality) ||
            !parse_uint8_field(fields[7], &satellites)) {
            return false;
        }
        strlcpy(sample.utc_time, fields[1], sizeof(sample.utc_time));
        sample.fix_quality = fix_quality;
        sample.satellites = satellites;
        state.saw_gga = true;
        state.gga_has_fix = fix_quality > 0U;
        if (fix_quality == 0U) {
            return true;
        }
        if (satellites == 0U) {
            return false;
        }

        float hdop = 0.0F;
        float altitude_m = 0.0F;
        double latitude = 0.0;
        double longitude = 0.0;
        if (!parse_float_field(fields[8], &hdop) || hdop < 0.0F ||
            !parse_float_field(fields[9], &altitude_m) ||
            !nmea_coordinate(
                fields[2],
                fields[3] != nullptr ? fields[3][0] : '\0',
                true,
                &latitude
            ) ||
            !nmea_coordinate(
                fields[4],
                fields[5] != nullptr ? fields[5][0] : '\0',
                false,
                &longitude
            )) {
            return false;
        }
        sample.hdop = hdop;
        sample.altitude_m = altitude_m;
        sample.latitude_degrees = latitude;
        sample.longitude_degrees = longitude;
        return true;
    } else if (kind == NmeaSentenceKind::Rmc &&
               std::strcmp(type, "RMC") == 0 && field_count >= 10) {
        if (fields[2] == nullptr ||
            (fields[2][0] != 'A' && fields[2][0] != 'V') ||
            fields[2][1] != '\0') {
            return false;
        }
        if (fields[2][0] == 'V') {
            state.saw_rmc = true;
            state.rmc_active = false;
            sample.utc_valid = false;
            return true;
        }
        if (!nmea_time_valid(fields[1]) ||
            fields[9] == nullptr ||
            std::strlen(fields[9]) != 6 ||
            !decimal_digits(fields[9], 6)) {
            return false;
        }

        const unsigned day = static_cast<unsigned>(
            (fields[9][0] - '0') * 10 + (fields[9][1] - '0')
        );
        const unsigned month = static_cast<unsigned>(
            (fields[9][2] - '0') * 10 + (fields[9][3] - '0')
        );
        const unsigned year = 2000U + static_cast<unsigned>(
            (fields[9][4] - '0') * 10 + (fields[9][5] - '0')
        );
        const LUNASHELL_calendar_time_t utc = {
            .clock_integrity = true,
            .year = static_cast<uint16_t>(year),
            .month = static_cast<uint8_t>(month),
            .day = static_cast<uint8_t>(day),
            .hour = static_cast<uint8_t>(
                (fields[1][0] - '0') * 10 + (fields[1][1] - '0')
            ),
            .minute = static_cast<uint8_t>(
                (fields[1][2] - '0') * 10 + (fields[1][3] - '0')
            ),
            .second = static_cast<uint8_t>(
                (fields[1][4] - '0') * 10 + (fields[1][5] - '0')
            ),
        };
        double latitude = 0.0;
        double longitude = 0.0;
        float speed_knots = 0.0F;
        float course_degrees = 0.0F;
        if (!valid_calendar_time(utc) ||
            !parse_float_field(fields[7], &speed_knots) ||
            speed_knots < 0.0F ||
            !parse_float_field(fields[8], &course_degrees) ||
            course_degrees < 0.0F || course_degrees > 360.0F ||
            !nmea_coordinate(
                fields[3],
                fields[4] != nullptr ? fields[4][0] : '\0',
                true,
                &latitude
            ) ||
            !nmea_coordinate(
                fields[5],
                fields[6] != nullptr ? fields[6][0] : '\0',
                false,
                &longitude
            )) {
            return false;
        }
        strlcpy(sample.utc_time, fields[1], sizeof(sample.utc_time));
        std::snprintf(
            sample.utc_date,
            sizeof(sample.utc_date),
            "%04u-%02u-%02u",
            year,
            month,
            day
        );
        sample.utc_valid = true;
        state.saw_rmc = true;
        state.rmc_active = true;
        sample.latitude_degrees = latitude;
        sample.longitude_degrees = longitude;
        sample.speed_mps = speed_knots * 0.514444F;
        sample.course_degrees = course_degrees;
        return true;
    }
    return false;
}

NmeaPayloadResult parse_nmea_payload(
    const char *payload,
    size_t length,
    LUNASHELL::GpsSnapshot &sample
)
{
    NmeaPayloadResult result;
    NmeaParseState state;
    size_t cursor = 0;
    bool copied_first = false;
    while (cursor < length) {
        while (cursor < length && payload[cursor] != '$') {
            ++cursor;
        }
        const size_t start = cursor;
        while (cursor < length && payload[cursor] != '\r' && payload[cursor] != '\n') {
            ++cursor;
        }
        if (start < cursor) {
            const size_t sentence_length = cursor - start;
            bool supported = false;
            if (parse_nmea_sentence(
                    payload + start,
                    sentence_length,
                    sample,
                    state,
                    &supported
                )) {
                ++result.valid_sentence_count;
            } else if (supported) {
                ++result.malformed_supported_sentence_count;
            }
            if (!copied_first && result.valid_sentence_count > 0) {
                const size_t output_length =
                    sentence_length < sizeof(sample.first_sentence) - 1U
                        ? sentence_length
                        : sizeof(sample.first_sentence) - 1U;
                std::memcpy(sample.first_sentence, payload + start, output_length);
                sample.first_sentence[output_length] = '\0';
                copied_first = true;
            }
        }
        while (cursor < length && (payload[cursor] == '\r' || payload[cursor] == '\n')) {
            ++cursor;
        }
    }

    if (result.malformed_supported_sentence_count > 0) {
        result.coherence_error = "supported NMEA sentence malformed";
    } else if (state.saw_rmc && !state.saw_gga) {
        result.coherence_error =
            "RMC status has no GGA fix-quality sentence";
    } else if (state.saw_gga && !state.saw_rmc) {
        result.coherence_error =
            "GGA fix quality has no RMC status sentence";
    } else if (state.saw_gga && state.saw_rmc &&
               state.gga_has_fix != state.rmc_active) {
        result.coherence_error =
            "GGA fix quality contradicts RMC status";
    } else {
        sample.has_fix =
            state.saw_gga && state.gga_has_fix &&
            state.saw_rmc && state.rmc_active;
    }
    return result;
}

constexpr uint8_t kGpsPendingLengthCommand[] = {
    0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00,
};
constexpr uint8_t kGpsWriteFreeLengthCommand[] = {
    0x04, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00,
};
constexpr uint8_t kGpsWriteConfigurationCommand[] = {
    0x00, 0x10, 0x53, 0xAA,
};
constexpr unsigned kGpsPairAckAttempts = 10;
constexpr TickType_t kGpsPairAckPollTicks = pdMS_TO_TICKS(100);

uint32_t gps_le_u32(const uint8_t value[4])
{
    return static_cast<uint32_t>(value[0]) |
           (static_cast<uint32_t>(value[1]) << 8) |
           (static_cast<uint32_t>(value[2]) << 16) |
           (static_cast<uint32_t>(value[3]) << 24);
}

esp_err_t gps_read_u32(
    const uint8_t command[8],
    const char *step,
    uint32_t *value
)
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t response[4] = {};
    const esp_err_t error = gps_exchange(
        command,
        8,
        response,
        sizeof(response),
        step
    );
    if (error != ESP_OK) {
        return error;
    }
    *value = gps_le_u32(response);
    return ESP_OK;
}

esp_err_t gps_read_pending_payload(uint32_t *data_length)
{
    if (data_length == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = gps_read_u32(
        kGpsPendingLengthCommand,
        "pending length",
        data_length
    );
    if (error != ESP_OK || *data_length == 0U) {
        return error;
    }
    if (*data_length > GPS_MAX_PENDING_BYTES) {
        ESP_LOGE(
            TAG,
            "GPS LC76G: pending payload length %lu exceeds safety limit",
            static_cast<unsigned long>(*data_length)
        );
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t read_length =
        *data_length < GPS_MAX_PAYLOAD ? *data_length : GPS_MAX_PAYLOAD;
    const uint8_t request[8] = {
        0x00, 0x20, 0x51, 0xAA,
        static_cast<uint8_t>(read_length),
        static_cast<uint8_t>(read_length >> 8),
        static_cast<uint8_t>(read_length >> 16),
        static_cast<uint8_t>(read_length >> 24),
    };
    error = gps_exchange(
        request,
        sizeof(request),
        reinterpret_cast<uint8_t *>(s_gps_payload),
        read_length,
        "pending payload"
    );
    if (error == ESP_OK) {
        s_gps_payload[read_length] = '\0';
        *data_length = read_length;
    }
    return error;
}

enum class GpsPairAck : uint8_t {
    None,
    Processing,
    Success,
    Rejected,
};

GpsPairAck gps_find_pair_ack(
    const char *payload,
    size_t length,
    uint16_t command_id
)
{
    if (payload == nullptr) {
        return GpsPairAck::None;
    }
    // The LC76G may place the in-progress and final PAIR001 responses in one
    // I2C payload. Retain an in-progress state, but keep scanning so the final
    // acknowledgement in the same drained payload is never discarded.
    GpsPairAck observed = GpsPairAck::None;
    size_t cursor = 0;
    while (cursor < length) {
        while (cursor < length && payload[cursor] != '$') {
            ++cursor;
        }
        const size_t start = cursor;
        while (cursor < length && payload[cursor] != '\r' &&
               payload[cursor] != '\n') {
            ++cursor;
        }
        const size_t sentence_length = cursor - start;
        if (sentence_length > 0U && sentence_length < 64U) {
            char sentence[64] = {};
            std::memcpy(sentence, payload + start, sentence_length);
            if (validate_and_strip_nmea_checksum(sentence, sentence_length) &&
                std::strncmp(sentence, "$PAIR001,", 9) == 0) {
                const char *command_text = sentence + 9;
                char *command_end = nullptr;
                const unsigned long acknowledged_command = std::strtoul(
                    command_text,
                    &command_end,
                    10
                );
                if (command_end != command_text && command_end != nullptr &&
                    *command_end == ',' &&
                    acknowledged_command == command_id &&
                    command_end[1] >= '0' && command_end[1] <= '5' &&
                    command_end[2] == '\0') {
                    if (command_end[1] == '0') {
                        return GpsPairAck::Success;
                    }
                    if (command_end[1] == '1') {
                        observed = GpsPairAck::Processing;
                        continue;
                    }
                    ESP_LOGE(
                        TAG,
                        "GPS LC76G: PAIR%03u rejected with result %c",
                        static_cast<unsigned>(command_id),
                        command_end[1]
                    );
                    return GpsPairAck::Rejected;
                }
            }
        }
        while (cursor < length && (payload[cursor] == '\r' ||
                                   payload[cursor] == '\n')) {
            ++cursor;
        }
    }
    return observed;
}

esp_err_t gps_send_pair_command(
    uint16_t command_id,
    const char *sentence,
    const char *operation
)
{
    if (sentence == nullptr || operation == nullptr || s_gps_control == nullptr ||
        s_gps_read == nullptr || s_gps_write == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t sentence_length = std::strlen(sentence);
    if (sentence_length == 0U || sentence_length > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t write_free_length = 0;
    esp_err_t error = gps_read_u32(
        kGpsWriteFreeLengthCommand,
        "write free length",
        &write_free_length
    );
    if (error != ESP_OK) {
        return error;
    }
    if (write_free_length < sentence_length) {
        ESP_LOGE(
            TAG,
            "GPS LC76G: %s rejected; write buffer=%lu bytes, command=%u bytes",
            operation,
            static_cast<unsigned long>(write_free_length),
            static_cast<unsigned>(sentence_length)
        );
        return ESP_ERR_NO_MEM;
    }

    uint8_t write_configuration[8] = {
        kGpsWriteConfigurationCommand[0],
        kGpsWriteConfigurationCommand[1],
        kGpsWriteConfigurationCommand[2],
        kGpsWriteConfigurationCommand[3],
        static_cast<uint8_t>(sentence_length),
        static_cast<uint8_t>(sentence_length >> 8),
        static_cast<uint8_t>(sentence_length >> 16),
        static_cast<uint8_t>(sentence_length >> 24),
    };
    error = gps_control_write(write_configuration, sizeof(write_configuration));
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GPS LC76G: %s write configuration failed (%s)",
            operation,
            esp_err_to_name(error)
        );
        return error;
    }
    vTaskDelay(kGpsI2cProtocolDelayTicks);
    error = i2c_master_transmit(
        s_gps_write,
        reinterpret_cast<const uint8_t *>(sentence),
        sentence_length,
        I2C_TIMEOUT_MS
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GPS LC76G: %s command write failed (%s)",
            operation,
            esp_err_to_name(error)
        );
        return error;
    }

    for (unsigned attempt = 1; attempt <= kGpsPairAckAttempts; ++attempt) {
        vTaskDelay(kGpsPairAckPollTicks);
        uint32_t payload_length = 0;
        error = gps_read_pending_payload(&payload_length);
        if (error != ESP_OK) {
            ESP_LOGW(
                TAG,
                "GPS LC76G: %s acknowledgement poll %u/%u failed (%s)",
                operation,
                attempt,
                kGpsPairAckAttempts,
                esp_err_to_name(error)
            );
            continue;
        }
        if (payload_length == 0U) {
            continue;
        }
        const GpsPairAck acknowledgement = gps_find_pair_ack(
            s_gps_payload,
            payload_length,
            command_id
        );
        if (acknowledgement == GpsPairAck::Success) {
            return ESP_OK;
        }
        if (acknowledgement == GpsPairAck::Rejected) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    ESP_LOGE(
        TAG,
        "GPS LC76G: %s did not receive final PAIR%03u acknowledgement",
        operation,
        static_cast<unsigned>(command_id)
    );
    return error == ESP_OK ? ESP_ERR_TIMEOUT : error;
}

esp_err_t sample_gps(const char *phase)
{
    static const uint8_t length_command[] = {0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00};
    LUNASHELL::GpsSnapshot sample;
    const int64_t attempted_at_us = esp_timer_get_time();
    if (s_gps_power_suspended) {
        const esp_err_t error = ESP_ERR_INVALID_STATE;
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(snapshot.gps, error, attempted_at_us);
            }
        );
        ESP_LOGE(TAG, "GPS LC76G: %s sample rejected while GNSS is suspended", phase);
        return sample_publication_result(
            "GPS LC76G",
            phase,
            error,
            publication_error
        );
    }
    uint8_t length_bytes[4] = {};

    esp_err_t error = gps_exchange(
        length_command,
        sizeof(length_command),
        length_bytes,
        sizeof(length_bytes),
        "length"
    );
    const uint32_t data_length =
        static_cast<uint32_t>(length_bytes[0]) |
        (static_cast<uint32_t>(length_bytes[1]) << 8) |
        (static_cast<uint32_t>(length_bytes[2]) << 16) |
        (static_cast<uint32_t>(length_bytes[3]) << 24);
    sample.pending_nmea_bytes = data_length;
    const uint32_t read_length =
        data_length < GPS_MAX_PAYLOAD ? data_length : GPS_MAX_PAYLOAD;

    if (error == ESP_OK && data_length > GPS_MAX_PENDING_BYTES) {
        ESP_LOGW(
            TAG,
            "GPS LC76G: invalid pending length=%lu raw=[%02x %02x %02x %02x]",
            static_cast<unsigned long>(data_length),
            length_bytes[0],
            length_bytes[1],
            length_bytes[2],
            length_bytes[3]
        );
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK && data_length > GPS_MAX_PAYLOAD) {
        ESP_LOGI(
            TAG,
            "GPS LC76G: %lu bytes pending; draining a bounded %u-byte chunk",
            static_cast<unsigned long>(data_length),
            GPS_MAX_PAYLOAD
        );
    }
    if (error == ESP_OK && read_length > 0) {
        const uint8_t request[8] = {
            0x00, 0x20, 0x51, 0xAA,
            static_cast<uint8_t>(read_length),
            static_cast<uint8_t>(read_length >> 8),
            static_cast<uint8_t>(read_length >> 16),
            static_cast<uint8_t>(read_length >> 24),
        };
        error = gps_exchange(
            request,
            sizeof(request),
            reinterpret_cast<uint8_t *>(s_gps_payload),
            read_length,
            "payload"
        );
        if (error == ESP_OK) {
            s_gps_payload[read_length] = '\0';
            const NmeaPayloadResult parse_result =
                parse_nmea_payload(s_gps_payload, read_length, sample);
            if (parse_result.valid_sentence_count == 0) {
                ESP_LOGE(
                    TAG,
                    "GPS LC76G: payload has no valid supported NMEA sentence"
                );
                error = ESP_ERR_INVALID_RESPONSE;
            } else if (parse_result.coherence_error != nullptr) {
                ESP_LOGE(
                    TAG,
                    "GPS LC76G: incoherent NMEA payload: %s "
                    "(valid=%u malformed=%u)",
                    parse_result.coherence_error,
                    static_cast<unsigned>(parse_result.valid_sentence_count),
                    static_cast<unsigned>(
                        parse_result.malformed_supported_sentence_count
                    )
                );
                error = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    if (error != ESP_OK) {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(snapshot.gps, error, attempted_at_us);
            }
        );
        ESP_LOGW(TAG, "GPS LC76G: %s sample failed (%s)", phase, esp_err_to_name(error));
        return sample_publication_result(
            "GPS LC76G",
            phase,
            error,
            publication_error
        );
    }
    mark_sample_success(sample, attempted_at_us);
    const esp_err_t publication_error = update_snapshot(
        [&sample](LUNASHELL::DeviceSnapshot &snapshot) {
            snapshot.gps = sample;
        }
    );
    esp_err_t aggregate = sample_publication_result(
        "GPS LC76G",
        phase,
        ESP_OK,
        publication_error
    );
    if (high_frequency_sample_phase(phase)) {
        ESP_LOGD(
            TAG,
            "GPS LC76G %s: read=%lu pending-before=%lu byte(s), fix=%s",
            phase,
            static_cast<unsigned long>(read_length),
            static_cast<unsigned long>(data_length),
            sample.has_fix ? "yes" : "no"
        );
    } else if (read_length == 0) {
        ESP_LOGI(TAG, "GPS LC76G %s: transport online, no NMEA bytes waiting", phase);
    } else if (sample.has_fix) {
        ESP_LOGI(
            TAG,
            "GPS LC76G %s: read=%lu pending-before=%lu byte(s), fix=%s quality=%u sats=%u lat=%.6f lon=%.6f alt=%.1fm speed=%.2fm/s hdop=%.1f first=\"%s\"",
            phase,
            static_cast<unsigned long>(read_length),
            static_cast<unsigned long>(data_length),
            sample.has_fix ? "yes" : "no",
            sample.fix_quality,
            sample.satellites,
            sample.latitude_degrees,
            sample.longitude_degrees,
            sample.altitude_m,
            sample.speed_mps,
            sample.hdop,
            sample.first_sentence
        );
    } else {
        ESP_LOGI(
            TAG,
            "GPS LC76G %s: read=%lu pending-before=%lu byte(s), "
            "fix=no first=\"%s\"",
            phase,
            static_cast<unsigned long>(read_length),
            static_cast<unsigned long>(data_length),
            sample.first_sentence
        );
    }
    if (sample.has_fix && sample.utc_valid) {
        const uint16_t year = static_cast<uint16_t>(std::strtoul(sample.utc_date, nullptr, 10));
        const uint8_t month = static_cast<uint8_t>(std::strtoul(sample.utc_date + 5, nullptr, 10));
        const uint8_t day = static_cast<uint8_t>(std::strtoul(sample.utc_date + 8, nullptr, 10));
        const uint8_t hour = static_cast<uint8_t>(
            (sample.utc_time[0] - '0') * 10 + (sample.utc_time[1] - '0')
        );
        const uint8_t minute = static_cast<uint8_t>(
            (sample.utc_time[2] - '0') * 10 + (sample.utc_time[3] - '0')
        );
        const uint8_t second = static_cast<uint8_t>(
            (sample.utc_time[4] - '0') * 10 + (sample.utc_time[5] - '0')
        );
        const esp_err_t sync_error =
            LUNASHELL_time_sync_from_gps(year, month, day, hour, minute, second);
        if (sync_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GPS LC76G: valid fix read but UTC synchronization failed (%s)",
                esp_err_to_name(sync_error)
            );
            record_first_error(sync_error, &aggregate);
        }
    }
    return aggregate;
}

// Project the measured microphone channels onto the compact DFT bank.
void build_audio_spectrum(AudioSpectrumBands &spectrum, size_t channel_count)
{
    for (size_t band = 0; band < LUNASHELL::kAudioSpectrumBandCount; ++band) {
        const float coefficient = 2.0F * std::cos(
            kAudioSpectrumTwoPi *
            static_cast<float>(kAudioSpectrumBins[band]) /
            static_cast<float>(kAudioSpectrumFrameCount)
        );
        for (size_t channel = 0; channel < channel_count; ++channel) {
            float previous = 0.0F;
            float previous_previous = 0.0F;
            for (size_t frame = 0; frame < kAudioSpectrumFrameCount; ++frame) {
                const float current = static_cast<float>(
                    s_microphone_samples[frame * channel_count + channel]
                ) + coefficient * previous - previous_previous;
                previous_previous = previous;
                previous = current;
            }
            const float power = std::max(
                0.0F,
                previous * previous +
                    previous_previous * previous_previous -
                    coefficient * previous * previous_previous
            );
            const float normalized =
                2.0F * std::sqrt(power) /
                (static_cast<float>(kAudioSpectrumFrameCount) *
                 kAudioSpectrumFullScale);
            const float dbfs = 20.0F * std::log10(
                std::max(normalized, kAudioSpectrumFloor)
            );
            const float intensity = std::clamp(
                (dbfs - kAudioSpectrumDisplayFloorDbfs) *
                    (100.0F / -kAudioSpectrumDisplayFloorDbfs),
                0.0F,
                100.0F
            );
            spectrum.value[channel][band] =
                static_cast<uint8_t>(intensity + 0.5F);
        }
    }
}

// Capture one selected-microphone window and publish levels plus the validated DFT bank.
esp_err_t sample_microphone(const char *phase)
{
    const size_t channel_count = LUNASHELL_audio_input_channel_count();
    const int64_t attempted_at_us = esp_timer_get_time();
    if (channel_count == 0U || channel_count > 2U) {
        const esp_err_t channel_error =
            channel_count == 0U
                ? ESP_ERR_NOT_SUPPORTED
                : ESP_ERR_INVALID_RESPONSE;
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us, channel_error](
                LUNASHELL::DeviceSnapshot &snapshot
            ) {
                mark_sample_failure(
                    snapshot.audio.microphone,
                    channel_error,
                    attempted_at_us
                );
            }
        );
        return sample_publication_result(
            "Microphones ES7210",
            phase,
            channel_error,
            publication_error
        );
    }
    const size_t frame_count =
        sizeof(s_microphone_samples) / sizeof(s_microphone_samples[0]) / channel_count;
    if (LUNASHELL_voice_afe_capture_active()) {
        // AFE owns the 16 kHz device while active; publish the real ownership
        // conflict instead of presenting the previous diagnostic sample as new.
        constexpr esp_err_t ownership_error = ESP_ERR_NOT_ALLOWED;
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(
                    snapshot.audio.microphone,
                    ESP_ERR_NOT_ALLOWED,
                    attempted_at_us
                );
            }
        );
        ESP_LOGD(
            TAG,
            "Microphone diagnostics rejected while voice AFE owns capture"
        );
        return sample_publication_result(
            "Microphones ES7210",
            phase,
            ownership_error,
            publication_error
        );
    }
    if (!s_microphone_ready) {
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(
                    snapshot.audio.microphone,
                    ESP_ERR_NOT_SUPPORTED,
                    attempted_at_us
                );
            }
        );
        return sample_publication_result(
            "Microphones ES7210",
            phase,
            ESP_ERR_NOT_SUPPORTED,
            publication_error
        );
    }
    const esp_err_t error = LUNASHELL_audio_capture_pcm16_channels(
        s_microphone_samples,
        frame_count,
        channel_count,
        22050,
        24.0F
    );

    if (error != ESP_OK) {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(
                    snapshot.audio.microphone,
                    error,
                    attempted_at_us
                );
            }
        );
        ESP_LOGW(
            TAG,
            "Microphones ES7210: %s capture failed (%s)",
            phase,
            esp_err_to_name(error)
        );
        return sample_publication_result(
            "Microphones ES7210",
            phase,
            error,
            publication_error
        );
    }

    uint32_t absolute_sum[2] = {};
    uint32_t peak[2] = {};
    AudioSpectrumBands spectrum;
    for (size_t index = 0; index < frame_count; ++index) {
        for (size_t channel = 0; channel < channel_count; ++channel) {
            const int16_t raw =
                s_microphone_samples[index * channel_count + channel];
            const uint32_t value =
                raw < 0
                    ? static_cast<uint32_t>(-static_cast<int32_t>(raw))
                    : static_cast<uint32_t>(raw);
            absolute_sum[channel] += value;
            if (value > peak[channel]) {
                peak[channel] = value;
            }
        }
    }
    build_audio_spectrum(spectrum, channel_count);
    const uint32_t mean =
        (absolute_sum[0] + absolute_sum[1]) / (frame_count * channel_count);
    const uint32_t aggregate_peak = peak[0] > peak[1] ? peak[0] : peak[1];
    const uint32_t channel_mean0 = absolute_sum[0] / frame_count;
    const uint32_t channel_mean1 = absolute_sum[1] / frame_count;
    const uint32_t channel_peak0 = peak[0];
    const uint32_t channel_peak1 = peak[1];
    const esp_err_t publication_error = update_snapshot(
        [&spectrum, mean, aggregate_peak, channel_count, channel_mean0,
         channel_mean1, channel_peak0, channel_peak1,
         attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
            mark_sample_success(
                snapshot.audio.microphone,
                attempted_at_us
            );
            snapshot.audio.channel_count =
                static_cast<uint8_t>(channel_count);
            snapshot.audio.sample_rate_hz = 22050;
            snapshot.audio.microphone_mean_level = mean;
            snapshot.audio.microphone_peak_level = aggregate_peak;
            snapshot.audio.channel_mean_level[0] = channel_mean0;
            snapshot.audio.channel_mean_level[1] = channel_mean1;
            snapshot.audio.channel_peak_level[0] = channel_peak0;
            snapshot.audio.channel_peak_level[1] = channel_peak1;
            std::memcpy(
                snapshot.audio.channel_spectrum,
                spectrum.value,
                sizeof(snapshot.audio.channel_spectrum)
            );
            ++snapshot.audio.spectrum_sequence;
        }
    );
    if (high_frequency_sample_phase(phase)) {
        ESP_LOGD(
            TAG,
            "Microphones ES7210 %s: channels=%u ch0=%lu/%lu ch1=%lu/%lu",
            phase,
            static_cast<unsigned>(channel_count),
            static_cast<unsigned long>(absolute_sum[0] / frame_count),
            static_cast<unsigned long>(peak[0]),
            static_cast<unsigned long>(absolute_sum[1] / frame_count),
            static_cast<unsigned long>(peak[1])
        );
    } else {
        ESP_LOGI(
            TAG,
            "Microphones ES7210 %s: channels=%u ch0=%lu/%lu ch1=%lu/%lu",
            phase,
            static_cast<unsigned>(channel_count),
            static_cast<unsigned long>(absolute_sum[0] / frame_count),
            static_cast<unsigned long>(peak[0]),
            static_cast<unsigned long>(absolute_sum[1] / frame_count),
            static_cast<unsigned long>(peak[1])
        );
    }
    return sample_publication_result(
        "Microphones ES7210",
        phase,
        ESP_OK,
        publication_error
    );
}

// Share one PSRAM-backed sampler between diagnostics and faster orientation updates.
void diagnostic_monitor_task(void *arg)
{
    (void)arg;
    configASSERT(s_diagnostic_stage_snapshot != nullptr);
    TickType_t last_diagnostic_sample = 0;
    TickType_t last_ui_motion_sample = 0;

    for (;;) {
        const bool ui_sampling_power_suspended =
            s_ui_sampling_power_suspended.load(std::memory_order_relaxed);
        const bool diagnostic_imu_requested =
            s_diagnostic_imu_active.load(std::memory_order_relaxed);
        const bool diagnostic_microphone_requested =
            s_diagnostic_microphone_active.load(std::memory_order_relaxed);
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
        const bool auto_rotation_requested =
            s_auto_rotation_enabled.load(std::memory_order_relaxed);
        const bool auto_rotation_active =
            auto_rotation_requested && !ui_sampling_power_suspended;
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
            s_imu_ready &&
            (last_ui_motion_sample == 0 ||
             now - last_ui_motion_sample >= ui_motion_period);
        const bool orientation_due =
            auto_rotation_active && s_imu_ready;
        const bool diagnostic_imu_due =
            diagnostic_due && diagnostic_imu_active;
        const bool diagnostic_microphone_due =
            diagnostic_due && diagnostic_microphone_active;
        const bool imu_sample_due =
            diagnostic_imu_due || ui_motion_due || orientation_due;
        const bool sampling_due =
            imu_sample_due || diagnostic_microphone_due;
        bool effective_diagnostic_due = diagnostic_due;
        bool effective_diagnostic_imu_due = diagnostic_imu_due;
        bool effective_diagnostic_microphone_due =
            diagnostic_microphone_due;
        bool effective_imu_sample_due = imu_sample_due;

        if (sampling_due) {
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
                    !effective_ui_sampling_power_suspended;
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
                effective_diagnostic_due =
                    effective_diagnostic_imu_due ||
                    effective_diagnostic_microphone_due;
                effective_imu_sample_due =
                    effective_diagnostic_imu_due ||
                    ui_motion_due ||
                    effective_auto_rotation_active;
                const bool effective_sampling_due =
                    effective_imu_sample_due ||
                    effective_diagnostic_microphone_due;
                if (effective_sampling_due) {
                    const esp_err_t snapshot_lock_error =
                        begin_snapshot_stage(
                            staged_snapshot,
                            pdMS_TO_TICKS(250),
                            "hardware monitor stage"
                        );
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
                            imu_sample_error = sample_imu(
                                effective_diagnostic_imu_due
                                    ? "diagnostic-ui"
                                    : ui_motion_due
                                        ? "adaptive-ui"
                                        : "auto-rotation",
                                effective_diagnostic_imu_due,
                                effective_auto_rotation_active
                            );
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
                            if (effective_diagnostic_microphone_due) {
                                microphone_sample_error =
                                    sample_microphone("diagnostic-ui");
                                record_first_error(
                                    microphone_sample_error,
                                    &aggregate
                                );
                            }
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
        } else {
            if (!monitor_active) {
                last_diagnostic_sample = 0;
            }
            if (!ui_motion_active) {
                last_ui_motion_sample = 0;
            }
        }

        if (!s_ui_sampling_power_suspended.load(std::memory_order_acquire) &&
            !s_ui_sampling_orientation_reset_pending.load(
                std::memory_order_acquire
            ) &&
            s_orientation.desired_rendering_degrees !=
                s_orientation.applied_rendering_degrees) {
            SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
            if (refresh_mutex != nullptr &&
                xSemaphoreTake(
                    refresh_mutex,
                    pdMS_TO_TICKS(250)
                ) == pdTRUE) {
                if (!s_ui_sampling_power_suspended.load(
                        std::memory_order_acquire
                    ) &&
                    !s_ui_sampling_orientation_reset_pending.load(
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

        const bool rotation_apply_pending =
            !s_ui_sampling_power_suspended.load(std::memory_order_acquire) &&
            !s_ui_sampling_orientation_reset_pending.load(
                std::memory_order_acquire
            ) &&
            s_orientation.desired_rendering_degrees !=
                s_orientation.applied_rendering_degrees;
        const bool wait_indefinitely =
            !auto_rotation_active &&
            !rotation_apply_pending &&
            !ui_motion_active &&
            !monitor_active;
        const TickType_t wait_ticks = wait_indefinitely
            ? portMAX_DELAY
            : pdMS_TO_TICKS(
                auto_rotation_active || rotation_apply_pending
                    ? kOrientationSamplePeriodMs
                    : ui_motion_active
                        ? ui_motion_period_ms
                        : s_diagnostic_period_ms.load(
                            std::memory_order_relaxed
                        )
            );
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

esp_err_t sample_system_state()
{
    LUNASHELL::MemorySnapshot memory;
    LUNASHELL::ProcessorSnapshot processor;
    const int64_t attempted_at_us = esp_timer_get_time();
    esp_err_t memory_error = ESP_OK;
    esp_err_t processor_error = ESP_OK;

    if (s_static_memory_metadata.ready) {
        memory.flash_capacity_bytes =
            s_static_memory_metadata.flash_capacity_bytes;
        memory.app_partition_bytes =
            s_static_memory_metadata.app_partition_bytes;
        memory.firmware_image_bytes =
            s_static_memory_metadata.firmware_image_bytes;
    } else {
        memory_error = esp_flash_get_size(
            nullptr,
            &memory.flash_capacity_bytes
        );
        const esp_partition_t *running_partition =
            esp_ota_get_running_partition();
        if (running_partition != nullptr) {
            memory.app_partition_bytes = running_partition->size;
            const esp_partition_pos_t partition_position = {
                .offset = running_partition->address,
                .size = running_partition->size,
            };
            esp_image_metadata_t image_metadata = {};
            const esp_err_t metadata_error =
                esp_image_get_metadata(&partition_position, &image_metadata);
            record_first_error(metadata_error, &memory_error);
            if (metadata_error == ESP_OK) {
                memory.firmware_image_bytes = image_metadata.image_len;
            }
        } else {
            record_first_error(ESP_ERR_NOT_FOUND, &memory_error);
        }
        if (memory_error == ESP_OK) {
            s_static_memory_metadata = {
                .ready = true,
                .flash_capacity_bytes = memory.flash_capacity_bytes,
                .app_partition_bytes = memory.app_partition_bytes,
                .firmware_image_bytes = memory.firmware_image_bytes,
            };
        }
    }

    constexpr uint32_t internal_caps =
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    multi_heap_info_t default_heap_info = {};
    multi_heap_info_t internal_heap_info = {};
    multi_heap_info_t psram_heap_info = {};
    heap_caps_get_info(&default_heap_info, MALLOC_CAP_DEFAULT);
    heap_caps_get_info(&internal_heap_info, internal_caps);
    heap_caps_get_info(&psram_heap_info, MALLOC_CAP_SPIRAM);

    const auto heap_info_is_valid = [](const multi_heap_info_t &info) {
        return info.largest_free_block <= info.total_free_bytes &&
               info.minimum_free_bytes <= info.total_free_bytes &&
               info.allocated_blocks <= info.total_blocks &&
               info.free_blocks <= info.total_blocks;
    };
    if (!heap_info_is_valid(default_heap_info) ||
        !heap_info_is_valid(internal_heap_info) ||
        !heap_info_is_valid(psram_heap_info)) {
        record_first_error(ESP_ERR_INVALID_RESPONSE, &memory_error);
    }

    const size_t default_heap_total =
        heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    const size_t internal_heap_total =
        heap_caps_get_total_size(internal_caps);
    const size_t psram_heap_total =
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psram_capacity = esp_psram_get_size();
    if (default_heap_total == 0U ||
        internal_heap_total == 0U ||
        psram_heap_total == 0U ||
        psram_capacity == 0U) {
        record_first_error(ESP_ERR_INVALID_RESPONSE, &memory_error);
    }

    memory.heap_total_bytes = static_cast<uint32_t>(default_heap_total);
    memory.heap_free_bytes =
        static_cast<uint32_t>(default_heap_info.total_free_bytes);
    memory.heap_minimum_free_bytes =
        static_cast<uint32_t>(default_heap_info.minimum_free_bytes);
    memory.internal_heap_total_bytes =
        static_cast<uint32_t>(internal_heap_total);
    memory.internal_free_bytes =
        static_cast<uint32_t>(internal_heap_info.total_free_bytes);
    memory.internal_largest_block_bytes =
        static_cast<uint32_t>(internal_heap_info.largest_free_block);
    memory.internal_minimum_free_bytes =
        static_cast<uint32_t>(internal_heap_info.minimum_free_bytes);
    memory.internal_allocated_blocks =
        static_cast<uint32_t>(internal_heap_info.allocated_blocks);
    memory.internal_free_blocks =
        static_cast<uint32_t>(internal_heap_info.free_blocks);
    memory.internal_total_blocks =
        static_cast<uint32_t>(internal_heap_info.total_blocks);
    memory.internal_memory_pressure =
        memory.internal_free_bytes < 32768U ||
        memory.internal_largest_block_bytes < 16384U;
    memory.psram_capacity_bytes = static_cast<uint32_t>(psram_capacity);
    memory.psram_heap_total_bytes =
        static_cast<uint32_t>(psram_heap_total);
    memory.psram_free_bytes =
        static_cast<uint32_t>(psram_heap_info.total_free_bytes);
    if (memory.heap_free_bytes > memory.heap_total_bytes ||
        memory.internal_free_bytes > memory.internal_heap_total_bytes ||
        memory.psram_free_bytes > memory.psram_heap_total_bytes) {
        record_first_error(ESP_ERR_INVALID_RESPONSE, &memory_error);
    } else {
        memory.psram_used_bytes =
            memory.psram_heap_total_bytes - memory.psram_free_bytes;
    }
    memory.psram_largest_block_bytes =
        static_cast<uint32_t>(psram_heap_info.largest_free_block);
    memory.psram_minimum_free_bytes =
        static_cast<uint32_t>(psram_heap_info.minimum_free_bytes);
    memory.psram_allocated_blocks =
        static_cast<uint32_t>(psram_heap_info.allocated_blocks);
    memory.psram_free_blocks =
        static_cast<uint32_t>(psram_heap_info.free_blocks);
    memory.psram_total_blocks =
        static_cast<uint32_t>(psram_heap_info.total_blocks);
    memory.allocation_failure_monitor_active =
        s_memory_telemetry_active.load(std::memory_order_acquire);
    memory.allocation_failure_count =
        s_allocation_failure_count;
    __asm__ __volatile__("memw" ::: "memory");
    memory.largest_failed_allocation_bytes =
        s_largest_failed_allocation_bytes;
    memory.failed_allocation_caps_seen_mask =
        s_failed_allocation_caps_seen_mask;
    if (!memory.allocation_failure_monitor_active) {
        record_first_error(ESP_ERR_INVALID_STATE, &memory_error);
    }
    const int64_t memory_completed_at_us = esp_timer_get_time();

    if (s_static_processor_metadata.ready) {
        processor.core_count = s_static_processor_metadata.core_count;
    } else {
        esp_chip_info_t chip = {};
        esp_chip_info(&chip);
        processor.core_count = static_cast<uint8_t>(chip.cores);
        if (chip.cores < 1 || chip.cores > 2) {
            record_first_error(
                ESP_ERR_INVALID_RESPONSE,
                &processor_error
            );
        }
        if (processor_error == ESP_OK) {
            s_static_processor_metadata = {
                .ready = true,
                .core_count = processor.core_count,
            };
        }
    }
    const int core_count = processor.core_count;
    const int sampling_core = esp_cpu_get_core_id();
    if (sampling_core < 0 || sampling_core >= core_count ||
        sampling_core >= 2) {
        record_first_error(
            ESP_ERR_INVALID_RESPONSE,
            &processor_error
        );
    } else {
        processor.sampling_core = static_cast<uint8_t>(sampling_core);
    }

    if (processor_error == ESP_OK) {
        std::lock_guard<std::mutex> lock(s_processor_usage_mutex);
        const configRUN_TIME_COUNTER_TYPE sampled_at =
            static_cast<configRUN_TIME_COUNTER_TYPE>(esp_timer_get_time());
        configRUN_TIME_COUNTER_TYPE idle_runtime[2] = {};
        for (int core = 0; core < core_count; ++core) {
            idle_runtime[core] = ulTaskGetIdleRunTimeCounterForCore(core);
            processor.cores[core].available = true;
        }
        if (processor_error == ESP_OK && s_processor_usage.initialized) {
            const configRUN_TIME_COUNTER_TYPE elapsed =
                sampled_at - s_processor_usage.sampled_at;
            if (elapsed == 0) {
                record_first_error(
                    ESP_ERR_INVALID_RESPONSE,
                    &processor_error
                );
            } else {
                for (int core = 0; core < core_count; ++core) {
                    const configRUN_TIME_COUNTER_TYPE idle_elapsed =
                        idle_runtime[core] -
                        s_processor_usage.idle_runtime[core];
                    if (idle_elapsed > elapsed) {
                        ESP_LOGE(
                            TAG,
                            "CPU core %d sample rejected: idle=%llu elapsed=%llu",
                            core,
                            static_cast<unsigned long long>(idle_elapsed),
                            static_cast<unsigned long long>(elapsed)
                        );
                        record_first_error(
                            ESP_ERR_INVALID_RESPONSE,
                            &processor_error
                        );
                        break;
                    }
                    LUNASHELL::CoreSnapshot &core_snapshot =
                        processor.cores[core];
                    core_snapshot.utilization_percent =
                        static_cast<uint8_t>(
                            ((elapsed - idle_elapsed) * 100ULL +
                             elapsed / 2ULL) /
                            elapsed
                        );
                    core_snapshot.utilization_available = true;
                }
            }
        }
        if (processor_error == ESP_OK) {
            s_processor_usage.initialized = true;
            s_processor_usage.sampled_at = sampled_at;
            for (int core = 0; core < core_count; ++core) {
                s_processor_usage.idle_runtime[core] = idle_runtime[core];
            }
        }
    }

    if (processor_error == ESP_OK) {
        TaskHandle_t task = xTaskGetCurrentTaskHandle();
        if (task == nullptr) {
            record_first_error(ESP_ERR_NOT_FOUND, &processor_error);
        } else {
            const char *task_name = pcTaskGetName(task);
            if (task_name == nullptr || task_name[0] == '\0') {
                record_first_error(
                    ESP_ERR_INVALID_RESPONSE,
                    &processor_error
                );
            } else {
                LUNASHELL::CoreSnapshot &sampled_core =
                    processor.cores[processor.sampling_core];
                strlcpy(
                    sampled_core.current_task,
                    task_name,
                    sizeof(sampled_core.current_task)
                );
                sampled_core.priority = uxTaskPriorityGet(task);
                sampled_core.stack_high_water_words =
                    uxTaskGetStackHighWaterMark(task);
            }
        }
    }
    const int64_t processor_completed_at_us = esp_timer_get_time();

    if (memory_error == ESP_OK) {
        mark_sample_success(
            memory,
            attempted_at_us,
            memory_completed_at_us
        );
    }
    if (processor_error == ESP_OK) {
        mark_sample_success(
            processor,
            attempted_at_us,
            processor_completed_at_us
        );
    }
    const esp_err_t publish_error = update_snapshot(
        [&memory, &processor, memory_error, processor_error, attempted_at_us](
            LUNASHELL::DeviceSnapshot &snapshot
        ) {
            if (memory_error == ESP_OK) {
                snapshot.memory = memory;
            } else {
                mark_sample_failure(
                    snapshot.memory,
                    memory_error,
                    attempted_at_us
                );
            }
            if (processor_error == ESP_OK) {
                snapshot.processor = processor;
            } else {
                mark_sample_failure(
                    snapshot.processor,
                    processor_error,
                    attempted_at_us
                );
            }
        }
    );

    if (memory_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Memory sample failed (%s)",
            esp_err_to_name(memory_error)
        );
    }
    if (processor_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Processor sample failed (%s)",
            esp_err_to_name(processor_error)
        );
    }
    if (publish_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "System-state publication failed (%s)",
            esp_err_to_name(publish_error)
        );
    }

    esp_err_t aggregate = ESP_OK;
    record_first_error(memory_error, &aggregate);
    record_first_error(processor_error, &aggregate);
    record_first_error(publish_error, &aggregate);
    return aggregate;
}

}  // namespace

namespace LUNASHELL {

const char *deviceHealthName(DeviceHealth health)
{
    switch (health) {
    case DeviceHealth::Ready:
        return "ready";
    case DeviceHealth::Degraded:
        return "degraded";
    case DeviceHealth::Missing:
        return "missing";
    case DeviceHealth::Unknown:
    default:
        return "unknown";
    }
}

DeviceSnapshotService &DeviceSnapshotService::instance()
{
    static DeviceSnapshotService service;
    return service;
}

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
    if (ensure_pmu_mutex() == nullptr) {
        return ESP_ERR_NO_MEM;
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
        s_imu_smoothing_valid.store(false, std::memory_order_relaxed);
        publish_imu_smoothing(error);
        return error;
    }
    s_imu_smoothing_percent.store(saved_smoothing, std::memory_order_relaxed);
    s_imu_smoothing_valid.store(true, std::memory_order_relaxed);
    publish_imu_smoothing(ESP_OK);

    i2c_master_bus_handle_t bus = nullptr;
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
    s_i2c_bus = bus;
    ESP_LOGI(TAG, "Shared BSP I2C: SDA=%d SCL=%d", BSP_I2C_SDA, BSP_I2C_SCL);

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

    error = init_pmu(bus);
    record_optional_chip("PMU", error);

#if CONFIG_LUNASHELL_BUILD_ENABLE_RTC
    error = init_rtc(bus);
    record_optional_chip("RTC", error);
#else
    update_snapshot([](DeviceSnapshot &snapshot) {
        set_component(snapshot.rtc, false, ESP_ERR_NOT_SUPPORTED);
    });
    ESP_LOGI(TAG, "RTC: disabled by build profile");
#endif

    error = init_imu(bus);
    record_optional_chip("IMU", error);

#if CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
    error = init_gps(bus);
    record_optional_chip("GNSS", error);
#else
    update_snapshot([](DeviceSnapshot &snapshot) {
        set_component(snapshot.gps, false, ESP_ERR_NOT_SUPPORTED);
    });
    ESP_LOGI(TAG, "GNSS: disabled by build profile");
#endif

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

    error = LUNASHELL_audio_init();
    if (error == ESP_ERR_NOT_SUPPORTED) {
        update_snapshot([error](DeviceSnapshot &snapshot) {
            set_component(snapshot.audio.microphone, false, error);
            set_component(snapshot.audio.speaker, false, error);
        });
        ESP_LOGI(TAG, "Audio codecs: disabled by board hardware selection");
    } else if (error != ESP_OK) {
        update_snapshot([error](DeviceSnapshot &snapshot) {
            set_component(snapshot.audio.microphone, false, error);
            set_component(snapshot.audio.speaker, false, error);
        });
        record_optional_chip("audio codecs", error);
    } else {
        s_microphone_ready = LUNASHELL_audio_input_channel_count() > 0;
        if (s_microphone_ready) {
            error = sample_microphone("initial");
            record_optional_chip("microphones", error);
        } else {
            update_snapshot([](DeviceSnapshot &snapshot) {
                set_component(snapshot.audio.microphone, false, ESP_ERR_NOT_SUPPORTED);
            });
            ESP_LOGI(TAG, "Microphones: disabled by board hardware selection");
        }
    }

    s_initialized = true;
    error = LUNASHELL_hardware_set_diagnostic_streams(false, false);
    if (error != ESP_OK) {
        s_initialization_result = error;
        return error;
    }
    error = refresh("boot");
    record_first_error(error, &aggregate);
    s_initialization_result = aggregate;
    if (aggregate != ESP_OK) {
        return aggregate;
    }
    ESP_LOGI(
        TAG,
        "Hardware initialization: PMU + RTC + IMU + GPS + microphone ready"
    );
    return ESP_OK;
}

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
    DeviceSnapshot staged_snapshot;
    DeviceSnapshot summary;
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
        if (s_pmu_ready) {
            record_first_error(sample_pmu(phase), &aggregate);
        }
        if (s_rtc_ready) {
            record_first_error(
                sample_rtc(phase, &rtc_latch_observation),
                &aggregate
            );
        }
        if (s_imu_ready) {
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
                record_first_error(sample_imu(phase), &aggregate);
            }
        }
        if (s_gps_ready) {
            record_first_error(sample_gps(phase), &aggregate);
        }
        if (s_microphone_ready) {
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
                record_first_error(sample_microphone(phase), &aggregate);
            }
        }
        record_first_error(sample_system_state(), &aggregate);
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
            apply_rtc_latch_observation(rtc_latch_observation);
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
    record_first_error(
        give_refresh_mutex(refresh_mutex, "Hardware refresh"),
        &aggregate
    );
    if (should_log_summary) {
        log_summary(summary);
    }
    return aggregate;
}

DeviceSnapshot DeviceSnapshotService::snapshot() const
{
    DeviceSnapshot copy;
    const esp_err_t lock_error =
        take_snapshot_mutex(kSnapshotMutexTimeoutTicks, "read");
    if (lock_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot read cannot return a synthetic empty snapshot "
            "(%s)",
            esp_err_to_name(lock_error)
        );
        std::abort();
    }
    copy = s_snapshot;
    const esp_err_t release_error = give_snapshot_mutex("read");
    if (release_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "DeviceSnapshot read mutex invariant failed (%s)",
            esp_err_to_name(release_error)
        );
        std::abort();
    }
    apply_measured_sample_aging(copy, esp_timer_get_time());
    copy.ui.touch_activity_sequence =
        s_touch_activity_sequence.load(std::memory_order_acquire);
    copy.ui.touch_last_activity_at_us =
        s_touch_last_activity_at_us.load(std::memory_order_relaxed);
    return copy;
}

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
    if (!s_imu_ready) {
        return ESP_ERR_NOT_FOUND;
    }

    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t error = sample_imu("distance-geometry", false);
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

void DeviceSnapshotService::reportStorage(
    const StorageMountReport &report
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const bool coherent_capacity =
        report.capacity_bytes > 0 &&
        report.filesystem_total_bytes > 0 &&
        report.filesystem_total_bytes <= report.capacity_bytes &&
        report.free_bytes <= report.filesystem_total_bytes;
    const bool coherent_mounted =
        report.outcome == StorageMountOutcome::Mounted &&
        report.error == ESP_OK &&
        report.cleanup_error == ESP_OK &&
        report.cleanup_complete &&
        !report.poisoned &&
        report.mounted &&
        report.card_response_verified &&
        !report.format_required &&
        coherent_capacity;
    esp_err_t effective_error = report.error;
    if (!coherent_mounted && effective_error == ESP_OK) {
        effective_error =
            report.cleanup_error != ESP_OK
                ? report.cleanup_error
                : (report.outcome == StorageMountOutcome::NotAttempted
                       ? ESP_ERR_INVALID_STATE
                       : ESP_ERR_INVALID_RESPONSE);
    }

    const esp_err_t publication_error = update_snapshot(
        [report, coherent_mounted, effective_error, attempted_at_us](
            DeviceSnapshot &snapshot
        ) {
            snapshot.storage.mount_outcome = report.outcome;
            snapshot.storage.mount_stage = report.stage;
            snapshot.storage.cleanup_stage = report.cleanup_stage;
            snapshot.storage.cleanup_error = report.cleanup_error;
            snapshot.storage.initial_fatfs_result_valid =
                report.initial_fatfs_result_valid;
            snapshot.storage.initial_fatfs_result =
                report.initial_fatfs_result;
            snapshot.storage.fatfs_result_valid =
                report.fatfs_result_valid;
            snapshot.storage.fatfs_result = report.fatfs_result;
            snapshot.storage.cleanup_fatfs_result_valid =
                report.cleanup_fatfs_result_valid;
            snapshot.storage.cleanup_fatfs_result =
                report.cleanup_fatfs_result;
            snapshot.storage.cleanup_complete = report.cleanup_complete;
            snapshot.storage.poisoned = report.poisoned;
            snapshot.storage.mounted = report.mounted;
            snapshot.storage.card_response_verified =
                report.card_response_verified;
            snapshot.storage.format_required = report.format_required;
            snapshot.storage.authorization_consumed =
                report.authorization_consumed;
            snapshot.storage.authorization_identity_matched =
                report.authorization_identity_matched;
            snapshot.storage.format_attempted =
                report.format_attempted;
            snapshot.storage.partition_written =
                report.partition_written;
            snapshot.storage.formatted = report.formatted;
            snapshot.storage.mount_attempt_count =
                report.mount_attempt_count;
            snapshot.storage.logger_mirror_requested =
                report.logger_mirror_requested;
            snapshot.storage.logger_mirror_enabled =
                report.logger_mirror_enabled;

            if (coherent_mounted) {
                snapshot.storage.capacity_bytes =
                    report.capacity_bytes;
                snapshot.storage.filesystem_total_bytes =
                    report.filesystem_total_bytes;
                snapshot.storage.free_bytes = report.free_bytes;
                mark_sample_success(
                    snapshot.storage,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.storage,
                    effective_error,
                    attempted_at_us
                );
                snapshot.storage.health = DeviceHealth::Degraded;
            }
        }
    );
    log_snapshot_publication_failure(
        "Storage report",
        publication_error
    );

    if (!coherent_mounted) {
        ESP_LOGW(
            TAG,
            "Storage report rejected as current data: outcome=%u mounted=%s card-response=%s error=%s cleanup=%s",
            static_cast<unsigned>(report.outcome),
            report.mounted ? "yes" : "no",
            report.card_response_verified ? "verified" : "unconfirmed",
            esp_err_to_name(effective_error),
            esp_err_to_name(report.cleanup_error)
        );
    }
}

void DeviceSnapshotService::reportStorageMirror(
    bool requested,
    bool enabled,
    esp_err_t error,
    int last_errno,
    int64_t changed_at_us,
    uint32_t failure_count
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.storage.logger_mirror_requested = requested;
            snapshot.storage.logger_mirror_enabled = enabled;
            snapshot.storage.logger_mirror_errno = last_errno;
            snapshot.storage.logger_mirror_changed_at_us = changed_at_us;
            snapshot.storage.logger_mirror_failure_count = failure_count;
            if (error == ESP_OK) {
                mark_sample_success(
                    snapshot.storage.logger_mirror,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.storage.logger_mirror,
                    error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Storage-mirror report",
        publication_error
    );
}

void DeviceSnapshotService::reportDiagnosticPeriod(
    uint32_t sample_period_ms,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK &&
                (sample_period_ms < kDiagnosticPeriodMinMs ||
                 sample_period_ms > kDiagnosticPeriodMaxMs)
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.diagnostics.sample_period_ms =
                    sample_period_ms;
                mark_sample_success(
                    snapshot.diagnostics.sample_period,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.sample_period,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Diagnostic-period report",
        publication_error
    );
}

void DeviceSnapshotService::reportImuSmoothing(
    uint32_t imu_smoothing_percent,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK &&
                imu_smoothing_percent > kImuSmoothingMaxPercent
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.diagnostics.imu_smoothing_percent =
                    imu_smoothing_percent;
                mark_sample_success(
                    snapshot.diagnostics.imu_smoothing,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.imu_smoothing,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "IMU-smoothing report",
        publication_error
    );
}

void DeviceSnapshotService::reportFullSnapshotPeriod(
    uint32_t full_snapshot_period_ms,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK &&
                !isFullSnapshotPeriodSupported(full_snapshot_period_ms)
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.diagnostics.full_snapshot_period_ms =
                    full_snapshot_period_ms;
                mark_sample_success(
                    snapshot.diagnostics.full_snapshot_period,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.full_snapshot_period,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Full-snapshot-period report",
        publication_error
    );
}

void DeviceSnapshotService::reportFullSnapshotMonitor(
    bool monitor_active,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.diagnostics.full_snapshot_monitor_active =
                monitor_active;
            if (error == ESP_OK) {
                mark_sample_success(
                    snapshot.diagnostics.full_snapshot_monitor,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.full_snapshot_monitor,
                    error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Full-snapshot-monitor report",
        publication_error
    );
}

void DeviceSnapshotService::reportSensorFusion(
    const SensorFusionResult &result,
    esp_err_t error,
    int64_t sampled_at_us
)
{
    const int64_t attempted_at_us =
        sampled_at_us > 0 ? sampled_at_us : esp_timer_get_time();
    const bool result_is_finite =
        std::isfinite(result.acceleration_magnitude_mps2) &&
        std::isfinite(result.angular_speed_dps);
    const esp_err_t effective_error =
        error == ESP_OK && !result_is_finite
            ? ESP_ERR_INVALID_RESPONSE
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.sensor_fusion.gravity_dominant =
                    result.gravity_dominant;
                snapshot.sensor_fusion.stationary = result.stationary;
                snapshot.sensor_fusion.acceleration_magnitude_mps2 =
                    result.acceleration_magnitude_mps2;
                snapshot.sensor_fusion.angular_speed_dps =
                    result.angular_speed_dps;
                mark_sample_success(
                    snapshot.sensor_fusion,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.sensor_fusion,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Sensor-fusion report",
        publication_error
    );
}

void DeviceSnapshotService::reportDistance(const DistanceSnapshot &distance)
{
    const esp_err_t publication_error = update_snapshot(
        [&distance](DeviceSnapshot &snapshot) {
            snapshot.distance = distance;
        }
    );
    log_snapshot_publication_failure(
        "Distance report",
        publication_error
    );
}

void DeviceSnapshotService::reportDiagnosticMonitor(
    bool imu_monitor_active,
    bool microphone_monitor_active,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (error == ESP_OK) {
                snapshot.diagnostics.monitor_active =
                    imu_monitor_active || microphone_monitor_active;
                snapshot.diagnostics.imu_monitor_active =
                    imu_monitor_active;
                snapshot.diagnostics.microphone_monitor_active =
                    microphone_monitor_active;
                mark_sample_success(
                    snapshot.diagnostics.monitor,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.monitor,
                    error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Diagnostic-monitor report",
        publication_error
    );
}

void DeviceSnapshotService::reportDisplayBrightness(
    uint32_t brightness_percent,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK && brightness_percent > 100U
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.ui.brightness_percent =
                    brightness_percent;
                mark_sample_success(
                    snapshot.ui.brightness,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.ui.brightness,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Display-brightness report",
        publication_error
    );
}

void DeviceSnapshotService::reportDisplayConfiguredBrightness(
    uint32_t brightness_percent,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK && brightness_percent > 100U
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.ui.configured_brightness_percent = brightness_percent;
                mark_sample_success(
                    snapshot.ui.brightness,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.ui.brightness,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Configured-display-brightness report",
        publication_error
    );
}

void DeviceSnapshotService::reportDisplayPowerMode(
    UiHardwareSnapshot::DisplayPowerMode display_power_mode,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK && display_power_mode == UiHardwareSnapshot::DisplayPowerMode::Unknown
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.ui.display_power_mode_state = display_power_mode;
                mark_sample_success(
                    snapshot.ui.display_power_mode,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.ui.display_power_mode,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Display-power-mode report",
        publication_error
    );
}

void DeviceSnapshotService::reportDisplay(bool ready, esp_err_t error)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        !ready && error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (ready && effective_error == ESP_OK) {
                mark_sample_success(snapshot.ui.display, attempted_at_us);
            } else {
                mark_sample_failure(
                    snapshot.ui.display,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure("Display report", publication_error);
}

void DeviceSnapshotService::reportTouch(bool ready, esp_err_t error)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        !ready && error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (ready && effective_error == ESP_OK) {
                mark_sample_success(snapshot.ui.touch, attempted_at_us);
            } else {
                mark_sample_failure(
                    snapshot.ui.touch,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure("Touch report", publication_error);
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

void DeviceSnapshotService::reportSpeaker(bool ready, esp_err_t error)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        !ready && error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (ready && effective_error == ESP_OK) {
                mark_sample_success(snapshot.audio.speaker, attempted_at_us);
            } else {
                mark_sample_failure(
                    snapshot.audio.speaker,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure("Speaker report", publication_error);
}

void DeviceSnapshotService::reportWifi(const WifiSnapshot &wifi)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.radios.wifi = wifi;
            if (wifi.health == DeviceHealth::Ready &&
                wifi.last_error == ESP_OK) {
                mark_sample_success(
                    snapshot.radios.wifi,
                    attempted_at_us
                );
            } else {
                const esp_err_t effective_error =
                    wifi.last_error == ESP_OK
                        ? ESP_ERR_INVALID_STATE
                        : wifi.last_error;
                mark_sample_failure(
                    snapshot.radios.wifi,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure("Wi-Fi report", publication_error);
}

void DeviceSnapshotService::reportBle(
    bool ready,
    esp_err_t error,
    bool advertising,
    bool connected,
    uint16_t connection_handle,
    uint32_t connection_count,
    uint32_t disconnection_count,
    const char *device_name,
    bool provisioning
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const bool device_name_valid =
        device_name != nullptr &&
        device_name[0] != '\0' &&
        std::strlen(device_name) < sizeof(BleSnapshot::device_name);
    const esp_err_t effective_error =
        error == ESP_OK && ready && !device_name_valid
            ? ESP_ERR_INVALID_ARG
            : (!ready && error == ESP_OK
                   ? ESP_ERR_INVALID_STATE
                   : error);
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (ready && effective_error == ESP_OK) {
                mark_sample_success(
                    snapshot.radios.ble,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.radios.ble,
                    effective_error,
                    attempted_at_us
                );
            }
            snapshot.radios.ble.provisioning = provisioning;
            snapshot.radios.ble.advertising = advertising;
            snapshot.radios.ble.connected = connected;
            snapshot.radios.ble.connection_handle = connection_handle;
            snapshot.radios.ble.connection_count = connection_count;
            snapshot.radios.ble.disconnection_count = disconnection_count;
            if (device_name_valid) {
                strlcpy(
                    snapshot.radios.ble.device_name,
                    device_name,
                    sizeof(snapshot.radios.ble.device_name)
                );
            }
        }
    );
    log_snapshot_publication_failure("BLE report", publication_error);
}

void DeviceSnapshotService::reportTime(
    bool system_time_valid,
    bool synchronized,
    const char *source,
    const char *timezone,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const bool source_valid =
        source != nullptr &&
        source[0] != '\0' &&
        std::strlen(source) < sizeof(RtcSnapshot::source);
    const bool timezone_valid =
        timezone != nullptr &&
        timezone[0] != '\0' &&
        std::strlen(timezone) < sizeof(RtcSnapshot::timezone);
    esp_err_t effective_error = error;
    if (effective_error == ESP_OK && !system_time_valid) {
        effective_error = ESP_ERR_INVALID_STATE;
    } else if (
        effective_error == ESP_OK &&
        (!source_valid || !timezone_valid)
    ) {
        effective_error = ESP_ERR_INVALID_ARG;
    }
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.rtc.system_time_valid = system_time_valid;
            snapshot.rtc.synchronized = synchronized;
            if (source_valid) {
                strlcpy(
                    snapshot.rtc.source,
                    source,
                    sizeof(snapshot.rtc.source)
                );
            }
            if (timezone_valid) {
                strlcpy(
                    snapshot.rtc.timezone,
                    timezone,
                    sizeof(snapshot.rtc.timezone)
                );
            }
            if (system_time_valid && effective_error == ESP_OK) {
                mark_sample_success(
                    snapshot.rtc.system_time,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.rtc.system_time,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure("Time report", publication_error);
}

void DeviceSnapshotService::reportInputs(const InputSnapshot &inputs)
{
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) { snapshot.input = inputs; }
    );
    log_snapshot_publication_failure("Input report", publication_error);
}

void DeviceSnapshotService::reportPowerManagement(
    DeviceOperatingState state,
    DeviceStateReason reason,
    bool audio_suspended,
    bool fullscreen_active,
    bool command_arc_visible,
    bool status_arc_visible,
    UiHardwareSnapshot::DisplayPowerMode display_idle_stage,
    uint32_t command_arc_timeout_seconds,
    uint32_t status_arc_timeout_seconds,
    uint32_t display_dim_timeout_seconds,
    uint32_t screen_sleep_delay_seconds,
    uint32_t transition_sequence,
    int64_t last_activity_at_us,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.power.state = state;
            snapshot.power.reason = reason;
            snapshot.power.audio_suspended = audio_suspended;
            snapshot.power.fullscreen_active = fullscreen_active;
            snapshot.power.command_arc_visible = command_arc_visible;
            snapshot.power.status_arc_visible = status_arc_visible;
            snapshot.power.display_idle_stage = display_idle_stage;
            snapshot.power.command_arc_timeout_seconds =
                command_arc_timeout_seconds;
            snapshot.power.status_arc_timeout_seconds =
                status_arc_timeout_seconds;
            snapshot.power.display_dim_timeout_seconds =
                display_dim_timeout_seconds;
            snapshot.power.screen_sleep_delay_seconds =
                screen_sleep_delay_seconds;
            snapshot.power.transition_sequence = transition_sequence;
            snapshot.power.last_activity_at_us = last_activity_at_us;
            snapshot.power.changed_at_us = attempted_at_us;
            if (error == ESP_OK) {
                mark_sample_success(snapshot.power, attempted_at_us);
            } else {
                mark_sample_failure(
                    snapshot.power,
                    error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Power-management report",
        publication_error
    );
}

}  // namespace LUNASHELL

extern "C" esp_err_t LUNASHELL_memory_telemetry_init(void)
{
    bool expected = false;
    if (!s_memory_telemetry_active.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
        return ESP_OK;
    }
    const esp_err_t error =
        heap_caps_register_failed_alloc_callback(allocation_failed_hook);
    if (error != ESP_OK) {
        s_memory_telemetry_active.store(false, std::memory_order_release);
        return error;
    }
    return ESP_OK;
}

// Allocate and start the shared diagnostics/orientation task once.
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
    s_ui_motion_monitor_active.store(false, std::memory_order_relaxed);
    s_ui_motion_monitor_standby.store(false, std::memory_order_relaxed);
    TaskHandle_t task = nullptr;
    s_diagnostic_monitor_started =
        xTaskCreateWithCaps(
            diagnostic_monitor_task,
            "LUNASHELL_diag_monitor",
            3072,
            nullptr,
            3,
            &task,
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

extern "C" esp_err_t LUNASHELL_hardware_take_power_key_events(
    uint32_t *short_presses,
    uint32_t *long_presses
)
{
    if (short_presses == nullptr || long_presses == nullptr) {
        ESP_LOGE(TAG, "PKEY event take rejected: output pointer is null");
        return ESP_ERR_INVALID_ARG;
    }
    *short_presses = 0;
    *long_presses = 0;

#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
    if (!s_initialized || !s_pmu_ready) {
        ESP_LOGE(
            TAG,
            "PKEY event take rejected: AXP2101 is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }

    SemaphoreHandle_t pmu_mutex = ensure_pmu_mutex();
    if (pmu_mutex == nullptr) {
        ESP_LOGE(TAG, "PKEY event take failed: PMU mutex unavailable");
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(pmu_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGW(
            TAG,
            "PKEY event take timed out waiting for PMU access"
        );
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t collect_error = collect_pmu_irq_events();
    if (collect_error == ESP_OK) {
        *short_presses =
            s_pkey_short_press_pending.exchange(0, std::memory_order_acq_rel);
        *long_presses =
            s_pkey_long_press_pending.exchange(0, std::memory_order_acq_rel);
    }
    const esp_err_t release_error = give_pmu_mutex(
        pmu_mutex,
        "PKEY event take"
    );
    if (collect_error != ESP_OK) {
        return collect_error;
    }
    return release_error;
#else
    ESP_LOGE(TAG, "PKEY event take rejected: AXP2101 support is disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

extern "C" esp_err_t LUNASHELL_hardware_set_diagnostic_streams(
    bool imu_enabled,
    bool microphone_enabled
)
{
    if (!s_initialized) {
        publish_diagnostic_monitor(ESP_ERR_INVALID_STATE);
        ESP_LOGE(
            TAG,
            "Diagnostic streams rejected: hardware is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }
    if ((imu_enabled && !s_imu_ready) ||
        (microphone_enabled && !s_microphone_ready)) {
        publish_diagnostic_monitor(ESP_ERR_INVALID_STATE);
        ESP_LOGE(
            TAG,
            "Diagnostic streams rejected: requested hardware is unavailable "
            "(IMU=%s microphone=%s)",
            !imu_enabled ? "off" : s_imu_ready ? "ready" : "unavailable",
            !microphone_enabled
                ? "off"
                : s_microphone_ready ? "ready" : "unavailable"
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (((imu_enabled || microphone_enabled) &&
         !s_diagnostic_period_valid.load(std::memory_order_relaxed)) ||
        (imu_enabled &&
         !s_imu_smoothing_valid.load(std::memory_order_relaxed))) {
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
        "Diagnostic streams: IMU=%s microphone=%s",
        imu_enabled ? "active" : "standby",
        microphone_enabled ? "active" : "standby"
    );
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_hardware_set_ui_motion_monitor(bool enabled)
{
    if (!s_initialized || !s_imu_ready) {
        ESP_LOGE(
            TAG,
            "Adaptive UI motion monitor rejected: hardware or IMU is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (enabled &&
        !s_imu_smoothing_valid.load(std::memory_order_relaxed)) {
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
extern "C" esp_err_t LUNASHELL_hardware_set_ui_motion_monitor_standby(bool standby)
{
    if (!s_initialized || !s_imu_ready) {
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
extern "C" esp_err_t LUNASHELL_hardware_set_gps_power_suspended(bool suspended)
{
#if !CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
    (void)suspended;
    return ESP_ERR_NOT_SUPPORTED;
#else
    const int64_t attempted_at_us = esp_timer_get_time();
    if (!s_initialized || !s_gps_ready) {
        ESP_LOGE(
            TAG,
            "GPS LC76G power transition to %s rejected: GNSS is unavailable",
            suspended ? "standby" : "active"
        );
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(
                    snapshot.gps,
                    ESP_ERR_INVALID_STATE,
                    attempted_at_us
                );
            }
        );
        if (publication_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GPS LC76G power transition rejection publication failed (%s)",
                esp_err_to_name(publication_error)
            );
        }
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "GPS LC76G power transition to %s timed out waiting for refresh",
            suspended ? "standby" : "active"
        );
        return ESP_ERR_TIMEOUT;
    }
    if (s_gps_power_suspended == suspended) {
        return give_refresh_mutex(refresh_mutex, "GPS LC76G power transition");
    }

    static constexpr char kPairPowerOff[] = "$PAIR003*39\r\n";
    static constexpr char kPairPowerOn[] = "$PAIR002*38\r\n";
    const esp_err_t error = gps_send_pair_command(
        suspended ? 3U : 2U,
        suspended ? kPairPowerOff : kPairPowerOn,
        suspended ? "GNSS subsystem power-off" : "GNSS subsystem power-on"
    );
    if (error == ESP_OK) {
        s_gps_power_suspended = suspended;
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(
                    snapshot.gps,
                    ESP_ERR_INVALID_STATE,
                    attempted_at_us
                );
            }
        );
        if (publication_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GPS LC76G power transition publication failed (%s)",
                esp_err_to_name(publication_error)
            );
            const esp_err_t release_error = give_refresh_mutex(
                refresh_mutex,
                "GPS LC76G power transition"
            );
            return release_error == ESP_OK ? publication_error : release_error;
        }
    } else {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(snapshot.gps, error, attempted_at_us);
            }
        );
        if (publication_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GPS LC76G failed transition publication (%s)",
                esp_err_to_name(publication_error)
            );
        }
    }
    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex,
        "GPS LC76G power transition"
    );
    if (release_error != ESP_OK) {
        return release_error;
    }
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GPS LC76G subsystem power-%s failed (%s)",
            suspended ? "off" : "on",
            esp_err_to_name(error)
        );
        return error;
    }
    ESP_LOGI(
        TAG,
        "GPS LC76G subsystem power-%s acknowledged",
        suspended ? "off" : "on"
    );
    return ESP_OK;
#endif
}

// Serialize QMI8658 profile changes with sampling and calibration ownership.
extern "C" esp_err_t LUNASHELL_hardware_set_imu_low_power(bool enabled)
{
    const int64_t attempted_at_us = esp_timer_get_time();
#if !CONFIG_LUNASHELL_BUILD_ENABLE_IMU || !CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
    publish_imu_profile_failure(ESP_ERR_NOT_SUPPORTED, attempted_at_us);
    ESP_LOGE(
        TAG,
        "IMU QMI8658 profile transition rejected: driver is unavailable"
    );
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_initialized || !s_imu_ready || s_imu.dev_handle == nullptr) {
        publish_imu_profile_failure(ESP_ERR_INVALID_STATE, attempted_at_us);
        ESP_LOGE(
            TAG,
            "IMU QMI8658 profile transition rejected: hardware or IMU is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> calibration_lock(s_imu_calibration_mutex);
    if (s_imu_calibration_request_pending.load(std::memory_order_acquire)) {
        publish_imu_profile_failure(ESP_ERR_INVALID_STATE, attempted_at_us);
        ESP_LOGE(
            TAG,
            "IMU QMI8658 profile transition rejected: calibration is pending"
        );
        return ESP_ERR_INVALID_STATE;
    }

    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        publish_imu_profile_failure(ESP_ERR_NO_MEM, attempted_at_us);
        ESP_LOGE(
            TAG,
            "IMU QMI8658 profile transition failed: refresh mutex allocation failed"
        );
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        publish_imu_profile_failure(ESP_ERR_TIMEOUT, attempted_at_us);
        ESP_LOGE(
            TAG,
            "IMU QMI8658 profile transition failed: sampler quiescence timed out"
        );
        return ESP_ERR_TIMEOUT;
    }

    if (s_imu_profile_valid && s_imu_profile_low_power == enabled) {
        give_refresh_mutex(refresh_mutex, "IMU profile transition");
        return ESP_OK;
    }

    const bool previous_low_power = s_imu_profile_low_power;
    const bool previous_valid = s_imu_profile_valid;
    const esp_err_t transition_error = configure_imu_profile(enabled);
    if (transition_error != ESP_OK) {
        esp_err_t recovery_error = ESP_ERR_INVALID_STATE;
        if (previous_valid) {
            recovery_error = configure_imu_profile(previous_low_power);
            if (recovery_error == ESP_OK) {
                s_imu_profile_low_power = previous_low_power;
                s_imu_profile_valid = true;
                s_imu_profile_error = ESP_OK;
                reset_imu_filter();
                ESP_LOGW(
                    TAG,
                    "IMU QMI8658 profile transition rolled back to %s",
                    previous_low_power ? "low-power" : "active"
                );
            }
        }
        if (!previous_valid || recovery_error != ESP_OK) {
            esp_err_t invalid_profile_error =
                previous_valid ? recovery_error : transition_error;
            const esp_err_t disable_error = qmi8658_enable_sensors(
                &s_imu,
                QMI8658_DISABLE_ALL
            );
            if (disable_error != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "IMU QMI8658 failed-profile sensor-disable failed (%s)",
                    esp_err_to_name(disable_error)
                );
                invalid_profile_error = disable_error;
            }
            s_imu_profile_valid = false;
            s_imu_profile_error = invalid_profile_error;
        }
        give_refresh_mutex(refresh_mutex, "IMU profile transition rollback");
        publish_imu_profile_failure(transition_error, attempted_at_us);
        return transition_error;
    }

    s_imu_profile_low_power = enabled;
    s_imu_profile_valid = true;
    s_imu_profile_error = ESP_OK;
    reset_imu_filter();
    give_refresh_mutex(refresh_mutex, "IMU profile transition");
    ESP_LOGI(
        TAG,
        "IMU QMI8658 profile: %s (accel=8g/%s gyro=512dps/%s)",
        enabled ? "low-power" : "active",
        enabled ? "31.25Hz" : "500Hz",
        enabled ? "31.25Hz" : "500Hz"
    );
    return ESP_OK;
#endif
}

// Change rotation state only after the shared monitor and snapshot are ready.
static esp_err_t set_auto_rotation_runtime(bool enabled)
{
    if (!s_initialized || !s_imu_ready) {
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
}

// Persist the preference before changing the active sampling policy.
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

    const esp_err_t runtime_error =
        set_auto_rotation_runtime(enabled);
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

// Restore both saved values exactly; invalid persisted data requires an explicit reset.
extern "C" esp_err_t LUNASHELL_display_apply_saved_auto_rotation(void)
{
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
        true,
        &enabled
    );
    if (error != ESP_OK) {
        return error;
    }

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
    s_orientation.applied_rendering_degrees = applied_degrees;
    s_orientation.physical_degrees = rendering_rotation_for_physical(
        static_cast<uint16_t>(saved_rendering_degrees)
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

    s_imu_smoothing_percent.store(percent, std::memory_order_relaxed);
    s_imu_smoothing_valid.store(true, std::memory_order_relaxed);
    reset_imu_filter();
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

extern "C" uint32_t LUNASHELL_hardware_get_imu_smoothing_percent(void)
{
    return s_imu_smoothing_percent.load(std::memory_order_relaxed);
}

// Reject calibration while the IMU is not in its canonical active profile.
extern "C" esp_err_t LUNASHELL_imu_calibrate(void)
{
    const int64_t calibration_attempted_at_us = esp_timer_get_time();
#if !CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    publish_imu_calibration_failure(
        ESP_ERR_NOT_SUPPORTED,
        calibration_attempted_at_us
    );
    return ESP_ERR_NOT_SUPPORTED;
#endif
    if (!s_initialized) {
        ESP_LOGE(TAG, "IMU calibration rejected: hardware is not initialized");
        publish_imu_calibration_failure(
            ESP_ERR_INVALID_STATE,
            calibration_attempted_at_us
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_imu_ready) {
        ESP_LOGE(TAG, "IMU calibration rejected: IMU is not ready");
        publish_imu_calibration_failure(
            ESP_ERR_INVALID_STATE,
            calibration_attempted_at_us
        );
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(s_imu_calibration_mutex);
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
    if (!s_imu_profile_valid || s_imu_profile_low_power) {
        publish_imu_calibration_failure(
            ESP_ERR_INVALID_STATE,
            calibration_attempted_at_us
        );
        ESP_LOGE(
            TAG,
            "IMU calibration rejected: active 500 Hz profile is unavailable"
        );
        return ESP_ERR_INVALID_STATE;
    }
#endif
    TaskHandle_t calibration_task = ensure_imu_calibration_task();
    if (calibration_task == nullptr) {
        publish_imu_calibration_failure(
            ESP_ERR_NO_MEM,
            calibration_attempted_at_us
        );
        ESP_LOGE(TAG, "IMU calibration task creation failed");
        return ESP_ERR_NO_MEM;
    }

    if (s_imu_calibration_request_pending.exchange(
            true,
            std::memory_order_acq_rel
        )) {
        publish_imu_calibration_failure(
            ESP_ERR_INVALID_STATE,
            calibration_attempted_at_us
        );
        ESP_LOGW(TAG, "IMU calibration rejected: another calibration is running");
        return ESP_ERR_INVALID_STATE;
    }

    xTaskNotifyGive(calibration_task);
    ESP_LOGI(TAG, "IMU calibration scheduled");
    return ESP_OK;
}

extern "C" void LUNASHELL_hardware_report_speaker(bool ready, esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportSpeaker(ready, error);
}

extern "C" esp_err_t LUNASHELL_rtc_read(LUNASHELL_calendar_time_t *time)
{
    if (time == nullptr) {
        ESP_LOGE(TAG, "RTC read rejected: output is null");
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);
    LUNASHELL_calendar_time_t sample = {};
    const uint32_t trusted_write_generation_at_read =
        s_rtc_trusted_write_generation.load(std::memory_order_acquire);
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t error = read_rtc_calendar(&sample);
    const esp_err_t publication_error =
        publish_rtc_read(sample, error, attempted_at_us);
    const RtcLatchObservation observation = make_rtc_latch_observation(
        sample,
        error,
        publication_error,
        trusted_write_generation_at_read
    );
    require_rtc_recovery_readback(observation);
    apply_rtc_latch_observation_locked(observation);
    const esp_err_t result = sample_publication_result(
        "RTC PCF85063",
        "direct-read",
        error,
        publication_error
    );
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "RTC read failed (%s)", esp_err_to_name(error));
        return result;
    }
    if (result != ESP_OK) {
        return result;
    }
    *time = sample;
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_rtc_write(const LUNASHELL_calendar_time_t *time)
{
    if (time == nullptr || !valid_calendar_time(*time)) {
        ESP_LOGE(TAG, "RTC write rejected: invalid UTC calendar");
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_rtc_ready || s_rtc == nullptr) {
        ESP_LOGE(TAG, "RTC write rejected: device is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    // Keep the register write and conditional recovery readback indivisible:
    // SNTP and GNSS are independent writers and must not verify each other's
    // calendar or publish their results out of order.
    std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);
    const uint8_t raw[7] = {
        decimal_to_bcd(time->second),
        decimal_to_bcd(time->minute),
        decimal_to_bcd(time->hour),
        decimal_to_bcd(time->day),
        0,
        decimal_to_bcd(time->month),
        decimal_to_bcd(static_cast<uint8_t>(time->year - 2000U)),
    };
    const int64_t attempted_at_us = esp_timer_get_time();
    const bool pending_recovery_at_start =
        s_rtc_recovery_readback_required.exchange(
            false,
            std::memory_order_acq_rel
        );
    bool recovery_readback_required =
        s_rtc_oscillator_stop_latched.load(std::memory_order_acquire) ||
        pending_recovery_at_start;
    esp_err_t error =
        write_registers(s_rtc, PCF85063_REG_SECONDS, raw, sizeof(raw));
    esp_err_t publication_error = ESP_OK;
    if (error == ESP_OK) {
        LUNASHELL_calendar_time_t published = *time;
        published.clock_integrity = true;
        const bool pending_recovery_after_write =
            s_rtc_recovery_readback_required.exchange(
                false,
                std::memory_order_acq_rel
            );
        recovery_readback_required =
            recovery_readback_required ||
            s_rtc_oscillator_stop_latched.load(
                std::memory_order_acquire
            ) ||
            pending_recovery_after_write;
        if (recovery_readback_required) {
            error = read_rtc_calendar(&published);
            if (error == ESP_OK &&
                !rtc_write_readback_matches(*time, published)) {
                ESP_LOGE(
                    TAG,
                    "RTC recovery readback mismatch: requested=%04u-%02u-%02u %02u:%02u:%02u observed=%04u-%02u-%02u %02u:%02u:%02u (%s)",
                    time->year,
                    time->month,
                    time->day,
                    time->hour,
                    time->minute,
                    time->second,
                    published.year,
                    published.month,
                    published.day,
                    published.hour,
                    published.minute,
                    published.second,
                    esp_err_to_name(ESP_ERR_INVALID_RESPONSE)
                );
                error = ESP_ERR_INVALID_RESPONSE;
            }
        }
        publication_error =
            publish_rtc_read(published, error, attempted_at_us);
    } else {
        LUNASHELL_calendar_time_t unavailable = {};
        publication_error =
            publish_rtc_read(unavailable, error, attempted_at_us);
    }

    if (error == ESP_OK && publication_error == ESP_OK) {
        s_rtc_trusted_write_generation.fetch_add(
            1,
            std::memory_order_acq_rel
        );
        s_rtc_oscillator_stop_latched.store(
            false,
            std::memory_order_release
        );
        ESP_LOGI(
            TAG,
            "RTC PCF85063 updated%s: %04u-%02u-%02u %02u:%02u:%02u UTC",
            recovery_readback_required ? " and verified" : "",
            time->year,
            time->month,
            time->day,
            time->hour,
            time->minute,
            time->second
        );
    } else if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "RTC %s failed (%s)",
            recovery_readback_required ? "write/recovery" : "write",
            esp_err_to_name(error)
        );
    }
    if ((error != ESP_OK || publication_error != ESP_OK) &&
        recovery_readback_required) {
        // A consumed recovery request is single-owner but not lossy: any
        // failed write, readback, or publication requires the next trusted
        // write to verify the hardware again.
        s_rtc_recovery_readback_required.store(
            true,
            std::memory_order_release
        );
    }
    return sample_publication_result(
        "RTC PCF85063",
        "write",
        error,
        publication_error
    );
}
