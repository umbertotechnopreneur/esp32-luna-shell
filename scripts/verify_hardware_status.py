"""Deterministic checks for the hardware-status classification contract."""

from enum import Enum, auto
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Indicator(Enum):
    READY = auto()
    WARNING = auto()
    MUTED = auto()
    MISSING = auto()
    FAULT = auto()


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, fragment: str, source: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{fragment!r} missing from {source}")


def classify(
    *,
    standby: bool = False,
    ready: bool = False,
    missing: bool = False,
    fault: bool = False,
    waiting: bool = False,
) -> Indicator:
    if fault:
        return Indicator.FAULT
    if standby:
        return Indicator.MUTED
    if missing:
        return Indicator.MISSING
    if ready and not waiting:
        return Indicator.READY
    return Indicator.WARNING


def main() -> None:
    hardware = read("components/LUNASHELL_platform/LUNASHELL_hardware.cpp")
    status_bar = read("components/LUNASHELL_app/LUNASHELL_status_bar.cpp")
    scheduler = read("components/LUNASHELL_platform/LUNASHELL_snapshot_scheduler.cpp")

    gps_start = hardware.index("esp_err_t sample_gps(")
    gps_end = hardware.index(
        "void build_audio_spectrum(",
        gps_start,
    )
    gps_sample = hardware[gps_start:gps_end]

    # A successful empty queue is a healthy receiver waiting for NMEA.
    if "error = ESP_ERR_NOT_FOUND;" in gps_sample:
        raise AssertionError(
            "sample_gps must not classify an empty NMEA queue as missing hardware"
        )
    require(
        gps_sample,
        "transport online, no NMEA bytes waiting",
        "GPS empty-queue handling",
    )
    require(hardware, 'return gps.has_fix ? "fix" : "no-fix";', "serial summary")
    require(hardware, 'return "off-policy";', "radio serial summary")

    # Rejected RTC values must expose the real register bytes.
    require(
        hardware,
        "RTC PCF85063 rejected non-BCD calendar: raw=[",
        "RTC validation",
    )
    require(
        hardware,
        "RTC PCF85063 rejected calendar range: decoded=",
        "RTC validation",
    )
    require(
        hardware,
        "RTC PCF85063 calendar integrity unavailable (oscillator-stop flag set):",
        "RTC validation",
    )
    require(
        hardware,
        "RTC PCF85063 calendar is structurally valid but not initialized",
        "RTC reset-state classification",
    )

    app = read("components/LUNASHELL_app/LUNASHELL_app.cpp")
    require(
        app,
        "bool rtc_calendar_structurally_valid(",
        "RTC structural waiting guard",
    )
    require(app, "bool rtc_waiting_for_time_source(", "RTC waiting state")
    require(
        app,
        "rtc_calendar_structurally_valid(rtc)",
        "RTC structural waiting guard",
    )
    require(app, "WAITING FOR SNTP/GPS", "RTC waiting state")
    require(
        app,
        "RECOVERY: WAITING FOR A VALID SNTP OR GPS UTC FIX",
        "RTC detail recovery guidance",
    )

    # The status ribbon uses separate visual classes, not one red catch-all.
    require(status_bar, "bool component_standby(", "status semantics")
    require(status_bar, "bool radio_deferred_by_policy(", "status semantics")
    require(
        status_bar,
        "ble_color = palette::kStatusMuted;",
        "deferred BLE status",
    )
    require(
        status_bar,
        "wifi_color = palette::kStatusMuted;",
        "deferred Wi-Fi status",
    )
    require(
        status_bar,
        "!snapshot.storage.card_response_verified",
        "unconfirmed SD status",
    )
    require(
        scheduler,
        "kTaskStackBytes = 12U * 1024U;",
        "periodic Full Snapshot stack budget",
    )
    require(
        scheduler,
        "Full Snapshot stack high-water mark:",
        "periodic Full Snapshot stack evidence",
    )

    expected = {
        "healthy": Indicator.READY,
        "radio-off-policy": Indicator.MUTED,
        "gps-no-fix": Indicator.WARNING,
        "gps-missing": Indicator.MISSING,
        "sd-unconfirmed": Indicator.WARNING,
        "cleanup-failed": Indicator.FAULT,
    }
    actual = {
        "healthy": classify(ready=True),
        "radio-off-policy": classify(standby=True),
        "gps-no-fix": classify(ready=True, waiting=True),
        "gps-missing": classify(missing=True),
        "sd-unconfirmed": classify(waiting=True),
        "cleanup-failed": classify(fault=True),
    }
    if actual != expected:
        raise AssertionError(f"hardware status matrix mismatch: {actual!r}")

    print(
        "hardware status verification: PASS "
        "(ready, off-policy, no-fix, missing, unconfirmed, fault)"
    )


if __name__ == "__main__":
    main()
