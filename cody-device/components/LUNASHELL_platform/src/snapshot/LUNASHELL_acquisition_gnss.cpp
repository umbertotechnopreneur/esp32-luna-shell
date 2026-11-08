#include "LUNASHELL_acquisition_gnss.hpp"
#include "LUNASHELL_acquisition_internal.hpp"
#include "LUNASHELL_acquisition_i2c.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_snapshot_internal.hpp"
#include "LUNASHELL_rtc_calendar.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace LUNASHELL::snapshot_detail::gnss {
namespace {
#if CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
const char *TAG = "LUNASHELL_snapshot";
constexpr uint32_t I2C_TIMEOUT_MS = acquisition_i2c::kTimeoutMs;
constexpr uint32_t GPS_MAX_PAYLOAD = 4096;
constexpr uint32_t GPS_MAX_PENDING_BYTES = 1024U * 1024U;
constexpr uint8_t LC76G_WRITE_ADDRESS = 0x50;
constexpr uint8_t LC76G_READ_ADDRESS = 0x54;
constexpr uint8_t LC76G_DATA_WRITE_ADDRESS = 0x58;
// A failed exchange keeps its real error visible while releasing high-rate work.
constexpr int64_t kGpsPeriodicRetryBackoffUs = 15LL * 1000LL * 1000LL;
i2c_master_dev_handle_t s_gps_control;
i2c_master_dev_handle_t s_gps_read;
i2c_master_dev_handle_t s_gps_write;
bool s_gps_ready;
bool s_gps_power_suspended;
// Initialization is single-owner; subsequent access holds the shared refresh lock.
esp_err_t s_gps_periodic_error = ESP_OK;
int64_t s_gps_periodic_retry_after_us = 0;
// I2C copies from its internal buffers into this bounded acquisition-owned payload.
EXT_RAM_BSS_ATTR char s_gps_payload[GPS_MAX_PAYLOAD + 1];

using acquisition_i2c::add_i2c_device;


// bus: Shared I2C bus owned by the hardware initialization stage.
// Exception: Returns original hardware or canonical publication failures.
esp_err_t init_gps(i2c_master_bus_handle_t bus)
{
    esp_err_t error = add_i2c_device(bus, LC76G_WRITE_ADDRESS, 100000, &s_gps_control);
    if (error == ESP_OK) {
        error = add_i2c_device(bus, LC76G_READ_ADDRESS, 100000, &s_gps_read);
    }
    if (error == ESP_OK) {
        error = add_i2c_device(
            bus,
            LC76G_DATA_WRITE_ADDRESS,
            100000,
            &s_gps_write
        );
    }
    if (error == ESP_OK) {
        const esp_err_t write_probe = i2c_master_probe(bus, LC76G_WRITE_ADDRESS, I2C_TIMEOUT_MS);
        ESP_LOGI(
            TAG,
            "GPS LC76G control endpoint 0x50: %s; read endpoint 0x54 is command-gated",
            esp_err_to_name(write_probe)
        );
        if (write_probe != ESP_OK) {
            error = ESP_ERR_NOT_FOUND;
        }
    }
    s_gps_ready = error == ESP_OK;
    s_gps_power_suspended = false;
    s_gps_periodic_error = ESP_OK;
    s_gps_periodic_retry_after_us = 0;
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.gps, error == ESP_OK, error);
    });
    if (!s_gps_ready) {
        ESP_LOGW(TAG, "GPS LC76G: initialization failed (%s)", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(
        TAG,
        "GPS LC76G: I2C bridge configured (control=0x50 read=0x54 data-write=0x58)"
    );
    return ESP_OK;
}

// data: Borrowed bounded transfer buffer.
// length: Exact number of bytes in the borrowed buffer.
// Exception: Returns original hardware or canonical publication failures.
esp_err_t gps_control_write(const uint8_t *data, size_t length)
{
    if (data == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit(s_gps_control, data, length, 1000);
}

// The LC76G I2C bridge requires a quiet phase after a read response before
// accepting the next control frame (including 0xAA531000 for host writes).
constexpr TickType_t kGpsI2cProtocolDelayTicks = pdMS_TO_TICKS(10);

// command: Borrowed LC76G control frame.
// command_length: Exact control frame size in bytes.
// data: Borrowed bounded transfer buffer.
// data_length: Bounded response size or caller-owned pending-length output.
// step: Acquisition operation label retained in failure logs.
// Exception: Returns original hardware or canonical publication failures.
esp_err_t gps_exchange(
    const uint8_t *command,
    size_t command_length,
    uint8_t *data,
    size_t data_length,
    const char *step
)
{
    esp_err_t error = ESP_FAIL;
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        error = gps_control_write(command, command_length);
        if (error == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            error = i2c_master_receive(s_gps_read, data, data_length, 1000);
        }
        if (error == ESP_OK) {
            // Do not begin a new LC76G I2C phase immediately after the 0x54
            // read. The module NACKs the following 0x50 control transaction
            // otherwise, which used to abort transactional Device Standby.
            vTaskDelay(kGpsI2cProtocolDelayTicks);
            return ESP_OK;
        }
        ESP_LOGW(
            TAG,
            "GPS LC76G: %s exchange attempt %u/3 failed (%s)",
            step,
            attempt,
            esp_err_to_name(error)
        );
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return error;
}

// value: Bounded encoded field or caller-owned parsed value.
// count: Exact number of decimal digits required.
bool decimal_digits(const char *value, size_t count)
{
    if (value == nullptr) {
        return false;
    }
    for (size_t index = 0; index < count; ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    return true;
}

// value: Bounded encoded field or caller-owned parsed value.
int hexadecimal_digit(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

// sentence: Bounded NMEA sentence processed by the acquisition owner.
// length: Exact number of bytes in the borrowed buffer.
bool validate_and_strip_nmea_checksum(char *sentence, size_t length)
{
    if (sentence == nullptr || length < 5 || sentence[0] != '$') {
        return false;
    }
    char *checksum_marker = static_cast<char *>(
        std::memchr(sentence, '*', length)
    );
    if (checksum_marker == nullptr ||
        checksum_marker + 3 != sentence + length) {
        return false;
    }
    const int high = hexadecimal_digit(checksum_marker[1]);
    const int low = hexadecimal_digit(checksum_marker[2]);
    if (high < 0 || low < 0) {
        return false;
    }
    uint8_t calculated = 0;
    for (char *cursor = sentence + 1; cursor < checksum_marker; ++cursor) {
        calculated ^= static_cast<uint8_t>(*cursor);
    }
    const uint8_t supplied = static_cast<uint8_t>((high << 4) | low);
    if (calculated != supplied) {
        return false;
    }
    *checksum_marker = '\0';
    return true;
}

// value: Bounded encoded field or caller-owned parsed value.
bool nmea_time_valid(const char *value)
{
    if (value == nullptr || std::strlen(value) < 6 ||
        !decimal_digits(value, 6)) {
        return false;
    }
    const uint8_t hour = static_cast<uint8_t>(
        (value[0] - '0') * 10 + (value[1] - '0')
    );
    const uint8_t minute = static_cast<uint8_t>(
        (value[2] - '0') * 10 + (value[3] - '0')
    );
    const uint8_t second = static_cast<uint8_t>(
        (value[4] - '0') * 10 + (value[5] - '0')
    );
    if (hour > 23U || minute > 59U || second > 59U) {
        return false;
    }
    if (value[6] == '\0') {
        return true;
    }
    if (value[6] != '.' || value[7] == '\0') {
        return false;
    }
    for (const char *cursor = value + 7; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
    }
    return true;
}

// value: Bounded encoded field or caller-owned parsed value.
// parsed: Caller-owned numeric output assigned only after validation.
bool parse_float_field(const char *value, float *parsed)
{
    if (value == nullptr || value[0] == '\0' || parsed == nullptr) {
        return false;
    }
    char *end = nullptr;
    const float result = std::strtof(value, &end);
    if (end == value || end == nullptr || *end != '\0' ||
        !std::isfinite(result)) {
        return false;
    }
    *parsed = result;
    return true;
}

// value: Bounded encoded field or caller-owned parsed value.
// parsed: Caller-owned numeric output assigned only after validation.
bool parse_uint8_field(const char *value, uint8_t *parsed)
{
    if (value == nullptr || value[0] == '\0' || parsed == nullptr) {
        return false;
    }
    char *end = nullptr;
    const unsigned long result = std::strtoul(value, &end, 10);
    if (end == value || end == nullptr || *end != '\0' || result > UINT8_MAX) {
        return false;
    }
    *parsed = static_cast<uint8_t>(result);
    return true;
}

// value: Bounded encoded field or caller-owned parsed value.
// hemisphere: Validated NMEA cardinal hemisphere.
// latitude: Whether the coordinate uses latitude rather than longitude limits.
// coordinate: Caller-owned coordinate output assigned only after validation.
bool nmea_coordinate(
    const char *value,
    char hemisphere,
    bool latitude,
    double *coordinate
)
{
    if (value == nullptr || value[0] == '\0' || coordinate == nullptr) {
        return false;
    }
    if ((latitude && hemisphere != 'N' && hemisphere != 'S') ||
        (!latitude && hemisphere != 'E' && hemisphere != 'W')) {
        return false;
    }
    char *end = nullptr;
    const double encoded = std::strtod(value, &end);
    if (end == value || end == nullptr || *end != '\0' ||
        !std::isfinite(encoded) || encoded < 0.0) {
        return false;
    }
    const int degrees = static_cast<int>(encoded / 100.0);
    const double minutes = encoded - degrees * 100.0;
    const int maximum_degrees = latitude ? 90 : 180;
    if (degrees > maximum_degrees || minutes < 0.0 || minutes >= 60.0 ||
        (degrees == maximum_degrees && minutes != 0.0)) {
        return false;
    }
    double result = static_cast<double>(degrees) + minutes / 60.0;
    if (hemisphere == 'S' || hemisphere == 'W') {
        result = -result;
    }
    *coordinate = result;
    return true;
}

enum class NmeaSentenceKind : uint8_t {
    Unsupported,
    Gga,
    Rmc,
};

struct NmeaParseState {
    bool saw_gga = false;
    bool gga_has_fix = false;
    bool saw_rmc = false;
    bool rmc_active = false;
};

struct NmeaPayloadResult {
    size_t valid_sentence_count = 0;
    size_t malformed_supported_sentence_count = 0;
    const char *coherence_error = nullptr;
};

// sentence: Bounded NMEA sentence processed by the acquisition owner.
// length: Exact number of bytes in the borrowed buffer.
NmeaSentenceKind nmea_sentence_kind(const char *sentence, size_t length)
{
    if (sentence == nullptr || length < 6 || sentence[0] != '$') {
        return NmeaSentenceKind::Unsupported;
    }
    size_t type_end = 1;
    while (type_end < length && sentence[type_end] != ',' &&
           sentence[type_end] != '*') {
        ++type_end;
    }
    if (type_end < 4) {
        return NmeaSentenceKind::Unsupported;
    }
    const char *suffix = sentence + type_end - 3U;
    if (std::memcmp(suffix, "GGA", 3) == 0) {
        return NmeaSentenceKind::Gga;
    }
    if (std::memcmp(suffix, "RMC", 3) == 0) {
        return NmeaSentenceKind::Rmc;
    }
    return NmeaSentenceKind::Unsupported;
}

// sentence: Bounded NMEA sentence processed by the acquisition owner.
// length: Exact number of bytes in the borrowed buffer.
// sample: Acquisition-owned candidate updated only from validated fields.
// state: Acquisition-owned sentence validity and coherence state.
// supported: Optional indication that the sentence belongs to a supported kind.
bool parse_nmea_sentence(
    const char *sentence,
    size_t length,
    LUNASHELL::GpsSnapshot &sample,
    NmeaParseState &state,
    bool *supported
)
{
    const NmeaSentenceKind kind = nmea_sentence_kind(sentence, length);
    if (supported != nullptr) {
        *supported = kind != NmeaSentenceKind::Unsupported;
    }
    if (kind == NmeaSentenceKind::Unsupported) {
        return false;
    }

    char line[192] = {};
    if (length >= sizeof(line)) {
        return false;
    }
    const size_t copy_length = length;
    std::memcpy(line, sentence, copy_length);
    if (!validate_and_strip_nmea_checksum(line, copy_length)) {
        return false;
    }

    char *fields[20] = {line};
    size_t field_count = 1;
    for (size_t index = 0; index < copy_length &&
                           field_count < sizeof(fields) / sizeof(fields[0]);
         ++index) {
        if (line[index] == ',') {
            line[index] = '\0';
            fields[field_count++] = &line[index + 1U];
        }
    }
    if (field_count < 2 || fields[0] == nullptr) {
        return false;
    }

    const size_t type_length = std::strlen(fields[0]);
    const char *type = type_length >= 3 ? fields[0] + type_length - 3 : fields[0];
    if (kind == NmeaSentenceKind::Gga &&
        std::strcmp(type, "GGA") == 0 && field_count >= 10) {
        uint8_t fix_quality = 0;
        uint8_t satellites = 0;
        if (!nmea_time_valid(fields[1]) ||
            !parse_uint8_field(fields[6], &fix_quality) ||
            !parse_uint8_field(fields[7], &satellites)) {
            return false;
        }
        strlcpy(sample.utc_time, fields[1], sizeof(sample.utc_time));
        sample.fix_quality = fix_quality;
        sample.satellites = satellites;
        state.saw_gga = true;
        state.gga_has_fix = fix_quality > 0U;
        if (fix_quality == 0U) {
            return true;
        }
        if (satellites == 0U) {
            return false;
        }

        float hdop = 0.0F;
        float altitude_m = 0.0F;
        double latitude = 0.0;
        double longitude = 0.0;
        if (!parse_float_field(fields[8], &hdop) || hdop < 0.0F ||
            !parse_float_field(fields[9], &altitude_m) ||
            !nmea_coordinate(
                fields[2],
                fields[3] != nullptr ? fields[3][0] : '\0',
                true,
                &latitude
            ) ||
            !nmea_coordinate(
                fields[4],
                fields[5] != nullptr ? fields[5][0] : '\0',
                false,
                &longitude
            )) {
            return false;
        }
        sample.hdop = hdop;
        sample.altitude_m = altitude_m;
        sample.latitude_degrees = latitude;
        sample.longitude_degrees = longitude;
        return true;
    } else if (kind == NmeaSentenceKind::Rmc &&
               std::strcmp(type, "RMC") == 0 && field_count >= 10) {
        if (fields[2] == nullptr ||
            (fields[2][0] != 'A' && fields[2][0] != 'V') ||
            fields[2][1] != '\0') {
            return false;
        }
        if (fields[2][0] == 'V') {
            state.saw_rmc = true;
            state.rmc_active = false;
            sample.utc_valid = false;
            return true;
        }
        if (!nmea_time_valid(fields[1]) ||
            fields[9] == nullptr ||
            std::strlen(fields[9]) != 6 ||
            !decimal_digits(fields[9], 6)) {
            return false;
        }

        const unsigned day = static_cast<unsigned>(
            (fields[9][0] - '0') * 10 + (fields[9][1] - '0')
        );
        const unsigned month = static_cast<unsigned>(
            (fields[9][2] - '0') * 10 + (fields[9][3] - '0')
        );
        const unsigned year = 2000U + static_cast<unsigned>(
            (fields[9][4] - '0') * 10 + (fields[9][5] - '0')
        );
        const LUNASHELL_calendar_time_t utc = {
            .clock_integrity = true,
            .year = static_cast<uint16_t>(year),
            .month = static_cast<uint8_t>(month),
            .day = static_cast<uint8_t>(day),
            .hour = static_cast<uint8_t>(
                (fields[1][0] - '0') * 10 + (fields[1][1] - '0')
            ),
            .minute = static_cast<uint8_t>(
                (fields[1][2] - '0') * 10 + (fields[1][3] - '0')
            ),
            .second = static_cast<uint8_t>(
                (fields[1][4] - '0') * 10 + (fields[1][5] - '0')
            ),
        };
        double latitude = 0.0;
        double longitude = 0.0;
        float speed_knots = 0.0F;
        float course_degrees = 0.0F;
        if (!LUNASHELL::rtc_calendar::valid_calendar_time(utc) ||
            !parse_float_field(fields[7], &speed_knots) ||
            speed_knots < 0.0F ||
            !parse_float_field(fields[8], &course_degrees) ||
            course_degrees < 0.0F || course_degrees > 360.0F ||
            !nmea_coordinate(
                fields[3],
                fields[4] != nullptr ? fields[4][0] : '\0',
                true,
                &latitude
            ) ||
            !nmea_coordinate(
                fields[5],
                fields[6] != nullptr ? fields[6][0] : '\0',
                false,
                &longitude
            )) {
            return false;
        }
        strlcpy(sample.utc_time, fields[1], sizeof(sample.utc_time));
        std::snprintf(
            sample.utc_date,
            sizeof(sample.utc_date),
            "%04u-%02u-%02u",
            year,
            month,
            day
        );
        sample.utc_valid = true;
        state.saw_rmc = true;
        state.rmc_active = true;
        sample.latitude_degrees = latitude;
        sample.longitude_degrees = longitude;
        sample.speed_mps = speed_knots * 0.514444F;
        sample.course_degrees = course_degrees;
        return true;
    }
    return false;
}

// payload: Borrowed bounded receiver payload.
// length: Exact number of bytes in the borrowed buffer.
// sample: Acquisition-owned candidate updated only from validated fields.
NmeaPayloadResult parse_nmea_payload(
    const char *payload,
    size_t length,
    LUNASHELL::GpsSnapshot &sample
)
{
    NmeaPayloadResult result;
    NmeaParseState state;
    size_t cursor = 0;
    bool copied_first = false;
    while (cursor < length) {
        while (cursor < length && payload[cursor] != '$') {
            ++cursor;
        }
        const size_t start = cursor;
        while (cursor < length && payload[cursor] != '\r' && payload[cursor] != '\n') {
            ++cursor;
        }
        if (start < cursor) {
            const size_t sentence_length = cursor - start;
            bool supported = false;
            if (parse_nmea_sentence(
                    payload + start,
                    sentence_length,
                    sample,
                    state,
                    &supported
                )) {
                ++result.valid_sentence_count;
            } else if (supported) {
                ++result.malformed_supported_sentence_count;
            }
            if (!copied_first && result.valid_sentence_count > 0) {
                const size_t output_length =
                    sentence_length < sizeof(sample.first_sentence) - 1U
                        ? sentence_length
                        : sizeof(sample.first_sentence) - 1U;
                std::memcpy(sample.first_sentence, payload + start, output_length);
                sample.first_sentence[output_length] = '\0';
                copied_first = true;
            }
        }
        while (cursor < length && (payload[cursor] == '\r' || payload[cursor] == '\n')) {
            ++cursor;
        }
    }

    if (result.malformed_supported_sentence_count > 0) {
        result.coherence_error = "supported NMEA sentence malformed";
    } else if (state.saw_rmc && !state.saw_gga) {
        result.coherence_error =
            "RMC status has no GGA fix-quality sentence";
    } else if (state.saw_gga && !state.saw_rmc) {
        result.coherence_error =
            "GGA fix quality has no RMC status sentence";
    } else if (state.saw_gga && state.saw_rmc &&
               state.gga_has_fix != state.rmc_active) {
        result.coherence_error =
            "GGA fix quality contradicts RMC status";
    } else {
        sample.has_fix =
            state.saw_gga && state.gga_has_fix &&
            state.saw_rmc && state.rmc_active;
    }
    return result;
}

constexpr uint8_t kGpsPendingLengthCommand[] = {
    0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00,
};
constexpr uint8_t kGpsWriteFreeLengthCommand[] = {
    0x04, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00,
};
constexpr uint8_t kGpsWriteConfigurationCommand[] = {
    0x00, 0x10, 0x53, 0xAA,
};
constexpr unsigned kGpsPairAckAttempts = 10;
constexpr TickType_t kGpsPairAckPollTicks = pdMS_TO_TICKS(100);

// value: Bounded encoded field or caller-owned parsed value.
uint32_t gps_le_u32(const uint8_t value[4])
{
    return static_cast<uint32_t>(value[0]) |
           (static_cast<uint32_t>(value[1]) << 8) |
           (static_cast<uint32_t>(value[2]) << 16) |
           (static_cast<uint32_t>(value[3]) << 24);
}

// command: Borrowed LC76G control frame.
// step: Acquisition operation label retained in failure logs.
// value: Bounded encoded field or caller-owned parsed value.
// Exception: Returns original hardware or canonical publication failures.
esp_err_t gps_read_u32(
    const uint8_t command[8],
    const char *step,
    uint32_t *value
)
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t response[4] = {};
    const esp_err_t error = gps_exchange(
        command,
        8,
        response,
        sizeof(response),
        step
    );
    if (error != ESP_OK) {
        return error;
    }
    *value = gps_le_u32(response);
    return ESP_OK;
}

// data_length: Bounded response size or caller-owned pending-length output.
// Exception: Returns original hardware or canonical publication failures.
esp_err_t gps_read_pending_payload(uint32_t *data_length)
{
    if (data_length == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = gps_read_u32(
        kGpsPendingLengthCommand,
        "pending length",
        data_length
    );
    if (error != ESP_OK || *data_length == 0U) {
        return error;
    }
    if (*data_length > GPS_MAX_PENDING_BYTES) {
        ESP_LOGE(
            TAG,
            "GPS LC76G: pending payload length %lu exceeds safety limit",
            static_cast<unsigned long>(*data_length)
        );
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t read_length =
        *data_length < GPS_MAX_PAYLOAD ? *data_length : GPS_MAX_PAYLOAD;
    const uint8_t request[8] = {
        0x00, 0x20, 0x51, 0xAA,
        static_cast<uint8_t>(read_length),
        static_cast<uint8_t>(read_length >> 8),
        static_cast<uint8_t>(read_length >> 16),
        static_cast<uint8_t>(read_length >> 24),
    };
    error = gps_exchange(
        request,
        sizeof(request),
        reinterpret_cast<uint8_t *>(s_gps_payload),
        read_length,
        "pending payload"
    );
    if (error == ESP_OK) {
        s_gps_payload[read_length] = '\0';
        *data_length = read_length;
    }
    return error;
}

enum class GpsPairAck : uint8_t {
    None,
    Processing,
    Success,
    Rejected,
};

// payload: Borrowed bounded receiver payload.
// length: Exact number of bytes in the borrowed buffer.
// command_id: PAIR command identity whose final acknowledgment is required.
GpsPairAck gps_find_pair_ack(
    const char *payload,
    size_t length,
    uint16_t command_id
)
{
    if (payload == nullptr) {
        return GpsPairAck::None;
    }
    // The LC76G may place the in-progress and final PAIR001 responses in one
    // I2C payload. Retain an in-progress state, but keep scanning so the final
    // acknowledgement in the same drained payload is never discarded.
    GpsPairAck observed = GpsPairAck::None;
    size_t cursor = 0;
    while (cursor < length) {
        while (cursor < length && payload[cursor] != '$') {
            ++cursor;
        }
        const size_t start = cursor;
        while (cursor < length && payload[cursor] != '\r' &&
               payload[cursor] != '\n') {
            ++cursor;
        }
        const size_t sentence_length = cursor - start;
        if (sentence_length > 0U && sentence_length < 64U) {
            char sentence[64] = {};
            std::memcpy(sentence, payload + start, sentence_length);
            if (validate_and_strip_nmea_checksum(sentence, sentence_length) &&
                std::strncmp(sentence, "$PAIR001,", 9) == 0) {
                const char *command_text = sentence + 9;
                char *command_end = nullptr;
                const unsigned long acknowledged_command = std::strtoul(
                    command_text,
                    &command_end,
                    10
                );
                if (command_end != command_text && command_end != nullptr &&
                    *command_end == ',' &&
                    acknowledged_command == command_id &&
                    command_end[1] >= '0' && command_end[1] <= '5' &&
                    command_end[2] == '\0') {
                    if (command_end[1] == '0') {
                        return GpsPairAck::Success;
                    }
                    if (command_end[1] == '1') {
                        observed = GpsPairAck::Processing;
                        continue;
                    }
                    ESP_LOGE(
                        TAG,
                        "GPS LC76G: PAIR%03u rejected with result %c",
                        static_cast<unsigned>(command_id),
                        command_end[1]
                    );
                    return GpsPairAck::Rejected;
                }
            }
        }
        while (cursor < length && (payload[cursor] == '\r' ||
                                   payload[cursor] == '\n')) {
            ++cursor;
        }
    }
    return observed;
}

// command_id: PAIR command identity whose final acknowledgment is required.
// sentence: Bounded NMEA sentence processed by the acquisition owner.
// operation: Component operation name retained in transfer failure logs.
// Exception: Returns original hardware or canonical publication failures.
esp_err_t gps_send_pair_command(
    uint16_t command_id,
    const char *sentence,
    const char *operation
)
{
    if (sentence == nullptr || operation == nullptr || s_gps_control == nullptr ||
        s_gps_read == nullptr || s_gps_write == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t sentence_length = std::strlen(sentence);
    if (sentence_length == 0U || sentence_length > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t write_free_length = 0;
    esp_err_t error = gps_read_u32(
        kGpsWriteFreeLengthCommand,
        "write free length",
        &write_free_length
    );
    if (error != ESP_OK) {
        return error;
    }
    if (write_free_length < sentence_length) {
        ESP_LOGE(
            TAG,
            "GPS LC76G: %s rejected; write buffer=%lu bytes, command=%u bytes",
            operation,
            static_cast<unsigned long>(write_free_length),
            static_cast<unsigned>(sentence_length)
        );
        return ESP_ERR_NO_MEM;
    }

    uint8_t write_configuration[8] = {
        kGpsWriteConfigurationCommand[0],
        kGpsWriteConfigurationCommand[1],
        kGpsWriteConfigurationCommand[2],
        kGpsWriteConfigurationCommand[3],
        static_cast<uint8_t>(sentence_length),
        static_cast<uint8_t>(sentence_length >> 8),
        static_cast<uint8_t>(sentence_length >> 16),
        static_cast<uint8_t>(sentence_length >> 24),
    };
    error = gps_control_write(write_configuration, sizeof(write_configuration));
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GPS LC76G: %s write configuration failed (%s)",
            operation,
            esp_err_to_name(error)
        );
        return error;
    }
    vTaskDelay(kGpsI2cProtocolDelayTicks);
    error = i2c_master_transmit(
        s_gps_write,
        reinterpret_cast<const uint8_t *>(sentence),
        sentence_length,
        I2C_TIMEOUT_MS
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GPS LC76G: %s command write failed (%s)",
            operation,
            esp_err_to_name(error)
        );
        return error;
    }

    for (unsigned attempt = 1; attempt <= kGpsPairAckAttempts; ++attempt) {
        vTaskDelay(kGpsPairAckPollTicks);
        uint32_t payload_length = 0;
        error = gps_read_pending_payload(&payload_length);
        if (error != ESP_OK) {
            ESP_LOGW(
                TAG,
                "GPS LC76G: %s acknowledgement poll %u/%u failed (%s)",
                operation,
                attempt,
                kGpsPairAckAttempts,
                esp_err_to_name(error)
            );
            continue;
        }
        if (payload_length == 0U) {
            continue;
        }
        const GpsPairAck acknowledgement = gps_find_pair_ack(
            s_gps_payload,
            payload_length,
            command_id
        );
        if (acknowledgement == GpsPairAck::Success) {
            return ESP_OK;
        }
        if (acknowledgement == GpsPairAck::Rejected) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    ESP_LOGE(
        TAG,
        "GPS LC76G: %s did not receive final PAIR%03u acknowledgement",
        operation,
        static_cast<unsigned>(command_id)
    );
    return error == ESP_OK ? ESP_ERR_TIMEOUT : error;
}

// phase: Acquisition phase used for diagnostic logging and retry policy.
// Exception: Returns original hardware or canonical publication failures.
esp_err_t sample_gps(const char *phase)
{
    static const uint8_t length_command[] = {0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00};
    LUNASHELL::GpsSnapshot sample;
    const int64_t attempted_at_us = esp_timer_get_time();
    if (s_gps_power_suspended) {
        const esp_err_t error = ESP_ERR_INVALID_STATE;
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(snapshot.gps, error, attempted_at_us);
            }
        );
        ESP_LOGE(TAG, "GPS LC76G: %s sample rejected while GNSS is suspended", phase);
        return sample_publication_result(
            "GPS LC76G",
            phase,
            error,
            publication_error
        );
    }
    uint8_t length_bytes[4] = {};

    esp_err_t error = gps_exchange(
        length_command,
        sizeof(length_command),
        length_bytes,
        sizeof(length_bytes),
        "length"
    );
    const uint32_t data_length =
        static_cast<uint32_t>(length_bytes[0]) |
        (static_cast<uint32_t>(length_bytes[1]) << 8) |
        (static_cast<uint32_t>(length_bytes[2]) << 16) |
        (static_cast<uint32_t>(length_bytes[3]) << 24);
    sample.pending_nmea_bytes = data_length;
    const uint32_t read_length =
        data_length < GPS_MAX_PAYLOAD ? data_length : GPS_MAX_PAYLOAD;

    if (error == ESP_OK && data_length > GPS_MAX_PENDING_BYTES) {
        ESP_LOGW(
            TAG,
            "GPS LC76G: invalid pending length=%lu raw=[%02x %02x %02x %02x]",
            static_cast<unsigned long>(data_length),
            length_bytes[0],
            length_bytes[1],
            length_bytes[2],
            length_bytes[3]
        );
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK && data_length > GPS_MAX_PAYLOAD) {
        ESP_LOGI(
            TAG,
            "GPS LC76G: %lu bytes pending; draining a bounded %u-byte chunk",
            static_cast<unsigned long>(data_length),
            GPS_MAX_PAYLOAD
        );
    }
    if (error == ESP_OK && read_length > 0) {
        const uint8_t request[8] = {
            0x00, 0x20, 0x51, 0xAA,
            static_cast<uint8_t>(read_length),
            static_cast<uint8_t>(read_length >> 8),
            static_cast<uint8_t>(read_length >> 16),
            static_cast<uint8_t>(read_length >> 24),
        };
        error = gps_exchange(
            request,
            sizeof(request),
            reinterpret_cast<uint8_t *>(s_gps_payload),
            read_length,
            "payload"
        );
        if (error == ESP_OK) {
            s_gps_payload[read_length] = '\0';
            const NmeaPayloadResult parse_result =
                parse_nmea_payload(s_gps_payload, read_length, sample);
            if (parse_result.valid_sentence_count == 0) {
                ESP_LOGE(
                    TAG,
                    "GPS LC76G: payload has no valid supported NMEA sentence"
                );
                error = ESP_ERR_INVALID_RESPONSE;
            } else if (parse_result.coherence_error != nullptr) {
                ESP_LOGE(
                    TAG,
                    "GPS LC76G: incoherent NMEA payload: %s "
                    "(valid=%u malformed=%u)",
                    parse_result.coherence_error,
                    static_cast<unsigned>(parse_result.valid_sentence_count),
                    static_cast<unsigned>(
                        parse_result.malformed_supported_sentence_count
                    )
                );
                error = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    if (error != ESP_OK) {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(snapshot.gps, error, attempted_at_us);
            }
        );
        ESP_LOGW(TAG, "GPS LC76G: %s sample failed (%s)", phase, esp_err_to_name(error));
        if (publication_error == ESP_OK &&
            std::strcmp(phase, "periodic-full") == 0) {
            s_gps_periodic_error = error;
            s_gps_periodic_retry_after_us =
                esp_timer_get_time() + kGpsPeriodicRetryBackoffUs;
            ESP_LOGW(
                TAG,
                "GPS LC76G: periodic retry deferred 15 s after %s; "
                "sample remains invalid",
                esp_err_to_name(error)
            );
        }
        return sample_publication_result(
            "GPS LC76G",
            phase,
            error,
            publication_error
        );
    }
    mark_sample_success(sample, attempted_at_us);
    const esp_err_t publication_error = update_snapshot(
        [&sample](LUNASHELL::DeviceSnapshot &snapshot) {
            snapshot.gps = sample;
        }
    );
    esp_err_t aggregate = sample_publication_result(
        "GPS LC76G",
        phase,
        ESP_OK,
        publication_error
    );
    if (publication_error == ESP_OK) {
        s_gps_periodic_error = ESP_OK;
        s_gps_periodic_retry_after_us = 0;
    }
    if (high_frequency_sample_phase(phase)) {
        ESP_LOGD(
            TAG,
            "GPS LC76G %s: read=%lu pending-before=%lu byte(s), fix=%s",
            phase,
            static_cast<unsigned long>(read_length),
            static_cast<unsigned long>(data_length),
            sample.has_fix ? "yes" : "no"
        );
    } else if (read_length == 0) {
        ESP_LOGI(TAG, "GPS LC76G %s: transport online, no NMEA bytes waiting", phase);
    } else if (sample.has_fix) {
        ESP_LOGI(
            TAG,
            "GPS LC76G %s: read=%lu pending-before=%lu byte(s), fix=%s quality=%u sats=%u lat=%.6f lon=%.6f alt=%.1fm speed=%.2fm/s hdop=%.1f first=\"%s\"",
            phase,
            static_cast<unsigned long>(read_length),
            static_cast<unsigned long>(data_length),
            sample.has_fix ? "yes" : "no",
            sample.fix_quality,
            sample.satellites,
            sample.latitude_degrees,
            sample.longitude_degrees,
            sample.altitude_m,
            sample.speed_mps,
            sample.hdop,
            sample.first_sentence
        );
    } else {
        ESP_LOGI(
            TAG,
            "GPS LC76G %s: read=%lu pending-before=%lu byte(s), "
            "fix=no first=\"%s\"",
            phase,
            static_cast<unsigned long>(read_length),
            static_cast<unsigned long>(data_length),
            sample.first_sentence
        );
    }
    if (sample.has_fix && sample.utc_valid) {
        const uint16_t year = static_cast<uint16_t>(std::strtoul(sample.utc_date, nullptr, 10));
        const uint8_t month = static_cast<uint8_t>(std::strtoul(sample.utc_date + 5, nullptr, 10));
        const uint8_t day = static_cast<uint8_t>(std::strtoul(sample.utc_date + 8, nullptr, 10));
        const uint8_t hour = static_cast<uint8_t>(
            (sample.utc_time[0] - '0') * 10 + (sample.utc_time[1] - '0')
        );
        const uint8_t minute = static_cast<uint8_t>(
            (sample.utc_time[2] - '0') * 10 + (sample.utc_time[3] - '0')
        );
        const uint8_t second = static_cast<uint8_t>(
            (sample.utc_time[4] - '0') * 10 + (sample.utc_time[5] - '0')
        );
        const esp_err_t sync_error =
            LUNASHELL_time_sync_from_gps(year, month, day, hour, minute, second);
        if (sync_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GPS LC76G: valid fix read but UTC synchronization failed (%s)",
                esp_err_to_name(sync_error)
            );
            record_first_error(sync_error, &aggregate);
        }
    }
    return aggregate;
}
#endif
}  // namespace

// bus: Shared I2C bus owned by the hardware initialization stage.
// Exception: Returns the receiver initialization or publication failure.
esp_err_t initialize(i2c_master_bus_handle_t bus)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
    return init_gps(bus);
#else
    (void)bus;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// Initialization and refresh retain the hardware owner's existing synchronization.
bool ready()
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
    return s_gps_ready;
#else
    return false;
#endif
}

// phase: Acquisition phase used for existing periodic retry and log policy.
// Exception: Returns the original deferred read, acquisition or publication failure.
esp_err_t sample(const char *phase)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
    if (std::strcmp(phase, "periodic-full") == 0 &&
        s_gps_periodic_error != ESP_OK &&
        esp_timer_get_time() < s_gps_periodic_retry_after_us) {
        // The failed read was already published. A skipped period is no new sample.
        return s_gps_periodic_error;
    }
    return sample_gps(phase);
#else
    (void)phase;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// suspended: Whether GNSS must enter acknowledged subsystem standby.
// Exception: Returns original receiver, refresh-lock and publication failures.
esp_err_t set_power_suspended(bool suspended)
{
#if !CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
    (void)suspended;
    return ESP_ERR_NOT_SUPPORTED;
#else
    const int64_t attempted_at_us = esp_timer_get_time();
    if (!hardware_initialized() || !s_gps_ready) {
        ESP_LOGE(
            TAG,
            "GPS LC76G power transition to %s rejected: GNSS is unavailable",
            suspended ? "standby" : "active"
        );
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(
                    snapshot.gps,
                    ESP_ERR_INVALID_STATE,
                    attempted_at_us
                );
            }
        );
        if (publication_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GPS LC76G power transition rejection publication failed (%s)",
                esp_err_to_name(publication_error)
            );
        }
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "GPS LC76G power transition to %s timed out waiting for refresh",
            suspended ? "standby" : "active"
        );
        return ESP_ERR_TIMEOUT;
    }
    if (s_gps_power_suspended == suspended) {
        return give_refresh_mutex(refresh_mutex, "GPS LC76G power transition");
    }

    static constexpr char kPairPowerOff[] = "$PAIR003*39\r\n";
    static constexpr char kPairPowerOn[] = "$PAIR002*38\r\n";
    const esp_err_t error = gps_send_pair_command(
        suspended ? 3U : 2U,
        suspended ? kPairPowerOff : kPairPowerOn,
        suspended ? "GNSS subsystem power-off" : "GNSS subsystem power-on"
    );
    if (error == ESP_OK) {
        s_gps_power_suspended = suspended;
        if (!suspended) {
            s_gps_periodic_error = ESP_OK;
            s_gps_periodic_retry_after_us = 0;
        }
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(
                    snapshot.gps,
                    ESP_ERR_INVALID_STATE,
                    attempted_at_us
                );
            }
        );
        if (publication_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GPS LC76G power transition publication failed (%s)",
                esp_err_to_name(publication_error)
            );
            const esp_err_t release_error = give_refresh_mutex(
                refresh_mutex,
                "GPS LC76G power transition"
            );
            return release_error == ESP_OK ? publication_error : release_error;
        }
    } else {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(snapshot.gps, error, attempted_at_us);
            }
        );
        if (publication_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GPS LC76G failed transition publication (%s)",
                esp_err_to_name(publication_error)
            );
        }
    }
    const esp_err_t release_error = give_refresh_mutex(
        refresh_mutex,
        "GPS LC76G power transition"
    );
    if (release_error != ESP_OK) {
        return release_error;
    }
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GPS LC76G subsystem power-%s failed (%s)",
            suspended ? "off" : "on",
            esp_err_to_name(error)
        );
        return error;
    }
    ESP_LOGI(
        TAG,
        "GPS LC76G subsystem power-%s acknowledged",
        suspended ? "off" : "on"
    );
    return ESP_OK;
#endif
}

}  // namespace LUNASHELL::snapshot_detail::gnss
