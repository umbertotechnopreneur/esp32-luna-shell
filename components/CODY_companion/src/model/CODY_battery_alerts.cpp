#include "CODY_battery_alerts.hpp"

#include <cstddef>
#include <limits>

#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {
namespace {

struct Threshold {
    BatteryAlertKind kind;
    uint8_t percent;
};

constexpr Threshold kThresholds[]{
    {BatteryAlertKind::Level50, 50},
    {BatteryAlertKind::Level20, 20},
    {BatteryAlertKind::Level10, 10},
    {BatteryAlertKind::Level5, 5},
    {BatteryAlertKind::Empty, 0},
};
constexpr size_t kThresholdCount = sizeof(kThresholds) / sizeof(kThresholds[0]);
constexpr uint8_t kRechargeHysteresis = 3;

// model: Current display owner whose old pending warning must no longer be shown.
// notice: Caller output cleared with the model's pending warning.
void suppress_notice(BatteryAlertModel &model, BatteryAlertNotice &notice)
{
    model.pending = {};
    notice = {};
}

// battery: Canonical aged source; valid percentages never substitute for a failed PMU read.
bool current_battery(const LUNASHELL::BatterySnapshot &battery)
{
    return battery.health == LUNASHELL::DeviceHealth::Ready && battery.last_error == ESP_OK &&
        battery.data_valid && !battery.data_stale && battery.last_success_at_us > 0 &&
        battery.battery_present && battery.level_percent_valid &&
        battery.level_percent >= 0 && battery.level_percent <= 100;
}

// model: Source and delivery state, already confirmed current and on external power.
void rearm_recharged_thresholds(BatteryAlertModel &model)
{
    // A gauge rebound on battery cannot replay a warning. Charging must actually
    // recover beyond this threshold before a later discharge may announce it again.
    for (size_t index = 0; index < kThresholdCount; ++index) {
        if (model.observed_level_percent >= kThresholds[index].percent + kRechargeHysteresis) {
            model.acknowledged_mask &= static_cast<uint8_t>(~(1U << index));
        }
    }
}

} // namespace

// model: Single display-owner state, preserved across routes and standby.
// battery: Current canonical projection; sample age and validity belong to its acquisition owner.
// notice: Receives one most severe eligible warning, never a backlog of crossed levels.
// Exception: Returns invalid source ordering/identity or notice sequence exhaustion.
esp_err_t update_battery_alert(BatteryAlertModel &model, const LUNASHELL::BatterySnapshot &battery,
    BatteryAlertNotice &notice)
{
    const bool previous_current = model.source_current;
    model.source_error = battery.last_error;
    model.source_current = current_battery(battery);
    if (!model.source_current) {
        suppress_notice(model, notice);
        return ESP_OK;
    }
    const auto percent = static_cast<uint8_t>(battery.level_percent);
    const bool external_power = battery.usb_present || battery.vbus_good;
    const bool repeated = battery.last_success_at_us == model.sampled_at_us;
    if (battery.last_success_at_us < model.sampled_at_us ||
        (repeated && (percent != model.observed_level_percent || external_power != model.external_power))) {
        model.source_current = false;
        suppress_notice(model, notice);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (repeated && previous_current) {
        notice = model.pending;
        return ESP_OK;
    }
    model.sampled_at_us = battery.last_success_at_us;
    model.observed_level_percent = percent;
    model.external_power = external_power;
    if (external_power) {
        rearm_recharged_thresholds(model);
        suppress_notice(model, notice);
        return ESP_OK;
    }

    const Threshold *selected = nullptr;
    for (size_t index = 0; index < kThresholdCount; ++index) {
        if (percent <= kThresholds[index].percent && (model.acknowledged_mask & (1U << index)) == 0) {
            selected = &kThresholds[index];
        }
    }
    if (selected == nullptr) {
        suppress_notice(model, notice);
        return ESP_OK;
    }
    if (model.pending.kind != selected->kind || model.pending.level_percent != percent) {
        if (model.issued_sequence == std::numeric_limits<uint32_t>::max()) {
            suppress_notice(model, notice);
            return ESP_ERR_INVALID_STATE;
        }
        model.pending = {++model.issued_sequence, selected->kind, selected->percent, percent};
    }
    notice = model.pending;
    return ESP_OK;
}

// model: Persistent single-owner delivery state.
// notice: Receives the actual current warning, or None after a failed/unavailable observation.
// Exception: Returns original canonical read and model errors without hardware access.
esp_err_t poll_battery_alert(BatteryAlertModel &model, BatteryAlertNotice &notice)
{
    LUNASHELL::BatterySnapshot battery{};
    const esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readBatteryObservation(battery);
    if (error != ESP_OK) {
        model.source_current = false;
        model.source_error = error;
        suppress_notice(model, notice);
        return error;
    }
    return update_battery_alert(model, battery, notice);
}

// model: Display-owner state, not mutated concurrently by the power callback.
// notice: Exact notice whose visible frame retired or whose displayed action was dismissed.
// Returns false without changing delivery state when the notice was superseded or withdrawn.
bool acknowledge_battery_alert(BatteryAlertModel &model, const BatteryAlertNotice &notice)
{
    if (!model.source_current || model.external_power || notice.kind == BatteryAlertKind::None ||
        notice.sequence == 0 || model.pending.sequence != notice.sequence || model.pending.kind != notice.kind ||
        model.pending.threshold_percent != notice.threshold_percent || model.pending.level_percent != notice.level_percent) {
        return false;
    }
    for (size_t index = 0; index < kThresholdCount; ++index) {
        if (kThresholds[index].kind == notice.kind) {
            model.acknowledged_mask |= static_cast<uint8_t>((1U << (index + 1U)) - 1U);
            model.pending = {};
            return true;
        }
    }
    return false;
}

} // namespace CODY
