/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_snapshot_internal.hpp"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <limits>
#include <initializer_list>
#include <sys/time.h>

#include "esp_log.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#endif
#include "esp_timer.h"

namespace {

const char *TAG = "LUNASHELL_time";
std::atomic<bool> s_initialized{false};
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
bool s_sntp_started;
#endif
esp_err_t s_initialization_result = ESP_ERR_INVALID_STATE;
char s_timezone[40] = "UTC0";
std::mutex s_sync_mutex;
bool s_trusted_gps_fix_observed;
bool s_reported_system_time_valid;
bool s_reported_system_time_synchronized;
char s_reported_system_time_source[8] = "none";
time_t s_last_rtc_persisted_epoch;
int64_t s_rtc_persistence_retry_after_us;
uint32_t s_rtc_persistence_retry_delay_seconds;
esp_err_t s_rtc_persistence_error = ESP_OK;
LUNASHELL::ClockPreferences s_clock_preferences;
bool s_clock_preferences_ready;
esp_err_t s_clock_preferences_error = ESP_ERR_INVALID_STATE;
constexpr char kClockPreferencesKey[] = "clock_routine";
constexpr char kClockCompanionInitializedKey[] = "clock_adopted";
struct DriftBaseline {
    bool valid = false;
    int32_t offset_seconds = 0;
    int64_t reference_seconds = 0;
};
DriftBaseline s_drift_baseline;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
constexpr uint32_t kMinimumDriftIntervalSeconds = 600;
constexpr uint32_t kOffsetUncertaintySeconds = 2;
#endif

constexpr int64_t kGpsSystemResyncDriftUs = 2LL * 1000LL * 1000LL;
constexpr time_t kGpsRtcPersistenceIntervalSeconds = 6 * 60 * 60;
constexpr uint32_t kRtcPersistenceRetryInitialSeconds = 30;
constexpr uint32_t kRtcPersistenceRetryMaximumSeconds = 30 * 60;

// epoch: UTC timestamp that must fit the external RTC's supported calendar.
bool epoch_valid(time_t epoch)
{
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    return utc.tm_year >= (2024 - 1900) && utc.tm_year <= (2099 - 1900);
}

// preferences: Explicit configuration, including a nonempty half-open quiet window.
esp_err_t validate_clock_preferences(const LUNASHELL::ClockPreferences &preferences)
{
    if (LUNASHELL::clock_timezone_spec(preferences.timezone) == nullptr ||
        preferences.quiet_start_minute >= 1440 || preferences.quiet_end_minute >= 1440 ||
        (preferences.quiet_hours_enabled &&
         preferences.quiet_start_minute == preferences.quiet_end_minute)) {
        ESP_LOGE(TAG, "Clock preferences rejected: zone=%u enabled=%u quiet=%u start=%u end=%u (%s)",
            static_cast<unsigned>(preferences.timezone), preferences.routines_enabled,
            preferences.quiet_hours_enabled, preferences.quiet_start_minute,
            preferences.quiet_end_minute, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// preferences: Validated values encoded as one atomic NVS setting, without padding.
uint32_t encode_clock_preferences(const LUNASHELL::ClockPreferences &preferences)
{
    return static_cast<uint32_t>(preferences.routines_enabled) |
        (static_cast<uint32_t>(preferences.quiet_hours_enabled) << 1U) |
        (static_cast<uint32_t>(preferences.timezone) << 2U) |
        (static_cast<uint32_t>(preferences.quiet_start_minute) << 10U) |
        (static_cast<uint32_t>(preferences.quiet_end_minute) << 21U);
}

// default_timezone: Reviewed initial zone used only when the whole NVS key is absent.
esp_err_t load_clock_preferences(const char *default_timezone)
{
    auto &settings = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = settings.isInitialized() ? ESP_OK : settings.initialize();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Clock settings initialization failed (%s)", esp_err_to_name(error));
        return error;
    }
    uint32_t encoded = 0;
    error = settings.getUInt32(kClockPreferencesKey, &encoded);
    LUNASHELL::ClockPreferences loaded;
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        bool found = false;
        for (const auto zone : {LUNASHELL::ClockTimezone::Utc, LUNASHELL::ClockTimezone::Rome,
                                LUNASHELL::ClockTimezone::HoChiMinh}) {
            if (std::strcmp(default_timezone, LUNASHELL::clock_timezone_spec(zone)) == 0) {
                loaded.timezone = zone;
                found = true;
                break;
            }
        }
        if (!found) {
            ESP_LOGE(TAG, "Unsupported default timezone '%s' (%s)", default_timezone,
                esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
            return ESP_ERR_NOT_SUPPORTED;
        }
        ESP_LOGI(TAG, "First boot: key '%s' absent; persist routines=1 quiet=0 22:00-07:00 zone=%s",
            kClockPreferencesKey, default_timezone);
        error = settings.setUInt32(kClockPreferencesKey, encode_clock_preferences(loaded));
    } else if (error == ESP_OK) {
        loaded.routines_enabled = (encoded & 1U) != 0;
        loaded.quiet_hours_enabled = (encoded & 2U) != 0;
        loaded.timezone = static_cast<LUNASHELL::ClockTimezone>((encoded >> 2U) & 0xFFU);
        loaded.quiet_start_minute = (encoded >> 10U) & 0x7FFU;
        loaded.quiet_end_minute = (encoded >> 21U) & 0x7FFU;
        error = validate_clock_preferences(loaded);
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Clock settings read/default '%s' failed; no repair (%s)",
            kClockPreferencesKey, esp_err_to_name(error));
        return error;
    }
    s_clock_preferences = loaded;
    s_clock_preferences_ready = true;
    s_clock_preferences_error = ESP_OK;
    return ESP_OK;
}

// minute: Valid local minute observed by the time owner, never inferred by a consumer.
LUNASHELL::RoutinePeriod routine_period(uint16_t minute)
{
    if (minute >= 6 * 60 && minute < 12 * 60) {
        return LUNASHELL::RoutinePeriod::Morning;
    }
    if (minute >= 12 * 60 && minute < 18 * 60) {
        return LUNASHELL::RoutinePeriod::Afternoon;
    }
    if (minute >= 18 * 60 && minute < 22 * 60) {
        return LUNASHELL::RoutinePeriod::Evening;
    }
    return LUNASHELL::RoutinePeriod::Night;
}

// No parameters: called under the time-owner mutex; system time is a real clock read.
esp_err_t publish_clock_observation()
{
    using namespace LUNASHELL;
    ClockSnapshot clock;
    const int64_t attempted_at_us = esp_timer_get_time();
    clock.preferences_valid = s_clock_preferences_ready;
    if (s_clock_preferences_ready) {
        clock.timezone = s_clock_preferences.timezone;
        clock.routines_enabled = s_clock_preferences.routines_enabled;
        clock.quiet_hours_enabled = s_clock_preferences.quiet_hours_enabled;
        clock.quiet_start_minute = s_clock_preferences.quiet_start_minute;
        clock.quiet_end_minute = s_clock_preferences.quiet_end_minute;
    }
    struct timeval current = {};
    struct tm local = {};
    esp_err_t error = s_clock_preferences_ready ? ESP_ERR_INVALID_STATE : s_clock_preferences_error;
    if (s_clock_preferences_ready && s_reported_system_time_valid) {
        if (gettimeofday(&current, nullptr) != 0) {
            error = ESP_FAIL;
        } else if (!epoch_valid(current.tv_sec)) {
            error = ESP_ERR_INVALID_RESPONSE;
        } else if (localtime_r(&current.tv_sec, &local) == nullptr) {
            error = ESP_FAIL;
        } else {
            error = ESP_OK;
        }
    }
    if (error == ESP_OK) {
        clock.utc_epoch_seconds = current.tv_sec;
        clock.local_minute = local.tm_hour * 60 + local.tm_min;
        clock.local_year = static_cast<uint16_t>(local.tm_year + 1900);
        clock.local_month = static_cast<uint8_t>(local.tm_mon + 1);
        clock.local_day = static_cast<uint8_t>(local.tm_mday);
        clock.local_weekday = static_cast<uint8_t>(local.tm_wday);
        clock.local_second = static_cast<uint8_t>(local.tm_sec);
        strlcpy(clock.source, s_reported_system_time_source, sizeof(clock.source));
        if (clock.routines_enabled) {
            clock.period = routine_period(clock.local_minute);
            const uint16_t start = clock.quiet_start_minute;
            const uint16_t end = clock.quiet_end_minute;
            const bool in_window = start < end
                ? clock.local_minute >= start && clock.local_minute < end
                : clock.local_minute >= start || clock.local_minute < end;
            clock.quiet = clock.quiet_hours_enabled && in_window;
        }
        snapshot_detail::mark_sample_success(clock, attempted_at_us, esp_timer_get_time());
    } else {
        // No plausible hour, period or cue permission escapes an unavailable clock.
        snapshot_detail::mark_sample_failure(clock, error, attempted_at_us);
    }
    const esp_err_t publication_error = DeviceSnapshotService::instance().reportClock(clock);
    if (publication_error != ESP_OK) {
        return publication_error;
    }
    // Publish unavailability and propagate it. The startup orchestrator, not
    // this component, decides whether to continue until trusted NTP arrives.
    if (error != ESP_OK) {
        if (error != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "System clock observation failed (%s)", esp_err_to_name(error));
        }
        return error;
    }
    return ESP_OK;
}

bool calendar_valid(const LUNASHELL_calendar_time_t &calendar)
{
    if (calendar.year < 2024 || calendar.year > 2099 ||
        calendar.month < 1 || calendar.month > 12 ||
        calendar.hour > 23 || calendar.minute > 59 ||
        calendar.second > 59) {
        return false;
    }
    static constexpr uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    uint8_t maximum_day = days_per_month[calendar.month - 1U];
    const bool leap =
        (calendar.year % 4U == 0U && calendar.year % 100U != 0U) ||
        calendar.year % 400U == 0U;
    if (calendar.month == 2U && leap) {
        maximum_day = 29;
    }
    return calendar.day >= 1U && calendar.day <= maximum_day;
}

// Convert a UTC civil date without relying on the host timezone or timegm().
time_t utc_epoch(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
)
{
    int adjusted_year = year;
    const int adjusted_month = month;
    adjusted_year -= adjusted_month <= 2;
    const int era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
    const unsigned year_of_era =
        static_cast<unsigned>(adjusted_year - era * 400);
    const unsigned day_of_year = static_cast<unsigned>(
        (153 * (adjusted_month + (adjusted_month > 2 ? -3 : 9)) + 2) / 5 +
        day - 1
    );
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 +
        day_of_year;
    const int64_t days =
        static_cast<int64_t>(era) * 146097 + day_of_era - 719468;
    return static_cast<time_t>(
        days * 86400 + hour * 3600 + minute * 60 + second
    );
}

esp_err_t timezone_errno_to_esp(int error_number)
{
    if (error_number == ENOMEM) {
        return ESP_ERR_NO_MEM;
    }
    if (error_number == EINVAL) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_FAIL;
}

esp_err_t apply_timezone(const char *timezone)
{
    errno = 0;
    if (setenv("TZ", timezone, 1) != 0) {
        const int error_number = errno;
        const esp_err_t error = timezone_errno_to_esp(error_number);
        ESP_LOGE(
            TAG,
            "Time service setenv(TZ=%s) failed: errno=%d (%s)",
            timezone,
            error_number,
            esp_err_to_name(error)
        );
        return error;
    }

    tzset();
    const char *applied_timezone = getenv("TZ");
    if (applied_timezone == nullptr ||
        std::strcmp(applied_timezone, timezone) != 0) {
        ESP_LOGE(
            TAG,
            "Time service tzset verification failed: requested=%s observed=%s",
            timezone,
            applied_timezone != nullptr ? applied_timezone : "<unset>"
        );
        return ESP_ERR_INVALID_STATE;
    }

    // tzset() has no return value, so verify the active conversion state.
    const time_t probe_epoch = utc_epoch(2024, 1, 1, 0, 0, 0);
    struct tm probe_local = {};
    errno = 0;
    if (localtime_r(&probe_epoch, &probe_local) == nullptr) {
        const int error_number = errno;
        const esp_err_t error = timezone_errno_to_esp(error_number);
        ESP_LOGE(
            TAG,
            "Time service tzset localtime verification failed: timezone=%s errno=%d (%s)",
            timezone,
            error_number,
            esp_err_to_name(error)
        );
        return error;
    }
    if (std::strcmp(timezone, "UTC0") == 0) {
        struct tm probe_utc = {};
        if (gmtime_r(&probe_epoch, &probe_utc) == nullptr ||
            probe_local.tm_year != probe_utc.tm_year ||
            probe_local.tm_mon != probe_utc.tm_mon ||
            probe_local.tm_mday != probe_utc.tm_mday ||
            probe_local.tm_hour != probe_utc.tm_hour ||
            probe_local.tm_min != probe_utc.tm_min ||
            probe_local.tm_sec != probe_utc.tm_sec ||
            probe_local.tm_isdst != 0) {
            ESP_LOGE(
                TAG,
                "Time service tzset UTC verification failed"
            );
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    struct tm round_trip_local = probe_local;
    errno = 0;
    const time_t round_trip_epoch = mktime(&round_trip_local);
    if (round_trip_epoch != probe_epoch) {
        const int error_number = errno;
        ESP_LOGE(
            TAG,
            "Time service tzset round-trip verification failed: timezone=%s errno=%d",
            timezone,
            error_number
        );
        return error_number != 0
                   ? timezone_errno_to_esp(error_number)
                   : ESP_ERR_INVALID_RESPONSE;
    }

    strlcpy(s_timezone, timezone, sizeof(s_timezone));
    return ESP_OK;
}

void report_time(
    bool valid,
    bool synchronized,
    const char *source,
    esp_err_t error
)
{
    s_reported_system_time_valid = valid;
    s_reported_system_time_synchronized = synchronized;
    if (source != nullptr && source[0] != '\0' &&
        std::strlen(source) < sizeof(s_reported_system_time_source)) {
        strlcpy(
            s_reported_system_time_source,
            source,
            sizeof(s_reported_system_time_source)
        );
    }
    LUNASHELL::DeviceSnapshotService::instance().reportTime(
        valid,
        synchronized,
        source,
        s_timezone,
        error
    );
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
void report_time_failure_preserving_valid_clock(esp_err_t error)
{
    struct timeval current_time = {};
    const bool clock_still_valid =
        s_reported_system_time_valid &&
        gettimeofday(&current_time, nullptr) == 0 &&
        epoch_valid(current_time.tv_sec);
    if (!clock_still_valid) {
        report_time(false, false, "none", error);
        return;
    }
    LUNASHELL::DeviceSnapshotService::instance().reportTime(
        true,
        s_reported_system_time_synchronized,
        s_reported_system_time_source,
        s_timezone,
        error
    );
}
#endif

void record_rtc_persistence_success(time_t persisted_epoch)
{
    s_last_rtc_persisted_epoch = persisted_epoch;
    s_rtc_persistence_retry_after_us = 0;
    s_rtc_persistence_retry_delay_seconds = 0;
    s_rtc_persistence_error = ESP_OK;
}

uint32_t record_rtc_persistence_failure(
    esp_err_t error,
    int64_t failed_at_us
)
{
    uint32_t retry_delay_seconds = s_rtc_persistence_retry_delay_seconds;
    if (retry_delay_seconds == 0) {
        retry_delay_seconds = kRtcPersistenceRetryInitialSeconds;
    } else if (
        retry_delay_seconds <= kRtcPersistenceRetryMaximumSeconds / 2U
    ) {
        retry_delay_seconds *= 2U;
    } else {
        retry_delay_seconds = kRtcPersistenceRetryMaximumSeconds;
    }
    s_rtc_persistence_retry_delay_seconds = retry_delay_seconds;
    s_rtc_persistence_retry_after_us =
        failed_at_us + static_cast<int64_t>(retry_delay_seconds) * 1000000LL;
    s_rtc_persistence_error = error;
    return retry_delay_seconds;
}

// epoch: Trusted UTC to persist; any write invalidates an earlier drift baseline.
esp_err_t persist_epoch_to_rtc(time_t epoch)
{
    s_drift_baseline.valid = false;
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    const LUNASHELL_calendar_time_t calendar = {
        .clock_integrity = true,
        .year = static_cast<uint16_t>(utc.tm_year + 1900),
        .month = static_cast<uint8_t>(utc.tm_mon + 1),
        .day = static_cast<uint8_t>(utc.tm_mday),
        .hour = static_cast<uint8_t>(utc.tm_hour),
        .minute = static_cast<uint8_t>(utc.tm_min),
        .second = static_cast<uint8_t>(utc.tm_sec),
    };
    return LUNASHELL_rtc_write(&calendar);
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
// measurement: Receives the RTC/system comparison from this owner's serialized read.
// Called only after a trusted SNTP callback, never from a UI/animation consumer.
esp_err_t measure_rtc_offset(LUNASHELL::RtcDriftSnapshot &measurement)
{
    using namespace LUNASHELL;
    const int64_t attempted_at_us = esp_timer_get_time();
    LUNASHELL_calendar_time_t rtc = {};
    int64_t rtc_sampled_at_us = 0;
    esp_err_t error = LUNASHELL_rtc_read(&rtc, &rtc_sampled_at_us);
    const int64_t rtc_completed_at_us = esp_timer_get_time();
    struct timeval reference = {};
    if (error == ESP_OK && gettimeofday(&reference, nullptr) != 0) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK && (!rtc.clock_integrity || !calendar_valid(rtc) ||
        !epoch_valid(reference.tv_sec))) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    // Reject a slow transaction rather than advertising a two-second bound
    // for an arbitrarily old RTC/system comparison.
    if (error == ESP_OK && esp_timer_get_time() - attempted_at_us > 1000000) {
        error = ESP_ERR_TIMEOUT;
    }
    int64_t offset_seconds = 0;
    if (error == ESP_OK) {
        offset_seconds = static_cast<int64_t>(utc_epoch(rtc.year, rtc.month, rtc.day,
            rtc.hour, rtc.minute, rtc.second)) - reference.tv_sec;
        if (offset_seconds < std::numeric_limits<int32_t>::min() ||
            offset_seconds > std::numeric_limits<int32_t>::max()) {
            error = ESP_ERR_INVALID_SIZE;
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "RTC drift measurement unavailable (%s)", esp_err_to_name(error));
        snapshot_detail::mark_sample_failure(measurement, error, attempted_at_us);
        return error;
    }
    snapshot_detail::mark_sample_success(measurement, attempted_at_us, rtc_completed_at_us);
    measurement.offset_valid = true;
    measurement.rtc_minus_reference_seconds = static_cast<int32_t>(offset_seconds);
    measurement.reference_utc_seconds = reference.tv_sec;
    measurement.rtc_sampled_at_us = rtc_sampled_at_us;
    measurement.offset_uncertainty_seconds = kOffsetUncertaintySeconds;
    strlcpy(measurement.source, "sntp", sizeof(measurement.source));
    return ESP_OK;
}

// No parameters: compare before correction; an unreadable RTC cannot invalidate SNTP UTC.
LUNASHELL::RtcDriftSnapshot measure_drift_before_correction()
{
    LUNASHELL::RtcDriftSnapshot measurement;
    const esp_err_t error = measure_rtc_offset(measurement);
    if (error == ESP_OK && s_drift_baseline.valid) {
        const int64_t interval = measurement.reference_utc_seconds - s_drift_baseline.reference_seconds;
        if (interval >= kMinimumDriftIntervalSeconds && interval <= UINT32_MAX) {
            const int64_t offset_change = static_cast<int64_t>(measurement.rtc_minus_reference_seconds) -
                s_drift_baseline.offset_seconds;
            measurement.interval_seconds = static_cast<uint32_t>(interval);
            measurement.rate_ppm = static_cast<float>(offset_change) * 1000000.f /
                measurement.interval_seconds;
            measurement.rate_uncertainty_ppm = 2.f * kOffsetUncertaintySeconds * 1000000.f /
                measurement.interval_seconds;
            measurement.rate_valid = true;
        } else {
            ESP_LOGI(TAG, "RTC drift rate unavailable: reference interval=%lld s (minimum=%lu)",
                static_cast<long long>(interval), static_cast<unsigned long>(kMinimumDriftIntervalSeconds));
        }
    }
    return measurement;
}

// measurement: Pre-correction result retained alongside the post-write baseline health.
// persistence_error: Original RTC write failure; do not manufacture a baseline on failure.
void establish_drift_baseline(LUNASHELL::RtcDriftSnapshot &measurement, esp_err_t persistence_error)
{
    LUNASHELL::RtcDriftSnapshot baseline;
    esp_err_t error = persistence_error;
    if (error == ESP_OK) {
        error = measure_rtc_offset(baseline);
    }
    if (error == ESP_OK) {
        s_drift_baseline = {true, baseline.rtc_minus_reference_seconds, baseline.reference_utc_seconds};
        measurement.baseline = baseline;
    } else {
        s_drift_baseline.valid = false;
        LUNASHELL::snapshot_detail::mark_sample_failure(measurement.baseline, error, esp_timer_get_time());
        ESP_LOGE(TAG, "RTC drift baseline unavailable (%s); rate requires a new trusted baseline",
            esp_err_to_name(error));
    }
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportRtcDrift(measurement);
    if (publication_error != ESP_OK) {
        ESP_LOGE(TAG, "RTC drift publication failed (%s)", esp_err_to_name(publication_error));
    }
    if (measurement.offset_valid) {
        ESP_LOGI(TAG, "RTC before SNTP correction: offset=%ld s +/- %lu s; rate=%s interval=%lu s",
            static_cast<long>(measurement.rtc_minus_reference_seconds),
            static_cast<unsigned long>(measurement.offset_uncertainty_seconds),
            measurement.rate_valid ? "measured" : "unavailable",
            static_cast<unsigned long>(measurement.interval_seconds));
        if (measurement.rate_valid) {
            ESP_LOGI(TAG, "RTC drift=%+.2f ppm +/- %.2f ppm (one-second RTC resolution)",
                static_cast<double>(measurement.rate_ppm), static_cast<double>(measurement.rate_uncertainty_ppm));
        }
    }
}

// time_value: SNTP's committed trusted UTC; reject malformed/out-of-calendar values.
void sntp_sync_callback(struct timeval *time_value)
{
    std::lock_guard<std::mutex> sync_lock(s_sync_mutex);
    if (time_value == nullptr || !epoch_valid(time_value->tv_sec) ||
        time_value->tv_usec < 0 || time_value->tv_usec >= 1000000) {
        ESP_LOGE(
            TAG,
            "SNTP callback rejected an invalid epoch (%s)",
            esp_err_to_name(ESP_ERR_INVALID_RESPONSE)
        );
        // A rejected network sample is not proof that a previously trusted
        // RTC/GPS/SNTP clock became invalid. Preserve that clock state while
        // publishing the real synchronization failure.
        report_time_failure_preserving_valid_clock(ESP_ERR_INVALID_RESPONSE);
        s_drift_baseline.valid = false;
        LUNASHELL::RtcDriftSnapshot failed_drift;
        LUNASHELL::snapshot_detail::mark_sample_failure(failed_drift, ESP_ERR_INVALID_RESPONSE, esp_timer_get_time());
        const esp_err_t drift_publication_error = LUNASHELL::DeviceSnapshotService::instance().reportRtcDrift(failed_drift);
        if (drift_publication_error != ESP_OK) {
            ESP_LOGE(TAG, "Rejected SNTP drift publication failed (%s)", esp_err_to_name(drift_publication_error));
        }
        const esp_err_t clock_error = publish_clock_observation();
        if (clock_error != ESP_OK) {
            ESP_LOGE(TAG, "Rejected SNTP clock publication failed (%s)", esp_err_to_name(clock_error));
        }
        return;
    }

    auto drift = measure_drift_before_correction();
    const esp_err_t rtc_result = persist_epoch_to_rtc(time_value->tv_sec);
    establish_drift_baseline(drift, rtc_result);
    struct tm utc = {};
    gmtime_r(&time_value->tv_sec, &utc);
    if (rtc_result == ESP_OK) {
        record_rtc_persistence_success(time_value->tv_sec);
        ESP_LOGI(
            TAG,
            "SNTP synchronized UTC=%04d-%02d-%02d %02d:%02d:%02d",
            utc.tm_year + 1900,
            utc.tm_mon + 1,
            utc.tm_mday,
            utc.tm_hour,
            utc.tm_min,
            utc.tm_sec
        );
    } else {
        const uint32_t retry_delay_seconds = record_rtc_persistence_failure(
            rtc_result,
            esp_timer_get_time()
        );
        ESP_LOGE(
            TAG,
            "SNTP set system UTC but RTC persistence failed (%s); retry in "
            "%lu s",
            esp_err_to_name(rtc_result),
            static_cast<unsigned long>(retry_delay_seconds)
        );
    }
    s_initialization_result = rtc_result;
    // The system clock was already committed by SNTP. RTC persistence is a
    // separate component result and must not invalidate that real clock.
    report_time(true, true, "sntp", ESP_OK);
    const esp_err_t clock_error = publish_clock_observation();
    if (clock_error != ESP_OK) {
        ESP_LOGE(TAG, "SNTP clock publication failed (%s)", esp_err_to_name(clock_error));
    }
}
#endif

esp_err_t validate_utc_calendar(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second,
    time_t *epoch
)
{
    if (epoch == nullptr) {
        ESP_LOGE(TAG, "GPS time rejected: epoch output is null");
        return ESP_ERR_INVALID_ARG;
    }
    const LUNASHELL_calendar_time_t calendar = {
        .clock_integrity = true,
        .year = year,
        .month = month,
        .day = day,
        .hour = hour,
        .minute = minute,
        .second = second,
    };
    if (!calendar_valid(calendar)) {
        ESP_LOGE(TAG, "GPS time rejected: invalid UTC calendar");
        return ESP_ERR_INVALID_ARG;
    }
    const time_t candidate = utc_epoch(year, month, day, hour, minute, second);
    if (candidate <= 0 || !epoch_valid(candidate)) {
        ESP_LOGE(TAG, "GPS time rejected: invalid UTC epoch");
        return ESP_ERR_INVALID_ARG;
    }

    *epoch = candidate;
    return ESP_OK;
}

esp_err_t apply_system_utc(time_t epoch, const char *source)
{
    const struct timeval system_time = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&system_time, nullptr) != 0) {
        report_time(false, false, "none", ESP_FAIL);
        return ESP_FAIL;
    }
    report_time(true, true, source, ESP_OK);
    return ESP_OK;
}

}  // namespace

// timezone: Reviewed first-boot default only; an existing setting always takes precedence.
extern "C" esp_err_t LUNASHELL_time_init(const char *timezone)
{
    std::lock_guard<std::mutex> sync_lock(s_sync_mutex);
    if (s_initialized) {
        return s_initialization_result;
    }

    if (timezone == nullptr || timezone[0] == '\0' ||
        std::strlen(timezone) >= sizeof(s_timezone)) {
        ESP_LOGE(TAG, "Time service rejected invalid timezone");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t timezone_result = load_clock_preferences(timezone);
    if (timezone_result == ESP_OK) {
        timezone_result = apply_timezone(LUNASHELL::clock_timezone_spec(s_clock_preferences.timezone));
    }
    if (timezone_result != ESP_OK) {
        s_clock_preferences_ready = false;
        s_clock_preferences_error = timezone_result;
        LUNASHELL::DeviceSnapshotService::instance().reportTime(
            false,
            false,
            "none",
            timezone,
            timezone_result
        );
        LUNASHELL::ClockSnapshot failed;
        LUNASHELL::snapshot_detail::mark_sample_failure(failed, timezone_result, esp_timer_get_time());
        const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportClock(failed);
        if (publication_error != ESP_OK) {
            ESP_LOGE(TAG, "Clock configuration failure publication failed (%s)", esp_err_to_name(publication_error));
        }
        return timezone_result;
    }

    bool restored = false;
    esp_err_t restore_result = ESP_OK;
    LUNASHELL_calendar_time_t rtc = {};
    int64_t rtc_sampled_at_us = 0;
    const esp_err_t rtc_result = LUNASHELL_rtc_read(&rtc, &rtc_sampled_at_us);
    if (rtc_result == ESP_OK) {
        const time_t epoch = utc_epoch(
            rtc.year,
            rtc.month,
            rtc.day,
            rtc.hour,
            rtc.minute,
            rtc.second
        );
        const struct timeval system_time = {
            .tv_sec = epoch,
            .tv_usec = 0,
        };
        restored = epoch_valid(epoch) && settimeofday(&system_time, nullptr) == 0;
        if (!restored) {
            restore_result = ESP_FAIL;
        }
    } else {
        restore_result = rtc_result;
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
    const esp_err_t netif_result = esp_netif_init();
    if (netif_result != ESP_OK && netif_result != ESP_ERR_INVALID_STATE) {
        report_time(
            restored,
            restored,
            restored ? "rtc" : "none",
            netif_result
        );
        return netif_result;
    }

    // The target SDK is configured with one SNTP server slot.
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.start = false;
    config.sync_cb = sntp_sync_callback;
    const esp_err_t sntp_result = esp_netif_sntp_init(&config);
    if (sntp_result != ESP_OK) {
        ESP_LOGE(TAG, "SNTP initialization failed: %s", esp_err_to_name(sntp_result));
        report_time(
            restored,
            restored,
            restored ? "rtc" : "none",
            sntp_result
        );
        return sntp_result;
    }
#endif

    s_initialized = true;
    s_initialization_result = restore_result;
    report_time(
        restored,
        restored,
        restored ? "rtc" : "none",
        restore_result
    );
    const esp_err_t clock_error = publish_clock_observation();
    if (clock_error != ESP_OK) {
        ESP_LOGE(TAG, "Initial clock observation failed (%s)", esp_err_to_name(clock_error));
        return restore_result != ESP_OK ? restore_result : clock_error;
    }
    ESP_LOGI(
        TAG,
        "Time service ready: timezone=%s RTC=%s restored=%s",
        s_timezone,
        esp_err_to_name(rtc_result),
        restored ? "yes" : "no"
    );
    return restore_result;
}

// No parameters: the first DHCP success requests NTP once; callback confirms real success.
extern "C" esp_err_t LUNASHELL_time_notify_wifi_online(void)
{
    std::lock_guard<std::mutex> sync_lock(s_sync_mutex);
#if !CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
    ESP_LOGE(
        TAG,
        "SNTP start rejected: Wi-Fi networking is disabled by build profile (%s)",
        esp_err_to_name(ESP_ERR_NOT_SUPPORTED)
    );
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_initialized) {
        ESP_LOGE(TAG, "NTP start before time initialization (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        report_time_failure_preserving_valid_clock(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sntp_started) {
        return ESP_OK;
    }
    const esp_err_t result = esp_netif_sntp_start();
    if (result == ESP_OK) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "First network connection: NTP requested after DHCP; Internet/time not yet verified");
    } else {
        ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(result));
        report_time_failure_preserving_valid_clock(result);
    }
    return result;
#endif
}

