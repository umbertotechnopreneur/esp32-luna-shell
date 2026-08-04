/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "LUNASHELL_power_policy.hpp"

namespace LUNASHELL {

struct SensorFusionResult;

enum class DeviceHealth : uint8_t {
    Unknown,
    Ready,
    Degraded,
    Missing,
};

struct ComponentSnapshot {
    DeviceHealth health = DeviceHealth::Unknown;
    esp_err_t last_error = ESP_ERR_INVALID_STATE;
    bool data_valid = false;
    bool data_stale = false;
    int64_t last_attempt_at_us = 0;
    int64_t last_success_at_us = 0;
};

struct BatterySnapshot : ComponentSnapshot {
    bool battery_present = false;
    bool usb_present = false;
    bool vbus_good = false;
    bool batfet_enabled = false;
    bool battery_active_mode = false;
    bool thermal_regulation_active = false;
    bool input_current_limit_active = false;
    int level_percent = -1;
    int battery_mv = 0;
    int usb_mv = 0;
    int system_mv = 0;
    float die_temperature_c = 0.0F;
    char power_state[16] = "unknown";
    char charger_phase[20] = "unknown";
};

struct ImuSnapshot : ComponentSnapshot {
    ComponentSnapshot calibration;
    float acceleration_mps2[3] = {};
    float angular_velocity_dps[3] = {};
    float temperature_c = 0.0F;
    bool calibrated = false;
    bool calibration_stationary = false;
    uint16_t calibration_samples = 0;
    float calibration_accel_max_span_mps2 = 0.0F;
    float calibration_gyro_max_span_dps = 0.0F;
    float acceleration_bias_mps2[3] = {};
    float angular_velocity_bias_dps[3] = {};
};

struct RtcSnapshot : ComponentSnapshot {
    ComponentSnapshot system_time;
    bool clock_integrity = false;
    bool system_time_valid = false;
    bool synchronized = false;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    char source[8] = "none";
    char timezone[40] = "UTC0";
};

struct GpsSnapshot : ComponentSnapshot {
    uint32_t pending_nmea_bytes = 0;
    bool has_fix = false;
    bool utc_valid = false;
    uint8_t fix_quality = 0;
    uint8_t satellites = 0;
    double latitude_degrees = 0.0;
    double longitude_degrees = 0.0;
    float altitude_m = 0.0F;
    float speed_mps = 0.0F;
    float course_degrees = 0.0F;
    float hdop = 0.0F;
    char utc_time[11] = {};
    char utc_date[11] = {};
    char first_sentence[181] = {};
};

constexpr std::size_t kAudioSpectrumBandCount = 16;

struct AudioSnapshot {
    ComponentSnapshot speaker;
    ComponentSnapshot microphone;
    uint8_t channel_count = 0;
    uint32_t sample_rate_hz = 0;
    uint32_t microphone_mean_level = 0;
    uint32_t microphone_peak_level = 0;
    uint32_t channel_mean_level[2] = {};
    uint32_t channel_peak_level[2] = {};
    uint8_t channel_spectrum[2][kAudioSpectrumBandCount] = {};
    uint32_t spectrum_sequence = 0;
};

struct WifiSnapshot : ComponentSnapshot {
    bool started = false;
    bool credentials_configured = false;
    bool connect_requested = false;
    bool connected = false;
    bool ip_acquired = false;
    bool reconnect_pending = false;
    int8_t rssi = 0;
    uint8_t channel = 0;
    uint16_t last_disconnect_reason = 0;
    uint32_t event_sequence = 0;
    uint32_t connect_attempt_count = 0;
    uint32_t reconnect_attempt_count = 0;
    uint32_t disconnect_count = 0;
    char ssid[33] = {};
    char ip_address[16] = {};
    char mac_address[18] = {};
};

struct BleSnapshot : ComponentSnapshot {
    bool provisioning = false;
    bool advertising = false;
    bool connected = false;
    uint16_t connection_handle = 0xFFFF;
    uint32_t connection_count = 0;
    uint32_t disconnection_count = 0;
    char device_name[20] = {};
};

struct RadioSnapshot {
    WifiSnapshot wifi;
    BleSnapshot ble;
};

enum class StorageMountOutcome : uint8_t {
    NotAttempted,
    Mounted,
    CardOrTransportFailed,
    FormatRequired,
    FilesystemFailed,
    FormatFailed,
    InternalFailed,
};

enum class StorageMountStage : uint8_t {
    None,
    Request,
    HostInit,
    SlotInit,
    CardInit,
    CardIdentity,
    DriveAlloc,
    DiskIoRegister,
    VfsRegister,
    FatMount,
    PreFormatUnmount,
    Partition,
    Format,
    PostFormatMount,
    RuntimeStatus,
    RuntimeFilesystem,
    RuntimeUnmount,
};

enum class StorageCleanupStage : uint8_t {
    None,
    FatUnmount,
    VfsUnregister,
    HostDeinit,
};

struct StorageMountReport {
    StorageMountOutcome outcome = StorageMountOutcome::NotAttempted;
    StorageMountStage stage = StorageMountStage::None;
    StorageCleanupStage cleanup_stage = StorageCleanupStage::None;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    esp_err_t cleanup_error = ESP_OK;
    bool initial_fatfs_result_valid = false;
    int32_t initial_fatfs_result = 0;
    bool fatfs_result_valid = false;
    int32_t fatfs_result = 0;
    bool cleanup_fatfs_result_valid = false;
    int32_t cleanup_fatfs_result = 0;
    bool cleanup_complete = true;
    bool poisoned = false;
    bool mounted = false;
    bool card_response_verified = false;
    bool format_required = false;
    bool authorization_consumed = false;
    bool authorization_identity_matched = false;
    bool format_attempted = false;
    bool partition_written = false;
    bool formatted = false;
    uint32_t mount_attempt_count = 0;
    uint64_t capacity_bytes = 0;
    uint64_t filesystem_total_bytes = 0;
    uint64_t free_bytes = 0;
    bool logger_mirror_requested = true;
    bool logger_mirror_enabled = false;
};

struct StorageSnapshot : ComponentSnapshot {
    ComponentSnapshot logger_mirror;
    StorageMountOutcome mount_outcome = StorageMountOutcome::NotAttempted;
    StorageMountStage mount_stage = StorageMountStage::None;
    StorageCleanupStage cleanup_stage = StorageCleanupStage::None;
    esp_err_t cleanup_error = ESP_OK;
    bool initial_fatfs_result_valid = false;
    int32_t initial_fatfs_result = 0;
    bool fatfs_result_valid = false;
    int32_t fatfs_result = 0;
    bool cleanup_fatfs_result_valid = false;
    int32_t cleanup_fatfs_result = 0;
    bool cleanup_complete = true;
    bool poisoned = false;
    bool mounted = false;
    bool card_response_verified = false;
    bool format_required = false;
    bool authorization_consumed = false;
    bool authorization_identity_matched = false;
    bool format_attempted = false;
    bool partition_written = false;
    bool formatted = false;
    uint32_t mount_attempt_count = 0;
    uint64_t capacity_bytes = 0;
    uint64_t filesystem_total_bytes = 0;
    uint64_t free_bytes = 0;
    bool logger_mirror_requested = true;
    bool logger_mirror_enabled = false;
    int logger_mirror_errno = 0;
    int64_t logger_mirror_changed_at_us = 0;
    uint32_t logger_mirror_failure_count = 0;
};

struct OrientationSnapshot : ComponentSnapshot {
    bool auto_rotation_enabled = false;
    bool gravity_valid = false;
    bool rotation_pending = false;
    uint16_t physical_orientation_degrees = 0;
    uint16_t rendering_rotation_degrees = 0;
    uint32_t change_sequence = 0;
    int64_t sampled_at_us = 0;
    int64_t changed_at_us = 0;
};

struct UiHardwareSnapshot {
    ComponentSnapshot display;
    ComponentSnapshot touch;
    ComponentSnapshot brightness;
    ComponentSnapshot display_power_mode;
    uint32_t touch_activity_sequence = 0;
    int64_t touch_last_activity_at_us = 0;
    uint32_t brightness_percent = 100;
    uint32_t configured_brightness_percent = 100;
    enum class DisplayPowerMode : uint8_t {
        Unknown,
        Active,
        Dimmed,
        DisplayOff,
    } display_power_mode_state = DisplayPowerMode::Unknown;
    OrientationSnapshot orientation;
};

struct SensorFusionSnapshot : ComponentSnapshot {
    bool gravity_dominant = false;
    bool stationary = false;
    float acceleration_magnitude_mps2 = 0.0F;
    float angular_speed_dps = 0.0F;
};

struct DistanceGeometrySnapshot {
    ComponentSnapshot imu;
    ComponentSnapshot sensor_fusion;
    float acceleration_mps2[3] = {};
    bool imu_calibrated = false;
    bool gravity_dominant = false;
    bool stationary = false;
};

constexpr std::size_t kTofSensorCount = 2;
constexpr uint16_t kDistanceMinimumSampleCount = 20;
constexpr uint16_t kDistanceMaximumSampleCount = 500;
constexpr uint16_t kDistanceMinimumReferenceMm = 500;
constexpr uint16_t kDistanceMaximumReferenceMm = 1500;

enum class DistanceCalibrationState : uint8_t {
    Required,
    Ready,
    Running,
    Succeeded,
    Failed,
};

struct TofSensorSnapshot : ComponentSnapshot {
    uint16_t raw_distance_mm = 0;
    uint16_t filtered_distance_mm = 0;
    uint16_t window_sample_count = 0;
    uint16_t inlier_count = 0;
    uint16_t median_absolute_deviation_mm = 0;
    int32_t calibration_offset_mm = 0;
    uint32_t frame_sequence = 0;
    uint32_t crc_error_count = 0;
    uint32_t protocol_error_count = 0;
    uint32_t timeout_count = 0;
    uint32_t uart_error_count = 0;
};

struct DistanceSettingsSnapshot : ComponentSnapshot {
    uint16_t sample_count = kDistanceMaximumSampleCount;
    uint16_t reference_distance_mm = 1000;
    uint32_t sample_interval_ms = 250;
};

struct DistanceCalibrationSnapshot : ComponentSnapshot {
    DistanceCalibrationState state = DistanceCalibrationState::Required;
    bool record_valid = false;
    uint16_t requested_samples = 0;
    uint16_t accepted_samples[kTofSensorCount] = {};
    uint16_t rejected_samples[kTofSensorCount] = {};
    uint16_t reference_distance_mm = 0;
    uint8_t progress_percent = 0;
    int32_t offsets_mm[kTofSensorCount] = {};
    int8_t z_axis_sign = 0;
    int64_t started_at_us = 0;
    int64_t completed_at_us = 0;
};

struct DistanceSnapshot : ComponentSnapshot {
    TofSensorSnapshot sensors[kTofSensorCount];
    DistanceSettingsSnapshot settings;
    DistanceCalibrationSnapshot calibration;
    uint16_t slant_distance_mm = 0;
    uint16_t vertical_distance_mm = 0;
    uint16_t sensor_spread_mm = 0;
    uint16_t samples_used = 0;
    float tilt_degrees = 0.0F;
    uint8_t confidence_percent = 0;
    bool tilt_compensated = false;
    bool acquisition_timing_valid = false;
    uint32_t tof_pair_skew_us = 0;
    uint32_t geometry_skew_us = 0;
    uint32_t rejected_pair_count = 0;
    uint32_t timing_rejection_count = 0;
    uint32_t consecutive_transport_failure_count = 0;
    uint32_t recovery_attempt_count = 0;
    uint32_t current_backoff_ms = 0;
    bool worker_stack_high_water_valid = false;
    uint32_t worker_stack_min_free_bytes = 0;
};

struct DiagnosticsSettingsSnapshot {
    ComponentSnapshot sample_period;
    ComponentSnapshot imu_smoothing;
    ComponentSnapshot monitor;
    ComponentSnapshot full_snapshot_period;
    ComponentSnapshot full_snapshot_monitor;
    uint32_t sample_period_ms = 300;
    uint32_t imu_smoothing_percent = 70;
    uint32_t full_snapshot_period_ms = 1000;
    bool monitor_active = false;
    bool imu_monitor_active = false;
    bool microphone_monitor_active = false;
    bool full_snapshot_monitor_active = false;
};

struct InputSnapshot {
    ComponentSnapshot io_expander;
    ComponentSnapshot boot_button;
    ComponentSnapshot power_button;
    bool boot_pressed = false;
    bool power_pressed = false;
    uint32_t boot_press_count = 0;
    uint32_t boot_long_press_count = 0;
    uint32_t power_short_press_count = 0;
    uint32_t power_long_press_count = 0;
    uint32_t wakeup_cause = 0;
};

enum class DeviceOperatingState : uint8_t {
    Active,
    EnteringStandby,
    Standby,
    Waking,
};

enum class DeviceStateReason : uint8_t {
    Startup,
    BootButton,
    PowerButton,
    Touch,
    ImuMotion,
};

struct PowerManagementSnapshot : ComponentSnapshot {
    DeviceOperatingState state = DeviceOperatingState::Active;
    DeviceStateReason reason = DeviceStateReason::Startup;
    bool audio_suspended = false;
    bool fullscreen_active = false;
    bool command_arc_visible = true;
    bool status_arc_visible = true;
    UiHardwareSnapshot::DisplayPowerMode display_idle_stage =
        UiHardwareSnapshot::DisplayPowerMode::Active;
    uint32_t command_arc_timeout_seconds =
        power_policy::kDefaultCommandArcTimeoutSeconds;
    uint32_t status_arc_timeout_seconds =
        power_policy::kDefaultStatusArcTimeoutSeconds;
    uint32_t display_dim_timeout_seconds =
        power_policy::kDefaultDisplayDimTimeoutSeconds;
    uint32_t screen_sleep_delay_seconds =
        power_policy::kDefaultScreenSleepDelaySeconds;
    uint32_t transition_sequence = 0;
    int64_t last_activity_at_us = 0;
    int64_t changed_at_us = 0;
};

struct MemorySnapshot : ComponentSnapshot {
    uint32_t flash_capacity_bytes = 0;
    uint32_t app_partition_bytes = 0;
    uint32_t firmware_image_bytes = 0;
    uint32_t heap_total_bytes = 0;
    uint32_t heap_free_bytes = 0;
    uint32_t heap_minimum_free_bytes = 0;
    uint32_t internal_heap_total_bytes = 0;
    uint32_t internal_free_bytes = 0;
    uint32_t internal_largest_block_bytes = 0;
    uint32_t internal_minimum_free_bytes = 0;
    uint32_t internal_allocated_blocks = 0;
    uint32_t internal_free_blocks = 0;
    uint32_t internal_total_blocks = 0;
    bool internal_memory_pressure = false;
    uint32_t psram_capacity_bytes = 0;
    uint32_t psram_heap_total_bytes = 0;
    uint32_t psram_free_bytes = 0;
    uint32_t psram_used_bytes = 0;
    uint32_t psram_largest_block_bytes = 0;
    uint32_t psram_minimum_free_bytes = 0;
    uint32_t psram_allocated_blocks = 0;
    uint32_t psram_free_blocks = 0;
    uint32_t psram_total_blocks = 0;
    bool allocation_failure_monitor_active = false;
    uint32_t allocation_failure_count = 0;
    uint32_t largest_failed_allocation_bytes = 0;
    uint32_t failed_allocation_caps_seen_mask = 0;
};

struct CoreSnapshot {
    bool available = false;
    bool utilization_available = false;
    uint8_t utilization_percent = 0;
    char current_task[24] = "unavailable";
    uint32_t stack_high_water_words = 0;
    uint32_t priority = 0;
};

struct ProcessorSnapshot : ComponentSnapshot {
    uint8_t core_count = 0;
    uint8_t sampling_core = 0;
    CoreSnapshot cores[2];
};

struct DeviceSnapshot {
    uint32_t sequence = 0;
    int64_t last_update_attempt_at_us = 0;
    bool last_update_complete = false;
    esp_err_t last_update_error = ESP_ERR_INVALID_STATE;
    char reason[32] = "not-refreshed";
    BatterySnapshot battery;
    ImuSnapshot imu;
    SensorFusionSnapshot sensor_fusion;
    DistanceSnapshot distance;
    RtcSnapshot rtc;
    GpsSnapshot gps;
    AudioSnapshot audio;
    RadioSnapshot radios;
    StorageSnapshot storage;
    DiagnosticsSettingsSnapshot diagnostics;
    UiHardwareSnapshot ui;
    InputSnapshot input;
    PowerManagementSnapshot power;
    MemorySnapshot memory;
    ProcessorSnapshot processor;
};

/**
 * Canonical point-in-time hardware state.
 *
 * Drivers are sampled only through refresh(). Manual requests and the periodic
 * Full Snapshot worker share that serialized input boundary, while lightweight
 * diagnostic publication remains separate.
 */
class DeviceSnapshotService final {
public:
    static DeviceSnapshotService &instance();

