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
};

struct RtcSnapshot : ComponentSnapshot {
    bool clock_integrity = false;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
};

struct GpsSnapshot : ComponentSnapshot {
    uint32_t pending_nmea_bytes = 0;
    char first_sentence[181] = {};
};

struct AudioSnapshot {
    ComponentSnapshot speaker;
    ComponentSnapshot microphone;
    uint32_t microphone_mean_level = 0;
    uint32_t microphone_peak_level = 0;
};

struct RadioSnapshot {
    ComponentSnapshot wifi;
    ComponentSnapshot ble;
};

struct StorageSnapshot : ComponentSnapshot {
    uint64_t capacity_bytes = 0;
    bool logger_mirror_enabled = false;
};

struct UiHardwareSnapshot {
    ComponentSnapshot display;
    ComponentSnapshot touch;
};

struct MemorySnapshot {
    uint32_t heap_free_bytes = 0;
    uint32_t heap_minimum_free_bytes = 0;
    uint32_t internal_free_bytes = 0;
    uint32_t internal_largest_block_bytes = 0;
    uint32_t psram_free_bytes = 0;
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

    void reportStorage(bool mounted, uint64_t capacity_bytes, bool logger_mirror, esp_err_t error);
    void reportDisplay(bool ready, esp_err_t error);
    void reportTouch(bool ready, esp_err_t error);
    void reportSpeaker(bool ready, esp_err_t error);
    void reportWifi(bool ready, esp_err_t error);
    void reportBle(bool ready, esp_err_t error);

private:
    DeviceSnapshotService() = default;
};

const char *deviceHealthName(DeviceHealth health);

}  // namespace qsee
