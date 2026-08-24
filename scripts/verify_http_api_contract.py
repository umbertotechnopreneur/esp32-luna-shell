"""Verify the non-UI HTTP API settings and lifecycle contract."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "components/LUNASHELL_platform/include/LUNASHELL_http_server.hpp"
SOURCE = ROOT / "components/LUNASHELL_platform/LUNASHELL_http_server.cpp"
JSON_SOURCE = ROOT / "components/LUNASHELL_platform/LUNASHELL_http_json.cpp"
DOC = ROOT / "docs/architecture/http-device-api.md"

MEMORY_FIELDS = (
    "internal_heap_total_bytes",
    "internal_minimum_free_bytes",
    "internal_allocated_blocks",
    "internal_free_blocks",
    "internal_total_blocks",
    "psram_minimum_free_bytes",
    "psram_allocated_blocks",
    "psram_free_blocks",
    "psram_total_blocks",
    "allocation_failure_monitor_active",
    "allocation_failure_count",
    "largest_failed_allocation_bytes",
    "failed_allocation_caps_seen_mask",
)


def require_contains(text: str, fragment: str, label: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{label} is missing: {fragment}")


def function_text(text: str, signature: str, label: str) -> str:
    signature_at = text.find(signature)
    if signature_at < 0:
        raise AssertionError(f"{label} is missing: {signature}")
    body_at = text.find("{", signature_at + len(signature))
    if body_at < 0:
        raise AssertionError(f"{label} has no body: {signature}")
    depth = 0
    for index in range(body_at, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[signature_at : index + 1]
    raise AssertionError(f"{label} has an unterminated body: {signature}")


def main() -> int:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    json_source = JSON_SOURCE.read_text(encoding="utf-8")
    doc = DOC.read_text(encoding="utf-8")

    require_contains(header, "esp_err_t httpServerSetApiKey(const char *api_key);", "header API")
    require_contains(source, "esp_err_t validateApiKeyValue(const char *value)", "bounded validation")
    require_contains(source, "error = settings.setString(kApiKeySetting, api_key);", "persistent API key write")
    require_contains(source, "error = loadApiKeyValue(api_key);", "active credential update")
    require_contains(source, "HTTP API key updated without changing server lifecycle", "lifecycle guard")
    require_contains(source, "error = settings.getBool(kEnabledSetting, &enabled);", "persisted enable read")
    require_contains(source, "error = loadApiKeyFromSettings();", "persisted key read")
    require_contains(source, "if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)", "station lifecycle start")
    require_contains(doc, "httpServerSetApiKey()", "API documentation")

    component_current = function_text(
        json_source,
        "bool componentCurrent(",
        "component current predicate",
    )
    for fragment in (
        "component.health == LUNASHELL::DeviceHealth::Ready",
        "component.last_error == ESP_OK",
        "component.data_valid",
        "!component.data_stale",
    ):
        require_contains(
            component_current,
            fragment,
            "component current predicate",
        )

    component_json = function_text(
        json_source,
        "void writeComponentStart(",
        "component freshness JSON serializer",
    )
    for fragment in (
        '\\"data_valid\\"',
        "json.boolean(component.data_valid)",
        '\\"data_stale\\"',
        "json.boolean(component.data_stale)",
        '\\"last_attempt_at_us\\"',
        '\\"last_success_at_us\\"',
    ):
        require_contains(
            component_json,
            fragment,
            "component freshness JSON contract",
        )

    battery_json = function_text(
        json_source,
        "void writeBattery(",
        "battery JSON serializer",
    )
    for fragment in (
        "const bool battery_current = componentCurrent(battery);",
        "writeComponentStart(json, battery);",
    ):
        require_contains(
            battery_json,
            fragment,
            "battery current/freshness JSON contract",
        )
    for field, value in (
        ("level_percent", "battery.level_percent"),
        ("battery_mv", "battery.battery_mv"),
    ):
        valid = f"{field}_valid"
        require_contains(
            battery_json,
            f"const bool {valid} =\n        battery_current &&\n        battery.{valid}",
            f"{field} effective current-field guard",
        )
        flag_at = battery_json.find(f'\\"{valid}\\"')
        flag_value_at = battery_json.find(
            f"json.boolean({valid})",
            flag_at,
        )
        value_at = battery_json.find(f'\\"{field}\\"', flag_value_at)
        guard_at = battery_json.find(f"if ({valid})", value_at)
        numeric_at = battery_json.find(f'json.format("%d", {value})', guard_at)
        else_at = battery_json.find("} else {", numeric_at)
        null_at = battery_json.find('json.raw("null")', else_at)
        if not (
            0 <= flag_at < flag_value_at < value_at < guard_at <
            numeric_at < else_at < null_at
        ):
            raise AssertionError(
                f"{field} must expose {valid} and serialize invalid data as null"
            )

    for field in MEMORY_FIELDS:
        require_contains(
            json_source,
            f'\\"{field}\\"',
            f"memory JSON field {field}",
        )
        require_contains(
            doc,
            f"`{field}`",
            f"documented memory JSON field {field}",
        )

    require_contains(
        source,
        "LUNASHELL::httpJsonWriteInfo(response, snapshot);",
        "redacted info dispatch",
    )
    require_contains(
        source,
        "LUNASHELL::httpJsonWriteDiagnostics(response, snapshot);",
        "complete diagnostics dispatch",
    )

    envelope_json = function_text(
        json_source,
        "void writeEnvelopeStart(",
        "snapshot JSON envelope",
    )
    for fragment in (
        '\\"last_update_error\\"',
        "esp_err_to_name(snapshot.last_update_error)",
    ):
        require_contains(
            envelope_json,
            fragment,
            "snapshot last-update error envelope",
        )

    info_json = function_text(
        json_source,
        "void httpJsonWriteInfo(",
        "info JSON serializer",
    )
    require_contains(
        info_json,
        '"LUNASHELL.device.info.v1"',
        "info JSON schema",
    )
    require_contains(
        info_json,
        "writeSnapshotData(json, snapshot, false);",
        "redacted info snapshot",
    )

    diagnostics_json = function_text(
        json_source,
        "void httpJsonWriteDiagnostics(",
        "diagnostics JSON serializer",
    )
    require_contains(
        diagnostics_json,
        '"LUNASHELL.device.diagnostics.v1"',
        "diagnostics JSON schema",
    )
    require_contains(
        diagnostics_json,
        "writeSnapshotData(json, snapshot, true);",
        "complete diagnostics snapshot",
    )

    snapshot_json = function_text(
        json_source,
        "void writeSnapshotData(",
        "DeviceSnapshot JSON serializer",
    )
    for group in (
        "battery",
        "imu",
        "sensor_fusion",
        "distance",
        "rtc",
        "gps",
        "audio",
        "radios",
        "storage",
        "diagnostics",
        "ui",
        "input",
        "power",
        "memory",
        "processor",
    ):
        require_contains(
            snapshot_json,
            f'\\"{group}\\"',
            f"complete DeviceSnapshot group {group}",
        )

    for signature, label in (
        ("void writeImu(", "IMU"),
        ("void writeSensorFusion(", "sensor fusion"),
        ("void writeRtcData(", "RTC"),
        ("void writeGpsData(", "GPS"),
        ("void writeWifi(", "Wi-Fi"),
        ("void writeBle(", "BLE"),
        ("void writeStorage(", "storage"),
        ("void writePowerManagement(", "power management"),
        ("void writeMemory(", "memory"),
        ("void writeProcessor(", "processor"),
        ("void writeTofSensor(", "ToF sensor"),
        ("void writeDistanceSettings(", "distance settings"),
        ("void writeDistanceCalibration(", "distance calibration"),
        ("void writeDistance(", "distance"),
    ):
        component_writer = function_text(
            json_source,
            signature,
            f"{label} JSON serializer",
        )
        require_contains(
            component_writer,
            "writeComponentStart(json,",
            f"{label} common component prefix",
        )

    gps_json = function_text(
        json_source,
        "void writeGpsData(",
        "GPS JSON serializer",
    )
    for fragment in (
        '\\"fix_valid\\"',
        "componentCurrent(gps)",
        "std::isfinite(gps.latitude_degrees)",
        "std::isfinite(gps.longitude_degrees)",
        'json.raw("null")',
    ):
        require_contains(gps_json, fragment, "guarded GPS numeric telemetry")

    storage_json = function_text(
        json_source,
        "void writeStorage(",
        "storage JSON serializer",
    )
    for field in (
        "logger_mirror",
        "mount_outcome",
        "mount_stage",
        "cleanup_stage",
        "cleanup_error",
        "initial_fatfs_result_valid",
        "initial_fatfs_result",
        "fatfs_result_valid",
        "fatfs_result",
        "cleanup_fatfs_result_valid",
        "cleanup_fatfs_result",
        "cleanup_complete",
        "poisoned",
        "mounted",
        "card_response_verified",
        "format_required",
        "authorization_consumed",
        "authorization_identity_matched",
        "format_attempted",
        "partition_written",
        "formatted",
        "mount_attempt_count",
        "capacity_bytes",
        "filesystem_total_bytes",
        "free_bytes",
        "logger_mirror_requested",
        "logger_mirror_enabled",
        "logger_mirror_errno",
        "logger_mirror_changed_at_us",
        "logger_mirror_failure_count",
    ):
        require_contains(
            storage_json,
            f'\\"{field}\\"',
            f"complete storage field {field}",
        )

    audio_json = function_text(
        json_source,
        "void writeAudio(",
        "audio JSON serializer",
    )
    for fragment in (
        '\\"channel_spectrum\\"',
        "audio.channel_spectrum[channel][band]",
        '\\"spectrum_sequence\\"',
    ):
        require_contains(audio_json, fragment, "complete audio spectrum")

    processor_json = function_text(
        json_source,
        "void writeProcessor(",
        "processor JSON serializer",
    )
    for fragment in (
        '\\"available\\"',
        '\\"utilization_available\\"',
        '\\"utilization_percent\\"',
        'json.raw("null")',
    ):
        require_contains(processor_json, fragment, "processor validity contract")

    for fragment in (
        "`LUNASHELL.device.diagnostics.v1`",
        "`last_update_error`",
        "`format_required`",
    ):
        require_contains(doc, fragment, "documented diagnostics contract")

    print(
        "HTTP API contract verification: PASS "
        "(bounded key persistence, lifecycle guard, nullable battery fields, "
        "raw heap telemetry)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
