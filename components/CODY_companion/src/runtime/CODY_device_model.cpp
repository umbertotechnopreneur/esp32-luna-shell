#include "CODY_device_model.hpp"

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

// data: Canonical weekly allowance and device observations shared by all awake screens.
void update_device_observations(DeviceDashboardData &data)
{
    const auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    if (snapshots.readCodexWeeklyLimit(data.weekly) != ESP_OK) data.weekly = {};
    data.connection_valid = snapshots.readDeviceStatusObservation(data.connection) == ESP_OK;
    if (!data.connection_valid) data.connection = {};
    data.clock = data.connection.clock;
    data.voice_ready = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    data.voice_ready = data.connection_valid && record_voice_available(snapshots);
#endif
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

// data: Current observed allowance and clock. An elapsed sample retires the ring.
uint16_t device_allowance_ring(const DeviceDashboardData &data)
{
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

// data: Receives the canonical projection needed by this page.
// screen: Visible top-level dashboard page.
// interval: Token history range selected on the device.
// Exception: Returns the first original observation error for the selected page.
esp_err_t read_device_dashboard(DeviceDashboardData &data, WidgetScreen screen, TokenHistoryInterval interval)
{
    if (screen > WidgetScreen::About) {
        return ESP_ERR_INVALID_ARG;
    }
    const auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    if (screen == WidgetScreen::About) return ESP_OK;
    if (screen == WidgetScreen::Context) {
        return snapshots.readCodexTechnicalObservation(data.context);
    }
    if (screen == WidgetScreen::Device) {
        if (!data.connection.codex_app_open ||
            snapshots.readCodexTechnicalObservation(data.context) != ESP_OK) {
            data.context = {};
        }
        return ESP_OK;
    }

    esp_err_t first_error = ESP_OK;
    const auto observe = [&first_error](esp_err_t error, auto &observation) {
        if (error != ESP_OK) {
            observation = {};
            if (first_error == ESP_OK) {
                first_error = error;
            }
        }
    };
    if (screen == WidgetScreen::TokenHistory) {
        observe(snapshots.readCodexTokenIntervals(data.intervals), data.intervals);
        int64_t first = 0;
        int64_t last = 0;
        if (token_history_utc_range(interval, data.intervals, first, last)) {
            project_local(data.clock, first, data.history_local.first);
            project_local(data.clock, last, data.history_local.last);
        } else {
            data.history_local = {};
        }
        return first_error;
    }
    if (screen == WidgetScreen::Reset) {
        project_local(data.clock, data.weekly.reset_unix_seconds, data.reset_local);
    } else {
        observe(snapshots.readCodexAccount(data.details), data.details);
    }
    return first_error;
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

// model: Gesture recognition and arrow timeout state, independent of the current view.
// now_us: Monotonic time shared by touch, gesture recognition and the frame owner.
bool update_device_navigation(DeviceModel &model, int64_t now_us)
{
    bool shaken = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    const auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    MotionSample sample{};
    if (snapshots.readMotionPowerObservation(model.motion) == ESP_OK) {
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
    shaken = observe_companion_shake(model.shake, sample, now_us, true);
    if (shaken) model.navigation.interact(now_us);
#endif
    model.navigation.update(now_us, kNavigationTimeoutUs);
    return shaken;
}

} // namespace CODY
