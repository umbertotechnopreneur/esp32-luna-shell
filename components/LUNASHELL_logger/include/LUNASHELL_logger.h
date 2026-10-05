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
 * Capture precedes best-effort USB output. A bounded boot backlog and the UI
 * ring retain diagnostics before the removable-storage owner enables SD.
 * Initialization returns its real error; the startup orchestrator explicitly
 * treats logging as optional. A boot-buffer failure leaves the core sinks usable.
 */
// No parameters: initialize once and retain the original result for repeated requests.
// Exception: Returns the actual driver/allocation error without resetting the device.
esp_err_t LUNASHELL_logger_init(void);

// No parameters: seal the boot backlog and request its asynchronous PSRAM archival.
// Exception: Returns the real initialization/notification error; never waits for file I/O.
esp_err_t LUNASHELL_logger_finish_boot_capture(void);

/**
 * Enable SD mirroring after the card has been mounted.
 *
 * The logger never formats or mounts media by itself.
 */
esp_err_t LUNASHELL_logger_enable_sd(const char *mount_point);

/**
 * Stop SD mirroring safely. Serial output and the RAM ring remain active.
 */
esp_err_t LUNASHELL_logger_disable_sd(void);

/**
 * Suspend or resume RAM-ring capture while keeping best-effort USB output.
 *
 * Performance Mode uses this to avoid the second formatting pass and ring
 * synchronization on every log line. SD mirroring is controlled separately.
 */
esp_err_t LUNASHELL_logger_set_capture_suspended(bool suspended);

/**
 * Liveness hooks supplied by the removable-storage owner.
 *
 * The logger component deliberately does not own the firmware watchdog. The
 * platform registers these hooks once before SD mirroring starts. Logger I/O is
 * optional and must not subscribe a watchdog that resets the whole device.
 * Ownership and original filesystem errors are retained independently.
 */
typedef struct {
    esp_err_t (*begin)(void);
    esp_err_t (*end)(esp_err_t operation_result);
} LUNASHELL_logger_io_watch_t;

esp_err_t LUNASHELL_logger_set_io_watch(
    const LUNASHELL_logger_io_watch_t *watch
);

typedef struct {
    bool enabled;
    esp_err_t last_error;
    int last_errno;
    int64_t changed_at_us;
    uint32_t failure_count;
    /** Advances only after buffered SD data is flushed successfully. */
    uint32_t persisted_write_sequence;
    uint32_t boot_dropped_lines;
    uint32_t usb_dropped_lines;
    /** Loss counters do not disable a healthy best-effort SD sink. */
    uint32_t format_dropped_lines;
    uint32_t queue_dropped_lines;
    uint32_t truncated_lines;
    esp_err_t capture_error;
    int64_t capture_error_at_us;
    uint32_t boot_retained_bytes;
    uint32_t boot_archive_bytes;
    int64_t sampled_at_us;
} LUNASHELL_logger_sd_status_t;

/**
 * Return the current SD writer state, including asynchronous write failures.
 */
// status receives writer health, retained memory and independently counted sink losses.
// Returns an argument/state error when status cannot be supplied.
esp_err_t LUNASHELL_logger_get_sd_status(LUNASHELL_logger_sd_status_t *status);

typedef enum {
    LUNASHELL_LOG_FILE_CURRENT,
    LUNASHELL_LOG_FILE_PREVIOUS,
} LUNASHELL_logger_log_file_t;

// mount_point is the verified card root supplied by the storage owner after mount.
// Returns the real ownership/path error; binding never creates or formats files.
esp_err_t LUNASHELL_logger_bind_sd(const char *mount_point);

// file identifies one of the logger's two owned files; arbitrary paths are rejected.
// offset is the requested byte position within that file.
// expected_revision is zero on the first read, then pins the original file identity.
// buffer receives a bounded block without text conversion.
// buffer_size is the capacity of buffer.
// bytes_read receives the exact successful read length.
// total_size receives the flushed file size at this read.
// revision receives the current identity, which changes on rotation or mirror restart.
// system_errno receives the original filesystem errno, or zero for policy failures.
// Returns the real I/O or argument error; callers must retain storage ownership.
esp_err_t LUNASHELL_logger_read_sd_log(
    LUNASHELL_logger_log_file_t file, uint32_t offset,
    uint32_t expected_revision, void *buffer, size_t buffer_size,
    size_t *bytes_read, uint32_t *total_size, uint32_t *revision,
    int *system_errno
);

typedef esp_err_t (*LUNASHELL_logger_sd_reader_t)(
    LUNASHELL_logger_log_file_t file, uint32_t offset,
    uint32_t expected_revision, void *buffer, size_t buffer_size,
    size_t *bytes_read, uint32_t *total_size, uint32_t *revision,
    int *system_errno
);

// reader is the immutable storage-owner callback that protects mount/power lifetime.
// Returns an argument/state error if ownership cannot be established.
esp_err_t LUNASHELL_logger_set_sd_reader(LUNASHELL_logger_sd_reader_t reader);

#define LUNASHELL_LOGGER_SNAPSHOT_MAX_LINES 15U

/**
 * Copy up to LUNASHELL_LOGGER_SNAPSHOT_MAX_LINES newest complete log lines into a
 * caller-owned text buffer.
 */
esp_err_t LUNASHELL_logger_snapshot(
    char *buffer,
    size_t buffer_size,
    size_t max_lines,
    size_t *bytes_written
);

/** Clear only the in-memory UI ring; serial and SD logging continue. */
esp_err_t LUNASHELL_logger_clear(void);

#ifdef __cplusplus
}
#endif
