/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * The HTTP API is intentionally read-only. Transport, authentication and
 * lifecycle handling live here; JSON serialization is kept in
 * qsee_http_json.cpp.
 */

// Read-only HTTP server for device status, diagnostics, and capabilities.
// Handles auth, request routing, and snapshot refresh orchestration.

#include "qsee_http_server.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_http_json.hpp"
#include "qsee_platform.h"
#include "qsee_settings.hpp"

namespace {

constexpr const char *kTag = "qsee_http";
constexpr const char *kApiKeySetting = "ApiKey";
constexpr const char *kEnabledSetting = "http_enabled";
constexpr std::size_t kAuthorizationPrefixLength = 7U;

constexpr uint64_t kAuthFailureWindowUs = 60ULL * 1000000ULL;
constexpr uint32_t kAuthFailureLimit = 20U;
constexpr std::size_t kAuthHeaderCapacity = 96U;

constexpr const char *kHealthPath = "/api/v1/health";
constexpr const char *kCapabilitiesPath = "/api/v1/capabilities";
constexpr const char *kInfoPath = "/api/v1/info";
constexpr const char *kDiagnosticsPath = "/api/v1/diagnostics";
constexpr const char *kRtcPath = "/api/v1/rtc";
constexpr const char *kGpsPath = "/api/v1/gps";
constexpr const char *kStatusPath = "/api/v1/status";

struct HttpMetricsState {
    qsee::HttpMetricsView view{};
    uint64_t auth_window_started_us = 0U;
};

httpd_handle_t s_server = nullptr;
bool s_initialized = false;
std::atomic<bool> s_enabled{false};
std::atomic<bool> s_server_running{false};
std::mutex s_lifecycle_mutex;

std::mutex s_auth_mutex;
bool s_auth_ready = false;
char s_api_key[qsee::kHttpApiKeyMaxLength + 1U] = {};
char s_expected_authorization[
    qsee::kHttpApiKeyMaxLength + kAuthorizationPrefixLength + 1U
] = {};

std::mutex s_metrics_mutex;
HttpMetricsState s_metrics{};

esp_event_handler_instance_t s_ip_event_instance = nullptr;
esp_event_handler_instance_t s_wifi_event_instance = nullptr;

void resetAuthWindowIfExpired(uint64_t now_us)
{
    if (s_metrics.auth_window_started_us == 0U ||
        now_us - s_metrics.auth_window_started_us >= kAuthFailureWindowUs) {
        s_metrics.auth_window_started_us = now_us;
        s_metrics.view.auth_failures_in_window = 0U;
    }
}

void recordRequest()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    ++s_metrics.view.request_count;
}

void recordAuthorized()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    ++s_metrics.view.authorized_count;
}

void recordUnauthorized()
{
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    resetAuthWindowIfExpired(now_us);
    ++s_metrics.view.unauthorized_count;
    if (s_metrics.view.auth_failures_in_window < 0xFFFFFFFFU) {
        ++s_metrics.view.auth_failures_in_window;
    }
}

void recordResponseFailure()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    ++s_metrics.view.response_failure_count;
}

void recordServerStart()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    ++s_metrics.view.start_count;
}

void recordServerStop()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    ++s_metrics.view.stop_count;
}

void recordServerStartFailure()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    ++s_metrics.view.start_failure_count;
}

qsee::HttpMetricsView snapshotMetrics()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    resetAuthWindowIfExpired(static_cast<uint64_t>(esp_timer_get_time()));
    return s_metrics.view;
}

bool authRateLimited()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    resetAuthWindowIfExpired(static_cast<uint64_t>(esp_timer_get_time()));
    return s_metrics.view.auth_failures_in_window >= kAuthFailureLimit;
}

