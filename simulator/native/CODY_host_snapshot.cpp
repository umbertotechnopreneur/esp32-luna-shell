// Host transport owns synthetic inputs only; production modules own decoding,
// sample aging and JSON. No host observation enters DeviceSnapshotService.
#include "LUNASHELL_snapshot_aging.hpp"
#include "LUNASHELL_http_json.hpp"
#include "LUNASHELL_rtc_calendar.hpp"
#include "esp_log.h"
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#define HOST_EXPORT extern "C" __declspec(dllexport)
#else
#define HOST_EXPORT extern "C"
#endif

namespace {
using namespace LUNASHELL;
struct HostSnapshot {
    DeviceSnapshot snapshot;
    std::string output;
};

// target: Explicit enum field whose semantic name is resolved below.
// text: Original host label, never a storage or display index.
// pairs: Unique semantic names mapped to the production typed identities.
// Exception: Returns INVALID_ARG for any unsupported identity.
template<class T, std::size_t N>
esp_err_t enumValue(T &target, const char *text, const std::pair<const char *, T> (&pairs)[N])
{
    for (const auto &entry : pairs) {
        if (std::strcmp(entry.first, text) == 0) {
            target = entry.second;
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

// target: Synthetic typed field, not a firmware canonical observation.
// text: Literal bounded value with no clamping or default substitution.
// Exception: Returns INVALID_ARG or INVALID_SIZE for syntax/range failures.
template<class T>
esp_err_t assign(T &target, const char *text)
{
    if constexpr (std::is_same_v<T, bool>) {
        if (std::strcmp(text, "true") == 0) { target = true; return ESP_OK; }
        if (std::strcmp(text, "false") == 0) { target = false; return ESP_OK; }
        return ESP_ERR_INVALID_ARG;
    } else if constexpr (std::is_floating_point_v<T>) {
        char *end = nullptr;
        errno = 0;
        const double value = std::strtod(text, &end);
        if (errno == ERANGE) { return ESP_ERR_INVALID_SIZE; }
        if (end == text || *end != '\0' || !std::isfinite(value)) { return ESP_ERR_INVALID_ARG; }
        if (std::fabs(value) > std::numeric_limits<T>::max()) { return ESP_ERR_INVALID_SIZE; }
        target = static_cast<T>(value);
        return ESP_OK;
    } else {
        T value{};
        const auto parsed = std::from_chars(text, text + std::strlen(text), value);
        if (parsed.ec == std::errc::result_out_of_range) { return ESP_ERR_INVALID_SIZE; }
        if (parsed.ec != std::errc{} || *parsed.ptr != '\0') { return ESP_ERR_INVALID_ARG; }
        target = value;
        return ESP_OK;
    }
}

// target: Bounded ASCII production text field in the host-owned snapshot.
// text: Complete original value; truncation is rejected.
// Exception: Returns INVALID_SIZE or INVALID_ARG for overflow or nonprintable input.
template<std::size_t N>
esp_err_t assign(char (&target)[N], const char *text)
{
    const auto size = std::strlen(text);
    if (size >= N) { return ESP_ERR_INVALID_SIZE; }
    for (std::size_t i = 0; i < size; ++i) {
        if (text[i] < 0x20 || text[i] > 0x7e) { return ESP_ERR_INVALID_ARG; }
    }
    std::memcpy(target, text, size + 1);
    return ESP_OK;
}

// target: Production health identity.
// text: Explicit synthetic semantic label.
// Exception: Returns INVALID_ARG for an unknown health.
esp_err_t assign(DeviceHealth &target, const char *text)
{
    const std::pair<const char *, DeviceHealth> names[] = {
        {"unknown", DeviceHealth::Unknown}, {"ready", DeviceHealth::Ready},
        {"degraded", DeviceHealth::Degraded}, {"missing", DeviceHealth::Missing}};
    return enumValue(target, text, names);
}

// component: Host-owned measurement metadata resolved by identity.
// field: Exact metadata member name.
// text: Literal original value.
// Exception: Returns NOT_FOUND for an unrecognized field, or the original conversion error.
esp_err_t componentField(ComponentSnapshot &component, const char *field, const char *text)
{
#define COMPONENT_FIELD(member) if (std::strcmp(field, #member) == 0) { return assign(component.member, text); }
    COMPONENT_FIELD(health)
    COMPONENT_FIELD(last_error)
    COMPONENT_FIELD(data_valid)
    COMPONENT_FIELD(data_stale)
    COMPONENT_FIELD(last_attempt_at_us)
    COMPONENT_FIELD(last_success_at_us)
#undef COMPONENT_FIELD
    return ESP_ERR_NOT_FOUND;
}

// snapshot: Host buffer whose component is resolved by its schema identity.
// path: Exact dotted component name; storage indices occur only at the array boundary.
// Exception: Returns nullptr for an unsupported component.
ComponentSnapshot *componentAt(DeviceSnapshot &snapshot, const char *path)
{
#define COMPONENT(member) if (std::strcmp(path, #member) == 0) { return &snapshot.member; }
    COMPONENT(battery) COMPONENT(imu) COMPONENT(imu.calibration)
    COMPONENT(sensor_fusion) COMPONENT(distance) COMPONENT(distance.settings)
    COMPONENT(distance.calibration) COMPONENT(rtc) COMPONENT(rtc.system_time)
    COMPONENT(gps) COMPONENT(audio.speaker) COMPONENT(audio.microphone) COMPONENT(audio.spectrum)
    COMPONENT(radios.wifi) COMPONENT(radios.ble) COMPONENT(storage) COMPONENT(storage.logger_mirror)
    COMPONENT(diagnostics.sample_period) COMPONENT(diagnostics.imu_smoothing) COMPONENT(diagnostics.monitor)
    COMPONENT(diagnostics.full_snapshot_period) COMPONENT(diagnostics.full_snapshot_monitor)
    COMPONENT(ui.display) COMPONENT(ui.touch) COMPONENT(ui.brightness) COMPONENT(ui.display_power_mode)
    COMPONENT(ui.orientation) COMPONENT(input.io_expander) COMPONENT(input.boot_button) COMPONENT(input.power_button)
    COMPONENT(power) COMPONENT(memory) COMPONENT(processor)
#undef COMPONENT
    if (std::strcmp(path, "distance.sensors.0") == 0) { return &snapshot.distance.sensors[0]; }
    if (std::strcmp(path, "distance.sensors.1") == 0) { return &snapshot.distance.sensors[1]; }
    return nullptr;
}

// snapshot: Host-owned destination; full schema defaults remain explicitly unavailable when unmodeled.
// path: Complete leaf path from a validated synthetic scenario.
// text: Literal value; enums are semantic labels and errors are their numeric ABI values.
// Exception: Returns NOT_FOUND for unmodeled fields or original conversion errors.
esp_err_t setField(DeviceSnapshot &snapshot, const char *path, const char *text)
{
    const char *last_dot = std::strrchr(path, '.');
    if (last_dot != nullptr && static_cast<std::size_t>(last_dot - path) < 96) {
        char parent[96]{};
        std::memcpy(parent, path, last_dot - path);
        if (auto *component = componentAt(snapshot, parent)) {
            const auto error = componentField(*component, last_dot + 1, text);
            if (error != ESP_ERR_NOT_FOUND) { return error; }
        }
    }
#define FIELD(member) if (std::strcmp(path, #member) == 0) { return assign(snapshot.member, text); }
    FIELD(sequence) FIELD(last_update_attempt_at_us) FIELD(last_update_complete) FIELD(last_update_error) FIELD(reason)
    FIELD(battery.battery_present) FIELD(battery.usb_present) FIELD(battery.vbus_good)
    FIELD(battery.batfet_enabled) FIELD(battery.battery_active_mode) FIELD(battery.thermal_regulation_active)
    FIELD(battery.input_current_limit_active) FIELD(battery.level_percent_valid) FIELD(battery.battery_mv_valid)
    FIELD(battery.level_percent) FIELD(battery.battery_mv) FIELD(battery.usb_mv) FIELD(battery.system_mv)
    FIELD(battery.die_temperature_c) FIELD(battery.power_state) FIELD(battery.charger_phase)
    FIELD(imu.temperature_c) FIELD(imu.calibrated) FIELD(imu.calibration_stationary) FIELD(imu.calibration_samples)
    FIELD(imu.calibration_accel_max_span_mps2) FIELD(imu.calibration_gyro_max_span_dps)
    FIELD(sensor_fusion.gravity_dominant) FIELD(sensor_fusion.stationary)
    FIELD(sensor_fusion.acceleration_magnitude_mps2) FIELD(sensor_fusion.angular_speed_dps)
    FIELD(distance.settings.sample_count) FIELD(distance.settings.reference_distance_mm) FIELD(distance.settings.sample_interval_ms)
    FIELD(distance.calibration.record_valid) FIELD(distance.calibration.requested_samples)
    FIELD(distance.calibration.reference_distance_mm) FIELD(distance.calibration.progress_percent)
    FIELD(distance.calibration.z_axis_sign) FIELD(distance.calibration.started_at_us) FIELD(distance.calibration.completed_at_us)
    FIELD(distance.slant_distance_mm) FIELD(distance.vertical_distance_mm) FIELD(distance.sensor_spread_mm)
    FIELD(distance.samples_used) FIELD(distance.tilt_degrees) FIELD(distance.confidence_percent) FIELD(distance.tilt_compensated)
    FIELD(distance.acquisition_timing_valid) FIELD(distance.tof_pair_skew_us) FIELD(distance.geometry_skew_us)
    FIELD(distance.rejected_pair_count) FIELD(distance.timing_rejection_count) FIELD(distance.consecutive_transport_failure_count)
    FIELD(distance.recovery_attempt_count) FIELD(distance.current_backoff_ms)
    FIELD(distance.worker_stack_high_water_valid) FIELD(distance.worker_stack_min_free_bytes)
    FIELD(rtc.clock_integrity) FIELD(rtc.system_time_valid) FIELD(rtc.synchronized)
    FIELD(rtc.year) FIELD(rtc.month) FIELD(rtc.day) FIELD(rtc.hour) FIELD(rtc.minute) FIELD(rtc.second)
    FIELD(rtc.source) FIELD(rtc.timezone)
    FIELD(gps.pending_nmea_bytes) FIELD(gps.has_fix) FIELD(gps.utc_valid) FIELD(gps.fix_quality) FIELD(gps.satellites)
    FIELD(gps.latitude_degrees) FIELD(gps.longitude_degrees) FIELD(gps.altitude_m) FIELD(gps.speed_mps)
    FIELD(gps.course_degrees) FIELD(gps.hdop) FIELD(gps.utc_time) FIELD(gps.utc_date) FIELD(gps.first_sentence)
    FIELD(audio.channel_count) FIELD(audio.sample_rate_hz) FIELD(audio.microphone_mean_level)
    FIELD(audio.microphone_peak_level) FIELD(audio.spectrum_sequence)
    FIELD(radios.wifi.started) FIELD(radios.wifi.credentials_configured) FIELD(radios.wifi.connect_requested)
    FIELD(radios.wifi.connected) FIELD(radios.wifi.ip_acquired) FIELD(radios.wifi.reconnect_pending)
    FIELD(radios.wifi.rssi) FIELD(radios.wifi.channel) FIELD(radios.wifi.last_disconnect_reason)
    FIELD(radios.wifi.event_sequence) FIELD(radios.wifi.connect_attempt_count)
    FIELD(radios.wifi.reconnect_attempt_count) FIELD(radios.wifi.disconnect_count)
    FIELD(radios.wifi.ssid) FIELD(radios.wifi.ip_address) FIELD(radios.wifi.mac_address)
    FIELD(radios.ble.provisioning) FIELD(radios.ble.advertising) FIELD(radios.ble.connected)
    FIELD(radios.ble.connection_handle) FIELD(radios.ble.connection_count) FIELD(radios.ble.disconnection_count) FIELD(radios.ble.device_name)
    FIELD(storage.cleanup_error) FIELD(storage.initial_fatfs_result_valid) FIELD(storage.initial_fatfs_result)
    FIELD(storage.fatfs_result_valid) FIELD(storage.fatfs_result) FIELD(storage.cleanup_fatfs_result_valid)
    FIELD(storage.cleanup_fatfs_result) FIELD(storage.cleanup_complete) FIELD(storage.poisoned) FIELD(storage.mounted)
    FIELD(storage.card_response_verified) FIELD(storage.format_required) FIELD(storage.authorization_consumed)
    FIELD(storage.authorization_identity_matched) FIELD(storage.format_attempted) FIELD(storage.partition_written)
    FIELD(storage.formatted) FIELD(storage.mount_attempt_count) FIELD(storage.capacity_bytes)
    FIELD(storage.filesystem_total_bytes) FIELD(storage.free_bytes) FIELD(storage.logger_mirror_requested)
    FIELD(storage.logger_mirror_enabled) FIELD(storage.logger_mirror_errno)
    FIELD(storage.logger_mirror_changed_at_us) FIELD(storage.logger_mirror_failure_count)
    FIELD(diagnostics.sample_period_ms) FIELD(diagnostics.imu_smoothing_percent) FIELD(diagnostics.full_snapshot_period_ms)
    FIELD(diagnostics.monitor_active) FIELD(diagnostics.imu_monitor_active) FIELD(diagnostics.microphone_monitor_active)
    FIELD(diagnostics.microphone_spectrum_monitor_active) FIELD(diagnostics.full_snapshot_monitor_active)
    FIELD(ui.touch_activity_sequence) FIELD(ui.touch_last_activity_at_us) FIELD(ui.brightness_percent)
    FIELD(ui.configured_brightness_percent) FIELD(ui.orientation.auto_rotation_enabled)
    FIELD(ui.orientation.gravity_valid) FIELD(ui.orientation.rotation_pending) FIELD(ui.orientation.physical_orientation_degrees)
    FIELD(ui.orientation.rendering_rotation_degrees) FIELD(ui.orientation.change_sequence)
    FIELD(ui.orientation.sampled_at_us) FIELD(ui.orientation.changed_at_us)
    FIELD(input.boot_pressed) FIELD(input.power_pressed) FIELD(input.boot_press_count) FIELD(input.boot_long_press_count)
    FIELD(input.power_short_press_count) FIELD(input.power_long_press_count) FIELD(input.wakeup_cause)
    FIELD(power.audio_suspended) FIELD(power.fullscreen_active) FIELD(power.command_arc_visible) FIELD(power.status_arc_visible)
    FIELD(power.command_arc_timeout_seconds) FIELD(power.status_arc_timeout_seconds) FIELD(power.display_dim_timeout_seconds)
    FIELD(power.screen_sleep_delay_seconds) FIELD(power.transition_sequence) FIELD(power.last_activity_at_us) FIELD(power.changed_at_us)
    FIELD(memory.flash_capacity_bytes) FIELD(memory.app_partition_bytes) FIELD(memory.firmware_image_bytes)
    FIELD(memory.heap_total_bytes) FIELD(memory.heap_free_bytes) FIELD(memory.heap_minimum_free_bytes)
    FIELD(memory.internal_heap_total_bytes) FIELD(memory.internal_free_bytes) FIELD(memory.internal_largest_block_bytes)
    FIELD(memory.internal_minimum_free_bytes) FIELD(memory.internal_allocated_blocks) FIELD(memory.internal_free_blocks)
    FIELD(memory.internal_total_blocks) FIELD(memory.internal_memory_pressure) FIELD(memory.psram_capacity_bytes)
    FIELD(memory.psram_heap_total_bytes) FIELD(memory.psram_free_bytes) FIELD(memory.psram_used_bytes)
    FIELD(memory.psram_largest_block_bytes) FIELD(memory.psram_minimum_free_bytes) FIELD(memory.psram_allocated_blocks)
    FIELD(memory.psram_free_blocks) FIELD(memory.psram_total_blocks) FIELD(memory.allocation_failure_monitor_active)
    FIELD(memory.allocation_failure_count) FIELD(memory.largest_failed_allocation_bytes) FIELD(memory.failed_allocation_caps_seen_mask)
    FIELD(processor.core_count) FIELD(processor.sampling_core)
#undef FIELD
    // Typed enum identities are resolved at this explicit transport boundary.
#define ENUM_FIELD(member, type, ...) if (std::strcmp(path, #member) == 0) { const std::pair<const char *, type> names[] = {__VA_ARGS__}; return enumValue(snapshot.member, text, names); }
    ENUM_FIELD(power.state, DeviceOperatingState, {"active", DeviceOperatingState::Active}, {"entering-standby", DeviceOperatingState::EnteringStandby}, {"standby", DeviceOperatingState::Standby}, {"waking", DeviceOperatingState::Waking})
    ENUM_FIELD(power.reason, DeviceStateReason, {"startup", DeviceStateReason::Startup}, {"boot-button", DeviceStateReason::BootButton}, {"power-button", DeviceStateReason::PowerButton}, {"touch", DeviceStateReason::Touch}, {"imu-motion", DeviceStateReason::ImuMotion}, {"inactivity", DeviceStateReason::Inactivity})
    ENUM_FIELD(ui.display_power_mode_state, UiHardwareSnapshot::DisplayPowerMode, {"unknown", UiHardwareSnapshot::DisplayPowerMode::Unknown}, {"active", UiHardwareSnapshot::DisplayPowerMode::Active}, {"dimmed", UiHardwareSnapshot::DisplayPowerMode::Dimmed}, {"display-off", UiHardwareSnapshot::DisplayPowerMode::DisplayOff})
    ENUM_FIELD(power.display_idle_stage, UiHardwareSnapshot::DisplayPowerMode, {"unknown", UiHardwareSnapshot::DisplayPowerMode::Unknown}, {"active", UiHardwareSnapshot::DisplayPowerMode::Active}, {"dimmed", UiHardwareSnapshot::DisplayPowerMode::Dimmed}, {"display-off", UiHardwareSnapshot::DisplayPowerMode::DisplayOff})
    ENUM_FIELD(distance.calibration.state, DistanceCalibrationState, {"required", DistanceCalibrationState::Required}, {"ready", DistanceCalibrationState::Ready}, {"running", DistanceCalibrationState::Running}, {"succeeded", DistanceCalibrationState::Succeeded}, {"failed", DistanceCalibrationState::Failed})
    ENUM_FIELD(storage.mount_outcome, StorageMountOutcome, {"not-attempted", StorageMountOutcome::NotAttempted}, {"mounted", StorageMountOutcome::Mounted}, {"card-or-transport-failed", StorageMountOutcome::CardOrTransportFailed}, {"format-required", StorageMountOutcome::FormatRequired}, {"filesystem-failed", StorageMountOutcome::FilesystemFailed}, {"format-failed", StorageMountOutcome::FormatFailed}, {"internal-failed", StorageMountOutcome::InternalFailed})
    ENUM_FIELD(storage.mount_stage, StorageMountStage, {"none", StorageMountStage::None}, {"request", StorageMountStage::Request}, {"host-init", StorageMountStage::HostInit}, {"slot-init", StorageMountStage::SlotInit}, {"card-init", StorageMountStage::CardInit}, {"card-identity", StorageMountStage::CardIdentity}, {"drive-alloc", StorageMountStage::DriveAlloc}, {"disk-io-register", StorageMountStage::DiskIoRegister}, {"vfs-register", StorageMountStage::VfsRegister}, {"fat-mount", StorageMountStage::FatMount}, {"pre-format-unmount", StorageMountStage::PreFormatUnmount}, {"partition", StorageMountStage::Partition}, {"format", StorageMountStage::Format}, {"post-format-mount", StorageMountStage::PostFormatMount}, {"runtime-status", StorageMountStage::RuntimeStatus}, {"runtime-filesystem", StorageMountStage::RuntimeFilesystem}, {"runtime-unmount", StorageMountStage::RuntimeUnmount})
    ENUM_FIELD(storage.cleanup_stage, StorageCleanupStage, {"none", StorageCleanupStage::None}, {"fat-unmount", StorageCleanupStage::FatUnmount}, {"vfs-unregister", StorageCleanupStage::VfsUnregister}, {"host-deinit", StorageCleanupStage::HostDeinit})
#undef ENUM_FIELD
    char indexed[96]{};
#define INDEX_FIELD(prefix, index, member) std::snprintf(indexed, sizeof(indexed), prefix ".%zu." #member, index); if (std::strcmp(path, indexed) == 0) { return assign(snapshot.member, text); }
    for (std::size_t i = 0; i < 3; ++i) {
#define VECTOR(member) std::snprintf(indexed, sizeof(indexed), #member ".%zu", i); if (std::strcmp(path, indexed) == 0) { return assign(snapshot.member[i], text); }
        VECTOR(imu.acceleration_mps2) VECTOR(imu.angular_velocity_dps)
        VECTOR(imu.acceleration_bias_mps2) VECTOR(imu.angular_velocity_bias_dps)
#undef VECTOR
    }
    for (std::size_t i = 0; i < 2; ++i) {
#define SENSOR_FIELD(member) std::snprintf(indexed, sizeof(indexed), "distance.sensors.%zu." #member, i); if (std::strcmp(path, indexed) == 0) { return assign(snapshot.distance.sensors[i].member, text); }
        SENSOR_FIELD(raw_distance_mm) SENSOR_FIELD(filtered_distance_mm) SENSOR_FIELD(window_sample_count)
        SENSOR_FIELD(inlier_count) SENSOR_FIELD(median_absolute_deviation_mm) SENSOR_FIELD(calibration_offset_mm)
        SENSOR_FIELD(frame_sequence) SENSOR_FIELD(crc_error_count) SENSOR_FIELD(protocol_error_count)
        SENSOR_FIELD(timeout_count) SENSOR_FIELD(uart_error_count)
#undef SENSOR_FIELD
#define CORE_FIELD(member) std::snprintf(indexed, sizeof(indexed), "processor.cores.%zu." #member, i); if (std::strcmp(path, indexed) == 0) { return assign(snapshot.processor.cores[i].member, text); }
        CORE_FIELD(available) CORE_FIELD(utilization_available) CORE_FIELD(utilization_percent)
        CORE_FIELD(current_task) CORE_FIELD(stack_high_water_words) CORE_FIELD(priority)
#undef CORE_FIELD
#define PAIR_FIELD(member) std::snprintf(indexed, sizeof(indexed), #member ".%zu", i); if (std::strcmp(path, indexed) == 0) { return assign(snapshot.member[i], text); }
        PAIR_FIELD(distance.calibration.accepted_samples) PAIR_FIELD(distance.calibration.rejected_samples)
        PAIR_FIELD(distance.calibration.offsets_mm) PAIR_FIELD(audio.channel_mean_level) PAIR_FIELD(audio.channel_peak_level)
#undef PAIR_FIELD
        for (std::size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
            std::snprintf(indexed, sizeof(indexed), "audio.channel_spectrum.%zu.%zu", i, band);
            if (std::strcmp(path, indexed) == 0) { return assign(snapshot.audio.channel_spectrum[i][band], text); }
        }
    }
#undef INDEX_FIELD
    return ESP_ERR_NOT_FOUND;
}

// context: Exclusive host snapshot owner used for this response only.
// data: Borrowed serializer chunk; never retained as a pointer.
// size: Exact bounded chunk byte count.
// final: Serializer completion marker, no extra bytes.
// Exception: Returns NO_MEM or INVALID_SIZE for failed or oversized host output.
esp_err_t writeChunk(void *context, const char *data, std::size_t size, bool final)
{
    auto &owner = *static_cast<HostSnapshot *>(context);
    if (final) { return ESP_OK; }
    if (owner.output.size() + size > 128 * 1024) { return ESP_ERR_INVALID_SIZE; }
    try { owner.output.append(data, size); }
    catch (const std::bad_alloc &) { return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
}

// No parameters: stable ABI of the explicit synthetic runtime transport.
HOST_EXPORT uint32_t cody_host_runtime_abi() { return 1; }
// No parameters: allocates a host-only snapshot owner; nullptr reports real allocation failure.
HOST_EXPORT void *cody_host_snapshot_create() { return new (std::nothrow) HostSnapshot{}; }
// handle: Exclusive host owner previously allocated by this transport.
HOST_EXPORT void cody_host_snapshot_destroy(void *handle) { delete static_cast<HostSnapshot *>(handle); }
// handle: Exclusive synthetic destination owner.
// path: Exact dotted leaf schema identity.
// value: Original bounded ASCII literal.
// Exception: Returns the actual schema or conversion error and logs the rejected field identity.
HOST_EXPORT int cody_host_snapshot_set(void *handle, const char *path, const char *value)
{
    if (handle == nullptr || path == nullptr || value == nullptr || std::strlen(path) >= 96) { return ESP_ERR_INVALID_ARG; }
    const auto error = setField(static_cast<HostSnapshot *>(handle)->snapshot, path, value);
    if (error != ESP_OK) { ESP_LOGE("CODY_host_snapshot", "synthetic field %s rejected (%s)", path, esp_err_to_name(error)); }
    return error;
}
// handle: Exclusive synthetic snapshot owner.
// now_us: Explicit virtual observation time; never derived from a firmware clock.
// Exception: Returns INVALID_ARG for an invalid owner or observation time.
HOST_EXPORT int cody_host_snapshot_age(void *handle, int64_t now_us)
{
    if (handle == nullptr || now_us <= 0) { return ESP_ERR_INVALID_ARG; }
    apply_measured_sample_aging(static_cast<HostSnapshot *>(handle)->snapshot, now_us);
    return ESP_OK;
}
// handle: Owner of the synthetic observations.
// path: Exact component identity, not an acquisition operation.
// values: Receives health, error, validity, staleness and original attempt/success timestamps.
// Exception: Returns INVALID_ARG or NOT_FOUND without inventing an observation.
HOST_EXPORT int cody_host_snapshot_component(void *handle, const char *path, int64_t *values)
{
    if (handle == nullptr || path == nullptr || values == nullptr) { return ESP_ERR_INVALID_ARG; }
    const auto *component = componentAt(static_cast<HostSnapshot *>(handle)->snapshot, path);
    if (component == nullptr) { return ESP_ERR_NOT_FOUND; }
    values[0] = static_cast<int64_t>(component->health); values[1] = component->last_error;
    values[2] = component->data_valid; values[3] = component->data_stale;
    values[4] = component->last_attempt_at_us; values[5] = component->last_success_at_us;
    return ESP_OK;
}
// handle: Exclusive host owner; its previous successful sample remains historical on failure.
// registers: Seven synthetic hardware register bytes, fed to the production RTC decoder.
// attempted_us: Actual virtual acquisition attempt time.
// Exception: Returns the production decoder error without fabricating a measurement.
HOST_EXPORT int cody_host_snapshot_rtc(void *handle, const uint8_t *registers, int64_t attempted_us)
{
    if (handle == nullptr || registers == nullptr || attempted_us <= 0) { return ESP_ERR_INVALID_ARG; }
    uint8_t raw[7]{}; std::memcpy(raw, registers, sizeof(raw));
    LUNASHELL_calendar_time_t decoded{};
    auto &rtc = static_cast<HostSnapshot *>(handle)->snapshot.rtc;
    const auto error = rtc_calendar::decode_registers(raw, decoded);
    rtc.last_attempt_at_us = attempted_us; rtc.last_error = error;
    rtc.data_valid = error == ESP_OK; rtc.data_stale = error != ESP_OK && rtc.last_success_at_us > 0;
    rtc.health = error == ESP_OK ? DeviceHealth::Ready : DeviceHealth::Degraded;
    if (error == ESP_OK) {
        rtc.clock_integrity = decoded.clock_integrity; rtc.year = decoded.year;
        rtc.month = decoded.month; rtc.day = decoded.day; rtc.hour = decoded.hour;
        rtc.minute = decoded.minute; rtc.second = decoded.second; rtc.last_success_at_us = attempted_us;
        std::memcpy(rtc.source, "rtc", 4);
    }
    return error;
}
// handle: Exclusive host snapshot/response owner.
// path: Exact public API route.
// now_us: Virtual observation time supplied to the real serializer.
// data: Borrowed output bytes valid until the next response or owner destruction.
// size: Receives exact JSON byte count only after successful serialization.
// Exception: Returns original serializer or route errors; failed output is not exposed.
HOST_EXPORT int cody_host_snapshot_json(void *handle, const char *path, int64_t now_us, const char **data, std::size_t *size)
{
    if (handle == nullptr || path == nullptr || data == nullptr || size == nullptr || now_us <= 0) { return ESP_ERR_INVALID_ARG; }
    auto &owner = *static_cast<HostSnapshot *>(handle);
    owner.output.clear(); apply_measured_sample_aging(owner.snapshot, now_us);
    HttpJsonWriter json({&owner, writeChunk}, now_us);
    const auto &snapshot = owner.snapshot;
    const HttpLifecycleView lifecycle{true, false, true, true};
    if (std::strcmp(path, "/api/v1/health") == 0) { httpJsonWriteHealth(json, snapshot, lifecycle, HttpLivenessView{}); }
    else if (std::strcmp(path, "/api/v1/info") == 0) { httpJsonWriteInfo(json, snapshot); }
    else if (std::strcmp(path, "/api/v1/diagnostics") == 0) { httpJsonWriteDiagnostics(json, snapshot); }
    else if (std::strcmp(path, "/api/v1/rtc") == 0) { httpJsonWriteRtc(json, snapshot); }
    else if (std::strcmp(path, "/api/v1/gps") == 0) { httpJsonWriteGps(json, snapshot); }
    else if (std::strcmp(path, "/api/v1/status") == 0) { httpJsonWriteStatus(json, snapshot, HttpMetricsView{}, lifecycle); }
    else if (std::strcmp(path, "/api/v1/capabilities") == 0) {
        const auto &period = snapshot.diagnostics.full_snapshot_period;
        const bool valid = period.health == DeviceHealth::Ready && period.last_error == ESP_OK && period.data_valid;
        httpJsonWriteCapabilities(json, valid ? snapshot.diagnostics.full_snapshot_period_ms : 0);
    } else { return ESP_ERR_NOT_FOUND; }
    const auto error = json.finish();
    if (error != ESP_OK) { return error; }
    *data = owner.output.data(); *size = owner.output.size();
    return ESP_OK;
}
