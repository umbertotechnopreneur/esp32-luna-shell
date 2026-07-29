"""Deterministic, hardware-free verification for the power policy.

The behavioral models below are executable specifications.  They deliberately
do not import firmware code, so boundary and event-order regressions remain
visible even when the implementation changes.  The source-contract checks tie
those specifications to the ESP-IDF implementation without requiring a build.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path
import re
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]
POWER_SOURCE = ROOT / "components" / "qsee_platform" / "qsee_power.cpp"
HARDWARE_SOURCE = ROOT / "components" / "qsee_platform" / "qsee_hardware.cpp"
PLATFORM_HEADER = (
    ROOT / "components" / "qsee_platform" / "include" / "qsee_platform.h"
)
DISPLAY_SOURCE = (
    ROOT / "components" / "qsee_platform" / "qsee_display_settings.cpp"
)
BSP_DISPLAY_SOURCE = (
    ROOT
    / "components"
    / "esp32_s3_touch_amoled_1_75"
    / "esp32_s3_touch_amoled_1_75.c"
)

PKEY_SHORT_IRQ = 1 << 0
PKEY_LONG_IRQ = 1 << 1
ERR_OK = 0
ERR_DELIVERY_FAILED = 1


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require_contains(text: str, fragment: str, source: str) -> None:
    require(fragment in text, f"{fragment!r} missing from {source}")


def require_regex(text: str, pattern: str, source: str) -> re.Match[str]:
    match = re.search(pattern, text, flags=re.IGNORECASE | re.DOTALL)
    require(match is not None, f"/{pattern}/ missing from {source}")
    return match


def function_text(text: str, signature: str, source: str) -> str:
    signature_at = text.find(signature)
    require(signature_at >= 0, f"{signature!r} missing from {source}")
    body_at = text.find("{", signature_at + len(signature))
    require(body_at >= 0, f"body for {signature!r} missing from {source}")

    depth = 0
    for index in range(body_at, len(text)):
        token = text[index]
        if token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
            if depth == 0:
                return text[signature_at : index + 1]
    raise AssertionError(f"unterminated body for {signature!r} in {source}")


class DisplayStage(Enum):
    ACTIVE = auto()
    DIMMED = auto()
    OFF = auto()


@dataclass(frozen=True)
class InactivityPolicy:
    command_timeout_ms: int
    status_timeout_ms: int
    dim_timeout_ms: int
    screen_sleep_delay_ms: int

    def __post_init__(self) -> None:
        require(
            min(
                self.command_timeout_ms,
                self.status_timeout_ms,
                self.dim_timeout_ms,
                self.screen_sleep_delay_ms,
            )
            >= 0,
            "timeouts must not be negative",
        )


@dataclass(frozen=True)
class IdleState:
    command_visible: bool
    status_visible: bool
    display: DisplayStage


@dataclass
class InactivityTimeline:
    policy: InactivityPolicy
    last_activity_ms: int = 0

    def note_activity(self, now_ms: int) -> None:
        require(
            now_ms >= self.last_activity_ms,
            "activity timestamps must be monotonic",
        )
        self.last_activity_ms = now_ms

    def state_at(self, now_ms: int) -> IdleState:
        require(
            now_ms >= self.last_activity_ms,
            "policy timestamps must be monotonic",
        )
        elapsed_ms = now_ms - self.last_activity_ms

        def expired(timeout_ms: int) -> bool:
            return timeout_ms > 0 and elapsed_ms >= timeout_ms

        display = DisplayStage.ACTIVE
        if expired(self.policy.dim_timeout_ms):
            display = DisplayStage.DIMMED
            if (
                self.policy.screen_sleep_delay_ms > 0
                and elapsed_ms
                >= self.policy.dim_timeout_ms
                + self.policy.screen_sleep_delay_ms
            ):
                display = DisplayStage.OFF

        return IdleState(
            command_visible=not expired(self.policy.command_timeout_ms),
            status_visible=not expired(self.policy.status_timeout_ms),
            display=display,
        )


@dataclass(frozen=True)
class MotionPolicy:
    acceleration_enter: float = 0.80
    acceleration_quiet: float = 0.40
    angular_enter: float = 4.0
    angular_quiet: float = 2.0
    enter_samples: int = 3
    quiet_samples: int = 3
    cooldown_ms: int = 2_000
    manual_standby_grace_ms: int = 2_000

    def __post_init__(self) -> None:
        require(
            0 <= self.acceleration_quiet < self.acceleration_enter,
            "acceleration thresholds must form hysteresis",
        )
        require(
            0 <= self.angular_quiet < self.angular_enter,
            "angular thresholds must form hysteresis",
        )
        require(self.enter_samples >= 2, "motion needs consecutive enter samples")
        require(self.quiet_samples >= 2, "motion needs a quiet dwell")
        require(self.cooldown_ms > 0, "motion cooldown must be explicit")
        require(
            self.manual_standby_grace_ms > 0,
            "manual standby motion grace must be explicit",
        )


@dataclass
class QualifiedMotionFilter:
    policy: MotionPolicy
    armed: bool = False
    enter_count: int = 0
    quiet_count: int = 0
    last_trigger_ms: int | None = None
    grace_until_ms: int = 0
    last_sample_ms: int | None = None

    def begin_manual_standby(self, now_ms: int) -> None:
        self._require_monotonic(now_ms)
        self.grace_until_ms = now_ms + self.policy.manual_standby_grace_ms
        self.armed = False
        self.enter_count = 0
        self.quiet_count = 0

    def observe(
        self,
        now_ms: int,
        *,
        acceleration_delta: float,
        angular_delta: float,
    ) -> bool:
        self._require_monotonic(now_ms)
        self.last_sample_ms = now_ms
        require(
            acceleration_delta >= 0 and angular_delta >= 0,
            "motion deltas must be magnitudes",
        )

        if now_ms < self.grace_until_ms:
            self.armed = False
            self.enter_count = 0
            self.quiet_count = 0
            return False

        above_enter = (
            acceleration_delta >= self.policy.acceleration_enter
            or angular_delta >= self.policy.angular_enter
        )
        below_quiet = (
            acceleration_delta <= self.policy.acceleration_quiet
            and angular_delta <= self.policy.angular_quiet
        )

        if not self.armed:
            self.enter_count = 0
            self.quiet_count = self.quiet_count + 1 if below_quiet else 0
            if self.quiet_count >= self.policy.quiet_samples:
                self.armed = True
                self.quiet_count = 0
            return False

        if (
            self.last_trigger_ms is not None
            and now_ms - self.last_trigger_ms < self.policy.cooldown_ms
        ):
            self.enter_count = 0
            return False

        self.enter_count = self.enter_count + 1 if above_enter else 0
        if self.enter_count < self.policy.enter_samples:
            return False

        self.armed = False
        self.enter_count = 0
        self.quiet_count = 0
        self.last_trigger_ms = now_ms
        return True

    def _require_monotonic(self, now_ms: int) -> None:
        require(
            self.last_sample_ms is None or now_ms >= self.last_sample_ms,
            "motion sample timestamps must be monotonic",
        )


@dataclass(frozen=True)
class PowerKeyEvents:
    short_presses: int
    long_presses: int


def publish_then_clear_pkey_irq(
    irq_status: int,
    publish: Callable[[PowerKeyEvents], int],
    clear: Callable[[], int],
) -> int:
    """Model the required PMIC ordering.

    A failed event publication leaves the IRQ latched so the event can be
    retried.  Clearing first would irreversibly lose the physical button event.
    """

    events = PowerKeyEvents(
        short_presses=1 if irq_status & PKEY_SHORT_IRQ else 0,
        long_presses=1 if irq_status & PKEY_LONG_IRQ else 0,
    )
    if events.short_presses or events.long_presses:
        error = publish(events)
        if error != ERR_OK:
            return error
    return clear()


def verify_inactivity_timeline() -> None:
    timeline = InactivityTimeline(
        InactivityPolicy(
            command_timeout_ms=10_000,
            status_timeout_ms=20_000,
            dim_timeout_ms=30_000,
            screen_sleep_delay_ms=40_000,
        )
    )
    expected = {
        0: IdleState(True, True, DisplayStage.ACTIVE),
        9_999: IdleState(True, True, DisplayStage.ACTIVE),
        10_000: IdleState(False, True, DisplayStage.ACTIVE),
        19_999: IdleState(False, True, DisplayStage.ACTIVE),
        20_000: IdleState(False, False, DisplayStage.ACTIVE),
        29_999: IdleState(False, False, DisplayStage.ACTIVE),
        30_000: IdleState(False, False, DisplayStage.DIMMED),
        69_999: IdleState(False, False, DisplayStage.DIMMED),
        70_000: IdleState(False, False, DisplayStage.OFF),
    }
    actual = {at_ms: timeline.state_at(at_ms) for at_ms in expected}
    require(actual == expected, f"inactivity boundary mismatch: {actual!r}")

    timeline.note_activity(70_000)
    require(
        timeline.state_at(70_000)
        == IdleState(True, True, DisplayStage.ACTIVE),
        "activity must synchronously restore chrome and active display",
    )
    require(
        timeline.state_at(80_000)
        == IdleState(False, True, DisplayStage.ACTIVE),
        "all inactivity boundaries must restart from real activity",
    )

    disabled = InactivityTimeline(InactivityPolicy(0, 0, 0, 0))
    require(
        disabled.state_at(10_000_000)
        == IdleState(True, True, DisplayStage.ACTIVE),
        "zero must keep each inactivity feature disabled",
    )
    no_panel_off = InactivityTimeline(
        InactivityPolicy(0, 0, 1_000, 0)
    )
    require(
        no_panel_off.state_at(10_000) == IdleState(True, True, DisplayStage.DIMMED),
        "zero sleep delay must disable OFF without disabling DIMMED",
    )


def verify_dim_brightness_round_trip() -> None:
    configured_percent = 100
    dimmed_percent = configured_percent * 50 // 100
    controller_value = dimmed_percent * 255 // 100
    reported_percent = (controller_value * 100 + 127) // 255
    require(dimmed_percent == 50, "100% configured brightness must dim to 50%")
    require(
        reported_percent == 50,
        "controller quantization must not report a requested 50% as 49%",
    )


def _arm_motion_filter(filter_: QualifiedMotionFilter, start_ms: int) -> int:
    now_ms = start_ms
    for _ in range(filter_.policy.quiet_samples):
        require(
            not filter_.observe(
                now_ms,
                acceleration_delta=0.05,
                angular_delta=0.20,
            ),
            "quiet dwell must not publish activity",
        )
        now_ms += 100
    require(filter_.armed, "quiet dwell must arm motion detection")
    return now_ms


def verify_motion_filter() -> None:
    filter_ = QualifiedMotionFilter(MotionPolicy())
    now_ms = _arm_motion_filter(filter_, 0)

    require(
        not filter_.observe(
            now_ms,
            acceleration_delta=0.10,
            angular_delta=4.50,
        ),
        "one gyro spike must not publish activity",
    )
    now_ms += 100
    require(
        not filter_.observe(
            now_ms,
            acceleration_delta=0.10,
            angular_delta=0.30,
        ),
        "quiet after one spike must not publish activity",
    )

    trigger_results: list[bool] = []
    for _ in range(filter_.policy.enter_samples):
        now_ms += 100
        trigger_results.append(
            filter_.observe(
                now_ms,
                acceleration_delta=0.90,
                angular_delta=0.50,
            )
        )
    require(
        trigger_results == [False, False, True],
        f"qualified motion must publish one event: {trigger_results!r}",
    )
    first_trigger_ms = now_ms

    for _ in range(12):
        now_ms += 100
        require(
            not filter_.observe(
                now_ms,
                acceleration_delta=1.20,
                angular_delta=5.0,
            ),
            "sustained motion must not continuously reset inactivity",
        )

    now_ms = _arm_motion_filter(filter_, now_ms + 100)
    while now_ms - first_trigger_ms < filter_.policy.cooldown_ms:
        require(
            not filter_.observe(
                now_ms,
                acceleration_delta=1.0,
                angular_delta=5.0,
            ),
            "re-armed motion inside cooldown must remain suppressed",
        )
        now_ms += 100

    cooldown_results: list[bool] = []
    for _ in range(filter_.policy.enter_samples):
        cooldown_results.append(
            filter_.observe(
                now_ms,
                acceleration_delta=1.0,
                angular_delta=5.0,
            )
        )
        now_ms += 100
    require(
        cooldown_results == [False, False, True],
        f"motion may trigger again after cooldown: {cooldown_results!r}",
    )

    filter_.begin_manual_standby(now_ms)
    for _ in range(10):
        now_ms += 100
        require(
            not filter_.observe(
                now_ms,
                acceleration_delta=1.0,
                angular_delta=5.0,
            ),
            "motion during manual standby grace must not wake the device",
        )
    require(not filter_.armed, "grace-period motion must not pre-arm wake")

    now_ms = filter_.grace_until_ms
    now_ms = _arm_motion_filter(filter_, now_ms)
    wake_results: list[bool] = []
    for _ in range(filter_.policy.enter_samples):
        wake_results.append(
            filter_.observe(
                now_ms,
                acceleration_delta=1.0,
                angular_delta=5.0,
            )
        )
        now_ms += 100
    require(
        wake_results == [False, False, True],
        f"qualified motion after grace may wake once: {wake_results!r}",
    )


def verify_motion_does_not_starve_inactivity() -> None:
    timeline = InactivityTimeline(InactivityPolicy(10_000, 20_000, 30_000, 40_000))
    filter_ = QualifiedMotionFilter(MotionPolicy())
    _arm_motion_filter(filter_, 0)

    activity_at_ms = 9_000
    results = [
        filter_.observe(
            activity_at_ms - 200 + offset * 100,
            acceleration_delta=1.0,
            angular_delta=5.0,
        )
        for offset in range(filter_.policy.enter_samples)
    ]
    require(results == [False, False, True], "qualified motion setup failed")
    timeline.note_activity(activity_at_ms)

    for now_ms in range(activity_at_ms + 100, activity_at_ms + 5_100, 100):
        require(
            not filter_.observe(
                now_ms,
                acceleration_delta=1.0,
                angular_delta=5.0,
            ),
            "sustained movement must publish at most one activity event",
        )
    require(
        timeline.state_at(activity_at_ms + 9_999).command_visible,
        "qualified motion must restart the command timeout",
    )
    require(
        not timeline.state_at(activity_at_ms + 10_000).command_visible,
        "sustained motion must not postpone the command timeout forever",
    )


def verify_pkey_delivery_order() -> None:
    operations: list[str] = []

    def publish(events: PowerKeyEvents) -> int:
        operations.append(
            f"publish:{events.short_presses}:{events.long_presses}"
        )
        return ERR_OK

    def clear() -> int:
        operations.append("clear")
        return ERR_OK

    result = publish_then_clear_pkey_irq(
        PKEY_SHORT_IRQ | PKEY_LONG_IRQ,
        publish,
        clear,
    )
    require(result == ERR_OK, "successful PKEY delivery must succeed")
    require(
        operations == ["publish:1:1", "clear"],
        f"PKEY events must be published before clear: {operations!r}",
    )

    operations.clear()

    def failed_publish(events: PowerKeyEvents) -> int:
        operations.append(
            f"publish:{events.short_presses}:{events.long_presses}"
        )
        return ERR_DELIVERY_FAILED

    result = publish_then_clear_pkey_irq(PKEY_SHORT_IRQ, failed_publish, clear)
    require(
        result == ERR_DELIVERY_FAILED,
        "PKEY publication failure must remain visible",
    )
    require(
        operations == ["publish:1:0"],
        "failed PKEY publication must not clear and lose the IRQ",
    )


def verify_source_contracts() -> None:
    power = read(POWER_SOURCE)
    hardware = read(HARDWARE_SOURCE)
    header = read(PLATFORM_HEADER)
    display = read(DISPLAY_SOURCE)
    bsp_display = read(BSP_DISPLAY_SOURCE)

    require(
        "GPIO_NUM_41" not in power,
        "qsee_power must not treat SDMMC_CS GPIO41 as the physical PWR key",
    )
    require(
        "gpio_get_level(kPowerButton)" not in power,
        "qsee_power must consume PMIC PKEY events instead of polling a GPIO",
    )
    require_regex(
        header,
        r"qsee_hardware_take_power_key_events\s*\(\s*"
        r"uint32_t\s*\*\s*short_presses\s*,\s*"
        r"uint32_t\s*\*\s*long_presses\s*\)",
        "qsee_platform.h",
    )
    require_contains(power, "qsee_hardware_take_power_key_events(", "qsee_power.cpp")
    require_regex(
        power,
        r"(?:esp_err_t|auto)\s+\w+\s*="
        r"\s*qsee_hardware_take_power_key_events\(",
        "checked PKEY consumption",
    )

    sample_pmu = function_text(
        hardware,
        "esp_err_t sample_pmu(",
        "qsee_hardware.cpp",
    )
    require(
        "read_pmu_irq_status(" not in sample_pmu
        and "clear_and_verify_pmu_irq_status(" not in sample_pmu
        and "XPOWERS_AXP2101_PKEY_" not in sample_pmu,
        "sample_pmu must remain telemetry-only and must not own PMIC IRQs",
    )

    irq_reader = function_text(
        hardware,
        "esp_err_t read_pmu_irq_status(",
        "qsee_hardware.cpp",
    )
    require_regex(
        irq_reader,
        r"read_pmu_registers\s*\(\s*XPOWERS_AXP2101_INTSTS1\s*,"
        r"\s*status_bytes\s*,\s*sizeof\s*\(\s*status_bytes\s*\)\s*\)",
        "single-burst PMIC IRQ status read",
    )
    require(
        "for (" not in irq_reader,
        "PMIC IRQ polling must not split INTSTS1..3 into repeated transactions",
    )
    irq_mask_verifier = function_text(
        hardware,
        "esp_err_t verify_pmu_irq_enable_mask(",
        "qsee_hardware.cpp",
    )
    for fragment in (
        "XPOWERS_AXP2101_INTEN1",
        "sizeof(enabled_bytes)",
        "observed != expected",
        "ESP_ERR_INVALID_RESPONSE",
    ):
        require_contains(
            irq_mask_verifier,
            fragment,
            "PMIC IRQ enable readback",
        )
    init_pmu = function_text(
        hardware,
        "esp_err_t init_pmu(",
        "qsee_hardware.cpp",
    )
    require_regex(
        init_pmu,
        r"s_power\.begin\s*\(\s*AXP2101_ADDRESS\s*,\s*"
        r"xpowers_read_register\s*,\s*xpowers_write_register\s*\)",
        "bounded XPowers callback initialization",
    )
    runtime_pmu_reader = function_text(
        hardware,
        "esp_err_t read_pmu_registers(",
        "qsee_hardware.cpp",
    )
    require_contains(
        runtime_pmu_reader,
        "read_register(s_pmu_device, reg, data, length)",
        "bounded runtime PMU read",
    )
    require(
        "s_power.readRegister(" not in runtime_pmu_reader,
        "runtime PMU reads must not use XPowers' unbounded IDF transaction",
    )
    irq_clearer = function_text(
        hardware,
        "esp_err_t clear_and_verify_pmu_irq_status(",
        "qsee_hardware.cpp",
    )
    require_contains(
        irq_clearer,
        "write_register(s_pmu_device, reg, value)",
        "bounded runtime PMU write",
    )
    require(
        "s_power.writeRegister(" not in irq_clearer,
        "runtime PMU clears must not use XPowers' unbounded IDF transaction",
    )
    enable_at = init_pmu.find("s_power.enableIRQ(kPmuRuntimeIrqMask)")
    verify_at = init_pmu.find("verify_pmu_irq_enable_mask(kPmuRuntimeIrqMask)")
    require(
        0 <= enable_at < verify_at,
        "PMIC initialization must verify the runtime IRQ mask after enabling it",
    )

    collector = function_text(
        hardware,
        "esp_err_t collect_pmu_irq_events(",
        "qsee_hardware.cpp",
    )
    read_at = collector.find("read_pmu_irq_status(")
    clear_at = collector.find("clear_and_verify_pmu_irq_status(")
    require(
        0 <= read_at < clear_at,
        "the dedicated PMIC collector must read before clearing IRQ status",
    )
    short_at = collector.find("XPOWERS_AXP2101_PKEY_SHORT_IRQ")
    long_at = collector.find("XPOWERS_AXP2101_PKEY_LONG_IRQ")
    require(
        read_at < short_at < clear_at and read_at < long_at < clear_at,
        "the dedicated PMIC collector must decode PKEY before IRQ clear",
    )
    queued_before_clear = collector[:clear_at]
    require_regex(
        queued_before_clear,
        r"\b[A-Za-z0-9_]*short[A-Za-z0-9_]*\.(?:fetch_add|fetch_or)\(",
        "short PKEY event queue before clear",
    )
    require_regex(
        queued_before_clear,
        r"\b[A-Za-z0-9_]*long[A-Za-z0-9_]*\.(?:fetch_add|fetch_or)\(",
        "long PKEY event queue before clear",
    )
    short_queue_at = require_regex(
        queued_before_clear,
        r"\b[A-Za-z0-9_]*short[A-Za-z0-9_]*\.(?:fetch_add|fetch_or)\(",
        "short PKEY event queue ordering",
    ).start()
    long_queue_at = require_regex(
        queued_before_clear,
        r"\b[A-Za-z0-9_]*long[A-Za-z0-9_]*\.(?:fetch_add|fetch_or)\(",
        "long PKEY event queue ordering",
    ).start()
    dedup_at = collector.find("s_pmu_queued_pkey_status |= pkey_status")
    require(
        max(short_queue_at, long_queue_at) < dedup_at < clear_at,
        "PKEY counts and de-duplication state must be queued before IRQ clear",
    )

    take_function = function_text(
        hardware,
        "qsee_hardware_take_power_key_events(",
        "qsee_hardware.cpp",
    )
    require_contains(
        take_function,
        "ensure_pmu_mutex()",
        "dedicated PMU lock for PKEY polling",
    )
    require(
        "ensure_refresh_mutex()" not in take_function,
        "PKEY polling must not depend on the full hardware refresh mutex",
    )
    collect_at = take_function.find("collect_pmu_irq_events()")
    first_exchange_at = take_function.find(".exchange(")
    require(
        0 <= collect_at < first_exchange_at,
        "the PKEY take API must poll the PMIC before consuming queued counts",
    )
    require(
        len(re.findall(r"\.exchange\(\s*0", take_function)) >= 2,
        "taking PKEY counters must atomically consume both queued counts",
    )
    require_contains(
        take_function,
        "if (collect_error == ESP_OK)",
        "successful-collection guard around PKEY counter consumption",
    )

    power_task = function_text(
        power,
        "void power_task(",
        "qsee_power.cpp",
    )
    take_at = power_task.find("qsee_hardware_take_power_key_events(")
    handle_at = power_task.find("handle_power_key_events(")
    require(
        0 <= take_at < handle_at,
        "the power worker must poll PMIC PKEY events before handling them",
    )
    require_contains(
        power_task,
        "if (pkey_error == ESP_OK)",
        "PKEY poll error propagation",
    )

    idle_policy = function_text(
        power,
        "esp_err_t apply_idle_policy(",
        "qsee_power.cpp",
    )
    for fragment in (
        "s_command_timeout_seconds.load(",
        "s_status_timeout_seconds.load(",
        "s_dim_timeout_seconds.load(",
        "s_screen_sleep_delay_seconds.load(",
        "dim_seconds + screen_sleep_delay",
        "DisplayPowerMode::Dimmed",
        "DisplayPowerMode::DisplayOff",
    ):
        require_contains(fragment=fragment, text=idle_policy, source="idle policy")

    require_regex(
        power,
        r"motion[A-Za-z0-9_]*(?:enter|trigger)[A-Za-z0-9_]*"
        r"(?:sample|count)|(?:enter|trigger)[A-Za-z0-9_]*"
        r"(?:sample|count)[A-Za-z0-9_]*motion",
        "consecutive motion-enter samples",
    )
    require_regex(
        power,
        r"motion[A-Za-z0-9_]*(?:quiet|release)[A-Za-z0-9_]*"
        r"(?:sample|count)|(?:quiet|release)[A-Za-z0-9_]*"
        r"(?:sample|count)[A-Za-z0-9_]*motion",
        "quiet dwell before motion re-arm",
    )
    require_regex(
        power,
        r"motion[A-Za-z0-9_]*cooldown|cooldown[A-Za-z0-9_]*motion",
        "explicit motion cooldown",
    )
    require_regex(
        power,
        r"motion[A-Za-z0-9_]*grace|grace[A-Za-z0-9_]*motion",
        "manual-standby motion grace",
    )
    motion_filter = function_text(
        power,
        "bool qualified_motion_detected(",
        "qsee_power.cpp",
    )
    for fragment in (
        "now_us < s_motion_grace_until_us",
        "s_motion_quiet_sample_count >= kMotionQuietSampleCount",
        "now_us < s_motion_cooldown_until_us",
        "s_motion_enter_sample_count < kMotionEnterSampleCount",
        "s_motion_armed = false",
    ):
        require_contains(
            text=motion_filter,
            fragment=fragment,
            source="qualified motion filter",
        )
    require_regex(
        power_task,
        r"if\s*\(\s*s_motion_monitor_started\s*&&\s*"
        r"qualified_motion_detected\s*\(\s*snapshot\.imu\s*\)\s*\)",
        "qualified-motion power integration",
    )

    require_regex(
        display,
        r"bsp_display_panel_set_enabled\s*\(\s*false\s*\)",
        "DisplayOff transition",
    )
    require_regex(
        display,
        r"bsp_display_panel_set_enabled\s*\(\s*true\s*\)",
        "Active/Dimmed transition",
    )
    display_transition = function_text(
        display,
        "esp_err_t transition_power_mode_locked(",
        "qsee_display_settings.cpp",
    )
    panel_enable_at = require_regex(
        display_transition,
        r"bsp_display_panel_set_enabled\s*\(\s*true\s*\)",
        "Active/Dimmed transition order",
    ).start()
    fade_loop_at = require_regex(
        display_transition,
        r"for\s*\(\s*uint32_t\s+step",
        "display fade loop",
    ).start()
    panel_disable_at = require_regex(
        display_transition,
        r"bsp_display_panel_set_enabled\s*\(\s*false\s*\)",
        "DisplayOff transition order",
    ).start()
    display_publish_matches = list(
        re.finditer(
            r"reportDisplayPowerMode\s*\(",
            display_transition,
            flags=re.IGNORECASE,
        )
    )
    require(
        len(display_publish_matches) >= 2,
        "display transition must publish both failures and final success",
    )
    display_publish_at = display_publish_matches[-1].start()
    require(
        0 <= panel_enable_at < fade_loop_at,
        "Active/Dimmed must enable the panel before changing brightness",
    )
    require(
        fade_loop_at < panel_disable_at < display_publish_at,
        "DisplayOff must fade to zero, disable the panel, then publish OFF",
    )
    require_contains(
        display,
        "kDimmedBrightnessNumerator = 50",
        "50-percent dim policy",
    )
    require_contains(
        display,
        "kDimmedBrightnessDenominator = 100",
        "50-percent dim policy",
    )
    require_regex(
        bsp_display,
        r"return\s*\(\s*brightness\s*\*\s*100\s*\+\s*127\s*\)"
        r"\s*/\s*255\s*;",
        "rounded BSP brightness readback",
    )
    brightness_set = function_text(
        bsp_display,
        "esp_err_t bsp_display_brightness_set(",
        "BSP display brightness",
    )
    panel_set = function_text(
        bsp_display,
        "esp_err_t bsp_display_panel_set_enabled(",
        "BSP panel power",
    )
    rotation_set = function_text(
        bsp_display,
        "esp_err_t bsp_display_rotation_set(",
        "BSP display rotation",
    )
    for body, source in (
        (brightness_set, "BSP display brightness"),
        (panel_set, "BSP panel power"),
        (rotation_set, "BSP display rotation"),
    ):
        lock_at = body.find("bsp_display_lock(")
        unlock_at = body.find("bsp_display_unlock(")
        require(
            0 <= lock_at < unlock_at,
            f"{source} must share the display lock",
        )
    require_contains(
        panel_set,
        "display_panel_output_enabled = enabled",
        "panel power state tracking",
    )
    require_contains(
        rotation_set,
        "if (!display_panel_output_enabled)",
        "deferred rotation while the panel is off",
    )
    fade_api = function_text(
        display,
        "qsee_display_fade_to_power_mode(",
        "display fade API",
    )
    fade_display_lock_at = fade_api.find("DisplayOperationLock")
    fade_brightness_lock_at = fade_api.find(
        "std::lock_guard<std::mutex> lock(s_brightness_mutex)"
    )
    require(
        0 <= fade_display_lock_at < fade_brightness_lock_at,
        "display transitions must acquire LVGL before the brightness mutex",
    )
    standby_transition = function_text(
        power,
        "esp_err_t set_standby(",
        "standby transition",
    )
    require_contains(
        standby_transition,
        "Display rollback after standby failure failed",
        "standby display rollback",
    )
    require_contains(
        standby_transition,
        "Display rollback after wake failure failed",
        "wake display rollback",
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run deterministic power-policy and static source checks."
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--model-only",
        action="store_true",
        help="run only the hardware-free executable specification",
    )
    mode.add_argument(
        "--source-only",
        action="store_true",
        help="run only static integration contracts",
    )
    args = parser.parse_args()

    if not args.source_only:
        verify_inactivity_timeline()
        verify_dim_brightness_round_trip()
        verify_motion_filter()
        verify_motion_does_not_starve_inactivity()
        verify_pkey_delivery_order()
    if not args.model_only:
        verify_source_contracts()

    checked = (
        "source contracts"
        if args.source_only
        else "behavioral model"
        if args.model_only
        else "behavioral model and source contracts"
    )
    print(
        "power policy verification: PASS "
        f"({checked}: inactivity, 50% dim, qualified motion, "
        "bounded PKEY isolation, serialized panel-off)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
