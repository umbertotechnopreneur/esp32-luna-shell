#include "qsee_tof400f.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define TOF400F_ADDRESS 0x01U
#define TOF400F_READ_FUNCTION 0x03U
#define TOF400F_WRITE_FUNCTION 0x06U
#define TOF400F_MODBUS_EXCEPTION_FLAG 0x80U
#define TOF400F_MODBUS_EXCEPTION_SIZE 5U
#define TOF400F_RANGE_MODE_REGISTER 0x0004U
#define TOF400F_CONTINUOUS_OUTPUT_REGISTER 0x0005U
#define TOF400F_LONG_RANGE_VALUE 0x0001U
#define TOF400F_CONTINUOUS_OUTPUT_DISABLED 0x0000U
#define TOF400F_RX_BUFFER_SIZE 256U
#define TOF400F_SCAN_BUFFER_SIZE 64U
#define TOF400F_RESPONSE_TIMEOUT_MS 350U
#define TOF400F_LOCK_TIMEOUT_MS 1500U
#define TOF400F_UART_EVENT_QUEUE_DEPTH 8U
#define TOF400F_READ_FAILURE_THRESHOLD 3U
#define TOF400F_RECOVERY_INITIAL_BACKOFF_MS 1000U
#define TOF400F_RECOVERY_MAX_BACKOFF_MS 30000U
#define TOF400F_REPEATED_LOG_INTERVAL_MS 10000U

typedef enum {
    RESPONSE_DISTANCE,
    RESPONSE_HOLDING_REGISTER,
    RESPONSE_WRITE_REGISTER,
} response_kind_t;

typedef struct {
    response_kind_t kind;
    uint16_t register_address;
    uint16_t value;
} expected_response_t;

typedef struct {
    bool valid;
    bool uart_fault;
    esp_err_t error;
    uint16_t value;
    int64_t sampled_at_us;
} transaction_response_t;

typedef struct {
    bool owned;
    bool poisoned;
    QueueHandle_t event_queue;
} uart_slot_state_t;

typedef struct {
    int64_t next_allowed_at_us;
    uint32_t suppressed;
} log_rate_limit_t;

static const char *TAG = "qsee_tof400f";
static const uart_port_t SENSOR_UARTS[QSEE_TOF400F_SENSOR_COUNT] = {
    UART_NUM_1,
    UART_NUM_2,
};

static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static portMUX_TYPE s_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool s_transport_initialized;
static atomic_bool s_pair_configured;
static uart_slot_state_t s_uart_slots[QSEE_TOF400F_SENSOR_COUNT];
static qsee_tof400f_sensor_stats_t
    s_stats[QSEE_TOF400F_SENSOR_COUNT];
static uint32_t s_consecutive_pair_failures;
static bool s_failure_streak_had_uart_fault;
static uint32_t s_recovery_backoff_ms =
    TOF400F_RECOVERY_INITIAL_BACKOFF_MS;
static int64_t s_next_recovery_attempt_us;
static esp_err_t s_last_recovery_error = ESP_ERR_INVALID_STATE;
static log_rate_limit_t
    s_sensor_failure_logs[QSEE_TOF400F_SENSOR_COUNT];
static log_rate_limit_t
    s_uart_event_logs[QSEE_TOF400F_SENSOR_COUNT];
static log_rate_limit_t s_state_failure_log;

static SemaphoreHandle_t ensure_mutex(void)
{
    SemaphoreHandle_t mutex;
    portENTER_CRITICAL(&s_mutex_init_lock);
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    }
    mutex = s_mutex;
    portEXIT_CRITICAL(&s_mutex_init_lock);
    return mutex;
}

