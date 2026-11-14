// Explicitly unavailable RTC/SNTP transport. It must never substitute the PC
// clock or report a synchronization that has not occurred.
#include "LUNASHELL_platform.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include <atomic>

namespace { std::atomic<bool> s_netif_initialized{false}; }

// No parameters: initialize the isolated simulated interface registry only.
extern "C" esp_err_t esp_netif_init()
{
    return s_netif_initialized.exchange(true) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

// config: Accept the firmware's deferred configuration; no network request is
// made here. Starting SNTP remains explicitly unsupported on the host.
extern "C" esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config)
{ return config ? ESP_OK : ESP_ERR_INVALID_ARG; }

// No parameters: no network sample or callback can be delivered by this backend.
extern "C" esp_err_t esp_netif_sntp_start() { return ESP_ERR_NOT_SUPPORTED; }

// No parameters: no SNTP owner is created when configuration is rejected.
extern "C" void esp_netif_sntp_deinit() {}

// time: Receives no value because no physical or scenario RTC is available.
// sampled_at_us: Receives no timestamp on failure.
extern "C" esp_err_t LUNASHELL_rtc_read(LUNASHELL_calendar_time_t *time, int64_t *sampled_at_us)
{ return time && sampled_at_us ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG; }

// time: Explicit UTC calendar supplied by the real time owner.
// Exception: No host RTC is advertised or written.
extern "C" esp_err_t LUNASHELL_rtc_write(const LUNASHELL_calendar_time_t *time)
{ return time ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG; }
