#include "LUNASHELL_logger_usb.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LOG_USB_BLOCK_BYTES 128U
#define LOG_USB_COMMAND_BYTES 112U
#define LOG_USB_OUTPUT_BYTES 768U

static SemaphoreHandle_t s_tx_mutex;
static StaticSemaphore_t s_tx_mutex_control;
static TaskHandle_t s_command_task;
static LUNASHELL_logger_sd_reader_t s_sd_reader;
static atomic_bool s_reader_ready;
static atomic_uint s_dropped_serial_lines;

// No parameters: expose serial loss rather than treating delivery as guaranteed.
uint32_t LUNASHELL_logger_usb_dropped_lines(void)
{
    return atomic_load_explicit(&s_dropped_serial_lines, memory_order_relaxed);
}

// format is a bounded firmware diagnostic format.
// args belongs to the caller and is consumed only during this call.
int LUNASHELL_logger_usb_vprintf(const char *format, va_list args)
{
    char text[LOG_USB_OUTPUT_BYTES];
    const int length = vsnprintf(text, sizeof(text), format, args);
    if (length <= 0) {
        return length;
    }
    const size_t available = (size_t)length < sizeof(text)
        ? (size_t)length : sizeof(text) - 1U;
    // A connected USB cable does not mean the host is consuming serial data.
    // Offline logging must never wait for that host to drain its TX queue.
    if (s_tx_mutex == NULL ||
        xSemaphoreTake(s_tx_mutex, 0) != pdTRUE) {
        atomic_fetch_add_explicit(&s_dropped_serial_lines, 1U, memory_order_relaxed);
        return length;
    }
    const int sent = usb_serial_jtag_write_bytes(text, available, 0);
    if (sent != (int)available || (size_t)length >= sizeof(text)) {
        atomic_fetch_add_explicit(&s_dropped_serial_lines, 1U, memory_order_relaxed);
    }
    if (xSemaphoreGive(s_tx_mutex) != pdTRUE) {
        abort(); // A corrupted ownership primitive cannot safely serialize USB.
    }
    return length;
}

// reader supplies the storage owner's mounted-card lifetime guard.
// Returns the real argument/state error without replacing an existing owner.
esp_err_t LUNASHELL_logger_set_sd_reader(LUNASHELL_logger_sd_reader_t reader)
{
    if (reader == NULL) {
        ESP_LOGE("LUNASHELL_log_usb", "SD reader rejected: ESP_ERR_INVALID_ARG");
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_load_explicit(&s_reader_ready, memory_order_acquire)) {
        if (s_sd_reader == reader) {
            return ESP_OK;
        }
        ESP_LOGE("LUNASHELL_log_usb", "SD reader owner replacement rejected");
        return ESP_ERR_INVALID_STATE;
    }
    // Only the startup storage owner registers this callback; the receiver
    // observes it only after the release/acquire publication below.
    s_sd_reader = reader;
    atomic_store_explicit(&s_reader_ready, true, memory_order_release);
    return ESP_OK;
}

