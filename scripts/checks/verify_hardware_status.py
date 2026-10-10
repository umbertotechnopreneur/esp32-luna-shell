# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Deterministic checks for the hardware-status classification contract."""

from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common.source_contracts import read_component_source


ROOT = Path(__file__).resolve().parents[2]


class Indicator(Enum):
    READY = auto()
    WARNING = auto()
    MUTED = auto()
    MISSING = auto()
    FAULT = auto()


@dataclass
class RefreshErrorReportModel:
    last_error: int = 0
    last_report_ms: int = 0
    repeated_count: int = 0

    def observe(self, error: int, now_ms: int) -> tuple[str, int] | None:
        if error == 0:
            if self.last_error == 0:
                return None
            event = ("recovered", self.repeated_count)
            self.last_error = 0
            self.last_report_ms = 0
            self.repeated_count = 0
            return event
        if self.last_error != error:
            previous_repeats = self.repeated_count
            event = (
                "changed" if self.last_error != 0 else "failed",
                previous_repeats,
            )
            self.last_error = error
            self.last_report_ms = now_ms
            self.repeated_count = 0
            return event

        self.repeated_count += 1
        elapsed_ms = (now_ms - self.last_report_ms) & 0xFFFF_FFFF
        if elapsed_ms < 60_000:
            return None
        event = ("reminder", self.repeated_count)
        self.last_report_ms = now_ms
        self.repeated_count = 0
        return event


# relative: Requested source; split implementation families are read together.
# Raises OSError or UnicodeError when a required source is unavailable.
def read(relative: str) -> str:
    return read_component_source(ROOT / relative)


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


def verify_refresh_error_reporting() -> None:
    model = RefreshErrorReportModel()
    if model.observe(1, 9_000) != ("failed", 0):
        raise AssertionError("first aggregate refresh error was not reported")
    if model.observe(1, 10_000) is not None or model.repeated_count != 1:
        raise AssertionError("identical aggregate refresh error was not coalesced")
    if model.observe(1, 68_999) is not None or model.repeated_count != 2:
        raise AssertionError("refresh-error reminder fired before 60 seconds")
    if model.observe(1, 69_000) != ("reminder", 3):
        raise AssertionError("refresh-error reminder lost its repeat count")
    if model.observe(1, 70_000) is not None:
        raise AssertionError("post-reminder aggregate error was not coalesced")
    if model.observe(2, 71_000) != ("changed", 1):
        raise AssertionError("aggregate error change was not reported immediately")
    if model.observe(0, 72_000) != ("recovered", 0):
        raise AssertionError("aggregate refresh recovery was not reported immediately")
    if model.observe(0, 73_000) is not None:
        raise AssertionError("healthy refresh emitted a redundant recovery report")
    if model.observe(1, 74_000) != ("failed", 0):
        raise AssertionError("a new aggregate failure after recovery was not immediate")

    wrapped = RefreshErrorReportModel()
    wrap_start_ms = 0xFFFF_F000
    if wrapped.observe(1, wrap_start_ms) != ("failed", 0):
        raise AssertionError("tick-wrap fixture did not report its first failure")
    if wrapped.observe(1, (wrap_start_ms + 59_999) & 0xFFFF_FFFF) is not None:
        raise AssertionError("tick-wrap reminder fired before 60 seconds")
    if wrapped.observe(1, (wrap_start_ms + 60_000) & 0xFFFF_FFFF) != (
        "reminder",
        2,
    ):
        raise AssertionError("tick-wrap reminder was not subtraction-safe")

    fastest = RefreshErrorReportModel()
    if fastest.observe(1, 0) != ("failed", 0):
        raise AssertionError("minimum-cadence fixture did not report first failure")
    for offset_ms in range(300, 60_000, 300):
        if fastest.observe(1, offset_ms) is not None:
            raise AssertionError("minimum-cadence reminder fired before 60 seconds")
    if fastest.observe(1, 60_000) != ("reminder", 200):
        raise AssertionError("minimum-cadence reminder count is not deterministic")


