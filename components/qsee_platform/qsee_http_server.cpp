/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * The HTTP API is intentionally read-only. It exposes one coherent
 * DeviceSnapshotService sample and never calls a peripheral driver from an
 * HTTP request handler.
 */

#include "qsee_http_server.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_platform.h"
#include "qsee_settings.hpp"

namespace {

constexpr const char *kTag = "qsee_http";
constexpr const char *kApiKeySetting = "ApiKey";
constexpr const char *kDefaultApiKey = "ap-0000000000000000001";
constexpr std::size_t kAuthorizationPrefixLength = 7U;
constexpr std::size_t kResponseBufferCapacity = 512U;
constexpr std::size_t kFormatBufferCapacity = 384U;

constexpr const char *kHealthPath = "/api/v1/health";
constexpr const char *kInfoPath = "/api/v1/info";
constexpr const char *kRtcPath = "/api/v1/rtc";
constexpr const char *kGpsPath = "/api/v1/gps";
constexpr const char *kStatusPath = "/api/v1/status";

httpd_handle_t s_server = nullptr;
bool s_initialized = false;
bool s_auth_ready = false;
char s_api_key[qsee::kHttpApiKeyMaxLength + 1U] = {};
char s_expected_authorization[qsee::kHttpApiKeyMaxLength + kAuthorizationPrefixLength + 1U] = {};
esp_event_handler_instance_t s_ip_event_instance = nullptr;
esp_event_handler_instance_t s_wifi_event_instance = nullptr;

class JsonResponse final {
public:
    explicit JsonResponse(httpd_req_t *request): _request(request) {
        (void)httpd_resp_set_type(_request, "application/json");
        (void)httpd_resp_set_hdr(_request, "Cache-Control", "no-store");
    }

    bool raw(const char *text) {
        if (text == nullptr) {
            return true;
        }
        while (*text != '\0') {
            if (_used == sizeof(_buffer) && !flush()) {
                return false;
            }
            const std::size_t available = sizeof(_buffer) - _used;
            const std::size_t length = std::strlen(text);
            const std::size_t copied = length < available ? length : available;
            std::memcpy(_buffer + _used, text, copied);
            _used += copied;
            text += copied;
        }
        return true;
    }

    bool format(const char *format_string, ...) {
        char rendered[kFormatBufferCapacity] = {};
        va_list arguments;
        va_start(arguments, format_string);
        const int length = std::vsnprintf(
            rendered,
            sizeof(rendered),
            format_string,
            arguments
        );
        va_end(arguments);
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(rendered)) {
            return false;
        }
        return raw(rendered);
    }

    bool quoted(const char *value) {
        if (!raw("\"")) {
            return false;
        }
        const char *safe_value = value != nullptr ? value : "";
        for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(safe_value);
             *cursor != '\0';
             ++cursor) {
            switch (*cursor) {
            case '\\': if (!raw("\\\\")) return false; break;
            case '"':  if (!raw("\\\"")) return false; break;
            case '\b': if (!raw("\\b")) return false; break;
            case '\f': if (!raw("\\f")) return false; break;
            case '\n': if (!raw("\\n")) return false; break;
            case '\r': if (!raw("\\r")) return false; break;
            case '\t': if (!raw("\\t")) return false; break;
            default:
                if (*cursor < 0x20U) {
                    if (!format("\\u%04x", static_cast<unsigned>(*cursor))) {
                        return false;
                    }
                } else if (!append(static_cast<char>(*cursor))) {
                    return false;
                }
                break;
            }
        }
        return raw("\"");
    }

    bool boolean(bool value) {
        return raw(value ? "true" : "false");
    }

    bool finish() {
        return flush() && httpd_resp_send_chunk(_request, nullptr, 0) == ESP_OK;
    }