static esp_err_t lock_driver(SemaphoreHandle_t *mutex)
{
    if (mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *mutex = ensure_mutex();
    if (*mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(
            *mutex,
            pdMS_TO_TICKS(TOF400F_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {
        ESP_LOGE(TAG, "Driver mutex acquisition timed out");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void unlock_driver(SemaphoreHandle_t mutex)
{
    if (mutex == NULL || xSemaphoreGive(mutex) != pdTRUE) {
        ESP_LOGE(TAG, "Driver mutex release failed");
        abort();
    }
}

static bool transport_initialized_locked(void)
{
    return atomic_load_explicit(
        &s_transport_initialized,
        memory_order_relaxed
    );
}

static bool pair_configured_locked(void)
{
    return atomic_load_explicit(
        &s_pair_configured,
        memory_order_relaxed
    );
}

static void publish_driver_state_locked(
    bool transport_initialized,
    bool pair_configured
)
{
    if (transport_initialized) {
        atomic_store_explicit(
            &s_transport_initialized,
            true,
            memory_order_release
        );
        atomic_store_explicit(
            &s_pair_configured,
            pair_configured,
            memory_order_release
        );
    } else {
        atomic_store_explicit(
            &s_pair_configured,
            false,
            memory_order_release
        );
        atomic_store_explicit(
            &s_transport_initialized,
            false,
            memory_order_release
        );
    }
}

static bool allow_repeated_log_locked(
    log_rate_limit_t *rate_limit,
    uint32_t *suppressed
)
{
    const int64_t now_us = esp_timer_get_time();
    if (rate_limit->next_allowed_at_us != 0 &&
        now_us < rate_limit->next_allowed_at_us) {
        if (rate_limit->suppressed < UINT32_MAX) {
            ++rate_limit->suppressed;
        }
        return false;
    }

    if (suppressed != NULL) {
        *suppressed = rate_limit->suppressed;
    }
    rate_limit->suppressed = 0U;
    rate_limit->next_allowed_at_us =
        now_us + (int64_t)TOF400F_REPEATED_LOG_INTERVAL_MS * 1000;
    return true;
}

static void reset_repeated_log_locked(log_rate_limit_t *rate_limit)
{
    rate_limit->next_allowed_at_us = 0;
    rate_limit->suppressed = 0U;
}

static void clear_recovery_backoff_locked(void)
{
    s_recovery_backoff_ms = TOF400F_RECOVERY_INITIAL_BACKOFF_MS;
    s_next_recovery_attempt_us = 0;
    s_last_recovery_error = ESP_ERR_INVALID_STATE;
}

static void schedule_recovery_locked(esp_err_t error)
{
    if (error == ESP_OK) {
        error = ESP_FAIL;
    }
    s_last_recovery_error = error;
    s_next_recovery_attempt_us =
        esp_timer_get_time() + (int64_t)s_recovery_backoff_ms * 1000;
    if (s_recovery_backoff_ms <
        TOF400F_RECOVERY_MAX_BACKOFF_MS) {
        uint32_t next_backoff_ms = s_recovery_backoff_ms * 2U;
        if (next_backoff_ms >
            TOF400F_RECOVERY_MAX_BACKOFF_MS) {
            next_backoff_ms = TOF400F_RECOVERY_MAX_BACKOFF_MS;
        }
        s_recovery_backoff_ms = next_backoff_ms;
    }
}

static bool recovery_backoff_active_locked(void)
{
    return s_next_recovery_attempt_us != 0 &&
           esp_timer_get_time() < s_next_recovery_attempt_us;
}

static void add_stats_counter_saturated(
    uint32_t *counter,
    uint32_t amount
)
{
    if (counter == NULL || amount == 0U) {
        return;
    }
    if (UINT32_MAX - *counter < amount) {
        *counter = UINT32_MAX;
        return;
    }
    *counter += amount;
}

static void consume_bytes(
    uint8_t *buffer,
    size_t *used,
    size_t count
)
{
    if (count >= *used) {
        *used = 0U;
        return;
    }
    memmove(buffer, buffer + count, *used - count);
    *used -= count;
}

static bool frame_crc_is_valid(const uint8_t *frame, size_t frame_size)
{
    const uint16_t expected_crc =
        qsee_tof400f_crc16_modbus(frame, frame_size - 2U);
    const uint16_t received_crc =
        (uint16_t)frame[frame_size - 2U] |
        ((uint16_t)frame[frame_size - 1U] << 8U);
    return expected_crc == received_crc;
}

static esp_err_t protocol_result_to_esp_err(
    qsee_tof400f_protocol_result_t result
)
{
    switch (result) {
    case QSEE_TOF400F_PROTOCOL_OK:
        return ESP_OK;
    case QSEE_TOF400F_PROTOCOL_INVALID_ARGUMENT:
        return ESP_ERR_INVALID_ARG;
    case QSEE_TOF400F_PROTOCOL_INVALID_LENGTH:
        return ESP_ERR_INVALID_SIZE;
    case QSEE_TOF400F_PROTOCOL_INVALID_CRC:
        return ESP_ERR_INVALID_CRC;
    case QSEE_TOF400F_PROTOCOL_INVALID_ADDRESS:
    case QSEE_TOF400F_PROTOCOL_INVALID_FUNCTION:
    case QSEE_TOF400F_PROTOCOL_INVALID_PAYLOAD:
    case QSEE_TOF400F_PROTOCOL_DISTANCE_OUT_OF_RANGE:
        return ESP_ERR_INVALID_RESPONSE;
    default:
        return ESP_FAIL;
    }
}

static TickType_t remaining_ticks(int64_t deadline_us)
{
    const int64_t remaining_us = deadline_us - esp_timer_get_time();
    if (remaining_us <= 0) {
        return 0;
    }
    const uint64_t remaining_ms =
        ((uint64_t)remaining_us + 999U) / 1000U;
    TickType_t ticks = pdMS_TO_TICKS(remaining_ms);
    if (ticks == 0) {
        ticks = 1;
    }
    return ticks;
}

static void initialize_transaction_response(
    transaction_response_t *response
)
{
    memset(response, 0, sizeof(*response));
    response->error = ESP_ERR_INVALID_STATE;
}

static void copy_stats_to_result(
    qsee_tof400f_sensor_result_t *result,
    const qsee_tof400f_sensor_stats_t *stats
)
{
    result->frame_sequence = stats->frames_ok;
    result->crc_error_count = stats->crc_errors;
    result->protocol_error_count = stats->protocol_errors;
    result->timeout_count = stats->timeouts;
    result->uart_error_count = stats->uart_errors;
    result->stats = *stats;
}

static bool drain_uart_events_locked(size_t sensor_index)
{
    QueueHandle_t event_queue =
        s_uart_slots[sensor_index].event_queue;
    if (event_queue == NULL) {
        return false;
    }

    uint32_t fifo_overflows = 0U;
    uint32_t buffer_overflows = 0U;
    uint32_t frame_errors = 0U;
    uint32_t parity_errors = 0U;
    uint32_t breaks = 0U;
    uart_event_t event;
    while (xQueueReceive(event_queue, &event, 0) == pdTRUE) {
        switch (event.type) {
        case UART_FIFO_OVF:
            ++fifo_overflows;
            break;
        case UART_BUFFER_FULL:
            ++buffer_overflows;
            break;
        case UART_FRAME_ERR:
            ++frame_errors;
            break;
        case UART_PARITY_ERR:
            ++parity_errors;
            break;
        case UART_BREAK:
            ++breaks;
            break;
        default:
            break;
        }
    }

    const uint32_t fault_count =
        fifo_overflows + buffer_overflows + frame_errors +
        parity_errors + breaks;
    if (fault_count == 0U) {
        return false;
    }
    add_stats_counter_saturated(
        &s_stats[sensor_index].uart_errors,
        fault_count
    );

    uint32_t suppressed = 0U;
    if (allow_repeated_log_locked(
            &s_uart_event_logs[sensor_index],
            &suppressed
        )) {
        ESP_LOGE(
            TAG,
            "Sensor %u UART faults: fifo=%u buffer=%u frame=%u "
            "parity=%u break=%u; suppressed_logs=%u",
            (unsigned)(sensor_index + 1U),
            (unsigned)fifo_overflows,
            (unsigned)buffer_overflows,
            (unsigned)frame_errors,
            (unsigned)parity_errors,
            (unsigned)breaks,
            (unsigned)suppressed
        );
    }
    return true;
}

static void invalidate_uart_response(transaction_response_t *response)
{
    response->valid = false;
    response->uart_fault = true;
    response->error = ESP_FAIL;
    response->value = 0U;
    response->sampled_at_us = 0;
}

static esp_err_t validate_expected_frame(
    const uint8_t *frame,
    size_t frame_size,
    const expected_response_t *expected,
    uint16_t *value
)
{
    qsee_tof400f_protocol_result_t result;
    if (expected->kind == RESPONSE_DISTANCE) {
        result = qsee_tof400f_parse_distance_response(
            frame,
            frame_size,
            TOF400F_ADDRESS,
            value
        );
    } else if (expected->kind == RESPONSE_HOLDING_REGISTER) {
        result = qsee_tof400f_parse_holding_register_response(
            frame,
            frame_size,
            TOF400F_ADDRESS,
            value
        );
    } else {
        result = qsee_tof400f_validate_write_register_response(
            frame,
            frame_size,
            TOF400F_ADDRESS,
            expected->register_address,
            expected->value
        );
    }
    return protocol_result_to_esp_err(result);
}

static esp_err_t receive_expected_response(
    size_t sensor_index,
    const expected_response_t *expected,
    int64_t deadline_us,
    transaction_response_t *response
)
{
    const uart_port_t uart = SENSOR_UARTS[sensor_index];
    const bool is_register_write =
        expected->kind == RESPONSE_WRITE_REGISTER;
    const uint8_t expected_function = is_register_write
        ? TOF400F_WRITE_FUNCTION
        : TOF400F_READ_FUNCTION;
    const size_t expected_size = is_register_write
        ? QSEE_TOF400F_WRITE_RESPONSE_SIZE
        : QSEE_TOF400F_HOLDING_REGISTER_RESPONSE_SIZE;
    uint8_t buffer[TOF400F_SCAN_BUFFER_SIZE];
    size_t used = 0U;
    bool performed_read = false;
    bool saw_noise = false;
    bool saw_crc_error = false;
    bool recorded_protocol_error = false;

    for (;;) {
        while (used > 0U) {
            if (buffer[0] != TOF400F_ADDRESS) {
                saw_noise = true;
                consume_bytes(buffer, &used, 1U);
                continue;
            }
            if (used < 2U) {
                break;
            }

            if (buffer[1] ==
                (uint8_t)(expected_function |
                          TOF400F_MODBUS_EXCEPTION_FLAG)) {
                if (used < TOF400F_MODBUS_EXCEPTION_SIZE) {
                    break;
                }
                if (!frame_crc_is_valid(
                        buffer,
                        TOF400F_MODBUS_EXCEPTION_SIZE
                    )) {
                    add_stats_counter_saturated(
                        &s_stats[sensor_index].crc_errors,
                        1U
                    );
                    saw_crc_error = true;
                    consume_bytes(buffer, &used, 1U);
                    continue;
                }
                add_stats_counter_saturated(
                    &s_stats[sensor_index].protocol_errors,
                    1U
                );
                recorded_protocol_error = true;
                response->error = ESP_ERR_INVALID_RESPONSE;
                uint32_t suppressed = 0U;
                if (allow_repeated_log_locked(
                        &s_sensor_failure_logs[sensor_index],
                        &suppressed
                    )) {
                    ESP_LOGE(
                        TAG,
                        "Sensor %u returned Modbus exception 0x%02x; "
                        "suppressed_logs=%u",
                        (unsigned)(sensor_index + 1U),
                        (unsigned)buffer[2],
                        (unsigned)suppressed
                    );
                }
                return response->error;
            }

            if (buffer[1] != expected_function) {
                saw_noise = true;
                consume_bytes(buffer, &used, 1U);
                continue;
            }
            if (used < expected_size) {
                break;
            }

            uint16_t frame_value = 0U;
            const esp_err_t validation_error = validate_expected_frame(
                buffer,
                expected_size,
                expected,
                &frame_value
            );
            if (validation_error == ESP_OK) {
                response->valid = true;
                response->error = ESP_OK;
                response->value = frame_value;
                response->sampled_at_us = esp_timer_get_time();
                return ESP_OK;
            }
            if (validation_error == ESP_ERR_INVALID_CRC) {
                add_stats_counter_saturated(
                    &s_stats[sensor_index].crc_errors,
                    1U
                );
                saw_crc_error = true;
            } else {
                add_stats_counter_saturated(
                    &s_stats[sensor_index].protocol_errors,
                    1U
                );
                recorded_protocol_error = true;
                saw_noise = true;
            }
            consume_bytes(buffer, &used, 1U);
        }

        const TickType_t wait_ticks = remaining_ticks(deadline_us);
        if (wait_ticks == 0 && performed_read) {
            break;
        }
        if (used == sizeof(buffer)) {
            saw_noise = true;
            consume_bytes(buffer, &used, 1U);
        }

        const int read_size = uart_read_bytes(
            uart,
            buffer + used,
            sizeof(buffer) - used,
            wait_ticks
        );
        performed_read = true;
        if (read_size < 0) {
            add_stats_counter_saturated(
                &s_stats[sensor_index].uart_errors,
                1U
            );
            response->uart_fault = true;
            response->error = ESP_FAIL;
            return response->error;
        }
        if (read_size > 0) {
            used += (size_t)read_size;
        }
        if (read_size == 0 && wait_ticks == 0) {
            break;
        }
    }

    if (saw_crc_error) {
        response->error = ESP_ERR_INVALID_CRC;
    } else if (saw_noise) {
        if (!recorded_protocol_error) {
            add_stats_counter_saturated(
                &s_stats[sensor_index].protocol_errors,
                1U
            );
        }
        response->error = ESP_ERR_INVALID_RESPONSE;
    } else {
        add_stats_counter_saturated(
            &s_stats[sensor_index].timeouts,
            1U
        );
        response->error = ESP_ERR_TIMEOUT;
    }
    return response->error;
}

static esp_err_t first_response_error(
    const transaction_response_t responses[
        QSEE_TOF400F_SENSOR_COUNT
    ]
)
{
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        if (!responses[sensor_index].valid) {
            return responses[sensor_index].error;
        }
    }
    return ESP_OK;
}

static void set_response_error(
    transaction_response_t responses[
        QSEE_TOF400F_SENSOR_COUNT
    ],
    esp_err_t error,
    bool uart_fault
)
{
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        responses[sensor_index].valid = false;
        responses[sensor_index].uart_fault = uart_fault;
        responses[sensor_index].error = error;
        responses[sensor_index].value = 0U;
        responses[sensor_index].sampled_at_us = 0;
    }
}

static esp_err_t transact_pair_locked(
    const uint8_t request[QSEE_TOF400F_MODBUS_REQUEST_SIZE],
    const expected_response_t *expected,
    transaction_response_t responses[
        QSEE_TOF400F_SENSOR_COUNT
    ],
    int64_t *requested_at_us
)
{
    if (requested_at_us != NULL) {
        *requested_at_us = 0;
    }
    bool receive_enabled[QSEE_TOF400F_SENSOR_COUNT] = {
        true,
        true,
    };
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        initialize_transaction_response(&responses[sensor_index]);
        (void)drain_uart_events_locked(sensor_index);
        const esp_err_t flush_error =
            uart_flush_input(SENSOR_UARTS[sensor_index]);
        if (flush_error != ESP_OK) {
            add_stats_counter_saturated(
                &s_stats[sensor_index].uart_errors,
                1U
            );
            responses[sensor_index].uart_fault = true;
            responses[sensor_index].error = flush_error;
            receive_enabled[sensor_index] = false;
            uint32_t suppressed = 0U;
            if (allow_repeated_log_locked(
                    &s_uart_event_logs[sensor_index],
                    &suppressed
                )) {
                ESP_LOGE(
                    TAG,
                    "Sensor %u RX flush failed (%s); "
                    "suppressed_logs=%u",
                    (unsigned)(sensor_index + 1U),
                    esp_err_to_name(flush_error),
                    (unsigned)suppressed
                );
            }
        }
    }

    const int written = uart_write_bytes(
        UART_NUM_1,
        request,
        QSEE_TOF400F_MODBUS_REQUEST_SIZE
    );
    if (written != (int)QSEE_TOF400F_MODBUS_REQUEST_SIZE) {
        const esp_err_t error = ESP_FAIL;
        for (size_t sensor_index = 0U;
             sensor_index < QSEE_TOF400F_SENSOR_COUNT;
             ++sensor_index) {
            add_stats_counter_saturated(
                &s_stats[sensor_index].uart_errors,
                1U
            );
        }
        set_response_error(responses, error, true);
        uint32_t suppressed = 0U;
        if (allow_repeated_log_locked(
                &s_state_failure_log,
                &suppressed
            )) {
            ESP_LOGE(
                TAG,
                "Shared Modbus request write failed (%d/%u bytes); "
                "suppressed_logs=%u",
                written,
                (unsigned)QSEE_TOF400F_MODBUS_REQUEST_SIZE,
                (unsigned)suppressed
            );
        }
        return error;
    }

    const esp_err_t transmit_error = uart_wait_tx_done(
        UART_NUM_1,
        pdMS_TO_TICKS(TOF400F_RESPONSE_TIMEOUT_MS)
    );
    if (transmit_error != ESP_OK) {
        for (size_t sensor_index = 0U;
             sensor_index < QSEE_TOF400F_SENSOR_COUNT;
             ++sensor_index) {
            add_stats_counter_saturated(
                &s_stats[sensor_index].uart_errors,
                1U
            );
        }
        set_response_error(responses, transmit_error, true);
        uint32_t suppressed = 0U;
        if (allow_repeated_log_locked(
                &s_state_failure_log,
                &suppressed
            )) {
            ESP_LOGE(
                TAG,
                "Shared Modbus request transmission failed (%s); "
                "suppressed_logs=%u",
                esp_err_to_name(transmit_error),
                (unsigned)suppressed
            );
        }
        return transmit_error;
    }

    const int64_t transmitted_at_us = esp_timer_get_time();
    if (requested_at_us != NULL) {
        *requested_at_us = transmitted_at_us;
    }
    const int64_t deadline_us =
        transmitted_at_us +
        (int64_t)TOF400F_RESPONSE_TIMEOUT_MS * 1000;
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        if (!receive_enabled[sensor_index]) {
            continue;
        }
        const esp_err_t error = receive_expected_response(
            sensor_index,
            expected,
            deadline_us,
            &responses[sensor_index]
        );
        const bool event_fault =
            drain_uart_events_locked(sensor_index);
        if (event_fault) {
            invalidate_uart_response(&responses[sensor_index]);
        } else if (error != ESP_OK) {
            responses[sensor_index].valid = false;
        } else {
            add_stats_counter_saturated(
                &s_stats[sensor_index].frames_ok,
                1U
            );
        }
    }

    return first_response_error(responses);
}

static bool responses_have_uart_fault(
    const transaction_response_t responses[
        QSEE_TOF400F_SENSOR_COUNT
    ]
)
{
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        if (responses[sensor_index].uart_fault) {
            return true;
        }
    }
    return false;
}

static void mark_transport_poisoned_locked(void)
{
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        if (s_uart_slots[sensor_index].owned) {
            s_uart_slots[sensor_index].poisoned = true;
        }
    }
    publish_driver_state_locked(false, false);
}

