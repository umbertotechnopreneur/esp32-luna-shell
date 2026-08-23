/*
 * Application liveness watchdog support.
 *
 * Copyright (c) 2026
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Independently named progress domains reported by the ESP-IDF Task WDT.
 *
 * A domain is subscribed only while work that must make progress is active.
 * This avoids treating an intentionally parked worker or Device Standby as a
 * hang. A domain permits one active operation at a time.
 */
typedef enum {
    LUNASHELL_LIVENESS_GUI = 0,
    LUNASHELL_LIVENESS_DISPLAY,
    LUNASHELL_LIVENESS_SETTINGS,
    LUNASHELL_LIVENESS_PROVISIONING,
    LUNASHELL_LIVENESS_BOOT_AUDIO,
    LUNASHELL_LIVENESS_SNAPSHOT,
    LUNASHELL_LIVENESS_RADIOS,
    LUNASHELL_LIVENESS_WIFI,
    LUNASHELL_LIVENESS_WIFI_SCAN,
    LUNASHELL_LIVENESS_WIFI_EVENTS,
    LUNASHELL_LIVENESS_STARTUP,
    LUNASHELL_LIVENESS_POINT_COUNT,
} LUNASHELL_liveness_point_t;

typedef struct {
    bool valid;
    uint32_t operation_sequence;
    uint32_t active_mask;
    LUNASHELL_liveness_point_t last_progress_point;
} LUNASHELL_liveness_timeout_trace_t;

typedef struct {
    bool initialized;
    uint32_t boot_reset_reason;
    uint32_t watchdog_timeout_ms;
    uint32_t operation_sequence;
    uint32_t active_mask;
    bool last_progress_valid;
    LUNASHELL_liveness_point_t last_progress_point;
    uint32_t last_progress_age_ms;
    esp_err_t last_error;
    LUNASHELL_liveness_timeout_trace_t previous_timeout;
} LUNASHELL_liveness_status_t;

/**
 * Verify and apply the production Task-WDT policy.
 *
 * Call once near the beginning of app_main(), after the ESP-IDF scheduler and
 * logging are available but before starting watched services. Initialization
 * is idempotent. A previous Task-WDT ISR breadcrumb is consumed and retained
 * in the runtime status until the next reset.
 */
esp_err_t LUNASHELL_liveness_init(void);

/**
 * Subscribe one active progress domain to the Task WDT.
 *
 * Begin is rejected when the same domain is already active. A service that is
 * intentionally quiesced must close its watch before parking and reopen it
 * before resuming work. The caller must pair every successful begin with
 * watch_end().
 */
esp_err_t LUNASHELL_liveness_watch_begin(
    LUNASHELL_liveness_point_t point
);

/**
 * Feed one active domain only after observable forward progress.
 *
 * Do not call this from a blind periodic timer: doing so would conceal the
 * exact hang this service is intended to detect.
 */
esp_err_t LUNASHELL_liveness_watch_progress(
    LUNASHELL_liveness_point_t point
);

/**
 * Unsubscribe a completed progress domain and preserve its real result.
 *
 * After a successful unsubscribe, the operation result is returned unchanged.
 * Failure to close watchdog coverage is an ownership invariant violation and
 * aborts immediately rather than exposing an ambiguous live subscription.
 */
esp_err_t LUNASHELL_liveness_watch_end(
    LUNASHELL_liveness_point_t point,
    esp_err_t operation_result
);

/**
 * Copy health state without an unbounded wait.
 *
 * The output is untouched when the service is unavailable or its internal
 * lock cannot be acquired within the bounded diagnostic deadline.
 */
esp_err_t LUNASHELL_liveness_get_status(
    LUNASHELL_liveness_status_t *status
);

const char *LUNASHELL_liveness_point_name(
    LUNASHELL_liveness_point_t point
);

#ifdef __cplusplus
}
#endif
