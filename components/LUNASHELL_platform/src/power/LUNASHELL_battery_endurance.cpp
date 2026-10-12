#include "LUNASHELL_battery_endurance.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_settings.hpp"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include <cstring>
#include <cstdio>
#include <limits>

namespace LUNASHELL {
namespace {
constexpr char kTag[] = "Endurance";
constexpr char kKey[] = "bat_endurance";
constexpr size_t kWireSize = 112;
constexpr int64_t kHeartbeatUs = 60000000;
// The power task exclusively owns this state, including wake/off callbacks.
BatteryEnduranceSnapshot s_saved;
BatteryEnduranceSnapshot s_pending;
uint8_t s_wire[kWireSize];
bool s_attempted = false;
bool s_disabled = false;
bool s_live = false;
bool s_source_gap = false;
int64_t s_started_us = 0;
int64_t s_next_poll_us = 0;
int64_t s_next_save_us = 0;
int64_t s_next_log_us = 0;
int64_t s_next_error_log_us = 0;
esp_err_t s_logged_error = ESP_OK;

// bytes: Little-endian field destination.
// value: Unsigned value to encode.
// count: Field width, at most eight bytes.
void put(uint8_t *bytes, uint64_t value, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}

// bytes: Little-endian field source.
// count: Field width, at most eight bytes.
uint64_t get(const uint8_t *bytes, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    }
    return value;
}

// No parameters: CRC covers the header and both records, excluding its own field.
uint32_t checksum()
{
    return esp_rom_crc32_le(esp_rom_crc32_le(0, s_wire, 12), s_wire + 16, 96);
}

// bytes: Exact 48-byte record destination.
// record: Typed record; native structure padding is never persisted.
void encode_record(uint8_t *bytes, const BatteryEnduranceRecord &record)
{
    put(bytes, record.session_id, 4);
    bytes[4] = static_cast<uint8_t>(record.phase);
    bytes[5] = record.flags;
    put(bytes + 8, record.elapsed_ms, 8);
    put(bytes + 16, record.started_utc_s, 8);
    put(bytes + 24, record.last_alive_utc_s, 8);
    put(bytes + 32, record.first_wake_elapsed_ms, 8);
    put(bytes + 40, record.first_wake_utc_s, 8);
}

// bytes: Exact 48-byte record source.
// record: Destination changed only within a candidate that has not been published.
// Exception: Returns false for unsupported phases, flags, reserved bits or invalid fields.
bool decode_record(const uint8_t *bytes, BatteryEnduranceRecord &record)
{
    record.session_id = get(bytes, 4);
    record.phase = static_cast<EndurancePhase>(bytes[4]);
    record.flags = bytes[5];
    record.elapsed_ms = get(bytes + 8, 8);
    record.started_utc_s = static_cast<int64_t>(get(bytes + 16, 8));
    record.last_alive_utc_s = static_cast<int64_t>(get(bytes + 24, 8));
    record.first_wake_elapsed_ms = get(bytes + 32, 8);
    record.first_wake_utc_s = static_cast<int64_t>(get(bytes + 40, 8));
    if (bytes[6] || bytes[7] || bytes[4] > static_cast<uint8_t>(EndurancePhase::ShutdownRequested) ||
        (record.flags & ~31U) != 0) {
        return false;
    }
    if (record.phase == EndurancePhase::Empty) {
        for (size_t i = 0; i < 48; ++i) {
            if (bytes[i] != 0) {
                return false;
            }
        }
        return true;
    }
    return record.session_id != 0 &&
        record.elapsed_ms <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max() / 1000) &&
        ((record.flags & StartUtcValid) ? record.started_utc_s > 0 : record.started_utc_s == 0) &&
        ((record.flags & AliveUtcValid) ? record.last_alive_utc_s > 0 : record.last_alive_utc_s == 0) &&
        ((record.flags & FirstWakeUtcValid) ?
            record.first_wake_utc_s > 0 && (record.flags & FirstWakeRecorded) : record.first_wake_utc_s == 0) &&
        ((record.flags & FirstWakeRecorded) ?
            record.first_wake_elapsed_ms <= record.elapsed_ms : record.first_wake_elapsed_ms == 0);
}

