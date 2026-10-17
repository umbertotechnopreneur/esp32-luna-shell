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


// Canonical logging backend for the firmware runtime.
// Serializes log output and protects early-boot initialization ordering.

#include "LUNASHELL_logger.h"
#include "LUNASHELL_logger_usb.h"
#include "sdkconfig.h"

#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LUNASHELL_LOG_RING_LINES LUNASHELL_LOGGER_SNAPSHOT_MAX_LINES
#define LUNASHELL_LOG_LINE_BYTES 256
#define LUNASHELL_LOG_QUEUE_LINES 48
#define LUNASHELL_LOG_FORMAT_SLOTS 4
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
#define LUNASHELL_BOOT_LOG_LINES 128U
#else
#define LUNASHELL_BOOT_LOG_LINES 1U
#endif
#define LUNASHELL_LOG_FILE_LIMIT_BYTES (2U * 1024U * 1024U)
#define LUNASHELL_LOG_FLUSH_DEADLINE_MS 100U
#define LUNASHELL_LOG_IO_BATCH_LINES 8U
#define LUNASHELL_LOG_ENQUEUE_QUIESCE_TIMEOUT_MS 1000U

typedef struct {
    char text[LUNASHELL_LOG_LINE_BYTES];
} LUNASHELL_ram_log_line_t;

typedef struct {
    char text[LUNASHELL_LOG_LINE_BYTES];
    uint32_t sd_generation;
} LUNASHELL_log_line_t;

static const char *TAG = "LUNASHELL_logger";
static LUNASHELL_ram_log_line_t s_ring[LUNASHELL_LOG_RING_LINES];
static LUNASHELL_log_line_t s_format_slots[LUNASHELL_LOG_FORMAT_SLOTS];
static bool s_format_slot_used[LUNASHELL_LOG_FORMAT_SLOTS];
// Snapshot export runs outside the immediate vprintf path, so its bulk copy
// does not need the cache-off-safe internal memory used by the live log ring.
static EXT_RAM_BSS_ATTR
LUNASHELL_ram_log_line_t s_snapshot_copy[LUNASHELL_LOGGER_SNAPSHOT_MAX_LINES];
static size_t s_ring_head;
static size_t s_ring_count;
static portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_format_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_snapshot_mutex;
static StaticSemaphore_t s_snapshot_mutex_control;
static SemaphoreHandle_t s_writer_idle;
static StaticSemaphore_t s_writer_idle_control;
static SemaphoreHandle_t s_io_mutex;
static StaticSemaphore_t s_io_mutex_control;
static QueueHandle_t s_write_queue;
static StaticQueue_t s_write_queue_control;
static uint8_t s_write_queue_storage[LUNASHELL_LOG_QUEUE_LINES * sizeof(LUNASHELL_log_line_t)];
static TaskHandle_t s_writer_task;
static char s_log_path[160];
static LUNASHELL_logger_sd_status_t s_sd_status;
static uint32_t s_sd_generation;
static portMUX_TYPE s_sd_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;
static bool s_initialization_attempted;
static esp_err_t s_initialization_result = ESP_ERR_INVALID_STATE;
static atomic_bool s_capture_suspended;
static atomic_bool s_sd_enqueue_open;
static atomic_uint s_sd_enqueue_inflight;
static LUNASHELL_logger_io_watch_t s_io_watch;
static atomic_bool s_io_watch_configured;
// The boot buffer stays internal, like the live ring, so logging remains safe
// when another core temporarily disables the flash/PSRAM cache for NVS.
// Allocate before installing the callback; after sealing, only the writer may
// archive to PSRAM or release records after a confirmed replay flush.
static LUNASHELL_ram_log_line_t *s_boot_lines;
static portMUX_TYPE s_boot_lock = portMUX_INITIALIZER_UNLOCKED;
static size_t s_boot_count;
static size_t s_boot_written;
static uint32_t s_boot_dropped;
static uint32_t s_boot_dropped_total;
static bool s_boot_replay_complete;
static bool s_boot_lines_external;
static atomic_bool s_boot_archive_requested;
static atomic_bool s_boot_archive_started;
static atomic_bool s_boot_archive_completed;
static atomic_int s_boot_archive_error;
static portMUX_TYPE s_capture_status_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_format_dropped_lines;
static uint32_t s_queue_dropped_lines;
static uint32_t s_truncated_lines;
static esp_err_t s_capture_error;
static int64_t s_capture_error_at_us;
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
static bool s_boot_collecting = true;
#else
static bool s_boot_collecting;
#endif
static bool s_boot_file_prepared;
static bool s_boot_identity_written;
// Protected by s_io_mutex: appends retain identity; rename/remount changes it.
static uint32_t s_log_revision = 1U;

// No parameters: keep zero reserved for the first, unpinned host read.
static void advance_log_revision(void)
{
    s_log_revision++;
    if (s_log_revision == 0U) {
        s_log_revision = 1U;
    }
}

// format describes a diagnostic emitted outside the recursive log callback.
// args are supplied by this function's variadic caller.
static int serial_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = LUNASHELL_logger_usb_vprintf(format, args);
    va_end(args);
    return result;
}

static void read_sd_state(LUNASHELL_logger_sd_status_t *status, uint32_t *generation)
{
    portENTER_CRITICAL(&s_sd_lock);
    if (status != NULL) {
        *status = s_sd_status;
    }
    if (generation != NULL) {
        *generation = s_sd_generation;
    }
    portEXIT_CRITICAL(&s_sd_lock);
}

