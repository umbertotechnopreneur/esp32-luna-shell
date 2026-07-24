/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

namespace {

const char *TAG = "qsee_time";
bool s_initialized;
bool s_sntp_started;
char s_timezone[40] = "UTC0";

bool epoch_valid(time_t epoch)
{
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    return utc.tm_year >= (2024 - 1900);
}

bool calendar_valid(const qsee_calendar_time_t &calendar)
{
    return calendar.year >= 2024 && calendar.year <= 2099 &&
           calendar.month >= 1 && calendar.month <= 12 &&
           calendar.day >= 1 && calendar.day <= 31 &&
           calendar.hour <= 23 && calendar.minute <= 59 &&
           calendar.second <= 59;
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

void report_time(bool valid, bool synchronized, const char *source)
{
    qsee::DeviceSnapshotService::instance().reportTime(
        valid,
        synchronized,
        source,
        s_timezone
    );
}

esp_err_t persist_epoch_to_rtc(time_t epoch)
{
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    const qsee_calendar_time_t calendar = {
        .clock_integrity = true,
        .year = static_cast<uint16_t>(utc.tm_year + 1900),
        .month = static_cast<uint8_t>(utc.tm_mon + 1),
        .day = static_cast<uint8_t>(utc.tm_mday),
        .hour = static_cast<uint8_t>(utc.tm_hour),
        .minute = static_cast<uint8_t>(utc.tm_min),
        .second = static_cast<uint8_t>(utc.tm_sec),
    };
    return qsee_rtc_write(&calendar);
}

void sntp_sync_callback(struct timeval *time_value)
{
    if (time_value == nullptr || !epoch_valid(time_value->tv_sec)) {
        ESP_LOGW(TAG, "SNTP callback supplied an invalid epoch");
        report_time(false, false, "none");
        return;
    }

    const esp_err_t rtc_result = persist_epoch_to_rtc(time_value->tv_sec);
    struct tm utc = {};
    gmtime_r(&time_value->tv_sec, &utc);
    ESP_LOGI(
        TAG,
        "SNTP synchronized UTC=%04d-%02d-%02d %02d:%02d:%02d RTC=%s",
        utc.tm_year + 1900,
        utc.tm_mon + 1,
        utc.tm_mday,
        utc.tm_hour,
        utc.tm_min,
        utc.tm_sec,
        esp_err_to_name(rtc_result)
    );
    report_time(true, true, "sntp");
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
    const time_t epoch = utc_epoch(year, month, day, hour, minute, second);
    if (epoch <= 0 || !epoch_valid(epoch)) {
        return ESP_ERR_INVALID_ARG;
    }

    const struct timeval system_time = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&system_time, nullptr) != 0) {
        return ESP_FAIL;
    }

    const esp_err_t rtc_result = persist_epoch_to_rtc(epoch);
    report_time(true, true, source);
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

extern "C" esp_err_t qsee_time_init(const char *timezone)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (timezone != nullptr && timezone[0] != '\0' &&
        std::strlen(timezone) < sizeof(s_timezone)) {
        strlcpy(s_timezone, timezone, sizeof(s_timezone));
    }
    setenv("TZ", s_timezone, 1);
    tzset();

    bool restored = false;
    qsee_calendar_time_t rtc = {};
    const esp_err_t rtc_result = qsee_rtc_read(&rtc);
    if (rtc_result == ESP_OK && rtc.clock_integrity && calendar_valid(rtc)) {
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
    }

    const esp_err_t netif_result = esp_netif_init();
    if (netif_result != ESP_OK && netif_result != ESP_ERR_INVALID_STATE) {
        report_time(restored, restored, restored ? "rtc" : "none");
        return netif_result;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2,
        ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com")
    );
    config.start = false;
    config.sync_cb = sntp_sync_callback;
    const esp_err_t sntp_result = esp_netif_sntp_init(&config);
    if (sntp_result != ESP_OK) {
        ESP_LOGW(TAG, "SNTP initialization failed: %s", esp_err_to_name(sntp_result));
        report_time(restored, restored, restored ? "rtc" : "none");
        return sntp_result;
    }

    s_initialized = true;
    report_time(restored, restored, restored ? "rtc" : "none");
    ESP_LOGI(
        TAG,
        "Time service ready: timezone=%s RTC=%s restored=%s",
        s_timezone,
        esp_err_to_name(rtc_result),
        restored ? "yes" : "no"
    );
    return ESP_OK;
}

extern "C" esp_err_t qsee_time_notify_wifi_online(void)
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

extern "C" esp_err_t qsee_time_sync_from_gps(
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
