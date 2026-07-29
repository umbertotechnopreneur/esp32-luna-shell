/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

// Canonical logging backend for the firmware runtime.
// Serializes log output and protects early-boot initialization ordering.

#include "qsee_logger.h"

#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define QSEE_LOG_RING_LINES QSEE_LOGGER_SNAPSHOT_MAX_LINES
#define QSEE_LOG_LINE_BYTES 256
#define QSEE_LOG_QUEUE_LINES 48
#define QSEE_LOG_FORMAT_SLOTS 4
#define QSEE_LOG_FILE_LIMIT_BYTES (2U * 1024U * 1024U)

typedef struct {
    char text[QSEE_LOG_LINE_BYTES];
} qsee_ram_log_line_t;

typedef struct {
    char text[QSEE_LOG_LINE_BYTES];
    uint32_t sd_generation;
} qsee_log_line_t;

static const char *TAG = "qsee_logger";
static qsee_ram_log_line_t s_ring[QSEE_LOG_RING_LINES];
static qsee_log_line_t s_format_slots[QSEE_LOG_FORMAT_SLOTS];
static bool s_format_slot_used[QSEE_LOG_FORMAT_SLOTS];
// Snapshot export runs outside the immediate vprintf path, so its bulk copy
// does not need the cache-off-safe internal memory used by the live log ring.
static EXT_RAM_BSS_ATTR
qsee_ram_log_line_t s_snapshot_copy[QSEE_LOGGER_SNAPSHOT_MAX_LINES];
static size_t s_ring_head;
static size_t s_ring_count;
static portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_format_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_snapshot_mutex;
static StaticSemaphore_t s_snapshot_mutex_control;
static SemaphoreHandle_t s_writer_idle;
static StaticSemaphore_t s_writer_idle_control;
static QueueHandle_t s_write_queue;
static StaticQueue_t s_write_queue_control;
static uint8_t s_write_queue_storage[QSEE_LOG_QUEUE_LINES * sizeof(qsee_log_line_t)];
static TaskHandle_t s_writer_task;
static vprintf_like_t s_serial_vprintf;
static char s_log_path[160];
static qsee_logger_sd_status_t s_sd_status;
static uint32_t s_sd_generation;
static portMUX_TYPE s_sd_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;
static atomic_bool s_capture_suspended;

static int serial_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = s_serial_vprintf != NULL
        ? s_serial_vprintf(format, args)
        : vprintf(format, args);
    va_end(args);
    return result;
}

static void read_sd_state(qsee_logger_sd_status_t *status, uint32_t *generation)
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

static void set_sd_enabled(void)
{
    const int64_t changed_at_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_sd_lock);
    s_sd_status.enabled = true;
    s_sd_status.last_error = ESP_OK;
    s_sd_status.last_errno = 0;
    s_sd_status.changed_at_us = changed_at_us;
    s_sd_generation++;
    portEXIT_CRITICAL(&s_sd_lock);
}

static void set_sd_disabled(void)
{
    const int64_t changed_at_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_sd_lock);
    s_sd_status.enabled = false;
    s_sd_status.changed_at_us = changed_at_us;
    portEXIT_CRITICAL(&s_sd_lock);
}

static void record_sd_failure(const char *operation, esp_err_t error, int system_errno)
{
    const int64_t changed_at_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_sd_lock);
    s_sd_status.enabled = false;
    s_sd_status.last_error = error;
    s_sd_status.last_errno = system_errno;
    s_sd_status.changed_at_us = changed_at_us;
    s_sd_status.failure_count++;
    portEXIT_CRITICAL(&s_sd_lock);

    serial_printf(
        "E (%lld) %s: SD mirror %s failed: %s (errno=%d)\n",
        (long long)(changed_at_us / 1000),
        TAG,
        operation,
        esp_err_to_name(error),
        system_errno
    );
}

static void ring_push(const char *line)
{
    portENTER_CRITICAL(&s_ring_lock);
    strlcpy(s_ring[s_ring_head].text, line, sizeof(s_ring[s_ring_head].text));
    s_ring_head = (s_ring_head + 1U) % QSEE_LOG_RING_LINES;
    if (s_ring_count < QSEE_LOG_RING_LINES) {
        s_ring_count++;
    }
    portEXIT_CRITICAL(&s_ring_lock);
}

