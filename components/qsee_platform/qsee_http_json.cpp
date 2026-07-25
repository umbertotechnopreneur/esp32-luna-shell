/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * This file owns bounded JSON serialization. HTTP lifecycle, authentication,
 * rate limiting, and Wi-Fi event handling remain in qsee_http_server.cpp.
 */

// JSON serialization helpers for the read-only HTTP API.
// Converts device snapshots and metrics into compact response bodies.

#include "qsee_http_json.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "esp_timer.h"

namespace {

constexpr std::size_t kFormatBufferCapacity = 384U;
constexpr uint32_t kApiVersion = 1U;

uint64_t snapshotAgeMs(const qsee::DeviceSnapshot &snapshot)
{
    if (snapshot.last_update_attempt_at_us <= 0) {
        return 0U;
    }
    const int64_t now = esp_timer_get_time();
    if (now <= snapshot.last_update_attempt_at_us) {
        return 0U;
    }
    return static_cast<uint64_t>(now - snapshot.last_update_attempt_at_us) / 1000ULL;
}

void writeEnvelopeStart(
    qsee::HttpJsonWriter &json,
    const char *schema,
    const qsee::DeviceSnapshot &snapshot
)
{
    (void)json.raw("{\"schema\":");
    (void)json.quoted(schema);
    (void)json.format(
        ",\"api_version\":%lu,\"sequence\":%lu,\"last_update_attempt_at_us\":%lld,\"last_update_complete\":",
        static_cast<unsigned long>(kApiVersion),
        static_cast<unsigned long>(snapshot.sequence),
        static_cast<long long>(snapshot.last_update_attempt_at_us)
    );
    (void)json.boolean(snapshot.last_update_complete);
    (void)json.format(",\"age_ms\":%llu,\"data\":",
                      static_cast<unsigned long long>(snapshotAgeMs(snapshot)));
}

void writeComponentFields(
    qsee::HttpJsonWriter &json,
    const qsee::ComponentSnapshot &component
)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(component.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(component.last_error));
    (void)json.raw(",\"data_valid\":");
    (void)json.boolean(component.data_valid);
    (void)json.raw(",\"data_stale\":");
    (void)json.boolean(component.data_stale);
    (void)json.format(
        ",\"last_attempt_at_us\":%lld,\"last_success_at_us\":%lld}",
        static_cast<long long>(component.last_attempt_at_us),
        static_cast<long long>(component.last_success_at_us)
    );
}

void writeBattery(qsee::HttpJsonWriter &json, const qsee::BatterySnapshot &battery)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(battery.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(battery.last_error));
    (void)json.raw(",\"battery_present\":");
    (void)json.boolean(battery.battery_present);
    (void)json.raw(",\"usb_present\":");
    (void)json.boolean(battery.usb_present);
    (void)json.format(
        ",\"level_percent\":%d,\"battery_mv\":%d,\"usb_mv\":%d,\"system_mv\":%d,\"die_temperature_c\":%.2f,\"power_state\":",
        battery.level_percent,
        battery.battery_mv,
        battery.usb_mv,
        battery.system_mv,
        static_cast<double>(battery.die_temperature_c)
    );
    (void)json.quoted(battery.power_state);
    (void)json.raw("}");
}