// Wake the SD writer after a state change and report a broken task invariant.
static esp_err_t notify_writer_state_change(const char *operation)
{
    TaskHandle_t writer_task_handle = s_writer_task;
    if (writer_task_handle == NULL) {
        serial_printf(
            "E (%lld) %s: SD writer %s notification rejected: task is unavailable\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG,
            operation
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskNotifyGive(writer_task_handle) != pdPASS) {
        serial_printf(
            "E (%lld) %s: SD writer %s notification failed\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG,
            operation
        );
        return ESP_FAIL;
    }
    return ESP_OK;
}

// operation labels real SD I/O, serialized against reads, rotation and close.
// Returns the original liveness error or a bounded ownership timeout.
static esp_err_t begin_watched_io(const char *operation)
{
    if (xSemaphoreTake(s_io_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        serial_printf("E (%lld) %s: SD %s I/O ownership timed out\n",
            (long long)(esp_timer_get_time() / 1000), TAG, operation);
        return ESP_ERR_TIMEOUT;
    }
    if (!atomic_load_explicit(
            &s_io_watch_configured,
            memory_order_acquire
        )) {
        serial_printf(
            "E (%lld) %s: SD mirror %s rejected: I/O liveness is not configured\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG,
            operation
        );
        if (xSemaphoreGive(s_io_mutex) != pdTRUE) {
            abort();
        }
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t error = s_io_watch.begin();
    if (error != ESP_OK) {
        serial_printf(
            "E (%lld) %s: SD mirror %s liveness start failed: %s\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG,
            operation,
            esp_err_to_name(error)
        );
        if (xSemaphoreGive(s_io_mutex) != pdTRUE) {
            abort();
        }
    }
    return error;
}

// operation identifies the completed filesystem operation.
// operation_result retains its original I/O error.
// Returns the liveness completion result and releases I/O ownership.
static esp_err_t end_watched_io(
    const char *operation,
    esp_err_t operation_result
)
{
    const esp_err_t error = s_io_watch.end(operation_result);
    if (error != operation_result) {
        serial_printf(
            "E (%lld) %s: SD mirror %s liveness completion changed %s to %s\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG,
            operation,
            esp_err_to_name(operation_result),
            esp_err_to_name(error)
        );
    }
    if (xSemaphoreGive(s_io_mutex) != pdTRUE) {
        abort();
    }
    return error;
}

static esp_err_t current_sd_error(void)
{
    LUNASHELL_logger_sd_status_t status;
    read_sd_state(&status, NULL);
    return status.last_error != ESP_OK ? status.last_error : ESP_FAIL;
}

static void note_persisted_write(void)
{
    portENTER_CRITICAL(&s_sd_lock);
    s_sd_status.persisted_write_sequence++;
    portEXIT_CRITICAL(&s_sd_lock);
}

// Admit an SD queue producer without ever blocking the logging callback.
// The second gate read closes the load-before-disable race: a producer that was
// preempted before incrementing the counter observes the closed gate and exits
// without touching the queue.
static bool begin_sd_enqueue(
    LUNASHELL_logger_sd_status_t *status,
    uint32_t *generation
)
{
    if (!atomic_load_explicit(
            &s_sd_enqueue_open,
            memory_order_seq_cst
        )) {
        return false;
    }

    (void)atomic_fetch_add_explicit(
        &s_sd_enqueue_inflight,
        1U,
        memory_order_seq_cst
    );
    if (!atomic_load_explicit(
            &s_sd_enqueue_open,
            memory_order_seq_cst
        )) {
        (void)atomic_fetch_sub_explicit(
            &s_sd_enqueue_inflight,
            1U,
            memory_order_seq_cst
        );
        return false;
    }

    read_sd_state(status, generation);
    if (!status->enabled) {
        (void)atomic_fetch_sub_explicit(
            &s_sd_enqueue_inflight,
            1U,
            memory_order_seq_cst
        );
        return false;
    }
    return true;
}

static void end_sd_enqueue(void)
{
    const unsigned int previous = atomic_fetch_sub_explicit(
        &s_sd_enqueue_inflight,
        1U,
        memory_order_seq_cst
    );
    if (previous == 0U) {
        serial_printf(
            "E (%lld) %s: SD enqueue ownership counter underflow\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG
        );
        abort();
    }
}

// Close producer admission and wait only for callbacks already inside their
// bounded, non-blocking queue section. A stuck producer keeps stop incomplete,
// so callers cannot treat the writer acknowledgement as permission to unmount.
static esp_err_t close_sd_enqueue_gate(void)
{
    atomic_store_explicit(
        &s_sd_enqueue_open,
        false,
        memory_order_seq_cst
    );
    const TickType_t started_at = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(
        LUNASHELL_LOG_ENQUEUE_QUIESCE_TIMEOUT_MS
    );
    while (atomic_load_explicit(
               &s_sd_enqueue_inflight,
               memory_order_seq_cst
           ) != 0U) {
        if ((xTaskGetTickCount() - started_at) >= timeout_ticks) {
            serial_printf(
                "E (%lld) %s: SD enqueue quiescence timed out after %u ms\n",
                (long long)(esp_timer_get_time() / 1000),
                TAG,
                (unsigned int)LUNASHELL_LOG_ENQUEUE_QUIESCE_TIMEOUT_MS
            );
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
    return ESP_OK;
}

// Publish the enabled generation and release a writer parked without polling.
static esp_err_t set_sd_enabled(void)
{
    const int64_t changed_at_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_sd_lock);
    s_sd_status.enabled = true;
    s_sd_status.last_error = ESP_OK;
    s_sd_status.last_errno = 0;
    s_sd_status.changed_at_us = changed_at_us;
    s_sd_generation++;
    portEXIT_CRITICAL(&s_sd_lock);
    // Publish producer admission before waking the writer that closes boot capture.
    atomic_store_explicit(&s_sd_enqueue_open, true, memory_order_seq_cst);
    const esp_err_t notification_error =
        notify_writer_state_change("enable");
    return notification_error;
}

// Publish the disabled state and force a fresh idle acknowledgement.
static esp_err_t set_sd_disabled(void)
{
    const esp_err_t quiescence_error = close_sd_enqueue_gate();
    if (quiescence_error != ESP_OK) {
        return quiescence_error;
    }
    const int64_t changed_at_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_sd_lock);
    s_sd_status.enabled = false;
    s_sd_status.changed_at_us = changed_at_us;
    portEXIT_CRITICAL(&s_sd_lock);
    return notify_writer_state_change("disable");
}

// Record the real SD error and wake the writer so it can close its open file.
// Producer-side failures bind to the generation they entered: a delayed
// callback from an old lease must never close a newly enabled mirror.
static bool record_sd_failure_impl(
    const char *operation,
    esp_err_t error,
    int system_errno,
    bool expected_generation_valid,
    uint32_t expected_generation
)
{
    const int64_t changed_at_us = esp_timer_get_time();
    bool committed = false;
    uint32_t current_generation = 0U;
    portENTER_CRITICAL(&s_sd_lock);
    current_generation = s_sd_generation;
    if (!expected_generation_valid ||
        expected_generation == current_generation) {
        atomic_store_explicit(
            &s_sd_enqueue_open,
            false,
            memory_order_seq_cst
        );
        s_sd_status.enabled = false;
        s_sd_status.last_error = error;
        s_sd_status.last_errno = system_errno;
        s_sd_status.changed_at_us = changed_at_us;
        s_sd_status.failure_count++;
        committed = true;
    }
    portEXIT_CRITICAL(&s_sd_lock);

    if (!committed) {
        serial_printf(
            "W (%lld) %s: Ignoring stale SD mirror %s failure for generation %lu; current generation=%lu error=%s\n",
            (long long)(changed_at_us / 1000),
            TAG,
            operation,
            (unsigned long)expected_generation,
            (unsigned long)current_generation,
            esp_err_to_name(error)
        );
        return false;
    }

    serial_printf(
        "E (%lld) %s: SD mirror %s failed: %s (errno=%d)\n",
        (long long)(changed_at_us / 1000),
        TAG,
        operation,
        esp_err_to_name(error),
        system_errno
    );
    const esp_err_t notification_error =
        notify_writer_state_change("failure-state");
    if (notification_error != ESP_OK) {
        serial_printf(
            "E (%lld) %s: SD failure-state wake also failed: %s\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG,
            esp_err_to_name(notification_error)
        );
    }
    return true;
}

static void record_sd_failure(
    const char *operation,
    esp_err_t error,
    int system_errno
)
{
    (void)record_sd_failure_impl(
        operation,
        error,
        system_errno,
        false,
        0U
    );
}

static void record_sd_failure_for_generation(
    const char *operation,
    esp_err_t error,
    int system_errno,
    uint32_t expected_generation
)
{
    (void)record_sd_failure_impl(
        operation,
        error,
        system_errno,
        true,
        expected_generation
    );
}

static void ring_push(const char *line)
{
    portENTER_CRITICAL(&s_ring_lock);
    strlcpy(s_ring[s_ring_head].text, line, sizeof(s_ring[s_ring_head].text));
    s_ring_head = (s_ring_head + 1U) % LUNASHELL_LOG_RING_LINES;
    if (s_ring_count < LUNASHELL_LOG_RING_LINES) {
        s_ring_count++;
    }
    portEXIT_CRITICAL(&s_ring_lock);
}

static LUNASHELL_log_line_t *acquire_format_slot(void)
{
    LUNASHELL_log_line_t *slot = NULL;
    portENTER_CRITICAL(&s_format_lock);
    for (size_t i = 0; i < LUNASHELL_LOG_FORMAT_SLOTS; ++i) {
        if (!s_format_slot_used[i]) {
            s_format_slot_used[i] = true;
            slot = &s_format_slots[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_format_lock);
    return slot;
}

static void release_format_slot(LUNASHELL_log_line_t *slot)
{
    if (slot == NULL) {
        return;
    }

    const size_t index = (size_t)(slot - s_format_slots);
    if (index >= LUNASHELL_LOG_FORMAT_SLOTS) {
        return;
    }

    portENTER_CRITICAL(&s_format_lock);
    s_format_slot_used[index] = false;
    portEXIT_CRITICAL(&s_format_lock);
}

// counter: Internal counter already protected by its owning critical section.
static void increment_loss_count(uint32_t *counter)
{
    if (*counter != UINT32_MAX) {
        ++*counter;
    }
}

// counter: Optional internal loss counter; null retains an error without inventing a lost line.
// error: Real formatting/capacity error retained independently from SD I/O health.
static void record_capture_loss(uint32_t *counter, esp_err_t error)
{
    const int64_t failed_at_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_capture_status_lock);
    if (counter != NULL) {
        increment_loss_count(counter);
    }
    s_capture_error = error;
    s_capture_error_at_us = failed_at_us;
    portEXIT_CRITICAL(&s_capture_status_lock);
}

// text contains one captured boot line; storage starts before display startup.
// Returns whether this line belongs to the bounded early-boot backlog.
static bool capture_boot_line(const char *text)
{
    portENTER_CRITICAL(&s_boot_lock);
    const bool collecting = s_boot_collecting;
    if (collecting) {
        if (s_boot_lines != NULL && s_boot_count < LUNASHELL_BOOT_LOG_LINES) {
            strlcpy(s_boot_lines[s_boot_count++].text, text, LUNASHELL_LOG_LINE_BYTES);
        } else {
            increment_loss_count(&s_boot_dropped);
            increment_loss_count(&s_boot_dropped_total);
        }
    }
    portEXIT_CRITICAL(&s_boot_lock);
    return collecting;
}

// No parameters: retain early capture loss even when no serial host can see it.
static void note_boot_capture_loss(void)
{
    portENTER_CRITICAL(&s_boot_lock);
    if (s_boot_collecting) {
        increment_loss_count(&s_boot_dropped);
        increment_loss_count(&s_boot_dropped_total);
    }
    portEXIT_CRITICAL(&s_boot_lock);
}

// format is the original ESP-IDF log format.
// args contains caller-owned arguments, copied separately for each sink.
static int LUNASHELL_log_vprintf(const char *format, va_list args)
{
    va_list capture_args;

    if (atomic_load_explicit(&s_capture_suspended, memory_order_relaxed)) {
        return LUNASHELL_logger_usb_vprintf(format, args);
    }

    LUNASHELL_log_line_t *line = acquire_format_slot();
    if (line == NULL) {
        note_boot_capture_loss();
        record_capture_loss(&s_format_dropped_lines, ESP_ERR_NO_MEM);
        return LUNASHELL_logger_usb_vprintf(format, args);
    }

    va_copy(capture_args, args);
    const int capture_result =
        vsnprintf(line->text, sizeof(line->text), format, capture_args);
    va_end(capture_args);
    if (capture_result < 0) {
        note_boot_capture_loss();
        record_capture_loss(&s_format_dropped_lines, ESP_ERR_INVALID_RESPONSE);
        serial_printf(
            "E (%lld) %s: Log capture formatting failed; serial output only\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG
        );
        release_format_slot(line);
        return LUNASHELL_logger_usb_vprintf(format, args);
    }
    if ((size_t)capture_result >= sizeof(line->text)) {
        record_capture_loss(&s_truncated_lines, ESP_ERR_INVALID_SIZE);
        static const char truncated_suffix[] = " [TRUNCATED]\n";
        const size_t suffix_size = sizeof(truncated_suffix);
        memcpy(
            line->text + sizeof(line->text) - suffix_size,
            truncated_suffix,
            suffix_size
        );
        serial_printf(
            "E (%lld) %s: Captured log line exceeded %u bytes and was marked truncated\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG,
            (unsigned int)(sizeof(line->text) - 1U)
        );
    }

    ring_push(line->text);
    const bool boot_captured = capture_boot_line(line->text);
    LUNASHELL_logger_sd_status_t sd_status;
    uint32_t sd_generation;
    bool enqueue_failed = false;
    esp_err_t notification_error = ESP_OK;
    if (!boot_captured && s_write_queue != NULL &&
        begin_sd_enqueue(&sd_status, &sd_generation)) {
        line->sd_generation = sd_generation;
        if (xQueueSend(s_write_queue, line, 0) != pdTRUE) {
            enqueue_failed = true;
        } else {
            notification_error = notify_writer_state_change("queued-data");
        }
        if (enqueue_failed) {
            // Capacity pressure loses this record, not the healthy SD mirror.
            // The caller never waits for space, file I/O or a flush.
            record_capture_loss(&s_queue_dropped_lines, ESP_ERR_NO_MEM);
        } else if (notification_error != ESP_OK) {
            record_sd_failure_for_generation(
                "queue notification",
                notification_error,
                0,
                sd_generation
            );
        }
        // Failure publication is part of producer ownership. Stop cannot
        // acknowledge quiescence, nor can a new generation open, before this
        // callback has either committed or rejected its generation-bound error.
        end_sd_enqueue();
    }
    release_format_slot(line);

    // Capturing first makes SD independent of host USB receive progress.
    return LUNASHELL_logger_usb_vprintf(format, args);
}

// No parameters: rotate only the two logger-owned files, preserving the last boot.
// Returns false and records the real filesystem error on failed rotation.
static bool rotate_log_path(void)
{
    char previous_path[sizeof(s_log_path) + 4U];
    const int length = snprintf(previous_path, sizeof(previous_path), "%s.1", s_log_path);
    if (length < 0 || (size_t)length >= sizeof(previous_path)) {
        record_sd_failure("rotation path", ESP_ERR_INVALID_SIZE, 0);
        return false;
    }
    if (remove(previous_path) != 0 && errno != ENOENT) {
        const int saved_errno = errno;
        record_sd_failure("rotation remove", ESP_FAIL, saved_errno);
        return false;
    }
    if (rename(s_log_path, previous_path) != 0) {
        const int saved_errno = errno;
        record_sd_failure("rotation rename", ESP_FAIL, saved_errno);
        return false;
    }
    advance_log_revision();
    return true;
}

static bool close_log_file(FILE **file, const char *operation)
{
    if (*file == NULL) {
        return true;
    }

    FILE *open_file = *file;
    *file = NULL;
    if (fclose(open_file) != 0) {
        const int close_errno = errno;
        record_sd_failure(operation, ESP_FAIL, close_errno);
        return false;
    }
    return true;
}

// file is the writer's open current log, already flushed before rotation.
// Returns false with the original rotation error retained in SD status.
static bool rotate_log_if_needed(FILE **file)
{
    const long position = ftell(*file);
    if (position < 0) {
        const int position_errno = errno;
        record_sd_failure("size check", ESP_FAIL, position_errno);
        return false;
    }
    if ((unsigned long)position < LUNASHELL_LOG_FILE_LIMIT_BYTES) {
        return true;
    }

    if (!close_log_file(file, "rotation close")) {
        return false;
    }

    return rotate_log_path();
}

static bool flush_log_file(
    FILE **file,
    unsigned int *lines_since_flush,
    const char *operation
)
{
    if (*file == NULL || *lines_since_flush == 0U) {
        return true;
    }
    if (fflush(*file) != 0) {
        const int flush_errno = errno;
        *lines_since_flush = 0;
        record_sd_failure(operation, ESP_FAIL, flush_errno);
        close_log_file(file, "close after flush");
        return false;
    }
    note_persisted_write();
    *lines_since_flush = 0;
    return true;
}

static bool write_log_line(
    FILE **file,
    unsigned int *lines_since_flush,
    const LUNASHELL_log_line_t *line
)
{
    if (*file == NULL) {
        *file = fopen(s_log_path, "ab");
        if (*file == NULL) {
            const int open_errno = errno;
            record_sd_failure("open", ESP_FAIL, open_errno);
            return false;
        }
        if (fseek(*file, 0, SEEK_END) != 0) {
            const int seek_errno = errno;
            record_sd_failure("seek", ESP_FAIL, seek_errno);
            close_log_file(file, "close after seek");
            return false;
        }
    }

    if (fputs(line->text, *file) == EOF) {
        const int write_errno = errno;
        record_sd_failure("write", ESP_FAIL, write_errno);
        close_log_file(file, "close after write");
        return false;
    }
    const size_t length = strlen(line->text);
    if (length == 0U || line->text[length - 1U] != '\n') {
        if (fputc('\n', *file) == EOF) {
            const int newline_errno = errno;
            record_sd_failure("newline write", ESP_FAIL, newline_errno);
            close_log_file(file, "close after newline write");
            return false;
        }
    }

    (*lines_since_flush)++;
    if (*lines_since_flush < 8U) {
        return true;
    }
    if (!flush_log_file(file, lines_since_flush, "flush")) {
        return false;
    }
    if (!rotate_log_if_needed(file)) {
        close_log_file(file, "close after rotation");
        return false;
    }
    return true;
}

static bool write_queued_lines_watched(
    FILE **file,
    unsigned int *lines_since_flush,
    const LUNASHELL_log_line_t *first_line,
    uint32_t active_generation
)
{
    esp_err_t result = begin_watched_io("write batch");
    if (result != ESP_OK) {
        record_sd_failure("write liveness", result, 0);
        return false;
    }

    LUNASHELL_log_line_t line = *first_line;
    unsigned int processed_lines = 0U;
    while (processed_lines < LUNASHELL_LOG_IO_BATCH_LINES) {
        processed_lines++;
        if (line.sd_generation == active_generation) {
            if (!write_log_line(file, lines_since_flush, &line)) {
                result = current_sd_error();
                break;
            }
        }
        if (processed_lines >= LUNASHELL_LOG_IO_BATCH_LINES) {
            break;
        }
        if (xQueueReceive(s_write_queue, &line, 0) != pdTRUE) {
            break;
        }
    }

    const esp_err_t watch_error = end_watched_io("write batch", result);
    if (result == ESP_OK && watch_error != ESP_OK) {
        record_sd_failure("write liveness completion", watch_error, 0);
        return false;
    }
    return result == ESP_OK;
}

static bool flush_and_close_watched(
    FILE **file,
    unsigned int *lines_since_flush,
    const char *flush_operation,
    const char *close_operation
)
{
    if (*file == NULL && *lines_since_flush == 0U) {
        return true;
    }

    esp_err_t result = begin_watched_io(close_operation);
    if (result != ESP_OK) {
        record_sd_failure("close liveness", result, 0);
        return false;
    }

    const bool flushed = flush_log_file(
        file,
        lines_since_flush,
        flush_operation
    );
    if (!flushed) {
        result = current_sd_error();
    }

    const bool closed = close_log_file(file, close_operation);
    if (!closed && result == ESP_OK) {
        result = current_sd_error();
    }
    const esp_err_t watch_error = end_watched_io(close_operation, result);
    if (result == ESP_OK && watch_error != ESP_OK) {
        record_sd_failure("close liveness completion", watch_error, 0);
        return false;
    }
    return flushed && closed && result == ESP_OK;
}

static bool flush_log_file_watched(
    FILE **file,
    unsigned int *lines_since_flush,
    const char *operation
)
{
    if (*file == NULL || *lines_since_flush == 0U) {
        return true;
    }

    esp_err_t result = begin_watched_io(operation);
    if (result != ESP_OK) {
        record_sd_failure("flush liveness", result, 0);
        return false;
    }
    const bool flushed = flush_log_file(file, lines_since_flush, operation);
    result = flushed ? ESP_OK : current_sd_error();
    const esp_err_t watch_error = end_watched_io(operation, result);
    if (flushed && watch_error != ESP_OK) {
        record_sd_failure("flush liveness completion", watch_error, 0);
        return false;
    }
    return flushed;
}

// file is the current SD writer handle.
// lines_since_flush tracks buffered writes, including early boot records.
// Returns false with a structured SD failure when replay cannot be persisted.
static bool replay_boot_log(FILE **file, unsigned int *lines_since_flush)
{
    portENTER_CRITICAL(&s_boot_lock);
    s_boot_collecting = false;
    const size_t count = s_boot_count;
    const uint32_t dropped = s_boot_dropped;
    portEXIT_CRITICAL(&s_boot_lock);

    while (s_boot_written < count) {
        esp_err_t result = begin_watched_io("boot replay");
        if (result != ESP_OK) {
            record_sd_failure("boot replay liveness", result, 0);
            return false;
        }
        size_t next_written = s_boot_written;
        size_t batch = 0U;
        while (next_written < count && batch < LUNASHELL_LOG_IO_BATCH_LINES) {
            LUNASHELL_log_line_t line = {0};
            strlcpy(line.text, s_boot_lines[next_written].text, sizeof(line.text));
            if (!write_log_line(file, lines_since_flush, &line)) {
                result = current_sd_error();
                break;
            }
            next_written++;
            batch++;
        }
        // Keep every unconfirmed record resident if writing or flushing fails.
        // An explicitly restarted mirror may repeat the last uncertain batch.
        if (result == ESP_OK && !flush_log_file(file, lines_since_flush, "boot batch flush")) {
            result = current_sd_error();
        }
        result = end_watched_io("boot replay", result);
        if (result != ESP_OK) {
            record_sd_failure("boot replay", result, 0);
            return false;
        }
        portENTER_CRITICAL(&s_boot_lock);
        s_boot_written = next_written;
        portEXIT_CRITICAL(&s_boot_lock);
    }
    if (dropped != 0U) {
        LUNASHELL_log_line_t marker = {0};
        snprintf(marker.text, sizeof(marker.text),
            "E (%lld) %s: BOOT LOG INCOMPLETE: dropped=%lu capacity=%u\n",
            (long long)(esp_timer_get_time() / 1000), TAG,
            (unsigned long)dropped, (unsigned int)LUNASHELL_BOOT_LOG_LINES);
        esp_err_t result = begin_watched_io("boot overflow marker");
        if (result != ESP_OK) {
            record_sd_failure("boot overflow marker liveness", result, 0);
            return false;
        }
        const bool written = write_log_line(file, lines_since_flush, &marker);
        const bool flushed = written && flush_log_file(file, lines_since_flush, "boot marker flush");
        result = end_watched_io("boot overflow marker", flushed ? ESP_OK : current_sd_error());
        if (result != ESP_OK) {
            record_sd_failure("boot overflow marker", result, 0);
            return false;
        }
        serial_printf("%s", marker.text);
        // This count remains visible in status; only the persisted marker is single-use.
        portENTER_CRITICAL(&s_boot_lock);
        s_boot_dropped = 0U;
        portEXIT_CRITICAL(&s_boot_lock);
    }
    if (!flush_log_file_watched(file, lines_since_flush, "boot replay flush")) {
        return false;
    }
    // Producers have stopped touching this buffer under the same lock. Only
    // this writer owns its contents; detach before freeing outside the lock.
    portENTER_CRITICAL(&s_boot_lock);
    LUNASHELL_ram_log_line_t *completed_boot_lines = s_boot_lines;
    const bool completed_external = s_boot_lines_external;
    s_boot_lines = NULL;
    s_boot_replay_complete = true;
    portEXIT_CRITICAL(&s_boot_lock);
    const unsigned int released_internal_bytes = completed_boot_lines != NULL && !completed_external
        ? (unsigned int)(LUNASHELL_BOOT_LOG_LINES * sizeof(*completed_boot_lines)) : 0U;
    const unsigned int released_external_bytes = completed_boot_lines != NULL && completed_external
        ? (unsigned int)(count * sizeof(*completed_boot_lines)) : 0U;
    heap_caps_free(completed_boot_lines);
    serial_printf("I (%lld) %s: Boot replay flushed; released internal=%u external=%u bytes; dropped=%lu\n",
        (long long)(esp_timer_get_time() / 1000), TAG,
        released_internal_bytes, released_external_bytes,
        (unsigned long)dropped);
    return true;
}

// No parameters: only the writer moves sealed records; producers cannot access PSRAM.
// Exception: Returns the real PSRAM allocation failure while retaining the internal backlog.
static esp_err_t archive_boot_records(void)
{
    portENTER_CRITICAL(&s_boot_lock);
    LUNASHELL_ram_log_line_t *internal_lines = s_boot_lines;
    const size_t count = s_boot_count;
    const bool sealed = !s_boot_collecting;
    const bool external = s_boot_lines_external;
    portEXIT_CRITICAL(&s_boot_lock);
    if (!sealed || external || internal_lines == NULL) {
        return ESP_OK;
    }
    const size_t archive_bytes = count * sizeof(*internal_lines);
    LUNASHELL_ram_log_line_t *archive = NULL;
    if (archive_bytes != 0U) {
        archive = heap_caps_malloc(archive_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (archive == NULL) {
            record_capture_loss(NULL, ESP_ERR_NO_MEM);
            serial_printf("E (%lld) %s: Boot archive allocation failed: %s bytes=%u; internal backlog retained\n",
                (long long)(esp_timer_get_time() / 1000), TAG,
                esp_err_to_name(ESP_ERR_NO_MEM), (unsigned int)archive_bytes);
            return ESP_ERR_NO_MEM;
        }
        // PSRAM copying is outside every callback/critical section and outside
        // the SD lock. The only writer owns these now-immutable records.
        memcpy(archive, internal_lines, archive_bytes);
    }
    portENTER_CRITICAL(&s_boot_lock);
    s_boot_lines = archive;
    s_boot_lines_external = true;
    portEXIT_CRITICAL(&s_boot_lock);
    heap_caps_free(internal_lines);
    serial_printf("I (%lld) %s: Boot archive moved to PSRAM: bytes=%u; released=%u internal bytes\n",
        (long long)(esp_timer_get_time() / 1000), TAG, (unsigned int)archive_bytes,
        (unsigned int)(LUNASHELL_BOOT_LOG_LINES * sizeof(*internal_lines)));
    return ESP_OK;
}

// arg is unused; this task alone owns the open SD writer handle and sealed backlog.
// Persist enabled SD logs and park indefinitely once the mirror is disabled.
static void writer_task(void *arg)
{
    (void)arg;
    FILE *file = NULL;
    unsigned int lines_since_flush = 0;
    LUNASHELL_log_line_t line;
    uint32_t active_generation = 0;

    while (true) {
        if (atomic_exchange_explicit(&s_boot_archive_requested, false, memory_order_acquire)) {
            const esp_err_t archive_error = archive_boot_records();
            // The helper returns only after freeing the internal allocation.
            // Publish completion separately from the earlier protected pointer swap.
            atomic_store_explicit(&s_boot_archive_error, archive_error, memory_order_relaxed);
            atomic_store_explicit(&s_boot_archive_completed, true, memory_order_release);
            if (archive_error != ESP_OK) {
                // The helper retained the error and original records. Do not
                // retry allocations in a loop or stop other best-effort sinks.
                serial_printf("W (%lld) %s: Optional boot archival unavailable (%s); backlog retained\n",
                    (long long)(esp_timer_get_time() / 1000), TAG, esp_err_to_name(archive_error));
            }
        }
        LUNASHELL_logger_sd_status_t sd_status;
        uint32_t generation;
        read_sd_state(&sd_status, &generation);

        if (generation != active_generation) {
            if (file != NULL) {
                if (!flush_and_close_watched(
                        &file,
                        &lines_since_flush,
                        "configuration flush",
                        "configuration close"
                    )) {
                    if (file != NULL) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        continue;
                    }
                    lines_since_flush = 0;
                    active_generation = generation;
                    continue;
                }
                lines_since_flush = 0;
            }
            active_generation = generation;
            continue;
        }

        if (!sd_status.enabled) {
            if (sd_status.last_error == ESP_OK) {
                bool persistence_available = true;
                while (xQueueReceive(s_write_queue, &line, 0) == pdTRUE) {
                    if (!write_queued_lines_watched(
                        &file,
                        &lines_since_flush,
                        &line,
                        active_generation
                    )) {
                        persistence_available = false;
                        break;
                    }
                }
                if (!persistence_available) {
                    while (xQueueReceive(s_write_queue, &line, 0) == pdTRUE) {
                        /* The recorded failure prevents further persistence. */
                    }
                }
            } else {
                while (xQueueReceive(s_write_queue, &line, 0) == pdTRUE) {
                    /* The recorded writer failure prevents safe persistence. */
                }
            }
            const bool stop_io_complete = flush_and_close_watched(
                &file,
                &lines_since_flush,
                "stop flush",
                "stop close"
            );
            if (!stop_io_complete && file != NULL) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            if (s_writer_idle != NULL) {
                const BaseType_t acknowledged = xSemaphoreGive(s_writer_idle);
                if (acknowledged != pdTRUE) {
                    /* A binary acknowledgement is already pending. */
                }
            }
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        portENTER_CRITICAL(&s_boot_lock);
        const bool boot_replay_pending = !s_boot_replay_complete;
        portEXIT_CRITICAL(&s_boot_lock);
        if (boot_replay_pending) {
            if (!replay_boot_log(&file, &lines_since_flush)) {
                continue;
            }
        }

        if (xQueueReceive(s_write_queue, &line, 0) != pdTRUE) {
            const TickType_t wait_ticks = lines_since_flush == 0U
                ? portMAX_DELAY
                : pdMS_TO_TICKS(LUNASHELL_LOG_FLUSH_DEADLINE_MS);
            if (ulTaskNotifyTake(pdTRUE, wait_ticks) == 0U &&
                lines_since_flush != 0U) {
                (void)flush_log_file_watched(
                    &file,
                    &lines_since_flush,
                    "idle flush"
                );
            }
            continue;
        }
        (void)write_queued_lines_watched(
            &file,
            &lines_since_flush,
            &line,
            active_generation
        );
    }
}

// Disable the mirror and wait for the writer's post-close acknowledgement.
static esp_err_t stop_sd_writer(void)
{
    LUNASHELL_logger_sd_status_t before;
    read_sd_state(&before, NULL);

    while (xSemaphoreTake(s_writer_idle, 0) == pdTRUE) {
        /* Remove a stale idle acknowledgement before requesting the stop. */
    }
    const esp_err_t notification_error = set_sd_disabled();
    if (notification_error != ESP_OK) {
        record_sd_failure("stop transition", notification_error, 0);
        return notification_error;
    }
    if (xSemaphoreTake(s_writer_idle, pdMS_TO_TICKS(1000)) != pdTRUE) {
        record_sd_failure("stop acknowledgement", ESP_ERR_TIMEOUT, 0);
        return ESP_ERR_TIMEOUT;
    }

    LUNASHELL_logger_sd_status_t after;
    read_sd_state(&after, NULL);
    if (after.failure_count != before.failure_count) {
        return after.last_error;
    }
    return ESP_OK;
}

// mount_point is the just-mounted card root, with the writer already stopped.
// Returns the actual ownership/path error; this read binding never writes media.
esp_err_t LUNASHELL_logger_bind_sd(const char *mount_point)
{
    if (!s_initialized || mount_point == NULL || mount_point[0] == '\0') {
        serial_printf("E (%lld) %s: SD read binding rejected: invalid state/path\n",
            (long long)(esp_timer_get_time() / 1000), TAG);
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_io_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        record_sd_failure("read binding ownership", ESP_ERR_TIMEOUT, 0);
        return ESP_ERR_TIMEOUT;
    }
    LUNASHELL_logger_sd_status_t status;
    read_sd_state(&status, NULL);
    esp_err_t result = ESP_OK;
    char path[sizeof(s_log_path)];
    const int length = snprintf(path, sizeof(path), "%s/LUNASHELL/logs/LUNASHELL.log", mount_point);
    if (status.enabled) {
        result = ESP_ERR_INVALID_STATE;
    } else if (length < 0 || (size_t)length >= sizeof(path)) {
        result = ESP_ERR_INVALID_SIZE;
    } else {
        strlcpy(s_log_path, path, sizeof(s_log_path));
        advance_log_revision();
    }
    if (xSemaphoreGive(s_io_mutex) != pdTRUE) {
        abort();
    }
    if (result != ESP_OK) {
        record_sd_failure("read binding", result, 0);
    }
    return result;
}

// mount_point: Verified card root owned by the removable-storage service.
// Exception: Returns real path, identity-write, flush and close errors.
static esp_err_t prepare_sd_mirror_path(const char *mount_point)
{
    char app_dir[128];
    char logs_dir[144];
    const int app_dir_length =
        snprintf(app_dir, sizeof(app_dir), "%s/LUNASHELL", mount_point);
    if (app_dir_length < 0 || (size_t)app_dir_length >= sizeof(app_dir)) {
        record_sd_failure("application path", ESP_ERR_INVALID_SIZE, 0);
        return ESP_ERR_INVALID_SIZE;
    }
    const int logs_dir_length =
        snprintf(logs_dir, sizeof(logs_dir), "%s/logs", app_dir);
    if (logs_dir_length < 0 || (size_t)logs_dir_length >= sizeof(logs_dir)) {
        record_sd_failure("logs path", ESP_ERR_INVALID_SIZE, 0);
        return ESP_ERR_INVALID_SIZE;
    }

    if ((mkdir(app_dir, 0775) != 0) && errno != EEXIST) {
        const int mkdir_errno = errno;
        record_sd_failure("application directory", ESP_FAIL, mkdir_errno);
        return ESP_FAIL;
    }
    if ((mkdir(logs_dir, 0775) != 0) && errno != EEXIST) {
        const int mkdir_errno = errno;
        record_sd_failure("logs directory", ESP_FAIL, mkdir_errno);
        return ESP_FAIL;
    }
    const int log_path_length =
        snprintf(s_log_path, sizeof(s_log_path), "%s/LUNASHELL.log", logs_dir);
    if (log_path_length < 0 || (size_t)log_path_length >= sizeof(s_log_path)) {
        record_sd_failure("log path", ESP_ERR_INVALID_SIZE, 0);
        return ESP_ERR_INVALID_SIZE;
    }
    if (!s_boot_file_prepared) {
        struct stat previous_boot;
        if (stat(s_log_path, &previous_boot) == 0) {
            if (previous_boot.st_size > 0 && !rotate_log_path()) {
                return current_sd_error();
            }
        } else if (errno != ENOENT) {
            const int stat_errno = errno;
            record_sd_failure("boot file stat", ESP_FAIL, stat_errno);
            return ESP_FAIL;
        }
        // A failed identity write must not rotate over the previous boot on retry.
        s_boot_file_prepared = true;
    }
    FILE *probe = fopen(s_log_path, "ab");
    if (probe == NULL) {
        const int open_errno = errno;
        record_sd_failure("probe open", ESP_FAIL, open_errno);
        return ESP_FAIL;
    }
    // Persist the current boot identity before admitting the asynchronous writer.
    esp_err_t result = ESP_OK;
    if (!s_boot_identity_written) {
        if (fprintf(probe, "W (%lld) %s: SD SESSION START reset=%d version=%s\n",
                (long long)(esp_timer_get_time() / 1000), TAG,
                (int)esp_reset_reason(), esp_app_get_description()->version) < 0) {
            record_sd_failure("session identity write", ESP_FAIL, errno);
            result = ESP_FAIL;
        } else if (fflush(probe) != 0) {
            record_sd_failure("session identity flush", ESP_FAIL, errno);
            result = ESP_FAIL;
        } else {
            note_persisted_write();
        }
    }
    if (fclose(probe) != 0) {
        const int close_errno = errno;
        if (result == ESP_OK) {
            record_sd_failure("probe close", ESP_FAIL, close_errno);
            result = ESP_FAIL;
        } else {
            serial_printf("E (%lld) %s: Session probe secondary close failed: errno=%d\n",
                (long long)(esp_timer_get_time() / 1000), TAG, close_errno);
        }
    }
    if (result != ESP_OK) {
        return result;
    }
    s_boot_identity_written = true;
    advance_log_revision();
    return ESP_OK;
}

// operation: Logger initialization stage that actually failed.
// error: Original allocation/USB-driver error, retained for later status queries.
// Exception: Returns the original failure without resetting or retrying startup.
static esp_err_t fail_logger_initialization(const char *operation, esp_err_t error)
{
    s_initialization_result = error;
    record_capture_loss(NULL, error);
    serial_printf("E (%lld) %s: Optional initialization %s failed: %s\n",
        (long long)(esp_timer_get_time() / 1000), TAG, operation, esp_err_to_name(error));
    return error;
}

// No parameters: establish capture, SD ownership primitives and native USB logging.
// Exception: Returns the actual initialization error; startup treats logging as optional.
esp_err_t LUNASHELL_logger_init(void)
{
    if (s_initialization_attempted) {
        return s_initialization_result;
    }
    s_initialization_attempted = true;

    s_snapshot_mutex = xSemaphoreCreateMutexStatic(&s_snapshot_mutex_control);
    if (s_snapshot_mutex == NULL) {
        return fail_logger_initialization("snapshot mutex", ESP_ERR_NO_MEM);
    }

    s_writer_idle = xSemaphoreCreateBinaryStatic(&s_writer_idle_control);
    if (s_writer_idle == NULL) {
        return fail_logger_initialization("writer acknowledgement", ESP_ERR_NO_MEM);
    }

    s_io_mutex = xSemaphoreCreateMutexStatic(&s_io_mutex_control);
    if (s_io_mutex == NULL) {
        return fail_logger_initialization("I/O mutex", ESP_ERR_NO_MEM);
    }
    // Seed the opaque identity per boot: restarting mid-download must not reuse
    // the same small counter merely because the mount order stayed identical.
    s_log_revision = esp_random();
    if (s_log_revision == 0U) {
        s_log_revision = 1U;
    }
    const esp_err_t usb_error = LUNASHELL_logger_usb_init();
    if (usb_error != ESP_OK) {
        return fail_logger_initialization("native USB", usb_error);
    }

    s_write_queue = xQueueCreateStatic(
        LUNASHELL_LOG_QUEUE_LINES,
        sizeof(LUNASHELL_log_line_t),
        s_write_queue_storage,
        &s_write_queue_control
    );
    if (s_write_queue == NULL) {
        return fail_logger_initialization("writer queue", ESP_ERR_NO_MEM);
    }

    if (xTaskCreateWithCaps(
        writer_task,
        "LUNASHELL_log_sd",
        4096,
        NULL,
        2,
        &s_writer_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ) != pdPASS) {
        s_writer_task = NULL;
        return fail_logger_initialization("PSRAM writer task", ESP_ERR_NO_MEM);
    }

    esp_err_t boot_capture_error = ESP_OK;
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
    s_boot_lines = heap_caps_calloc(
        LUNASHELL_BOOT_LOG_LINES, sizeof(*s_boot_lines), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_boot_lines == NULL) {
        // The core ring/USB/SD writer remain usable; publish the optional
        // backlog failure and return its real error to the startup orchestrator.
        boot_capture_error = ESP_ERR_NO_MEM;
        record_capture_loss(NULL, boot_capture_error);
        serial_printf("E (0) %s: Optional boot capture allocation failed: %s bytes=%u\n", TAG,
            esp_err_to_name(boot_capture_error),
            (unsigned int)(LUNASHELL_BOOT_LOG_LINES * sizeof(*s_boot_lines)));
    }
#endif
    (void)esp_log_set_vprintf(LUNASHELL_log_vprintf);
    const int64_t initialized_at_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_sd_lock);
    s_sd_status = (LUNASHELL_logger_sd_status_t){
        .enabled = false,
        .last_error = ESP_OK,
        .last_errno = 0,
        .changed_at_us = initialized_at_us,
        .failure_count = 0,
        .persisted_write_sequence = 0,
    };
    s_sd_generation = 1;
    portEXIT_CRITICAL(&s_sd_lock);
    atomic_store_explicit(
        &s_sd_enqueue_open,
        false,
        memory_order_seq_cst
    );
    atomic_store_explicit(
        &s_sd_enqueue_inflight,
        0U,
        memory_order_seq_cst
    );
    s_initialization_result = boot_capture_error;
    s_initialized = true;
    atomic_store_explicit(&s_capture_suspended, false, memory_order_relaxed);
    // Keep this identity even in Release's warning-only log policy.
    ESP_LOGW(TAG, "BOOT START reset=%d version=%s; capture before non-blocking USB; early-lines=%u",
        (int)esp_reset_reason(), esp_app_get_description()->version,
        (unsigned int)LUNASHELL_BOOT_LOG_LINES);
    return boot_capture_error;
}

// No parameters: seal records under their producer lock, then wake their sole owner.
// Exception: Returns the original state/notification/archive failure; no flush is awaited.
esp_err_t LUNASHELL_logger_finish_boot_capture(void)
{
    if (!s_initialized) {
        const esp_err_t error = s_initialization_attempted
            ? s_initialization_result : ESP_ERR_INVALID_STATE;
        serial_printf("E (0) %s: Boot capture finish rejected: %s\n", TAG, esp_err_to_name(error));
        return error;
    }
    if (atomic_exchange_explicit(&s_boot_archive_started, true, memory_order_acq_rel)) {
        return atomic_load_explicit(&s_boot_archive_completed, memory_order_acquire)
            ? atomic_load_explicit(&s_boot_archive_error, memory_order_relaxed) : ESP_OK;
    }
    portENTER_CRITICAL(&s_boot_lock);
    s_boot_collecting = false;
    portEXIT_CRITICAL(&s_boot_lock);
    atomic_store_explicit(&s_boot_archive_requested, true, memory_order_release);
    const esp_err_t notification_error = notify_writer_state_change("boot-archive");
    if (notification_error != ESP_OK) {
        record_capture_loss(NULL, notification_error);
        atomic_store_explicit(&s_boot_archive_error, notification_error, memory_order_relaxed);
        atomic_store_explicit(&s_boot_archive_completed, true, memory_order_release);
    }
    return notification_error;
}

// timeout_ms: Bounded startup wait; completion belongs exclusively to the writer.
// Exception: Returns original archive/init/notification failure or the actual wait timeout.
esp_err_t LUNASHELL_logger_finish_boot_capture_wait(uint32_t timeout_ms)
{
    if (timeout_ms == 0U || timeout_ms > 5000U) {
        ESP_LOGE(TAG, "Boot archive wait rejected: timeout_ms=%lu (%s)",
            (unsigned long)timeout_ms, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t request_error = LUNASHELL_logger_finish_boot_capture();
    if (request_error != ESP_OK) {
        return request_error;
    }
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (!atomic_load_explicit(&s_boot_archive_completed, memory_order_acquire)) {
        if (esp_timer_get_time() >= deadline_us) {
            ESP_LOGE(TAG, "Boot archive completion timed out after %lu ms; owner retains pending work (%s)",
                (unsigned long)timeout_ms, esp_err_to_name(ESP_ERR_TIMEOUT));
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
    return atomic_load_explicit(&s_boot_archive_error, memory_order_relaxed);
}

esp_err_t LUNASHELL_logger_set_io_watch(
    const LUNASHELL_logger_io_watch_t *watch
)
{
    if (!s_initialized) {
        serial_printf(
            "E (0) %s: Cannot configure SD I/O liveness before logger init\n",
            TAG
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (watch == NULL || watch->begin == NULL || watch->end == NULL) {
        serial_printf(
            "E (%lld) %s: Invalid SD I/O liveness hooks\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG
        );
        return ESP_ERR_INVALID_ARG;
    }

    LUNASHELL_logger_sd_status_t status;
    read_sd_state(&status, NULL);
    if (status.enabled) {
        serial_printf(
            "E (%lld) %s: Cannot replace SD I/O liveness while mirroring is active\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (atomic_load_explicit(
            &s_io_watch_configured,
            memory_order_acquire
        )) {
        if (s_io_watch.begin == watch->begin &&
            s_io_watch.end == watch->end) {
            return ESP_OK;
        }
        serial_printf(
            "E (%lld) %s: SD I/O liveness hooks are immutable after registration\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG
        );
        return ESP_ERR_INVALID_STATE;
    }

    s_io_watch = *watch;
    atomic_store_explicit(
        &s_io_watch_configured,
        true,
        memory_order_release
    );
    return ESP_OK;
}

// mount_point identifies the card already mounted by the storage owner.
// Returns the real stop, filesystem or liveness failure; no card is formatted here.
esp_err_t LUNASHELL_logger_enable_sd(const char *mount_point)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mount_point == NULL || mount_point[0] == '\0') {
        record_sd_failure("enable arguments", ESP_ERR_INVALID_ARG, 0);
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t stop_result = stop_sd_writer();
    if (stop_result != ESP_OK) {
        return stop_result;
    }

    const esp_err_t watch_error = begin_watched_io("enable");
    if (watch_error != ESP_OK) {
        record_sd_failure("enable liveness", watch_error, 0);
        return watch_error;
    }
    const esp_err_t prepare_error = prepare_sd_mirror_path(mount_point);
    const esp_err_t watch_end_error = end_watched_io("enable", prepare_error);
    if (watch_end_error != ESP_OK) {
        if (prepare_error == ESP_OK) {
            record_sd_failure(
                "enable liveness completion",
                watch_end_error,
                0
            );
        }
        return watch_end_error;
    }

    const esp_err_t enable_error = set_sd_enabled();
    if (enable_error != ESP_OK) {
        record_sd_failure("enable notification", enable_error, 0);
        return enable_error;
    }
    ESP_LOGI(TAG, "SD mirror enabled: %s", s_log_path);
    return ESP_OK;
}

esp_err_t LUNASHELL_logger_disable_sd(void)
{
    if (!s_initialized) {
        serial_printf("E (0) %s: Cannot disable SD mirror before logger init\n", TAG);
        return ESP_ERR_INVALID_STATE;
    }
    return stop_sd_writer();
}

esp_err_t LUNASHELL_logger_set_capture_suspended(bool suspended)
{
    if (!s_initialized) {
        serial_printf(
            "E (0) %s: Cannot change RAM capture before logger init\n",
            TAG
        );
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store_explicit(
        &s_capture_suspended,
        suspended,
        memory_order_relaxed
    );
    ESP_LOGI(
        TAG,
        "RAM log capture: %s; serial output remains active",
        suspended ? "suspended" : "active"
    );
    return ESP_OK;
}

esp_err_t LUNASHELL_logger_clear(void)
{
    if (!s_initialized) {
        serial_printf("E (0) %s: Cannot clear RAM ring before logger init\n", TAG);
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_ring_lock);
    memset(s_ring, 0, sizeof(s_ring));
    s_ring_head = 0U;
    s_ring_count = 0U;
    portEXIT_CRITICAL(&s_ring_lock);
    return ESP_OK;
}

esp_err_t LUNASHELL_logger_snapshot(
    char *buffer,
    size_t buffer_size,
    size_t max_lines,
    size_t *bytes_written
)
{
    if (buffer == NULL || buffer_size == 0U || max_lines == 0U ||
        max_lines > LUNASHELL_LOGGER_SNAPSHOT_MAX_LINES ||
        bytes_written == NULL) {
        serial_printf("E (0) %s: Invalid RAM ring snapshot arguments\n", TAG);
        return ESP_ERR_INVALID_ARG;
    }
    *bytes_written = 0U;
    buffer[0] = '\0';

    if (!s_initialized) {
        serial_printf("E (0) %s: Cannot read RAM ring before logger init\n", TAG);
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_snapshot_mutex, 0) != pdTRUE) {
        ESP_LOGE(TAG, "RAM ring snapshot busy");
        return ESP_ERR_TIMEOUT;
    }

    size_t copy_count;
    size_t first;

    portENTER_CRITICAL(&s_ring_lock);
    copy_count = s_ring_count < max_lines ? s_ring_count : max_lines;
    first = (s_ring_head + LUNASHELL_LOG_RING_LINES - copy_count) % LUNASHELL_LOG_RING_LINES;
    for (size_t i = 0; i < copy_count; ++i) {
        s_snapshot_copy[i] = s_ring[(first + i) % LUNASHELL_LOG_RING_LINES];
    }
    portEXIT_CRITICAL(&s_ring_lock);

    esp_err_t result = ESP_OK;
    size_t used = 0;
    for (size_t i = 0; i < copy_count && used + 1U < buffer_size; ++i) {
        const char *text = s_snapshot_copy[i].text;
        const size_t available = buffer_size - used;
        const int written = snprintf(buffer + used, available, "%s%s", text,
                                     (text[0] != '\0' && text[strlen(text) - 1U] == '\n') ? "" : "\n");
        if (written < 0) {
            result = ESP_FAIL;
            break;
        }
        if ((size_t)written >= available) {
            used = buffer_size - 1U;
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        used += (size_t)written;
    }
    buffer[used] = '\0';
    *bytes_written = used;
    if (xSemaphoreGive(s_snapshot_mutex) != pdTRUE) {
        ESP_LOGE(TAG, "RAM ring snapshot mutex release failed");
        return ESP_ERR_INVALID_STATE;
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "RAM ring snapshot formatting failed");
    }
    return result;
}

// file selects current or previous; no arbitrary filesystem paths are accepted.
// offset is the requested byte position.
// expected_revision pins the file identity after the initial zero-revision read.
// buffer receives a raw byte block, bounded by buffer_size.
// buffer_size is the caller's available capacity, at most 128 bytes.
// bytes_read receives the successful byte count, zero on failure.
// total_size receives this read's persisted file size.
// revision receives the identity protected by the logger I/O mutex.
// system_errno receives the original filesystem errno when applicable.
// Returns real argument, identity, filesystem or liveness errors.
esp_err_t LUNASHELL_logger_read_sd_log(
    LUNASHELL_logger_log_file_t file, uint32_t offset,
    uint32_t expected_revision, void *buffer, size_t buffer_size,
    size_t *bytes_read, uint32_t *total_size, uint32_t *revision,
    int *system_errno
)
{
    if (!s_initialized || buffer == NULL || buffer_size == 0U ||
        buffer_size > 128U || bytes_read == NULL || total_size == NULL ||
        revision == NULL || system_errno == NULL ||
        (file != LUNASHELL_LOG_FILE_CURRENT && file != LUNASHELL_LOG_FILE_PREVIOUS)) {
        serial_printf("E (%lld) %s: SD read arguments/state rejected\n",
            (long long)(esp_timer_get_time() / 1000), TAG);
        return ESP_ERR_INVALID_ARG;
    }
    *bytes_read = 0U;
    *total_size = 0U;
    *revision = 0U;
    *system_errno = 0;
    esp_err_t result = begin_watched_io("download read");
    if (result != ESP_OK) {
        return result;
    }
    *revision = s_log_revision;
    FILE *input = NULL;
    char path[sizeof(s_log_path) + 4U];
    if (expected_revision != 0U && expected_revision != s_log_revision) {
        result = ESP_ERR_INVALID_STATE;
    } else if (s_log_path[0] == '\0') {
        result = ESP_ERR_INVALID_STATE;
    } else {
        const int length = snprintf(path, sizeof(path), "%s%s", s_log_path,
            file == LUNASHELL_LOG_FILE_PREVIOUS ? ".1" : "");
        if (length < 0 || (size_t)length >= sizeof(path)) {
            result = ESP_ERR_INVALID_SIZE;
        } else {
            input = fopen(path, "rb");
            if (input == NULL) {
                *system_errno = errno;
                result = *system_errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
            }
        }
    }
    if (result == ESP_OK && fseek(input, 0L, SEEK_END) != 0) {
        *system_errno = errno;
        result = ESP_FAIL;
    }
    if (result == ESP_OK) {
        const long length = ftell(input);
        if (length < 0L) {
            *system_errno = errno;
            result = ESP_FAIL;
        } else if ((unsigned long)length > LUNASHELL_LOG_FILE_LIMIT_BYTES + 4096U) {
            result = ESP_ERR_INVALID_SIZE;
        } else {
            *total_size = (uint32_t)length;
            if (offset > *total_size) {
                result = ESP_ERR_INVALID_ARG;
            } else if (fseek(input, (long)offset, SEEK_SET) != 0) {
                *system_errno = errno;
                result = ESP_FAIL;
            }
        }
    }
    if (result == ESP_OK) {
        const size_t remaining = *total_size - offset;
        const size_t requested = remaining < buffer_size ? remaining : buffer_size;
        *bytes_read = fread(buffer, 1U, requested, input);
        if (*bytes_read != requested || ferror(input)) {
            *system_errno = errno;
            result = ESP_FAIL;
        }
    }
    if (input != NULL && fclose(input) != 0) {
        const int close_errno = errno;
        serial_printf("E (%lld) %s: SD download close failed: errno=%d\n",
            (long long)(esp_timer_get_time() / 1000), TAG, close_errno);
        if (result == ESP_OK) {
            *system_errno = close_errno;
            result = ESP_FAIL;
        }
    }
    result = end_watched_io("download read", result);
    if (result != ESP_OK) {
        *bytes_read = 0U;
        serial_printf("E (%lld) %s: SD download read failed: %s errno=%d\n",
            (long long)(esp_timer_get_time() / 1000), TAG,
            esp_err_to_name(result), *system_errno);
    }
    return result;
}

// status receives the real asynchronous writer state and retained capture loss.
// Exception: Returns the original initialization or argument/state error if status is unavailable.
esp_err_t LUNASHELL_logger_get_sd_status(LUNASHELL_logger_sd_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return s_initialization_attempted ? s_initialization_result : ESP_ERR_INVALID_STATE;
    }

    read_sd_state(status, NULL);
    portENTER_CRITICAL(&s_boot_lock);
    status->boot_dropped_lines = s_boot_dropped_total;
    status->boot_retained_bytes = s_boot_lines != NULL && !s_boot_lines_external
        ? LUNASHELL_BOOT_LOG_LINES * sizeof(*s_boot_lines) : 0U;
    status->boot_archive_bytes = s_boot_lines != NULL && s_boot_lines_external
        ? s_boot_count * sizeof(*s_boot_lines) : 0U;
    portEXIT_CRITICAL(&s_boot_lock);
    portENTER_CRITICAL(&s_capture_status_lock);
    status->format_dropped_lines = s_format_dropped_lines;
    status->queue_dropped_lines = s_queue_dropped_lines;
    status->truncated_lines = s_truncated_lines;
    status->capture_error = s_capture_error;
    status->capture_error_at_us = s_capture_error_at_us;
    portEXIT_CRITICAL(&s_capture_status_lock);
    status->usb_dropped_lines = LUNASHELL_logger_usb_dropped_lines();
    status->sampled_at_us = esp_timer_get_time();
    return ESP_OK;
}