static qsee_log_line_t *acquire_format_slot(void)
{
    qsee_log_line_t *slot = NULL;
    portENTER_CRITICAL(&s_format_lock);
    for (size_t i = 0; i < QSEE_LOG_FORMAT_SLOTS; ++i) {
        if (!s_format_slot_used[i]) {
            s_format_slot_used[i] = true;
            slot = &s_format_slots[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_format_lock);
    return slot;
}

static void release_format_slot(qsee_log_line_t *slot)
{
    if (slot == NULL) {
        return;
    }

    const size_t index = (size_t)(slot - s_format_slots);
    if (index >= QSEE_LOG_FORMAT_SLOTS) {
        return;
    }

    portENTER_CRITICAL(&s_format_lock);
    s_format_slot_used[index] = false;
    portEXIT_CRITICAL(&s_format_lock);
}

static int qsee_log_vprintf(const char *format, va_list args)
{
    va_list serial_args;
    va_list capture_args;

    va_copy(serial_args, args);
    const int serial_result = s_serial_vprintf != NULL
        ? s_serial_vprintf(format, serial_args)
        : vprintf(format, serial_args);
    va_end(serial_args);

    if (atomic_load_explicit(&s_capture_suspended, memory_order_relaxed)) {
        return serial_result;
    }

    qsee_log_line_t *line = acquire_format_slot();
    if (line == NULL) {
        qsee_logger_sd_status_t sd_status;
        read_sd_state(&sd_status, NULL);
        if (sd_status.enabled) {
            record_sd_failure("format slot", ESP_ERR_NO_MEM, 0);
        } else {
            serial_printf(
                "E (%lld) %s: Log capture format slots exhausted; serial output only\n",
                (long long)(esp_timer_get_time() / 1000),
                TAG
            );
        }
        return serial_result;
    }

    va_copy(capture_args, args);
    const int capture_result =
        vsnprintf(line->text, sizeof(line->text), format, capture_args);
    va_end(capture_args);
    if (capture_result < 0) {
        serial_printf(
            "E (%lld) %s: Log capture formatting failed; serial output only\n",
            (long long)(esp_timer_get_time() / 1000),
            TAG
        );
        release_format_slot(line);
        return serial_result;
    }
    if ((size_t)capture_result >= sizeof(line->text)) {
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
    qsee_logger_sd_status_t sd_status;
    uint32_t sd_generation;
    read_sd_state(&sd_status, &sd_generation);
    line->sd_generation = sd_generation;
    if (sd_status.enabled && s_write_queue != NULL &&
        xQueueSend(s_write_queue, line, 0) != pdTRUE) {
        record_sd_failure("queue", ESP_ERR_NO_MEM, 0);
    }
    release_format_slot(line);

    return serial_result;
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

static bool rotate_log_if_needed(FILE **file)
{
    const long position = ftell(*file);
    if (position < 0) {
        const int position_errno = errno;
        record_sd_failure("size check", ESP_FAIL, position_errno);
        return false;
    }
    if ((unsigned long)position < QSEE_LOG_FILE_LIMIT_BYTES) {
        return true;
    }

    if (!close_log_file(file, "rotation close")) {
        return false;
    }

    char rotated_path[sizeof(s_log_path) + 4];
    const int rotated_length =
        snprintf(rotated_path, sizeof(rotated_path), "%s.1", s_log_path);
    if (rotated_length < 0 || (size_t)rotated_length >= sizeof(rotated_path)) {
        record_sd_failure("rotation path", ESP_ERR_INVALID_SIZE, 0);
        return false;
    }
    if (remove(rotated_path) != 0 && errno != ENOENT) {
        const int remove_errno = errno;
        record_sd_failure("rotation remove", ESP_FAIL, remove_errno);
        return false;
    }
    if (rename(s_log_path, rotated_path) != 0) {
        const int rename_errno = errno;
        record_sd_failure("rotation rename", ESP_FAIL, rename_errno);
        return false;
    }
    return true;
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
    *lines_since_flush = 0;
    return true;
}

static bool write_log_line(
    FILE **file,
    unsigned int *lines_since_flush,
    const qsee_log_line_t *line
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

static void writer_task(void *arg)
{
    (void)arg;
    FILE *file = NULL;
    unsigned int lines_since_flush = 0;
    qsee_log_line_t line;
    uint32_t active_generation = 0;

    while (true) {
        qsee_logger_sd_status_t sd_status;
        uint32_t generation;
        read_sd_state(&sd_status, &generation);

        if (generation != active_generation) {
            if (file != NULL) {
                if (!flush_log_file(
                        &file,
                        &lines_since_flush,
                        "configuration flush"
                    ) ||
                    !close_log_file(&file, "configuration close")) {
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
                while (xQueueReceive(s_write_queue, &line, 0) == pdTRUE) {
                    if (line.sd_generation == active_generation &&
                        !write_log_line(&file, &lines_since_flush, &line)) {
                        break;
                    }
                }
            } else {
                while (xQueueReceive(s_write_queue, &line, 0) == pdTRUE) {
                    /* The recorded writer failure prevents safe persistence. */
                }
            }
            flush_log_file(&file, &lines_since_flush, "stop flush");
            close_log_file(&file, "stop close");
            if (s_writer_idle != NULL) {
                const BaseType_t acknowledged = xSemaphoreGive(s_writer_idle);
                if (acknowledged != pdTRUE) {
                    /* A binary acknowledgement is already pending. */
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (xQueueReceive(s_write_queue, &line, pdMS_TO_TICKS(100)) != pdTRUE) {
            flush_log_file(&file, &lines_since_flush, "idle flush");
            continue;
        }
        if (line.sd_generation != active_generation) {
            continue;
        }

        write_log_line(&file, &lines_since_flush, &line);
    }
}

static esp_err_t stop_sd_writer(void)
{
    qsee_logger_sd_status_t before;
    read_sd_state(&before, NULL);

    while (xSemaphoreTake(s_writer_idle, 0) == pdTRUE) {
        /* Remove a stale idle acknowledgement before requesting the stop. */
    }
    set_sd_disabled();
    if (xSemaphoreTake(s_writer_idle, pdMS_TO_TICKS(1000)) != pdTRUE) {
        record_sd_failure("stop acknowledgement", ESP_ERR_TIMEOUT, 0);
        return ESP_ERR_TIMEOUT;
    }

    qsee_logger_sd_status_t after;
    read_sd_state(&after, NULL);
    if (after.failure_count != before.failure_count) {
        return after.last_error;
    }
    return ESP_OK;
}

esp_err_t qsee_logger_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_snapshot_mutex = xSemaphoreCreateMutexStatic(&s_snapshot_mutex_control);
    if (s_snapshot_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_writer_idle = xSemaphoreCreateBinaryStatic(&s_writer_idle_control);
    if (s_writer_idle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_write_queue = xQueueCreateStatic(
        QSEE_LOG_QUEUE_LINES,
        sizeof(qsee_log_line_t),
        s_write_queue_storage,
        &s_write_queue_control
    );
    if (s_write_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreateWithCaps(
        writer_task,
        "qsee_log_sd",
        4096,
        NULL,
        2,
        &s_writer_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ) != pdPASS) {
        s_writer_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_serial_vprintf = esp_log_set_vprintf(qsee_log_vprintf);
    const int64_t initialized_at_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_sd_lock);
    s_sd_status = (qsee_logger_sd_status_t){
        .enabled = false,
        .last_error = ESP_OK,
        .last_errno = 0,
        .changed_at_us = initialized_at_us,
        .failure_count = 0,
    };
    s_sd_generation = 1;
    portEXIT_CRITICAL(&s_sd_lock);
    s_initialized = true;
    atomic_store_explicit(&s_capture_suspended, false, memory_order_relaxed);
    ESP_LOGI(TAG, "Canonical logger online: serial + RAM ring");
    return ESP_OK;
}

esp_err_t qsee_logger_enable_sd(const char *mount_point)
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

    char app_dir[128];
    char logs_dir[144];
    const int app_dir_length =
        snprintf(app_dir, sizeof(app_dir), "%s/qsee", mount_point);
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
        snprintf(s_log_path, sizeof(s_log_path), "%s/qsee.log", logs_dir);
    if (log_path_length < 0 || (size_t)log_path_length >= sizeof(s_log_path)) {
        record_sd_failure("log path", ESP_ERR_INVALID_SIZE, 0);
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *probe = fopen(s_log_path, "ab");
    if (probe == NULL) {
        const int open_errno = errno;
        record_sd_failure("probe open", ESP_FAIL, open_errno);
        return ESP_FAIL;
    }
    if (fclose(probe) != 0) {
        const int close_errno = errno;
        record_sd_failure("probe close", ESP_FAIL, close_errno);
        return ESP_FAIL;
    }

    set_sd_enabled();
    ESP_LOGI(TAG, "SD mirror enabled: %s", s_log_path);
    return ESP_OK;
}

esp_err_t qsee_logger_disable_sd(void)
{
    if (!s_initialized) {
        serial_printf("E (0) %s: Cannot disable SD mirror before logger init\n", TAG);
        return ESP_ERR_INVALID_STATE;
    }
    return stop_sd_writer();
}

esp_err_t qsee_logger_set_capture_suspended(bool suspended)
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

esp_err_t qsee_logger_clear(void)
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

esp_err_t qsee_logger_snapshot(
    char *buffer,
    size_t buffer_size,
    size_t max_lines,
    size_t *bytes_written
)
{
    if (buffer == NULL || buffer_size == 0U || max_lines == 0U ||
        max_lines > QSEE_LOGGER_SNAPSHOT_MAX_LINES ||
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
    first = (s_ring_head + QSEE_LOG_RING_LINES - copy_count) % QSEE_LOG_RING_LINES;
    for (size_t i = 0; i < copy_count; ++i) {
        s_snapshot_copy[i] = s_ring[(first + i) % QSEE_LOG_RING_LINES];
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

esp_err_t qsee_logger_get_sd_status(qsee_logger_sd_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    read_sd_state(status, NULL);
    return ESP_OK;
}