// error: Actual operation error; persistence failures disable further writes this boot.
// disable: Preserve the stored record until an explicit repair when true.
// Exception: Returns the original error, or a publication error if the operation succeeded.
esp_err_t publish(esp_err_t error, bool disable = false)
{
    s_disabled |= disable;
    s_saved.error = error;
    s_saved.initialized = !s_disabled;
    s_saved.observed_at_us = esp_timer_get_time();
    const esp_err_t report_error = DeviceSnapshotService::instance().reportBatteryEndurance(s_saved);
    const esp_err_t result = error != ESP_OK ? error : report_error;
    if (result != ESP_OK && (result != s_logged_error || s_saved.observed_at_us >= s_next_error_log_us)) {
        ESP_LOGE(kTag, "Recorder operation=%s publication=%s disabled=%u",
                 esp_err_to_name(error), esp_err_to_name(report_error), s_disabled);
        s_logged_error = result;
        s_next_error_log_us = s_saved.observed_at_us + kHeartbeatUs;
    }
    return result;
}

// No parameters: atomically persist the candidate, then expose only committed records.
// Exception: Returns original settings failure and preserves last durable records.
esp_err_t save()
{
    std::memset(s_wire, 0, sizeof(s_wire));
    put(s_wire, 0x454e4431U, 4);
    put(s_wire + 4, 1, 2);
    put(s_wire + 6, kWireSize, 2);
    encode_record(s_wire + 16, s_pending.current);
    encode_record(s_wire + 64, s_pending.previous);
    put(s_wire + 12, checksum(), 4);
    const esp_err_t error = UserSettingsService::instance().setBlob(kKey, s_wire, sizeof(s_wire));
    if (error != ESP_OK) {
        return publish(error, true);
    }
    s_saved = s_pending;
    s_saved.persisted_at_us = esp_timer_get_time();
    return publish(ESP_OK);
}

// phase: Persisted typed session outcome; shutdown request is never physical evidence.
const char *phase_name(EndurancePhase phase)
{
    switch (phase) {
    case EndurancePhase::Empty: return "EMPTY";
    case EndurancePhase::Running: return "RUNNING";
    case EndurancePhase::ExternalPowerObserved: return "EXTERNAL_POWER";
    case EndurancePhase::InterruptedUnknown: return "INTERRUPTED_UNKNOWN";
    case EndurancePhase::ShutdownRequested: return "SHUTDOWN_REQUESTED";
    }
    return "INVALID";
}

// destination: Bounded printable UTC field, at least 21 bytes.
// value: Persisted epoch seconds, shown only when its validity flag is present.
// valid: Original measurement validity; missing UTC is never displayed numerically.
void format_utc(char (&destination)[21], int64_t value, bool valid)
{
    if (valid) {
        std::snprintf(destination, sizeof(destination), "%lld", static_cast<long long>(value));
    } else {
        std::snprintf(destination, sizeof(destination), "UNAVAILABLE");
    }
}

// record: Durably stored current or previous session, never a death-time estimate.
// label: Stable role shown when USB reconnects after the startup log.
void log_record(const BatteryEnduranceRecord &record, const char *label)
{
    char started[21], alive[21], wake[21];
    format_utc(started, record.started_utc_s, record.flags & StartUtcValid);
    format_utc(alive, record.last_alive_utc_s, record.flags & AliveUtcValid);
    format_utc(wake, record.first_wake_utc_s, record.flags & FirstWakeUtcValid);
    ESP_LOGI(kTag, "%s id=%lu phase=%s liveness_lower_bound_ms=%llu source_gaps=%u wake_recorded=%u wake_ms=%llu",
             label, static_cast<unsigned long>(record.session_id), phase_name(record.phase),
             static_cast<unsigned long long>(record.elapsed_ms), (record.flags & SourceUnverified) != 0,
             (record.flags & FirstWakeRecorded) != 0,
             static_cast<unsigned long long>(record.first_wake_elapsed_ms));
    ESP_LOGI(kTag, "UTC id=%lu start=%s last_alive=%s first_wake=%s",
             static_cast<unsigned long>(record.session_id), started, alive, wake);
}

