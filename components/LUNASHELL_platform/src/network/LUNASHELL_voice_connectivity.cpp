#include "LUNASHELL_voice_connectivity.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#if CONFIG_ESP_TLS_USING_MBEDTLS && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE && CONFIG_MBEDTLS_HAVE_TIME_DATE
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <mutex>
#endif

namespace {
constexpr char kTag[] = "LUNASHELL_voice_net";

#if CONFIG_ESP_TLS_USING_MBEDTLS && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE && CONFIG_MBEDTLS_HAVE_TIME_DATE
constexpr char kHost[] = "api.openai.com";
constexpr int kPort = 443;
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr int64_t kMaxCheckBudgetUs = 5000000;

struct InternetCheck {
    uint32_t id = 0;
    int64_t deadline_us = 0;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    bool running = false;
};

std::mutex s_mutex;
InternetCheck s_check;
uint32_t s_last_id = 0;

// No parameters: reads the Wi-Fi acquisition owner's current state.
// Exception: Returns INVALID_STATE for an unavailable link or the original snapshot error.
esp_err_t check_wifi()
{
    LUNASHELL::PairingObservation observation{};
    const esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readPairingObservation(observation);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "Wi-Fi observation failed (%s)", esp_err_to_name(error));
        return error;
    }
    if (!observation.wifi_online) {
        ESP_LOGD(kTag, "Internet check rejected: Wi-Fi has no current IP");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// No parameters: reads the canonical clock needed for certificate date verification.
// Exception: Returns the original snapshot/clock error or INVALID_STATE for unavailable time.
esp_err_t check_clock()
{
    LUNASHELL::ClockSnapshot clock{};
    const esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
    if (error != ESP_OK) {
        return error;
    }
    if (!clock.data_valid || clock.data_stale || clock.last_error != ESP_OK || clock.utc_epoch_seconds <= 0) {
        return clock.last_error != ESP_OK ? clock.last_error : ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// tls: Failed ESP-TLS connection whose error record is read before destroying it.
// Exception: Returns the original ESP-TLS failure or ESP_FAIL when none was recorded.
esp_err_t connection_error(esp_tls_t *tls)
{
    esp_tls_error_handle_t handle = nullptr;
    const esp_err_t handle_error = esp_tls_get_error_handle(tls, &handle);
    if (handle_error != ESP_OK) {
        return handle_error;
    }
    int tls_error = 0;
    int certificate_flags = 0;
    const esp_err_t error = esp_tls_get_and_clear_last_error(handle, &tls_error, &certificate_flags);
    ESP_LOGW(kTag, "TLS handshake failed: error=%s TLS=%d certificate_flags=0x%x",
        esp_err_to_name(error), tls_error, static_cast<unsigned>(certificate_flags));
    return error != ESP_OK ? error : ESP_FAIL;
}

// deadline_us: Absolute gesture deadline. A result at or after this time cannot admit capture.
// Exception: Returns the original clock, TLS or Wi-Fi error, or TIMEOUT after expiry.
esp_err_t probe_internet(int64_t deadline_us)
{
    if (esp_timer_get_time() >= deadline_us) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = check_clock();
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "TLS clock unavailable (%s)", esp_err_to_name(result));
        return result;
    }
    const int64_t remaining_us = deadline_us - esp_timer_get_time();
    if (remaining_us <= 0) {
        return ESP_ERR_TIMEOUT;
    }
    esp_tls_t *const tls = esp_tls_init();
    if (tls == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    esp_tls_cfg_t config{};
    config.timeout_ms = static_cast<int>((remaining_us + 999) / 1000);
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.addr_family = ESP_TLS_AF_INET;

    // Connect and authenticate only. No HTTP request or audio leaves the device.
    // DNS retries and individual SDK socket waits can outlast the UI deadline.
    // This worker retains exclusive ownership until ESP-TLS returns.
    if (esp_timer_get_time() >= deadline_us) {
        result = ESP_ERR_TIMEOUT;
    } else if (esp_tls_conn_new_sync(kHost, sizeof(kHost) - 1, kPort, &config, tls) != 1) {
        result = connection_error(tls);
    } else {
        result = esp_timer_get_time() < deadline_us ? check_wifi() : ESP_ERR_TIMEOUT;
    }
    const int destroy_result = esp_tls_conn_destroy(tls);
    if (destroy_result != 0) {
        ESP_LOGW(kTag, "TLS connection cleanup failed (%d)", destroy_result);
        if (result == ESP_OK) {
            result = ESP_FAIL;
        }
    }
    if (result == ESP_OK && esp_timer_get_time() >= deadline_us) {
        return ESP_ERR_TIMEOUT;
    }
    return result;
}

// context: Unused task argument. The one in-flight request owns the deadline.
void run_check(void *context)
{
    (void)context;
    int64_t deadline_us = 0;
    {
        const std::lock_guard<std::mutex> guard(s_mutex);
        deadline_us = s_check.deadline_us;
    }
    const esp_err_t result = probe_internet(deadline_us);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Internet check did not admit dictation (%s)", esp_err_to_name(result));
    }
    {
        const std::lock_guard<std::mutex> guard(s_mutex);
        s_check.result = result;
        s_check.running = false;
    }
    vTaskDelete(nullptr);
}
#endif
} // namespace

