#pragma once
#include "LUNASHELL_battery_endurance.hpp"
namespace LUNASHELL {
enum class DeviceHealth : uint8_t { Unknown, Ready, Degraded, Missing };
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
    bool battery_mv_valid = false;
    int battery_mv = 0;
};
struct ClockSnapshot : ComponentSnapshot {
    int64_t utc_epoch_seconds = 0;
};
class DeviceSnapshotService {
public:
    // No parameters: fixture-owned canonical transport.
    static DeviceSnapshotService &instance();
    // destination: Original aged battery observation supplied by the fixture.
    esp_err_t readBatteryObservation(BatterySnapshot &destination) const;
    // No parameters: simulates the canonical acquisition-owner boundary only.
    esp_err_t refreshBatteryForEndurance();
    // report: Real service projection captured without implementing recorder policy.
    esp_err_t reportBatteryEndurance(const BatteryEnduranceSnapshot &report);
    // clock: Original canonical UTC observation supplied by the fixture.
    esp_err_t readClockObservation(ClockSnapshot &clock) const;
};
}