    esp_err_t initialize();
    /**
     * Refresh all full-snapshot components.
     *
     * The periodic worker may provide a non-zero diagnostic reuse window. A
     * committed IMU or microphone sample can then be consumed once without
     * changing the time at which that hardware measurement was taken.
     */
    esp_err_t refresh(
        const char *reason = "manual",
        uint32_t diagnostic_reuse_limit_ms = 0
    );
    DeviceSnapshot snapshot() const;
    /** Sample only the fresh IMU/fusion geometry needed by the TOF worker. */
    esp_err_t sampleDistanceGeometry(DistanceGeometrySnapshot *geometry);

    void reportStorage(const StorageMountReport &report);
    void reportStorageMirror(
        bool requested,
        bool enabled,
        esp_err_t error,
        int last_errno,
        int64_t changed_at_us,
        uint32_t failure_count
    );
    void reportDiagnosticPeriod(uint32_t sample_period_ms, esp_err_t error);
    void reportImuSmoothing(uint32_t imu_smoothing_percent, esp_err_t error);
    void reportFullSnapshotPeriod(
        uint32_t full_snapshot_period_ms,
        esp_err_t error
    );
    void reportFullSnapshotMonitor(bool monitor_active, esp_err_t error);
    void reportSensorFusion(
        const SensorFusionResult &result,
        esp_err_t error,
        int64_t sampled_at_us = 0
    );
    void reportDistance(const DistanceSnapshot &distance);
    void reportDiagnosticMonitor(
        bool imu_monitor_active,
        bool microphone_monitor_active,
        esp_err_t error
    );
    void reportDisplayBrightness(uint32_t brightness_percent, esp_err_t error);
    void reportDisplayConfiguredBrightness(
        uint32_t brightness_percent,
        esp_err_t error
    );
    void reportDisplayPowerMode(
        UiHardwareSnapshot::DisplayPowerMode display_power_mode,
        esp_err_t error
    );
    void reportDisplay(bool ready, esp_err_t error);
    void reportTouch(bool ready, esp_err_t error);
    void reportTouchActivity();
    void reportSpeaker(bool ready, esp_err_t error);
    void reportWifi(const WifiSnapshot &wifi);
    void reportBle(
        bool ready,
        esp_err_t error,
        bool advertising = false,
        bool connected = false,
        uint16_t connection_handle = 0xFFFF,
        uint32_t connection_count = 0,
        uint32_t disconnection_count = 0,
        const char *device_name = nullptr,
        bool provisioning = false
    );
    void reportTime(
        bool system_time_valid,
        bool synchronized,
        const char *source,
        const char *timezone,
        esp_err_t error
    );
    void reportInputs(const InputSnapshot &inputs);
    void reportPowerManagement(
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
    );

private:
    DeviceSnapshotService() = default;
};

const char *deviceHealthName(DeviceHealth health);

}  // namespace LUNASHELL