private:
    bool append(char value) {
        if (_used == sizeof(_buffer) && !flush()) {
            return false;
        }
        _buffer[_used++] = value;
        return true;
    }

    bool flush() {
        if (_used == 0U) {
            return true;
        }
        const esp_err_t error = httpd_resp_send_chunk(_request, _buffer, _used);
        _used = 0U;
        return error == ESP_OK;
    }

    httpd_req_t *_request;
    char _buffer[kResponseBufferCapacity] = {};
    std::size_t _used = 0U;
};

esp_err_t loadApiKeyFromSettings()
{
    qsee::UserSettingsService &settings = qsee::UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error != ESP_OK) {
        return error;
    }

    std::string value;
    error = settings.getString(kApiKeySetting, kDefaultApiKey, &value);
    if (error != ESP_OK) {
        return error;
    }
    if (value.empty() || value.length() > qsee::kHttpApiKeyMaxLength) {
        ESP_LOGW(kTag, "Settings ApiKey must contain 1..%u characters",
                 static_cast<unsigned>(qsee::kHttpApiKeyMaxLength));
        return ESP_ERR_INVALID_SIZE;
    }

    std::memset(s_api_key, 0, sizeof(s_api_key));
    std::memcpy(s_api_key, value.data(), value.length());
    std::snprintf(
        s_expected_authorization,
        sizeof(s_expected_authorization),
        "Bearer %s",
        s_api_key
    );
    s_auth_ready = true;
    return ESP_OK;
}

bool constantTimeEquals(
    const char *actual,
    std::size_t actual_length,
    const char *expected,
    std::size_t expected_length
)
{
    const std::size_t length = actual_length > expected_length ? actual_length : expected_length;
    unsigned difference = static_cast<unsigned>(actual_length ^ expected_length);
    for (std::size_t index = 0U; index < length; ++index) {
        const unsigned actual_byte = index < actual_length
            ? static_cast<unsigned>(static_cast<unsigned char>(actual[index])) : 0U;
        const unsigned expected_byte = index < expected_length
            ? static_cast<unsigned>(static_cast<unsigned char>(expected[index])) : 0U;
        difference |= actual_byte ^ expected_byte;
    }
    return difference == 0U;
}

bool headerMatches(
    httpd_req_t *request,
    const char *header_name,
    const char *expected,
    std::size_t expected_length
)
{
    const std::size_t length = httpd_req_get_hdr_value_len(request, header_name);
    if (length != expected_length || length == 0U || length >= 96U) {
        return false;
    }
    char value[96] = {};
    if (httpd_req_get_hdr_value_str(request, header_name, value, sizeof(value)) != ESP_OK) {
        return false;
    }
    return constantTimeEquals(value, length, expected, expected_length);
}

bool authorized(httpd_req_t *request)
{
    return s_auth_ready &&
           (headerMatches(request, "Authorization", s_expected_authorization,
                          std::strlen(s_expected_authorization)) ||
            headerMatches(request, "X-API-Key", s_api_key, std::strlen(s_api_key)));
}

esp_err_t sendError(httpd_req_t *request, const char *status, const char *message, bool challenge)
{
    (void)httpd_resp_set_type(request, "application/json");
    (void)httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (challenge) {
        (void)httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer");
    }
    (void)httpd_resp_set_status(request, status);
    char body[160] = {};
    const int length = std::snprintf(
        body,
        sizeof(body),
        "{\"error\":\"%s\"}",
        message != nullptr ? message : "request failed"
    );
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(body)) {
        return httpd_resp_send(request, "{\"error\":\"request failed\"}", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(request, body, length);
}

void writeComponentFields(JsonResponse &json, const qsee::ComponentSnapshot &component)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(component.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(component.last_error));
    (void)json.raw("}");
}

