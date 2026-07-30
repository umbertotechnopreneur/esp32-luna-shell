#include "qsee_tof400f_protocol.h"

#define CHECK(condition)             \
    do {                             \
        if (!(condition)) {          \
            return (int)__LINE__;    \
        }                            \
    } while (0)

static void append_crc(uint8_t *frame, size_t payload_size)
{
    const uint16_t crc =
        qsee_tof400f_crc16_modbus(frame, payload_size);
    frame[payload_size] = (uint8_t)(crc & 0xFFU);
    frame[payload_size + 1U] = (uint8_t)(crc >> 8U);
}

static int bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
)
{
    for (size_t index = 0U; index < size; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

int qsee_tof400f_protocol_run_tests(void)
{
    static const uint8_t official_read_request[] = {
        0x01U, 0x03U, 0x00U, 0x10U,
        0x00U, 0x01U, 0x85U, 0xCFU,
    };
    static const uint8_t official_distance_response[] = {
        0x01U, 0x03U, 0x02U, 0x00U,
        0x15U, 0x79U, 0x8BU,
    };
    uint8_t request[QSEE_TOF400F_MODBUS_REQUEST_SIZE] = {0U};

    CHECK(
        qsee_tof400f_crc16_modbus(
            official_read_request,
            6U
        ) == 0xCF85U
    );
    CHECK(
        qsee_tof400f_build_read_distance_request(
            1U,
            request
        ) == QSEE_TOF400F_PROTOCOL_OK
    );
    CHECK(
        bytes_equal(
            request,
            official_read_request,
            sizeof(official_read_request)
        )
    );
    CHECK(
        qsee_tof400f_build_read_distance_request(
            0U,
            request
        ) == QSEE_TOF400F_PROTOCOL_INVALID_ARGUMENT
    );
    CHECK(
        qsee_tof400f_build_read_distance_request(
            248U,
            request
        ) == QSEE_TOF400F_PROTOCOL_INVALID_ARGUMENT
    );
    CHECK(
        qsee_tof400f_build_read_distance_request(
            1U,
            NULL
        ) == QSEE_TOF400F_PROTOCOL_INVALID_ARGUMENT
    );

    uint16_t value = 0xA55AU;
    CHECK(
        qsee_tof400f_parse_distance_response(
            official_distance_response,
            sizeof(official_distance_response),
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_OK
    );
    CHECK(value == 21U);

    uint8_t frame[QSEE_TOF400F_WRITE_RESPONSE_SIZE] = {
        0x01U, 0x03U, 0x02U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
    };
    append_crc(frame, 5U);
    value = 0xA55AU;
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_OK
    );
    CHECK(value == QSEE_TOF400F_MIN_DISTANCE_MM);

    frame[3] = 0x0FU;
    frame[4] = 0xA0U;
    append_crc(frame, 5U);
    value = 0xA55AU;
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_OK
    );
    CHECK(value == QSEE_TOF400F_MAX_DISTANCE_MM);

    frame[3] = 0x00U;
    frame[4] = 0x00U;
    append_crc(frame, 5U);
    value = 0xA55AU;
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_DISTANCE_OUT_OF_RANGE
    );
    CHECK(value == 0xA55AU);

    frame[3] = 0x0FU;
    frame[4] = 0xA1U;
    append_crc(frame, 5U);
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_DISTANCE_OUT_OF_RANGE
    );

    frame[3] = 0x03U;
    frame[4] = 0xE8U;
    append_crc(frame, 5U);
    frame[6] ^= 0x01U;
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_INVALID_CRC
    );
    frame[6] ^= 0x01U;
    frame[0] = 0x02U;
    append_crc(frame, 5U);
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_INVALID_ADDRESS
    );
    frame[0] = 0x01U;
    frame[1] = 0x04U;
    append_crc(frame, 5U);
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_INVALID_FUNCTION
    );
    frame[1] = 0x03U;
    frame[2] = 0x03U;
    append_crc(frame, 5U);
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_INVALID_PAYLOAD
    );
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE - 1U,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_INVALID_LENGTH
    );
    CHECK(
        qsee_tof400f_parse_distance_response(
            NULL,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            &value
        ) == QSEE_TOF400F_PROTOCOL_INVALID_ARGUMENT
    );
    CHECK(
        qsee_tof400f_parse_distance_response(
            frame,
            QSEE_TOF400F_DISTANCE_RESPONSE_SIZE,
            1U,
            NULL
        ) == QSEE_TOF400F_PROTOCOL_INVALID_ARGUMENT
    );

    CHECK(
        qsee_tof400f_build_write_register_request(
            1U,
            0x0005U,
            0U,
            request
        ) == QSEE_TOF400F_PROTOCOL_OK
    );
    static const uint8_t official_write_request[] = {
        0x01U, 0x06U, 0x00U, 0x05U,
        0x00U, 0x00U, 0x99U, 0xCBU,
    };
    CHECK(
        bytes_equal(
            request,
            official_write_request,
            sizeof(official_write_request)
        )
    );
    CHECK(
        qsee_tof400f_validate_write_register_response(
            request,
            sizeof(request),
            1U,
            0x0005U,
            0U
        ) == QSEE_TOF400F_PROTOCOL_OK
    );
    CHECK(
        qsee_tof400f_validate_write_register_response(
            request,
            sizeof(request),
            1U,
            0x0004U,
            0U
        ) == QSEE_TOF400F_PROTOCOL_INVALID_PAYLOAD
    );
    request[1] = 0x03U;
    append_crc(request, 6U);
    CHECK(
        qsee_tof400f_validate_write_register_response(
            request,
            sizeof(request),
            1U,
            0x0005U,
            0U
        ) == QSEE_TOF400F_PROTOCOL_INVALID_FUNCTION
    );
    return 0;
}

#ifdef QSEE_TOF400F_PROTOCOL_TEST_MAIN
int main(void)
{
    return qsee_tof400f_protocol_run_tests();
}
#endif