esp_err_t validateApiKeyValue(const char *value)
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const std::size_t length = std::strlen(value);
    if (length == 0U || length > qsee::kHttpApiKeyMaxLength) {
        ESP_LOGW(kTag, "Settings ApiKey must contain 1..%u characters",
                 static_cast<unsigned>(qsee::kHttpApiKeyMaxLength));
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

// Replace the in-memory Bearer credential after validating its bounded size.
esp_err_t loadApiKeyValue(const char *value)
{
    const esp_err_t validation_error = validateApiKeyValue(value);
    if (validation_error == ESP_ERR_INVALID_SIZE &&
        value != nullptr && value[0] == '\0') {
        std::lock_guard<std::mutex> lock(s_auth_mutex);
        s_auth_ready = false;
        std::memset(s_api_key, 0, sizeof(s_api_key));
        std::memset(s_expected_authorization, 0, sizeof(s_expected_authorization));
        return ESP_ERR_INVALID_SIZE;
    }
    if (validation_error != ESP_OK) {
        return validation_error;
    }
    const std::size_t length = std::strlen(value);

    std::lock_guard<std::mutex> lock(s_auth_mutex);
    std::memset(s_api_key, 0, sizeof(s_api_key));
    std::memcpy(s_api_key, value, length);
    const int written = std::snprintf(
        s_expected_authorization,
        sizeof(s_expected_authorization),
        "Bearer %s",
        s_api_key
    );
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(s_expected_authorization)) {
        s_auth_ready = false;
        return ESP_ERR_INVALID_SIZE;
    }
    s_auth_ready = true;
    return ESP_OK;
}

esp_err_t loadApiKeyFromSettings()
{
    qsee::UserSettingsService &settings = qsee::UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error != ESP_OK) {
        return error;
    }

    std::string value;
    error = settings.getString(kApiKeySetting, &value);
    if (error != ESP_OK) {
        return error;
    }
    return loadApiKeyValue(value.c_str());
}

// Compare complete buffers so API-key failures do not reveal a prefix.
bool constantTimeEquals(
    const char *actual,
    std::size_t actual_length,
    const char *expected,
    std::size_t expected_length
)
{
    const std::size_t length = actual_length > expected_length ? actual_length : expected_length;
    unsigned difference = static_cast<unsigned>(actual_length ^ expected_length);
    for (std::size_t index = 0U; index < length; ++index) {
        const unsigned actual_byte = index < actual_length
            ? static_cast<unsigned>(static_cast<unsigned char>(actual[index])) : 0U;
        const unsigned expected_byte = index < expected_length
            ? static_cast<unsigned>(static_cast<unsigned char>(expected[index])) : 0U;
        difference |= actual_byte ^ expected_byte;
    }
    return difference == 0U;
}

bool headerMatches(
    httpd_req_t *request,
    const char *header_name,
    const char *expected,
    std::size_t expected_length
)
{
    const std::size_t length = httpd_req_get_hdr_value_len(request, header_name);
    if (length != expected_length || length == 0U || length >= kAuthHeaderCapacity) {
        return false;
    }
    char value[kAuthHeaderCapacity] = {};
    if (httpd_req_get_hdr_value_str(request, header_name, value, sizeof(value)) != ESP_OK) {
        return false;
    }
    return constantTimeEquals(value, length, expected, expected_length);
}

// Compare a request against a mutex-protected copy of the active credential.
bool authorized(httpd_req_t *request)
{
    char expected_authorization[
        qsee::kHttpApiKeyMaxLength + kAuthorizationPrefixLength + 1U
    ] = {};
    {
        std::lock_guard<std::mutex> lock(s_auth_mutex);
        if (!s_auth_ready) {
            return false;
        }
        std::memcpy(expected_authorization, s_expected_authorization,
                    sizeof(expected_authorization));
    }

    return headerMatches(
        request,
        "Authorization",
        expected_authorization,
        std::strlen(expected_authorization)
    );
}

esp_err_t sendError(
    httpd_req_t *request,
    const char *status,
    const char *message,
    bool challenge,
    bool retry_after
)
{
    (void)httpd_resp_set_type(request, "application/json");
    (void)httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (challenge) {
        (void)httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer");
    }
    if (retry_after) {
        (void)httpd_resp_set_hdr(request, "Retry-After", "60");
    }
    (void)httpd_resp_set_status(request, status);
    char body[160] = {};
    const int length = std::snprintf(
        body,
        sizeof(body),
        "{\"error\":\"%s\"}",
        message != nullptr ? message : "request failed"
    );
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(body)) {
        return httpd_resp_send(request, "{\"error\":\"request failed\"}", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(request, body, length);
}

// Record lifecycle failures without inventing an HTTP component snapshot.
void reportLifecycle(esp_err_t error = ESP_OK)
{
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP lifecycle error: %s", esp_err_to_name(error));
    }
}

