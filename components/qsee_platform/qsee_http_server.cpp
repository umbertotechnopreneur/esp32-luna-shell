/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * The HTTP API is intentionally read-only. Transport, authentication and
 * lifecycle handling live here; JSON serialization is kept in
 * qsee_http_json.cpp.
 */

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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_http_json.hpp"
#include "qsee_platform.h"
#include "qsee_settings.hpp"

namespace {

constexpr const char *kTag = "qsee_http";
constexpr const char *kApiKeySetting = "ApiKey";
constexpr std::size_t kAuthorizationPrefixLength = 7U;

constexpr uint32_t kSnapshotRefreshIntervalMs = 5000U;
constexpr uint64_t kAuthFailureWindowUs = 60ULL * 1000000ULL;
constexpr uint32_t kAuthFailureLimit = 20U;
constexpr std::size_t kAuthHeaderCapacity = 96U;
constexpr std::size_t kSnapshotTaskStackWords = 3072U;

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
std::atomic<bool> s_server_running{false};

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

StaticTask_t s_snapshot_task_control{};
StackType_t s_snapshot_task_stack[kSnapshotTaskStackWords] = {};
TaskHandle_t s_snapshot_task = nullptr;
bool s_snapshot_task_started = false;

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

esp_err_t loadApiKeyValue(const char *value)
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
    error = settings.getString(kApiKeySetting, nullptr, &value);
    if (error != ESP_OK) {
        return error;
    }
    return loadApiKeyValue(value.c_str());
}

void settingsChanged(const char *key, const char *value, void *)
{
    if (key == nullptr || std::strcmp(key, kApiKeySetting) != 0) {
        return;
    }
    const esp_err_t error = loadApiKeyValue(value);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "New Settings ApiKey rejected: %s", esp_err_to_name(error));
    }
}

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

void snapshotRefreshTask(void *)
{
    for (;;) {
        if (s_server_running.load()) {
            (void)qsee_hardware_refresh("http-api");
        }
        vTaskDelay(pdMS_TO_TICKS(kSnapshotRefreshIntervalMs));
    }
}

bool startSnapshotRefreshTask()
{
    if (s_snapshot_task_started) {
        return true;
    }
    s_snapshot_task = xTaskCreateStatic(
        snapshotRefreshTask,
        "qsee_http_refresh",
        kSnapshotTaskStackWords,
        nullptr,
        3,
        s_snapshot_task_stack,
        &s_snapshot_task_control
    );
    s_snapshot_task_started = s_snapshot_task != nullptr;
    if (!s_snapshot_task_started) {
        ESP_LOGW(kTag, "Snapshot refresh task could not be created");
    }
    return s_snapshot_task_started;
}

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
        qsee::httpJsonWriteCapabilities(response, kSnapshotRefreshIntervalMs);
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

esp_err_t startServer()
{
    if (s_server != nullptr) {
        return ESP_OK;
    }
    if (!startSnapshotRefreshTask()) {
        recordServerStartFailure();
        return ESP_ERR_NO_MEM;
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
            ESP_LOGW(kTag, "HTTP route registration failed: %s", esp_err_to_name(error));
            return error;
        }
    }

    s_server = server;
    s_server_running.store(true);
    recordServerStart();
    ESP_LOGI(kTag, "HTTP API online on port %u", static_cast<unsigned>(qsee::kHttpServerPort));
    return ESP_OK;
}

void stopServer()
{
    if (s_server == nullptr) {
        return;
    }
    s_server_running.store(false);
    const esp_err_t error = httpd_stop(s_server);
    s_server = nullptr;
    recordServerStop();
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP server stop failed: %s", esp_err_to_name(error));
    } else {
        ESP_LOGI(kTag, "HTTP API offline until the next Wi-Fi lease");
    }
}

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
        stopServer();
    }
}

}  // namespace

namespace qsee {

esp_err_t httpServerInit()
{
    if (s_initialized) {
        return ESP_OK;
    }

    const esp_err_t wifi_error = qsee_wifi_init();
    if (wifi_error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API requires Wi-Fi initialization: %s", esp_err_to_name(wifi_error));
        return wifi_error;
    }

    UserSettingsService &settings = UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API settings initialization failed: %s", esp_err_to_name(error));
        return error;
    }

    error = settings.setStringChangeObserver(settingsChanged, nullptr);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API key observer registration failed: %s", esp_err_to_name(error));
        return error;
    }

    error = loadApiKeyFromSettings();
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API key initialization failed: %s", esp_err_to_name(error));
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
    qsee_wifi_status_t wifi = {};
    qsee_wifi_get_status(&wifi);
    if (wifi.ip_acquired) {
        (void)startServer();
    }
    ESP_LOGI(kTag, "HTTP API initialized; health probe is public and data routes require API key");
    return ESP_OK;
}

}  // namespace qsee