static esp_err_t write_register_pair_locked(
    uint16_t register_address,
    uint16_t value
)
{
    uint8_t request[QSEE_TOF400F_MODBUS_REQUEST_SIZE];
    const qsee_tof400f_protocol_result_t build_result =
        qsee_tof400f_build_write_register_request(
            TOF400F_ADDRESS,
            register_address,
            value,
            request
        );
    if (build_result != QSEE_TOF400F_PROTOCOL_OK) {
        const esp_err_t error =
            protocol_result_to_esp_err(build_result);
        ESP_LOGE(
            TAG,
            "Building register 0x%04x request failed (%s)",
            (unsigned)register_address,
            esp_err_to_name(error)
        );
        return error;
    }

    const expected_response_t expected = {
        .kind = RESPONSE_WRITE_REGISTER,
        .register_address = register_address,
        .value = value,
    };
    transaction_response_t responses[
        QSEE_TOF400F_SENSOR_COUNT
    ];
    const esp_err_t error = transact_pair_locked(
        request,
        &expected,
        responses,
        NULL
    );
    if (error != ESP_OK) {
        if (responses_have_uart_fault(responses)) {
            mark_transport_poisoned_locked();
        }
        ESP_LOGE(
            TAG,
            "Pair register 0x%04x write failed (%s)",
            (unsigned)register_address,
            esp_err_to_name(error)
        );
    }
    return error;
}