void writeBatteryFull(JsonResponse &json, const qsee::BatterySnapshot &battery)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(battery.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(battery.last_error));
    (void)json.raw(",\"battery_present\":");
    (void)json.boolean(battery.battery_present);
    (void)json.raw(",\"usb_present\":");
    (void)json.boolean(battery.usb_present);
    (void)json.format(",\"level_percent\":%d,\"battery_mv\":%d,\"usb_mv\":%d,\"system_mv\":%d,\"die_temperature_c\":%.2f,\"power_state\":", battery.level_percent, battery.battery_mv, battery.usb_mv, battery.system_mv, static_cast<double>(battery.die_temperature_c));
    (void)json.quoted(battery.power_state);
    (void)json.raw("}");
}

void writeImu(JsonResponse &json, const qsee::ImuSnapshot &imu)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(imu.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(imu.last_error));
    (void)json.format(",\"acceleration_mps2\":[%.4f,%.4f,%.4f],\"angular_velocity_dps\":[%.4f,%.4f,%.4f],\"temperature_c\":%.2f,\"calibrated\":", static_cast<double>(imu.acceleration_mps2[0]), static_cast<double>(imu.acceleration_mps2[1]), static_cast<double>(imu.acceleration_mps2[2]), static_cast<double>(imu.angular_velocity_dps[0]), static_cast<double>(imu.angular_velocity_dps[1]), static_cast<double>(imu.angular_velocity_dps[2]), static_cast<double>(imu.temperature_c));
    (void)json.boolean(imu.calibrated);
    (void)json.format(",\"calibration_stationary\":");
    (void)json.boolean(imu.calibration_stationary);
    (void)json.format(",\"calibration_samples\":%u,\"calibration_accel_max_span_mps2\":%.4f,\"calibration_gyro_max_span_dps\":%.4f,\"acceleration_bias_mps2\":[%.4f,%.4f,%.4f],\"angular_velocity_bias_dps\":[%.4f,%.4f,%.4f]}", static_cast<unsigned>(imu.calibration_samples), static_cast<double>(imu.calibration_accel_max_span_mps2), static_cast<double>(imu.calibration_gyro_max_span_dps), static_cast<double>(imu.acceleration_bias_mps2[0]), static_cast<double>(imu.acceleration_bias_mps2[1]), static_cast<double>(imu.acceleration_bias_mps2[2]), static_cast<double>(imu.angular_velocity_bias_dps[0]), static_cast<double>(imu.angular_velocity_bias_dps[1]), static_cast<double>(imu.angular_velocity_bias_dps[2]));
}

void writeRtc(JsonResponse &json, const qsee::RtcSnapshot &rtc)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(rtc.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(rtc.last_error));
    (void)json.raw(",\"clock_integrity\":");
    (void)json.boolean(rtc.clock_integrity);
    (void)json.raw(",\"system_time_valid\":");
    (void)json.boolean(rtc.system_time_valid);
    (void)json.raw(",\"synchronized\":");
    (void)json.boolean(rtc.synchronized);
    (void)json.format(",\"year\":%u,\"month\":%u,\"day\":%u,\"hour\":%u,\"minute\":%u,\"second\":%u,\"source\":", static_cast<unsigned>(rtc.year), static_cast<unsigned>(rtc.month), static_cast<unsigned>(rtc.day), static_cast<unsigned>(rtc.hour), static_cast<unsigned>(rtc.minute), static_cast<unsigned>(rtc.second));
    (void)json.quoted(rtc.source);
    (void)json.raw(",\"timezone\":");
    (void)json.quoted(rtc.timezone);
    (void)json.raw("}");
}

void writeGps(JsonResponse &json, const qsee::GpsSnapshot &gps)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(gps.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(gps.last_error));
    (void)json.format(",\"pending_nmea_bytes\":%lu,\"has_fix\":", static_cast<unsigned long>(gps.pending_nmea_bytes));
    (void)json.boolean(gps.has_fix);
    (void)json.format(",\"fix_quality\":%u,\"satellites\":%u,\"latitude_degrees\":%.7f,\"longitude_degrees\":%.7f,\"altitude_m\":%.2f,\"speed_mps\":%.2f,\"course_degrees\":%.2f,\"hdop\":%.2f,\"utc_time\":", static_cast<unsigned>(gps.fix_quality), static_cast<unsigned>(gps.satellites), gps.latitude_degrees, gps.longitude_degrees, static_cast<double>(gps.altitude_m), static_cast<double>(gps.speed_mps), static_cast<double>(gps.course_degrees), static_cast<double>(gps.hdop));
    (void)json.quoted(gps.utc_time);
    (void)json.raw(",\"utc_date\":");
    (void)json.quoted(gps.utc_date);
    (void)json.raw(",\"first_sentence\":");
    (void)json.quoted(gps.first_sentence);
    (void)json.raw("}");
}

