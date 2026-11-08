#pragma once

#include <cstdint>

#include "esp_err.h"

namespace LUNASHELL { struct BatterySnapshot; }

namespace CODY {

enum class BatteryAlertKind : uint8_t { None, Level50, Level20, Level10, Level5, Empty };

// A notice describes an observed level, never a predicted shutdown time or cutoff.
struct BatteryAlertNotice {
    uint32_t sequence = 0;
    BatteryAlertKind kind = BatteryAlertKind::None;
    uint8_t threshold_percent = 0;
    uint8_t level_percent = 0;
};

// Single display-owner state. No hardware access, persistence or independent task.
struct BatteryAlertModel {
    BatteryAlertNotice pending{};
    int64_t sampled_at_us = 0;
    uint32_t issued_sequence = 0;
    esp_err_t source_error = ESP_ERR_NOT_FOUND;
    uint8_t acknowledged_mask = 0;
    uint8_t observed_level_percent = 0;
    bool external_power = false;
    bool source_current = false;
};
static_assert(sizeof(BatteryAlertModel) <= 48, "Battery alerts retain only bounded delivery and sample state");

// model: Display-owned threshold and delivery state, retained across routes and standby.
// battery: Already-aged canonical sample, with its original PMU error and source timestamp.
// notice: Receives the most severe eligible notice, or None when no current warning is due.
// Exception: Rejects reversed/conflicting source timestamps and exhausted notice identities.
// Unavailable samples suppress pending warnings without acknowledging or rearming thresholds.
esp_err_t update_battery_alert(BatteryAlertModel &model, const LUNASHELL::BatterySnapshot &battery,
    BatteryAlertNotice &notice);

// model: Same single-owner model used for presentation and acknowledgment.
// notice: Receives the current highest-severity warning after a canonical snapshot read.
// Exception: Returns original snapshot-read or model errors; never samples hardware.
esp_err_t poll_battery_alert(BatteryAlertModel &model, BatteryAlertNotice &notice);

// model: Display-owned state whose current pending notice must match the retired presentation.
// notice: Exact notice that was shown successfully or explicitly dismissed by the user.
// Returns false for a stale/mismatched notice; success acknowledges it and all milder thresholds.
bool acknowledge_battery_alert(BatteryAlertModel &model, const BatteryAlertNotice &notice);

} // namespace CODY