void writeImu(qsee::HttpJsonWriter &json, const qsee::ImuSnapshot &imu)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(imu.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(imu.last_error));
    (void)json.format(
        ",\"acceleration_mps2\":[%.4f,%.4f,%.4f],\"angular_velocity_dps\":[%.4f,%.4f,%.4f],\"temperature_c\":%.2f,\"calibrated\":",
        static_cast<double>(imu.acceleration_mps2[0]),
        static_cast<double>(imu.acceleration_mps2[1]),
        static_cast<double>(imu.acceleration_mps2[2]),
        static_cast<double>(imu.angular_velocity_dps[0]),
        static_cast<double>(imu.angular_velocity_dps[1]),
        static_cast<double>(imu.angular_velocity_dps[2]),
        static_cast<double>(imu.temperature_c)
    );
    (void)json.boolean(imu.calibrated);
    (void)json.raw(",\"calibration_stationary\":");
    (void)json.boolean(imu.calibration_stationary);
    (void)json.format(
        ",\"calibration_samples\":%u,\"calibration_accel_max_span_mps2\":%.4f,\"calibration_gyro_max_span_dps\":%.4f,\"acceleration_bias_mps2\":[%.4f,%.4f,%.4f],\"angular_velocity_bias_dps\":[%.4f,%.4f,%.4f]}",
        static_cast<unsigned>(imu.calibration_samples),
        static_cast<double>(imu.calibration_accel_max_span_mps2),
        static_cast<double>(imu.calibration_gyro_max_span_dps),
        static_cast<double>(imu.acceleration_bias_mps2[0]),
        static_cast<double>(imu.acceleration_bias_mps2[1]),
        static_cast<double>(imu.acceleration_bias_mps2[2]),
        static_cast<double>(imu.angular_velocity_bias_dps[0]),
        static_cast<double>(imu.angular_velocity_bias_dps[1]),
        static_cast<double>(imu.angular_velocity_bias_dps[2])
    );
}

void writeRtcData(qsee::HttpJsonWriter &json, const qsee::RtcSnapshot &rtc)
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
    (void)json.format(
        ",\"year\":%u,\"month\":%u,\"day\":%u,\"hour\":%u,\"minute\":%u,\"second\":%u,\"source\":",
        static_cast<unsigned>(rtc.year),
        static_cast<unsigned>(rtc.month),
        static_cast<unsigned>(rtc.day),
        static_cast<unsigned>(rtc.hour),
        static_cast<unsigned>(rtc.minute),
        static_cast<unsigned>(rtc.second)
    );
    (void)json.quoted(rtc.source);
    (void)json.raw(",\"timezone\":");
    (void)json.quoted(rtc.timezone);
    (void)json.raw("}");
}

void writeGpsData(
    qsee::HttpJsonWriter &json,
    const qsee::GpsSnapshot &gps,
    bool include_sensitive
)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(gps.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(gps.last_error));
    (void)json.format(
        ",\"pending_nmea_bytes\":%lu,\"has_fix\":",
        static_cast<unsigned long>(gps.pending_nmea_bytes)
    );
    (void)json.boolean(gps.has_fix);
    (void)json.format(
        ",\"fix_quality\":%u,\"satellites\":%u,\"latitude_degrees\":%.7f,\"longitude_degrees\":%.7f,\"altitude_m\":%.2f,\"speed_mps\":%.2f,\"course_degrees\":%.2f,\"hdop\":%.2f,\"utc_time\":",
        static_cast<unsigned>(gps.fix_quality),
        static_cast<unsigned>(gps.satellites),
        gps.latitude_degrees,
        gps.longitude_degrees,
        static_cast<double>(gps.altitude_m),
        static_cast<double>(gps.speed_mps),
        static_cast<double>(gps.course_degrees),
        static_cast<double>(gps.hdop)
    );
    (void)json.quoted(gps.utc_time);
    (void)json.raw(",\"utc_date\":");
    (void)json.quoted(gps.utc_date);
    if (include_sensitive) {
        (void)json.raw(",\"first_sentence\":");
        (void)json.quoted(gps.first_sentence);
    }
    (void)json.raw("}");
}

