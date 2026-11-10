#pragma once

#include <array>

#include "CODY_account_dashboard.hpp"
#include "CODY_battery_alerts.hpp"
#include "CODY_device_effects.hpp"
#include "CODY_local_timer.hpp"
#include "CODY_pet_service.hpp"
#include "CODY_screensaver.hpp"
#include "CODY_companion_gestures.hpp"
#include "CODY_navigation_visibility.hpp"
#include "CODY_system_info.hpp"
#include "CODY_token_history.hpp"
#include "CODY_widget_state.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_build_metadata.hpp"

namespace CODY {

// Canonical model projections. The display renderer borrows these read-only.
struct DeviceDashboardData {
    PetObservation pet{};
    LUNASHELL::CodexWeeklyLimitSnapshot weekly{};
    LUNASHELL::CodexAccountSnapshot details{};
    LUNASHELL::CodexVoucherSnapshot vouchers{};
    LUNASHELL::CodexSavedSnapshot saved{};
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

// Exact UTC instants identify warnings; the read-only list keeps every reported row.
// This bounded, volatile state is deliberately absent from the saved account schema.
struct VoucherModel {
    std::array<uint32_t, LUNASHELL::CodexVoucherSnapshot::kMaximumExpiries> expiries{};
    std::array<uint32_t, LUNASHELL::CodexVoucherSnapshot::kMaximumExpiries> dismissed{};
    uint64_t series = 0;
    uint32_t warning_expiry = 0;
    uint8_t expiry_count = 0;
    uint8_t dismissed_count = 0;
    uint8_t first_row = 0;
};
static_assert(sizeof(VoucherModel) <= 96, "Voucher cards and dismissal identities stay bounded in RAM");

struct DeviceModel {
    DeviceEffectsModel effects{};
    LocalTimerModel timer{};
    ScreensaverModel screensaver{};
    WidgetRouteState widgets{};
    WidgetVisibility draft{};
    DeviceDashboardData dashboard{};
    VoucherModel vouchers{};
    LUNASHELL::DiagnosticsObservation diagnostics{};
    InfoPage info_page = InfoPage::System;
    esp_err_t diagnostics_error = ESP_ERR_INVALID_STATE;
    BatteryAlertModel battery{};
    BatteryAlertNotice battery_banner{};
    int64_t battery_banner_until_us = 0;
    NavigationVisibility navigation{};
    CompanionShakeState shake{};
    LUNASHELL::MotionPowerObservation motion{};
    TokenHistoryInterval interval = TokenHistoryInterval::FiveHours;
    uint8_t editor_page = 0;
    VisibilitySave save = VisibilitySave::Idle;
    esp_err_t settings_error = ESP_OK;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    LUNASHELL::VoiceSnapshot voice{};
    CODY2D::Point voice_touch_start{};
    int64_t voice_touch_started_us = 0;
    int64_t voice_tapped_us = 0;
#endif
    bool editing = false;
};

// data: Receives canonical weekly, voucher and clock observations shared by awake screens.
void update_device_observations(DeviceDashboardData &data);

// data: Canonical live voucher projection and authenticated app presence.
bool device_voucher_expiries_current(const DeviceDashboardData &data);

// model: Refreshes exact expiry identities without retaining stale dates for presentation.
void update_device_vouchers(DeviceModel &model);

// model: Current live dates, trusted clock and per-account dismissal identities.
uint32_t pending_device_voucher_warning(const DeviceModel &model);

// model: Receives a once-per-expiry dismissal for the current account and uptime.
void dismiss_device_voucher_warning(DeviceModel &model);

// model: Current reported dates and viewport offset.
// step: Explicit up/down movement through the list; it stops at the ends.
void step_device_voucher(VoucherModel &model, WidgetStep step);

// model: Current live list; exposes only the rows that fit on Credits.
VoucherList project_device_voucher_list(const DeviceModel &model);

// data: Historical data admitted for the retained pairing while Codex is unavailable.
// screen: Current dashboard page, used to select its independent saved source.
int64_t device_saved_page_epoch(const DeviceDashboardData &data, WidgetScreen screen);

// Ages the battery banner against canonical observations and its display dwell.
void update_device_battery(DeviceModel &model, int64_t now_us);

// Projects the currently valid perimeter fill, in basis points.
uint16_t device_allowance_ring(const DeviceDashboardData &data);

// data: Persistent canonical projections for the dashboard and current turn.
void update_device_statistics(DeviceDashboardData &data);

// data: Current and separately saved observations, owned by the display worker.
// preferences: Stored user choices, copied before filtering for data availability.
// now_us: Monotonic time for chat and context coherence.
WidgetVisibility available_device_statistics(const DeviceDashboardData &data, WidgetVisibility preferences, int64_t now_us);

// data: Dashboard observations and cached calendar conversions.
// screen: Selected, data-qualified page.
// interval: Selected history window.
void project_device_statistics_calendar(DeviceDashboardData &data, WidgetScreen screen, TokenHistoryInterval interval);

// data: Aged canonical history with explicit missing slots.
// interval: Window to inspect for at least one measured count, including zero.
bool device_history_has_data(const DeviceDashboardData &data, TokenHistoryInterval interval);

// model: Retains the selected Info page and its one canonical diagnostics projection.
// Exception: Preserves snapshot read failures; immutable Firmware/ProjectLink pages need no acquisition.
esp_err_t read_device_info(DeviceModel &model);

// Boot owner captures the previous RTC sample before the worker overwrites it.
void initialize_device_uptime(DeviceDashboardData &data);
// Worker updates the current projection once per second, with no flash write.
void update_device_uptime(DeviceDashboardData &data, int64_t now_us);

struct DeviceNavigationUpdate {
    esp_err_t motion_error = ESP_ERR_NOT_SUPPORTED;
    bool shaken = false;
};

// model: Receives the canonical projection shared by gestures and dictation artwork.
// now_us: Monotonic time shared by touch, gesture recognition and the frame owner.
DeviceNavigationUpdate update_device_navigation(DeviceModel &model, int64_t now_us);

} // namespace CODY