static esp_err_t read_register_pair_locked(
    uint16_t register_address,
    transaction_response_t responses[
        QSEE_TOF400F_SENSOR_COUNT
    ]
)
{
    uint8_t request[QSEE_TOF400F_MODBUS_REQUEST_SIZE];
    const qsee_tof400f_protocol_result_t build_result =
        qsee_tof400f_build_read_holding_register_request(
            TOF400F_ADDRESS,
            register_address,
            request
        );
    if (build_result != QSEE_TOF400F_PROTOCOL_OK) {
        const esp_err_t error =
            protocol_result_to_esp_err(build_result);
        ESP_LOGE(
            TAG,
            "Building register 0x%04x read request failed (%s)",
            (unsigned)register_address,
            esp_err_to_name(error)
        );
        return error;
    }

    const expected_response_t expected = {
        .kind = RESPONSE_HOLDING_REGISTER,
        .register_address = register_address,
        .value = 0U,
    };
    const esp_err_t error = transact_pair_locked(
        request,
        &expected,
        responses,
        NULL
    );
    if (error != ESP_OK) {
        if (responses_have_uart_fault(responses)) {
            mark_transport_poisoned_locked();
        }
        ESP_LOGE(
            TAG,
            "Pair register 0x%04x read failed (%s)",
            (unsigned)register_address,
            esp_err_to_name(error)
        );
    }
    return error;
}