void writeAudio(qsee::HttpJsonWriter &json, const qsee::AudioSnapshot &audio)
{
    (void)json.raw("{\"speaker\":");
    writeComponentFields(json, audio.speaker);
    (void)json.raw(",\"microphone\":");
    writeComponentFields(json, audio.microphone);
    (void)json.format(
        ",\"channel_count\":%u,\"sample_rate_hz\":%lu,\"microphone_mean_level\":%lu,\"microphone_peak_level\":%lu,\"channel_mean_level\":[%lu,%lu],\"channel_peak_level\":[%lu,%lu]}",
        static_cast<unsigned>(audio.channel_count),
        static_cast<unsigned long>(audio.sample_rate_hz),
        static_cast<unsigned long>(audio.microphone_mean_level),
        static_cast<unsigned long>(audio.microphone_peak_level),
        static_cast<unsigned long>(audio.channel_mean_level[0]),
        static_cast<unsigned long>(audio.channel_mean_level[1]),
        static_cast<unsigned long>(audio.channel_peak_level[0]),
        static_cast<unsigned long>(audio.channel_peak_level[1])
    );
}

void writeWifi(
    qsee::HttpJsonWriter &json,
    const qsee::WifiSnapshot &wifi,
    bool include_sensitive
)
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
    (void)json.format(
        ",\"rssi\":%d,\"channel\":%u,\"last_disconnect_reason\":%u,\"event_sequence\":%lu,\"connect_attempt_count\":%lu,\"reconnect_attempt_count\":%lu,\"disconnect_count\":%lu",
        static_cast<int>(wifi.rssi),
        static_cast<unsigned>(wifi.channel),
        static_cast<unsigned>(wifi.last_disconnect_reason),
        static_cast<unsigned long>(wifi.event_sequence),
        static_cast<unsigned long>(wifi.connect_attempt_count),
        static_cast<unsigned long>(wifi.reconnect_attempt_count),
        static_cast<unsigned long>(wifi.disconnect_count)
    );
    if (include_sensitive) {
        (void)json.raw(",\"ssid\":");
        (void)json.quoted(wifi.ssid);
        (void)json.raw(",\"ip_address\":");
        (void)json.quoted(wifi.ip_address);
        (void)json.raw(",\"mac_address\":");
        (void)json.quoted(wifi.mac_address);
    }
    (void)json.raw("}");
}

void writeBle(qsee::HttpJsonWriter &json, const qsee::BleSnapshot &ble)
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
    (void)json.format(
        ",\"connection_handle\":%u,\"connection_count\":%lu,\"disconnection_count\":%lu,\"device_name\":",
        static_cast<unsigned>(ble.connection_handle),
        static_cast<unsigned long>(ble.connection_count),
        static_cast<unsigned long>(ble.disconnection_count)
    );
    (void)json.quoted(ble.device_name);
    (void)json.raw("}");
}

void writeStorage(qsee::HttpJsonWriter &json, const qsee::StorageSnapshot &storage)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(qsee::deviceHealthName(storage.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(storage.last_error));
    (void)json.raw(",\"card_response_verified\":");
    (void)json.boolean(storage.card_response_verified);
    (void)json.raw(",\"format_attempted\":");
    (void)json.boolean(storage.format_attempted);
    (void)json.raw(",\"formatted\":");
    (void)json.boolean(storage.formatted);
    (void)json.format(
        ",\"mount_attempt_count\":%lu,\"capacity_bytes\":%llu,\"filesystem_total_bytes\":%llu,\"free_bytes\":%llu,\"logger_mirror_enabled\":",
        static_cast<unsigned long>(storage.mount_attempt_count),
        static_cast<unsigned long long>(storage.capacity_bytes),
        static_cast<unsigned long long>(storage.filesystem_total_bytes),
        static_cast<unsigned long long>(storage.free_bytes)
    );
    (void)json.boolean(storage.logger_mirror_enabled);
    (void)json.raw("}");
}

// Serialize UI hardware using only values committed by the snapshot service.
void writeUi(qsee::HttpJsonWriter &json, const qsee::UiHardwareSnapshot &ui)
{
    (void)json.raw("{\"display\":");
    writeComponentFields(json, ui.display);
    (void)json.raw(",\"touch\":");
    writeComponentFields(json, ui.touch);
    (void)json.raw(",\"brightness\":");
    writeComponentFields(json, ui.brightness);
    (void)json.format(",\"brightness_percent\":%lu}",
                      static_cast<unsigned long>(ui.brightness_percent));
    (void)json.raw("}");
}

