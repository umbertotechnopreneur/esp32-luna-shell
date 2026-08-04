"""Deterministic checks for RTC reset-state and real-source recovery."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HARDWARE = ROOT / "components" / "LUNASHELL_platform" / "LUNASHELL_hardware.cpp"
TIME = ROOT / "components" / "LUNASHELL_platform" / "LUNASHELL_time.cpp"
WIFI = ROOT / "components" / "LUNASHELL_platform" / "LUNASHELL_wifi.cpp"


@dataclass(frozen=True)
class Calendar:
    integrity: bool
    year: int
    month: int
    day: int
    hour: int
    minute: int
    second: int


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_contains(text: str, fragment: str, source: str) -> None:
    require(fragment in text, f"{fragment!r} missing from {source}")


def bcd(value: int) -> int:
    return ((value >> 4) * 10) + (value & 0x0F)


def decode(raw: tuple[int, ...]) -> Calendar:
    require(len(raw) == 7, "RTC register image must contain seven bytes")
    return Calendar(
        integrity=(raw[0] & 0x80) == 0,
        year=2000 + bcd(raw[6]),
        month=bcd(raw[5] & 0x1F),
        day=bcd(raw[3] & 0x3F),
        hour=bcd(raw[2] & 0x3F),
        minute=bcd(raw[1] & 0x7F),
        second=bcd(raw[0] & 0x7F),
    )


def main() -> int:
    hardware = HARDWARE.read_text(encoding="utf-8")
    time_source = TIME.read_text(encoding="utf-8")
    wifi = WIFI.read_text(encoding="utf-8")

    reset_calendar = decode((0x80, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00))
    require(not reset_calendar.integrity, "power-on OS flag must invalidate integrity")
    require(reset_calendar.year == 2000, "power-on year must decode structurally")

    read_start = hardware.index("esp_err_t read_rtc_calendar(")
    read_end = hardware.index("esp_err_t publish_rtc_read(", read_start)
    rtc_read = hardware[read_start:read_end]
    for fragment in (
        "valid_rtc_register_calendar(decoded)",
        "*time = decoded;",
        "if (!decoded.clock_integrity)",
        "if (!valid_calendar_time(decoded))",
        "return ESP_ERR_INVALID_STATE;",
    ):
        require_contains(rtc_read, fragment, "read_rtc_calendar")
    require(
        rtc_read.index("valid_rtc_register_calendar(decoded)")
        < rtc_read.index("*time = decoded;")
        < rtc_read.index("if (!decoded.clock_integrity)")
        < rtc_read.index("if (!valid_calendar_time(decoded))"),
        "RTC read must separate malformed registers, OS integrity, and trusted epoch",
    )
    publish_start = hardware.index("esp_err_t publish_rtc_read(")
    publish_end = hardware.index("esp_err_t sample_rtc(", publish_start)
    rtc_publish = hardware[publish_start:publish_end]
    require_contains(
        rtc_publish,
        "error == ESP_ERR_INVALID_STATE &&",
        "RTC invalid-state publication",
    )
    require_contains(
        rtc_publish,
        "valid_rtc_register_calendar(time)",
        "RTC invalid-state publication",
    )
    require_contains(
        rtc_publish,
        "snapshot.rtc.year = time.year;",
        "RTC recovery-state publication",
    )
    require_contains(
        hardware,
        "RTC PCF85063: detected, 24-hour mode configured",
        "RTC initialization log",
    )
    require(
        "oscillator running in 24-hour mode" not in hardware,
        "RTC initialization must not claim oscillator integrity before reading OS",
    )

    for fragment in (
        "config.sync_cb = sntp_sync_callback;",
        "persist_epoch_to_rtc(time_value->tv_sec)",
        'apply_utc_calendar(year, month, day, hour, minute, second, "gps")',
    ):
        require_contains(time_source, fragment, "real UTC synchronization")
    require_contains(
        wifi,
        "LUNASHELL_time_notify_wifi_online();",
        "DHCP-to-SNTP wiring",
    )
    require_contains(
        hardware,
        "LUNASHELL_time_sync_from_gps(year, month, day, hour, minute, second);",
        "GPS-to-RTC wiring",
    )

    print(
        "RTC contract verification: PASS "
        "(reset image, OS integrity, trusted epoch, SNTP/GPS persistence)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