void writeAudio(JsonResponse &json, const qsee::AudioSnapshot &audio)
{
    (void)json.raw("{\"speaker\":");
    writeComponentFields(json, audio.speaker);
    (void)json.raw(",\"microphone\":");
    writeComponentFields(json, audio.microphone);
    (void)json.format(",\"channel_count\":%u,\"sample_rate_hz\":%lu,\"microphone_mean_level\":%lu,\"microphone_peak_level\":%lu,\"channel_mean_level\":[%lu,%lu],\"channel_peak_level\":[%lu,%lu]}", static_cast<unsigned>(audio.channel_count), static_cast<unsigned long>(audio.sample_rate_hz), static_cast<unsigned long>(audio.microphone_mean_level), static_cast<unsigned long>(audio.microphone_peak_level), static_cast<unsigned long>(audio.channel_mean_level[0]), static_cast<unsigned long>(audio.channel_mean_level[1]), static_cast<unsigned long>(audio.channel_peak_level[0]), static_cast<unsigned long>(audio.channel_peak_level[1]));
}

void writeWifi(JsonResponse &json, const qsee::WifiSnapshot &wifi)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(wifi.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(wifi.last_error));
    (void)json.raw(",\"started\":");
    (void)json.boolean(wifi.started);
    (void)json.raw(",\"credentials_configured\":");
    (void)json.boolean(wifi.credentials_configured);
    (void)json.raw(",\"connect_requested\":");
    (void)json.boolean(wifi.connect_requested);
    (void)json.raw(",\"connected\":");
    (void)json.boolean(wifi.connected);
    (void)json.raw(",\"ip_acquired\":");
    (void)json.boolean(wifi.ip_acquired);
    (void)json.raw(",\"reconnect_pending\":");
    (void)json.boolean(wifi.reconnect_pending);
    (void)json.format(",\"rssi\":%d,\"channel\":%u,\"last_disconnect_reason\":%u,\"event_sequence\":%lu,\"connect_attempt_count\":%lu,\"reconnect_attempt_count\":%lu,\"disconnect_count\":%lu,\"ssid\":", static_cast<int>(wifi.rssi), static_cast<unsigned>(wifi.channel), static_cast<unsigned>(wifi.last_disconnect_reason), static_cast<unsigned long>(wifi.event_sequence), static_cast<unsigned long>(wifi.connect_attempt_count), static_cast<unsigned long>(wifi.reconnect_attempt_count), static_cast<unsigned long>(wifi.disconnect_count));
    (void)json.quoted(wifi.ssid);
    (void)json.raw(",\"ip_address\":");
    (void)json.quoted(wifi.ip_address);
    (void)json.raw(",\"mac_address\":");
    (void)json.quoted(wifi.mac_address);
    (void)json.raw("}");
}

void writeBle(JsonResponse &json, const qsee::BleSnapshot &ble)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(ble.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(ble.last_error));
    (void)json.raw(",\"provisioning\":");
    (void)json.boolean(ble.provisioning);
    (void)json.raw(",\"advertising\":");
    (void)json.boolean(ble.advertising);
    (void)json.raw(",\"connected\":");
    (void)json.boolean(ble.connected);
    (void)json.format(",\"connection_handle\":%u,\"connection_count\":%lu,\"disconnection_count\":%lu,\"device_name\":", static_cast<unsigned>(ble.connection_handle), static_cast<unsigned long>(ble.connection_count), static_cast<unsigned long>(ble.disconnection_count));
    (void)json.quoted(ble.device_name);
    (void)json.raw("}");
}

