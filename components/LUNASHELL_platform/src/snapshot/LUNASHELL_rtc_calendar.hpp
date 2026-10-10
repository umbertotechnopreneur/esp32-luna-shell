#pragma once

#include "LUNASHELL_platform.h"

namespace LUNASHELL::rtc_calendar {
// value: Validated decimal calendar field for an RTC write.
uint8_t decimal_to_bcd(uint8_t value);
// time: Calendar whose epoch must be trusted (2024 through 2099).
bool valid_calendar_time(const LUNASHELL_calendar_time_t &time);
// time: Calendar checked against the RTC register range (2000 through 2099).
bool valid_rtc_register_calendar(const LUNASHELL_calendar_time_t &time);
// requested: Trusted calendar submitted by the acquisition owner.
// observed: Structurally valid readback; at most one hardware tick is accepted.
bool rtc_write_readback_matches(const LUNASHELL_calendar_time_t &requested,
                                const LUNASHELL_calendar_time_t &observed);
// raw: Seven registers already acquired by the sole RTC hardware owner.
// time: Receives a structurally valid decoded calendar, even when untrusted.
// Exception: Returns malformed register or integrity/epoch failure without fabricating a sample.
esp_err_t decode_registers(const uint8_t (&raw)[7], LUNASHELL_calendar_time_t &time);
}