// Authenticate data routes and serialize one immutable snapshot per request.
esp_err_t handleApiRequest(httpd_req_t *request)
{
    recordRequest();
    const char *route = static_cast<const char *>(request->user_ctx);
    if (route == nullptr) {
        return sendError(request, "500 Internal Server Error", "route is not configured", false, false);
    }

    const bool is_health = std::strcmp(route, kHealthPath) == 0;
    if (!is_health) {
        if (authRateLimited()) {
            return sendError(request, "429 Too Many Requests", "too many authentication failures", false, true);
        }
        if (!authorized(request)) {
            recordUnauthorized();
            return sendError(request, "401 Unauthorized", "authentication required", true, false);
        }
        recordAuthorized();
    }

    const qsee::DeviceSnapshot snapshot = qsee::DeviceSnapshotService::instance().snapshot();
    qsee::HttpJsonWriter response(request);
    if (is_health) {
        qsee::httpJsonWriteHealth(response, snapshot, s_server_running.load());
    } else if (std::strcmp(route, kCapabilitiesPath) == 0) {
        const uint32_t refresh_interval_ms =
            snapshot.diagnostics.full_snapshot_period.health ==
                    qsee::DeviceHealth::Ready &&
                snapshot.diagnostics.full_snapshot_period.last_error ==
                    ESP_OK &&
                snapshot.diagnostics.full_snapshot_period.data_valid
                ? snapshot.diagnostics.full_snapshot_period_ms
                : 0U;
        qsee::httpJsonWriteCapabilities(response, refresh_interval_ms);
    } else if (std::strcmp(route, kInfoPath) == 0) {
        qsee::httpJsonWriteInfo(response, snapshot, false);
    } else if (std::strcmp(route, kDiagnosticsPath) == 0) {
        qsee::httpJsonWriteInfo(response, snapshot, true);
    } else if (std::strcmp(route, kRtcPath) == 0) {
        qsee::httpJsonWriteRtc(response, snapshot);
    } else if (std::strcmp(route, kGpsPath) == 0) {
        qsee::httpJsonWriteGps(response, snapshot);
    } else if (std::strcmp(route, kStatusPath) == 0) {
        qsee::httpJsonWriteStatus(response, snapshot, snapshotMetrics(), s_server_running.load());
    } else {
        return sendError(request, "404 Not Found", "route not found", false, false);
    }

    if (!response.finish()) {
        recordResponseFailure();
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Start read-only routes; the canonical Full Snapshot worker owns acquisition.
esp_err_t startServer()
{
    std::lock_guard<std::mutex> lifecycle_lock(s_lifecycle_mutex);
    if (!s_enabled.load()) {
        reportLifecycle();
        return ESP_OK;
    }
    if (s_server != nullptr) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = qsee::kHttpServerPort;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 8;
    config.stack_size = 6144;
    config.lru_purge_enable = true;

    httpd_handle_t server = nullptr;
    esp_err_t error = httpd_start(&server, &config);
    if (error != ESP_OK) {
        recordServerStartFailure();
        reportLifecycle(error);
        ESP_LOGW(kTag, "HTTP server start failed: %s", esp_err_to_name(error));
        return error;
    }

    const httpd_uri_t routes[] = {
        {.uri = kHealthPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kHealthPath)},
        {.uri = kCapabilitiesPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kCapabilitiesPath)},
        {.uri = kInfoPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kInfoPath)},
        {.uri = kDiagnosticsPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kDiagnosticsPath)},
        {.uri = kRtcPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kRtcPath)},
        {.uri = kGpsPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kGpsPath)},
        {.uri = kStatusPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kStatusPath)},
    };
    for (const httpd_uri_t &route : routes) {
        error = httpd_register_uri_handler(server, &route);
        if (error != ESP_OK) {
            (void)httpd_stop(server);
            recordServerStartFailure();
            reportLifecycle(error);
            ESP_LOGW(kTag, "HTTP route registration failed: %s", esp_err_to_name(error));
            return error;
        }
    }

    s_server = server;
    s_server_running.store(true);
    recordServerStart();
    reportLifecycle();
    ESP_LOGI(kTag, "HTTP API online on port %u", static_cast<unsigned>(qsee::kHttpServerPort));
    return ESP_OK;
}

esp_err_t stopServer()
{
    std::lock_guard<std::mutex> lifecycle_lock(s_lifecycle_mutex);
    if (s_server == nullptr) {
        s_server_running.store(false);
        reportLifecycle();
        return ESP_OK;
    }
    s_server_running.store(false);
    const esp_err_t error = httpd_stop(s_server);
    s_server = nullptr;
    recordServerStop();
    reportLifecycle(error);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP server stop failed: %s", esp_err_to_name(error));
    } else {
        ESP_LOGI(kTag, "HTTP API offline until the next Wi-Fi lease");
    }
    return error;
}

