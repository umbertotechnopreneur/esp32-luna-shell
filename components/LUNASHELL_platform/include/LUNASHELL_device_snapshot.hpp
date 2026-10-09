/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include "LUNASHELL_provisioning_state.hpp"

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "sdkconfig.h"
#include "LUNASHELL_clock.hpp"
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
#include "LUNASHELL_speech_state.hpp"
#endif
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
    bool level_percent_valid = false;
    bool battery_mv_valid = false;
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

// Actual system-clock observation by the time owner, not an extrapolated RTC sample.
struct ClockSnapshot : ComponentSnapshot {
    int64_t utc_epoch_seconds = 0;
    uint16_t local_minute = 0;
    uint16_t local_year = 0;
    uint8_t local_month = 0;
    uint8_t local_day = 0;
    uint8_t local_weekday = 0;
    uint8_t local_second = 0;
    ClockTimezone timezone = ClockTimezone::Utc;
    bool preferences_valid = false;
    bool routines_enabled = false;
    bool quiet_hours_enabled = false;
    uint16_t quiet_start_minute = 0;
    uint16_t quiet_end_minute = 0;
    RoutinePeriod period = RoutinePeriod::Unavailable;
    bool quiet = false;
    char source[8] = "none";
};

// Last synchronization measurement is historical, not a continuously sampled metric.
struct RtcDriftSnapshot : ComponentSnapshot {
    ComponentSnapshot baseline;
    bool offset_valid = false;
    bool rate_valid = false;
    int32_t rtc_minus_reference_seconds = 0;
    float rate_ppm = 0;
    float rate_uncertainty_ppm = 0;
    uint32_t interval_seconds = 0;
    uint32_t offset_uncertainty_seconds = 0;
    int64_t reference_utc_seconds = 0;
    int64_t rtc_sampled_at_us = 0;
    char source[8] = "none";
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

enum class PlaybackState : uint8_t { Unavailable, Idle, Playing, Failed };
struct PlaybackSnapshot : ComponentSnapshot {
    PlaybackState state = PlaybackState::Unavailable;
    bool level_valid = false;
    uint32_t mean_level = 0;
    uint32_t sample_rate_hz = 0;
    uint8_t volume_percent = 0;
    // Application cue identity from the successful write; zero means generic PCM.
    uint32_t cue_id = 0;
};

struct AudioSnapshot {
    PlaybackSnapshot playback;
    ComponentSnapshot speaker;
    ComponentSnapshot microphone;
    ComponentSnapshot spectrum;
    uint8_t channel_count = 0;
    uint32_t sample_rate_hz = 0;
    uint32_t microphone_mean_level = 0;
    uint32_t microphone_peak_level = 0;
    uint32_t channel_mean_level[2] = {};
    uint32_t channel_peak_level[2] = {};
    uint8_t channel_spectrum[2][kAudioSpectrumBandCount] = {};
    uint32_t spectrum_sequence = 0;
};

// Stable identities are independent of a model's position in ESP-SR arrays.
enum class WakeWordId : uint8_t {
    None,
    Sophia,
};

enum class VoiceState : uint8_t {
    Disabled,
    Starting,
    Listening,
    Paused,
    Failed,
};

struct VoiceSnapshot : ComponentSnapshot {
    VoiceState state = VoiceState::Disabled;
    WakeWordId configured_word = WakeWordId::None;
    WakeWordId detected_word = WakeWordId::None;
    uint64_t detection_sequence = 0;
    // Historical event time stays intact when listening pauses or fails.
    int64_t detected_at_us = 0;
    char model_name[32] = {};
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

struct ProvisioningSnapshot : ComponentSnapshot {
    ProvisioningState state = ProvisioningState::Idle;
    ProvisioningFailureReason failure_reason = ProvisioningFailureReason::None;
    bool initialized = false;
    bool running = false;
    uint32_t session_id = 0;
    uint32_t event_sequence = 0;
    int64_t changed_at_us = 0;
    esp_err_t cleanup_error = ESP_OK;
    // Pairing secrets and credential payloads never enter the public snapshot.
};

struct PairingSessionSnapshot : ComponentSnapshot {
    PairingSessionState state = PairingSessionState::Idle;
    ProvisioningFailureReason failure_reason = ProvisioningFailureReason::None;
    uint32_t attempt_id = 0;
    uint32_t provisioning_session_id = 0;
    int64_t changed_at_us = 0;
    int64_t deadline_at_us = 0;
    esp_err_t cleanup_error = ESP_OK;
    esp_err_t dma_reservation_error = ESP_OK;
    bool command_pending = false;
};

struct CodexActivitySnapshot : ComponentSnapshot {
    CodexActivityState state = CodexActivityState::None;
    uint64_t revision = 0;
    char chat_id[65] = {}; // SHA-256 identity; no transcript or prompt content.
    char project[25] = {}; // Explicit ASCII display label, never an identity.
};

struct CodexLinkSnapshot : ComponentSnapshot {
    CodexLinkState state = CodexLinkState::Idle;
    uint32_t attempt_id = 0;
    int64_t changed_at_us = 0;
    int64_t deadline_at_us = 0;
    int64_t ready_at_us = 0;
    bool command_pending = false;
    bool pairing_window_active = false;
    bool retained_link_present = false;
    char device_id[13] = {};
    char paired_pc_id[33] = {};
    char peer_ip_address[16] = {};
    CodexActivitySnapshot activity;
    // The displayed code, verifier, salt and link key never enter the snapshot.
};

struct RadioSnapshot {
    WifiSnapshot wifi;
    BleSnapshot ble;
    ProvisioningSnapshot provisioning;
    PairingSessionSnapshot pairing;
    CodexLinkSnapshot codex_link;
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

struct LoggerCaptureReport {
    esp_err_t query_error = ESP_OK;
    int64_t sampled_at_us = 0;
    uint32_t boot_dropped_lines = 0;
    uint32_t usb_dropped_lines = 0;
    uint32_t format_dropped_lines = 0;
    uint32_t queue_dropped_lines = 0;
    uint32_t truncated_lines = 0;
    uint32_t boot_retained_bytes = 0;
    uint32_t boot_archive_bytes = 0;
    esp_err_t capture_error = ESP_OK;
    int64_t capture_error_at_us = 0;
};

struct LoggerCaptureSnapshot : ComponentSnapshot {
    LoggerCaptureReport observation{};
};

enum class StorageDebugWriteStage : uint8_t {
    NotAttempted,
    Directory,
    Open,
    Write,
    Sync,
    Readback,
    Close,
    Complete,
};

// Debug acquisition belongs to the existing storage owner. Last I/O timestamps
// remain unchanged during suspension; a heartbeat is not a new card sample.
struct StorageDebugReport {
    bool enabled = false;
    bool mounted = false;
    bool power_suspended = false;
    bool performance_suspended = false;
    bool terminal_failure = false;
    bool waiting_for_format = false;
    bool resume_pending = false;
    bool logger_enabled = false;
    bool probe_allowed = false;
    uint32_t mount_attempt_count = 0;
    esp_err_t mount_error = ESP_ERR_INVALID_STATE;
    esp_err_t cleanup_error = ESP_OK;
    int64_t heartbeat_at_us = 0;
    esp_err_t previous_publication_error = ESP_OK;
    bool status_attempted = false;
    uint32_t status_mount_attempt_count = 0;
    esp_err_t status_error = ESP_ERR_INVALID_STATE;
    int64_t status_sampled_at_us = 0;
    bool write_attempted = false;
    bool write_blocked = false;
    StorageDebugWriteStage write_stage = StorageDebugWriteStage::NotAttempted;
    esp_err_t write_error = ESP_ERR_INVALID_STATE;
    int write_errno = 0;
    uint32_t write_sequence = 0;
    uint32_t write_mount_attempt_count = 0;
    uint32_t requested_bytes = 0;
    uint32_t written_bytes = 0;
    uint32_t read_bytes = 0;
    int close_errno = 0;
    int64_t write_started_at_us = 0;
    int64_t write_completed_at_us = 0;
    int64_t write_verified_at_us = 0;
};

struct StorageSnapshot : ComponentSnapshot {
    ComponentSnapshot logger_mirror;
    LoggerCaptureSnapshot logger_capture;
    StorageDebugReport debug;
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

struct DisplayFrameSnapshot {
    bool measured = false;
    uint32_t completed_frames = 0;
    uint32_t failed_frames = 0;
    uint32_t last_lock_wait_us = 0;
    uint32_t last_duration_us = 0;
    uint32_t last_rectangle_count = 0;
    uint32_t last_conversion_us = 0;
    uint32_t last_submission_us = 0;
    uint32_t last_completion_wait_us = 0;
    uint16_t last_buffer_count = 0;
    uint16_t last_transfer_rows = 0;
    esp_err_t last_completion_error = ESP_ERR_INVALID_STATE;
    uint32_t retained_dma_bytes = 0;
    uint32_t retained_consumer_bytes = 0;
    esp_err_t retained_consumer_error = ESP_OK;
    bool reset_required = false;
    esp_err_t last_error = ESP_ERR_INVALID_STATE;
    int64_t completed_at_us = 0;
};

struct UiHardwareSnapshot {
    ComponentSnapshot display;
    DisplayFrameSnapshot rendering;
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
    bool microphone_spectrum_monitor_active = false;
    bool full_snapshot_monitor_active = false;
};

struct InputSnapshot {
    ComponentSnapshot io_expander;
    ComponentSnapshot boot_button;
    ComponentSnapshot power_button;
    bool boot_pressed = false;
    bool power_pressed = false;
    bool power_pressed_valid = false;
    int64_t power_pressed_at_us = 0;
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
    WakeWord,
    Inactivity,
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
    uint32_t screen_idle_timeout_seconds =
        power_policy::kDefaultScreenIdleTimeoutSeconds;
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

// Derived from acquired touch coordinates and the displayed character geometry.
struct CharacterInteractionSnapshot : ComponentSnapshot {
    enum class Kind : uint8_t { None, EyeLeftTap, EyeRightTap };
    Kind kind = Kind::None;
};

// Acquired body gesture in logical display coordinates, not an IMU-frame vector.
struct CharacterTouchSnapshot : ComponentSnapshot {
    bool pressed = false;
    uint16_t origin_x = 0;
    uint16_t origin_y = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t rendering_rotation_degrees = 0;
    int64_t gesture_started_at_us = 0;
};

// Outcome of an optional, explicitly requested preference write, not sensor data.
struct CharacterLookSaveSnapshot {
    enum class State : uint8_t { NoAttempt, Saving, Saved, Failed };
    State state = State::NoAttempt;
    uint32_t request_id = 0;
    char requested_id[64]{};
    esp_err_t error = ESP_OK;
    int64_t started_at_us = 0;
    int64_t completed_at_us = 0;
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
    ClockSnapshot clock;
    RtcDriftSnapshot rtc_drift;
    GpsSnapshot gps;
    AudioSnapshot audio;
    VoiceSnapshot voice;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
    SpeechSnapshot speech;
#endif
    CharacterInteractionSnapshot character_interaction;
    CharacterTouchSnapshot character_touch;
    CharacterLookSaveSnapshot character_look_save;
    RadioSnapshot radios;
    StorageSnapshot storage;
    DiagnosticsSettingsSnapshot diagnostics;
    UiHardwareSnapshot ui;
    InputSnapshot input;
    PowerManagementSnapshot power;
    MemorySnapshot memory;
    ProcessorSnapshot processor;
};

#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
struct SpeechObservation {
    ClockSnapshot clock;
    StorageSnapshot storage;
    DeviceOperatingState operating_state = DeviceOperatingState::Active;
};
#endif

struct TouchActivityObservation {
    uint32_t sequence = 0;
    int64_t last_activity_at_us = 0;
};

// A coherent view of the same canonical snapshot, not a second sensor cache.
// Small consumers avoid copying audio spectra and storage inventories on their stack.
struct ConnectivityObservation {
    RadioSnapshot radios;
    DeviceOperatingState operating_state = DeviceOperatingState::Active;
};

// Only the canonical inputs consumed by the internal-stack power worker.
// Unrelated audio, storage and CCP growth must not enlarge its wake call chain.
struct PowerControlObservation {
    WifiSnapshot wifi;
    BleSnapshot ble;
    InputSnapshot input;
    ImuSnapshot imu;
    VoiceSnapshot voice;
};

// Read again after wake: the earlier control sample predates panel restoration.
struct DisplayPolicyObservation {
    ComponentSnapshot brightness;
    ComponentSnapshot display_power_mode;
    uint32_t brightness_percent = 100;
    uint32_t configured_brightness_percent = 100;
    UiHardwareSnapshot::DisplayPowerMode display_power_mode_state =
        UiHardwareSnapshot::DisplayPowerMode::Unknown;
};

// A small coherent projection for informational chrome in either renderer.
// Calendar fields are published by the clock owner; consumers never read live time.
struct DeviceStatusObservation {
    BatterySnapshot battery;
    ClockSnapshot clock;
    WifiSnapshot wifi;
    BleSnapshot ble;
};

// One coherent projection for the native diagnostics HUD, without spectra or routes.
struct DiagnosticsObservation {
    WifiSnapshot wifi;
    MemorySnapshot memory;
    ProcessorSnapshot processor;
    StorageSnapshot storage;
};

struct MotionPowerObservation {
    bool storage_available = false;
    BatterySnapshot battery;
    ImuSnapshot imu;
    ClockSnapshot clock;
    uint16_t rendering_rotation_degrees = 0;
    AudioSnapshot audio;
    VoiceSnapshot voice;
    int64_t touch_last_activity_at_us = 0;
    CharacterInteractionSnapshot character_interaction;
    CharacterTouchSnapshot character_touch;
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
    [[nodiscard]]
    TouchActivityObservation touchActivityObservation() const noexcept;
    DeviceSnapshot snapshot() const;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
    // observation: Coherent speech policy and card availability; never samples hardware.
    // Exception: Returns the original canonical lock or release error.
    esp_err_t readSpeechObservation(SpeechObservation &observation) const;
    // speech: Actual service state, identity, timestamps and original error causes.
    // Exception: Canonical publication failure follows the platform fatal invariant.
    esp_err_t reportSpeech(const SpeechSnapshot &speech);
#endif
    // observation: Canonical radio/session state and power state under one lock.
    esp_err_t readConnectivityObservation(ConnectivityObservation &observation) const;
    // observation: Coherent radio, button, motion and voice inputs, with original timestamps.
    // Exception: Returns the original lock/release error without changing the destination.
    esp_err_t readPowerControlObservation(PowerControlObservation &observation) const;
    // observation: Current applied panel policy from the canonical owner, without hardware I/O.
    // Exception: Returns the original lock/release error without changing the destination.
    esp_err_t readDisplayPolicyObservation(DisplayPolicyObservation &observation) const;
    // observation: Receives battery, local calendar, Wi-Fi and BLE from one canonical lock.
    // No hardware read, live-clock read or renderer-specific cache is introduced here.
    esp_err_t readDeviceStatusObservation(DeviceStatusObservation &observation) const;
    // processor: Receives current/aged CPU fields under the canonical snapshot lock.
    // This read never samples FreeRTOS directly or starts another telemetry cache.
    esp_err_t readProcessorObservation(ProcessorSnapshot &processor) const;
    // observation: Receives canonical Wi-Fi, memory, processors and removable storage.
    // No renderer-side hardware reads or independent telemetry cache are introduced.
    // Exception: Returns the original snapshot lock or release error without publishing a partial copy.
    esp_err_t readDiagnosticsObservation(DiagnosticsObservation &observation) const;
    // frame: Last completed operation, including its timestamp and measured/error flags.
    // This projection never asks the display driver to acquire another measurement.
    esp_err_t readDisplayFrameObservation(DisplayFrameSnapshot &frame) const;
    // observation: Receives published motion/power fields under one snapshot lock.
    // No hardware refresh or independent measurement ownership is introduced here.
    esp_err_t readMotionPowerObservation(MotionPowerObservation &observation) const;
    // clock: Receives only the canonical time-owner observation, aged at read time.
    esp_err_t readClockObservation(ClockSnapshot &clock) const;