void writeInput(qsee::HttpJsonWriter &json, const qsee::InputSnapshot &input)
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
    (void)json.format(
        ",\"boot_press_count\":%lu,\"power_short_press_count\":%lu,\"power_long_press_count\":%lu,\"wakeup_cause\":%lu}",
        static_cast<unsigned long>(input.boot_press_count),
        static_cast<unsigned long>(input.power_short_press_count),
        static_cast<unsigned long>(input.power_long_press_count),
        static_cast<unsigned long>(input.wakeup_cause)
    );
}

void writeMemory(qsee::HttpJsonWriter &json, const qsee::MemorySnapshot &memory)
{
    (void)json.format(
        "{\"flash_capacity_bytes\":%lu,\"app_partition_bytes\":%lu,\"firmware_image_bytes\":%lu,\"heap_free_bytes\":%lu,\"heap_minimum_free_bytes\":%lu,\"internal_free_bytes\":%lu,\"internal_largest_block_bytes\":%lu,\"internal_memory_pressure\":",
        static_cast<unsigned long>(memory.flash_capacity_bytes),
        static_cast<unsigned long>(memory.app_partition_bytes),
        static_cast<unsigned long>(memory.firmware_image_bytes),
        static_cast<unsigned long>(memory.heap_free_bytes),
        static_cast<unsigned long>(memory.heap_minimum_free_bytes),
        static_cast<unsigned long>(memory.internal_free_bytes),
        static_cast<unsigned long>(memory.internal_largest_block_bytes)
    );
    (void)json.boolean(memory.internal_memory_pressure);
    (void)json.format(
        ",\"psram_capacity_bytes\":%lu,\"psram_heap_total_bytes\":%lu,\"psram_free_bytes\":%lu,\"psram_used_bytes\":%lu,\"psram_largest_block_bytes\":%lu}",
        static_cast<unsigned long>(memory.psram_capacity_bytes),
        static_cast<unsigned long>(memory.psram_heap_total_bytes),
        static_cast<unsigned long>(memory.psram_free_bytes),
        static_cast<unsigned long>(memory.psram_used_bytes),
        static_cast<unsigned long>(memory.psram_largest_block_bytes)
    );
}

void writeProcessor(qsee::HttpJsonWriter &json, const qsee::ProcessorSnapshot &processor)
{
    (void)json.format(
        "{\"core_count\":%u,\"sampling_core\":%u,\"cores\":[",
        static_cast<unsigned>(processor.core_count),
        static_cast<unsigned>(processor.sampling_core)
    );
    for (std::size_t index = 0U; index < 2U; ++index) {
        if (index != 0U) (void)json.raw(",");
        (void)json.raw("{\"current_task\":");
        (void)json.quoted(processor.cores[index].current_task);
        (void)json.format(
            ",\"stack_high_water_words\":%lu,\"priority\":%lu}",
            static_cast<unsigned long>(processor.cores[index].stack_high_water_words),
            static_cast<unsigned long>(processor.cores[index].priority)
        );
    }
    (void)json.raw("]}");
}

// Collapse required component health into the public device health value.
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

void writeInfoData(
    qsee::HttpJsonWriter &json,
    const qsee::DeviceSnapshot &snapshot,
    bool include_sensitive
)
{
    (void)json.raw("{\"reason\":");
    (void)json.quoted(snapshot.reason);
    (void)json.raw(",\"battery\":");
    writeBattery(json, snapshot.battery);
    (void)json.raw(",\"imu\":");
    writeImu(json, snapshot.imu);
    (void)json.raw(",\"rtc\":");
    writeRtcData(json, snapshot.rtc);
    (void)json.raw(",\"gps\":");
    writeGpsData(json, snapshot.gps, include_sensitive);
    (void)json.raw(",\"audio\":");
    writeAudio(json, snapshot.audio);
    (void)json.raw(",\"radios\":{\"wifi\":");
    writeWifi(json, snapshot.radios.wifi, include_sensitive);
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

}  // namespace

