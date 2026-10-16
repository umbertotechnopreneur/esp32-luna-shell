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

"""Deterministic, hardware-free verification for the power policy.

The behavioral models below are executable specifications.  They deliberately
do not import firmware code, so boundary and event-order regressions remain
visible even when the implementation changes.  The source-contract checks tie
those specifications to the ESP-IDF implementation without requiring a build.
"""

from __future__ import annotations

from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common.source_contracts import read_component_source

# Direct command execution resolves the shared repository package explicitly.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import argparse
import json
from dataclasses import dataclass
from enum import Enum, auto
import re
from typing import Callable

from scripts.common.firmware_profile import read_profile_defaults
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage


ROOT = Path(__file__).resolve().parents[2]
POWER_SOURCE = ROOT / "components" / "LUNASHELL_platform" / "src" / "power" / "LUNASHELL_power.cpp"
HARDWARE_SOURCE = ROOT / "components" / "LUNASHELL_platform" / "src" / "snapshot" / "LUNASHELL_hardware.cpp"
PLATFORM_HEADER = (
    ROOT / "components" / "LUNASHELL_platform" / "include" / "LUNASHELL_platform.h"
)
DEVICE_SNAPSHOT_HEADER = (
    ROOT
    / "components"
    / "LUNASHELL_platform"
    / "include"
    / "LUNASHELL_device_snapshot.hpp"
)
HTTP_JSON_SOURCE = ROOT / "components" / "LUNASHELL_platform" / "src" / "http" / "LUNASHELL_http_json.cpp"
DISPLAY_SOURCE = (
    ROOT / "components" / "LUNASHELL_platform" / "src" / "display" / "LUNASHELL_display_settings.cpp"
)
BSP_DISPLAY_SOURCE = (
    ROOT
    / "components"
    / "esp32_s3_touch_amoled_1_75"
    / "esp32_s3_touch_amoled_1_75.c"
)
BSP_DISPLAY_HEADER = (
    ROOT
    / "components"
    / "esp32_s3_touch_amoled_1_75"
    / "include"
    / "bsp"
    / "display.h"
)
BSP_DISPLAY_CMAKE = (
    ROOT / "components" / "esp32_s3_touch_amoled_1_75" / "CMakeLists.txt"
)
ADAPTER_PAUSE_PATCH = (
    ROOT
    / "components"
    / "esp32_s3_touch_amoled_1_75"
    / "patches"
    / "esp_lvgl_adapter_pause_recovery.patch"
)
APP_SOURCE = ROOT / "components" / "LUNASHELL_app" / "src" / "runtime" / "LUNASHELL_app.cpp"
APP_HEADER = ROOT / "components" / "LUNASHELL_app" / "include" / "LUNASHELL_app.hpp"
SETTINGS_SCREEN = (
    ROOT / "components" / "LUNASHELL_app" / "package" / "res" / "screen_settings.json"
)
SETTINGS_SWITCH = (
    ROOT
    / "components"
    / "LUNASHELL_app"
    / "package"
    / "res"
    / "templates"
    / "LUNASHELL_settings_switch.json"
)
ADAPTIVE_CHROME_SOURCE = (
    ROOT / "components" / "LUNASHELL_app" / "src" / "chrome" / "LUNASHELL_adaptive_chrome.cpp"
)
ADAPTIVE_COMMAND_SOURCE = (
    ROOT / "components" / "LUNASHELL_app" / "src" / "chrome" / "LUNASHELL_adaptive_command_bar.cpp"
)
BROOKESIA_GUI_SOURCE = (
    ROOT
    / "third_party"
    / "esp-brookesia"
    / "system"
    / "brookesia_system_core"
    / "src"
    / "system"
    / "gui.cpp"
)
BROOKESIA_GUI_RUNTIME_HEADER = (
    ROOT
    / "third_party"
    / "esp-brookesia"
    / "system"
    / "brookesia_system_core"
    / "include"
    / "brookesia"
    / "system_core"
    / "app"
    / "gui_runtime.hpp"
)
BROOKESIA_APP_MANAGER_SOURCE = (
    ROOT
    / "third_party"
    / "esp-brookesia"
    / "system"
    / "brookesia_system_core"
    / "src"
    / "app"
    / "manager.cpp"
)
BROOKESIA_TASK_SOURCE = (
    ROOT
    / "third_party"
    / "esp-brookesia"
    / "system"
    / "brookesia_system_core"
    / "src"
    / "system"
    / "task.cpp"
)
BROOKESIA_IMPL_HEADER = (
    ROOT
    / "third_party"
    / "esp-brookesia"
    / "system"
    / "brookesia_system_core"
    / "src"
    / "private"
    / "system"
    / "impl.hpp"
)
AUDIO_SOURCE = ROOT / "components" / "LUNASHELL_platform" / "src" / "audio" / "LUNASHELL_audio.cpp"
VOICE_AFE_SOURCE = (
    ROOT / "components" / "LUNASHELL_platform" / "src" / "audio" / "LUNASHELL_voice_afe.cpp"
)
LOGGER_SOURCE = ROOT / "components" / "LUNASHELL_logger" / "src" / "LUNASHELL_logger.c"
SNAPSHOT_SOURCE = (
    ROOT / "components" / "LUNASHELL_platform" / "src" / "snapshot" / "LUNASHELL_snapshot_scheduler.cpp"
)
DISTANCE_SOURCE = (
    ROOT / "components" / "LUNASHELL_platform" / "src" / "distance" / "LUNASHELL_distance_service.cpp"
)
DISTANCE_STUB_SOURCE = (
    ROOT / "components" / "LUNASHELL_platform" / "src" / "distance" / "LUNASHELL_distance_service_stub.cpp"
)
STORAGE_SOURCE = ROOT / "components" / "LUNASHELL_platform" / "src" / "storage" / "LUNASHELL_storage.cpp"
PLATFORM_CMAKE = ROOT / "components" / "LUNASHELL_platform" / "CMakeLists.txt"

PKEY_SHORT_IRQ = 1 << 0
PKEY_LONG_IRQ = 1 << 1
ERR_OK = 0
ERR_DELIVERY_FAILED = 1
ERR_TRANSITION_FAILED = 2
ERR_INCONSISTENT_STATE = 3
ERR_STORAGE_FAILED = 4


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# path: Requested source; split implementation families are read together.
# Raises OSError or UnicodeError when a required source is unavailable.
def read(path: Path) -> str:
    return read_component_source(path)


# Reconstruct the post-patch side of a unified diff for source-order checks.
def patch_new_side(text: str) -> str:
    result: list[str] = []
    for line in text.splitlines():
        if line.startswith("@@"):
            trailing_context = line.rsplit("@@", 1)[-1].strip()
            if trailing_context:
                result.append(trailing_context)
        elif line.startswith(" "):
            result.append(line[1:])
        elif line.startswith("+") and not line.startswith("+++ "):
            result.append(line[1:])
    return "\n".join(result)


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
    screen_idle_timeout_ms: int

    def __post_init__(self) -> None:
        require(
            min(
                self.command_timeout_ms,
                self.status_timeout_ms,
                self.dim_timeout_ms,
                self.screen_idle_timeout_ms,
            )
            >= 0,
            "timeouts must not be negative",
        )


