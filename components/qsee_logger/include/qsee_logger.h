#pragma once

#include <stdbool.h>
#include <stddef.h>

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
 * Copy the newest complete log lines into a caller-owned text buffer.
 */
size_t qsee_logger_snapshot(char *buffer, size_t buffer_size, size_t max_lines);

bool qsee_logger_sd_enabled(void);

#ifdef __cplusplus
}
#endif
