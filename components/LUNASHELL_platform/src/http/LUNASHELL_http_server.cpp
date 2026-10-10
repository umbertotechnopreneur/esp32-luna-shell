/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * The HTTP API is intentionally read-only. Transport, authentication and
 * lifecycle handling live here; JSON serialization is kept in
 * LUNASHELL_http_json.cpp.
 */

// Read-only HTTP server for device status, diagnostics, and capabilities.
// Handles auth, request routing, and snapshot refresh orchestration.

#include "LUNASHELL_http_server.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_http_json.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_settings.hpp"

namespace {

constexpr const char *kTag = "LUNASHELL_http";
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
    LUNASHELL::HttpMetricsView view{};
    uint64_t auth_window_started_us = 0U;
};

httpd_handle_t s_server = nullptr;
enum class ServerLifecycleState : uint8_t {
    Stopped,
    Running,
    CleanupRequired,
};

ServerLifecycleState s_server_state = ServerLifecycleState::Stopped;
std::atomic<bool> s_initialized{false};
std::atomic<bool> s_enabled{false};
constexpr uint32_t kLifecycleRunning = 1U << 0;
constexpr uint32_t kLifecycleCleanupRequired = 1U << 1;
constexpr uint32_t kLifecycleHandleRetained = 1U << 2;
constexpr uint32_t kLifecycleAcceptingRequests = 1U << 3;
std::atomic<uint32_t> s_lifecycle_flags{0U};
// HTTP settings reads/writes and initialization decisions share this lock. It
// is always acquired before s_lifecycle_mutex when both are needed.
std::mutex s_initialization_mutex;
std::mutex s_lifecycle_mutex;
// The event loop owns these observations under s_lifecycle_mutex. Events may
// arrive during initialization, but cannot act until infrastructure is ready.
bool s_infrastructure_ready = false;
bool s_network_event_seen = false;
bool s_network_has_ip = false;

std::mutex s_auth_mutex;
bool s_auth_ready = false;
char s_api_key[LUNASHELL::kHttpApiKeyMaxLength + 1U] = {};
char s_expected_authorization[
    LUNASHELL::kHttpApiKeyMaxLength + kAuthorizationPrefixLength + 1U
] = {};

std::mutex s_metrics_mutex;
HttpMetricsState s_metrics{};

// HTTP owns one full response buffer in PSRAM. The lock covers canonical copy
// and the entire serialization/send; no handler can observe another request's data.
std::mutex s_response_snapshot_mutex;
alignas(LUNASHELL::DeviceSnapshot) EXT_RAM_BSS_ATTR
uint8_t s_response_snapshot_storage[sizeof(LUNASHELL::DeviceSnapshot)]{};
LUNASHELL::DeviceSnapshot &s_response_snapshot =
    *new (s_response_snapshot_storage) LUNASHELL::DeviceSnapshot{};

esp_event_handler_instance_t s_ip_event_instance = nullptr;
esp_event_handler_instance_t s_wifi_event_instance = nullptr;

// Publish every externally observable lifecycle property in one atomic word.
// The caller owns s_lifecycle_mutex and has already committed s_server and
// s_server_state. A retained handle is conservatively reported as running when
// ESP-IDF did not confirm that its server task stopped.
void publishLifecycleLocked(bool serving_possible, bool accepting_requests)
{
    uint32_t flags = 0U;
    if (serving_possible) {
        flags |= kLifecycleRunning;
    }
    if (s_server_state == ServerLifecycleState::CleanupRequired) {
        flags |= kLifecycleCleanupRequired;
    }
    if (s_server != nullptr) {
        flags |= kLifecycleHandleRetained;
    }
    if (accepting_requests) {
        flags |= kLifecycleAcceptingRequests;
    }
    s_lifecycle_flags.store(flags, std::memory_order_release);
}

LUNASHELL::HttpLifecycleView snapshotLifecycle()
{
    const uint32_t flags =
        s_lifecycle_flags.load(std::memory_order_acquire);
    return {
        .running = (flags & kLifecycleRunning) != 0U,
        .cleanup_required =
            (flags & kLifecycleCleanupRequired) != 0U,
        .handle_retained = (flags & kLifecycleHandleRetained) != 0U,
        .accepting_requests =
            (flags & kLifecycleAcceptingRequests) != 0U,
    };
}

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

