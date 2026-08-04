/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

namespace {

const char *TAG = "LUNASHELL_time";
bool s_initialized;
bool s_sntp_started;
esp_err_t s_initialization_result = ESP_ERR_INVALID_STATE;
char s_timezone[40] = "UTC0";

bool epoch_valid(time_t epoch)
{
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    return utc.tm_year >= (2024 - 1900);
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
    LUNASHELL::DeviceSnapshotService::instance().reportTime(
        valid,
        synchronized,
        source,
        s_timezone,
        error
    );
}

esp_err_t persist_epoch_to_rtc(time_t epoch)
{
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

void sntp_sync_callback(struct timeval *time_value)
{
    if (time_value == nullptr || !epoch_valid(time_value->tv_sec)) {
        ESP_LOGW(TAG, "SNTP callback supplied an invalid epoch");
        report_time(false, false, "none", ESP_ERR_INVALID_RESPONSE);
        return;
    }

    const esp_err_t rtc_result = persist_epoch_to_rtc(time_value->tv_sec);
    struct tm utc = {};
    gmtime_r(&time_value->tv_sec, &utc);
    if (rtc_result == ESP_OK) {
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
        ESP_LOGE(
            TAG,
            "SNTP set system UTC but RTC persistence failed (%s)",
            esp_err_to_name(rtc_result)
        );
    }
    s_initialization_result = rtc_result;
    report_time(true, true, "sntp", rtc_result);
}

esp_err_t apply_utc_calendar(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second,
    const char *source
)
{
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
        ESP_LOGE(
            TAG,
            "%s time rejected: invalid UTC calendar",
            source != nullptr ? source : "unknown"
        );
        return ESP_ERR_INVALID_ARG;
    }
    const time_t epoch = utc_epoch(year, month, day, hour, minute, second);
    if (epoch <= 0 || !epoch_valid(epoch)) {
        return ESP_ERR_INVALID_ARG;
    }

    const struct timeval system_time = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&system_time, nullptr) != 0) {
        report_time(false, false, "none", ESP_FAIL);
        return ESP_FAIL;
    }

    const esp_err_t rtc_result = persist_epoch_to_rtc(epoch);
    report_time(true, true, source, rtc_result);
    ESP_LOGI(
        TAG,
        "%s synchronized UTC=%04u-%02u-%02u %02u:%02u:%02u RTC=%s",
        source,
        year,
        month,
        day,
        hour,
        minute,
        second,
        esp_err_to_name(rtc_result)
    );
    return rtc_result;
}

}  // namespace

extern "C" esp_err_t LUNASHELL_time_init(const char *timezone)
{
    if (s_initialized) {
        return s_initialization_result;
    }

    if (timezone == nullptr || timezone[0] == '\0' ||
        std::strlen(timezone) >= sizeof(s_timezone)) {
        ESP_LOGE(TAG, "Time service rejected invalid timezone");
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t timezone_result = apply_timezone(timezone);
    if (timezone_result != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportTime(
            false,
            false,
            "none",
            timezone,
            timezone_result
        );
        return timezone_result;
    }

    bool restored = false;
    esp_err_t restore_result = ESP_OK;
    LUNASHELL_calendar_time_t rtc = {};
    const esp_err_t rtc_result = LUNASHELL_rtc_read(&rtc);
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

    s_initialized = true;
    s_initialization_result = restore_result;
    report_time(
        restored,
        restored,
        restored ? "rtc" : "none",
        restore_result
    );
    ESP_LOGI(
        TAG,
        "Time service ready: timezone=%s RTC=%s restored=%s",
        s_timezone,
        esp_err_to_name(rtc_result),
        restored ? "yes" : "no"
    );
    return restore_result;
}

extern "C" esp_err_t LUNASHELL_time_notify_wifi_online(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sntp_started) {
        return ESP_OK;
    }
    const esp_err_t result = esp_netif_sntp_start();
    if (result == ESP_OK) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "SNTP synchronization requested after DHCP");
    } else {
        ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(result));
    }
    return result;
}

extern "C" esp_err_t LUNASHELL_time_sync_from_gps(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
)
{
    return apply_utc_calendar(year, month, day, hour, minute, second, "gps");
}
