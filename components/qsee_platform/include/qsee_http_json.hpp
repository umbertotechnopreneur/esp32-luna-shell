/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_http_server.h"
#include "qsee_device_snapshot.hpp"

namespace qsee {

struct HttpMetricsView {
    uint32_t request_count = 0;
    uint32_t authorized_count = 0;
    uint32_t unauthorized_count = 0;
    uint32_t response_failure_count = 0;
    uint32_t start_count = 0;
    uint32_t stop_count = 0;
    uint32_t start_failure_count = 0;
    uint32_t auth_failures_in_window = 0;
};

/** Bounded chunked JSON writer used by the read-only HTTP response paths. */
class HttpJsonWriter final {
public:
    explicit HttpJsonWriter(httpd_req_t *request);

    bool raw(const char *text);
    bool format(const char *format_string, ...);
    bool quoted(const char *value);
    bool boolean(bool value);
    bool finish();

private:
    bool append(char value);
    bool flush();

    httpd_req_t *_request;
    char _buffer[512] = {};
    std::size_t _used = 0;
    bool _failed = false;
};

void httpJsonWriteHealth(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    bool server_running
);

void httpJsonWriteInfo(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    bool include_sensitive
);

void httpJsonWriteRtc(HttpJsonWriter &json, const DeviceSnapshot &snapshot);
void httpJsonWriteGps(HttpJsonWriter &json, const DeviceSnapshot &snapshot);

void httpJsonWriteStatus(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    const HttpMetricsView &metrics,
    bool server_running
);

void httpJsonWriteCapabilities(HttpJsonWriter &json, uint32_t refresh_interval_ms);

}  // namespace qsee