void writeStorage(JsonResponse &json, const qsee::StorageSnapshot &storage)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(storage.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(storage.last_error));
    (void)json.raw(",\"card_present\":");
    (void)json.boolean(storage.card_present);
    (void)json.raw(",\"format_attempted\":");
    (void)json.boolean(storage.format_attempted);
    (void)json.raw(",\"formatted\":");
    (void)json.boolean(storage.formatted);
    (void)json.format(",\"mount_attempt_count\":%lu,\"capacity_bytes\":%llu,\"filesystem_total_bytes\":%llu,\"free_bytes\":%llu,\"logger_mirror_enabled\":", static_cast<unsigned long>(storage.mount_attempt_count), static_cast<unsigned long long>(storage.capacity_bytes), static_cast<unsigned long long>(storage.filesystem_total_bytes), static_cast<unsigned long long>(storage.free_bytes));
    (void)json.boolean(storage.logger_mirror_enabled);
    (void)json.raw("}");
}

void writeUi(JsonResponse &json, const qsee::UiHardwareSnapshot &ui)
{
    (void)json.raw("{\"display\":");
    writeComponentFields(json, ui.display);
    (void)json.raw(",\"touch\":");
    writeComponentFields(json, ui.touch);
    (void)json.raw("}");
}

void writeInput(JsonResponse &json, const qsee::InputSnapshot &input)
{
    (void)json.raw("{\"io_expander\":");
    writeComponentFields(json, input.io_expander);
    (void)json.raw(",\"boot_button\":");
    writeComponentFields(json, input.boot_button);
    (void)json.raw(",\"power_button\":");
    writeComponentFields(json, input.power_button);
    (void)json.raw(",\"boot_pressed\":");
    (void)json.boolean(input.boot_pressed);
    (void)json.raw(",\"power_pressed\":");
    (void)json.boolean(input.power_pressed);
    (void)json.format(",\"boot_press_count\":%lu,\"power_short_press_count\":%lu,\"power_long_press_count\":%lu,\"wakeup_cause\":%lu}", static_cast<unsigned long>(input.boot_press_count), static_cast<unsigned long>(input.power_short_press_count), static_cast<unsigned long>(input.power_long_press_count), static_cast<unsigned long>(input.wakeup_cause));
}

void writeMemory(JsonResponse &json, const qsee::MemorySnapshot &memory)
{
    (void)json.format("{\"flash_capacity_bytes\":%lu,\"app_partition_bytes\":%lu,\"firmware_image_bytes\":%lu,\"heap_free_bytes\":%lu,\"heap_minimum_free_bytes\":%lu,\"internal_free_bytes\":%lu,\"internal_largest_block_bytes\":%lu,\"internal_memory_pressure\":", static_cast<unsigned long>(memory.flash_capacity_bytes), static_cast<unsigned long>(memory.app_partition_bytes), static_cast<unsigned long>(memory.firmware_image_bytes), static_cast<unsigned long>(memory.heap_free_bytes), static_cast<unsigned long>(memory.heap_minimum_free_bytes), static_cast<unsigned long>(memory.internal_free_bytes), static_cast<unsigned long>(memory.internal_largest_block_bytes));
    (void)json.boolean(memory.internal_memory_pressure);
    (void)json.format(",\"psram_capacity_bytes\":%lu,\"psram_heap_total_bytes\":%lu,\"psram_free_bytes\":%lu,\"psram_used_bytes\":%lu,\"psram_largest_block_bytes\":%lu}", static_cast<unsigned long>(memory.psram_capacity_bytes), static_cast<unsigned long>(memory.psram_heap_total_bytes), static_cast<unsigned long>(memory.psram_free_bytes), static_cast<unsigned long>(memory.psram_used_bytes), static_cast<unsigned long>(memory.psram_largest_block_bytes));
}