static esp_err_t ensure_register_value_pair_locked(
    uint16_t register_address,
    uint16_t required_value
)
{
    transaction_response_t responses[
        QSEE_TOF400F_SENSOR_COUNT
    ];
    esp_err_t error = read_register_pair_locked(
        register_address,
        responses
    );
    if (error != ESP_OK) {
        return error;
    }

    bool write_required = false;
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        if (responses[sensor_index].value != required_value) {
            write_required = true;
        }
    }
    if (!write_required) {
        ESP_LOGI(
            TAG,
            "Pair register 0x%04x already verified at 0x%04x",
            (unsigned)register_address,
            (unsigned)required_value
        );
        return ESP_OK;
    }

    error = write_register_pair_locked(register_address, required_value);
    if (error != ESP_OK) {
        return error;
    }
    error = read_register_pair_locked(register_address, responses);
    if (error != ESP_OK) {
        return error;
    }

    esp_err_t verification_error = ESP_OK;
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        if (responses[sensor_index].value == required_value) {
            continue;
        }
        add_stats_counter_saturated(
            &s_stats[sensor_index].protocol_errors,
            1U
        );
        verification_error = ESP_ERR_INVALID_RESPONSE;
        ESP_LOGE(
            TAG,
            "Sensor %u register 0x%04x readback mismatch: "
            "expected 0x%04x, got 0x%04x",
            (unsigned)(sensor_index + 1U),
            (unsigned)register_address,
            (unsigned)required_value,
            (unsigned)responses[sensor_index].value
        );
    }
    return verification_error;
}