void recordServerStopFailure()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    ++s_metrics.view.stop_failure_count;
}

void recordEventLifecycleFailure()
{
    std::lock_guard<std::mutex> lock(s_metrics_mutex);
    ++s_metrics.view.event_lifecycle_failure_count;
}

LUNASHELL::HttpMetricsView snapshotMetrics()
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
    if (length == 0U || length > LUNASHELL::kHttpApiKeyMaxLength) {
        ESP_LOGW(kTag, "Settings ApiKey must contain 1..%u characters",
                 static_cast<unsigned>(LUNASHELL::kHttpApiKeyMaxLength));
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
    LUNASHELL::UserSettingsService &settings = LUNASHELL::UserSettingsService::instance();
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
        LUNASHELL::kHttpApiKeyMaxLength + kAuthorizationPrefixLength + 1U
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

esp_err_t prepareJsonResponse(
    httpd_req_t *request,
    const char *status = nullptr
)
{
    esp_err_t error = httpd_resp_set_type(request, "application/json");
    if (error == ESP_OK) {
        error = httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    }
    if (error == ESP_OK && status != nullptr) {
        error = httpd_resp_set_status(request, status);
    }
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "HTTP response metadata failed (%s)",
            esp_err_to_name(error)
        );
    }
    return error;
}

