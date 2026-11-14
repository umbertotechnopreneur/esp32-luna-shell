#include "CODY_host_time_compat.hpp"
#include "esp_timer.h"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

namespace {
std::mutex s_time_mutex;
bool s_clock_valid = false;
int64_t s_base_us = 0;
int64_t s_base_tick_us = 0;
char s_timezone[40] = "UTC0";
int s_offset_seconds = 0;

// text: Firmware-emitted POSIX fixed-offset timezone, without daylight rules.
// offset: Receives seconds east of UTC on successful parsing.
bool parse_timezone(const char *text, int &offset)
{
    if (!text || std::strncmp(text, "UTC", 3) != 0) return false;
    const char *cursor = text + 3;
    int sign = 1;
    if (*cursor == '-' || *cursor == '+') sign = *cursor++ == '-' ? -1 : 1;
    if (*cursor < '0' || *cursor > '9') return false;
    long hours = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        hours = hours * 10 + (*cursor++ - '0');
        if (hours > 14) return false;
    }
    long minutes = 0;
    if (*cursor == ':') {
        ++cursor;
        if (cursor[0] < '0' || cursor[0] > '9' || cursor[1] < '0' || cursor[1] > '9') return false;
        minutes = (cursor[0] - '0') * 10 + cursor[1] - '0';
        cursor += 2;
    }
    if (*cursor != '\0' || minutes > 59 || (hours == 14 && minutes != 0)) return false;
    offset = -sign * static_cast<int>(hours * 3600 + minutes * 60);
    return true;
}
} // namespace

// epoch_seconds: Explicit synthetic UTC from one successful scenario sample.
// sampled_at_us: Sample time in the simulator's monotonic domain.
// now_us: Current observation time in that same domain.
// Exception: Rejects missing, future or overflowing values without changing either clock.
extern "C" esp_err_t cody_host_time_seed_scenario(int64_t epoch_seconds, int64_t sampled_at_us, int64_t now_us)
{
    if (epoch_seconds <= 0 || sampled_at_us <= 0 || now_us < sampled_at_us ||
        epoch_seconds > INT64_MAX / 1000000) return ESP_ERR_INVALID_ARG;
    const auto delta = now_us - sampled_at_us;
    const auto base = epoch_seconds * 1000000;
    if (base > INT64_MAX - delta) return ESP_ERR_INVALID_ARG;
    const auto current = base + delta;
    const auto epoch = static_cast<time_t>(current / 1000000);
    if (static_cast<int64_t>(epoch) != current / 1000000) return ESP_ERR_INVALID_SIZE;
    const timeval virtual_time{epoch, static_cast<long>(current % 1000000)};
    return cody_host_settimeofday(&virtual_time, nullptr) == 0 ? ESP_OK : ESP_FAIL;
}