static esp_err_t configure_pair_locked(void)
{
    publish_driver_state_locked(
        transport_initialized_locked(),
        false
    );

    esp_err_t error = ensure_register_value_pair_locked(
        TOF400F_CONTINUOUS_OUTPUT_REGISTER,
        TOF400F_CONTINUOUS_OUTPUT_DISABLED
    );
    if (error != ESP_OK) {
        return error;
    }

    error = ensure_register_value_pair_locked(
        TOF400F_RANGE_MODE_REGISTER,
        TOF400F_LONG_RANGE_VALUE
    );
    if (error != ESP_OK) {
        return error;
    }

    if (!transport_initialized_locked()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_consecutive_pair_failures = 0U;
    s_failure_streak_had_uart_fault = false;
    clear_recovery_backoff_locked();
    publish_driver_state_locked(true, true);
    ESP_LOGI(
        TAG,
        "Both TOF400F modules configured for polled 4 m mode"
    );
    return ESP_OK;
}

static esp_err_t configure_uart(
    uart_port_t uart,
    int tx_gpio,
    int rx_gpio,
    QueueHandle_t *event_queue
)
{
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *event_queue = NULL;
    const uart_config_t config = {
        .baud_rate = QSEE_TOF400F_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0U,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t error = uart_param_config(uart, &config);
    if (error != ESP_OK) {
        return error;
    }
    error = uart_set_pin(
        uart,
        tx_gpio,
        rx_gpio,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );
    if (error != ESP_OK) {
        return error;
    }
    return uart_driver_install(
        uart,
        TOF400F_RX_BUFFER_SIZE,
        0,
        (int)TOF400F_UART_EVENT_QUEUE_DEPTH,
        event_queue,
        0
    );
}

static bool transport_slots_ready_locked(void)
{
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        const uart_slot_state_t *slot =
            &s_uart_slots[sensor_index];
        if (!slot->owned || slot->poisoned ||
            slot->event_queue == NULL ||
            !uart_is_driver_installed(SENSOR_UARTS[sensor_index])) {
            return false;
        }
    }
    return true;
}

static esp_err_t cleanup_transport_locked(void)
{
    esp_err_t first_error = ESP_OK;
    publish_driver_state_locked(false, false);

    for (size_t reverse_index = QSEE_TOF400F_SENSOR_COUNT;
         reverse_index > 0U;
         --reverse_index) {
        const size_t sensor_index = reverse_index - 1U;
        uart_slot_state_t *slot = &s_uart_slots[sensor_index];
        if (!slot->owned) {
            slot->poisoned = false;
            slot->event_queue = NULL;
            continue;
        }

        const uart_port_t uart = SENSOR_UARTS[sensor_index];
        if (!uart_is_driver_installed(uart)) {
            add_stats_counter_saturated(
                &s_stats[sensor_index].uart_errors,
                1U
            );
            if (first_error == ESP_OK) {
                first_error = ESP_ERR_INVALID_STATE;
            }
            ESP_LOGE(
                TAG,
                "UART%u ownership lost before cleanup",
                (unsigned)(sensor_index + 1U)
            );
            slot->owned = false;
            slot->poisoned = false;
            slot->event_queue = NULL;
            continue;
        }

        const esp_err_t cleanup_error = uart_driver_delete(uart);
        if (cleanup_error != ESP_OK) {
            add_stats_counter_saturated(
                &s_stats[sensor_index].uart_errors,
                1U
            );
            slot->poisoned = true;
            if (first_error == ESP_OK) {
                first_error = cleanup_error;
            }
            ESP_LOGE(
                TAG,
                "UART%u cleanup failed (%s)",
                (unsigned)(sensor_index + 1U),
                esp_err_to_name(cleanup_error)
            );
            continue;
        }
        slot->owned = false;
        slot->poisoned = false;
        slot->event_queue = NULL;
    }
    return first_error;
}

static esp_err_t install_uart_slot_locked(
    size_t sensor_index,
    int tx_gpio,
    int rx_gpio
)
{
    uart_slot_state_t *slot = &s_uart_slots[sensor_index];
    const uart_port_t uart = SENSOR_UARTS[sensor_index];
    if (slot->owned || uart_is_driver_installed(uart)) {
        slot->poisoned = true;
        return ESP_ERR_INVALID_STATE;
    }

    QueueHandle_t event_queue = NULL;
    const esp_err_t error = configure_uart(
        uart,
        tx_gpio,
        rx_gpio,
        &event_queue
    );
    if (error != ESP_OK) {
        add_stats_counter_saturated(
            &s_stats[sensor_index].uart_errors,
            1U
        );
        if (uart_is_driver_installed(uart)) {
            /*
             * Treat an installed driver after our install attempt as owned
             * until cleanup proves otherwise.
             */
            slot->owned = true;
            slot->event_queue = event_queue;
            slot->poisoned = true;
        } else {
            slot->poisoned = false;
            slot->event_queue = NULL;
        }
        ESP_LOGE(
            TAG,
            "UART%u installation failed (%s)",
            (unsigned)(sensor_index + 1U),
            esp_err_to_name(error)
        );
        return error;
    }

    slot->owned = true;
    slot->event_queue = event_queue;
    if (event_queue == NULL) {
        add_stats_counter_saturated(
            &s_stats[sensor_index].uart_errors,
            1U
        );
        slot->poisoned = true;
        ESP_LOGE(
            TAG,
            "UART%u installed without its bounded event queue",
            (unsigned)(sensor_index + 1U)
        );
        return ESP_ERR_INVALID_STATE;
    }
    slot->poisoned = false;
    return ESP_OK;
}

static esp_err_t prepare_transport_locked(void)
{
    if (transport_slots_ready_locked()) {
        publish_driver_state_locked(
            true,
            pair_configured_locked()
        );
        return ESP_OK;
    }

    bool foreign_driver_present = false;
    bool owned_driver_present = false;
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        uart_slot_state_t *slot = &s_uart_slots[sensor_index];
        const bool installed =
            uart_is_driver_installed(SENSOR_UARTS[sensor_index]);
        if (slot->owned) {
            owned_driver_present = true;
            if (!installed || slot->event_queue == NULL) {
                slot->poisoned = true;
            }
        } else if (installed) {
            slot->poisoned = true;
            foreign_driver_present = true;
        } else {
            slot->poisoned = false;
            slot->event_queue = NULL;
        }
    }

    if (owned_driver_present) {
        const esp_err_t cleanup_error =
            cleanup_transport_locked();
        if (cleanup_error != ESP_OK) {
            return cleanup_error;
        }
    }
    if (foreign_driver_present) {
        uint32_t suppressed = 0U;
        if (allow_repeated_log_locked(
                &s_state_failure_log,
                &suppressed
            )) {
            ESP_LOGE(
                TAG,
                "UART transport ownership conflict; "
                "suppressed_logs=%u",
                (unsigned)suppressed
            );
        }
        publish_driver_state_locked(false, false);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = install_uart_slot_locked(
        QSEE_TOF400F_SENSOR_1,
        QSEE_TOF400F_SHARED_TX_GPIO,
        QSEE_TOF400F_SENSOR_1_RX_GPIO
    );
    if (error != ESP_OK) {
        const esp_err_t cleanup_error =
            cleanup_transport_locked();
        return cleanup_error != ESP_OK ? cleanup_error : error;
    }
    error = install_uart_slot_locked(
        QSEE_TOF400F_SENSOR_2,
        UART_PIN_NO_CHANGE,
        QSEE_TOF400F_SENSOR_2_RX_GPIO
    );
    if (error != ESP_OK) {
        const esp_err_t cleanup_error =
            cleanup_transport_locked();
        return cleanup_error != ESP_OK ? cleanup_error : error;
    }

    publish_driver_state_locked(true, false);
    reset_repeated_log_locked(&s_state_failure_log);
    ESP_LOGI(
        TAG,
        "UART transport ready: TX GPIO%d, RX GPIO%d/GPIO%d, %u 8N1",
        QSEE_TOF400F_SHARED_TX_GPIO,
        QSEE_TOF400F_SENSOR_1_RX_GPIO,
        QSEE_TOF400F_SENSOR_2_RX_GPIO,
        (unsigned)QSEE_TOF400F_UART_BAUD_RATE
    );
    return ESP_OK;
}

esp_err_t qsee_tof400f_init(void)
{
    SemaphoreHandle_t mutex;
    esp_err_t error = lock_driver(&mutex);
    if (error != ESP_OK) {
        return error;
    }

    if (!pair_configured_locked() &&
        recovery_backoff_active_locked()) {
        error = s_last_recovery_error;
        unlock_driver(mutex);
        return error;
    }

    error = prepare_transport_locked();
    if (error == ESP_OK && !pair_configured_locked()) {
        error = configure_pair_locked();
    }
    if (error != ESP_OK) {
        schedule_recovery_locked(error);
    }
    unlock_driver(mutex);
    return error;
}

esp_err_t qsee_tof400f_deinit(void)
{
    SemaphoreHandle_t mutex;
    esp_err_t error = lock_driver(&mutex);
    if (error != ESP_OK) {
        return error;
    }

    error = cleanup_transport_locked();
    s_consecutive_pair_failures = 0U;
    s_failure_streak_had_uart_fault = false;
    if (error == ESP_OK) {
        clear_recovery_backoff_locked();
    } else {
        schedule_recovery_locked(error);
    }
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        reset_repeated_log_locked(
            &s_sensor_failure_logs[sensor_index]
        );
        reset_repeated_log_locked(
            &s_uart_event_logs[sensor_index]
        );
    }
    reset_repeated_log_locked(&s_state_failure_log);
    unlock_driver(mutex);
    return error;
}