esp_err_t sendError(
    httpd_req_t *request,
    const char *status,
    const char *message,
    bool challenge,
    bool retry_after
)
{
    esp_err_t error = prepareJsonResponse(request, status);
    if (challenge) {
        if (error == ESP_OK) {
            error = httpd_resp_set_hdr(
                request,
                "WWW-Authenticate",
                "Bearer"
            );
        }
    }
    if (retry_after) {
        if (error == ESP_OK) {
            error = httpd_resp_set_hdr(request, "Retry-After", "60");
        }
    }
    if (error != ESP_OK) {
        recordResponseFailure();
        ESP_LOGE(
            kTag,
            "HTTP error response setup failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    char body[160] = {};
    const int length = std::snprintf(
        body,
        sizeof(body),
        "{\"error\":\"%s\"}",
        message != nullptr ? message : "request failed"
    );
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(body)) {
        error = httpd_resp_send(
            request,
            "{\"error\":\"request failed\"}",
            HTTPD_RESP_USE_STRLEN
        );
    } else {
        error = httpd_resp_send(request, body, length);
    }
    if (error != ESP_OK) {
        recordResponseFailure();
        ESP_LOGE(
            kTag,
            "HTTP error response send failed (%s)",
            esp_err_to_name(error)
        );
    }
    return error;
}

// Record lifecycle failures without inventing an HTTP component snapshot.
void reportLifecycle(esp_err_t error = ESP_OK)
{
    if (error != ESP_OK) {
        {
            std::lock_guard<std::mutex> lock(s_metrics_mutex);
            s_metrics.view.last_lifecycle_error = error;
        }
        ESP_LOGW(kTag, "HTTP lifecycle error: %s", esp_err_to_name(error));
    }
}

// context: Real HTTP request owned by this server task.
// data: Borrowed bounded serialized chunk; null only on explicit termination.
// size: Exact byte count; no driver-side inference or copied snapshot.
// final: True to finish the HTTP chunked transfer.
// Exception: Returns the original HTTP transport error.
esp_err_t writeHttpJsonChunk(void *context, const char *data, std::size_t size, bool final)
{
    return httpd_resp_send_chunk(static_cast<httpd_req_t *>(context),
        final ? nullptr : data, final ? 0 : size);
}

// request: HTTP owner request; protected routes authenticate before reading canonical data.
// Exception: Returns original canonical read or response errors; snapshot failures send HTTP 503.
esp_err_t handleApiRequest(httpd_req_t *request)
{
    recordRequest();
    const char *route = static_cast<const char *>(request->user_ctx);
    if (route == nullptr) {
        return sendError(request, "500 Internal Server Error", "route is not configured", false, false);
    }

    const bool is_health = std::strcmp(route, kHealthPath) == 0;
    const bool desired_enabled =
        s_enabled.load(std::memory_order_acquire);
    LUNASHELL::HttpLifecycleView lifecycle = snapshotLifecycle();
    // The persisted preference is committed before reconciliation starts. Fold
    // it into every request so disabling fails closed even while this handler
    // still races the old Running lifecycle snapshot. The redundant lifecycle
    // predicates also keep a malformed/cleanup state from ever accepting data.
    lifecycle.accepting_requests =
        desired_enabled &&
        lifecycle.running &&
        !lifecycle.cleanup_required &&
        lifecycle.handle_retained &&
        lifecycle.accepting_requests;
    if (!lifecycle.accepting_requests && !is_health) {
        return sendError(
            request,
            "503 Service Unavailable",
            "server lifecycle is not accepting requests",
            false,
            true
        );
    }
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

    std::lock_guard<std::mutex> snapshot_lock(s_response_snapshot_mutex);
    const esp_err_t snapshot_error =
        LUNASHELL::DeviceSnapshotService::instance().readSnapshot(s_response_snapshot);
    if (snapshot_error != ESP_OK) {
        recordResponseFailure();
        ESP_LOGE(kTag, "HTTP canonical snapshot read failed (%s)", esp_err_to_name(snapshot_error));
        // esp_err_to_name produces bounded ASCII tokens, safe in this JSON message.
        const esp_err_t send_error = sendError(request, "503 Service Unavailable",
            esp_err_to_name(snapshot_error), false, true);
        if (send_error != ESP_OK) {
            ESP_LOGE(kTag, "HTTP snapshot error response failed (%s); read cause remains %s",
                     esp_err_to_name(send_error), esp_err_to_name(snapshot_error));
        }
        return snapshot_error;
    }
    const LUNASHELL::DeviceSnapshot &snapshot = s_response_snapshot;
    const esp_err_t response_setup_error = prepareJsonResponse(
        request,
        is_health && !lifecycle.accepting_requests
            ? "503 Service Unavailable"
            : nullptr
    );
    if (response_setup_error != ESP_OK) {
        recordResponseFailure();
        return response_setup_error;
    }
    LUNASHELL::HttpJsonWriter response({request, writeHttpJsonChunk}, esp_timer_get_time());
    if (is_health) {
        LUNASHELL::HttpLivenessView liveness;
        liveness.query_error = LUNASHELL_liveness_get_status(&liveness.status);
        if (liveness.query_error != ESP_OK) {
            ESP_LOGE(kTag, "HTTP liveness read failed (%s)", esp_err_to_name(liveness.query_error));
        }
        liveness.last_progress_name = LUNASHELL_liveness_point_name(liveness.status.last_progress_point);
        liveness.previous_timeout_name = LUNASHELL_liveness_point_name(liveness.status.previous_timeout.last_progress_point);
        LUNASHELL::httpJsonWriteHealth(response, snapshot, lifecycle, liveness);
    } else if (std::strcmp(route, kCapabilitiesPath) == 0) {
        const uint32_t refresh_interval_ms =
            snapshot.diagnostics.full_snapshot_period.health ==
                    LUNASHELL::DeviceHealth::Ready &&
                snapshot.diagnostics.full_snapshot_period.last_error ==
                    ESP_OK &&
                snapshot.diagnostics.full_snapshot_period.data_valid
                ? snapshot.diagnostics.full_snapshot_period_ms
                : 0U;
        LUNASHELL::httpJsonWriteCapabilities(response, refresh_interval_ms);
    } else if (std::strcmp(route, kInfoPath) == 0) {
        LUNASHELL::httpJsonWriteInfo(response, snapshot);
    } else if (std::strcmp(route, kDiagnosticsPath) == 0) {
        LUNASHELL::httpJsonWriteDiagnostics(response, snapshot);
    } else if (std::strcmp(route, kRtcPath) == 0) {
        LUNASHELL::httpJsonWriteRtc(response, snapshot);
    } else if (std::strcmp(route, kGpsPath) == 0) {
        LUNASHELL::httpJsonWriteGps(response, snapshot);
    } else if (std::strcmp(route, kStatusPath) == 0) {
        LUNASHELL::httpJsonWriteStatus(
            response,
            snapshot,
            snapshotMetrics(),
            lifecycle
        );
    } else {
        return sendError(request, "404 Not Found", "route not found", false, false);
    }

    const esp_err_t response_error = response.finish();
    if (response_error != ESP_OK) {
        recordResponseFailure();
        return response_error;
    }
    return ESP_OK;
}

// Start read-only routes; the canonical Full Snapshot worker owns acquisition.
esp_err_t startServerLocked()
{
    if (s_server_state == ServerLifecycleState::Running) {
        if (s_server == nullptr) {
            const esp_err_t error = ESP_ERR_INVALID_STATE;
            s_server_state = ServerLifecycleState::CleanupRequired;
            publishLifecycleLocked(false, false);
            recordServerStartFailure();
            reportLifecycle(error);
            ESP_LOGE(kTag, "HTTP running state has no server handle");
            return error;
        }
        publishLifecycleLocked(true, true);
        return ESP_OK;
    }
    if (s_server != nullptr) {
        publishLifecycleLocked(true, false);
        const esp_err_t cleanup_error = httpd_stop(s_server);
        if (cleanup_error != ESP_OK) {
            s_server_state = ServerLifecycleState::CleanupRequired;
            publishLifecycleLocked(true, false);
            recordServerStopFailure();
            reportLifecycle(cleanup_error);
            ESP_LOGE(
                kTag,
                "HTTP retained-server cleanup failed before start (%s); "
                "handle retained",
                esp_err_to_name(cleanup_error)
            );
            return cleanup_error;
        }
        s_server = nullptr;
        s_server_state = ServerLifecycleState::Stopped;
        publishLifecycleLocked(false, false);
        recordServerStop();
        ESP_LOGI(kTag, "HTTP retained server cleaned up before restart");
    } else if (s_server_state != ServerLifecycleState::Stopped) {
        const esp_err_t error = ESP_ERR_INVALID_STATE;
        s_server_state = ServerLifecycleState::CleanupRequired;
        publishLifecycleLocked(false, false);
        recordServerStartFailure();
        reportLifecycle(error);
        ESP_LOGE(kTag, "HTTP lifecycle state requires cleanup but has no handle");
        return error;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = LUNASHELL::kHttpServerPort;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 8;
    config.stack_size = 6144;
    config.lru_purge_enable = true;

    httpd_handle_t server = nullptr;
    esp_err_t error = httpd_start(&server, &config);
    if (error != ESP_OK) {
        publishLifecycleLocked(false, false);
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
            recordServerStartFailure();
            reportLifecycle(error);
            ESP_LOGE(
                kTag,
                "HTTP route registration failed: %s",
                esp_err_to_name(error)
            );
            const esp_err_t cleanup_error = httpd_stop(server);
            if (cleanup_error != ESP_OK) {
                s_server = server;
                s_server_state = ServerLifecycleState::CleanupRequired;
                publishLifecycleLocked(true, false);
                recordServerStopFailure();
                reportLifecycle(cleanup_error);
                ESP_LOGE(
                    kTag,
                    "HTTP failed-start cleanup failed (%s); handle retained",
                    esp_err_to_name(cleanup_error)
                );
            }
            return error;
        }
    }

    s_server = server;
    s_server_state = ServerLifecycleState::Running;
    publishLifecycleLocked(true, true);
    recordServerStart();
    reportLifecycle();
    ESP_LOGI(kTag, "HTTP API online on port %u", static_cast<unsigned>(LUNASHELL::kHttpServerPort));
    return ESP_OK;
}

esp_err_t stopServerLocked()
{
    if (s_server == nullptr) {
        if (s_server_state != ServerLifecycleState::Stopped) {
            const esp_err_t error = ESP_ERR_INVALID_STATE;
            s_server_state = ServerLifecycleState::CleanupRequired;
            publishLifecycleLocked(false, false);
            recordServerStopFailure();
            reportLifecycle(error);
            ESP_LOGE(kTag, "HTTP stop cannot reconcile a missing server handle");
            return error;
        }
        publishLifecycleLocked(false, false);
        reportLifecycle();
        return ESP_OK;
    }
    // Stop accepting protected data immediately, but keep reporting that the
    // ESP-IDF server may be serving until httpd_stop confirms task teardown.
    publishLifecycleLocked(true, false);
    const esp_err_t error = httpd_stop(s_server);
    if (error != ESP_OK) {
        s_server_state = ServerLifecycleState::CleanupRequired;
        publishLifecycleLocked(true, false);
        recordServerStopFailure();
        reportLifecycle(error);
        ESP_LOGE(
            kTag,
            "HTTP server stop failed (%s); handle retained for reconciliation",
            esp_err_to_name(error)
        );
        return error;
    }
    s_server = nullptr;
    s_server_state = ServerLifecycleState::Stopped;
    publishLifecycleLocked(false, false);
    recordServerStop();
    reportLifecycle();
    ESP_LOGI(kTag, "HTTP API offline until the next Wi-Fi lease");
    return ESP_OK;
}

// Reconcile desired availability with every owned handle/state combination.
// The caller must hold s_lifecycle_mutex.
esp_err_t reconcileServerLocked(bool should_run)
{
    return should_run ? startServerLocked() : stopServerLocked();
}

esp_err_t reconcileServer(bool should_run)
{
    std::lock_guard<std::mutex> lifecycle_lock(s_lifecycle_mutex);
    return reconcileServerLocked(should_run);
}

esp_err_t reconcileObservedNetworkState(
    bool observed_has_ip,
    bool commit_infrastructure
)
{
    std::lock_guard<std::mutex> lifecycle_lock(s_lifecycle_mutex);
    // Once an event has been seen it is authoritative: this HTTP handler may
    // run before the Wi-Fi owner's handler updates its status snapshot.
    if (!s_network_event_seen) {
        s_network_has_ip = observed_has_ip;
    }
    if (commit_infrastructure) {
        s_infrastructure_ready = true;
        s_initialized.store(true, std::memory_order_release);
    }
    if (!s_infrastructure_ready ||
        !s_initialized.load(std::memory_order_acquire)) {
        const esp_err_t error = ESP_ERR_INVALID_STATE;
        reportLifecycle(error);
        ESP_LOGE(kTag, "HTTP reconciliation requested before infrastructure commit");
        return error;
    }
    return reconcileServerLocked(
        s_enabled.load(std::memory_order_acquire) && s_network_has_ip
    );
}

// Bind server lifetime to acquisition and loss of a usable station IP.
void networkEventHandler(void *argument, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;
    const bool got_ip =
        event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP;
    const bool lost_ip =
        event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP;
    const bool station_unavailable =
        event_base == WIFI_EVENT &&
        (event_id == WIFI_EVENT_STA_DISCONNECTED ||
         event_id == WIFI_EVENT_STA_STOP);
    if (!got_ip && !lost_ip && !station_unavailable) {
        return;
    }
    const bool has_ip = got_ip;

    esp_err_t error = ESP_OK;
    bool deferred = false;
    {
        std::lock_guard<std::mutex> lifecycle_lock(s_lifecycle_mutex);
        s_network_event_seen = true;
        s_network_has_ip = has_ip;
        deferred =
            !s_infrastructure_ready ||
            !s_initialized.load(std::memory_order_acquire);
        if (!deferred) {
            error = reconcileServerLocked(
                s_enabled.load(std::memory_order_acquire) && s_network_has_ip
            );
        }
    }
    if (deferred) {
        ESP_LOGD(
            kTag,
            "HTTP network event %ld recorded before infrastructure commit",
            static_cast<long>(event_id)
        );
        return;
    }
    if (error != ESP_OK) {
        recordEventLifecycleFailure();
        ESP_LOGE(
            kTag,
            "HTTP reconcile failed for network event %ld (%s)",
            static_cast<long>(event_id),
            esp_err_to_name(error)
        );
    }
}

}  // namespace