void writeProcessor(JsonResponse &json, const qsee::ProcessorSnapshot &processor)
{
    (void)json.raw("{\"core_count\":");
    (void)json.format("%u,\"sampling_core\":%u,\"cores\":[", static_cast<unsigned>(processor.core_count), static_cast<unsigned>(processor.sampling_core));
    for (std::size_t index = 0U; index < 2U; ++index) {
        if (index != 0U) {
            (void)json.raw(",");
        }
        (void)json.raw("{\"current_task\":");
        (void)json.quoted(processor.cores[index].current_task);
        (void)json.format(",\"stack_high_water_words\":%lu,\"priority\":%lu}", static_cast<unsigned long>(processor.cores[index].stack_high_water_words), static_cast<unsigned long>(processor.cores[index].priority));
    }
    (void)json.raw("]}");
}

qsee::DeviceHealth overallHealth(const qsee::DeviceSnapshot &snapshot)
{
    const qsee::ComponentSnapshot *components[] = {
        &snapshot.battery, &snapshot.imu, &snapshot.rtc, &snapshot.gps,
        &snapshot.audio.speaker, &snapshot.audio.microphone,
        &snapshot.radios.wifi, &snapshot.radios.ble,
        &snapshot.storage, &snapshot.ui.display, &snapshot.ui.touch,
        &snapshot.input.io_expander, &snapshot.input.boot_button,
        &snapshot.input.power_button,
    };
    bool unknown = false;
    bool degraded = false;
    bool missing = false;
    for (const qsee::ComponentSnapshot *component : components) {
        missing = missing || component->health == qsee::DeviceHealth::Missing;
        degraded = degraded || component->health == qsee::DeviceHealth::Degraded;
        unknown = unknown || component->health == qsee::DeviceHealth::Unknown;
    }
    if (missing) return qsee::DeviceHealth::Missing;
    if (degraded) return qsee::DeviceHealth::Degraded;
    if (unknown) return qsee::DeviceHealth::Unknown;
    return qsee::DeviceHealth::Ready;
}

void writeInfo(JsonResponse &json, const qsee::DeviceSnapshot &snapshot)
{
    (void)json.raw("{\"schema\":\"qsee.device.info.v1\",\"sequence\":");
    (void)json.format("%lu,\"refreshed_at_us\":%lld,\"reason\":", static_cast<unsigned long>(snapshot.sequence), static_cast<long long>(snapshot.refreshed_at_us));
    (void)json.quoted(snapshot.reason);
    (void)json.raw(",\"battery\":");
    writeBatteryFull(json, snapshot.battery);
    (void)json.raw(",\"imu\":");
    writeImu(json, snapshot.imu);
    (void)json.raw(",\"rtc\":");
    writeRtc(json, snapshot.rtc);
    (void)json.raw(",\"gps\":");
    writeGps(json, snapshot.gps);
    (void)json.raw(",\"audio\":");
    writeAudio(json, snapshot.audio);
    (void)json.raw(",\"radios\":{\"wifi\":");
    writeWifi(json, snapshot.radios.wifi);
    (void)json.raw(",\"ble\":");
    writeBle(json, snapshot.radios.ble);
    (void)json.raw("},\"storage\":");
    writeStorage(json, snapshot.storage);
    (void)json.raw(",\"ui\":");
    writeUi(json, snapshot.ui);
    (void)json.raw(",\"input\":");
    writeInput(json, snapshot.input);
    (void)json.raw(",\"memory\":");
    writeMemory(json, snapshot.memory);
    (void)json.raw(",\"processor\":");
    writeProcessor(json, snapshot.processor);
    (void)json.raw("}");
}

