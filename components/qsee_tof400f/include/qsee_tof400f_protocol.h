#ifndef QSEE_TOF400F_PROTOCOL_H
#define QSEE_TOF400F_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QSEE_TOF400F_MODBUS_REQUEST_SIZE 8U
#define QSEE_TOF400F_HOLDING_REGISTER_RESPONSE_SIZE 7U
#define QSEE_TOF400F_DISTANCE_RESPONSE_SIZE \
    QSEE_TOF400F_HOLDING_REGISTER_RESPONSE_SIZE
#define QSEE_TOF400F_WRITE_RESPONSE_SIZE 8U
#define QSEE_TOF400F_MIN_DISTANCE_MM 1U
#define QSEE_TOF400F_MAX_DISTANCE_MM 4000U

typedef enum {
    QSEE_TOF400F_PROTOCOL_OK = 0,
    QSEE_TOF400F_PROTOCOL_INVALID_ARGUMENT,
    QSEE_TOF400F_PROTOCOL_INVALID_LENGTH,
    QSEE_TOF400F_PROTOCOL_INVALID_CRC,
    QSEE_TOF400F_PROTOCOL_INVALID_ADDRESS,
    QSEE_TOF400F_PROTOCOL_INVALID_FUNCTION,
    QSEE_TOF400F_PROTOCOL_INVALID_PAYLOAD,
    QSEE_TOF400F_PROTOCOL_DISTANCE_OUT_OF_RANGE,
} qsee_tof400f_protocol_result_t;

uint16_t qsee_tof400f_crc16_modbus(
    const uint8_t *data,
    size_t data_size
);

qsee_tof400f_protocol_result_t
qsee_tof400f_build_read_holding_register_request(
    uint8_t address,
    uint16_t register_address,
    uint8_t request[QSEE_TOF400F_MODBUS_REQUEST_SIZE]
);

qsee_tof400f_protocol_result_t
qsee_tof400f_build_read_distance_request(
    uint8_t address,
    uint8_t request[QSEE_TOF400F_MODBUS_REQUEST_SIZE]
);

qsee_tof400f_protocol_result_t
qsee_tof400f_build_write_register_request(
    uint8_t address,
    uint16_t register_address,
    uint16_t value,
    uint8_t request[QSEE_TOF400F_MODBUS_REQUEST_SIZE]
);

qsee_tof400f_protocol_result_t
qsee_tof400f_parse_holding_register_response(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t expected_address,
    uint16_t *value
);

qsee_tof400f_protocol_result_t
qsee_tof400f_parse_distance_response(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t expected_address,
    uint16_t *distance_mm
);

qsee_tof400f_protocol_result_t
qsee_tof400f_validate_write_register_response(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t expected_address,
    uint16_t expected_register_address,
    uint16_t expected_value
);

#ifdef __cplusplus
}
#endif

#endif