// deadline_us: Absolute deadline on the same monotonic clock as the display owner.
// check_id: Receives a new result identity only on successful admission.
// Exception: Returns the original snapshot/task error, or rejects offline, busy or expired attempts.
esp_err_t LUNASHELL::startVoiceInternetCheck(int64_t deadline_us, uint32_t &check_id)
{
    check_id = 0;
#if CONFIG_ESP_TLS_USING_MBEDTLS && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE && CONFIG_MBEDTLS_HAVE_TIME_DATE
    const esp_err_t wifi_error = check_wifi();
    if (wifi_error != ESP_OK) {
        return wifi_error;
    }
    const std::lock_guard<std::mutex> guard(s_mutex);
    const int64_t now_us = esp_timer_get_time();
    if (deadline_us <= now_us) {
        return ESP_ERR_TIMEOUT;
    }
    if (deadline_us - now_us > kMaxCheckBudgetUs) {
        ESP_LOGW(kTag, "Internet check deadline exceeds the gesture budget");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_check.running || s_last_id == UINT32_MAX) {
        ESP_LOGD(kTag, "Internet check worker is unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    s_check = {++s_last_id, deadline_us, ESP_ERR_INVALID_STATE, true};
    // ESP-IDF's dynamic FreeRTOS allocator supplies an internal RAM stack.
    // The task exists only while this gesture's DNS/TLS attempt is in flight.
    if (xTaskCreatePinnedToCore(run_check, "CodyVoiceNet", kWorkerStackBytes,
            nullptr, 1, nullptr, 0) != pdPASS) {
        s_check = {};
        ESP_LOGW(kTag, "Internet check task allocation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    check_id = s_check.id;
    return ESP_OK;
#else
    (void)deadline_us;
    ESP_LOGW(kTag, "Internet check requires ESP-TLS, a CA bundle and certificate date verification");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// check_id: Exact gesture identity from startVoiceInternetCheck.
// complete: False only while the identified check is pending and unexpired.
// Exception: Returns the original failure or rejects expired/unknown identities.
esp_err_t LUNASHELL::pollVoiceInternetCheck(uint32_t check_id, bool &complete)
{
    complete = true;
#if CONFIG_ESP_TLS_USING_MBEDTLS && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE && CONFIG_MBEDTLS_HAVE_TIME_DATE
    const std::lock_guard<std::mutex> guard(s_mutex);
    if (check_id == 0 || check_id != s_check.id) {
        return ESP_ERR_INVALID_STATE;
    }
    if (esp_timer_get_time() >= s_check.deadline_us) {
        s_check.id = 0;
        return ESP_ERR_TIMEOUT;
    }
    if (s_check.running) {
        complete = false;
        return ESP_OK;
    }
    s_check.id = 0; // A previous probe is never reusable evidence for another tap.
    if (s_check.result != ESP_OK) {
        return s_check.result;
    }
    const esp_err_t wifi_error = check_wifi();
    if (wifi_error != ESP_OK) {
        return wifi_error;
    }
    return esp_timer_get_time() < s_check.deadline_us ? ESP_OK : ESP_ERR_TIMEOUT;
#else
    (void)check_id;
    return ESP_ERR_INVALID_STATE;
#endif
}