void writeHealth(JsonResponse &json, const qsee::DeviceSnapshot &snapshot)
{
    qsee_wifi_status_t wifi = {};
    qsee_wifi_get_status(&wifi);
    (void)json.raw("{\"schema\":\"qsee.device.health.v1\",\"ok\":true,\"service\":\"qsee-device-http\",\"api_version\":1,\"auth_required_for_data\":true,\"wifi_connected\":");
    (void)json.boolean(wifi.connected && wifi.ip_acquired);
    (void)json.format(",\"server_running\":true,\"snapshot_sequence\":%lu}", static_cast<unsigned long>(snapshot.sequence));
}

void writeStatus(JsonResponse &json, const qsee::DeviceSnapshot &snapshot)
{
    (void)json.raw("{\"schema\":\"qsee.device.status.v1\",\"sequence\":");
    (void)json.format("%lu,\"overall\":", static_cast<unsigned long>(snapshot.sequence));
    (void)json.quoted(qsee::deviceHealthName(overallHealth(snapshot)));
    (void)json.raw(",\"components\":{");
    struct NamedComponent { const char *name; const qsee::ComponentSnapshot *component; };
    const NamedComponent components[] = {
        {"battery", &snapshot.battery}, {"rtc", &snapshot.rtc},
        {"imu", &snapshot.imu}, {"gps", &snapshot.gps},
        {"speaker", &snapshot.audio.speaker}, {"microphone", &snapshot.audio.microphone},
        {"wifi", &snapshot.radios.wifi}, {"ble", &snapshot.radios.ble},
        {"storage", &snapshot.storage}, {"display", &snapshot.ui.display},
        {"touch", &snapshot.ui.touch}, {"io_expander", &snapshot.input.io_expander},
        {"boot_button", &snapshot.input.boot_button}, {"power_button", &snapshot.input.power_button},
    };
    for (std::size_t index = 0U; index < sizeof(components) / sizeof(components[0]); ++index) {
        if (index != 0U) (void)json.raw(",");
        (void)json.quoted(components[index].name);
        (void)json.raw(":");
        writeComponentFields(json, *components[index].component);
    }
    (void)json.raw("},\"wifi\":{\"connected\":");
    (void)json.boolean(snapshot.radios.wifi.connected);
    (void)json.raw(",\"ip_acquired\":");
    (void)json.boolean(snapshot.radios.wifi.ip_acquired);
    (void)json.raw(",\"ip_address\":");
    (void)json.quoted(snapshot.radios.wifi.ip_address);
    (void)json.raw("},\"gps\":{\"has_fix\":");
    (void)json.boolean(snapshot.gps.has_fix);
    (void)json.format(",\"satellites\":%u},\"storage\":{\"card_present\":", static_cast<unsigned>(snapshot.gps.satellites));
    (void)json.boolean(snapshot.storage.card_present);
    (void)json.raw(",\"formatted\":");
    (void)json.boolean(snapshot.storage.formatted);
    (void)json.raw("},\"memory\":{\"heap_free_bytes\":");
    (void)json.format("%lu,\"internal_memory_pressure\":", static_cast<unsigned long>(snapshot.memory.heap_free_bytes));
    (void)json.boolean(snapshot.memory.internal_memory_pressure);
    (void)json.raw("}}");
}

esp_err_t handleApiRequest(httpd_req_t *request)
{
    const char *route = static_cast<const char *>(request->user_ctx);
    if (route == nullptr) {
        return sendError(request, "500 Internal Server Error", "route is not configured", false);
    }
    if (std::strcmp(route, kHealthPath) != 0 && !authorized(request)) {
        return sendError(request, "401 Unauthorized", "authentication required", true);
    }

    const qsee::DeviceSnapshot snapshot = qsee::DeviceSnapshotService::instance().snapshot();
    JsonResponse response(request);
    if (std::strcmp(route, kHealthPath) == 0) {
        writeHealth(response, snapshot);
    } else if (std::strcmp(route, kInfoPath) == 0) {
        writeInfo(response, snapshot);
    } else if (std::strcmp(route, kRtcPath) == 0) {
        writeRtc(response, snapshot.rtc);
    } else if (std::strcmp(route, kGpsPath) == 0) {
        writeGps(response, snapshot.gps);
    } else if (std::strcmp(route, kStatusPath) == 0) {
        writeStatus(response, snapshot);
    } else {
        return sendError(request, "404 Not Found", "route not found", false);
    }
    return response.finish() ? ESP_OK : ESP_FAIL;
}

