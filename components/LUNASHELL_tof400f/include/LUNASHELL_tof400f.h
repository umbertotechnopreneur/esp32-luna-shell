#ifndef LUNASHELL_TOF400F_H
#define LUNASHELL_TOF400F_H

#include <stdbool.h>
#include <stdint.h>

#include "LUNASHELL_tof400f_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUNASHELL_TOF400F_SENSOR_COUNT 2U
#define LUNASHELL_TOF400F_UART_BAUD_RATE 115200U
#define LUNASHELL_TOF400F_SHARED_TX_GPIO 16
#define LUNASHELL_TOF400F_SENSOR_1_RX_GPIO 17
#define LUNASHELL_TOF400F_SENSOR_2_RX_GPIO 18

typedef enum {
    LUNASHELL_TOF400F_SENSOR_1 = 0,
    LUNASHELL_TOF400F_SENSOR_2 = 1,
} LUNASHELL_tof400f_sensor_id_t;

typedef struct {
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t protocol_errors;
    uint32_t timeouts;
    uint32_t uart_errors;
} LUNASHELL_tof400f_sensor_stats_t;

typedef struct {
    bool valid;
    esp_err_t error;
    uint16_t distance_mm;
    int64_t sampled_at_us;
    uint32_t frame_sequence;
    uint32_t crc_error_count;
    uint32_t protocol_error_count;
    uint32_t timeout_count;
    uint32_t uart_error_count;
    LUNASHELL_tof400f_sensor_stats_t stats;
} LUNASHELL_tof400f_sensor_result_t;

typedef struct {
    /*
     * Common timestamp captured immediately after the shared request has
     * finished transmitting. It is zero when transmission did not complete.
     */
    int64_t requested_at_us;
    LUNASHELL_tof400f_sensor_result_t sensors[
        LUNASHELL_TOF400F_SENSOR_COUNT
    ];
} LUNASHELL_tof400f_pair_result_t;

typedef LUNASHELL_tof400f_sensor_result_t
    LUNASHELL_tof400f_sensor_sample_t;
typedef LUNASHELL_tof400f_pair_result_t
    LUNASHELL_tof400f_pair_sample_t;

/*
 * Installs UART1/UART2 and configures both modules. If the modules do not
 * answer, the function returns the real configuration error while retaining
 * the installed UART transport so LUNASHELL_tof400f_configure_pair() can be
 * retried after the modules are connected.
 */
esp_err_t LUNASHELL_tof400f_init(void);

/*
 * Releases every UART owned by this component. The call is idempotent and
 * retries any prior partial cleanup. Both UARTs are always considered, and
 * the first cleanup error is returned after the remaining cleanup attempts.
 */
esp_err_t LUNASHELL_tof400f_deinit(void);

/*
 * Disables autonomous serial output and selects the module's 4 m long-range
 * mode. Existing values are read first to avoid unnecessary persistent
 * writes. Both independent 0x03 readbacks must match before the pair is
 * considered configured.
 */
esp_err_t LUNASHELL_tof400f_configure_pair(void);

/*
 * Sends one shared Modbus read request. The two module TX outputs must remain
 * physically separate on GPIO17 and GPIO18; each response is validated and
 * reported independently.
 */
esp_err_t LUNASHELL_tof400f_read_pair(
    LUNASHELL_tof400f_pair_result_t *sample
);

esp_err_t LUNASHELL_tof400f_get_stats(
    LUNASHELL_tof400f_sensor_stats_t stats[
        LUNASHELL_TOF400F_SENSOR_COUNT
    ]
);

/* True once both UART drivers are installed, even if configuration failed. */
bool LUNASHELL_tof400f_initialized(void);

/* True only after both modules acknowledged the required configuration. */
bool LUNASHELL_tof400f_configured(void);

/*
 * Ready-state spelling used by acquisition services. Unlike initialized(),
 * this is false until configuration succeeds, causing init() to retry module
 * configuration while reusing an already installed UART transport.
 */
bool LUNASHELL_tof400f_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif
