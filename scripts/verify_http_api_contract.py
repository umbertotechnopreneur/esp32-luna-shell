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
        "(bounded key persistence, lifecycle guard, raw heap telemetry)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