// No parameters: safe acquisition-owner check; no sensor or system-clock access.
extern "C" bool LUNASHELL_time_is_initialized(void)
{
    return s_initialized.load(std::memory_order_acquire);
}

// No parameters: refresh on the existing Full Snapshot cadence, not a new timer/task.
extern "C" esp_err_t LUNASHELL_time_refresh_snapshot(void)
{
    std::lock_guard<std::mutex> sync_lock(s_sync_mutex);
    if (!s_initialized) {
        ESP_LOGE(TAG, "Clock refresh before time initialization (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    return publish_clock_observation();
}

namespace LUNASHELL {

// preferences: Receives only the validated active configuration.
esp_err_t read_clock_preferences(ClockPreferences &preferences)
{
    std::lock_guard<std::mutex> sync_lock(s_sync_mutex);
    if (!s_initialized || !s_clock_preferences_ready) {
        ESP_LOGE(TAG, "Clock preferences read before initialization (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    preferences = s_clock_preferences;
    return ESP_OK;
}

// preferences: Explicit runtime change; one persisted key prevents partial quiet windows.
// Exception: Returns original validation, durable readback, timezone or publication failures.
esp_err_t configure_clock_preferences(const ClockPreferences &preferences)
{
    const esp_err_t validation_error = validate_clock_preferences(preferences);
    if (validation_error != ESP_OK) {
        return validation_error;
    }
    std::lock_guard<std::mutex> sync_lock(s_sync_mutex);
    if (!s_initialized || !s_clock_preferences_ready) {
        ESP_LOGE(TAG, "Clock preferences update before initialization (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const ClockPreferences previous = s_clock_preferences;
    esp_err_t error = apply_timezone(clock_timezone_spec(preferences.timezone));
    if (error == ESP_OK) {
        error = UserSettingsService::instance().setUInt32(kClockPreferencesKey, encode_clock_preferences(preferences));
    }
    if (error == ESP_OK) {
        uint32_t confirmed = 0;
        error = UserSettingsService::instance().getUInt32(kClockPreferencesKey, &confirmed);
        if (error == ESP_OK && confirmed != encode_clock_preferences(preferences)) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Clock preferences update failed (%s); retaining original configuration", esp_err_to_name(error));
        const esp_err_t rollback_error = apply_timezone(clock_timezone_spec(previous.timezone));
        if (rollback_error != ESP_OK) {
            s_clock_preferences_ready = false;
            s_clock_preferences_error = rollback_error;
            ESP_LOGE(TAG, "Clock timezone rollback failed (%s); routine unavailable", esp_err_to_name(rollback_error));
        }
        ClockSnapshot failed;
        snapshot_detail::mark_sample_failure(failed, error, esp_timer_get_time());
        const esp_err_t publication_error = DeviceSnapshotService::instance().reportClock(failed);
        if (publication_error != ESP_OK) {
            ESP_LOGE(TAG, "Clock preferences failure publication failed (%s)", esp_err_to_name(publication_error));
        }
        return error;
    }
    s_clock_preferences = preferences;
    report_time(s_reported_system_time_valid, s_reported_system_time_synchronized,
        s_reported_system_time_source, ESP_OK);
    ESP_LOGI(TAG, "Clock preferences persisted: zone=%s routines=%u quiet=%u start=%u end=%u",
        s_timezone, preferences.routines_enabled, preferences.quiet_hours_enabled,
        preferences.quiet_start_minute, preferences.quiet_end_minute);
    return publish_clock_observation();
}

// preferences: Receives only validated settings belonging to the time owner.
// initialized: Receives the strict durable companion-initialization marker.
// Exception: Returns the original read error; absence is uninitialized, never an invalid value repair.
esp_err_t read_remote_clock_preferences(ClockPreferences &preferences, bool &initialized)
{
    ClockPreferences current;
    esp_err_t error = read_clock_preferences(current);
    if (error != ESP_OK) {
        return error;
    }
    uint32_t durable_preferences = 0;
    error = UserSettingsService::instance().getUInt32(kClockPreferencesKey, &durable_preferences);
    if (error == ESP_OK && durable_preferences != encode_clock_preferences(current)) {
        error = ESP_ERR_INVALID_STATE;
    }
    if (error != ESP_OK) {
        // A failed commit/readback may leave durable and runtime state different.
        // An acknowledgement cannot present either candidate as confirmed configuration.
        ESP_LOGE(TAG, "Clock companion preferences readback unavailable (%s)", esp_err_to_name(error));
        return error;
    }
    uint32_t marker = 0;
    error = UserSettingsService::instance().getUInt32(kClockCompanionInitializedKey, &marker);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        // This marker is written only by an authenticated initialization or explicit change.
        marker = 0;
        error = ESP_OK;
    }
    if (error == ESP_OK && marker > 1U) {
        error = ESP_ERR_INVALID_ARG;
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Clock companion marker read rejected: value=%lu (%s)",
            static_cast<unsigned long>(marker), esp_err_to_name(error));
        return error;
    }
    preferences = current;
    initialized = marker == 1U;
    return ESP_OK;
}

// No parameters: seals automatic timezone adoption without ever clearing user intent.
// Exception: Returns original write/readback errors or an exact-value mismatch.
static esp_err_t persist_clock_companion_marker()
{
    auto &settings = UserSettingsService::instance();
    esp_err_t error = settings.setUInt32(kClockCompanionInitializedKey, 1U);
    uint32_t confirmed = 0;
    if (error == ESP_OK) {
        error = settings.getUInt32(kClockCompanionInitializedKey, &confirmed);
    }
    if (error == ESP_OK && confirmed != 1U) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Clock companion marker persistence failed (%s)", esp_err_to_name(error));
    }
    return error;
}

// operation: Authenticated read, initial timezone adoption or explicit configuration.
// timezone: Typed timezone 0..2 or -1 to preserve it.
// enabled: Quiet-hours flag 0/1 or -1 to preserve it.
// start_minute: Half-open quiet-window start 0..1439 or -1 to preserve it.
// end_minute: Half-open quiet-window end 0..1439 or -1 to preserve it.
// preferences: Receives actual validated settings, including partial-operation outcomes.
// initialized: Receives the actual durable adoption seal.
// Exception: Preserves the first validation/persistence/publication error and logs readback failures.
esp_err_t apply_remote_clock_preferences(ClockRemoteOperation operation, int timezone,
    int enabled, int start_minute, int end_minute, ClockPreferences &preferences, bool &initialized)
{
    esp_err_t error = read_remote_clock_preferences(preferences, initialized);
    if (error != ESP_OK) {
        return error;
    }
    const bool unchanged = timezone == -1 && enabled == -1 && start_minute == -1 && end_minute == -1;
    const bool valid_values = timezone >= -1 && timezone <= 2 && enabled >= -1 && enabled <= 1 &&
        start_minute >= -1 && start_minute < 1440 && end_minute >= -1 && end_minute < 1440;
    bool valid_operation = false;
    switch (operation) {
    case ClockRemoteOperation::Read:
        valid_operation = unchanged;
        break;
    case ClockRemoteOperation::Initialize:
        valid_operation = timezone >= 0 && enabled == -1 && start_minute == -1 && end_minute == -1;
        break;
    case ClockRemoteOperation::Configure:
        valid_operation = !unchanged;
        break;
    }
    if (!valid_values || !valid_operation) {
        ESP_LOGE(TAG, "Remote clock values rejected: operation=%u zone=%d enabled=%d start=%d end=%d (%s)",
            static_cast<unsigned>(operation), timezone, enabled, start_minute, end_minute,
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (operation == ClockRemoteOperation::Read ||
        (operation == ClockRemoteOperation::Initialize && initialized)) {
        return ESP_OK;
    }
    ClockPreferences requested = preferences;
    if (timezone != -1) {
        requested.timezone = static_cast<ClockTimezone>(timezone);
    }
    if (enabled != -1) {
        requested.quiet_hours_enabled = enabled == 1;
    }
    if (start_minute != -1) {
        requested.quiet_start_minute = static_cast<uint16_t>(start_minute);
    }
    if (end_minute != -1) {
        requested.quiet_end_minute = static_cast<uint16_t>(end_minute);
    }
    error = validate_clock_preferences(requested);
    if (error != ESP_OK) {
        return error;
    }
    if (operation == ClockRemoteOperation::Configure && !initialized) {
        // Persist explicit user intent before changing preferences: a partial failure
        // must never let a later automatic reconnect replace that user's choice.
        error = persist_clock_companion_marker();
    }
    if (error == ESP_OK) {
        error = configure_clock_preferences(requested);
    }
    if (error == ESP_OK && operation == ClockRemoteOperation::Initialize) {
        error = persist_clock_companion_marker();
    }
    const esp_err_t readback_error = read_remote_clock_preferences(preferences, initialized);
    if (readback_error != ESP_OK) {
        ESP_LOGE(TAG, "Remote clock outcome readback failed (%s)", esp_err_to_name(readback_error));
        if (error == ESP_OK) {
            error = readback_error;
        }
    }
    return error;
}

} // namespace LUNASHELL

// year: Trusted GPS UTC year in the supported RTC calendar.
// month: UTC month, 1..12.
// day: Valid day in that month.
// hour: UTC hour, 0..23.
// minute: UTC minute, 0..59.
// second: UTC second, 0..59 (leap seconds are not representable by this RTC).
extern "C" esp_err_t LUNASHELL_time_sync_from_gps(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
)
{
    time_t gps_epoch = 0;
    const esp_err_t validation_error = validate_utc_calendar(
        year,
        month,
        day,
        hour,
        minute,
        second,
        &gps_epoch
    );
    if (validation_error != ESP_OK) {
        return validation_error;
    }

    std::lock_guard<std::mutex> sync_lock(s_sync_mutex);
    // Do not label a mixed GPS/SNTP comparison as an uninterrupted SNTP interval.
    s_drift_baseline.valid = false;
    struct timeval current_time = {};
    const bool system_time_valid =
        gettimeofday(&current_time, nullptr) == 0 &&
        epoch_valid(current_time.tv_sec);
    const int64_t gps_epoch_us =
        static_cast<int64_t>(gps_epoch) * 1000LL * 1000LL;
    const int64_t current_epoch_us =
        static_cast<int64_t>(current_time.tv_sec) * 1000LL * 1000LL +
        current_time.tv_usec;
    const int64_t drift_us = system_time_valid
        ? std::llabs(gps_epoch_us - current_epoch_us)
        : kGpsSystemResyncDriftUs;
    const bool system_sync_required =
        !s_trusted_gps_fix_observed ||
        !system_time_valid ||
        drift_us >= kGpsSystemResyncDriftUs;
    const int64_t persisted_delta_seconds =
        s_last_rtc_persisted_epoch == 0
            ? static_cast<int64_t>(kGpsRtcPersistenceIntervalSeconds)
            : std::llabs(
                  static_cast<int64_t>(gps_epoch) -
                  static_cast<int64_t>(s_last_rtc_persisted_epoch)
              );
    const bool rtc_persistence_required =
        system_sync_required ||
        persisted_delta_seconds >=
            static_cast<int64_t>(kGpsRtcPersistenceIntervalSeconds);

    if (system_sync_required) {
        const esp_err_t system_error = apply_system_utc(gps_epoch, "gps");
        if (system_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GPS UTC system synchronization failed: drift_us=%lld (%s)",
                static_cast<long long>(drift_us),
                esp_err_to_name(system_error)
            );
            return system_error;
        }
        s_trusted_gps_fix_observed = true;
    } else {
        // A coalesced fix still proves that the already-running system clock is
        // valid. Repair any older unavailable snapshot without rewriting it.
        report_time(true, true, "gps", ESP_OK);
    }

    if (!rtc_persistence_required) {
        return ESP_OK;
    }

    const int64_t persistence_attempt_at_us = esp_timer_get_time();
    if (s_rtc_persistence_retry_after_us != 0) {
        if (persistence_attempt_at_us < s_rtc_persistence_retry_after_us) {
            // The previous real error remains latched in the RTC snapshot and
            // in s_initialization_result. This fix performs no new persistence
            // I/O, so return that same error without manufacturing a new
            // hardware attempt or success sample.
            return s_rtc_persistence_error;
        }
        ESP_LOGW(
            TAG,
            "Retrying RTC persistence after %s",
            esp_err_to_name(s_rtc_persistence_error)
        );
    }

    const esp_err_t rtc_result = persist_epoch_to_rtc(gps_epoch);
    s_initialization_result = rtc_result;
    if (rtc_result != ESP_OK) {
        const uint32_t retry_delay_seconds = record_rtc_persistence_failure(
            rtc_result,
            persistence_attempt_at_us
        );
        ESP_LOGE(
            TAG,
            "GPS UTC is valid but RTC persistence failed (%s); retry in "
            "%lu s",
            esp_err_to_name(rtc_result),
            static_cast<unsigned long>(retry_delay_seconds)
        );
        return rtc_result;
    }
    record_rtc_persistence_success(gps_epoch);
    ESP_LOGI(
        TAG,
        "GPS synchronized UTC=%04u-%02u-%02u %02u:%02u:%02u "
        "system=%s RTC=persisted",
        year,
        month,
        day,
        hour,
        minute,
        second,
        system_sync_required ? "updated" : "unchanged"
    );
    return ESP_OK;
}
