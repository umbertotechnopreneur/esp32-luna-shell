"""Verify the non-UI HTTP API settings and lifecycle contract."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "components/qsee_platform/include/qsee_http_server.hpp"
SOURCE = ROOT / "components/qsee_platform/qsee_http_server.cpp"
DOC = ROOT / "docs/architecture/http-device-api.md"


def require_contains(text: str, fragment: str, label: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{label} is missing: {fragment}")


def main() -> int:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
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

    print("HTTP API contract verification: PASS (bounded key persistence, lifecycle guard)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