esp_err_t startServer()
{
    if (s_server != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = qsee::kHttpServerPort;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 8;
    config.stack_size = 6144;
    config.lru_purge_enable = true;

    httpd_handle_t server = nullptr;
    esp_err_t error = httpd_start(&server, &config);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP server start failed: %s", esp_err_to_name(error));
        return error;
    }

    const httpd_uri_t routes[] = {
        {.uri = kHealthPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kHealthPath)},
        {.uri = kInfoPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kInfoPath)},
        {.uri = kRtcPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kRtcPath)},
        {.uri = kGpsPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kGpsPath)},
        {.uri = kStatusPath, .method = HTTP_GET, .handler = handleApiRequest, .user_ctx = const_cast<char *>(kStatusPath)},
    };
    for (const httpd_uri_t &route : routes) {
        error = httpd_register_uri_handler(server, &route);
        if (error != ESP_OK) {
            (void)httpd_stop(server);
            ESP_LOGW(kTag, "HTTP route registration failed: %s", esp_err_to_name(error));
            return error;
        }
    }
    s_server = server;
    ESP_LOGI(kTag, "HTTP API online on port %u", static_cast<unsigned>(qsee::kHttpServerPort));
    return ESP_OK;
}

void stopServer()
{
    if (s_server == nullptr) {
        return;
    }
    const esp_err_t error = httpd_stop(s_server);
    s_server = nullptr;
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP server stop failed: %s", esp_err_to_name(error));
    } else {
        ESP_LOGI(kTag, "HTTP API offline until the next Wi-Fi lease");
    }
}

void networkEventHandler(void *argument, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        (void)startServer();
        return;
    }
    if ((event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) ||
        (event_base == WIFI_EVENT &&
         (event_id == WIFI_EVENT_STA_DISCONNECTED || event_id == WIFI_EVENT_STA_STOP))) {
        stopServer();
    }
}

}  // namespace

namespace qsee {

esp_err_t httpServerInit()
{
    if (s_initialized) {
        return ESP_OK;
    }

    const esp_err_t wifi_error = qsee_wifi_init();
    if (wifi_error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API requires Wi-Fi initialization: %s", esp_err_to_name(wifi_error));
        return wifi_error;
    }
    esp_err_t error = loadApiKeyFromSettings();
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTP API key initialization failed: %s", esp_err_to_name(error));
        return error;
    }

    error = esp_event_handler_instance_register(
        IP_EVENT,
        ESP_EVENT_ANY_ID,
        networkEventHandler,
        nullptr,
        &s_ip_event_instance
    );
    if (error != ESP_OK) {
        return error;
    }
    error = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        networkEventHandler,
        nullptr,
        &s_wifi_event_instance
    );
    if (error != ESP_OK) {
        (void)esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_event_instance);
        s_ip_event_instance = nullptr;
        return error;
    }

    s_initialized = true;
    qsee_wifi_status_t wifi = {};
    qsee_wifi_get_status(&wifi);
    if (wifi.ip_acquired) {
        (void)startServer();
    }
    ESP_LOGI(kTag, "HTTP API initialized; health probe is public and data routes require API key");
    return ESP_OK;
}

bool httpServerGetApiKey(char *buffer, std::size_t capacity)
{
    if (!s_auth_ready || buffer == nullptr || capacity <= std::strlen(s_api_key)) {
        return false;
    }
    std::memcpy(buffer, s_api_key, std::strlen(s_api_key) + 1U);
    return true;
}

esp_err_t httpServerReloadApiKey()
{
    return loadApiKeyFromSettings();
}

}  // namespace qsee
