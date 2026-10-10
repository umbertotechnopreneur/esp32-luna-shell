/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 * Original file notice:
 *
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * VBWR E */


// JSON builders for snapshot and metrics responses.
// Keeps transport formatting separate from HTTP routing.

#pragma once

#include <cstddef>
#include <cstdint>

#include "LUNASHELL_liveness.h"
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
struct HttpJsonSink {
    void *context = nullptr;
    // final is true only for chunk termination; transport errors stay original.
    esp_err_t (*write)(void *context, const char *data, std::size_t size, bool final) = nullptr;
};

struct HttpLivenessView {
    LUNASHELL_liveness_status_t status{};
    esp_err_t query_error = ESP_ERR_NOT_SUPPORTED;
    const char *last_progress_name = "none";
    const char *previous_timeout_name = "none";
};

class HttpJsonWriter final {
public:
    // sink: Explicit transport owner; serialization never imports an HTTP driver.
    // now_us: Original observation time, supplied by the firmware or synthetic host owner.
    explicit HttpJsonWriter(HttpJsonSink sink, int64_t now_us);

    // No parameters: returns the owner's observation time without reading a clock.
    [[nodiscard]] int64_t observationTimeUs() const { return _now_us; }

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

    HttpJsonSink _sink;
    int64_t _now_us;
    char _buffer[512] = {};
    std::size_t _used = 0;
    bool _failed = false;
    esp_err_t _last_error = ESP_OK;
};

void httpJsonWriteHealth(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    const HttpLifecycleView &lifecycle,
    const HttpLivenessView &liveness
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
