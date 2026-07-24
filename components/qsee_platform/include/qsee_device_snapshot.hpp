/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

#pragma once

#include <cstdint>

#include "esp_err.h"

namespace qsee {

enum class DeviceHealth : uint8_t {
    Unknown,
    Ready,
    Degraded,
    Missing,
};

struct ComponentSnapshot {
    DeviceHealth health = DeviceHealth::Unknown;
    esp_err_t last_error = ESP_ERR_INVALID_STATE;
};

struct BatterySnapshot : ComponentSnapshot {
    bool battery_present = false;
    bool usb_present = false;
    int level_percent = -1;
    int battery_mv = 0;
    int usb_mv = 0;
    int system_mv = 0;
    float die_temperature_c = 0.0F;
    char power_state[16] = "unknown";
};

struct ImuSnapshot : ComponentSnapshot {
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

struct AudioSnapshot {
    ComponentSnapshot speaker;
    ComponentSnapshot microphone;
    uint8_t channel_count = 0;
    uint32_t sample_rate_hz = 0;
    uint32_t microphone_mean_level = 0;
    uint32_t microphone_peak_level = 0;
    uint32_t channel_mean_level[2] = {};
    uint32_t channel_peak_level[2] = {};
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

struct StorageSnapshot : ComponentSnapshot {
    bool card_present = false;
    bool format_attempted = false;
    bool formatted = false;
    uint32_t mount_attempt_count = 0;
    uint64_t capacity_bytes = 0;
    uint64_t filesystem_total_bytes = 0;
    uint64_t free_bytes = 0;
    bool logger_mirror_enabled = false;
};

struct UiHardwareSnapshot {
    ComponentSnapshot display;
    ComponentSnapshot touch;
};

struct InputSnapshot {
    ComponentSnapshot io_expander;
    ComponentSnapshot boot_button;
    ComponentSnapshot power_button;
    bool boot_pressed = false;
    bool power_pressed = false;
    uint32_t boot_press_count = 0;
    uint32_t power_short_press_count = 0;
    uint32_t power_long_press_count = 0;
    uint32_t wakeup_cause = 0;
};

struct MemorySnapshot {
    uint32_t flash_capacity_bytes = 0;
    uint32_t app_partition_bytes = 0;
    uint32_t firmware_image_bytes = 0;
    uint32_t heap_free_bytes = 0;
    uint32_t heap_minimum_free_bytes = 0;
    uint32_t internal_free_bytes = 0;
    uint32_t internal_largest_block_bytes = 0;
    bool internal_memory_pressure = false;
    uint32_t psram_capacity_bytes = 0;
    uint32_t psram_heap_total_bytes = 0;
    uint32_t psram_free_bytes = 0;
    uint32_t psram_used_bytes = 0;
    uint32_t psram_largest_block_bytes = 0;
};

struct CoreSnapshot {
    char current_task[24] = "unknown";
    uint32_t stack_high_water_words = 0;
    uint32_t priority = 0;
};

struct ProcessorSnapshot {
    uint8_t core_count = 0;
    uint8_t sampling_core = 0;
    CoreSnapshot cores[2];
};

struct DeviceSnapshot {
    uint32_t sequence = 0;
    int64_t refreshed_at_us = 0;
    char reason[32] = "not-refreshed";
    BatterySnapshot battery;
    ImuSnapshot imu;
    RtcSnapshot rtc;
    GpsSnapshot gps;
    AudioSnapshot audio;
    RadioSnapshot radios;
    StorageSnapshot storage;
    UiHardwareSnapshot ui;
    InputSnapshot input;
    MemorySnapshot memory;
    ProcessorSnapshot processor;
};

/**
 * Canonical point-in-time hardware state.
 *
 * Drivers are sampled only when refresh() is explicitly requested. This keeps
 * sensor acquisition deterministic and gives future sensor fusion one coherent
 * input boundary.
 */
class DeviceSnapshotService final {
public:
    static DeviceSnapshotService &instance();

    esp_err_t initialize();
    esp_err_t refresh(const char *reason = "manual");
    DeviceSnapshot snapshot() const;

    void reportStorage(
        bool mounted,
        bool card_present,
        bool format_attempted,
        bool formatted,
        uint32_t mount_attempt_count,
        uint64_t capacity_bytes,
        uint64_t filesystem_total_bytes,
        uint64_t free_bytes,
        bool logger_mirror,
        esp_err_t error
    );
    void reportDisplay(bool ready, esp_err_t error);
    void reportTouch(bool ready, esp_err_t error);
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
        const char *timezone
    );
    void reportInputs(const InputSnapshot &inputs);

private:
    DeviceSnapshotService() = default;
};

const char *deviceHealthName(DeviceHealth health);

}  // namespace qsee
