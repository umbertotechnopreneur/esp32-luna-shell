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

#include "qsee_logger.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define QSEE_LOG_RING_LINES 32
#define QSEE_LOG_LINE_BYTES 256
#define QSEE_LOG_QUEUE_LINES 48
#define QSEE_LOG_FORMAT_SLOTS 4
#define QSEE_LOG_FILE_LIMIT_BYTES (2U * 1024U * 1024U)

typedef struct {
    char text[QSEE_LOG_LINE_BYTES];
} qsee_log_line_t;

static const char *TAG = "qsee_logger";
static qsee_log_line_t s_ring[QSEE_LOG_RING_LINES];
static qsee_log_line_t s_format_slots[QSEE_LOG_FORMAT_SLOTS];
static bool s_format_slot_used[QSEE_LOG_FORMAT_SLOTS];
static qsee_log_line_t s_snapshot_copy[QSEE_LOG_RING_LINES];
static size_t s_ring_head;
static size_t s_ring_count;
static portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_format_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_snapshot_mutex;
static StaticSemaphore_t s_snapshot_mutex_control;
static QueueHandle_t s_write_queue;
static StaticQueue_t s_write_queue_control;
static uint8_t s_write_queue_storage[QSEE_LOG_QUEUE_LINES * sizeof(qsee_log_line_t)];
static StaticTask_t s_writer_task_control;
static StackType_t s_writer_task_stack[4096];
static TaskHandle_t s_writer_task;
static vprintf_like_t s_serial_vprintf;
static char s_log_path[160];
static volatile bool s_sd_enabled;
static bool s_initialized;

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

    qsee_log_line_t *line = acquire_format_slot();
    if (line == NULL) {
        return serial_result;
    }

    va_copy(capture_args, args);
    vsnprintf(line->text, sizeof(line->text), format, capture_args);
    va_end(capture_args);

    ring_push(line->text);
    if (s_write_queue != NULL) {
        (void)xQueueSend(s_write_queue, line, 0);
    }
    release_format_slot(line);

    return serial_result;
}

static void rotate_log_if_needed(FILE **file)
{
    const long position = ftell(*file);
    if (position < 0 || (unsigned long)position < QSEE_LOG_FILE_LIMIT_BYTES) {
        return;
    }

    fclose(*file);
    *file = NULL;

    char rotated_path[sizeof(s_log_path) + 4];
    snprintf(rotated_path, sizeof(rotated_path), "%s.1", s_log_path);
    (void)remove(rotated_path);
    (void)rename(s_log_path, rotated_path);
}

static void writer_task(void *arg)
{
    (void)arg;
    FILE *file = NULL;
    unsigned int lines_since_flush = 0;
    qsee_log_line_t line;

    while (true) {
        if (!s_sd_enabled) {
            if (file != NULL) {
                (void)fclose(file);
                file = NULL;
                lines_since_flush = 0;
            }
            while (xQueueReceive(s_write_queue, &line, 0) == pdTRUE) {
                /* Drop queued SD copies while the card is unavailable. */
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (xQueueReceive(s_write_queue, &line, pdMS_TO_TICKS(500)) != pdTRUE) {
            if (file != NULL && lines_since_flush > 0U) {
                if (fflush(file) != 0) {
                    (void)fclose(file);
                    file = NULL;
                    s_sd_enabled = false;
                }
                lines_since_flush = 0;
            }
            continue;
        }

        if (file == NULL) {
            file = fopen(s_log_path, "ab");
            if (file == NULL) {
                s_sd_enabled = false;
                continue;
            }
            if (fseek(file, 0, SEEK_END) != 0) {
                (void)fclose(file);
                file = NULL;
                s_sd_enabled = false;
                continue;
            }
        }

        if (fputs(line.text, file) == EOF) {
            (void)fclose(file);
            file = NULL;
            s_sd_enabled = false;
            continue;
        }
        const size_t length = strlen(line.text);
        if (length == 0U || line.text[length - 1U] != '\n') {
            if (fputc('\n', file) == EOF) {
                (void)fclose(file);
                file = NULL;
                s_sd_enabled = false;
                continue;
            }
        }

        lines_since_flush++;
        if (lines_since_flush >= 8U) {
            if (fflush(file) != 0) {
                (void)fclose(file);
                file = NULL;
                s_sd_enabled = false;
                lines_since_flush = 0;
                continue;
            }
            lines_since_flush = 0;
            rotate_log_if_needed(&file);
        }
    }
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

    s_write_queue = xQueueCreateStatic(
        QSEE_LOG_QUEUE_LINES,
        sizeof(qsee_log_line_t),
        s_write_queue_storage,
        &s_write_queue_control
    );
    if (s_write_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_writer_task = xTaskCreateStatic(
        writer_task,
        "qsee_log_sd",
        sizeof(s_writer_task_stack) / sizeof(s_writer_task_stack[0]),
        NULL,
        2,
        s_writer_task_stack,
        &s_writer_task_control
    );
    if (s_writer_task == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_serial_vprintf = esp_log_set_vprintf(qsee_log_vprintf);
    s_initialized = true;
    ESP_LOGI(TAG, "Canonical logger online: serial + RAM ring");
    return ESP_OK;
}

esp_err_t qsee_logger_enable_sd(const char *mount_point)
{
    if (!s_initialized || mount_point == NULL || mount_point[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char app_dir[128];
    char logs_dir[144];
    snprintf(app_dir, sizeof(app_dir), "%s/qsee", mount_point);
    snprintf(logs_dir, sizeof(logs_dir), "%s/logs", app_dir);

    if ((mkdir(app_dir, 0775) != 0) && errno != EEXIST) {
        return ESP_FAIL;
    }
    if ((mkdir(logs_dir, 0775) != 0) && errno != EEXIST) {
        return ESP_FAIL;
    }

    snprintf(s_log_path, sizeof(s_log_path), "%s/qsee.log", logs_dir);
    FILE *probe = fopen(s_log_path, "ab");
    if (probe == NULL) {
        return ESP_FAIL;
    }
    fclose(probe);

    s_sd_enabled = true;
    ESP_LOGI(TAG, "SD mirror enabled: %s", s_log_path);
    return ESP_OK;
}

void qsee_logger_disable_sd(void)
{
    s_sd_enabled = false;
}

void qsee_logger_clear(void)
{
    portENTER_CRITICAL(&s_ring_lock);
    memset(s_ring, 0, sizeof(s_ring));
    s_ring_head = 0U;
    s_ring_count = 0U;
    portEXIT_CRITICAL(&s_ring_lock);
}

size_t qsee_logger_snapshot(char *buffer, size_t buffer_size, size_t max_lines)
{
    if (buffer == NULL || buffer_size == 0U || max_lines == 0U) {
        return 0;
    }

    if (xSemaphoreTake(s_snapshot_mutex, 0) != pdTRUE) {
        return 0;
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

    size_t used = 0;
    buffer[0] = '\0';
    for (size_t i = 0; i < copy_count && used + 1U < buffer_size; ++i) {
        const char *text = s_snapshot_copy[i].text;
        const size_t available = buffer_size - used;
        const int written = snprintf(buffer + used, available, "%s%s", text,
                                     (text[0] != '\0' && text[strlen(text) - 1U] == '\n') ? "" : "\n");
        if (written < 0) {
            break;
        }
        if ((size_t)written >= available) {
            used = buffer_size - 1U;
            break;
        }
        used += (size_t)written;
    }
    buffer[used] = '\0';
    xSemaphoreGive(s_snapshot_mutex);
    return used;
}

bool qsee_logger_sd_enabled(void)
{
    return s_sd_enabled;
}
