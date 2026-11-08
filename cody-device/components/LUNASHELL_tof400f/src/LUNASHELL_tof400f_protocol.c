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
 *
 * VBWR E */

#include "LUNASHELL_tof400f_protocol.h"

#define MODBUS_READ_HOLDING_REGISTERS 0x03U
#define MODBUS_WRITE_SINGLE_REGISTER 0x06U
#define TOF400F_DISTANCE_REGISTER 0x0010U

static int address_is_valid(uint8_t address)
{
    return address >= 1U && address <= 247U;
}

static void append_crc(
    uint8_t frame[LUNASHELL_TOF400F_MODBUS_REQUEST_SIZE]
)
{
    const uint16_t crc = LUNASHELL_tof400f_crc16_modbus(
        frame,
        LUNASHELL_TOF400F_MODBUS_REQUEST_SIZE - 2U
    );
    frame[LUNASHELL_TOF400F_MODBUS_REQUEST_SIZE - 2U] =
        (uint8_t)(crc & 0xFFU);
    frame[LUNASHELL_TOF400F_MODBUS_REQUEST_SIZE - 1U] =
        (uint8_t)(crc >> 8U);
}

static LUNASHELL_tof400f_protocol_result_t validate_crc(
    const uint8_t *frame,
    size_t frame_size
)
{
    const uint16_t expected_crc = LUNASHELL_tof400f_crc16_modbus(
        frame,
        frame_size - 2U
    );
    const uint16_t received_crc =
        (uint16_t)frame[frame_size - 2U] |
        ((uint16_t)frame[frame_size - 1U] << 8U);
    return expected_crc == received_crc
               ? LUNASHELL_TOF400F_PROTOCOL_OK
               : LUNASHELL_TOF400F_PROTOCOL_INVALID_CRC;
}

uint16_t LUNASHELL_tof400f_crc16_modbus(
    const uint8_t *data,
    size_t data_size
)
{
    uint16_t crc = 0xFFFFU;
    if (data == NULL) {
        return crc;
    }

    for (size_t byte_index = 0; byte_index < data_size; ++byte_index) {
        crc ^= data[byte_index];
        for (uint8_t bit_index = 0; bit_index < 8U; ++bit_index) {
            if ((crc & 1U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

LUNASHELL_tof400f_protocol_result_t
LUNASHELL_tof400f_build_read_holding_register_request(
    uint8_t address,
    uint16_t register_address,
    uint8_t request[LUNASHELL_TOF400F_MODBUS_REQUEST_SIZE]
)
{
    if (!address_is_valid(address) || request == NULL) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_ARGUMENT;
    }

    request[0] = address;
    request[1] = MODBUS_READ_HOLDING_REGISTERS;
    request[2] = (uint8_t)(register_address >> 8U);
    request[3] = (uint8_t)(register_address & 0xFFU);
    request[4] = 0U;
    request[5] = 1U;
    append_crc(request);
    return LUNASHELL_TOF400F_PROTOCOL_OK;
}

LUNASHELL_tof400f_protocol_result_t
LUNASHELL_tof400f_build_read_distance_request(
    uint8_t address,
    uint8_t request[LUNASHELL_TOF400F_MODBUS_REQUEST_SIZE]
)
{
    return LUNASHELL_tof400f_build_read_holding_register_request(
        address,
        TOF400F_DISTANCE_REGISTER,
        request
    );
}

LUNASHELL_tof400f_protocol_result_t
LUNASHELL_tof400f_build_write_register_request(
    uint8_t address,
    uint16_t register_address,
    uint16_t value,
    uint8_t request[LUNASHELL_TOF400F_MODBUS_REQUEST_SIZE]
)
{
    if (!address_is_valid(address) || request == NULL) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_ARGUMENT;
    }

    request[0] = address;
    request[1] = MODBUS_WRITE_SINGLE_REGISTER;
    request[2] = (uint8_t)(register_address >> 8U);
    request[3] = (uint8_t)(register_address & 0xFFU);
    request[4] = (uint8_t)(value >> 8U);
    request[5] = (uint8_t)(value & 0xFFU);
    append_crc(request);
    return LUNASHELL_TOF400F_PROTOCOL_OK;
}

LUNASHELL_tof400f_protocol_result_t
LUNASHELL_tof400f_parse_holding_register_response(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t expected_address,
    uint16_t *value
)
{
    if (frame == NULL || value == NULL ||
        !address_is_valid(expected_address)) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_ARGUMENT;
    }
    if (frame_size !=
        LUNASHELL_TOF400F_HOLDING_REGISTER_RESPONSE_SIZE) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_LENGTH;
    }

    LUNASHELL_tof400f_protocol_result_t result =
        validate_crc(frame, frame_size);
    if (result != LUNASHELL_TOF400F_PROTOCOL_OK) {
        return result;
    }
    if (frame[0] != expected_address) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_ADDRESS;
    }
    if (frame[1] != MODBUS_READ_HOLDING_REGISTERS) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_FUNCTION;
    }
    if (frame[2] != 2U) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_PAYLOAD;
    }

    *value = ((uint16_t)frame[3] << 8U) | frame[4];
    return LUNASHELL_TOF400F_PROTOCOL_OK;
}

LUNASHELL_tof400f_protocol_result_t
LUNASHELL_tof400f_parse_distance_response(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t expected_address,
    uint16_t *distance_mm
)
{
    if (distance_mm == NULL) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_ARGUMENT;
    }

    uint16_t parsed_distance = 0U;
    const LUNASHELL_tof400f_protocol_result_t result =
        LUNASHELL_tof400f_parse_holding_register_response(
            frame,
            frame_size,
            expected_address,
            &parsed_distance
        );
    if (result != LUNASHELL_TOF400F_PROTOCOL_OK) {
        return result;
    }
    if (parsed_distance < LUNASHELL_TOF400F_MIN_DISTANCE_MM ||
        parsed_distance > LUNASHELL_TOF400F_MAX_DISTANCE_MM) {
        return LUNASHELL_TOF400F_PROTOCOL_DISTANCE_OUT_OF_RANGE;
    }

    *distance_mm = parsed_distance;
    return LUNASHELL_TOF400F_PROTOCOL_OK;
}

LUNASHELL_tof400f_protocol_result_t
LUNASHELL_tof400f_validate_write_register_response(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t expected_address,
    uint16_t expected_register_address,
    uint16_t expected_value
)
{
    if (frame == NULL || !address_is_valid(expected_address)) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_ARGUMENT;
    }
    if (frame_size != LUNASHELL_TOF400F_WRITE_RESPONSE_SIZE) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_LENGTH;
    }

    LUNASHELL_tof400f_protocol_result_t result =
        validate_crc(frame, frame_size);
    if (result != LUNASHELL_TOF400F_PROTOCOL_OK) {
        return result;
    }
    if (frame[0] != expected_address) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_ADDRESS;
    }
    if (frame[1] != MODBUS_WRITE_SINGLE_REGISTER) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_FUNCTION;
    }

    const uint16_t register_address =
        ((uint16_t)frame[2] << 8U) | frame[3];
    const uint16_t value = ((uint16_t)frame[4] << 8U) | frame[5];
    if (register_address != expected_register_address ||
        value != expected_value) {
        return LUNASHELL_TOF400F_PROTOCOL_INVALID_PAYLOAD;
    }
    return LUNASHELL_TOF400F_PROTOCOL_OK;
}
