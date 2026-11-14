#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <sys/time.h>
#include "esp_err.h"

// Do not silently truncate device UTC to Winsock's 32-bit Windows timeval.
static_assert(sizeof(((timeval *)nullptr)->tv_sec) >= sizeof(time_t),
    "Host time owner requires a timeval with the complete time_t width");

// These private host calls replace POSIX clock and timezone functions only in
// the time owner's translation unit. They never change the computer's clock or TZ.
int cody_host_settimeofday(const timeval *value, const void *unused);
int cody_host_gettimeofday(timeval *value, void *unused);
tm *cody_host_gmtime_r(const time_t *value, tm *result);
tm *cody_host_localtime_r(const time_t *value, tm *result);
time_t cody_host_mktime(tm *value);
int cody_host_setenv(const char *name, const char *value, int overwrite);
const char *cody_host_getenv(const char *name);
void cody_host_tzset();
std::size_t cody_host_strlcpy(char *destination, const char *source, std::size_t capacity);

// epoch_seconds: Explicit synthetic UTC from one valid simulated clock sample.
// sampled_at_us: Monotonic timestamp attached to that exact sample.
// now_us: Current simulator monotonic time in the same domain.
// Exception: Rejects missing, future or out-of-range samples; never adjusts host time.
#if defined(_WIN32)
extern "C" __declspec(dllexport)
#else
extern "C" __attribute__((visibility("default")))
#endif
esp_err_t cody_host_time_seed_scenario(int64_t epoch_seconds, int64_t sampled_at_us, int64_t now_us);