    // voice: Receives canonical recognition state and the last historical event.
    esp_err_t readVoiceObservation(VoiceSnapshot &voice) const;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    // state: Confirmed AFE lifecycle state.
    // error: Original initialization, capture or inference error.
    // detected_at_us: Real Sophia detection time, or zero for a status-only update.
    // Returns publication status; the original operation error remains in the snapshot.
    esp_err_t reportVoice(VoiceState state, esp_err_t error, int64_t detected_at_us = 0);
    // audio: Real microphone block statistics acquired by the AFE owner.
    // sampled_at_us: Original completed capture timestamp, never refresh time.
    // error: Original hardware capture error; failures invalidate the sample.
    // Returns publication status, separate from the acquisition result.
    esp_err_t reportVoiceMicrophones(const AudioSnapshot &audio, int64_t sampled_at_us,
                                    esp_err_t error);
#endif
    /** Sample only the fresh IMU/fusion geometry needed by the TOF worker. */
    esp_err_t sampleDistanceGeometry(DistanceGeometrySnapshot *geometry);

    void reportStorage(const StorageMountReport &report);
    // report: Storage-owner heartbeat with unchanged original I/O timestamps.
    // Exception: Returns the original snapshot publication failure.
    esp_err_t reportStorageDebug(const StorageDebugReport &report);
    void reportStorageMirror(
        bool requested,
        bool enabled,
        esp_err_t error,
        int last_errno,
        int64_t changed_at_us,
        uint32_t failure_count
    );
    // report: Original owner query timestamp, counters, retained bytes and actual errors.
    // Exception: Returns an invalid report or the real snapshot publication failure.
    esp_err_t reportLoggerCapture(const LoggerCaptureReport &report);
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
        bool microphone_spectrum_monitor_active,
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
    // frame: Completed operation metrics; unmeasured fields remain explicitly unavailable.
    void reportDisplayFrame(const DisplayFrameSnapshot &frame);
    // bytes: Consumer-owned PSRAM retained because cleanup could not prove exclusive ownership.
    // error: Original cleanup failure, preserved separately from transfer failures.
    void reportDisplayConsumerRetention(uint32_t bytes, esp_err_t error);
    void reportTouch(bool ready, esp_err_t error);
    void reportTouchActivity();
    void reportSpeaker(bool ready, esp_err_t error);
    // active: Whether a successful codec write is currently driving playback.
    // mean_level: Mean absolute PCM16 amplitude from that successful write only.
    // sample_rate_hz: Actual selected playback format.
    // volume_percent: Actual requested codec volume, in the supported 0..100 range.
    // error: Original playback or codec error; failures invalidate the level.
    // cue_id: Stable application identity; zero for generic PCM.
    // Exception: Returns original publication or rejected-value errors.
    esp_err_t reportPlayback(bool active, uint32_t mean_level, uint32_t sample_rate_hz,
                            int volume_percent, esp_err_t error, uint32_t cue_id = 0);
    // kind: Semantic target recognized from displayed geometry and an acquired touch.
    // sampled_at_us: Original report timestamp; duplicate/backward events are rejected.
    esp_err_t reportCharacterInteraction(CharacterInteractionSnapshot::Kind kind, int64_t sampled_at_us);
    // sample: Body gesture derived from an acquired touch and the displayed geometry.
    // sampled_at_us: Original hardware report time; no synthetic release or freshening.
    esp_err_t reportCharacterTouch(const CharacterTouchSnapshot &sample, int64_t sampled_at_us);
    // operation: Actual admitted write or its terminal result, including the original error.
    // Exception: Returns publication/ordering errors; the write result remains in operation.error.
    esp_err_t reportCharacterLookSave(const CharacterLookSaveSnapshot &operation);
    void reportWifi(const WifiSnapshot &wifi);
    // provisioning: Coherent lifecycle observation published by its sole owner.
    void reportProvisioning(const ProvisioningSnapshot &provisioning);
    // pairing: Coherent async controller observation, without any secret payload.
    void reportPairingSession(const PairingSessionSnapshot &pairing);
    // codex_link: Coherent commissioning/liveness state, without code or key material.
    void reportCodexLink(const CodexLinkSnapshot &codex_link);
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
    // clock: Time-owner observation with its original attempt/success timestamps.
    esp_err_t reportClock(const ClockSnapshot &clock);
    // drift: Historical measurement made before correcting RTC against trusted UTC.
    esp_err_t reportRtcDrift(const RtcDriftSnapshot &drift);
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
        uint32_t screen_idle_timeout_seconds,
        uint32_t transition_sequence,
        int64_t last_activity_at_us,
        esp_err_t error
    );

private:
    // No parameters: acquisition owner reads logger counters independently of SD availability.
    // Exception: Returns the original logger query or snapshot publication error.
    esp_err_t refreshLoggerCapture();
    DeviceSnapshotService() = default;
};

const char *deviceHealthName(DeviceHealth health);

}  // namespace LUNASHELL
