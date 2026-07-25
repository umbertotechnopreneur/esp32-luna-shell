/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

// Canonical logger interface used across firmware components.
// Initializes the logging pipeline before the rest of the firmware starts.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Install the canonical logger before any other subsystem is initialized.
 *
 * Serial output remains authoritative. The same formatted lines are copied to
 * a bounded in-memory ring and, once enabled, to a low-priority SD writer.
 */
esp_err_t qsee_logger_init(void);

/**
 * Enable SD mirroring after the card has been mounted.
 *
 * The logger never formats or mounts media by itself.
 */
esp_err_t qsee_logger_enable_sd(const char *mount_point);

/**
 * Stop SD mirroring safely. Serial output and the RAM ring remain active.
 */
esp_err_t qsee_logger_disable_sd(void);

typedef struct {
    bool enabled;
    esp_err_t last_error;
    int last_errno;
    int64_t changed_at_us;
    uint32_t failure_count;
} qsee_logger_sd_status_t;

/**
 * Return the current SD writer state, including asynchronous write failures.
 */
esp_err_t qsee_logger_get_sd_status(qsee_logger_sd_status_t *status);

/**
 * Copy the newest complete log lines into a caller-owned text buffer.
 */
esp_err_t qsee_logger_snapshot(
    char *buffer,
    size_t buffer_size,
    size_t max_lines,
    size_t *bytes_written
);

/** Clear only the in-memory UI ring; serial and SD logging continue. */
esp_err_t qsee_logger_clear(void);

#ifdef __cplusplus
}
#endif