namespace LUNASHELL {

namespace {

esp_err_t httpServerInitLocked()
{
    if (s_initialized.load(std::memory_order_acquire)) {
        LUNASHELL_wifi_status_t wifi = {};
        LUNASHELL_wifi_get_status(&wifi);
        return reconcileObservedNetworkState(wifi.ip_acquired, false);
    }

    UserSettingsService &settings = UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API settings initialization failed: %s", esp_err_to_name(error));
        return error;
    }

    bool enabled = false;
    error = settings.getBool(kEnabledSetting, &enabled);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "First boot: missing %s, persisting default false", kEnabledSetting);
        error = settings.setBool(kEnabledSetting, false);
    }
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP preference initialization failed: %s", esp_err_to_name(error));
        return error;
    }
    s_enabled.store(enabled);

    error = loadApiKeyFromSettings();
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "HTTP API waiting for an explicitly configured ApiKey");
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

    if (enabled) {
        const esp_err_t wifi_error = LUNASHELL_wifi_init();
        if (wifi_error != ESP_OK) {
            reportLifecycle(wifi_error);
            ESP_LOGW(
                kTag,
                "HTTP API requires Wi-Fi initialization: %s",
                esp_err_to_name(wifi_error)
            );
            return wifi_error;
        }
    }

    bool ip_handler_registered_now = false;
    if (s_ip_event_instance == nullptr) {
        error = esp_event_handler_instance_register(
            IP_EVENT,
            ESP_EVENT_ANY_ID,
            networkEventHandler,
            nullptr,
            &s_ip_event_instance
        );
        if (error != ESP_OK) {
            reportLifecycle(error);
            ESP_LOGE(
                kTag,
                "HTTP IP event handler registration failed (%s)",
                esp_err_to_name(error)
            );
            return error;
        }
        ip_handler_registered_now = true;
    }
    if (s_wifi_event_instance == nullptr) {
        error = esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            networkEventHandler,
            nullptr,
            &s_wifi_event_instance
        );
        if (error != ESP_OK) {
            reportLifecycle(error);
            ESP_LOGE(
                kTag,
                "HTTP Wi-Fi event handler registration failed (%s)",
                esp_err_to_name(error)
            );
            if (ip_handler_registered_now) {
                const esp_err_t rollback_error =
                    esp_event_handler_instance_unregister(
                        IP_EVENT,
                        ESP_EVENT_ANY_ID,
                        s_ip_event_instance
                    );
                if (rollback_error == ESP_OK) {
                    s_ip_event_instance = nullptr;
                } else {
                    reportLifecycle(rollback_error);
                    ESP_LOGE(
                        kTag,
                        "HTTP IP event handler rollback failed (%s); "
                        "instance retained",
                        esp_err_to_name(rollback_error)
                    );
                }
            }
            return error;
        }
    }

    LUNASHELL_wifi_status_t wifi = {};
    LUNASHELL_wifi_get_status(&wifi);
    error = reconcileObservedNetworkState(wifi.ip_acquired, true);
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "HTTP API initialization reconciliation failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    if (!enabled) {
        ESP_LOGI(kTag, "HTTP API disabled by saved preference");
    } else {
        ESP_LOGI(
            kTag,
            "HTTP API initialized; health probe is public and data routes "
            "require API key"
        );
    }
    return ESP_OK;
}

}  // namespace

