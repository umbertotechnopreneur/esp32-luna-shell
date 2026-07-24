/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

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
 * Stop SD mirroring safely. Serial output and the RAM ring remain active.
 */
void qsee_logger_disable_sd(void);

/**
 * Copy the newest complete log lines into a caller-owned text buffer.
 */
size_t qsee_logger_snapshot(char *buffer, size_t buffer_size, size_t max_lines);

bool qsee_logger_sd_enabled(void);

#ifdef __cplusplus
}
#endif
