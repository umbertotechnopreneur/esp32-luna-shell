/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

// JSON builders for snapshot and metrics responses.
// Keeps transport formatting separate from HTTP routing.

#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_http_server.h"
#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {

struct HttpMetricsView {
    uint32_t request_count = 0;
    uint32_t authorized_count = 0;
    uint32_t unauthorized_count = 0;
    uint32_t response_failure_count = 0;
    uint32_t start_count = 0;
    uint32_t stop_count = 0;
    uint32_t start_failure_count = 0;
    uint32_t stop_failure_count = 0;
    uint32_t event_lifecycle_failure_count = 0;
    esp_err_t last_lifecycle_error = ESP_OK;
    uint32_t auth_failures_in_window = 0;
};

/**
 * One coherent, lock-free snapshot of HTTP server ownership.
 *
 * `running` is conservative: it remains true while an ESP-IDF HTTP task may
 * still be serving after a failed stop. `accepting_requests` is stricter and
 * becomes true only after every route has been registered and the desired
 * network lifecycle is Running.
 */
struct HttpLifecycleView {
    bool running = false;
    bool cleanup_required = false;
    bool handle_retained = false;
    bool accepting_requests = false;
};

/** Bounded chunked JSON writer used by the read-only HTTP response paths. */
class HttpJsonWriter final {
public:
    explicit HttpJsonWriter(httpd_req_t *request);

    bool raw(const char *text);
    bool format(const char *format_string, ...);
    /** Append a null-safe JSON string with control-character escaping. */
    bool quoted(const char *value);
    bool boolean(bool value);
    /** Flush buffered data and terminate the chunked HTTP response. */
    esp_err_t finish();

private:
    bool append(char value);
    bool flush();

    httpd_req_t *_request;
    char _buffer[512] = {};
    std::size_t _used = 0;
    bool _failed = false;
    esp_err_t _last_error = ESP_OK;
};

void httpJsonWriteHealth(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    const HttpLifecycleView &lifecycle
);

/** Serialize the authenticated, network-redacted operational view. */
void httpJsonWriteInfo(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot
);

/** Serialize the complete authenticated DeviceSnapshot diagnostic view. */
void httpJsonWriteDiagnostics(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot
);

void httpJsonWriteRtc(HttpJsonWriter &json, const DeviceSnapshot &snapshot);
void httpJsonWriteGps(HttpJsonWriter &json, const DeviceSnapshot &snapshot);

void httpJsonWriteStatus(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    const HttpMetricsView &metrics,
    const HttpLifecycleView &lifecycle
);

void httpJsonWriteCapabilities(HttpJsonWriter &json, uint32_t refresh_interval_ms);

}  // namespace LUNASHELL