esp_err_t httpServerInit()
{
    std::lock_guard<std::mutex> initialization_lock(s_initialization_mutex);
    return httpServerInitLocked();
}

// enabled: Explicit user choice, persisted before applying the server transition.
esp_err_t httpServerSetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> initialization_lock(s_initialization_mutex);
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
    if (!s_initialized.load(std::memory_order_acquire)) {
        return httpServerInitLocked();
    }
    if (!enabled) {
        return reconcileServer(false);
    }

    error = LUNASHELL_wifi_init();
    if (error != ESP_OK) {
        reportLifecycle(error);
        return error;
    }
    LUNASHELL_wifi_status_t wifi = {};
    LUNASHELL_wifi_get_status(&wifi);
    error = reconcileObservedNetworkState(wifi.ip_acquired, false);
    if (error != ESP_OK) {
        return error;
    }
    ESP_LOGI(kTag, "HTTP API enabled; lifecycle reconciled with Wi-Fi state");
    return ESP_OK;
}

// api_key: Explicit bounded Bearer secret; persisted before activation and never logged.
esp_err_t httpServerSetApiKey(const char *api_key)
{
    const esp_err_t validation_error = validateApiKeyValue(api_key);
    if (validation_error != ESP_OK) {
        return validation_error;
    }

    std::lock_guard<std::mutex> initialization_lock(s_initialization_mutex);
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

}  // namespace LUNASHELL
