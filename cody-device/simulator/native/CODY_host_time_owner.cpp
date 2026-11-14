// Compile the production owner unchanged, interposing only POSIX/platform IO.
// Include the standard headers first so substitutions cannot affect their ABI.
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <limits>
#include <initializer_list>
#include <sys/time.h>
#include "CODY_host_time_compat.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_snapshot_internal.hpp"
#include "esp_log.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "esp_timer.h"

// The production owner's own headers are loaded above. These substitutions
// affect only calls in the subsequently included source, not host libraries.
#define settimeofday cody_host_settimeofday
#define gettimeofday cody_host_gettimeofday
#define gmtime_r cody_host_gmtime_r
#define localtime_r cody_host_localtime_r
#define mktime cody_host_mktime
#define setenv cody_host_setenv
#define getenv cody_host_getenv
#define tzset cody_host_tzset
#define strlcpy cody_host_strlcpy
#include "../../components/LUNASHELL_platform/src/runtime/LUNASHELL_time.cpp"

#if defined(_WIN32)
#define CODY_HOST_CLOCK_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_CLOCK_API extern "C" __attribute__((visibility("default")))
#endif

// The simulated device uses the unchanged firmware time owner and an explicit
// PC time sample, without changing the host's own clock.
CODY_HOST_CLOCK_API esp_err_t cody_host_pc_clock_init(int64_t epoch, int offset_minutes)
{
    const auto result = LUNASHELL_time_init("UTC0");
    return result == ESP_OK ? LUNASHELL_time_sync_from_pc(epoch, offset_minutes) : result;
}

CODY_HOST_CLOCK_API esp_err_t cody_host_pc_clock_refresh()
{
    return LUNASHELL_time_refresh_snapshot();
}

// epoch: Receives the current simulated UTC or zero when unavailable.
// offset_minutes: Receives the simulated UTC offset or INT_MIN when unavailable.
// Exception: Preserves the canonical snapshot error and rejects null outputs.
CODY_HOST_CLOCK_API esp_err_t cody_host_simulated_clock_read(int64_t *epoch, int *offset_minutes)
{
    if (!epoch || !offset_minutes) return ESP_ERR_INVALID_ARG;
    LUNASHELL::HardwareStatus status{};
    const auto error = LUNASHELL::DeviceSnapshotService::instance().readHardwareStatus(status);
    if (error != ESP_OK) return error;
    const auto &clock = status.clock;
    *epoch = clock.data_valid && !clock.data_stale ? clock.utc_epoch_seconds : 0;
    *offset_minutes = clock.utc_offset_valid ? clock.utc_offset_minutes : INT_MIN;
    return ESP_OK;
}