# No parameters: check snapshot classification and the current status/settings consumers.
# Raises AssertionError when unavailable measurements or deferred radios are misrepresented.
def main() -> None:
    hardware = read("components/LUNASHELL_platform/src/snapshot/LUNASHELL_hardware.cpp")
    gnss = read("components/LUNASHELL_platform/src/snapshot/LUNASHELL_acquisition_gnss.cpp")
    rtc_calendar = read("components/LUNASHELL_platform/src/snapshot/LUNASHELL_rtc_calendar.cpp")
    status_bar = read("components/LUNASHELL_app/src/chrome/LUNASHELL_status_bar.cpp")
    scheduler = read("components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_scheduler.cpp")

    gps_start = gnss.index("esp_err_t sample_gps(")
    gps_end = gnss.index("\n}\n", gps_start) + 3
    gps_sample = gnss[gps_start:gps_end]

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
    gps_exchange_start = gnss.index("esp_err_t gps_exchange(")
    gps_exchange_end = gnss.index(
        "bool decimal_digits(",
        gps_exchange_start,
    )
    gps_exchange = gnss[gps_exchange_start:gps_exchange_end]
    receive_at = gps_exchange.index(
        "i2c_master_receive(s_gps_read, data, data_length, 1000)"
    )
    phase_delay_at = gps_exchange.index(
        "vTaskDelay(kGpsI2cProtocolDelayTicks)",
        receive_at,
    )
    return_at = gps_exchange.index("return ESP_OK;", receive_at)
    if not receive_at < phase_delay_at < return_at:
        raise AssertionError(
            "LC76G I2C exchange must wait after a successful 0x54 read before "
            "the next 0x50 control phase"
        )
    require(
        gnss,
        "constexpr TickType_t kGpsI2cProtocolDelayTicks = pdMS_TO_TICKS(10);",
        "LC76G I2C post-read phase delay",
    )
    pair_ack_start = gnss.index("GpsPairAck gps_find_pair_ack(")
    pair_ack_end = gnss.index("esp_err_t gps_send_pair_command(", pair_ack_start)
    pair_ack = gnss[pair_ack_start:pair_ack_end]
    require(
        pair_ack,
        "GpsPairAck observed = GpsPairAck::None;",
        "LC76G PAIR acknowledgement accumulation",
    )
    require(
        pair_ack,
        "observed = GpsPairAck::Processing;\n                        continue;",
        "LC76G PAIR in-progress acknowledgement scan",
    )
    require(
        pair_ack,
        "return observed;",
        "LC76G PAIR final acknowledgement result",
    )
    if "return GpsPairAck::Processing;" in pair_ack:
        raise AssertionError(
            "LC76G PAIR parser must scan past an in-progress acknowledgement "
            "to find a coalesced final acknowledgement"
        )
    require(hardware, 'return gps.has_fix ? "fix" : "no-fix";', "serial summary")
    require(hardware, 'return "off-policy";', "radio serial summary")

    # Rejected RTC values must expose the real register bytes.
    require(
        rtc_calendar,
        "RTC PCF85063 rejected non-BCD calendar: raw=[",
        "RTC validation",
    )
    require(
        rtc_calendar,
        "RTC PCF85063 rejected calendar range: decoded=",
        "RTC validation",
    )
    require(
        rtc_calendar,
        "RTC PCF85063 calendar integrity unavailable (oscillator-stop flag set):",
        "RTC validation",
    )
    require(
        rtc_calendar,
        "RTC PCF85063 calendar is structurally valid but not initialized",
        "RTC reset-state classification",
    )

    app = read("components/LUNASHELL_app/src/runtime/LUNASHELL_app.cpp")
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

    # Status and settings distinguish standby/deferred policy from hardware faults.
    require(status_bar, "bool component_standby(", "status semantics")
    require(app, "bool radio_deferred_by_policy(", "settings radio semantics")
    require(
        app,
        'std::snprintf(ble_state.data(), ble_state.size(), "off");',
        "deferred BLE status",
    )
    require(
        app,
        'std::snprintf(wifi_state.data(), wifi_state.size(), "off");',
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
    for fragment in (
        "constexpr uint32_t kRefreshErrorReminderMs = 60U * 1000U;",
        "struct RefreshErrorState",
        "void report_periodic_refresh_result(",
        "if (refresh_error == ESP_OK)",
        "if (state.last_error != refresh_error)",
        "state.repeated_count < UINT32_MAX",
        "now_tick - state.last_report_tick < reminder_ticks",
        "Periodic Full Snapshot refresh remains unavailable",
        "Periodic Full Snapshot refresh recovered from",
        "report_periodic_refresh_result(refresh_error, refresh_error_state);",
    ):
        require(scheduler, fragment, "periodic refresh error coalescing")
    refresh_at = scheduler.index("const esp_err_t refresh_error =")
    report_at = scheduler.index(
        "report_periodic_refresh_result(refresh_error, refresh_error_state);",
        refresh_at,
    )
    completion_watch_at = scheduler.index(
        "LUNASHELL_liveness_watch_progress(",
        report_at,
    )
    if not refresh_at < report_at < completion_watch_at:
        raise AssertionError(
            "aggregate refresh reporting must preserve refresh and watchdog ordering"
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

    verify_refresh_error_reporting()

    print(
        "hardware status verification: PASS "
        "(ready, off-policy, no-fix, missing, unconfirmed, fault, "
        "coalesced aggregate refresh errors)"
    )


if __name__ == "__main__":
    main()
