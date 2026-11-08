#pragma once

#include "CODY_battery_alerts.hpp"
#include "CODY_companion_gestures.hpp"
#include "CODY_navigation_visibility.hpp"
#include "CODY_token_history.hpp"
#include "CODY_widget_state.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_build_metadata.hpp"

namespace CODY {

// Canonical model projections. The display renderer borrows these read-only.
struct DeviceDashboardData {
    LUNASHELL::CodexWeeklyLimitSnapshot weekly{};
    LUNASHELL::CodexAccountSnapshot details{};
    LUNASHELL::CodexTokenIntervalsSnapshot intervals{};
    LUNASHELL::ClockSnapshot clock{};
    LUNASHELL::DeviceStatusObservation connection{};
    LUNASHELL::CodexTechnicalObservation context{};
    LUNASHELL::ClockLocalCalendar reset_local{};
    TokenHistoryLocalRange history_local{};
    const LUNASHELL::FirmwareBuildMetadata *build = nullptr;
    const char *build_version = nullptr;
    uint32_t uptime_seconds = 0;
    uint32_t previous_uptime_seconds = 0;
    bool previous_uptime_valid = false;
    bool connection_valid = false;
    bool voice_ready = false;
};

enum class VisibilitySave : uint8_t { Idle, Pending, Failed };

struct DeviceModel {
    WidgetRouteState widgets{};
    WidgetVisibility draft{};
    DeviceDashboardData dashboard{};
    LUNASHELL::DiagnosticsObservation diagnostics{};
    BatteryAlertModel battery{};
    BatteryAlertNotice battery_banner{};
    int64_t battery_banner_until_us = 0;
    NavigationVisibility navigation{};
    CompanionShakeState shake{};
    LUNASHELL::MotionPowerObservation motion{};
    TokenHistoryInterval interval = TokenHistoryInterval::FiveHours;
    WidgetScreen editor_screen = WidgetScreen::Reset;
    VisibilitySave save = VisibilitySave::Idle;
    esp_err_t settings_error = ESP_OK;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    LUNASHELL::VoiceSnapshot voice{};
    CODY2D::Point voice_touch_start{};
    int64_t voice_touch_started_us = 0;
    int64_t voice_tapped_us = 0;
#endif
    bool editing = false;
    bool about_link = false;
};

// Refreshes the canonical weekly limit and clock shared by all awake screens.
void update_device_observations(DeviceDashboardData &data);

// Ages the battery banner against canonical observations and its display dwell.
void update_device_battery(DeviceModel &model, int64_t now_us);

// Projects the currently valid perimeter fill, in basis points.
uint16_t device_allowance_ring(const DeviceDashboardData &data);

// Reads only the canonical observations needed for the selected screen.
// Failed projections are cleared, and the first original read error is returned.
esp_err_t read_device_dashboard(DeviceDashboardData &data, WidgetScreen screen, TokenHistoryInterval interval);

// Boot owner captures the previous RTC sample before the worker overwrites it.
void initialize_device_uptime(DeviceDashboardData &data);
// Worker updates the current projection once per second, with no flash write.
void update_device_uptime(DeviceDashboardData &data, int64_t now_us);

// Consume the raw, timestamp-matched gesture projection without reading the IMU directly.
bool update_device_navigation(DeviceModel &model, int64_t now_us);

} // namespace CODY