// Bind server lifetime to acquisition and loss of a usable station IP.
void networkEventHandler(void *argument, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        (void)startServer();
        return;
    }
    if ((event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) ||
        (event_base == WIFI_EVENT &&
         (event_id == WIFI_EVENT_STA_DISCONNECTED || event_id == WIFI_EVENT_STA_STOP))) {
        (void)stopServer();
    }
}

}  // namespace

namespace qsee {

esp_err_t httpServerInit()
{
    if (s_initialized) {
        return ESP_OK;
    }

    UserSettingsService &settings = UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API settings initialization failed: %s", esp_err_to_name(error));
        return error;
    }

    bool enabled = false;
    error = settings.getBool(kEnabledSetting, &enabled);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP preference initialization failed: %s", esp_err_to_name(error));
        return error;
    }
    s_enabled.store(enabled);

    error = loadApiKeyFromSettings();
    if (error == ESP_ERR_INVALID_SIZE) {
        ESP_LOGI(kTag, "HTTP API waiting for an ApiKey from Settings");
    } else if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API key initialization failed: %s", esp_err_to_name(error));
        return error;
    }

    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        reportLifecycle(error);
        ESP_LOGW(
            kTag,
            "HTTP event loop initialization failed: %s",
            esp_err_to_name(error)
        );
        return error;
    }

    error = esp_event_handler_instance_register(
        IP_EVENT,
        ESP_EVENT_ANY_ID,
        networkEventHandler,
        nullptr,
        &s_ip_event_instance
    );
    if (error != ESP_OK) {
        return error;
    }
    error = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        networkEventHandler,
        nullptr,
        &s_wifi_event_instance
    );
    if (error != ESP_OK) {
        (void)esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_event_instance);
        s_ip_event_instance = nullptr;
        return error;
    }

    s_initialized = true;
    reportLifecycle();
    if (!enabled) {
        ESP_LOGI(kTag, "HTTP API disabled by saved preference");
        return ESP_OK;
    }

    const esp_err_t wifi_error = qsee_wifi_init();
    if (wifi_error != ESP_OK) {
        reportLifecycle(wifi_error);
        ESP_LOGW(kTag, "HTTP API requires Wi-Fi initialization: %s", esp_err_to_name(wifi_error));
        return wifi_error;
    }

    qsee_wifi_status_t wifi = {};
    qsee_wifi_get_status(&wifi);
    if (wifi.ip_acquired) {
        (void)startServer();
    }
    ESP_LOGI(kTag, "HTTP API initialized; health probe is public and data routes require API key");
    return ESP_OK;
}

// Persist one user choice, then apply only the matching server transition.
esp_err_t httpServerSetEnabled(bool enabled)
{
    UserSettingsService &settings = UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error != ESP_OK) {
        return error;
    }
    error = settings.setBool(kEnabledSetting, enabled);
    if (error != ESP_OK) {
        return error;
    }

    s_enabled.store(enabled);
    if (!s_initialized) {
        return httpServerInit();
    }
    if (!enabled) {
        return stopServer();
    }

    error = qsee_wifi_init();
    if (error != ESP_OK) {
        reportLifecycle(error);
        return error;
    }
    qsee_wifi_status_t wifi = {};
    qsee_wifi_get_status(&wifi);
    if (wifi.ip_acquired) {
        return startServer();
    }
    reportLifecycle();
    ESP_LOGI(kTag, "HTTP API enabled; waiting for a Wi-Fi address");
    return ESP_OK;
}

// Persist first so a failed write never leaves a new in-memory key active.
esp_err_t httpServerSetApiKey(const char *api_key)
{
    const esp_err_t validation_error = validateApiKeyValue(api_key);
    if (validation_error != ESP_OK) {
        return validation_error;
    }

    UserSettingsService &settings = UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error != ESP_OK) {
        return error;
    }
    error = settings.setString(kApiKeySetting, api_key);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API key persistence failed: %s", esp_err_to_name(error));
        return error;
    }
    error = loadApiKeyValue(api_key);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API key activation failed: %s", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(kTag, "HTTP API key updated without changing server lifecycle");
    return ESP_OK;
}

}  // namespace qsee
