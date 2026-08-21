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

    battery_json = function_text(
        json_source,
        "void writeBattery(",
        "battery JSON serializer",
    )
    for fragment in (
        "battery.health == LUNASHELL::DeviceHealth::Ready",
        "battery.last_error == ESP_OK",
        "battery.data_valid",
        "!battery.data_stale",
        '\\"data_valid\\"',
        "json.boolean(battery.data_valid)",
        '\\"data_stale\\"',
        "json.boolean(battery.data_stale)",
        '\\"last_attempt_at_us\\"',
        '\\"last_success_at_us\\"',
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

    print(
        "HTTP API contract verification: PASS "
        "(bounded key persistence, lifecycle guard, nullable battery fields, "
        "raw heap telemetry)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