// No parameters: load once; only genuine NVS absence creates and persists defaults.
// Exception: Invalid schema/CRC/storage disables recording without overwriting the blob.
esp_err_t initialize()
{
    if (s_attempted) {
        return s_disabled ? s_saved.error : ESP_OK;
    }
    s_attempted = true;
    esp_err_t error = UserSettingsService::instance().getBlobExact(kKey, s_wire, sizeof(s_wire));
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "Absent key=%s: persist empty first-boot endurance record", kKey);
        s_pending = {};
        return save();
    }
    if (error != ESP_OK) {
        return publish(error, true);
    }
    if (get(s_wire, 4) != 0x454e4431U || get(s_wire + 4, 2) != 1 ||
        get(s_wire + 6, 2) != kWireSize || get(s_wire + 8, 4) != 0) {
        ESP_LOGE(kTag, "Load rejected: magic=%08lx/454e4431 schema=%lu/1 size=%lu/112 reserved=%08lx/0",
                 static_cast<unsigned long>(get(s_wire, 4)), static_cast<unsigned long>(get(s_wire + 4, 2)),
                 static_cast<unsigned long>(get(s_wire + 6, 2)), static_cast<unsigned long>(get(s_wire + 8, 4)));
        return publish(ESP_ERR_INVALID_RESPONSE, true);
    }
    const uint32_t expected_crc = checksum();
    if (get(s_wire + 12, 4) != expected_crc) {
        ESP_LOGE(kTag, "Load rejected: CRC stored=%08lx expected=%08lx",
                 static_cast<unsigned long>(get(s_wire + 12, 4)), static_cast<unsigned long>(expected_crc));
        return publish(ESP_ERR_INVALID_RESPONSE, true);
    }
    const bool current_valid = decode_record(s_wire + 16, s_pending.current);
    const bool previous_valid = decode_record(s_wire + 64, s_pending.previous);
    if (!current_valid || !previous_valid ||
        (s_pending.previous.session_id != 0 && s_pending.previous.session_id >= s_pending.current.session_id)) {
        ESP_LOGE(kTag, "Load rejected: record contract/order current(id=%lu phase=%u flags=%u valid=%u) previous(id=%lu phase=%u flags=%u valid=%u)",
                 static_cast<unsigned long>(s_pending.current.session_id), s_wire[20], s_wire[21], current_valid,
                 static_cast<unsigned long>(s_pending.previous.session_id), s_wire[68], s_wire[69], previous_valid);
        return publish(ESP_ERR_INVALID_RESPONSE, true);
    }
    s_saved = s_pending;
    // A reboot cannot establish when or why power disappeared. Never bridge clocks.
    if (s_pending.current.phase == EndurancePhase::Running) {
        s_pending.current.phase = EndurancePhase::InterruptedUnknown;
        return save();
    }
    return publish(ESP_OK);
}

// now: Current same-boot monotonic timestamp, including elapsed software standby.
// Exception: Returns canonical clock-read errors; unavailable UTC remains explicitly absent.
esp_err_t update_alive(int64_t now)
{
    auto &record = s_pending.current;
    record.elapsed_ms = static_cast<uint64_t>((now - s_started_us) / 1000);
    ClockSnapshot clock;
    const esp_err_t error = DeviceSnapshotService::instance().readClockObservation(clock);
    record.flags &= ~AliveUtcValid;
    record.last_alive_utc_s = 0;
    if (error == ESP_OK && clock.health == DeviceHealth::Ready && clock.data_valid &&
        !clock.data_stale && clock.last_error == ESP_OK && clock.last_success_at_us > 0 &&
        clock.last_success_at_us <= now && clock.utc_epoch_seconds > 0) {
        record.flags |= AliveUtcValid;
        record.last_alive_utc_s = clock.utc_epoch_seconds;
    }
    return error;
}
} // namespace

