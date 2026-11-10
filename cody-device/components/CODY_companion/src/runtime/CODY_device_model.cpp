#include "CODY_device_model.hpp"

#include <algorithm>

#include "LUNASHELL_platform.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"

namespace CODY {
namespace {

constexpr int64_t kNavigationTimeoutUs = 10000000;
constexpr uint32_t kUptimeMagic = 0x4c554e41;

struct RetainedUptime {
    uint32_t magic;
    uint32_t seconds;
    uint32_t inverse;
};
RTC_NOINIT_ATTR RetainedUptime s_uptime;

// clock: Canonical zone preference for this device.
// epoch: The UTC instant belonging to the source observation.
// calendar: Cached conversion of that exact instant in the same local zone.
void project_local(const LUNASHELL::ClockSnapshot &clock, int64_t epoch, LUNASHELL::ClockLocalCalendar &calendar)
{
    if (!clock.preferences_valid || epoch <= 0) {
        calendar = {};
    } else if (calendar.utc_epoch_seconds != epoch || calendar.timezone != clock.timezone) {
        if (LUNASHELL::project_clock_local_calendar(epoch, calendar) != ESP_OK) {
            calendar = {};
        }
    }
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
// phase: Current owner-published phase of the optional recording service.
bool voice_input_idle(LUNASHELL::VoiceInputPhase phase)
{
    using Phase = LUNASHELL::VoiceInputPhase;
    return phase == Phase::Idle || phase == Phase::Delivered || phase == Phase::Cancelled ||
        phase == Phase::Unconfirmed || phase == Phase::Failed;
}

// snapshots: Canonical observation owner for voice and LAN admission.
bool record_voice_available(const LUNASHELL::DeviceSnapshotService &snapshots)
{
    LUNASHELL::VoiceInputAdmissionObservation admission{};
    if (snapshots.readVoiceInputAdmissionObservation(admission) != ESP_OK ||
        !admission.dictation_valid || admission.dictation_error != ESP_OK ||
        !admission.dictation_enabled || !admission.lan_ready) {
        return false;
    }
    LUNASHELL::VoiceSnapshot voice{};
    return snapshots.readVoiceObservation(voice) == ESP_OK &&
        voice.data_valid && !voice.data_stale && voice.last_error == ESP_OK &&
        voice.state == LUNASHELL::VoiceState::Listening && voice_input_idle(voice.input.phase);
}
#endif

} // namespace

// data: Canonical weekly allowance, live vouchers and device observations shared by awake screens.
void update_device_observations(DeviceDashboardData &data)
{
    read_pet_status(data.pet);
    const auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    if (snapshots.readCodexWeeklyLimit(data.weekly) != ESP_OK) data.weekly = {};
    const auto previous_voucher_error = data.vouchers.last_error;
    const auto voucher_error = snapshots.readCodexVouchers(data.vouchers);
    if (voucher_error != ESP_OK) {
        if (voucher_error != previous_voucher_error) {
            ESP_LOGW("CODY_device_model", "Voucher snapshot read failed (%s)", esp_err_to_name(voucher_error));
        }
        data.vouchers = {};
        data.vouchers.last_error = voucher_error;
    }
    data.connection_valid = snapshots.readDeviceStatusObservation(data.connection) == ESP_OK;
    if (!data.connection_valid) data.connection = {};
    data.clock = data.connection.clock;
    if (data.connection_valid && !data.connection.codex_app_open) {
        const auto error = snapshots.readCodexSaved(data.saved);
        if (error != ESP_OK) {
            data.saved = {};
            data.saved.storage_error = error;
        }
    } else {
        data.saved = {};
    }
    data.voice_ready = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    data.voice_ready = data.connection_valid && record_voice_available(snapshots);
#endif
}

// data: Canonical live voucher projection and independently aged authenticated connection.
bool device_voucher_expiries_current(const DeviceDashboardData &data)
{
    const auto &vouchers = data.vouchers;
    return data.connection_valid && data.connection.codex_app_open &&
        vouchers.health == LUNASHELL::DeviceHealth::Ready && vouchers.last_error == ESP_OK &&
        vouchers.data_valid && !vouchers.data_stale && vouchers.series != 0 && vouchers.expiries_valid &&
        vouchers.expiry_count <= vouchers.kMaximumExpiries;
}

// model: Current exact dates, trusted UTC clock and bounded per-account dismissal state.
uint32_t pending_device_voucher_warning(const DeviceModel &model)
{
    const auto &data = model.dashboard;
    const auto &clock = data.clock;
    if (!device_voucher_expiries_current(data) || clock.health != LUNASHELL::DeviceHealth::Ready ||
        !clock.data_valid || clock.data_stale || clock.last_error != ESP_OK || clock.utc_epoch_seconds <= 0 ||
        !clock.preferences_valid || clock.quiet) {
        return 0;
    }
    const auto &vouchers = model.vouchers;
    for (uint8_t index = 0; index < vouchers.expiry_count; ++index) {
        const auto expiry = vouchers.expiries[index];
        const int64_t remaining = static_cast<int64_t>(expiry) - clock.utc_epoch_seconds;
        if (expiry == 0 || remaining <= 0 || remaining > 86400) {
            continue;
        }
        const auto end = vouchers.dismissed.begin() + vouchers.dismissed_count;
        if (std::find(vouchers.dismissed.begin(), end, expiry) == end) return expiry;
    }
    return 0;
}

// model: Display-owned source projection, viewport and live warning state.
void update_device_vouchers(DeviceModel &model)
{
    auto &vouchers = model.vouchers;
    const auto &source = model.dashboard.vouchers;
    // A source gap must not forget dismissals. Only a valid new account identity resets them.
    if (source.health == LUNASHELL::DeviceHealth::Ready && source.data_valid &&
        !source.data_stale && source.last_error == ESP_OK &&
        source.series != 0 && source.series != vouchers.series) {
        vouchers = {};
        vouchers.series = source.series;
    }
    vouchers.expiry_count = 0;
    if (device_voucher_expiries_current(model.dashboard)) {
        std::copy_n(source.expiries_unix_seconds, source.expiry_count, vouchers.expiries.begin());
        const auto end = vouchers.expiries.begin() + source.expiry_count;
        // Positive dates sort chronologically; explicit non-expiring vouchers follow them.
        std::sort(vouchers.expiries.begin(), end, [](uint32_t left, uint32_t right) {
            return right == 0 ? left != 0 : left != 0 && left < right;
        });
        vouchers.expiry_count = source.expiry_count;
        const auto kept = std::remove_if(vouchers.dismissed.begin(),
            vouchers.dismissed.begin() + vouchers.dismissed_count, [&](uint32_t expiry) {
                return std::find(vouchers.expiries.begin(), end, expiry) == end;
            });
        vouchers.dismissed_count = static_cast<uint8_t>(kept - vouchers.dismissed.begin());
        vouchers.first_row = std::min(vouchers.first_row, VoucherList::last_offset(vouchers.expiry_count));
    }
    // A warning can never outlive its current source, trusted clock or admission window.
    if (vouchers.warning_expiry != 0 && pending_device_voucher_warning(model) != vouchers.warning_expiry) {
        vouchers.warning_expiry = 0;
    }
}

// model: Records the visible warning's exact date without any flash writes.
void dismiss_device_voucher_warning(DeviceModel &model)
{
    auto &vouchers = model.vouchers;
    const auto expiry = vouchers.warning_expiry;
    if (expiry != 0 && vouchers.dismissed_count < vouchers.dismissed.size()) {
        const auto end = vouchers.dismissed.begin() + vouchers.dismissed_count;
        if (std::find(vouchers.dismissed.begin(), end, expiry) == end) {
            vouchers.dismissed[vouchers.dismissed_count++] = expiry;
        }
    }
    vouchers.warning_expiry = 0;
}

// model: The bounded chronological voucher viewport.
// step: Scroll direction, independent of horizontal dashboard navigation.
void step_device_voucher(VoucherModel &model, WidgetStep step)
{
    if (model.expiry_count < 2) return;
    const uint8_t last = VoucherList::last_offset(model.expiry_count);
    if (step == WidgetStep::Previous && model.first_row > 0) --model.first_row;
    if (step == WidgetStep::Next && model.first_row < last) ++model.first_row;
}

// model: Retains current source validity and the viewport position.
VoucherList project_device_voucher_list(const DeviceModel &model)
{
    VoucherList list{};
    list.current = device_voucher_expiries_current(model.dashboard);
    list.stale = model.dashboard.vouchers.data_stale;
    if (list.current) {
        const auto &vouchers = model.vouchers;
        list.series = vouchers.series;
        list.first = vouchers.first_row;
        list.count = vouchers.expiry_count;
        const size_t visible = std::min<size_t>(list.expiries.size(), list.count - list.first);
        std::copy_n(vouchers.expiries.begin() + list.first, visible, list.expiries.begin());
    }
    return list;
}

// data: Historical telemetry scoped to the retained pairing and current connection.
// screen: Dashboard page whose values can be read independently from the live link.
int64_t device_saved_page_epoch(const DeviceDashboardData &data, WidgetScreen screen)
{
    switch (screen) {
    case WidgetScreen::Reset:
        return data.saved.weekly_observed_utc;
    case WidgetScreen::Limits:
        return std::max(data.saved.weekly_observed_utc,
            data.saved.account.short_window_minutes != 0 ? data.saved.account_observed_utc : int64_t{0});
    case WidgetScreen::Today:
    case WidgetScreen::LastSevenDays:
        return data.saved.account.daily_valid_mask != 0 ? data.saved.account_observed_utc : 0;
    case WidgetScreen::Credits:
        return data.saved.account.credits_kind != LUNASHELL::CodexCreditKind::Unavailable ||
            data.saved.account.reset_vouchers_valid ? data.saved.account_observed_utc : 0;
    default:
        return 0;
    }
}

// model: Battery threshold state and last banner awaiting or confirmed by the display.
// now_us: Monotonic time for the five-second visible dwell.
void update_device_battery(DeviceModel &model, int64_t now_us)
{
    const auto old_error = model.battery.source_error;
    BatteryAlertNotice notice{};
    const auto error = update_battery_alert(model.battery, model.dashboard.connection.battery, notice);
    if (error != ESP_OK && error != old_error) {
        ESP_LOGW("CODY_device_model", "Battery notice rejected (%s)", esp_err_to_name(error));
    }
    if (notice.sequence != 0 && notice.sequence != model.battery_banner.sequence) {
        model.battery_banner = notice;
        model.battery_banner_until_us = now_us + 5000000;
    }
    if (!model.battery.source_current || model.battery.external_power || now_us >= model.battery_banner_until_us) {
        model.battery_banner = {};
    }
}

// data: Authenticated app presence, current allowance and clock for the shared perimeter.
uint16_t device_allowance_ring(const DeviceDashboardData &data)
{
    if (!data.connection_valid || !data.connection.codex_app_open) {
        return 0;
    }
    const auto &weekly = data.weekly;
    if (weekly.health != LUNASHELL::DeviceHealth::Ready || !weekly.data_valid ||
        weekly.data_stale || weekly.last_error != ESP_OK || weekly.reset_unix_seconds <= 0 ||
        weekly.remaining_basis_points > 10000) {
        return 0;
    }
    const auto &clock = data.clock;
    if (clock.health == LUNASHELL::DeviceHealth::Ready && clock.data_valid &&
        !clock.data_stale && clock.last_error == ESP_OK && clock.utc_epoch_seconds >= weekly.reset_unix_seconds) {
        return 0;
    }
    return weekly.remaining_basis_points;
}

// data: Persistent canonical projections for the dashboard and current turn.
void update_device_statistics(DeviceDashboardData &data)
{
    const auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    if (snapshots.readCodexAccount(data.details) != ESP_OK) data.details = {};
    if (snapshots.readCodexTechnicalObservation(data.context) != ESP_OK) data.context = {};
    if (snapshots.readCodexTokenIntervals(data.intervals) != ESP_OK) data.intervals = {};
}

// data: Aged canonical token intervals.
// interval: Window requiring at least one measured bucket.
bool device_history_has_data(const DeviceDashboardData &data, TokenHistoryInterval interval)
{
    const auto &samples = data.intervals;
    int64_t first_epoch = 0;
    int64_t last_epoch = 0;
    const bool current = samples.data_valid && samples.last_error == ESP_OK &&
        samples.health == LUNASHELL::DeviceHealth::Ready;
    if ((!current && !samples.data_stale) ||
        !token_history_utc_range(interval, samples, first_epoch, last_epoch)) return false;
    const auto first = static_cast<uint32_t>(first_epoch / 900);
    const auto count = static_cast<uint32_t>((last_epoch - first_epoch) / 900) + 1;
    for (uint32_t index = 0; index < count; ++index) {
        uint64_t value = 0;
        if (samples.read_token_delta(first + index, value)) return true;
    }
    return false;
}

// data: Current and separately saved observations.
// preferences: Stored user choices, copied before filtering.
// now_us: Monotonic time for chat and context coherence.
WidgetVisibility available_device_statistics(const DeviceDashboardData &data, WidgetVisibility preferences, int64_t now_us)
{
    const auto current = [](const LUNASHELL::ComponentSnapshot &sample) {
        return sample.health == LUNASHELL::DeviceHealth::Ready && sample.last_error == ESP_OK &&
            sample.data_valid && !sample.data_stale;
    };
    const int64_t now = current(data.clock) ? data.clock.utc_epoch_seconds : 0;
    const bool live_account = current(data.details);
    const auto has_data = [&](WidgetScreen screen) {
        const int64_t saved_at = device_saved_page_epoch(data, screen);
        const auto &account = saved_at > 0 ? data.saved.account : data.details;
        const auto &weekly = saved_at > 0 ? data.saved.weekly : data.weekly;
        const bool account_available = saved_at > 0 ? data.saved.account_observed_utc > 0 : live_account;
        const bool weekly_available = (saved_at > 0 ? data.saved.weekly_observed_utc > 0 : current(weekly)) &&
            weekly.reset_unix_seconds > 0 && weekly.remaining_basis_points <= 10000;
        const int64_t day = (saved_at > 0 ? saved_at : now) / 86400;
        switch (screen) {
        case WidgetScreen::Device:
        case WidgetScreen::About:
            return true;
        case WidgetScreen::Reset:
            return weekly_available && (now == 0 || weekly.reset_unix_seconds > now);
        case WidgetScreen::Context:
            return context_dashboard_has_data(data.context, now_us);
        case WidgetScreen::Limits:
            return (weekly_available && weekly.reset_unix_seconds > now) ||
                (account_available && account.short_window_minutes != 0 &&
                 account.short_remaining_basis_points <= 10000 && account.short_reset_unix_seconds > now);
        case WidgetScreen::Today: {
            int64_t observed_day = 0;
            return account_available && day > 0 &&
                (account.tokens_for_unix_day(day) || account.latest_tokens_before(day, observed_day));
        }
        case WidgetScreen::LastSevenDays:
            for (int64_t previous = day - 7; account_available && previous < day; ++previous) {
                if (account.tokens_for_unix_day(previous)) return true;
            }
            return false;
        case WidgetScreen::TokenHistory:
            return device_history_has_data(data, TokenHistoryInterval::TwentyFourHours);
        case WidgetScreen::Credits:
            return (account_available &&
                (account.credits_kind != LUNASHELL::CodexCreditKind::Unavailable || account.reset_vouchers_valid)) ||
                device_voucher_expiries_current(data);
        case WidgetScreen::Account:
            return live_account && (account.plan[0] != '\0' || account.plan_label[0] != '\0' ||
                account.workspace_messages_valid);
        }
        return false;
    };
    preferences.screen_mask |= WidgetStateDetail::bit(WidgetScreen::Device);
    for (const auto &entry : kWidgetScreens) {
        if (widget_visible(preferences, entry.id) && !has_data(entry.id)) {
            preferences.screen_mask &= ~WidgetStateDetail::bit(entry.id);
        }
    }
    return preferences;
}

// data: Canonical observations and cached zone projections.
// screen: Selected visible dashboard page.
// interval: Selected token history time window.
void project_device_statistics_calendar(DeviceDashboardData &data, WidgetScreen screen, TokenHistoryInterval interval)
{
    if (screen == WidgetScreen::TokenHistory) {
        int64_t first = 0;
        int64_t last = 0;
        if (token_history_utc_range(interval, data.intervals, first, last)) {
            project_local(data.clock, first, data.history_local.first);
            project_local(data.clock, last, data.history_local.last);
        } else {
            data.history_local = {};
        }
    } else if (screen == WidgetScreen::Reset) {
        const auto &weekly = data.saved.weekly_observed_utc > 0 ? data.saved.weekly : data.weekly;
        project_local(data.clock, weekly.reset_unix_seconds, data.reset_local);
    }
}

// model: Retained child page and the diagnostics projection also used by the diagnostic overlay.
// Exception: Returns the original read error and clears rejected data instead of retaining it as current.
esp_err_t read_device_info(DeviceModel &model)
{
    if (model.info_page > InfoPage::ProjectLink) { return ESP_ERR_INVALID_ARG; }
    if (model.info_page != InfoPage::System && model.info_page != InfoPage::Memory) { return ESP_OK; }
    model.diagnostics_error = LUNASHELL::DeviceSnapshotService::instance().readDiagnosticsObservation(model.diagnostics);
    if (model.diagnostics_error != ESP_OK) { model.diagnostics = {}; }
    return model.diagnostics_error;
}

// data: System identity and the last sampled previous boot, when RTC was retained.
void initialize_device_uptime(DeviceDashboardData &data)
{
    data.previous_uptime_valid = esp_reset_reason() != ESP_RST_POWERON &&
        s_uptime.magic == kUptimeMagic && s_uptime.inverse == ~s_uptime.seconds;
    data.previous_uptime_seconds = data.previous_uptime_valid ? s_uptime.seconds : 0;
    data.build_version = esp_app_get_description()->version;
    data.build = &LUNASHELL::firmware_build_metadata();
    s_uptime = {kUptimeMagic, 0, ~uint32_t{0}};
}

// data: Current measured uptime, projected for About.
// now_us: Monotonic time since this firmware boot.
void update_device_uptime(DeviceDashboardData &data, int64_t now_us)
{
    const auto seconds = static_cast<uint32_t>(now_us / 1000000);
    if (seconds != data.uptime_seconds) {
        data.uptime_seconds = seconds;
        s_uptime.seconds = seconds;
        s_uptime.inverse = ~seconds;
    }
}

// model: Receives the canonical projection and owns gesture and arrow timeout state.
// now_us: Monotonic time shared by touch, gesture recognition and the frame owner.
DeviceNavigationUpdate update_device_navigation(DeviceModel &model, int64_t now_us)
{
    DeviceNavigationUpdate update{};
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU || CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    const auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    update.motion_error = snapshots.readMotionPowerObservation(model.motion);
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    MotionSample sample{};
    if (update.motion_error == ESP_OK) {
        const auto &imu = model.motion.imu;
        if (imu.health == LUNASHELL::DeviceHealth::Ready && imu.data_valid && !imu.data_stale &&
            imu.last_error == ESP_OK && imu.gesture_acceleration_valid &&
            imu.gesture_sampled_at_us == imu.last_success_at_us) {
            sample.stamp = {SampleHealth::Current, InputOrigin::Hardware, imu.gesture_sampled_at_us, ESP_OK};
            sample.acceleration_mps2[Axis::X] = imu.gesture_acceleration_mps2[0];
            sample.acceleration_mps2[Axis::Y] = imu.gesture_acceleration_mps2[1];
            sample.acceleration_mps2[Axis::Z] = imu.gesture_acceleration_mps2[2];
        }
    }
    // Power owns the gesture when shake-to-standby is selected, preventing a
    // single physical shake from also navigating Home.
    update.shaken = observe_companion_shake(model.shake, sample, now_us,
        !LUNASHELL_power_manager_shake_wake_enabled());
    if (update.shaken) model.navigation.interact(now_us);
#endif
    model.navigation.update(now_us, kNavigationTimeoutUs);
    return update;
}

} // namespace CODY