@dataclass(frozen=True)
class IdleState:
    command_visible: bool
    status_visible: bool
    display: DisplayStage
    standby: bool = False


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

        standby_due = expired(self.policy.screen_idle_timeout_ms)
        display = (
            DisplayStage.OFF
            if standby_due
            else DisplayStage.DIMMED
            if expired(self.policy.dim_timeout_ms)
            else DisplayStage.ACTIVE
        )

        return IdleState(
            command_visible=not expired(self.policy.command_timeout_ms),
            status_visible=not expired(self.policy.status_timeout_ms),
            display=display,
            standby=standby_due,
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


class TransactionState(Enum):
    ACTIVE = auto()
    ENTERING_STANDBY = auto()
    STANDBY = auto()
    WAKING = auto()


@dataclass
class FaultInjectedResource:
    name: str
    applied: bool = False
    consistent: bool = True
    outcomes: list[int] | None = None
    calls: list[bool] | None = None

    # Give each modeled resource independent fault and call histories.
    def __post_init__(self) -> None:
        self.outcomes = [] if self.outcomes is None else self.outcomes
        self.calls = [] if self.calls is None else self.calls

    # Queue deterministic outcomes for the next physical transition attempts.
    def inject(self, *outcomes: int) -> None:
        self.outcomes.extend(outcomes)

    # Mark a failed transition unknown so compensation cannot skip it.
    def transition(self, target: bool) -> int:
        if self.consistent and self.applied == target:
            return ERR_OK
        self.calls.append(target)
        error = self.outcomes.pop(0) if self.outcomes else ERR_OK
        if error != ERR_OK:
            # A failed operation cannot prove that its nested rollback worked.
            self.consistent = False
            return error
        self.applied = target
        self.consistent = True
        return ERR_OK


class StandbyTransactionModel:
    """Executable specification for fail-fast standby compensation."""

    # Initialize a coherent active device with four representative resources.
    def __init__(self) -> None:
        self.resources = {
            name: FaultInjectedResource(name)
            for name in ("producer", "storage", "audio", "display")
        }
        self.state = TransactionState.ACTIVE
        self.published_error = ERR_OK

    # Seed a coherent standby state for wake-path fault injection.
    def assume_standby(self) -> None:
        for resource in self.resources.values():
            resource.applied = True
            resource.consistent = True
        self.state = TransactionState.STANDBY

    # Verify a terminal state while permitting only documented SD degradation.
    def _stable(self, target: bool, allow_storage_degraded: bool) -> bool:
        return all(
            (name == "storage" and allow_storage_degraded)
            or (resource.consistent and resource.applied == target)
            for name, resource in self.resources.items()
        )

    # Model ordered suspension and mandatory terminal-state validation.
    def _suspend(self, best_effort: bool) -> int:
        first_error = ERR_OK
        for name in ("producer", "storage", "audio"):
            if not best_effort and first_error != ERR_OK:
                break
            error = self.resources[name].transition(True)
            if first_error == ERR_OK and error != ERR_OK:
                first_error = error
        if best_effort or first_error == ERR_OK:
            error = self.resources["display"].transition(True)
            if first_error == ERR_OK and error != ERR_OK:
                first_error = error
        if not self._stable(True, allow_storage_degraded=False):
            first_error = first_error or ERR_INCONSISTENT_STATE
        return first_error

    # Model ordered resume while keeping optional SD errors separately visible.
    def _resume(self, best_effort: bool) -> tuple[int, int]:
        first_error = ERR_OK
        storage_error = ERR_OK
        for name in ("audio", "storage", "producer"):
            if not best_effort and first_error != ERR_OK:
                break
            error = self.resources[name].transition(False)
            if name == "storage" and error != ERR_OK:
                storage_error = error
            elif first_error == ERR_OK and error != ERR_OK:
                first_error = error
        # The active panel is optional-storage tolerant, but never exposed
        # after an inconsistent mandatory producer.
        if first_error == ERR_OK:
            error = self.resources["display"].transition(False)
            if error != ERR_OK:
                first_error = error
        if not self._stable(
            False,
            allow_storage_degraded=storage_error != ERR_OK,
        ):
            first_error = first_error or ERR_INCONSISTENT_STATE
        return first_error, storage_error

    # Enter standby or execute active/standby compensation after a failure.
    def enter(self) -> int:
        self.state = TransactionState.ENTERING_STANDBY
        transition_error = self._suspend(best_effort=False)
        if transition_error == ERR_OK:
            self.state = TransactionState.STANDBY
            self.published_error = ERR_OK
            return ERR_OK

        rollback_error, storage_error = self._resume(best_effort=True)
        if rollback_error == ERR_OK:
            self.state = TransactionState.ACTIVE
            self.published_error = storage_error or transition_error
            return transition_error
        recovery_error = self._suspend(best_effort=True)
        if recovery_error == ERR_OK:
            self.state = TransactionState.STANDBY
        self.published_error = transition_error
        return transition_error

    # Wake or compensate without exposing the panel after mandatory failure.
    def wake(self) -> int:
        self.state = TransactionState.WAKING
        transition_error, storage_error = self._resume(best_effort=False)
        if transition_error == ERR_OK:
            self.state = TransactionState.ACTIVE
            self.published_error = storage_error
            return storage_error

        rollback_error = self._suspend(best_effort=True)
        if rollback_error == ERR_OK:
            self.state = TransactionState.STANDBY
            self.published_error = transition_error
            return transition_error
        recovery_error, recovery_storage_error = self._resume(best_effort=True)
        if recovery_error == ERR_OK:
            self.state = TransactionState.ACTIVE
        self.published_error = recovery_storage_error or transition_error
        return transition_error


def verify_inactivity_timeline() -> None:
    timeline = InactivityTimeline(
        InactivityPolicy(
            command_timeout_ms=10_000,
            status_timeout_ms=20_000,
            dim_timeout_ms=30_000,
            screen_idle_timeout_ms=70_000,
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
        70_000: IdleState(False, False, DisplayStage.OFF, True),
    }
    actual = {at_ms: timeline.state_at(at_ms) for at_ms in expected}
    require(actual == expected, f"inactivity boundary mismatch: {actual!r}")

    timeline.note_activity(70_000)
    require(
        timeline.state_at(70_000)
        == IdleState(True, True, DisplayStage.ACTIVE),
        "successful wake/activity must synchronously restore active state",
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
        "zero screen inactivity must disable standby without disabling DIMMED",
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


def verify_transaction_fault_injection() -> None:
    entry_recovery = StandbyTransactionModel()
    entry_recovery.resources["producer"].inject(
        ERR_TRANSITION_FAILED,
        ERR_OK,
    )
    require(
        entry_recovery.enter() == ERR_TRANSITION_FAILED,
        "entry must preserve the original transition error",
    )
    require(
        entry_recovery.state == TransactionState.ACTIVE,
        "successful entry compensation must restore active state",
    )
    require(
        entry_recovery.resources["producer"].calls == [True, False],
        "unknown entry resource must be actively restored, not skipped",
    )

    entry_double_failure = StandbyTransactionModel()
    entry_double_failure.resources["producer"].inject(
        ERR_TRANSITION_FAILED,
        ERR_TRANSITION_FAILED,
        ERR_TRANSITION_FAILED,
    )
    entry_double_failure.enter()
    require(
        entry_double_failure.state == TransactionState.ENTERING_STANDBY,
        "double-failed entry recovery must remain visibly incomplete",
    )
    require(
        entry_double_failure.resources["producer"].calls
        == [True, False, True],
        "entry recovery must re-assert both compensation targets",
    )

    entry_storage_degraded = StandbyTransactionModel()
    entry_storage_degraded.resources["storage"].inject(
        ERR_OK,
        ERR_STORAGE_FAILED,
    )
    entry_storage_degraded.resources["audio"].inject(
        ERR_TRANSITION_FAILED,
        ERR_OK,
    )
    entry_storage_degraded.enter()
    require(
        entry_storage_degraded.state == TransactionState.ACTIVE,
        "entry compensation must restore the panel despite optional SD failure",
    )
    require(
        entry_storage_degraded.published_error == ERR_STORAGE_FAILED,
        "entry compensation must publish its optional SD recovery failure",
    )
    require(
        not entry_storage_degraded.resources["display"].applied,
        "entry compensation with degraded SD must leave the panel active",
    )

    wake_recovery = StandbyTransactionModel()
    wake_recovery.assume_standby()
    wake_recovery.resources["audio"].inject(
        ERR_TRANSITION_FAILED,
        ERR_OK,
    )
    require(
        wake_recovery.wake() == ERR_TRANSITION_FAILED,
        "wake rollback must preserve the original error",
    )
    require(
        wake_recovery.state == TransactionState.STANDBY,
        "successful wake compensation must restore standby",
    )
    require(
        wake_recovery.resources["audio"].calls == [False, True],
        "unknown wake resource must be actively re-suspended",
    )

    wake_double_failure = StandbyTransactionModel()
    wake_double_failure.assume_standby()
    wake_double_failure.resources["audio"].inject(
        ERR_TRANSITION_FAILED,
        ERR_TRANSITION_FAILED,
        ERR_TRANSITION_FAILED,
    )
    wake_double_failure.wake()
    require(
        wake_double_failure.state == TransactionState.WAKING,
        "double-failed wake recovery must remain visibly incomplete",
    )
    require(
        wake_double_failure.resources["audio"].calls
        == [False, True, False],
        "wake recovery must retry an unknown resource in both directions",
    )

    wake_storage_degraded = StandbyTransactionModel()
    wake_storage_degraded.assume_standby()
    wake_storage_degraded.resources["audio"].inject(
        ERR_TRANSITION_FAILED,
        ERR_TRANSITION_FAILED,
        ERR_OK,
    )
    wake_storage_degraded.resources["storage"].inject(
        ERR_STORAGE_FAILED,
    )
    wake_storage_degraded.wake()
    require(
        wake_storage_degraded.state == TransactionState.ACTIVE,
        "active recovery must tolerate optional SD failure after rollback",
    )
    require(
        wake_storage_degraded.published_error == ERR_STORAGE_FAILED,
        "active recovery must publish its optional SD failure",
    )
    require(
        not wake_storage_degraded.resources["display"].applied,
        "active recovery with degraded SD must expose the panel",
    )

    optional_storage = StandbyTransactionModel()
    optional_storage.assume_standby()
    optional_storage.resources["storage"].inject(ERR_STORAGE_FAILED)
    require(
        optional_storage.wake() == ERR_STORAGE_FAILED,
        "optional SD wake failure must remain visible to the caller",
    )
    require(
        optional_storage.state == TransactionState.ACTIVE,
        "optional SD failure must not leave the device or panel in standby",
    )
    require(
        optional_storage.resources["display"].consistent
        and not optional_storage.resources["display"].applied,
        "optional SD failure must still expose a coherent active panel",
    )
    require(
        optional_storage.published_error == ERR_STORAGE_FAILED,
        "optional SD recovery error must be published explicitly",
    )


# No parameters: tie the executable model to every firmware and scheduler lifecycle invariant.
# Raises AssertionError, OSError or ValueError when a required contract is missing or invalid.
def verify_source_contracts() -> None:
    power = read(POWER_SOURCE)
    shared_standby = read(ROOT / "components/LUNASHELL_platform/include/LUNASHELL_standby_transition.hpp")
    hardware = read(HARDWARE_SOURCE)
    pmu_owner = read(HARDWARE_SOURCE.parent / "LUNASHELL_acquisition_pmu.cpp")
    rtc_owner = read(HARDWARE_SOURCE.parent / "LUNASHELL_acquisition_rtc.cpp")
    gnss_owner = read(HARDWARE_SOURCE.parent / "LUNASHELL_acquisition_gnss.cpp")
    imu_owner = read(HARDWARE_SOURCE.parent / "LUNASHELL_acquisition_imu.cpp")
    header = read(PLATFORM_HEADER)
    device_snapshot = read(DEVICE_SNAPSHOT_HEADER)
    http_json = read(HTTP_JSON_SOURCE)
    display = read(DISPLAY_SOURCE)
    bsp_display = read(BSP_DISPLAY_SOURCE)
    bsp_display_header = read(BSP_DISPLAY_HEADER)
    bsp_display_cmake = read(BSP_DISPLAY_CMAKE)
    adapter_pause_patch = read(ADAPTER_PAUSE_PATCH)
    adapter_pause_patch_new = patch_new_side(adapter_pause_patch)
    app = read(APP_SOURCE)
    app_header = read(APP_HEADER)
    settings_screen = json.loads(read(SETTINGS_SCREEN))
    settings_switch = json.loads(read(SETTINGS_SWITCH))
    adaptive_chrome = read(ADAPTIVE_CHROME_SOURCE)
    adaptive_command = read(ADAPTIVE_COMMAND_SOURCE)
    brookesia_gui = read(BROOKESIA_GUI_SOURCE)
    brookesia_gui_runtime_header = read(BROOKESIA_GUI_RUNTIME_HEADER)
    brookesia_app_manager = read(BROOKESIA_APP_MANAGER_SOURCE)
    brookesia_task = read(BROOKESIA_TASK_SOURCE)
    brookesia_impl_header = read(BROOKESIA_IMPL_HEADER)
    audio = read(AUDIO_SOURCE)
    voice_afe = read(VOICE_AFE_SOURCE)
    logger = read(LOGGER_SOURCE)
    snapshot = read(SNAPSHOT_SOURCE)
    distance = read(DISTANCE_SOURCE)
    distance_stub = read(DISTANCE_STUB_SOURCE)
    storage = read(STORAGE_SOURCE)
    platform_cmake = read(PLATFORM_CMAKE)
    sdkconfig_defaults = read_profile_defaults()

    rotation_restore = function_text(
        hardware,
        'extern "C" esp_err_t LUNASHELL_display_apply_saved_auto_rotation(',
        "saved auto-rotation restore",
    )
    require_regex(
        rotation_restore,
        r'load_or_create_bool_setting\(\s*settings,\s*kAutoRotationSetting,\s*'
        r'"Cardinal display auto-rotation",\s*false,\s*&enabled',
        "auto-rotation first-boot default off",
    )
    require_contains(
        rotation_restore,
        "return set_auto_rotation_runtime(enabled);",
        "saved auto-rotation applied without overriding existing preference",
    )
    switch_nodes = [
        child
        for child in settings_switch["node"]["children"]
        if child.get("id") == "toggle"
    ]
    require(
        len(switch_nodes) == 1
        and switch_nodes[0]["type"] == "switch"
        and switch_nodes[0]["toggleProps"]["checked"] is False,
        "Settings switch must render unchecked before saved state is bound",
    )
    require(
        "styleRefs" not in switch_nodes[0]
        and switch_nodes[0]["style"]["bgColor"] == "#173544"
        and switch_nodes[0]["partStyles"]["indicator"]["bgColor"] == "#20dfc1"
        and switch_nodes[0]["partStyles"]["knob"]["bgColor"] == "#f3f7fa",
        "Settings switches need self-contained styling without an unavailable theme ref",
    )
    screens = [
        asset
        for asset in settings_screen["assets"]
        if isinstance(asset, dict) and asset.get("id") == "LUNASHELL_settings"
    ]
    require(len(screens) == 1, "Settings screen must exist exactly once")
    settings_rows = screens[0]["children"][0]["children"][0]["children"]
    rotation_rows = [row for row in settings_rows if row.get("id") == "auto_rotation"]
    require(
        len(rotation_rows) == 1
        and rotation_rows[0]["templateId"] == "LUNASHELL_settings_switch"
        and rotation_rows[0]["overrides"]["toggle"]["events"][0]["action"]
        == "LUNASHELL.settings.display.rotation.changed",
        "Automatic rotation Settings row must use the persisted switch event",
    )
    rotation_overrides = rotation_rows[0]["overrides"]
    template_placement = settings_switch["node"]["placement"]
    switch_placement = switch_nodes[0]["placement"]
    require(
        template_placement["height"] == "52dp"
        and switch_placement["width"] == "52dp"
        and switch_placement["height"] == "30dp"
        and all("placement" not in value for value in rotation_overrides.values()),
        "Automatic rotation must match the size and row layout of other Settings switches",
    )
    require_contains(
        app,
        "next_controls.rotation_checked = orientation.auto_rotation_enabled;",
        "Automatic rotation switch reflects saved runtime state",
    )
    rotation_handler = function_text(
        app,
        "void App::handle_auto_rotation_event(",
        "Automatic rotation switch handler",
    )
    require_contains(
        rotation_handler,
        "LUNASHELL_display_set_auto_rotation_enabled(*checked)",
        "Automatic rotation switch persists its selected state",
    )

    for source, fragment, label in (
        (snapshot, "constexpr uint32_t kDefaultPeriodMs = 1000;", "Full Snapshot default cadence"),
        (hardware, "constexpr uint32_t kDiagnosticPeriodDefaultMs = 300;", "Diagnostic default cadence"),
        (hardware, "constexpr uint32_t kDiagnosticPeriodMinMs = 100;", "Diagnostic minimum cadence"),
        (hardware, "constexpr uint32_t kDiagnosticPeriodMaxMs = 2000;", "Diagnostic maximum cadence"),
        (hardware, 'constexpr char kDiagnosticPeriodSetting[] = "diag_ms";', "persisted Diagnostic cadence key"),
    ):
        require_contains(source, fragment, label)

    require(
        "GPIO_NUM_41" not in power,
        "LUNASHELL_power must not treat SDMMC_CS GPIO41 as the physical PWR key",
    )
    require(
        "gpio_get_level(kPowerButton)" not in power,
        "LUNASHELL_power must consume PMIC PKEY events instead of polling a GPIO",
    )
    require_regex(
        header,
        r"LUNASHELL_hardware_take_power_key_events\s*\(\s*"
        r"uint32_t\s*\*\s*short_presses\s*,\s*"
        r"uint32_t\s*\*\s*long_presses\s*,\s*"
        r"LUNASHELL_power_key_sample_t\s*\*\s*sample\s*\)",
        "LUNASHELL_platform.h",
    )
    require_contains(power, "LUNASHELL_hardware_take_power_key_events(", "LUNASHELL_power.cpp")
    require_regex(
        power,
        r"(?:esp_err_t|auto)\s+\w+\s*="
        r"\s*LUNASHELL_hardware_take_power_key_events\(",
        "checked PKEY consumption",
    )

    sample_pmu = function_text(
        pmu_owner,
        "esp_err_t sample_pmu(",
        "LUNASHELL_hardware.cpp",
    )
    require(
        "read_pmu_irq_status(" not in sample_pmu
        and "clear_and_verify_pmu_irq_status(" not in sample_pmu
        and "XPOWERS_AXP2101_PKEY_" not in sample_pmu,
        "sample_pmu must remain telemetry-only and must not own PMIC IRQs",
    )

    irq_reader = function_text(
        pmu_owner,
        "esp_err_t read_pmu_irq_status(",
        "LUNASHELL_hardware.cpp",
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
        pmu_owner,
        "esp_err_t verify_pmu_irq_enable_mask(",
        "LUNASHELL_hardware.cpp",
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
        pmu_owner,
        "esp_err_t initialize(",
        "LUNASHELL_hardware.cpp",
    )
    require_regex(
        init_pmu,
        r"s_power\.begin\s*\(\s*AXP2101_ADDRESS\s*,\s*"
        r"xpowers_read_register\s*,\s*xpowers_write_register\s*\)",
        "bounded XPowers callback initialization",
    )
    battery_detection = function_text(
        pmu_owner,
        "esp_err_t enable_and_verify_pmu_battery_detection(",
        "AXP2101 battery detector",
    )
    for fragment in (
        "XPOWERS_AXP2101_BAT_DET_CTRL",
        "static_cast<uint8_t>(value | BIT(0))",
        "uint8_t readback = 0",
        "(readback & BIT(0)) == 0U",
        "return ESP_ERR_INVALID_RESPONSE;",
    ):
        require_contains(
            battery_detection,
            fragment,
            "AXP2101 battery detector enable/readback",
        )
    require(
        battery_detection.count("XPOWERS_AXP2101_BAT_DET_CTRL") >= 4,
        "battery detector must address BAT_DET_CTRL for read, write, and readback",
    )
    require_regex(
        battery_detection,
        r"write_register\s*\(\s*s_pmu_device\s*,\s*"
        r"XPOWERS_AXP2101_BAT_DET_CTRL\s*,\s*"
        r"static_cast<uint8_t>\(value\s*\|\s*BIT\(0\)\)\s*\)",
        "AXP2101 BAT_DET_CTRL read-modify-write",
    )
    detector_at = init_pmu.find("enable_and_verify_pmu_battery_detection()")
    adc_enable_at = init_pmu.find("s_power.enableVbusVoltageMeasure()")
    require(
        0 <= detector_at < adc_enable_at,
        "PMIC initialization must enable and verify battery detection before ADCs",
    )

    sample_pmu_locked = function_text(
        pmu_owner,
        "esp_err_t sample_pmu_locked(",
        "AXP2101 telemetry sample",
    )
    status1_at = sample_pmu_locked.find("XPOWERS_AXP2101_STATUS1")
    status2_at = sample_pmu_locked.find("XPOWERS_AXP2101_STATUS2")
    presence_at = sample_pmu_locked.find("sample.battery_present =")
    battery_adc_at = sample_pmu_locked.find("XPOWERS_AXP2101_ADC_DATA_RELUST0")
    percent_at = sample_pmu_locked.find("XPOWERS_AXP2101_BAT_PERCENT_DATA")
    valid_at = sample_pmu_locked.find("sample.level_percent_valid = true")
    require(
        0 <= status1_at < status2_at < presence_at < battery_adc_at < percent_at < valid_at,
        "STATUS1/STATUS2 and BAT presence must precede conditional cell telemetry",
    )
    require(
        sample_pmu_locked.count(
            "if (error == ESP_OK && sample.battery_present)"
        ) >= 3,
        "cell ADC, percentage, and validity publication must all require BAT presence",
    )
    for fragment in (
        "bool level_percent_valid = false;",
        "bool battery_mv_valid = false;",
        "int level_percent = -1;",
        "int battery_mv = 0;",
    ):
        require_contains(
            device_snapshot,
            fragment,
            "absent-battery snapshot sentinels",
        )
    for fragment in (
        "sample.level_percent_valid = true;",
        "sample.battery_mv_valid = true;",
        "sample.level_percent = static_cast<int>(percent);",
        "sample.battery_mv = battery_mv;",
    ):
        require(
            sample_pmu_locked.count(fragment) == 1,
            f"cell telemetry assignment must have one presence-gated owner: {fragment}",
        )
    presence_gate_at = sample_pmu_locked.rfind(
        "if (error == ESP_OK && sample.battery_present)",
        0,
        valid_at,
    )
    presence_gate_end = sample_pmu_locked.find("}", valid_at)
    require(
        0 <= presence_gate_at < valid_at < presence_gate_end,
        "valid cell fields must be assigned only inside the BAT-present gate",
    )
    for fragment in (
        "snapshot.battery.level_percent_valid = false;",
        "snapshot.battery.battery_mv_valid = false;",
    ):
        require(
            sample_pmu_locked.count(fragment) >= 2,
            f"probe and telemetry failures must invalidate cell fields: {fragment}",
        )
    pmu_lock_failure = function_text(
        pmu_owner,
        "esp_err_t publish_pmu_lock_failure(",
        "AXP2101 telemetry lock failure",
    )
    for fragment in (
        "snapshot.battery.level_percent_valid = false;",
        "snapshot.battery.battery_mv_valid = false;",
    ):
        require_contains(
            pmu_lock_failure,
            fragment,
            "PMU lock failure cell-field invalidation",
        )
    for sample_text, success_fragments, failure_fragment, label in (
        (
            sample_pmu_locked,
            (
                "high_frequency_sample_phase(phase)",
                "ESP_LOG_LEVEL_LOCAL(",
                "ESP_LOG_DEBUG",
                "ESP_LOG_INFO",
            ),
            "ESP_LOGW(",
            "PMU periodic log policy",
        ),
        (
            function_text(rtc_owner, "esp_err_t sample_rtc(", "RTC sample"),
            (
                "high_frequency_sample_phase(phase)",
                "ESP_LOGD(",
                "ESP_LOGI(",
            ),
            "ESP_LOGW(",
            "RTC periodic log policy",
        ),
        (
            function_text(gnss_owner, "esp_err_t sample_gps(", "GNSS sample"),
            (
                "high_frequency_sample_phase(phase)",
                "ESP_LOGD(",
                "ESP_LOGI(",
            ),
            "ESP_LOGW(",
            "GNSS periodic log policy",
        ),
    ):
        for fragment in success_fragments:
            require_contains(sample_text, fragment, label)
        require_contains(sample_text, failure_fragment, f"{label} error visibility")
    runtime_pmu_reader = function_text(
        pmu_owner,
        "esp_err_t read_pmu_registers(",
        "LUNASHELL_hardware.cpp",
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
        pmu_owner,
        "esp_err_t clear_and_verify_pmu_irq_status(",
        "LUNASHELL_hardware.cpp",
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
        pmu_owner,
        "esp_err_t collect_pmu_irq_events(",
        "LUNASHELL_hardware.cpp",
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
        pmu_owner,
        "LUNASHELL_hardware_take_power_key_events(",
        "LUNASHELL_hardware.cpp",
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
        "LUNASHELL_power.cpp",
    )
    take_at = power_task.find("LUNASHELL_hardware_take_power_key_events(")
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
    for edge in ("XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ", "XPOWERS_AXP2101_PKEY_POSITIVE_IRQ"):
        require_contains(pmu_owner, edge, "physical PWR press/release acquisition")
        require_contains(queued_before_clear, edge, "PWR edges queued before verified IRQ clear")
    for fragment in ("snapshot.input", "key.power_pressed_at_us", "key.power_button.data_valid",
                     "key.power_pressed_valid", "kPowerKeyFreshnessUs", "s_power_off_hold.update(",
                     "request_power_off()"):
        require_contains(power_task, fragment, "continuous PWR shutdown uses only fresh canonical input")
    require_contains(power, "kPowerOffPressUs = 3000000", "three-second PWR software hold")
    restart = function_text(power, "void restart_preserving_logs(", "LUNASHELL_power.cpp")
    for fragment in ("LUNASHELL_logger_disable_sd()", "std::fflush(stdout)", "esp_restart()"):
        require_contains(restart, fragment, "bounded soft restart preserves serial and SD logs")
    require("nvs_flash_erase" not in restart and "setLongPressRestart" not in restart,
            "BOOT/CCP software reset must not erase settings or power-cycle the PMIC")
    require_contains(init_pmu, "update_and_verify_power_configuration(0x27, 0x0C, 0x0C)",
                     "emergency shutdown timing is written and read back")
    require_contains(power_task, "key_current && key.power_pressed", "fresh PWR hold inhibits inactivity sleep")

    idle_policy = function_text(
        power,
        "esp_err_t apply_idle_policy(",
        "LUNASHELL_power.cpp",
    )
    for fragment in (
        "s_command_timeout_seconds.load(",
        "s_status_timeout_seconds.load(",
        "s_dim_timeout_seconds.load(",
        "s_screen_idle_timeout_seconds.load(",
        "const bool standby_due",
        "expired(screen_idle_seconds)",
        "DisplayPowerMode::Dimmed",
        "begin_motion_grace(standby_requested_at_us)",
        "DeviceStateReason::Inactivity",
        "return set_standby(",
    ):
        require_contains(fragment=fragment, text=idle_policy, source="idle policy")
    require(
        "DisplayPowerMode::DisplayOff" not in idle_policy,
        "the inactivity timeout must enter the full standby transaction, not panel-off alone",
    )
    require_contains(
        device_snapshot,
        "Inactivity,",
        "device-state reason inventory",
    )
    require_contains(
        http_json,
        'return "inactivity";',
        "HTTP power-reason serialization",
    )
    require_contains(
        app,
        'reason = "INACTIVITY";',
        "battery UI power-reason rendering",
    )

    app_timer = function_text(
        app,
        "std::expected<void, std::string> App::on_timer(",
        "LUNASHELL_app.cpp",
    )
    for fragment in (
        "snapshot.power.state == DeviceOperatingState::Standby",
        "standby_ui_state_ == StandbyUiState::Quiesced",
        "kStandbyPowerStateSyncPeriodMs",
        "kActivePowerStateSyncPeriodMs",
        "sync_power_state_timer_period(",
    ):
        require_contains(
            app_timer,
            fragment,
            "power-state UI synchronization lifecycle",
        )
    app_timer_sync = function_text(
        app,
        "std::expected<void, std::string> App::sync_power_state_timer_period(",
        "LUNASHELL_app.cpp",
    )
    replacement_at = app_timer_sync.find("context.timer().start_periodic(")
    stop_previous_at = app_timer_sync.find("context.timer().stop(previous_timer)")
    publish_replacement_at = app_timer_sync.find(
        "power_state_sync_timer_ = *replacement_timer"
    )
    require(
        0 <= replacement_at < stop_previous_at < publish_replacement_at,
        "power-state timer replacement must preserve the previous bridge until a new timer exists",
    )
    for fragment in (
        "power_state_sync_period_ms_",
        "replacement power-state UI synchronization",
        "Failed to stop previous power-state UI synchronization timer",
    ):
        require_contains(
            app_timer_sync,
            fragment,
            "power-state UI timer replacement",
        )
    require_contains(
        app_header,
        "uint32_t power_state_sync_period_ms_ = 0;",
        "power-state UI timer period state",
    )

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
        "LUNASHELL_power.cpp",
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
        r"const\s+bool\s+qualified_motion\s*=\s*s_motion_monitor_started\s*&&\s*"
        r"qualified_motion_detected\s*\(\s*snapshot\.imu\s*\)\s*;",
        "qualified-motion power integration",
    )
    require_regex(
        power_task,
        r"if\s*\(\s*!s_codex_offline_standby\s*&&\s*\(\s*"
        r"\(leg_mode\s*&&\s*wake_on_leg_motion\s*&&\s*leg_motion\.level\s*>\s*0\)\s*\|\|\s*"
        r"\(!leg_mode\s*&&\s*s_shake_wake_enabled\.load\(std::memory_order_acquire\)\s*&&\s*"
        r"qualified_motion\)\s*\)\s*\)",
        "manual-only offline wake with independent Leg/Cody motion preferences",
    )
    require_contains(
        power_task,
        "s_leg_wake_enabled.load(std::memory_order_acquire)",
        "persisted Leg motion wake preference",
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
        "LUNASHELL_display_settings.cpp",
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
    require_contains(
        bsp_display,
        "static bool display_rotation_consistent = true;",
        "BSP rotation consistency state",
    )
    rotation_consistency_at = rotation_set.find(
        "if (!display_rotation_consistent)"
    )
    rotation_noop_at = rotation_set.find("if (rotation == current_rotation)")
    require(
        0 <= rotation_consistency_at < rotation_noop_at,
        "BSP rotation must reject an inconsistent state before its no-op path",
    )
    for fragment in (
        "bsp_display_rollback_rotation_config(",
        "display_rotation_consistent = rollback_result == ESP_OK",
        "BSP_DISPLAY_ERR_ROTATION_INCONSISTENT",
    ):
        require_contains(
            rotation_set,
            fragment,
            "BSP rotation verified rollback",
        )
    require_contains(
        bsp_display_header,
        "#define BSP_DISPLAY_ERR_ROTATION_INCONSISTENT "
        "ESP_ERR_INVALID_RESPONSE",
        "BSP terminal rotation error",
    )
    require_contains(
        bsp_display_header,
        "bool bsp_display_rotation_is_consistent(void);",
        "BSP rotation consistency query",
    )
    require_contains(
        bsp_display,
        "bool bsp_display_rotation_is_consistent(void)",
        "BSP rotation consistency query",
    )
    require(
        rotation_set.count("BSP_DISPLAY_ERR_ROTATION_INCONSISTENT") >= 3,
        "BSP rotation must return its terminal error for a latched state and "
        "both failed rollback paths",
    )
    rotation_apply = function_text(
        hardware,
        "esp_err_t apply_pending_display_rotation()",
        "hardware display rotation apply",
    )
    for fragment in (
        "if (result == BSP_DISPLAY_ERR_ROTATION_INCONSISTENT &&",
        "!bsp_display_rotation_is_consistent()",
        "s_rotation_retry_blocked.store(true",
        "s_rotation_failure_latched.store(true",
        "s_rotation_retry_after_us.store(",
        "kOrientationApplyRetryBackoffMs",
        "mark_sample_failure(",
        "publish_orientation_state()",
    ):
        require_contains(
            rotation_apply,
            fragment,
            "terminal display rotation publication",
        )
    orientation_sample = function_text(
        hardware,
        "void process_orientation_sample(",
        "orientation sampling",
    )
    failure_guard_at = orientation_sample.find(
        "if (!s_rotation_failure_latched.load("
    )
    sample_success_at = orientation_sample.find(
        "mark_sample_success(s_orientation.component",
        failure_guard_at,
    )
    require(
        0 <= failure_guard_at < sample_success_at,
        "a successful IMU sample must not overwrite a latched rotation failure",
    )
    rotation_monitor = function_text(
        hardware,
        "void diagnostic_monitor_task(",
        "hardware rotation monitor",
    )
    require(
        rotation_monitor.count(
            "!s_rotation_retry_blocked.load("
        ) >= 3,
        "terminal display rotation must block every retry and pending-wake path",
    )
    for fragment in (
        "const bool rotation_retry_due =",
        "rotation_attempt_check_us >= rotation_retry_after_us",
        "pending_retry_after_us > rotation_wait_started_us",
        "rotation_wait_ms",
    ):
        require_contains(
            rotation_monitor,
            fragment,
            "bounded display rotation retry",
        )

    app_start = function_text(
        app,
        "std::expected<void, std::string> App::on_start(",
        "LUNASHELL app startup",
    )
    performance_status_at = app_start.find(
        "LUNASHELL_performance_mode_get_status(&performance_status)"
    )
    performance_unknown_at = app_start.find(
        "if (!performance_status.state_known)",
        performance_status_at,
    )
    performance_reconcile_at = app_start.find(
        "LUNASHELL_performance_mode_set_enabled(reconcile_target)",
        performance_unknown_at,
    )
    performance_recheck_at = app_start.find(
        "LUNASHELL_performance_mode_get_status(&performance_status)",
        performance_reconcile_at,
    )
    performance_cache_at = app_start.find(
        "performance_mode_enabled_ = performance_status.enabled",
        performance_recheck_at,
    )
    require(
        0 <= performance_status_at < performance_unknown_at
        < performance_reconcile_at < performance_recheck_at
        < performance_cache_at,
        "app startup must reconcile an unknown Performance Mode transaction "
        "before caching its state",
    )
    settings_refresh = function_text(
        app,
        "std::expected<void, std::string> App::refresh_settings(",
        "Settings refresh",
    )
    for fragment in (
        "LUNASHELL_performance_mode_get_status(&performance_status)",
        "if (!performance_status.state_known)",
        "Performance profile UNKNOWN",
        "UI cadence UNKNOWN",
        "performance_ui_cadence_error_",
        "performance_status.primary_error",
        "performance_status.rollback_error",
    ):
        require_contains(
            settings_refresh,
            fragment,
            "explicit unknown Performance Mode status",
        )
    performance_event = function_text(
        app,
        "void App::handle_performance_event(",
        "Performance Mode event",
    )
    for fragment in (
        "performance_ui_cadence_known_",
        "performance_ui_cadence_known_ = false;",
        "performance_ui_cadence_error_ = ESP_ERR_INVALID_STATE;",
        "sync_refresh_timer(",
        "performance_ui_cadence_error_ = ESP_FAIL;",
    ):
        require_contains(
            performance_event,
            fragment,
            "Performance Mode UI cadence transaction",
        )
    require_contains(
        app_header,
        "bool performance_ui_cadence_known_ = true;",
        "Performance Mode UI cadence state",
    )
    diagnostic_disable_at = app_start.find(
        "LUNASHELL_hardware_set_diagnostic_streams(false, false, false)"
    )
    spectrum_flag_reset_at = app_start.find(
        "audio_spectrum_diagnostic_active_ = false",
        diagnostic_disable_at,
    )
    full_snapshot_start_at = app_start.find(
        "LUNASHELL_hardware_start_full_snapshot_monitor()",
        spectrum_flag_reset_at,
    )
    require(
        0 <= diagnostic_disable_at < spectrum_flag_reset_at
        < full_snapshot_start_at,
        "app startup must clear its spectrum lease flag after disabling the "
        "hardware stream",
    )
    fade_api = function_text(
        display,
        "LUNASHELL_display_fade_to_power_mode(",
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
    for fragment in (
        "suspend_standby_resources(false)",
        "recover_standby_sequence(",
        "run_hardware_standby_recovery",
        "resume_standby_resources(false,",
        "StandbyRecoveryState::Previous",
        "StandbyRecoveryState::Requested",
        "begin_standby_sleep_measurement()",
        "log_standby_sleep_measurement(",
    ):
        require_contains(standby_transition, fragment, "standby transaction")

    ui_request_at = standby_transition.find(
        "s_ui_quiescence_requested_sequence.store("
    )
    ui_wait_at = standby_transition.find("wait_for_ui_quiescence(")
    resource_suspend_at = standby_transition.find(
        "suspend_standby_resources(false)"
    )
    require(
        0 <= ui_request_at < ui_wait_at < resource_suspend_at,
        "UI producers must acknowledge quiescence before resource suspension",
    )
    for fragment in (
        "std::atomic<int32_t> s_ui_quiescence_result{ESP_ERR_INVALID_STATE}",
        "kUiQuiescenceTimeoutTicks",
        "Standby control loop active",
        "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT",
    ):
        require_contains(power, fragment, "standby wake-control handshake")
    require_regex(
        header,
        r"LUNASHELL_power_manager_note_ui_standby_result\s*\(\s*"
        r"uint32_t\s+transition_sequence\s*,\s*"
        r"esp_err_t\s+result\s*\)",
        "standby UI result API",
    )
    require(
        "LUNASHELL_power_manager_note_ui_standby_ready" not in power
        and "LUNASHELL_power_manager_note_ui_standby_ready" not in header
        and "LUNASHELL_power_manager_note_ui_standby_ready" not in app,
        "the removed ready-only standby acknowledgement must not return",
    )

    ui_result_notifier = function_text(
        power,
        'extern "C" void LUNASHELL_power_manager_note_ui_standby_result(',
        "standby UI result notifier",
    )
    require_regex(
        ui_result_notifier,
        r"s_ui_quiescence_requested_sequence\.load\(\s*"
        r"std::memory_order_acquire\s*\).*?"
        r"s_ui_quiescence_result\.store\(\s*result\s*,\s*"
        r"std::memory_order_release\s*\).*?"
        r"s_ui_quiescence_acknowledged_sequence\.store\(\s*"
        r"transition_sequence\s*,\s*std::memory_order_release\s*\).*?"
        r"xTaskNotifyGive\(s_task\)",
        "standby result publication before acknowledgement and wake",
    )
    ui_quiescence_wait = function_text(
        power,
        "esp_err_t wait_for_ui_quiescence(",
        "standby UI result wait",
    )
    require_regex(
        ui_quiescence_wait,
        r"s_ui_quiescence_acknowledged_sequence\.load\(\s*"
        r"std::memory_order_acquire\s*\).*?"
        r"s_ui_quiescence_result\.load\(std::memory_order_acquire\)",
        "acknowledgement acquire before result acquire",
    )
    for fragment in (
        "if (result != ESP_OK)",
        "return result;",
        "UI standby acknowledged: sequence=%lu",
    ):
        require_contains(
            ui_quiescence_wait,
            fragment,
            "standby UI success and failure propagation",
        )

    app_power_timer = function_text(
        app,
        "std::expected<void, std::string> App::on_timer(",
        "app power-state synchronization",
    )
    app_quiesce_order = [
        app_power_timer.find(
            "sync_refresh_timer(context, DisplayIdleStage::Off)"
        ),
        app_power_timer.find("adaptive_chrome_.quiesce(context.gui())"),
        app_power_timer.find("context.gui().quiesce_input("),
    ]
    require(
        all(index >= 0 for index in app_quiesce_order)
        and app_quiesce_order == sorted(app_quiesce_order),
        "app must stop refresh and Chrome before closing app input",
    )
    for failed_stage in (
        r"sync_refresh_timer\(context, DisplayIdleStage::Off\).*?"
        r"LUNASHELL_power_manager_note_ui_standby_result\(\s*"
        r"transition_sequence\s*,\s*ESP_FAIL\s*\)",
        r"adaptive_chrome_\.quiesce\(context\.gui\(\)\).*?"
        r"LUNASHELL_power_manager_note_ui_standby_result\(\s*"
        r"transition_sequence\s*,\s*ESP_FAIL\s*\)",
        r"if\s*\(!drain_result\).*?"
        r"LUNASHELL_power_manager_note_ui_standby_result\(\s*"
        r"transition_sequence\s*,\s*ESP_FAIL\s*\)",
        r"if\s*\(!quiesce_result\).*?"
        r"LUNASHELL_power_manager_note_ui_standby_result\(\s*"
        r"transition_sequence\s*,\s*ESP_FAIL\s*\)",
    ):
        require_regex(
            app_power_timer,
            failed_stage,
            "LUNASHELL UI standby failure acknowledgement",
        )
    require_regex(
        app_power_timer,
        r"standby_ui_state_\s*=\s*StandbyUiState::Quiesced\s*;.*?"
        r"LUNASHELL_power_manager_note_ui_standby_result\(\s*"
        r"transition_sequence\s*,\s*ESP_OK\s*\)",
        "LUNASHELL UI standby success acknowledgement after natural drain",
    )
    for fragment in (
        "enum class StandbyUiState",
        "StandbyUiState standby_ui_state_ = StandbyUiState::Active",
        "uint32_t standby_ui_generation_ = 0",
        "uint32_t standby_ui_transition_sequence_ = 0",
    ):
        require_contains(app_header, fragment, "app standby generation gate")
    app_action = function_text(
        app,
        "std::expected<void, std::string> App::on_action(",
        "app action guard",
    )
    require_contains(
        app_action,
        "standby_ui_state_ != StandbyUiState::Active",
        "app standby action guard",
    )
    for fragment in (
        "drain_pending_work() const",
        "quiesce_input(",
        "resume_input() const",
        "std::expected<void, std::string> stop_animation(",
    ):
        require_contains(
            brookesia_gui_runtime_header,
            fragment,
            "Brookesia app GUI lifecycle API",
        )
    gui_drain = function_text(
        brookesia_gui,
        "std::expected<void, std::string> System::gui_drain_pending_work(",
        "Brookesia GUI drain",
    )
    gui_drain_order = [
        gui_drain.find("SYSTEM_GUI_INPUT_TASK_GROUP"),
        gui_drain.find("SYSTEM_GUI_TASK_GROUP"),
        gui_drain.find("flush_pending_gui_bindings(app_id)"),
    ]
    require(
        all(index >= 0 for index in gui_drain_order)
        and gui_drain_order == sorted(gui_drain_order),
        "GUI drain must serialize input before flushing pending bindings",
    )
    require_contains(
        brookesia_impl_header,
        "bool input_quiesced = false",
        "Brookesia per-app input gate",
    )
    gui_stop_animation = function_text(
        brookesia_gui,
        "std::expected<void, std::string> System::gui_stop_animation(",
        "Brookesia animation stop",
    )
    require_regex(
        gui_stop_animation,
        r"if\s*\(\s*!impl_->gui_runtime_->has_subscription\("
        r"subscription_id\)\s*\)\s*\{.*?"
        r"GUI animation already completed.*?return\s*\{\s*\}\s*;"
        r".*?unsubscribe_subscription\(subscription_id\)",
        "naturally completed animations are an idempotent stop success",
    )
    app_input_dispatch = function_text(
        brookesia_task,
        "std::expected<void, std::string> System::Impl::enqueue_app_input_task(",
        "Brookesia app input dispatch gate",
    )
    for fragment in (
        "!bypass_quiesce && record_result.value()->input_quiesced",
        "record_result.value()->info.state != AppState::Running",
        "task_scheduler_->post(",
        "SYSTEM_APP_INPUT_TASK_GROUP",
    ):
        require_contains(
            app_input_dispatch,
            fragment,
            "Brookesia app input execution gate",
        )
    app_input_barrier = function_text(
        brookesia_task,
        "std::expected<void, std::string> System::Impl::post_app_input_barrier(",
        "Brookesia deferred AppInput barrier",
    )
    require_contains(
        app_input_barrier,
        "return enqueue_app_input_task(app_id, std::move(task), true);",
        "Brookesia quiescence-bypassing AppInput barrier",
    )
    require(
        "return post_task(" not in app_input_barrier,
        "AppInput barrier must never execute inline from the App domain",
    )
    core_quiesce = function_text(
        brookesia_app_manager,
        "std::expected<void, std::string> System::app_quiesce_input(",
        "Brookesia app input quiescence",
    )
    core_quiesce_order = [
        core_quiesce.find("record.input_quiesced = true"),
        core_quiesce.find("post_app_input_barrier("),
        core_quiesce.find("gui_drain_pending_work(app_id)"),
        core_quiesce.find("on_drained(std::move(drain_result))"),
    ]
    require(
        all(index >= 0 for index in core_quiesce_order)
        and core_quiesce_order == sorted(core_quiesce_order),
        "Brookesia must gate input before its deferred GUI drain callback",
    )
    for fragment in (
        "post_app_input_task(app_id, std::move(post_action))",
        "impl_->post_app_input_task(",
    ):
        require_contains(
            brookesia_gui,
            fragment,
            "Brookesia app-scoped GUI input dispatch",
        )
    adaptive_quiesce = function_text(
        adaptive_chrome,
        "std::expected<void, std::string> AdaptiveChromeController::quiesce(",
        "adaptive chrome standby quiescence",
    )
    for fragment in (
        "stop_status_animation(gui)",
        'status_target_visible_ ? "255" : "0"',
        "status_visible_ = status_target_visible_",
        "command_bar_.quiesce(gui)",
        "set_wake_surface(gui, false)",
    ):
        require_contains(
            adaptive_quiesce,
            fragment,
            "adaptive chrome standby quiescence",
        )
    command_quiesce = function_text(
        adaptive_command,
        "std::expected<void, std::string> AdaptiveCommandBar::quiesce(",
        "command bar standby quiescence",
    )
    for fragment in (
        "stop_animations(gui)",
        'animation_target_visible_ ? "255" : "0"',
        "visible_ = animation_target_visible_",
        "set_hit_testing(gui, false)",
    ):
        require_contains(
            command_quiesce,
            fragment,
            "command bar standby quiescence",
        )

    resource_transition = function_text(
        power,
        "esp_err_t transition_standby_resource(const char *component, bool target,",
        "standby resource transition",
    )
    radio_policy = function_text(power, "esp_err_t set_standalone_wireless_standby(",
                                 "standby radio policy")
    require_regex(radio_policy,
                  r"if\s*\(standby\s*&&\s*!s_shutdown_in_progress\s*&&\s*"
                  r"!s_codex_offline_standby\s*&&\s*s_standby_resources\.wireless\.consistent\s*\)",
                  "radio retention excludes shutdown/offline standby and requires observed consistency")
    suspension = function_text(power, "esp_err_t suspend_standby_resources(", "resource suspension")
    require("s_standby_resources.wireless.consistent = false" not in suspension,
            "policy reassertion must preserve the last confirmed radio state")
    hardware_step = function_text(power, "execute_hardware_standby_step(", "hardware resource adapter")
    require_contains(hardware_step, "set_standalone_wireless_standby, target",
                     "radio policy reassertion is explicit")
    require_contains(suspension, "execute_standby_sequence(true, best_effort",
                     "firmware suspension executes shared resource order")
    require_contains(standby_transition,
                     "selected_standby_sleep() == LUNASHELL::power_policy::StandbySleep::Deep",
                     "service policy precedes optional deep-sleep admission")
    for fragment in (
        "status.consistent && status.applied == target",
        "status.consistent = false",
        "status.applied = target",
        "status.consistent = true",
    ):
        require_contains(
            resource_transition,
            fragment,
            "standby unknown-state tracking",
        )
    require_contains(
        power,
        "standby_resources_match_target(",
        "mandatory standby consistency validation",
    )
    for fragment in (
        "optional_errors.first_error() != ESP_OK",
        "recovery_optional_errors.first_error() != ESP_OK",
        "recovery_publication_error",
        "publish_power(optional_error)",
        "return optional_error",
    ):
        require_contains(
            standby_transition,
            fragment,
            "optional storage/radio recovery publication preserves real failures",
        )

    suspend_resources = function_text(
        power,
        "esp_err_t suspend_standby_resources(",
        "standby resource suspension",
    )
    suspend_plan = shared_standby.split("static constexpr StandbyStep suspend[] = {", 1)[1].split("};", 1)[0]
    suspend_order = [suspend_plan.find("StandbyStep::" + name) for name in (
        "FullSnapshots", "UiSampling", "Gps", "Distance", "Storage", "Imu", "MotionWake",
        "VoiceAfe", "BaseAudio", "Wireless", "StandaloneDisplay", "Display", "Lvgl")]
    require(all(index >= 0 for index in suspend_order) and suspend_order == sorted(suspend_order),
            "shared standby plan must quiesce producers before panel-off and LVGL pause")
    require_contains(hardware_step, "transition_standby_resource(name, requested, *status, transition)",
                     "shared plan adapter preserves actual hardware owner status")
    bsp_pause = function_text(
        bsp_display,
        "esp_err_t bsp_display_lvgl_pause(void)",
        "BSP LVGL pause",
    )
    for fragment in (
        "esp_lv_adapter_pause(",
        "recovery_result = esp_lv_adapter_resume()",
        "BSP_DISPLAY_LVGL_FAULTED_PAUSED",
    ):
        require_contains(bsp_pause, fragment, "direct transactional LVGL pause")
    bsp_resume = function_text(
        bsp_display,
        "esp_err_t bsp_display_lvgl_resume(void)",
        "BSP LVGL resume",
    )
    for fragment in (
        "previous_state != BSP_DISPLAY_LVGL_PAUSED",
        "previous_state != BSP_DISPLAY_LVGL_FAULTED_PAUSED",
        "result == ESP_OK ? BSP_DISPLAY_LVGL_ACTIVE : previous_state",
    ):
        require_contains(
            bsp_resume,
            fragment,
            "verified and faulted LVGL resume recovery",
        )
    for removed_async_path in (
        "lv_async_call(",
        "lv_async_call_cancel(",
        "bsp_display_lvgl_pause_from_worker",
    ):
        require(
            removed_async_path not in bsp_display,
            f"removed async LVGL pause path returned: {removed_async_path}",
        )

    require_regex(
        bsp_display_cmake,
        r"function\s*\(\s*bsp_apply_required_patches\s+component\s*\).*?"
        r"foreach\s*\(\s*_component_patch\s+IN\s+LISTS\s+ARGN\s*\).*?"
        r"\$\{GIT_EXECUTABLE\}\s+apply\s+--reverse\s+--check.*?"
        r"\$\{GIT_EXECUTABLE\}\s+apply\s+--check.*?"
        r"\$\{GIT_EXECUTABLE\}\s+apply\b",
        "reusable idempotent application of required pinned-component patches",
    )
    require_regex(
        bsp_display_cmake,
        r"bsp_apply_required_patches\s*\(\s*espressif__esp_lvgl_adapter\b.*?"
        r"esp_lvgl_adapter_synchronous_custom_draw\.patch.*?"
        r"esp_lvgl_adapter_pause_recovery\.patch.*?\)",
        "pinned adapter synchronous-draw and pause-recovery patch registration",
    )

    patched_pause_signature = "esp_err_t esp_lv_adapter_pause(int32_t timeout_ms)"
    patched_resume_signature = "esp_err_t esp_lv_adapter_resume(void)"
    patched_resume_at = adapter_pause_patch_new.rfind(
        patched_resume_signature
    )
    patched_pause_at = adapter_pause_patch_new.rfind(
        patched_pause_signature,
        0,
        patched_resume_at,
    )
    require(
        0 <= patched_pause_at < patched_resume_at,
        "patched adapter pause/resume hunks are missing",
    )
    patched_pause = adapter_pause_patch_new[patched_pause_at:patched_resume_at]
    pause_publish_order = [
        patched_pause.find("xSemaphoreTake(s_ctx.pause_done_sem, 0)"),
        patched_pause.find("s_ctx.paused = true"),
        patched_pause.find("xTaskNotifyGive(s_ctx.task)"),
    ]
    require(
        all(index >= 0 for index in pause_publish_order)
        and pause_publish_order == sorted(pause_publish_order),
        "adapter must clear stale ACK before publishing and notifying pause",
    )
    pause_timeout = require_regex(
        patched_pause,
        r"if\s*\(\s*xSemaphoreTake\(s_ctx\.pause_done_sem\s*,\s*ticks\)"
        r"\s*!=\s*pdTRUE\s*\)\s*\{.*?return\s+ESP_ERR_TIMEOUT\s*;\s*\}",
        "adapter pause timeout rollback",
    ).group(0)
    for fragment in (
        "s_ctx.paused = false",
        "s_ctx.pause_ack = false",
        "xTaskNotifyGive(s_ctx.task)",
    ):
        require_contains(
            pause_timeout,
            fragment,
            "adapter pause timeout rollback",
        )
    require(
        patched_pause.count("adapter_stop_tick_timer()") >= 3
        and patched_pause.count("adapter_start_tick_timer()") >= 3,
        "every adapter pause ownership path must stop and recover the tick",
    )
    for fragment in (
        "esp_err_t rollback_ret = adapter_start_tick_timer()",
        "Failed to restore LVGL tick timer after pause error",
    ):
        require_contains(
            patched_pause,
            fragment,
            "adapter tick-stop rollback",
        )
    patched_resume = adapter_pause_patch_new[patched_resume_at:]
    resume_tick_order = [
        patched_resume.find("esp_err_t ret = adapter_start_tick_timer()"),
        patched_resume.find("if (ret != ESP_OK)"),
        patched_resume.find("s_ctx.paused = false"),
    ]
    require(
        all(index >= 0 for index in resume_tick_order)
        and resume_tick_order == sorted(resume_tick_order),
        "adapter resume must restart the tick before clearing paused",
    )

    resume_resources = function_text(
        power,
        "esp_err_t resume_standby_resources(",
        "standby resource resume",
    )
    require_contains(resume_resources, "execute_standby_sequence(false, best_effort",
                     "firmware wake executes the same shared resource order")
    resume_plan = shared_standby.split("static constexpr StandbyStep resume[] = {", 1)[1].split("};", 1)[0]
    resume_order = [resume_plan.find("StandbyStep::" + name) for name in (
        "Lvgl", "MotionWake", "Imu", "Distance", "Storage", "BaseAudio", "VoiceAfe",
        "UiSampling", "Gps", "FullSnapshots", "Wireless", "PrepareDarkFrame",
        "StandaloneDisplay", "PrepareWakeFrame", "Display")]
    require(all(index >= 0 for index in resume_order) and resume_order == sorted(resume_order),
            "shared wake plan must restore producers before exposing the active panel")
    require_contains(shared_standby, "result.error != ESP_OK && (!best_effort || exposes_display)",
                     "failed producers keep the display dark even during best-effort recovery")
    require_contains(shared_standby, "run(owner, !requested, true)", "shared rollback restores previous state first")
    require_contains(shared_standby, "run(owner, requested, true)", "shared fallback retries requested state only after rollback failure")

    for fragment in (
        "LUNASHELL_hardware_set_full_snapshot_power_suspended(bool suspended)",
        "LUNASHELL_hardware_set_gps_power_suspended(bool suspended)",
        "LUNASHELL_distance_set_power_suspended(bool suspended)",
        "LUNASHELL_storage_set_power_suspended(bool suspended)",
        "LUNASHELL_hardware_set_ui_motion_monitor_standby(bool standby)",
        "LUNASHELL_hardware_set_ui_sampling_power_suspended(bool suspended)",
        "LUNASHELL_hardware_set_imu_low_power(bool enabled)",
    ):
        require_contains(header, fragment, "LUNASHELL_platform.h standby lifecycle")
    require_contains(
        snapshot,
        "ulTaskNotifyTake(pdTRUE, portMAX_DELAY)",
        "parked Full Snapshot worker",
    )
    distance_power = function_text(
        distance,
        "esp_err_t distanceSetPowerSuspended(",
        "TOF power lifecycle",
    )
    require_contains(
        distance_power,
        "LUNASHELL_tof400f_deinit()",
        "standby TOF UART release",
    )
    distance_stub_power = function_text(
        distance_stub,
        "esp_err_t distanceSetPowerSuspended(",
        "TOF build-off power lifecycle",
    )
    require_contains(
        distance_stub_power,
        "return ESP_ERR_NOT_SUPPORTED;",
        "TOF build-off power lifecycle",
    )
    require_regex(
        platform_cmake,
        r"if\s*\(\s*CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE\s+AND\s+"
        r"CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART\s*\)"
        r".*?LUNASHELL_distance_service\.cpp.*?else\s*\(\s*\)"
        r".*?LUNASHELL_distance_service_stub\.cpp.*?endif\s*\(\s*\)",
        "profile-selected distance lifecycle implementation",
    )
    require_contains(
        storage,
        "s_power_suspended",
        "standby storage reason",
    )
    audio_standby = function_text(
        audio,
        "LUNASHELL_audio_set_standby(",
        "audio standby transaction",
    )
    require_contains(
        audio_standby,
        "bsp_audio_set_transport_standby(true, false, false)",
        "I2S standby",
    )
    require_contains(
        audio_standby,
        "error = bsp_audio_set_transport_standby(\n        false,",
        "I2S resume",
    )
    for worker, paused in (("feed", "s_feed_paused"), ("fetch", "s_fetch_paused")):
        worker_body = function_text(
            voice_afe, f"void voice_{worker}_task(", f"voice AFE {worker} worker",
        )
        require_regex(
            worker_body,
            r"if\s*\(s_standby_requested\.load\([^)]*\)\)\s*\{\s*"
            rf"park_paused_worker\({paused}\);\s*continue;",
            f"voice AFE {worker} delegates standby to the shared event-driven gate",
        )
        require_contains(
            worker_body, f"wait_worker_start({'true' if worker == 'feed' else 'false'})",
            f"voice AFE {worker} uses the shared startup gate",
        )
        require("vTaskDelay(" not in worker_body,
                f"voice AFE {worker} must not retain startup or paused polling")
    afe_park = function_text(
        voice_afe, "void park_paused_worker(", "voice AFE shared paused gate",
    )
    require_regex(
        afe_park,
        r"paused\.store\(true,.*?signal_lifecycle_change\(\);.*?"
        r"while\s*\(s_running\.load\([^)]*\)\s*&&\s*"
        r"s_standby_requested\.load\([^)]*\)\)\s*\{\s*"
        r"\(void\)ulTaskNotifyTake\(pdTRUE,\s*portMAX_DELAY\);\s*\}.*?"
        r"paused\.store\(false,.*?signal_lifecycle_change\(\);",
        "AFE shared gate acknowledges both edges and waits indefinitely while paused and running",
    )
    require("vTaskDelay(" not in afe_park, "AFE shared paused gate must not poll")
    afe_wait = function_text(
        voice_afe, "void wait_lifecycle_change(", "AFE owner acknowledgment wait",
    )
    require_contains(afe_wait, "timeout - elapsed", "AFE owner retains its original deadline budget")
    require_contains(afe_wait, "xSemaphoreTake(", "AFE owner blocks on actual acknowledgment events")
    require("vTaskDelay(" not in afe_wait, "AFE owner acknowledgment must not poll")
    afe_standby = function_text(
        voice_afe,
        "esp_err_t apply_afe_standby_locked(",
        "voice AFE standby transaction",
    )
    require_contains(afe_standby, "wait_lifecycle_change(started_at, kPauseTimeout)",
                     "AFE owner delegates acknowledgment waiting to the bounded event gate")
    require("vTaskDelay(" not in afe_standby, "AFE standby transaction must not poll for acknowledgment")
    audio_resume_index = afe_standby.find("LUNASHELL_audio_set_standby(false)")
    afe_resume_order = [
        audio_resume_index,
        afe_standby.find(
            "s_standby_requested.store(false",
            audio_resume_index,
        ),
        afe_standby.find(
            'notify_workers("resume", true)',
            audio_resume_index,
        ),
    ]
    require(
        all(index >= 0 for index in afe_resume_order)
        and afe_resume_order == sorted(afe_resume_order),
        "voice AFE must restore audio before releasing and waking both workers",
    )
    logger_writer = function_text(
        logger,
        "static void writer_task(",
        "logger SD writer",
    )
    require_contains(
        logger_writer,
        "ulTaskNotifyTake(pdTRUE, portMAX_DELAY)",
        "event-driven disabled logger",
    )
    require(
        "vTaskDelay(pdMS_TO_TICKS(100))" not in logger_writer,
        "disabled logger must not retain its former 100 ms polling wake",
    )
    for signature in (
        "static esp_err_t set_sd_enabled(",
        "static esp_err_t set_sd_disabled(",
    ):
        require_contains(
            function_text(logger, signature, "logger SD state transition"),
            "notify_writer_state_change(",
            "logger writer wake",
        )
    require_contains(
        bsp_display,
        "esp_lv_adapter_pause(",
        "LVGL pause",
    )
    require_contains(
        bsp_display,
        "esp_lv_adapter_resume()",
        "LVGL resume",
    )
    imu_profile = function_text(
        imu_owner,
        "LUNASHELL_hardware_set_imu_low_power(",
        "IMU low-power profile",
    )
    require_contains(imu_profile, "configure_imu_profile(enabled)", "IMU profile")
    require_contains(imu_owner, "QMI8658_ACCEL_ODR_31_25HZ", "IMU low-power ODR")
    require_contains(imu_owner, "QMI8658_GYRO_ODR_31_25HZ", "IMU low-power ODR")
    require_contains(
        hardware,
        "kUiMotionStandbySamplePeriodMs = 1000",
        "IMU standby motion cadence",
    )
    ui_sampling = function_text(
        hardware,
        "LUNASHELL_hardware_set_ui_sampling_power_suspended(",
        "UI sampling standby lifecycle",
    )
    for fragment in (
        "s_diagnostic_settings_mutex",
        "ensure_refresh_mutex()",
        "s_ui_sampling_power_suspended.exchange(",
        "s_ui_sampling_orientation_reset_pending.store(",
        "UI sampling standby transition",
    ):
        require_contains(
            ui_sampling,
            fragment,
            "transactional UI sampling standby lifecycle",
        )
    diagnostic_monitor = function_text(
        hardware,
        "void diagnostic_monitor_task(",
        "diagnostic and orientation monitor",
    )
    for fragment in (
        "auto_rotation_requested && !ui_sampling_power_suspended",
        "effective_ui_sampling_power_suspended",
        "!effective_ui_sampling_power_suspended &&",
        "reset_auto_rotation_filter_after_power_resume()",
        "s_ui_sampling_orientation_reset_pending.load(",
    ):
        require_contains(
            diagnostic_monitor,
            fragment,
            "standby UI sampling gate",
        )
    require(
        "vTaskDelay(" not in diagnostic_monitor,
        "idle hardware monitor must not retain a periodic vTaskDelay wake",
    )
    for fragment in (
        "const bool wait_indefinitely =",
        "? portMAX_DELAY",
        "(void)ulTaskNotifyTake(pdTRUE, wait_ticks);",
    ):
        require_contains(
            diagnostic_monitor,
            fragment,
            "event-driven hardware monitor idle wait",
        )
    require_regex(
        diagnostic_monitor,
        r"const\s+bool\s+wait_indefinitely\s*=\s*"
        r"!auto_rotation_active\s*&&\s*"
        r"!rotation_apply_pending\s*&&\s*"
        r"!ui_motion_active\s*&&\s*"
        r"!monitor_active\s*&&\s*!activity_active\s*;",
        "hardware monitor idle-only indefinite wait",
    )
    notifier = function_text(
        hardware,
        "void notify_hardware_monitor(",
        "hardware monitor notifier",
    )
    require_contains(
        notifier,
        "xTaskNotifyGive(task);",
        "hardware monitor notifier",
    )
    monitor_start = function_text(
        hardware,
        "static esp_err_t ensure_hardware_monitor_task_locked(",
        "hardware monitor task start",
    )
    create_at = monitor_start.find("xTaskCreatePinnedToCoreWithCaps(")
    require_regex(
        monitor_start,
        r"#if\s+CONFIG_LUNASHELL_ACTIVITY_MODEL\s+"
        r"LUNASHELL::task_affinity::kActivityAcquisitionCoreId\s*,\s*"
        r"#else\s+tskNO_AFFINITY\s*,\s*#endif\s+"
        r"MALLOC_CAP_SPIRAM\s*\|\s*MALLOC_CAP_8BIT",
        "only activity-enabled acquisition pins the PSRAM-stack monitor to its declared core",
    )
    affinity = read(ROOT / "components/LUNASHELL_platform/include/LUNASHELL_task_affinity.hpp")
    require_contains(affinity, "kActivityAcquisitionCoreId != kVoiceCoreId",
                     "activity acquisition must retain its compile-time voice-core isolation")
    require_contains(affinity, '#error "Activity acquisition requires dual cores"',
                     "activity affinity must reject unsupported single-core configurations")
    handle_at = monitor_start.find(
        "s_diagnostic_monitor_task_handle.store(task",
        create_at,
    )
    ready_at = monitor_start.find(
        "Hardware diagnostics/orientation monitor ready",
        handle_at,
    )
    require(
        0 <= create_at < handle_at < ready_at,
        "created hardware monitor handle must be published before readiness",
    )
    for signature in (
        'extern "C" esp_err_t LUNASHELL_hardware_set_diagnostic_streams(',
        'extern "C" esp_err_t LUNASHELL_hardware_set_ui_motion_monitor(',
        'extern "C" esp_err_t LUNASHELL_hardware_set_ui_motion_monitor_standby(',
        'extern "C" esp_err_t LUNASHELL_hardware_set_ui_sampling_power_suspended(',
        "static esp_err_t set_auto_rotation_runtime(",
        'extern "C" esp_err_t LUNASHELL_hardware_set_diagnostic_period_ms(',
    ):
        require_contains(
            function_text(hardware, signature, "hardware monitor setter"),
            "notify_hardware_monitor();",
            f"hardware monitor wake from {signature}",
        )
    require_contains(
        power,
        "stop_radios_for_standby",
        "standby radio shutdown",
    )
    require_contains(
        power,
        "RTC_NOINIT_ATTR StandbyTrace s_retained_standby_trace",
        "retained standby trace",
    )
    for fragment in (
        "StandbyTracePhase::EntryRequested",
        "StandbyTracePhase::RadiosStopping",
        "StandbyTracePhase::StandbyEntered",
        "StandbyTracePhase::WakeRequested",
        "StandbyTracePhase::Awake",
        "Retained standby trace",
        "kDeferredStandbyTraceReportDelayUs",
        "kDeferredStandbyTraceReportCount",
        "s_deferred_standby_trace_report_at_us",
        "s_deferred_standby_trace_reports_remaining",
        "kDevelopmentDiagnosticsEnabled ? 100 : 1000",
    ):
        require_contains(power, fragment, "standby trace lifecycle")

    for fragment in (
        "CONFIG_PM_ENABLE=y",
        "CONFIG_FREERTOS_USE_TICKLESS_IDLE=y",
        "CONFIG_PM_LIGHT_SLEEP_CALLBACKS=y",
        "CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=n",
        "# CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP is not set",
        "CONFIG_BT_CTRL_MODEM_SLEEP=y",
        "CONFIG_ESP_PHY_MAC_BB_PD=n",
    ):
        require_contains(sdkconfig_defaults, fragment, "ESP-IDF power defaults")
    require_regex(
        power,
        r"esp_pm_config_t\s+power_policy\s*=\s*\{.*?"
        r"\.light_sleep_enable\s*=\s*automatic_light_sleep",
        "build-mode-selected ESP-IDF Light-sleep policy",
    )
    require_contains(
        power,
        "LUNASHELL::build_mode::kAutomaticLightSleepEnabled",
        "shared Debug/Release sleep policy",
    )
    require_contains(
        power,
        "esp_pm_light_sleep_register_cbs(",
        "automatic Light-sleep residency measurement",
    )
    require_contains(
        power,
        "connected host holds ",
        "USB Serial/JTAG Light-sleep host guard visibility",
    )


# compiler: Existing host C++ compiler selected by the caller, or automatic discovery.
# Raises the original source/compiler/executable failure; artifacts preserve each stage.
def verify_native_owner(compiler: str | None) -> None:
    output = ROOT / "artifacts" / "checks" / "power-owner"
    output.mkdir(parents=True, exist_ok=True)
    power = POWER_SOURCE.read_text(encoding="utf-8")
    signatures = (
        "bool audio_standby_target(",
        "bool codex_standby_link_known(",
        "bool codex_standby_pairing_active(",
        "esp_err_t apply_codex_offline_policy(",
        "esp_err_t wake_from_user_input(",
        "esp_err_t dispatch_power_single_press()",
        "esp_err_t handle_power_key_events(",
        "esp_err_t request_power_off()",
        "bool battery_allows_automatic_standby()",
    )
    extracted = "\n\n".join(function_text(power, signature, str(POWER_SOURCE)) for signature in signatures)
    (output / "power_owner_under_test.inc").write_text(extracted + "\n", encoding="utf-8")
    health = function_text(DEVICE_SNAPSHOT_HEADER.read_text(encoding="utf-8"),
                           "enum class DeviceHealth", str(DEVICE_SNAPSHOT_HEADER))
    (output / "power_health_under_test.inc").write_text(health + ";\n", encoding="utf-8")
    snapshot_header = DEVICE_SNAPSHOT_HEADER.read_text(encoding="utf-8")
    for signature, filename in (
        ("struct ComponentSnapshot", "power_component_under_test.inc"),
        ("struct CodexStandbyObservation", "power_codex_observation_under_test.inc"),
        ("enum class DeviceStateReason", "power_reason_under_test.inc"),
    ):
        declaration = function_text(snapshot_header, signature, str(DEVICE_SNAPSHOT_HEADER))
        (output / filename).write_text(declaration + ";\n", encoding="utf-8")
    executable = output / "power-owner-contract.exe"
    command = compile_command(resolve_compiler(compiler),
        ("scripts/tests/cody_native", "components/LUNASHELL_platform/include",
         "components/LUNASHELL_logger/include", str(output)),
        ("scripts/tests/cody_native/power_owner_contract.cpp",), executable,
        definitions=("CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION=1",
                     "CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX=1",
                     "CONFIG_LUNASHELL_BUILD_WAKE_WORD_STANDBY_LISTENING=1"))
    run_stage(command, output, "compile")
    run_stage([str(executable)], output, "run")


# No parameters: dispatch explicitly selected specification, source and native-owner checks.
# Raises original verification errors; never operates on firmware or physical hardware.
def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run deterministic power-policy and static source checks."
    )
    parser.add_argument("--native-owner", action="store_true",
                        help="Compile and execute verbatim firmware power owner functions with fake peripherals")
    parser.add_argument("--compiler", help="Existing host C++ compiler; nothing is installed")
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
        verify_transaction_fault_injection()
    if not args.model_only:
        verify_source_contracts()
    if args.native_owner:
        verify_native_owner(args.compiler)

    checked = (
        "source contracts"
        if args.source_only
        else "behavioral model"
        if args.model_only
        else "behavioral model and source contracts"
    )
    if args.native_owner:
        checked += " and production C++ owner with fake peripherals"
    print(
        "power policy verification: PASS "
        f"({checked}: inactivity, 50% dim, qualified motion, "
        "bounded PKEY isolation, fault-injected transactional standby, "
        "event-driven idle workers, automatic Light-sleep)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
