/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "LUNASHELL_rtc_calendar.hpp"
#include "esp_log.h"

namespace LUNASHELL::rtc_calendar {
namespace {
constexpr char TAG[] = "LUNASHELL_rtc";
}

// value: Validated packed BCD byte from the RTC owner.
uint8_t bcd_to_decimal(uint8_t value)
{
    return static_cast<uint8_t>(((value >> 4) * 10U) + (value & 0x0FU));
}

// value: Validated decimal calendar field for an RTC write.
uint8_t decimal_to_bcd(uint8_t value)
{
    return static_cast<uint8_t>(((value / 10U) << 4) | (value % 10U));
}

// time: Calendar whose epoch must be trusted (2024 through 2099).
bool valid_calendar_time(const LUNASHELL_calendar_time_t &time)
{
    if (time.year < 2024 || time.year > 2099 ||
        time.month < 1 || time.month > 12 ||
        time.hour > 23 || time.minute > 59 || time.second > 59) {
        return false;
    }
    static constexpr uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    uint8_t maximum_day = days_per_month[time.month - 1U];
    const bool leap =
        (time.year % 4U == 0U && time.year % 100U != 0U) ||
        time.year % 400U == 0U;
    if (time.month == 2U && leap) {
        maximum_day = 29;
    }
    return time.day >= 1U && time.day <= maximum_day;
}

// time: Calendar checked against the RTC register range (2000 through 2099).
bool valid_rtc_register_calendar(const LUNASHELL_calendar_time_t &time)
{
    if (time.year < 2000 || time.year > 2099 ||
        time.month < 1 || time.month > 12 ||
        time.hour > 23 || time.minute > 59 || time.second > 59) {
        return false;
    }
    static constexpr uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    uint8_t maximum_day = days_per_month[time.month - 1U];
    const bool leap =
        (time.year % 4U == 0U && time.year % 100U != 0U) ||
        time.year % 400U == 0U;
    if (time.month == 2U && leap) {
        maximum_day = 29;
    }
    return time.day >= 1U && time.day <= maximum_day;
}

// value: Packed BCD register field before calendar conversion.
bool valid_bcd(uint8_t value)
{
    return (value & 0x0FU) <= 9U && ((value >> 4U) & 0x0FU) <= 9U;
}

// left: First structurally valid calendar.
// right: Second calendar; oscillator integrity is checked separately by the owner.
bool calendar_fields_equal(
    const LUNASHELL_calendar_time_t &left,
    const LUNASHELL_calendar_time_t &right
)
{
    return left.year == right.year &&
           left.month == right.month &&
           left.day == right.day &&
           left.hour == right.hour &&
           left.minute == right.minute &&
           left.second == right.second;
}

// time: Structurally valid calendar advanced for bounded write/readback tolerance.
void advance_calendar_one_second(LUNASHELL_calendar_time_t *time)
{
    if (time == nullptr) {
        return;
    }
    if (++time->second <= 59U) {
        return;
    }
    time->second = 0;
    if (++time->minute <= 59U) {
        return;
    }
    time->minute = 0;
    if (++time->hour <= 23U) {
        return;
    }
    time->hour = 0;
    static constexpr uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    uint8_t maximum_day = days_per_month[time->month - 1U];
    const bool leap =
        (time->year % 4U == 0U && time->year % 100U != 0U) ||
        time->year % 400U == 0U;
    if (time->month == 2U && leap) {
        maximum_day = 29U;
    }
    if (++time->day <= maximum_day) {
        return;
    }
    time->day = 1;
    if (++time->month <= 12U) {
        return;
    }
    time->month = 1;
    ++time->year;
}

// requested: Trusted calendar submitted by the acquisition owner.
// observed: Structurally valid readback; at most one hardware tick is accepted.
bool rtc_write_readback_matches(
    const LUNASHELL_calendar_time_t &requested,
    const LUNASHELL_calendar_time_t &observed
)
{
    if (calendar_fields_equal(requested, observed)) {
        return true;
    }
    LUNASHELL_calendar_time_t next = requested;
    advance_calendar_one_second(&next);
    return calendar_fields_equal(next, observed);
}

// raw: Seven registers already acquired by the sole RTC hardware owner.
// time: Receives a structurally valid decoded calendar, even when untrusted.
// Exception: Returns malformed register or integrity/epoch failure without fabricating a sample.
esp_err_t decode_registers(const uint8_t (&raw)[7], LUNASHELL_calendar_time_t &time)
{
    const uint8_t seconds = raw[0] & 0x7FU;
    const uint8_t minutes = raw[1] & 0x7FU;
    const uint8_t hours = raw[2] & 0x3FU;
    const uint8_t day = raw[3] & 0x3FU;
    const uint8_t month = raw[5] & 0x1FU;
    if (!valid_bcd(seconds) || !valid_bcd(minutes) ||
        !valid_bcd(hours) || !valid_bcd(day) ||
        !valid_bcd(month) || !valid_bcd(raw[6])) {
        ESP_LOGE(
            TAG,
            "RTC PCF85063 rejected non-BCD calendar: raw=[%02x %02x %02x %02x %02x %02x %02x]",
            raw[0],
            raw[1],
            raw[2],
            raw[3],
            raw[4],
            raw[5],
            raw[6]
        );
        return ESP_ERR_INVALID_RESPONSE;
    }

    const LUNASHELL_calendar_time_t decoded = {
        .clock_integrity = (raw[0] & 0x80U) == 0,
        .year = static_cast<uint16_t>(2000U + bcd_to_decimal(raw[6])),
        .month = bcd_to_decimal(month),
        .day = bcd_to_decimal(day),
        .hour = bcd_to_decimal(hours),
        .minute = bcd_to_decimal(minutes),
        .second = bcd_to_decimal(seconds),
    };
    if (!valid_rtc_register_calendar(decoded)) {
        ESP_LOGE(
            TAG,
            "RTC PCF85063 rejected calendar range: decoded=%04u-%02u-%02u %02u:%02u:%02u raw=[%02x %02x %02x %02x %02x %02x %02x]",
            decoded.year,
            decoded.month,
            decoded.day,
            decoded.hour,
            decoded.minute,
            decoded.second,
            raw[0],
            raw[1],
            raw[2],
            raw[3],
            raw[4],
            raw[5],
            raw[6]
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    time = decoded;
    if (!decoded.clock_integrity) {
        ESP_LOGW(
            TAG,
            "RTC PCF85063 calendar integrity unavailable (oscillator-stop flag set): "
            "decoded=%04u-%02u-%02u %02u:%02u:%02u "
            "raw=[%02x %02x %02x %02x %02x %02x %02x]",
            decoded.year,
            decoded.month,
            decoded.day,
            decoded.hour,
            decoded.minute,
            decoded.second,
            raw[0],
            raw[1],
            raw[2],
            raw[3],
            raw[4],
            raw[5],
            raw[6]
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (!valid_calendar_time(decoded)) {
        ESP_LOGW(
            TAG,
            "RTC PCF85063 calendar is structurally valid but not initialized "
            "to a trusted epoch: decoded=%04u-%02u-%02u %02u:%02u:%02u",
            decoded.year,
            decoded.month,
            decoded.day,
            decoded.hour,
            decoded.minute,
            decoded.second
        );
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

} // namespace LUNASHELL::rtc_calendar