// text contains only unsigned decimal digits, or hexadecimal digits for tokens.
// base selects the protocol's explicit numeric representation.
// value receives a checked 32-bit result.
static bool parse_number(const char *text, int base, uint32_t *value)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        const bool decimal = *cursor >= '0' && *cursor <= '9';
        const bool hex = (*cursor >= 'a' && *cursor <= 'f') ||
            (*cursor >= 'A' && *cursor <= 'F');
        if (!decimal && !(base == 16 && hex)) {
            return false;
        }
    }
    errno = 0;
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, base);
    if (errno != 0 || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

// response is one complete protocol frame, already terminated with LF.
// Returns timeout if the host stops consuming; it never parks the boot task.
static esp_err_t send_response(const char *response)
{
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    // A best-effort log may have ended mid-line when its TX queue filled.
    // Start a fresh line so the host can recognize this response reliably.
    const int boundary = usb_serial_jtag_write_bytes("\n", 1U, pdMS_TO_TICKS(1000));
    const size_t length = strlen(response);
    const int sent = boundary == 1
        ? usb_serial_jtag_write_bytes(response, length, pdMS_TO_TICKS(1000)) : 0;
    if (xSemaphoreGive(s_tx_mutex) != pdTRUE) {
        abort();
    }
    return sent == (int)length ? ESP_OK : ESP_ERR_TIMEOUT;
}

// command is a bounded, complete ASCII request, with no serial terminator.
// Each request reads one block; no mount lease is held while USB transmits.
static void process_command(char *command)
{
    char *tokens[8];
    size_t count = 0U;
    char *context = NULL;
    char *token = strtok_r(command, " ", &context);
    while (token != NULL && count < 8U) {
        tokens[count++] = token;
        token = strtok_r(NULL, " ", &context);
    }
    if (count < 4U || strcmp(tokens[0], "LUNA") != 0 ||
        strcmp(tokens[1], "LOG") != 0) {
        ESP_LOGE("LUNASHELL_log_usb", "Unknown USB log command rejected");
        return;
    }
    uint32_t request_id = 0U;
    if (strlen(tokens[3]) != 8U || !parse_number(tokens[3], 16, &request_id)) {
        ESP_LOGE("LUNASHELL_log_usb", "Invalid USB log request token rejected");
        return;
    }
    uint32_t offset = 0U;
    uint32_t expected_revision = 0U;
    esp_err_t result = ESP_ERR_INVALID_ARG;
    LUNASHELL_logger_log_file_t file = LUNASHELL_LOG_FILE_CURRENT;
    if (count == 7U && token == NULL && strcmp(tokens[2], "READ") == 0 &&
        parse_number(tokens[5], 10, &offset) &&
        parse_number(tokens[6], 10, &expected_revision)) {
        if (strcmp(tokens[4], "current") == 0) {
            result = ESP_OK;
        } else if (strcmp(tokens[4], "previous") == 0) {
            file = LUNASHELL_LOG_FILE_PREVIOUS;
            result = ESP_OK;
        }
    }
    uint8_t data[LOG_USB_BLOCK_BYTES];
    size_t bytes_read = 0U;
    uint32_t total_size = 0U;
    uint32_t revision = 0U;
    int system_errno = 0;
    if (result == ESP_OK) {
        if (!atomic_load_explicit(&s_reader_ready, memory_order_acquire)) {
            result = ESP_ERR_NOT_SUPPORTED;
        } else {
            result = s_sd_reader(file, offset, expected_revision, data,
                sizeof(data), &bytes_read, &total_size, &revision, &system_errno);
        }
    }
    char response[384];
    if (result != ESP_OK) {
        ESP_LOGE("LUNASHELL_log_usb", "SD read rejected: %s errno=%d offset=%" PRIu32,
            esp_err_to_name(result), system_errno, offset);
        snprintf(response, sizeof(response), "LUNA LOG ERROR %08" PRIx32 " %d %d\n",
            request_id, (int)result, system_errno);
    } else {
        char encoded[LOG_USB_BLOCK_BYTES * 2U + 1U];
        static const char hex[] = "0123456789abcdef";
        for (size_t index = 0U; index < bytes_read; ++index) {
            encoded[index * 2U] = hex[data[index] >> 4U];
            encoded[index * 2U + 1U] = hex[data[index] & 15U];
        }
        encoded[bytes_read * 2U] = '\0';
        const uint32_t crc = esp_rom_crc32_le(0U, data, (uint32_t)bytes_read);
        snprintf(response, sizeof(response),
            "LUNA LOG DATA %08" PRIx32 " %" PRIu32 " %" PRIu32
            " %" PRIu32 " %08" PRIx32 " %s\n",
            request_id, revision, offset, total_size, crc,
            bytes_read == 0U ? "-" : encoded);
    }
    const esp_err_t send_error = send_response(response);
    if (send_error != ESP_OK) {
        ESP_LOGE("LUNASHELL_log_usb", "USB response failed: %s", esp_err_to_name(send_error));
    }
}

// argument is unused; this receiver owns no display, sensor, or mount access.
static void command_task(void *argument)
{
    (void)argument;
    char command[LOG_USB_COMMAND_BYTES];
    size_t used = 0U;
    bool rejecting = false;
    while (true) {
        uint8_t character;
        if (usb_serial_jtag_read_bytes(&character, 1U, portMAX_DELAY) != 1) {
            ESP_LOGE("LUNASHELL_log_usb", "USB command read failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            if (!rejecting && used != 0U) {
                command[used] = '\0';
                process_command(command);
            }
            used = 0U;
            rejecting = false;
            continue;
        }
        if (rejecting) {
            continue;
        }
        if (character < 32U || character > 126U || used + 1U >= sizeof(command)) {
            ESP_LOGE("LUNASHELL_log_usb", "USB command rejected: invalid ASCII or length");
            rejecting = true;
            continue;
        }
        command[used++] = (char)character;
    }
}

// No parameters: establish one explicit native USB driver owner before logging.
// Returns the original initialization error; an existing owner is never replaced.
esp_err_t LUNASHELL_logger_usb_init(void)
{
    if (usb_serial_jtag_is_driver_installed()) {
        ESP_LOGE("LUNASHELL_log_usb", "USB logging initialization rejected: driver already owned");
        return ESP_ERR_INVALID_STATE;
    }
    s_tx_mutex = xSemaphoreCreateMutexStatic(&s_tx_mutex_control);
    if (s_tx_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    usb_serial_jtag_driver_config_t configuration = {
        .tx_buffer_size = 2048U,
        .rx_buffer_size = 512U,
    };
    const esp_err_t install_error = usb_serial_jtag_driver_install(&configuration);
    if (install_error != ESP_OK) {
        ESP_LOGE("LUNASHELL_log_usb", "USB driver install failed: %s", esp_err_to_name(install_error));
        return install_error;
    }
    // All supported profiles select native USB in the common defaults. The
    // VFS shares this installed driver, so stdio never writes raw FIFO bytes
    // behind the protocol owner. IDF 6.0.2 bounds/drops its TX; only RX waits.
    usb_serial_jtag_vfs_use_driver();
    if (xTaskCreate(command_task, "LUNASHELL_log_usb", 4096U, NULL, 1U,
            &s_command_task) != pdPASS) {
        usb_serial_jtag_vfs_use_nonblocking();
        const esp_err_t uninstall_error = usb_serial_jtag_driver_uninstall();
        ESP_LOGE("LUNASHELL_log_usb", "USB receiver creation failed: ESP_ERR_NO_MEM; cleanup=%s",
            esp_err_to_name(uninstall_error));
        return uninstall_error != ESP_OK ? uninstall_error : ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