// value: Explicit UTC set by the actual firmware time owner.
// unused: POSIX timezone argument; only null is supported.
// Exception: Rejects unsupported values and never alters the computer clock.
int cody_host_settimeofday(const timeval *value, const void *unused)
{
    if (!value || unused || value->tv_usec < 0 || value->tv_usec >= 1000000 ||
        value->tv_sec < 0 || static_cast<uint64_t>(value->tv_sec) >
            static_cast<uint64_t>((INT64_MAX - value->tv_usec) / 1000000)) {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<std::mutex> lock(s_time_mutex);
    s_base_us = int64_t(value->tv_sec) * 1000000 + value->tv_usec;
    s_base_tick_us = esp_timer_get_time();
    s_clock_valid = true;
    return 0;
}

// value: Receives current virtual UTC only after an explicit successful setting.
// unused: POSIX timezone output; only null is supported.
// Exception: Returns EAGAIN when no virtual UTC was established.
int cody_host_gettimeofday(timeval *value, void *unused)
{
    if (!value || unused) { errno = EINVAL; return -1; }
    std::lock_guard<std::mutex> lock(s_time_mutex);
    if (!s_clock_valid) { errno = EAGAIN; return -1; }
    const auto elapsed = esp_timer_get_time() - s_base_tick_us;
    if (elapsed < 0 || s_base_us > INT64_MAX - elapsed) { errno = ERANGE; return -1; }
    const int64_t current = s_base_us + elapsed;
    value->tv_sec = static_cast<time_t>(current / 1000000);
    value->tv_usec = static_cast<long>(current % 1000000);
    return 0;
}

// value: UTC instant.
// result: Caller-owned calendar; never borrowed from the host libc's static storage.
// Exception: Preserves libc conversion failure.
tm *cody_host_gmtime_r(const time_t *value, tm *result)
{
    if (!value || !result) { errno = EINVAL; return nullptr; }
#ifdef _WIN32
    return gmtime_s(result, value) == 0 ? result : nullptr;
#else
    return ::gmtime_r(value, result);
#endif
}

// value: UTC instant.
// result: Receives a fixed-offset local calendar; no host timezone is consulted.
tm *cody_host_localtime_r(const time_t *value, tm *result)
{
    if (!value || !result) { errno = EINVAL; return nullptr; }
    int offset = 0;
    { std::lock_guard<std::mutex> lock(s_time_mutex); offset = s_offset_seconds; }
    const int64_t original = static_cast<int64_t>(*value);
    if ((offset > 0 && original > INT64_MAX - offset) || (offset < 0 && original < INT64_MIN - offset)) {
        errno = ERANGE;
        return nullptr;
    }
    const int64_t shifted = original + offset;
    const time_t local = static_cast<time_t>(shifted);
    if (static_cast<int64_t>(local) != shifted) { errno = ERANGE; return nullptr; }
    auto *converted = cody_host_gmtime_r(&local, result);
    if (converted) converted->tm_isdst = 0;
    return converted;
}

// value: Caller-owned local calendar, normalized using the configured fixed offset.
// Exception: Returns -1 and sets errno on invalid input or conversion failure.
time_t cody_host_mktime(tm *value)
{
    if (!value) { errno = EINVAL; return static_cast<time_t>(-1); }
    int offset = 0;
    { std::lock_guard<std::mutex> lock(s_time_mutex); offset = s_offset_seconds; }
    value->tm_isdst = 0;
#ifdef _WIN32
    const auto local_epoch = _mkgmtime64(value);
#else
    const auto local_epoch = timegm(value);
#endif
    if (local_epoch == static_cast<decltype(local_epoch)>(-1)) return static_cast<time_t>(-1);
    const int64_t epoch = static_cast<int64_t>(local_epoch);
    if ((offset > 0 && epoch < INT64_MIN + offset) || (offset < 0 && epoch > INT64_MAX + offset)) {
        errno = ERANGE;
        return static_cast<time_t>(-1);
    }
    const int64_t result = epoch - offset;
    const auto converted = static_cast<time_t>(result);
    if (static_cast<int64_t>(converted) != result) { errno = ERANGE; return static_cast<time_t>(-1); }
    return converted;
}

// name: Only the private virtual TZ key is accepted.
// value: Firmware's bounded fixed-offset TZ string.
// overwrite: POSIX replacement selector.
// Exception: Rejects unsupported specifications without mutating the process environment.
int cody_host_setenv(const char *name, const char *value, int overwrite)
{
    int offset = 0;
    if (!name || std::strcmp(name, "TZ") != 0 || !value || std::strlen(value) >= sizeof(s_timezone) ||
        !parse_timezone(value, offset)) { errno = EINVAL; return -1; }
    std::lock_guard<std::mutex> lock(s_time_mutex);
    if (!overwrite && s_timezone[0] != '\0') return 0;
    std::memcpy(s_timezone, value, std::strlen(value) + 1);
    s_offset_seconds = offset;
    return 0;
}

// name: Only the isolated virtual TZ key is available.
// Exception: Returns null for other keys; storage belongs to this time adapter.
const char *cody_host_getenv(const char *name)
{
    if (!name || std::strcmp(name, "TZ") != 0) return nullptr;
    return s_timezone;
}

// No parameters: virtual fixed-offset TZ was already validated and applied by setenv.
void cody_host_tzset() {}

// destination: Bounded output buffer.
// source: Null-terminated firmware string.
// capacity: Total output capacity, including terminator.
std::size_t cody_host_strlcpy(char *destination, const char *source, std::size_t capacity)
{
    const auto length = std::strlen(source);
    if (capacity) {
        const auto copied = length < capacity - 1 ? length : capacity - 1;
        std::memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}