namespace qsee {

HttpJsonWriter::HttpJsonWriter(httpd_req_t *request): _request(request)
{
    (void)httpd_resp_set_type(_request, "application/json");
    (void)httpd_resp_set_hdr(_request, "Cache-Control", "no-store");
}

// Stream arbitrary text through the fixed response buffer.
bool HttpJsonWriter::raw(const char *text)
{
    if (_failed || text == nullptr) return !_failed;
    while (*text != '\0') {
        if (_used == sizeof(_buffer) && !flush()) return false;
        const std::size_t available = sizeof(_buffer) - _used;
        const std::size_t length = std::strlen(text);
        const std::size_t copied = length < available ? length : available;
        std::memcpy(_buffer + _used, text, copied);
        _used += copied;
        text += copied;
    }
    return true;
}

bool HttpJsonWriter::format(const char *format_string, ...)
{
    char rendered[kFormatBufferCapacity] = {};
    va_list arguments;
    va_start(arguments, format_string);
    const int length = std::vsnprintf(rendered, sizeof(rendered), format_string, arguments);
    va_end(arguments);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(rendered)) {
        _failed = true;
        return false;
    }
    return raw(rendered);
}

// Append a null-safe JSON string with control-character escaping.
bool HttpJsonWriter::quoted(const char *value)
{
    if (!raw("\"")) return false;
    const char *safe_value = value != nullptr ? value : "";
    for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(safe_value);
         *cursor != '\0'; ++cursor) {
        switch (*cursor) {
        case '\\': if (!raw("\\\\")) return false; break;
        case '"': if (!raw("\\\"")) return false; break;
        case '\b': if (!raw("\\b")) return false; break;
        case '\f': if (!raw("\\f")) return false; break;
        case '\n': if (!raw("\\n")) return false; break;
        case '\r': if (!raw("\\r")) return false; break;
        case '\t': if (!raw("\\t")) return false; break;
        default:
            if (*cursor < 0x20U) {
                if (!format("\\u%04x", static_cast<unsigned>(*cursor))) return false;
            } else if (!append(static_cast<char>(*cursor))) {
                return false;
            }
            break;
        }
    }
    return raw("\"");
}

bool HttpJsonWriter::boolean(bool value)
{
    return raw(value ? "true" : "false");
}

// Flush buffered JSON and terminate the chunked HTTP response.
bool HttpJsonWriter::finish()
{
    if (_failed || !flush()) return false;
    const esp_err_t error = httpd_resp_send_chunk(_request, nullptr, 0);
    if (error != ESP_OK) _failed = true;
    return error == ESP_OK;
}

bool HttpJsonWriter::append(char value)
{
    if (_used == sizeof(_buffer) && !flush()) return false;
    _buffer[_used++] = value;
    return true;
}

bool HttpJsonWriter::flush()
{
    if (_used == 0U) return true;
    const esp_err_t error = httpd_resp_send_chunk(_request, _buffer, _used);
    _used = 0U;
    if (error != ESP_OK) _failed = true;
    return error == ESP_OK;
}

void httpJsonWriteHealth(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    bool server_running
)
{
    (void)json.raw("{\"schema\":\"qsee.device.health.v1\",\"api_version\":1,\"data\":{\"ok\":true,\"server_running\":");
    (void)json.boolean(server_running);
    (void)json.raw(",\"auth_required_for_data\":true,\"wifi_connected\":");
    (void)json.boolean(snapshot.radios.wifi.connected && snapshot.radios.wifi.ip_acquired);
    (void)json.format(",\"snapshot\":{\"sequence\":%lu,\"last_update_attempt_at_us\":%lld,\"last_update_complete\":",
                       static_cast<unsigned long>(snapshot.sequence),
                       static_cast<long long>(snapshot.last_update_attempt_at_us));
    (void)json.boolean(snapshot.last_update_complete);
    (void)json.format(",\"age_ms\":%llu}}}",
                      static_cast<unsigned long long>(snapshotAgeMs(snapshot)));
}