esp_err_t qsee_tof400f_configure_pair(void)
{
    SemaphoreHandle_t mutex;
    esp_err_t error = lock_driver(&mutex);
    if (error != ESP_OK) {
        return error;
    }
    if (!transport_initialized_locked() ||
        !transport_slots_ready_locked()) {
        uint32_t suppressed = 0U;
        if (allow_repeated_log_locked(
                &s_state_failure_log,
                &suppressed
            )) {
            ESP_LOGE(
                TAG,
                "Pair configuration rejected before UART init; "
                "suppressed_logs=%u",
                (unsigned)suppressed
            );
        }
        unlock_driver(mutex);
        return ESP_ERR_INVALID_STATE;
    }

    error = configure_pair_locked();
    if (error != ESP_OK) {
        schedule_recovery_locked(error);
    }
    unlock_driver(mutex);
    return error;
}

esp_err_t qsee_tof400f_read_pair(
    qsee_tof400f_pair_result_t *sample
)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(sample, 0, sizeof(*sample));
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        sample->sensors[sensor_index].error =
            ESP_ERR_INVALID_STATE;
    }

    SemaphoreHandle_t mutex;
    esp_err_t error = lock_driver(&mutex);
    if (error != ESP_OK) {
        for (size_t sensor_index = 0U;
             sensor_index < QSEE_TOF400F_SENSOR_COUNT;
             ++sensor_index) {
            sample->sensors[sensor_index].error = error;
        }
        return error;
    }
    const bool transport_initialized =
        transport_initialized_locked();
    const bool pair_configured = pair_configured_locked();
    if (!transport_initialized || !pair_configured) {
        for (size_t sensor_index = 0U;
             sensor_index < QSEE_TOF400F_SENSOR_COUNT;
             ++sensor_index) {
            copy_stats_to_result(
                &sample->sensors[sensor_index],
                &s_stats[sensor_index]
            );
        }
        uint32_t suppressed = 0U;
        if (allow_repeated_log_locked(
                &s_state_failure_log,
                &suppressed
            )) {
            ESP_LOGE(
                TAG,
                "Distance read rejected: transport=%s configured=%s; "
                "suppressed_logs=%u",
                transport_initialized ? "ready" : "not-ready",
                pair_configured ? "yes" : "no",
                (unsigned)suppressed
            );
        }
        unlock_driver(mutex);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t request[QSEE_TOF400F_MODBUS_REQUEST_SIZE];
    const qsee_tof400f_protocol_result_t build_result =
        qsee_tof400f_build_read_distance_request(
            TOF400F_ADDRESS,
            request
        );
    if (build_result != QSEE_TOF400F_PROTOCOL_OK) {
        error = protocol_result_to_esp_err(build_result);
        for (size_t sensor_index = 0U;
             sensor_index < QSEE_TOF400F_SENSOR_COUNT;
             ++sensor_index) {
            sample->sensors[sensor_index].error = error;
            copy_stats_to_result(
                &sample->sensors[sensor_index],
                &s_stats[sensor_index]
            );
        }
        unlock_driver(mutex);
        ESP_LOGE(
            TAG,
            "Distance request construction failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }

    const expected_response_t expected = {
        .kind = RESPONSE_DISTANCE,
        .register_address = 0U,
        .value = 0U,
    };
    transaction_response_t responses[
        QSEE_TOF400F_SENSOR_COUNT
    ];
    error = transact_pair_locked(
        request,
        &expected,
        responses,
        &sample->requested_at_us
    );
    for (size_t sensor_index = 0U;
         sensor_index < QSEE_TOF400F_SENSOR_COUNT;
         ++sensor_index) {
        sample->sensors[sensor_index].valid =
            responses[sensor_index].valid;
        sample->sensors[sensor_index].error =
            responses[sensor_index].error;
        if (responses[sensor_index].valid) {
            sample->sensors[sensor_index].distance_mm =
                responses[sensor_index].value;
            sample->sensors[sensor_index].sampled_at_us =
                responses[sensor_index].sampled_at_us;
        } else {
            uint32_t suppressed = 0U;
            if (allow_repeated_log_locked(
                    &s_sensor_failure_logs[sensor_index],
                    &suppressed
                )) {
                ESP_LOGE(
                    TAG,
                    "Sensor %u distance read failed (%s); "
                    "suppressed_logs=%u",
                    (unsigned)(sensor_index + 1U),
                    esp_err_to_name(responses[sensor_index].error),
                    (unsigned)suppressed
                );
            }
        }
        copy_stats_to_result(
            &sample->sensors[sensor_index],
            &s_stats[sensor_index]
        );
    }

    if (error == ESP_OK) {
        s_consecutive_pair_failures = 0U;
        s_failure_streak_had_uart_fault = false;
        clear_recovery_backoff_locked();
    } else {
        if (s_consecutive_pair_failures < UINT32_MAX) {
            ++s_consecutive_pair_failures;
        }
        s_failure_streak_had_uart_fault =
            s_failure_streak_had_uart_fault ||
            responses_have_uart_fault(responses);
        if (s_consecutive_pair_failures >=
            TOF400F_READ_FAILURE_THRESHOLD) {
            if (s_failure_streak_had_uart_fault) {
                mark_transport_poisoned_locked();
            } else {
                publish_driver_state_locked(
                    transport_initialized_locked(),
                    false
                );
            }
            schedule_recovery_locked(error);
            ESP_LOGW(
                TAG,
                "Pair marked %s after %u consecutive read failures; "
                "recovery scheduled (%s)",
                s_failure_streak_had_uart_fault
                    ? "transport-poisoned"
                    : "unconfigured",
                (unsigned)s_consecutive_pair_failures,
                esp_err_to_name(error)
            );
        }
    }
    unlock_driver(mutex);
    return error;
}

esp_err_t qsee_tof400f_get_stats(
    qsee_tof400f_sensor_stats_t stats[
        QSEE_TOF400F_SENSOR_COUNT
    ]
)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    SemaphoreHandle_t mutex;
    const esp_err_t error = lock_driver(&mutex);
    if (error != ESP_OK) {
        return error;
    }
    memcpy(stats, s_stats, sizeof(s_stats));
    unlock_driver(mutex);
    return ESP_OK;
}

bool qsee_tof400f_initialized(void)
{
    return atomic_load_explicit(
        &s_transport_initialized,
        memory_order_acquire
    );
}

bool qsee_tof400f_configured(void)
{
    return atomic_load_explicit(
        &s_pair_configured,
        memory_order_acquire
    );
}

bool qsee_tof400f_is_initialized(void)
{
    if (!atomic_load_explicit(
            &s_pair_configured,
            memory_order_acquire
        )) {
        return false;
    }
    return atomic_load_explicit(
        &s_transport_initialized,
        memory_order_acquire
    );
}
