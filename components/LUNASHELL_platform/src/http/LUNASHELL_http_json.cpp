/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * This file owns bounded JSON serialization. HTTP lifecycle, authentication,
 * rate limiting, and Wi-Fi event handling remain in LUNASHELL_http_server.cpp.
 */

// JSON serialization helpers for the read-only HTTP API.
// Converts device snapshots and metrics into compact response bodies.

#include "LUNASHELL_http_json.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "LUNASHELL_liveness.h"

namespace {

constexpr std::size_t kFormatBufferCapacity = 384U;
constexpr uint32_t kApiVersion = 1U;

// json: Serializer holding the owner-provided monotonic observation time.
// snapshot: Canonical or explicitly synthetic original snapshot timestamps.
uint64_t snapshotAgeMs(const LUNASHELL::HttpJsonWriter &json, const LUNASHELL::DeviceSnapshot &snapshot)
{
    if (snapshot.last_update_attempt_at_us <= 0) {
        return 0U;
    }
    const int64_t now = json.observationTimeUs();
    if (now <= snapshot.last_update_attempt_at_us) {
        return 0U;
    }
    return static_cast<uint64_t>(now - snapshot.last_update_attempt_at_us) / 1000ULL;
}

// json: Transport-independent bounded output owner.
// schema: Exact reviewed public response schema identity.
// snapshot: Original canonical or explicitly synthetic measurements.
void writeEnvelopeStart(
    LUNASHELL::HttpJsonWriter &json,
    const char *schema,
    const LUNASHELL::DeviceSnapshot &snapshot
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
    (void)json.raw(",\"last_update_error\":");
    (void)json.quoted(esp_err_to_name(snapshot.last_update_error));
    (void)json.format(",\"age_ms\":%llu,\"data\":",
                      static_cast<unsigned long long>(snapshotAgeMs(json, snapshot)));
}

bool componentCurrent(const LUNASHELL::ComponentSnapshot &component)
{
    return component.health == LUNASHELL::DeviceHealth::Ready &&
           component.last_error == ESP_OK &&
           component.data_valid &&
           !component.data_stale;
}

void writeComponentStart(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::ComponentSnapshot &component
)
{
    (void)json.raw("{\"health\":");
    (void)json.quoted(LUNASHELL::deviceHealthName(component.health));
    (void)json.raw(",\"last_error\":");
    (void)json.quoted(esp_err_to_name(component.last_error));
    (void)json.raw(",\"data_valid\":");
    (void)json.boolean(component.data_valid);
    (void)json.raw(",\"data_stale\":");
    (void)json.boolean(component.data_stale);
    (void)json.format(
        ",\"last_attempt_at_us\":%lld,\"last_success_at_us\":%lld",
        static_cast<long long>(component.last_attempt_at_us),
        static_cast<long long>(component.last_success_at_us)
    );
}

void writeComponentFields(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::ComponentSnapshot &component
)
{
    writeComponentStart(json, component);
    (void)json.raw("}");
}

void writeBattery(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::BatterySnapshot &battery)
{
    const bool battery_current = componentCurrent(battery);
    const bool level_percent_valid =
        battery_current &&
        battery.level_percent_valid &&
        battery.level_percent >= 0 &&
        battery.level_percent <= 100;
    const bool battery_mv_valid =
        battery_current &&
        battery.battery_mv_valid &&
        battery.battery_mv > 0;

    writeComponentStart(json, battery);
    (void)json.raw(",\"battery_present\":");
    (void)json.boolean(battery.battery_present);
    (void)json.raw(",\"usb_present\":");
    (void)json.boolean(battery.usb_present);
    (void)json.raw(",\"vbus_good\":");
    (void)json.boolean(battery.vbus_good);
    (void)json.raw(",\"batfet_enabled\":");
    (void)json.boolean(battery.batfet_enabled);
    (void)json.raw(",\"battery_active_mode\":");
    (void)json.boolean(battery.battery_active_mode);
    (void)json.raw(",\"thermal_regulation_active\":");
    (void)json.boolean(battery.thermal_regulation_active);
    (void)json.raw(",\"input_current_limit_active\":");
    (void)json.boolean(battery.input_current_limit_active);
    (void)json.raw(",\"level_percent_valid\":");
    (void)json.boolean(level_percent_valid);
    (void)json.raw(",\"level_percent\":");
    if (level_percent_valid) {
        (void)json.format("%d", battery.level_percent);
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"battery_mv_valid\":");
    (void)json.boolean(battery_mv_valid);
    (void)json.raw(",\"battery_mv\":");
    if (battery_mv_valid) {
        (void)json.format("%d", battery.battery_mv);
    } else {
        (void)json.raw("null");
    }
    (void)json.format(
        ",\"usb_mv\":%d,\"system_mv\":%d,\"die_temperature_c\":%.2f,\"power_state\":",
        battery.usb_mv,
        battery.system_mv,
        static_cast<double>(battery.die_temperature_c)
    );
    (void)json.quoted(battery.power_state);
    (void)json.raw(",\"charger_phase\":");
    (void)json.quoted(battery.charger_phase);
    (void)json.raw("}");
}

void writeImu(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::ImuSnapshot &imu)
{
    writeComponentStart(json, imu);
    (void)json.raw(",\"calibration\":");
    writeComponentFields(json, imu.calibration);
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

void writeSensorFusion(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::SensorFusionSnapshot &fusion
)
{
    writeComponentStart(json, fusion);
    (void)json.raw(",\"gravity_dominant\":");
    (void)json.boolean(fusion.gravity_dominant);
    (void)json.raw(",\"stationary\":");
    (void)json.boolean(fusion.stationary);
    (void)json.format(
        ",\"acceleration_magnitude_mps2\":%.4f,\"angular_speed_dps\":%.4f}",
        static_cast<double>(fusion.acceleration_magnitude_mps2),
        static_cast<double>(fusion.angular_speed_dps)
    );
}

// period: Published routine identity; never use a numeric table position as its meaning.
const char *routinePeriodName(LUNASHELL::RoutinePeriod period)
{
    using LUNASHELL::RoutinePeriod;
    switch (period) {
    case RoutinePeriod::Morning:
        return "morning";
    case RoutinePeriod::Afternoon:
        return "afternoon";
    case RoutinePeriod::Evening:
        return "evening";
    case RoutinePeriod::Night:
        return "night";
    case RoutinePeriod::Unavailable:
        return "unavailable";
    }
    return "unavailable";
}

// json: Existing bounded writer; it retains the original serialization error.
// snapshot: Canonical RTC, system-clock and historical synchronization observations.
void writeRtcData(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::DeviceSnapshot &snapshot)
{
    const auto &rtc = snapshot.rtc;
    writeComponentStart(json, rtc);
    (void)json.raw(",\"system_time\":");
    writeComponentFields(json, rtc.system_time);
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
    const auto &clock = snapshot.clock;
    const bool clock_current = LUNASHELL::clock_observation_current(clock);
    (void)json.raw(",\"clock\":");
    writeComponentStart(json, clock);
    (void)json.raw(",\"utc_epoch_seconds\":");
    if (clock_current) {
        (void)json.format("%lld", static_cast<long long>(clock.utc_epoch_seconds));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"local_minute\":");
    if (clock_current) {
        (void)json.format("%u", clock.local_minute);
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"period\":");
    (void)json.quoted(routinePeriodName(clock_current ? clock.period : LUNASHELL::RoutinePeriod::Unavailable));
    (void)json.raw(",\"quiet\":");
    if (clock_current) {
        (void)json.boolean(clock.quiet);
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"preferences_valid\":");
    (void)json.boolean(clock.preferences_valid);
    (void)json.raw(",\"routines_enabled\":");
    (void)json.boolean(clock.routines_enabled);
    (void)json.raw(",\"quiet_hours_enabled\":");
    (void)json.boolean(clock.quiet_hours_enabled);
    if (clock.preferences_valid) {
        (void)json.format(",\"quiet_start_minute\":%u,\"quiet_end_minute\":%u}",
            clock.quiet_start_minute, clock.quiet_end_minute);
    } else {
        (void)json.raw(",\"quiet_start_minute\":null,\"quiet_end_minute\":null}");
    }
    const auto &drift = snapshot.rtc_drift;
    (void)json.raw(",\"drift\":");
    writeComponentStart(json, drift);
    (void)json.raw(",\"baseline\":");
    writeComponentFields(json, drift.baseline);
    (void)json.raw(",\"rtc_minus_reference_seconds\":");
    if (drift.data_valid && drift.offset_valid && drift.last_error == ESP_OK) {
        (void)json.format("%ld", static_cast<long>(drift.rtc_minus_reference_seconds));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"rate_ppm\":");
    if (drift.data_valid && drift.rate_valid && drift.last_error == ESP_OK) {
        (void)json.format("%.3f", static_cast<double>(drift.rate_ppm));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"rate_uncertainty_ppm\":");
    if (drift.data_valid && drift.rate_valid && drift.last_error == ESP_OK) {
        (void)json.format("%.3f", static_cast<double>(drift.rate_uncertainty_ppm));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"interval_seconds\":");
    if (drift.data_valid && drift.rate_valid && drift.last_error == ESP_OK) {
        (void)json.format("%lu", static_cast<unsigned long>(drift.interval_seconds));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"offset_uncertainty_seconds\":");
    if (drift.data_valid && drift.offset_valid && drift.last_error == ESP_OK) {
        (void)json.format("%lu", static_cast<unsigned long>(drift.offset_uncertainty_seconds));
    } else {
        (void)json.raw("null");
    }
    (void)json.format(",\"rtc_sampled_at_us\":%lld,\"source\":", static_cast<long long>(drift.rtc_sampled_at_us));
    (void)json.quoted(drift.source);
    (void)json.raw(",\"reference_utc_seconds\":");
    if (drift.data_valid && drift.offset_valid && drift.last_error == ESP_OK) {
        (void)json.format("%lld", static_cast<long long>(drift.reference_utc_seconds));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw("}");
    (void)json.raw("}");
}

void writeGpsData(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::GpsSnapshot &gps,
    bool include_sensitive
)
{
    const bool fix_valid =
        componentCurrent(gps) && gps.has_fix && gps.utc_valid &&
        gps.fix_quality > 0U && gps.satellites > 0U &&
        std::isfinite(gps.latitude_degrees) &&
        gps.latitude_degrees >= -90.0 && gps.latitude_degrees <= 90.0 &&
        std::isfinite(gps.longitude_degrees) &&
        gps.longitude_degrees >= -180.0 && gps.longitude_degrees <= 180.0 &&
        std::isfinite(gps.altitude_m) && std::isfinite(gps.speed_mps) &&
        gps.speed_mps >= 0.0F && std::isfinite(gps.course_degrees) &&
        gps.course_degrees >= 0.0F && gps.course_degrees <= 360.0F &&
        std::isfinite(gps.hdop) && gps.hdop >= 0.0F;

    writeComponentStart(json, gps);
    (void)json.format(
        ",\"pending_nmea_bytes\":%lu,\"has_fix\":",
        static_cast<unsigned long>(gps.pending_nmea_bytes)
    );
    (void)json.boolean(gps.has_fix);
    (void)json.raw(",\"utc_valid\":");
    (void)json.boolean(gps.utc_valid);
    (void)json.raw(",\"fix_valid\":");
    (void)json.boolean(fix_valid);
    (void)json.format(
        ",\"fix_quality\":%u,\"satellites\":%u,\"latitude_degrees\":",
        static_cast<unsigned>(gps.fix_quality),
        static_cast<unsigned>(gps.satellites)
    );
    if (fix_valid) {
        (void)json.format("%.7f", gps.latitude_degrees);
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"longitude_degrees\":");
    if (fix_valid) {
        (void)json.format("%.7f", gps.longitude_degrees);
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"altitude_m\":");
    if (fix_valid) {
        (void)json.format("%.2f", static_cast<double>(gps.altitude_m));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"speed_mps\":");
    if (fix_valid) {
        (void)json.format("%.2f", static_cast<double>(gps.speed_mps));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"course_degrees\":");
    if (fix_valid) {
        (void)json.format("%.2f", static_cast<double>(gps.course_degrees));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"hdop\":");
    if (fix_valid) {
        (void)json.format("%.2f", static_cast<double>(gps.hdop));
    } else {
        (void)json.raw("null");
    }
    (void)json.raw(",\"utc_time\":");
    (void)json.quoted(gps.utc_time);
    (void)json.raw(",\"utc_date\":");
    (void)json.quoted(gps.utc_date);
    if (include_sensitive) {
        (void)json.raw(",\"first_sentence\":");
        (void)json.quoted(gps.first_sentence);
    }
    (void)json.raw("}");
}

void writeAudio(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::AudioSnapshot &audio)
{
    (void)json.raw("{\"speaker\":");
    writeComponentFields(json, audio.speaker);
    (void)json.raw(",\"microphone\":");
    writeComponentFields(json, audio.microphone);
    (void)json.raw(",\"spectrum\":");
    writeComponentFields(json, audio.spectrum);
    (void)json.format(
        ",\"channel_count\":%u,\"sample_rate_hz\":%lu,\"microphone_mean_level\":%lu,\"microphone_peak_level\":%lu,\"channel_mean_level\":[%lu,%lu],\"channel_peak_level\":[%lu,%lu]",
        static_cast<unsigned>(audio.channel_count),
        static_cast<unsigned long>(audio.sample_rate_hz),
        static_cast<unsigned long>(audio.microphone_mean_level),
        static_cast<unsigned long>(audio.microphone_peak_level),
        static_cast<unsigned long>(audio.channel_mean_level[0]),
        static_cast<unsigned long>(audio.channel_mean_level[1]),
        static_cast<unsigned long>(audio.channel_peak_level[0]),
        static_cast<unsigned long>(audio.channel_peak_level[1])
    );
    (void)json.raw(",\"channel_spectrum\":[");
    for (std::size_t channel = 0U; channel < 2U; ++channel) {
        if (channel != 0U) (void)json.raw(",");
        (void)json.raw("[");
        for (std::size_t band = 0U;
             band < LUNASHELL::kAudioSpectrumBandCount;
             ++band) {
            if (band != 0U) (void)json.raw(",");
            (void)json.format(
                "%u",
                static_cast<unsigned>(audio.channel_spectrum[channel][band])
            );
        }
        (void)json.raw("]");
    }
    (void)json.format(
        "],\"spectrum_sequence\":%lu}",
        static_cast<unsigned long>(audio.spectrum_sequence)
    );
}

// state: Confirmed recognition service lifecycle, independent of event history.
const char *voiceStateName(LUNASHELL::VoiceState state)
{
    switch (state) {
    case LUNASHELL::VoiceState::Disabled:
        return "disabled";
    case LUNASHELL::VoiceState::Starting:
        return "starting";
    case LUNASHELL::VoiceState::Listening:
        return "listening";
    case LUNASHELL::VoiceState::Paused:
        return "paused";
    case LUNASHELL::VoiceState::Failed:
        return "failed";
    }
    return "invalid";
}

// word: Stable domain identity, never an ESP-SR array position.
const char *wakeWordName(LUNASHELL::WakeWordId word)
{
    switch (word) {
    case LUNASHELL::WakeWordId::None:
        return "none";
    case LUNASHELL::WakeWordId::Sophia:
        return "Sophia";
    }
    return "invalid";
}

// json: Existing bounded JSON writer, preserving any earlier writer failure.
// voice: Canonical state and original historical event time.
void writeVoice(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::VoiceSnapshot &voice)
{
    writeComponentStart(json, voice);
    (void)json.raw(",\"state\":");
    (void)json.quoted(voiceStateName(voice.state));
    (void)json.raw(",\"configured_word\":");
    (void)json.quoted(wakeWordName(voice.configured_word));
    (void)json.raw(",\"detected_word\":");
    (void)json.quoted(wakeWordName(voice.detected_word));
    (void)json.raw(",\"model_name\":");
    (void)json.quoted(voice.model_name);
    (void)json.format(",\"detection_sequence\":%llu,\"detected_at_us\":%lld}",
                      static_cast<unsigned long long>(voice.detection_sequence),
                      static_cast<long long>(voice.detected_at_us));
}

void writeWifi(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::WifiSnapshot &wifi,
    bool include_sensitive
)
{
    writeComponentStart(json, wifi);
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

void writeBle(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::BleSnapshot &ble)
{
    writeComponentStart(json, ble);
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

// state: Typed state observed by the provisioning owner.
const char *provisioningStateName(LUNASHELL::ProvisioningState state)
{
    using State = LUNASHELL::ProvisioningState;
    switch (state) {
    case State::Idle:
        return "idle";
    case State::Starting:
        return "starting";
    case State::WaitingForApp:
        return "waiting-for-app";
    case State::CredentialsReceived:
        return "credentials-received";
    case State::Connecting:
        return "connecting";
    case State::Success:
        return "success";
    case State::Failed:
        return "failed";
    case State::Stopping:
        return "stopping";
    case State::Deinitializing:
        return "deinitializing";
    case State::Stopped:
        return "stopped";
    case State::Poisoned:
        return "poisoned";
    }
    return "unknown";
}

// reason: Typed failure reported by the credential manager.
const char *provisioningFailureName(LUNASHELL::ProvisioningFailureReason reason)
{
    using Reason = LUNASHELL::ProvisioningFailureReason;
    switch (reason) {
    case Reason::None:
        return "none";
    case Reason::Authentication:
        return "authentication";
    case Reason::AccessPointNotFound:
        return "access-point-not-found";
    case Reason::Unknown:
        return "unknown";
    }
    return "unknown";
}

// json: Bounded streaming writer; transport failures remain in its status.
// provisioning: Canonical lifecycle state; deliberately contains no secrets.
void writeProvisioning(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::ProvisioningSnapshot &provisioning)
{
    writeComponentStart(json, provisioning);
    (void)json.raw(",\"state\":");
    (void)json.quoted(provisioningStateName(provisioning.state));
    (void)json.raw(",\"failure_reason\":");
    (void)json.quoted(provisioningFailureName(provisioning.failure_reason));
    (void)json.raw(",\"running\":");
    (void)json.boolean(provisioning.running);
    (void)json.raw(",\"initialized\":");
    (void)json.boolean(provisioning.initialized);
    (void)json.format(",\"session_id\":%lu,\"event_sequence\":%lu,\"changed_at_us\":%lld,\"cleanup_error\":",
        static_cast<unsigned long>(provisioning.session_id),
        static_cast<unsigned long>(provisioning.event_sequence),
        static_cast<long long>(provisioning.changed_at_us));
    (void)json.quoted(esp_err_to_name(provisioning.cleanup_error));
    (void)json.raw("}");
}

// state: Typed controller state observed in the canonical snapshot.
const char *pairingSessionStateName(LUNASHELL::PairingSessionState state)
{
    using State = LUNASHELL::PairingSessionState;
    switch (state) {
    case State::Idle:
        return "idle";
    case State::Starting:
        return "starting";
    case State::ReadyToScan:
        return "ready-to-scan";
    case State::Connecting:
        return "connecting";
    case State::Completing:
        return "completing";
    case State::Online:
        return "online";
    case State::Cancelling:
        return "cancelling";
    case State::Cancelled:
        return "cancelled";
    case State::Failed:
        return "failed";
    case State::TimedOut:
        return "timed-out";
    }
    return "unknown";
}

// json: Bounded writer retaining all transport errors.
// pairing: Canonical controller status; no QR or credentials are present.
void writePairingSession(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::PairingSessionSnapshot &pairing)
{
    writeComponentStart(json, pairing);
    (void)json.raw(",\"state\":");
    (void)json.quoted(pairingSessionStateName(pairing.state));
    (void)json.raw(",\"failure_reason\":");
    (void)json.quoted(provisioningFailureName(pairing.failure_reason));
    (void)json.raw(",\"command_pending\":");
    (void)json.boolean(pairing.command_pending);
    (void)json.format(",\"attempt_id\":%lu,\"provisioning_session_id\":%lu,\"changed_at_us\":%lld,\"deadline_at_us\":%lld,\"cleanup_error\":",
        static_cast<unsigned long>(pairing.attempt_id),
        static_cast<unsigned long>(pairing.provisioning_session_id),
        static_cast<long long>(pairing.changed_at_us),
        static_cast<long long>(pairing.deadline_at_us));
    (void)json.quoted(esp_err_to_name(pairing.cleanup_error));
    (void)json.raw(",\"dma_reservation_error\":");
    (void)json.quoted(esp_err_to_name(pairing.dma_reservation_error));
    (void)json.raw("}");
}

// state: Typed CCP1 commissioning/liveness state from the canonical snapshot.
const char *codexLinkStateName(LUNASHELL::CodexLinkState state)
{
    using State = LUNASHELL::CodexLinkState;
    switch (state) {
    case State::Idle: return "idle";
    case State::StartingPairing: return "starting-pairing";
    case State::Pairing: return "pairing";
    case State::Paired: return "paired";
    case State::Discovering: return "discovering";
    case State::Connected: return "connected";
    case State::Failed: return "failed";
    case State::TimedOut: return "timed-out";
    case State::AwaitingConfirmation: return "awaiting-confirmation";
    }
    return "unknown";
}

// state: Typed activity from the authenticated CCP acquisition owner.
const char *codexActivityStateName(LUNASHELL::CodexActivityState state)
{
    using Activity = LUNASHELL::CodexActivityState;
    switch (state) {
    case Activity::None: return "none";
    case Activity::Active: return "active";
    case Activity::Working: return "working";
    case Activity::Editing: return "editing";
    case Activity::Running: return "running";
    case Activity::ResponseReady: return "ready";
    case Activity::Paused: return "paused";
    case Activity::Interrupted: return "interrupted";
    case Activity::Ended: return "ended";
    case Activity::Unavailable: return "unavailable";
    }
    return "unknown";
}

// json: Bounded writer retaining all transport errors.
// codex_link: Canonical non-secret CCP1 status; code, salt, proofs and keys are excluded.
void writeCodexLink(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::CodexLinkSnapshot &codex_link
)
{
    writeComponentStart(json, codex_link);
    (void)json.raw(",\"state\":");
    (void)json.quoted(codexLinkStateName(codex_link.state));
    (void)json.raw(",\"command_pending\":");
    (void)json.boolean(codex_link.command_pending);
    (void)json.raw(",\"pairing_window_active\":");
    (void)json.boolean(codex_link.pairing_window_active);
    (void)json.raw(",\"retained_link_present\":");
    (void)json.boolean(codex_link.retained_link_present);
    (void)json.raw(",\"pending_pairing_present\":");
    (void)json.boolean(codex_link.pending_pairing_present);
    (void)json.raw(",\"pairing_repair_required\":");
    (void)json.boolean(codex_link.pairing_repair_required);
    (void)json.format(
        ",\"attempt_id\":%lu,\"changed_at_us\":%lld,\"deadline_at_us\":%lld,\"ready_at_us\":%lld",
        static_cast<unsigned long>(codex_link.attempt_id),
        static_cast<long long>(codex_link.changed_at_us),
        static_cast<long long>(codex_link.deadline_at_us),
        static_cast<long long>(codex_link.ready_at_us)
    );
    (void)json.raw(",\"device_id\":");
    (void)json.quoted(codex_link.device_id);
    (void)json.raw(",\"paired_pc_id\":");
    (void)json.quoted(codex_link.paired_pc_id);
    (void)json.raw(",\"peer_ip_address\":");
    (void)json.quoted(codex_link.peer_ip_address);
    (void)json.raw(",\"activity\":");
    const auto &activity = codex_link.activity;
    writeComponentStart(json, activity);
    (void)json.raw(",\"state\":");
    (void)json.quoted(codexActivityStateName(activity.state));
    (void)json.raw(",\"chat_id\":");
    (void)json.quoted(activity.chat_id);
    (void)json.raw(",\"project\":");
    (void)json.quoted(activity.project);
    (void)json.raw(",\"branch\":");
    (void)json.quoted(activity.branch);
    (void)json.raw(",\"description\":");
    (void)json.quoted(activity.description);
    (void)json.format(",\"revision\":%llu}", static_cast<unsigned long long>(activity.revision));
    (void)json.raw("}");
}

const char *storageMountOutcomeName(LUNASHELL::StorageMountOutcome outcome)
{
    switch (outcome) {
    case LUNASHELL::StorageMountOutcome::NotAttempted:
        return "not-attempted";
    case LUNASHELL::StorageMountOutcome::Mounted:
        return "mounted";
    case LUNASHELL::StorageMountOutcome::CardOrTransportFailed:
        return "card-or-transport-failed";
    case LUNASHELL::StorageMountOutcome::FormatRequired:
        return "format-required";
    case LUNASHELL::StorageMountOutcome::FilesystemFailed:
        return "filesystem-failed";
    case LUNASHELL::StorageMountOutcome::FormatFailed:
        return "format-failed";
    case LUNASHELL::StorageMountOutcome::InternalFailed:
        return "internal-failed";
    }
    return "unknown";
}

const char *storageMountStageName(LUNASHELL::StorageMountStage stage)
{
    switch (stage) {
    case LUNASHELL::StorageMountStage::None: return "none";
    case LUNASHELL::StorageMountStage::Request: return "request";
    case LUNASHELL::StorageMountStage::HostInit: return "host-init";
    case LUNASHELL::StorageMountStage::SlotInit: return "slot-init";
    case LUNASHELL::StorageMountStage::CardInit: return "card-init";
    case LUNASHELL::StorageMountStage::CardIdentity: return "card-identity";
    case LUNASHELL::StorageMountStage::DriveAlloc: return "drive-alloc";
    case LUNASHELL::StorageMountStage::DiskIoRegister:
        return "disk-io-register";
    case LUNASHELL::StorageMountStage::VfsRegister: return "vfs-register";
    case LUNASHELL::StorageMountStage::FatMount: return "fat-mount";
    case LUNASHELL::StorageMountStage::PreFormatUnmount:
        return "pre-format-unmount";
    case LUNASHELL::StorageMountStage::Partition: return "partition";
    case LUNASHELL::StorageMountStage::Format: return "format";
    case LUNASHELL::StorageMountStage::PostFormatMount:
        return "post-format-mount";
    case LUNASHELL::StorageMountStage::RuntimeStatus:
        return "runtime-status";
    case LUNASHELL::StorageMountStage::RuntimeFilesystem:
        return "runtime-filesystem";
    case LUNASHELL::StorageMountStage::RuntimeUnmount:
        return "runtime-unmount";
    }
    return "unknown";
}

const char *storageCleanupStageName(LUNASHELL::StorageCleanupStage stage)
{
    switch (stage) {
    case LUNASHELL::StorageCleanupStage::None: return "none";
    case LUNASHELL::StorageCleanupStage::FatUnmount: return "fat-unmount";
    case LUNASHELL::StorageCleanupStage::VfsUnregister:
        return "vfs-unregister";
    case LUNASHELL::StorageCleanupStage::HostDeinit: return "host-deinit";
    }
    return "unknown";
}

void writeNullableInt32(
    LUNASHELL::HttpJsonWriter &json,
    bool valid,
    int32_t value
)
{
    if (valid) {
        (void)json.format("%ld", static_cast<long>(value));
    } else {
        (void)json.raw("null");
    }
}

void writeStorage(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::StorageSnapshot &storage
)
{
    writeComponentStart(json, storage);
    (void)json.raw(",\"logger_mirror\":");
    writeComponentFields(json, storage.logger_mirror);
    (void)json.raw(",\"mount_outcome\":");
    (void)json.quoted(storageMountOutcomeName(storage.mount_outcome));
    (void)json.raw(",\"mount_stage\":");
    (void)json.quoted(storageMountStageName(storage.mount_stage));
    (void)json.raw(",\"cleanup_stage\":");
    (void)json.quoted(storageCleanupStageName(storage.cleanup_stage));
    (void)json.raw(",\"cleanup_error\":");
    (void)json.quoted(esp_err_to_name(storage.cleanup_error));

    (void)json.raw(",\"initial_fatfs_result_valid\":");
    (void)json.boolean(storage.initial_fatfs_result_valid);
    (void)json.raw(",\"initial_fatfs_result\":");
    writeNullableInt32(
        json,
        storage.initial_fatfs_result_valid,
        storage.initial_fatfs_result
    );
    (void)json.raw(",\"fatfs_result_valid\":");
    (void)json.boolean(storage.fatfs_result_valid);
    (void)json.raw(",\"fatfs_result\":");
    writeNullableInt32(json, storage.fatfs_result_valid, storage.fatfs_result);
    (void)json.raw(",\"cleanup_fatfs_result_valid\":");
    (void)json.boolean(storage.cleanup_fatfs_result_valid);
    (void)json.raw(",\"cleanup_fatfs_result\":");
    writeNullableInt32(
        json,
        storage.cleanup_fatfs_result_valid,
        storage.cleanup_fatfs_result
    );

    (void)json.raw(",\"cleanup_complete\":");
    (void)json.boolean(storage.cleanup_complete);
    (void)json.raw(",\"poisoned\":");
    (void)json.boolean(storage.poisoned);
    (void)json.raw(",\"mounted\":");
    (void)json.boolean(storage.mounted);
    (void)json.raw(",\"card_response_verified\":");
    (void)json.boolean(storage.card_response_verified);
    (void)json.raw(",\"format_required\":");
    (void)json.boolean(storage.format_required);
    (void)json.raw(",\"authorization_consumed\":");
    (void)json.boolean(storage.authorization_consumed);
    (void)json.raw(",\"authorization_identity_matched\":");
    (void)json.boolean(storage.authorization_identity_matched);
    (void)json.raw(",\"format_attempted\":");
    (void)json.boolean(storage.format_attempted);
    (void)json.raw(",\"partition_written\":");
    (void)json.boolean(storage.partition_written);
    (void)json.raw(",\"formatted\":");
    (void)json.boolean(storage.formatted);
    (void)json.format(
        ",\"mount_attempt_count\":%lu,\"capacity_bytes\":%llu,\"filesystem_total_bytes\":%llu,\"free_bytes\":%llu",
        static_cast<unsigned long>(storage.mount_attempt_count),
        static_cast<unsigned long long>(storage.capacity_bytes),
        static_cast<unsigned long long>(storage.filesystem_total_bytes),
        static_cast<unsigned long long>(storage.free_bytes)
    );
    (void)json.raw(",\"logger_mirror_requested\":");
    (void)json.boolean(storage.logger_mirror_requested);
    (void)json.raw(",\"logger_mirror_enabled\":");
    (void)json.boolean(storage.logger_mirror_enabled);
    (void)json.format(
        ",\"logger_mirror_errno\":%d,\"logger_mirror_changed_at_us\":%lld,\"logger_mirror_failure_count\":%lu",
        storage.logger_mirror_errno,
        static_cast<long long>(storage.logger_mirror_changed_at_us),
        static_cast<unsigned long>(storage.logger_mirror_failure_count)
    );
    (void)json.raw("}");
}

// Serialize UI hardware using only values committed by the snapshot service.
const char *displayPowerModeName(LUNASHELL::UiHardwareSnapshot::DisplayPowerMode mode)
{
    switch (mode) {
    case LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active:
        return "active";
    case LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Dimmed:
        return "dimmed";
    case LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::DisplayOff:
        return "display-off";
    case LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown:
    default:
        return "unknown";
    }
}

void writeUi(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::UiHardwareSnapshot &ui)
{
    (void)json.raw("{\"display\":");
    writeComponentFields(json, ui.display);
    (void)json.raw(",\"touch\":");
    writeComponentFields(json, ui.touch);
    (void)json.raw(",\"brightness\":");
    writeComponentFields(json, ui.brightness);
    (void)json.raw(",\"display_power_mode\":");
    writeComponentFields(json, ui.display_power_mode);
    (void)json.format(
        ",\"touch_activity_sequence\":%lu,\"touch_last_activity_at_us\":%lld,\"brightness_percent\":%lu,\"configured_brightness_percent\":%lu,\"display_power_mode_state\":",
        static_cast<unsigned long>(ui.touch_activity_sequence),
        static_cast<long long>(ui.touch_last_activity_at_us),
        static_cast<unsigned long>(ui.brightness_percent),
        static_cast<unsigned long>(ui.configured_brightness_percent)
    );
    (void)json.quoted(displayPowerModeName(ui.display_power_mode_state));
    (void)json.raw(",\"orientation\":");
    writeComponentStart(json, ui.orientation);
    (void)json.raw(",\"auto_rotation_enabled\":");
    (void)json.boolean(ui.orientation.auto_rotation_enabled);
    (void)json.raw(",\"gravity_valid\":");
    (void)json.boolean(ui.orientation.gravity_valid);
    (void)json.raw(",\"rotation_pending\":");
    (void)json.boolean(ui.orientation.rotation_pending);
    (void)json.format(
        ",\"physical_degrees\":%u,\"rendering_degrees\":%u,\"change_sequence\":%lu,\"sampled_at_us\":%lld,\"changed_at_us\":%lld}",
        static_cast<unsigned>(ui.orientation.physical_orientation_degrees),
        static_cast<unsigned>(ui.orientation.rendering_rotation_degrees),
        static_cast<unsigned long>(ui.orientation.change_sequence),
        static_cast<long long>(ui.orientation.sampled_at_us),
        static_cast<long long>(ui.orientation.changed_at_us)
    );
    (void)json.raw("}");
}

void writeInput(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::InputSnapshot &input)
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
    (void)json.raw(",\"power_pressed_valid\":");
    (void)json.boolean(input.power_pressed_valid);
    (void)json.format(",\"power_pressed_at_us\":%lld", static_cast<long long>(input.power_pressed_at_us));
    (void)json.format(
        ",\"boot_press_count\":%lu,\"boot_long_press_count\":%lu,\"power_short_press_count\":%lu,\"power_long_press_count\":%lu,\"wakeup_cause\":%lu}",
        static_cast<unsigned long>(input.boot_press_count),
        static_cast<unsigned long>(input.boot_long_press_count),
        static_cast<unsigned long>(input.power_short_press_count),
        static_cast<unsigned long>(input.power_long_press_count),
        static_cast<unsigned long>(input.wakeup_cause)
    );
}

const char *operatingStateName(LUNASHELL::DeviceOperatingState state)
{
    switch (state) {
    case LUNASHELL::DeviceOperatingState::Active:
        return "active";
    case LUNASHELL::DeviceOperatingState::EnteringStandby:
        return "entering-standby";
    case LUNASHELL::DeviceOperatingState::Standby:
        return "standby";
    case LUNASHELL::DeviceOperatingState::Waking:
        return "waking";
    }
    return "unknown";
}

// reason: Typed power-policy transition cause.
const char *deviceStateReasonName(LUNASHELL::DeviceStateReason reason)
{
    switch (reason) {
    case LUNASHELL::DeviceStateReason::Startup:
        return "startup";
    case LUNASHELL::DeviceStateReason::BootButton:
        return "boot-button";
    case LUNASHELL::DeviceStateReason::PowerButton:
        return "power-button";
    case LUNASHELL::DeviceStateReason::Touch:
        return "touch";
    case LUNASHELL::DeviceStateReason::ImuMotion:
        return "imu-motion";
    case LUNASHELL::DeviceStateReason::WakeWord:
        return "wake-word";
    case LUNASHELL::DeviceStateReason::Inactivity:
        return "inactivity";
    case LUNASHELL::DeviceStateReason::CodexNotification:
        return "codex-notification";
    }
    return "unknown";
}

// json: Response writer that owns formatting and serialization errors.
// power: Canonical published power state, including the live inactivity setting.
void writePowerManagement(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::PowerManagementSnapshot &power
)
{
    writeComponentStart(json, power);
    (void)json.raw(",\"state\":");
    (void)json.quoted(operatingStateName(power.state));
    (void)json.raw(",\"reason\":");
    (void)json.quoted(deviceStateReasonName(power.reason));
    (void)json.raw(",\"audio_suspended\":");
    (void)json.boolean(power.audio_suspended);
    (void)json.raw(",\"fullscreen_active\":");
    (void)json.boolean(power.fullscreen_active);
    (void)json.raw(",\"command_arc_visible\":");
    (void)json.boolean(power.command_arc_visible);
    (void)json.raw(",\"status_arc_visible\":");
    (void)json.boolean(power.status_arc_visible);
    (void)json.raw(",\"display_idle_stage\":");
    (void)json.quoted(displayPowerModeName(power.display_idle_stage));
    (void)json.format(
        ",\"command_arc_timeout_seconds\":%lu,\"status_arc_timeout_seconds\":%lu,\"display_dim_timeout_seconds\":%lu,\"screen_sleep_delay_seconds\":%lu,\"screen_idle_timeout_seconds\":%lu,\"transition_sequence\":%lu,\"last_activity_at_us\":%lld,\"changed_at_us\":%lld}",
        static_cast<unsigned long>(power.command_arc_timeout_seconds),
        static_cast<unsigned long>(power.status_arc_timeout_seconds),
        static_cast<unsigned long>(power.display_dim_timeout_seconds),
        static_cast<unsigned long>(power.screen_sleep_delay_seconds),
        static_cast<unsigned long>(power.screen_idle_timeout_seconds),
        static_cast<unsigned long>(power.transition_sequence),
        static_cast<long long>(power.last_activity_at_us),
        static_cast<long long>(power.changed_at_us)
    );
}

void writeMemory(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::MemorySnapshot &memory)
{
    writeComponentStart(json, memory);
    (void)json.format(
        ",\"flash_capacity_bytes\":%lu,\"app_partition_bytes\":%lu,\"firmware_image_bytes\":%lu,\"heap_total_bytes\":%lu,\"heap_free_bytes\":%lu,\"heap_minimum_free_bytes\":%lu",
        static_cast<unsigned long>(memory.flash_capacity_bytes),
        static_cast<unsigned long>(memory.app_partition_bytes),
        static_cast<unsigned long>(memory.firmware_image_bytes),
        static_cast<unsigned long>(memory.heap_total_bytes),
        static_cast<unsigned long>(memory.heap_free_bytes),
        static_cast<unsigned long>(memory.heap_minimum_free_bytes)
    );
    (void)json.format(
        ",\"internal_heap_total_bytes\":%lu,\"internal_free_bytes\":%lu,\"internal_largest_block_bytes\":%lu,\"internal_minimum_free_bytes\":%lu,\"internal_allocated_blocks\":%lu,\"internal_free_blocks\":%lu,\"internal_total_blocks\":%lu,\"internal_memory_pressure\":",
        static_cast<unsigned long>(memory.internal_heap_total_bytes),
        static_cast<unsigned long>(memory.internal_free_bytes),
        static_cast<unsigned long>(memory.internal_largest_block_bytes),
        static_cast<unsigned long>(memory.internal_minimum_free_bytes),
        static_cast<unsigned long>(memory.internal_allocated_blocks),
        static_cast<unsigned long>(memory.internal_free_blocks),
        static_cast<unsigned long>(memory.internal_total_blocks)
    );
    (void)json.boolean(memory.internal_memory_pressure);
    (void)json.format(
        ",\"psram_capacity_bytes\":%lu,\"psram_heap_total_bytes\":%lu,\"psram_free_bytes\":%lu,\"psram_used_bytes\":%lu,\"psram_largest_block_bytes\":%lu,\"psram_minimum_free_bytes\":%lu",
        static_cast<unsigned long>(memory.psram_capacity_bytes),
        static_cast<unsigned long>(memory.psram_heap_total_bytes),
        static_cast<unsigned long>(memory.psram_free_bytes),
        static_cast<unsigned long>(memory.psram_used_bytes),
        static_cast<unsigned long>(memory.psram_largest_block_bytes),
        static_cast<unsigned long>(memory.psram_minimum_free_bytes)
    );
    (void)json.format(
        ",\"psram_allocated_blocks\":%lu,\"psram_free_blocks\":%lu,\"psram_total_blocks\":%lu,\"allocation_failure_monitor_active\":",
        static_cast<unsigned long>(memory.psram_allocated_blocks),
        static_cast<unsigned long>(memory.psram_free_blocks),
        static_cast<unsigned long>(memory.psram_total_blocks)
    );
    (void)json.boolean(memory.allocation_failure_monitor_active);
    (void)json.format(
        ",\"allocation_failure_count\":%lu,\"largest_failed_allocation_bytes\":%lu,\"failed_allocation_caps_seen_mask\":%lu}",
        static_cast<unsigned long>(memory.allocation_failure_count),
        static_cast<unsigned long>(memory.largest_failed_allocation_bytes),
        static_cast<unsigned long>(memory.failed_allocation_caps_seen_mask)
    );
}

void writeProcessor(LUNASHELL::HttpJsonWriter &json, const LUNASHELL::ProcessorSnapshot &processor)
{
    writeComponentStart(json, processor);
    (void)json.format(
        ",\"core_count\":%u,\"sampling_core\":%u,\"cores\":[",
        static_cast<unsigned>(processor.core_count),
        static_cast<unsigned>(processor.sampling_core)
    );
    for (std::size_t index = 0U; index < 2U; ++index) {
        if (index != 0U) (void)json.raw(",");
        (void)json.raw("{\"available\":");
        (void)json.boolean(processor.cores[index].available);
        (void)json.raw(",\"utilization_available\":");
        (void)json.boolean(processor.cores[index].utilization_available);
        (void)json.raw(",\"utilization_percent\":");
        if (processor.cores[index].utilization_available) {
            (void)json.format(
                "%u",
                static_cast<unsigned>(
                    processor.cores[index].utilization_percent
                )
            );
        } else {
            (void)json.raw("null");
        }
        (void)json.raw(",\"current_task\":");
        (void)json.quoted(processor.cores[index].current_task);
        (void)json.format(
            ",\"stack_high_water_words\":%lu,\"priority\":%lu}",
            static_cast<unsigned long>(processor.cores[index].stack_high_water_words),
            static_cast<unsigned long>(processor.cores[index].priority)
        );
    }
    (void)json.raw("]}");
}

const char *distanceCalibrationStateName(
    LUNASHELL::DistanceCalibrationState state
)
{
    switch (state) {
    case LUNASHELL::DistanceCalibrationState::Required: return "required";
    case LUNASHELL::DistanceCalibrationState::Ready: return "ready";
    case LUNASHELL::DistanceCalibrationState::Running: return "running";
    case LUNASHELL::DistanceCalibrationState::Succeeded: return "succeeded";
    case LUNASHELL::DistanceCalibrationState::Failed: return "failed";
    }
    return "unknown";
}

void writeTofSensor(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::TofSensorSnapshot &sensor
)
{
    writeComponentStart(json, sensor);
    (void)json.format(
        ",\"raw_distance_mm\":%u,\"filtered_distance_mm\":%u,\"window_sample_count\":%u,\"inlier_count\":%u,\"median_absolute_deviation_mm\":%u,\"calibration_offset_mm\":%ld",
        static_cast<unsigned>(sensor.raw_distance_mm),
        static_cast<unsigned>(sensor.filtered_distance_mm),
        static_cast<unsigned>(sensor.window_sample_count),
        static_cast<unsigned>(sensor.inlier_count),
        static_cast<unsigned>(sensor.median_absolute_deviation_mm),
        static_cast<long>(sensor.calibration_offset_mm)
    );
    (void)json.format(
        ",\"frame_sequence\":%lu,\"crc_error_count\":%lu,\"protocol_error_count\":%lu,\"timeout_count\":%lu,\"uart_error_count\":%lu}",
        static_cast<unsigned long>(sensor.frame_sequence),
        static_cast<unsigned long>(sensor.crc_error_count),
        static_cast<unsigned long>(sensor.protocol_error_count),
        static_cast<unsigned long>(sensor.timeout_count),
        static_cast<unsigned long>(sensor.uart_error_count)
    );
}

void writeDistanceSettings(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::DistanceSettingsSnapshot &settings
)
{
    writeComponentStart(json, settings);
    (void)json.format(
        ",\"sample_count\":%u,\"reference_distance_mm\":%u,\"sample_interval_ms\":%lu}",
        static_cast<unsigned>(settings.sample_count),
        static_cast<unsigned>(settings.reference_distance_mm),
        static_cast<unsigned long>(settings.sample_interval_ms)
    );
}

void writeDistanceCalibration(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::DistanceCalibrationSnapshot &calibration
)
{
    writeComponentStart(json, calibration);
    (void)json.raw(",\"state\":");
    (void)json.quoted(distanceCalibrationStateName(calibration.state));
    (void)json.raw(",\"record_valid\":");
    (void)json.boolean(calibration.record_valid);
    (void)json.format(
        ",\"requested_samples\":%u,\"accepted_samples\":[%u,%u],\"rejected_samples\":[%u,%u],\"reference_distance_mm\":%u,\"progress_percent\":%u,\"offsets_mm\":[%ld,%ld],\"z_axis_sign\":%d",
        static_cast<unsigned>(calibration.requested_samples),
        static_cast<unsigned>(calibration.accepted_samples[0]),
        static_cast<unsigned>(calibration.accepted_samples[1]),
        static_cast<unsigned>(calibration.rejected_samples[0]),
        static_cast<unsigned>(calibration.rejected_samples[1]),
        static_cast<unsigned>(calibration.reference_distance_mm),
        static_cast<unsigned>(calibration.progress_percent),
        static_cast<long>(calibration.offsets_mm[0]),
        static_cast<long>(calibration.offsets_mm[1]),
        static_cast<int>(calibration.z_axis_sign)
    );
    (void)json.format(
        ",\"started_at_us\":%lld,\"completed_at_us\":%lld}",
        static_cast<long long>(calibration.started_at_us),
        static_cast<long long>(calibration.completed_at_us)
    );
}

void writeDistance(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::DistanceSnapshot &distance
)
{
    writeComponentStart(json, distance);
    (void)json.raw(",\"sensors\":[");
    for (std::size_t index = 0U; index < LUNASHELL::kTofSensorCount; ++index) {
        if (index != 0U) (void)json.raw(",");
        writeTofSensor(json, distance.sensors[index]);
    }
    (void)json.raw("],\"settings\":");
    writeDistanceSettings(json, distance.settings);
    (void)json.raw(",\"calibration\":");
    writeDistanceCalibration(json, distance.calibration);
    (void)json.format(
        ",\"slant_distance_mm\":%u,\"vertical_distance_mm\":%u,\"sensor_spread_mm\":%u,\"samples_used\":%u,\"tilt_degrees\":%.3f,\"confidence_percent\":%u",
        static_cast<unsigned>(distance.slant_distance_mm),
        static_cast<unsigned>(distance.vertical_distance_mm),
        static_cast<unsigned>(distance.sensor_spread_mm),
        static_cast<unsigned>(distance.samples_used),
        static_cast<double>(distance.tilt_degrees),
        static_cast<unsigned>(distance.confidence_percent)
    );
    (void)json.raw(",\"tilt_compensated\":");
    (void)json.boolean(distance.tilt_compensated);
    (void)json.raw(",\"acquisition_timing_valid\":");
    (void)json.boolean(distance.acquisition_timing_valid);
    (void)json.format(
        ",\"tof_pair_skew_us\":%lu,\"geometry_skew_us\":%lu,\"rejected_pair_count\":%lu,\"timing_rejection_count\":%lu",
        static_cast<unsigned long>(distance.tof_pair_skew_us),
        static_cast<unsigned long>(distance.geometry_skew_us),
        static_cast<unsigned long>(distance.rejected_pair_count),
        static_cast<unsigned long>(distance.timing_rejection_count)
    );
    (void)json.format(
        ",\"consecutive_transport_failure_count\":%lu,\"recovery_attempt_count\":%lu,\"current_backoff_ms\":%lu,\"worker_stack_high_water_valid\":",
        static_cast<unsigned long>(
            distance.consecutive_transport_failure_count
        ),
        static_cast<unsigned long>(distance.recovery_attempt_count),
        static_cast<unsigned long>(distance.current_backoff_ms)
    );
    (void)json.boolean(distance.worker_stack_high_water_valid);
    (void)json.raw(",\"worker_stack_min_free_bytes\":");
    if (distance.worker_stack_high_water_valid) {
        (void)json.format(
            "%lu",
            static_cast<unsigned long>(distance.worker_stack_min_free_bytes)
        );
    } else {
        (void)json.raw("null");
    }
    (void)json.raw("}");
}

void writeDiagnosticsSettings(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::DiagnosticsSettingsSnapshot &diagnostics
)
{
    (void)json.raw("{\"sample_period\":");
    writeComponentFields(json, diagnostics.sample_period);
    (void)json.raw(",\"imu_smoothing\":");
    writeComponentFields(json, diagnostics.imu_smoothing);
    (void)json.raw(",\"monitor\":");
    writeComponentFields(json, diagnostics.monitor);
    (void)json.raw(",\"full_snapshot_period\":");
    writeComponentFields(json, diagnostics.full_snapshot_period);
    (void)json.raw(",\"full_snapshot_monitor\":");
    writeComponentFields(json, diagnostics.full_snapshot_monitor);
    (void)json.format(
        ",\"sample_period_ms\":%lu,\"imu_smoothing_percent\":%lu,\"full_snapshot_period_ms\":%lu,\"monitor_active\":",
        static_cast<unsigned long>(diagnostics.sample_period_ms),
        static_cast<unsigned long>(diagnostics.imu_smoothing_percent),
        static_cast<unsigned long>(diagnostics.full_snapshot_period_ms)
    );
    (void)json.boolean(diagnostics.monitor_active);
    (void)json.raw(",\"imu_monitor_active\":");
    (void)json.boolean(diagnostics.imu_monitor_active);
    (void)json.raw(",\"microphone_monitor_active\":");
    (void)json.boolean(diagnostics.microphone_monitor_active);
    (void)json.raw(",\"microphone_spectrum_monitor_active\":");
    (void)json.boolean(diagnostics.microphone_spectrum_monitor_active);
    (void)json.raw(",\"full_snapshot_monitor_active\":");
    (void)json.boolean(diagnostics.full_snapshot_monitor_active);
    (void)json.raw("}");
}

// Collapse required component health into the public device health value.
LUNASHELL::DeviceHealth overallHealth(const LUNASHELL::DeviceSnapshot &snapshot)
{
    const LUNASHELL::ComponentSnapshot *components[] = {
        &snapshot.battery, &snapshot.imu, &snapshot.sensor_fusion,
        &snapshot.rtc, &snapshot.gps,
        &snapshot.audio.speaker, &snapshot.audio.microphone,
        &snapshot.radios.wifi, &snapshot.radios.ble,
        &snapshot.storage, &snapshot.ui.display, &snapshot.ui.touch,
        &snapshot.input.io_expander, &snapshot.input.boot_button,
        &snapshot.input.power_button, &snapshot.power,
    };
    bool unknown = false;
    bool degraded = false;
    bool missing = false;
    for (const LUNASHELL::ComponentSnapshot *component : components) {
        missing = missing || component->health == LUNASHELL::DeviceHealth::Missing;
        degraded = degraded || component->health == LUNASHELL::DeviceHealth::Degraded;
        unknown = unknown || component->health == LUNASHELL::DeviceHealth::Unknown;
    }
    if (missing) return LUNASHELL::DeviceHealth::Missing;
    if (degraded) return LUNASHELL::DeviceHealth::Degraded;
    if (unknown) return LUNASHELL::DeviceHealth::Unknown;
    return LUNASHELL::DeviceHealth::Ready;
}

// json: Bounded streaming writer that retains its transport error.
// snapshot: Canonical device state with original validity and timestamps.
// complete_diagnostics: Whether to include protected network identity/details.
void writeSnapshotData(
    LUNASHELL::HttpJsonWriter &json,
    const LUNASHELL::DeviceSnapshot &snapshot,
    bool complete_diagnostics
)
{
    (void)json.raw("{\"reason\":");
    (void)json.quoted(snapshot.reason);
    (void)json.raw(",\"battery\":");
    writeBattery(json, snapshot.battery);
    (void)json.raw(",\"imu\":");
    writeImu(json, snapshot.imu);
    (void)json.raw(",\"sensor_fusion\":");
    writeSensorFusion(json, snapshot.sensor_fusion);
    if (complete_diagnostics) {
        (void)json.raw(",\"distance\":");
        writeDistance(json, snapshot.distance);
    }
    (void)json.raw(",\"rtc\":");
    writeRtcData(json, snapshot);
    (void)json.raw(",\"gps\":");
    writeGpsData(json, snapshot.gps, complete_diagnostics);
    (void)json.raw(",\"audio\":");
    writeAudio(json, snapshot.audio);
    (void)json.raw(",\"voice\":");
    writeVoice(json, snapshot.voice);
    (void)json.raw(",\"radios\":{\"wifi\":");
    writeWifi(json, snapshot.radios.wifi, complete_diagnostics);
    (void)json.raw(",\"ble\":");
    writeBle(json, snapshot.radios.ble);
    (void)json.raw(",\"provisioning\":");
    writeProvisioning(json, snapshot.radios.provisioning);
    (void)json.raw(",\"pairing\":");
    writePairingSession(json, snapshot.radios.pairing);
    (void)json.raw(",\"codex_link\":");
    writeCodexLink(json, snapshot.radios.codex_link);
    (void)json.raw("},\"storage\":");
    writeStorage(json, snapshot.storage);
    if (complete_diagnostics) {
        (void)json.raw(",\"diagnostics\":");
        writeDiagnosticsSettings(json, snapshot.diagnostics);
    }
    (void)json.raw(",\"ui\":");
    writeUi(json, snapshot.ui);
    (void)json.raw(",\"input\":");
    writeInput(json, snapshot.input);
    (void)json.raw(",\"power\":");
    writePowerManagement(json, snapshot.power);
    (void)json.raw(",\"memory\":");
    writeMemory(json, snapshot.memory);
    (void)json.raw(",\"processor\":");
    writeProcessor(json, snapshot.processor);
    (void)json.raw("}");
}

}  // namespace

namespace LUNASHELL {

// sink: Explicit output owner; errors must be returned, never hidden.
// now_us: Observation time supplied by its caller without serializer-side clock reads.
HttpJsonWriter::HttpJsonWriter(HttpJsonSink sink, int64_t now_us): _sink(sink), _now_us(now_us)
{
    if (_sink.write == nullptr || now_us < 0) {
        _failed = true;
        _last_error = ESP_ERR_INVALID_ARG;
    }
}

// Stream arbitrary text through the fixed response buffer.
bool HttpJsonWriter::raw(const char *text)
{
    if (_failed) return false;
    if (text == nullptr) {
        _failed = true;
        _last_error = ESP_ERR_INVALID_ARG;
        return false;
    }
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
        _last_error = length < 0 ? ESP_FAIL : ESP_ERR_INVALID_SIZE;
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

// No parameters: flushes the bounded buffer and asks the explicit sink to finish.
// Exception: Returns the original formatting or sink error.
esp_err_t HttpJsonWriter::finish()
{
    if (_failed || !flush()) {
        return _last_error != ESP_OK ? _last_error : ESP_FAIL;
    }
    const esp_err_t error = _sink.write(_sink.context, nullptr, 0, true);
    if (error != ESP_OK) {
        _failed = true;
        _last_error = error;
    }
    return error;
}

bool HttpJsonWriter::append(char value)
{
    if (_used == sizeof(_buffer) && !flush()) return false;
    _buffer[_used++] = value;
    return true;
}

// No parameters: transfers one chunk to the explicit sink.
// Exception: Records the original sink error for finish() to propagate.
bool HttpJsonWriter::flush()
{
    if (_used == 0U) return true;
    const esp_err_t error = _sink.write(_sink.context, _buffer, _used, false);
    _used = 0U;
    if (error != ESP_OK) {
        _failed = true;
        _last_error = error;
    }
    return error == ESP_OK;
}

// json: Bounded sink and explicit observation clock.
// snapshot: Canonical or explicitly synthetic snapshot, never acquired here.
// lifecycle: Observed HTTP ownership flags supplied by the transport.
// observation: Real liveness query result and names supplied by the runtime owner.
void httpJsonWriteHealth(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    const HttpLifecycleView &lifecycle,
    const HttpLivenessView &observation
)
{
    const auto &liveness = observation.status;
    const esp_err_t liveness_result = observation.query_error;
    const bool liveness_ok =
        liveness_result == ESP_OK && liveness.initialized &&
        liveness.last_error == ESP_OK;
    const bool healthy =
        lifecycle.running && lifecycle.accepting_requests &&
        !lifecycle.cleanup_required && liveness_ok;

    (void)json.raw("{\"schema\":\"LUNASHELL.device.health.v1\",\"api_version\":1,\"data\":{\"ok\":");
    (void)json.boolean(healthy);
    (void)json.raw(",\"server_running\":");
    (void)json.boolean(lifecycle.running);
    (void)json.raw(",\"server_cleanup_required\":");
    (void)json.boolean(lifecycle.cleanup_required);
    (void)json.raw(",\"server_handle_retained\":");
    (void)json.boolean(lifecycle.handle_retained);
    (void)json.raw(",\"server_accepting_requests\":");
    (void)json.boolean(lifecycle.accepting_requests);
    (void)json.raw(",\"auth_required_for_data\":true,\"wifi_connected\":");
    (void)json.boolean(snapshot.radios.wifi.connected && snapshot.radios.wifi.ip_acquired);
    (void)json.format(",\"snapshot\":{\"sequence\":%lu,\"last_update_attempt_at_us\":%lld,\"last_update_complete\":",
                       static_cast<unsigned long>(snapshot.sequence),
                       static_cast<long long>(snapshot.last_update_attempt_at_us));
    (void)json.boolean(snapshot.last_update_complete);
    (void)json.format(",\"age_ms\":%llu},\"liveness\":{\"available\":",
                      static_cast<unsigned long long>(snapshotAgeMs(json, snapshot)));
    (void)json.boolean(liveness_result == ESP_OK);
    (void)json.raw(",\"query_error\":");
    (void)json.quoted(esp_err_to_name(liveness_result));
    (void)json.raw(",\"initialized\":");
    (void)json.boolean(liveness.initialized);
    (void)json.format(
        ",\"boot_reset_reason\":%lu,\"watchdog_timeout_ms\":%lu,"
        "\"operation_sequence\":%lu,\"active_mask\":%lu,\"last_error\":",
        static_cast<unsigned long>(liveness.boot_reset_reason),
        static_cast<unsigned long>(liveness.watchdog_timeout_ms),
        static_cast<unsigned long>(liveness.operation_sequence),
        static_cast<unsigned long>(liveness.active_mask)
    );
    (void)json.quoted(esp_err_to_name(liveness.last_error));
    (void)json.raw(",\"last_progress\":{\"valid\":");
    (void)json.boolean(liveness.last_progress_valid);
    (void)json.raw(",\"point\":");
    (void)json.quoted(
        liveness.last_progress_valid ? observation.last_progress_name : "none"
    );
    (void)json.format(
        ",\"age_ms\":%lu},\"previous_timeout\":{\"valid\":",
        static_cast<unsigned long>(liveness.last_progress_age_ms)
    );
    (void)json.boolean(liveness.previous_timeout.valid);
    (void)json.format(
        ",\"operation_sequence\":%lu,\"active_mask\":%lu,\"last_progress_point\":",
        static_cast<unsigned long>(
            liveness.previous_timeout.operation_sequence
        ),
        static_cast<unsigned long>(liveness.previous_timeout.active_mask)
    );
    (void)json.quoted(
        liveness.previous_timeout.valid ? observation.previous_timeout_name : "none"
    );
    (void)json.raw("}}}}");
}

void httpJsonWriteInfo(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot
)
{
    writeEnvelopeStart(json, "LUNASHELL.device.info.v1", snapshot);
    writeSnapshotData(json, snapshot, false);
    (void)json.raw("}");
}

void httpJsonWriteDiagnostics(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot
)
{
    writeEnvelopeStart(json, "LUNASHELL.device.diagnostics.v1", snapshot);
    writeSnapshotData(json, snapshot, true);
    (void)json.raw("}");
}

// json: Bounded writer receiving the RTC diagnostics envelope.
// snapshot: Canonical acquired state, including routine clock and measured drift.
void httpJsonWriteRtc(HttpJsonWriter &json, const DeviceSnapshot &snapshot)
{
    writeEnvelopeStart(json, "LUNASHELL.device.rtc.v1", snapshot);
    writeRtcData(json, snapshot);
    (void)json.raw("}");
}

void httpJsonWriteGps(HttpJsonWriter &json, const DeviceSnapshot &snapshot)
{
    writeEnvelopeStart(json, "LUNASHELL.device.gps.v1", snapshot);
    writeGpsData(json, snapshot.gps, false);
    (void)json.raw("}");
}

// json: Bounded status output writer.
// snapshot: Canonical component state with original sample/event timestamps.
// metrics: Measured HTTP service counters.
// lifecycle: Confirmed HTTP server ownership and cleanup state.
void httpJsonWriteStatus(
    HttpJsonWriter &json,
    const DeviceSnapshot &snapshot,
    const HttpMetricsView &metrics,
    const HttpLifecycleView &lifecycle
)
{
    writeEnvelopeStart(json, "LUNASHELL.device.status.v1", snapshot);
    (void)json.raw("{\"overall\":");
    (void)json.quoted(deviceHealthName(overallHealth(snapshot)));
    (void)json.raw(",\"server_running\":");
    (void)json.boolean(lifecycle.running);
    (void)json.raw(",\"server_cleanup_required\":");
    (void)json.boolean(lifecycle.cleanup_required);
    (void)json.raw(",\"server_handle_retained\":");
    (void)json.boolean(lifecycle.handle_retained);
    (void)json.raw(",\"server_accepting_requests\":");
    (void)json.boolean(lifecycle.accepting_requests);
    (void)json.raw(",\"components\":{");
    struct NamedComponent {
        const char *name;
        const ComponentSnapshot *component;
    };
    const NamedComponent components[] = {
        {"battery", &snapshot.battery}, {"rtc", &snapshot.rtc},
        {"imu", &snapshot.imu}, {"sensor_fusion", &snapshot.sensor_fusion},
        {"gps", &snapshot.gps},
        {"speaker", &snapshot.audio.speaker}, {"microphone", &snapshot.audio.microphone},
        {"spectrum", &snapshot.audio.spectrum}, {"voice", &snapshot.voice},
        {"wifi", &snapshot.radios.wifi}, {"ble", &snapshot.radios.ble},
        {"storage", &snapshot.storage}, {"display", &snapshot.ui.display},
        {"touch", &snapshot.ui.touch}, {"io_expander", &snapshot.input.io_expander},
        {"boot_button", &snapshot.input.boot_button}, {"power_button", &snapshot.input.power_button},
        {"power_management", &snapshot.power},
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
        "\"request_count\":%lu,\"authorized_count\":%lu,\"unauthorized_count\":%lu,\"response_failure_count\":%lu,\"start_count\":%lu,\"stop_count\":%lu,\"start_failure_count\":%lu,\"stop_failure_count\":%lu,\"event_lifecycle_failure_count\":%lu,\"last_lifecycle_error\":%ld,\"auth_failures_in_window\":%lu}",
        static_cast<unsigned long>(metrics.request_count),
        static_cast<unsigned long>(metrics.authorized_count),
        static_cast<unsigned long>(metrics.unauthorized_count),
        static_cast<unsigned long>(metrics.response_failure_count),
        static_cast<unsigned long>(metrics.start_count),
        static_cast<unsigned long>(metrics.stop_count),
        static_cast<unsigned long>(metrics.start_failure_count),
        static_cast<unsigned long>(metrics.stop_failure_count),
        static_cast<unsigned long>(metrics.event_lifecycle_failure_count),
        static_cast<long>(metrics.last_lifecycle_error),
        static_cast<unsigned long>(metrics.auth_failures_in_window)
    );
    (void)json.raw("}}");
}

void httpJsonWriteCapabilities(HttpJsonWriter &json, uint32_t refresh_interval_ms)
{
    (void)json.raw(
        "{\"schema\":\"LUNASHELL.device.capabilities.v1\",\"api_version\":1,\"data\":{\"read_only\":true,\"snapshot_refresh_interval_ms\":"
    );
    (void)json.format("%lu,\"features\":[\"battery\",\"rtc\",\"imu\",\"gps\",\"audio\",\"wifi\",\"ble\",\"storage\",\"display\",\"touch\"],\"endpoints\":[\"/api/v1/health\",\"/api/v1/capabilities\",\"/api/v1/info\",\"/api/v1/rtc\",\"/api/v1/gps\",\"/api/v1/status\",\"/api/v1/diagnostics\"]}}", static_cast<unsigned long>(refresh_interval_ms));
}

}  // namespace LUNASHELL