void httpJsonWriteInfo(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    bool include_sensitive
)
{
    writeEnvelopeStart(json, "qsee.device.info.v1", snapshot);
    writeInfoData(json, snapshot, include_sensitive);
    (void)json.raw("}");
}

void httpJsonWriteRtc(HttpJsonWriter &json, const DeviceSnapshot &snapshot)
{
    writeEnvelopeStart(json, "qsee.device.rtc.v1", snapshot);
    writeRtcData(json, snapshot.rtc);
    (void)json.raw("}");
}

void httpJsonWriteGps(HttpJsonWriter &json, const DeviceSnapshot &snapshot)
{
    writeEnvelopeStart(json, "qsee.device.gps.v1", snapshot);
    writeGpsData(json, snapshot.gps, false);
    (void)json.raw("}");
}

void httpJsonWriteStatus(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    const HttpMetricsView &metrics,
    bool server_running
)
{
    writeEnvelopeStart(json, "qsee.device.status.v1", snapshot);
    (void)json.raw("{\"overall\":");
    (void)json.quoted(deviceHealthName(overallHealth(snapshot)));
    (void)json.raw(",\"server_running\":");
    (void)json.boolean(server_running);
    (void)json.raw(",\"components\":{");
    struct NamedComponent {
        const char *name;
        const ComponentSnapshot *component;
    };
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
    (void)json.raw("},\"gps\":{\"has_fix\":");
    (void)json.boolean(snapshot.gps.has_fix);
    (void)json.format(",\"satellites\":%u},\"wifi\":{\"connected\":",
                      static_cast<unsigned>(snapshot.gps.satellites));
    (void)json.boolean(snapshot.radios.wifi.connected);
    (void)json.raw(",\"ip_acquired\":");
    (void)json.boolean(snapshot.radios.wifi.ip_acquired);
    (void)json.raw("},\"storage\":{\"card_response_verified\":");
    (void)json.boolean(snapshot.storage.card_response_verified);
    (void)json.raw(",\"formatted\":");
    (void)json.boolean(snapshot.storage.formatted);
    (void)json.raw("},\"metrics\":{");
    (void)json.format(
        "\"request_count\":%lu,\"authorized_count\":%lu,\"unauthorized_count\":%lu,\"response_failure_count\":%lu,\"start_count\":%lu,\"stop_count\":%lu,\"start_failure_count\":%lu,\"auth_failures_in_window\":%lu}",
        static_cast<unsigned long>(metrics.request_count),
        static_cast<unsigned long>(metrics.authorized_count),
        static_cast<unsigned long>(metrics.unauthorized_count),
        static_cast<unsigned long>(metrics.response_failure_count),
        static_cast<unsigned long>(metrics.start_count),
        static_cast<unsigned long>(metrics.stop_count),
        static_cast<unsigned long>(metrics.start_failure_count),
        static_cast<unsigned long>(metrics.auth_failures_in_window)
    );
    (void)json.raw("}}");
}

void httpJsonWriteCapabilities(HttpJsonWriter &json, uint32_t refresh_interval_ms)
{
    (void)json.raw(
        "{\"schema\":\"qsee.device.capabilities.v1\",\"api_version\":1,\"data\":{\"read_only\":true,\"snapshot_refresh_interval_ms\":"
    );
    (void)json.format("%lu,\"features\":[\"battery\",\"rtc\",\"imu\",\"gps\",\"audio\",\"wifi\",\"ble\",\"storage\",\"display\",\"touch\"],\"endpoints\":[\"/api/v1/health\",\"/api/v1/capabilities\",\"/api/v1/info\",\"/api/v1/rtc\",\"/api/v1/gps\",\"/api/v1/status\",\"/api/v1/diagnostics\"]}}", static_cast<unsigned long>(refresh_interval_ms));
}

}  // namespace qsee