// standby: Existing policy state, retained for callers without creating wake activity.
// Exception: Returns real optional recorder errors; never changes the power policy.
esp_err_t battery_endurance_tick(bool standby)
{
    (void)standby;
    const int64_t now = esp_timer_get_time();
    if (now < s_next_poll_us) {
        return s_disabled ? s_saved.error : ESP_OK;
    }
    s_next_poll_us = now + 1000000;
    esp_err_t error = initialize();
    if (now >= s_next_log_us) {
        log_record(s_saved.current, "current");
        log_record(s_saved.previous, "previous");
        s_next_log_us = now + kHeartbeatUs;
    }
    if (error != ESP_OK || s_disabled) {
        return error;
    }
    const bool checkpoint = now >= s_next_save_us;
    if (checkpoint) {
        error = DeviceSnapshotService::instance().refreshBatteryForEndurance();
        s_next_save_us = now + kHeartbeatUs;
    }
    BatterySnapshot battery;
    const esp_err_t read_error = DeviceSnapshotService::instance().readBatteryObservation(battery);
    if (error == ESP_OK) {
        error = read_error;
    }
    const bool valid = error == ESP_OK && battery.health == DeviceHealth::Ready &&
        battery.data_valid && !battery.data_stale && battery.last_error == ESP_OK &&
        battery.last_success_at_us > 0 && battery.last_success_at_us <= esp_timer_get_time();
    const bool battery_only = valid && battery.battery_present && !battery.usb_present && !battery.vbus_good;
    const bool external = valid && (battery.usb_present || battery.vbus_good);
    if (s_live && !battery_only && !external) {
        // A later good observation cannot erase an intervening unknown source.
        s_source_gap = true;
    }
    if (error == ESP_OK && read_error == ESP_OK && battery.last_error != ESP_OK) {
        error = battery.last_error;
    }
    s_saved.source_current = battery_only;
    s_pending = s_saved;
    bool changed = false;
    if (!s_live && battery_only) {
        if (s_saved.current.session_id == std::numeric_limits<uint32_t>::max()) {
            ESP_LOGE(kTag, "Start rejected: session id=%lu exhausted; no wrap or reset permitted",
                     static_cast<unsigned long>(s_saved.current.session_id));
            return publish(ESP_ERR_INVALID_SIZE, true);
        }
        s_pending.previous = s_saved.current;
        s_pending.current = {};
        s_pending.current.session_id = s_saved.current.session_id + 1;
        s_pending.current.phase = EndurancePhase::Running;
        s_started_us = esp_timer_get_time();
        s_live = true;
        s_source_gap = false;
        changed = true;
    }
    if (s_live && s_pending.current.phase == EndurancePhase::Running && (checkpoint || changed || external)) {
        const esp_err_t clock_error = update_alive(esp_timer_get_time());
        if (error == ESP_OK) {
            error = clock_error;
        }
        if (changed && (s_pending.current.flags & AliveUtcValid)) {
            s_pending.current.flags |= StartUtcValid;
            s_pending.current.started_utc_s = s_pending.current.last_alive_utc_s;
        }
        if (s_source_gap) {
            s_pending.current.flags |= SourceUnverified;
        }
        if (external) {
            s_pending.current.phase = EndurancePhase::ExternalPowerObserved;
            s_live = false;
        }
        const esp_err_t save_error = save();
        if (save_error != ESP_OK) {
            return save_error;
        }
        log_record(s_saved.current, "checkpoint");
    }
    return publish(error);
}

// No parameters: only successful real wake transitions reach this callback.
// Exception: Returns clock, persistence or publication failure without inventing a ping.
esp_err_t battery_endurance_woke()
{
    if (!s_live || s_disabled || (s_saved.current.flags & FirstWakeRecorded)) {
        return s_disabled ? s_saved.error : ESP_OK;
    }
    s_pending = s_saved;
    if (s_source_gap) {
        s_pending.current.flags |= SourceUnverified;
    }
    const esp_err_t clock_error = update_alive(esp_timer_get_time());
    auto &record = s_pending.current;
    record.flags |= FirstWakeRecorded;
    record.first_wake_elapsed_ms = record.elapsed_ms;
    if (record.flags & AliveUtcValid) {
        record.flags |= FirstWakeUtcValid;
        record.first_wake_utc_s = record.last_alive_utc_s;
    }
    const esp_err_t error = save();
    if (error != ESP_OK) {
        return error;
    }
    log_record(s_saved.current, "first-wake");
    return publish(clock_error);
}

// requested: True before settings seal; false only after the abort restores settings access.
// Exception: Returns actual persistence/clock/publication failures; no physical-off claim.
esp_err_t battery_endurance_power_off(bool requested)
{
    if (!s_live || s_disabled) {
        return s_disabled ? s_saved.error : ESP_OK;
    }
    s_pending = s_saved;
    if (s_source_gap) {
        s_pending.current.flags |= SourceUnverified;
    }
    const esp_err_t clock_error = update_alive(esp_timer_get_time());
    s_pending.current.phase = requested ? EndurancePhase::ShutdownRequested : EndurancePhase::Running;
    const esp_err_t error = save();
    if (error != ESP_OK) {
        return error;
    }
    log_record(s_saved.current, requested ? "shutdown-request" : "shutdown-cancelled");
    return publish(clock_error);
}
} // namespace LUNASHELL
