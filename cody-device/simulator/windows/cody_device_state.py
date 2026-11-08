"""Typed local preview state; no desktop measurements impersonate device telemetry."""

from dataclasses import dataclass
from enum import Enum, auto
from math import ceil


class InfoPage(Enum):
    SYSTEM = auto()
    MEMORY = auto()
    FIRMWARE = auto()
    PROJECT_LINK = auto()


INFO_PAGES = tuple(InfoPage)


@dataclass(frozen=True)
class SystemInfoObservation:
    """Only Studio uptime is available from the current simulator source."""

    studio_uptime_seconds: int = 0


@dataclass
class SystemInfoModel:
    page: InfoPage = InfoPage.SYSTEM
    observation: SystemInfoObservation = SystemInfoObservation()

    # self: Local Info navigation, independent of the renderer.
    # step: Previous (-1) or next (1) intent from a visible navigation control.
    # Raises ValueError for unsupported navigation directions.
    # Returns False only when the previous route is outside Info or Next is unavailable.
    def move(self, step: int) -> bool:
        if step not in (-1, 1):
            raise ValueError("Info navigation must move one page")
        destination = INFO_PAGES.index(self.page) + step
        if not 0 <= destination < len(INFO_PAGES):
            return False
        self.page = INFO_PAGES[destination]
        return True


@dataclass
class ScreensaverModel:
    enabled: bool = False
    active: bool = False

    # self: Local screensaver state, with the same eligibility policy as firmware.
    # eligible: Only an awake, unobstructed dashboard admits the screensaver.
    # idle_seconds: Time since actual interaction; rendering never resets it.
    def observe(self, eligible: bool, idle_seconds: float) -> None:
        self.active = self.enabled and eligible and idle_seconds >= 15.0

    # self: Preview state owning the reversible user preference.
    def toggle(self) -> None:
        self.enabled = not self.enabled
        self.active = False


TIMER_MIN_MINUTES = 1
TIMER_MAX_MINUTES = 120


class LocalTimerPhase(Enum):
    IDLE = auto()
    RUNNING = auto()
    FINISHED = auto()


class LocalTimerSound(Enum):
    BELL = "Bell"
    CHIME = "Chime"


@dataclass
class LocalTimerModel:
    duration_minutes: int = 5
    sound: LocalTimerSound = LocalTimerSound.BELL
    phase: LocalTimerPhase = LocalTimerPhase.IDLE
    deadline: float = 0.0

    # self: Timer state retained across navigation and software standby.
    # step: One-minute adjustment from the idle timer controls.
    def adjust(self, step: int) -> None:
        candidate = self.duration_minutes + step
        if self.phase is LocalTimerPhase.IDLE and step in (-1, 1) and TIMER_MIN_MINUTES <= candidate <= TIMER_MAX_MINUTES:
            self.duration_minutes = candidate

    # self: Timer configuration whose chosen sound is represented in Studio.
    def cycle_sound(self) -> None:
        if self.phase is LocalTimerPhase.IDLE:
            self.sound = LocalTimerSound.CHIME if self.sound is LocalTimerSound.BELL else LocalTimerSound.BELL

    # self: Timer state admitting a start only while idle.
    # now: Monotonic time at activation.
    def start(self, now: float) -> None:
        if self.phase is LocalTimerPhase.IDLE:
            self.deadline = now + self.duration_minutes * 60
            self.phase = LocalTimerPhase.RUNNING

    # self: Running timer stopped by the Cancel action.
    def cancel(self) -> None:
        if self.phase is LocalTimerPhase.RUNNING:
            self.phase = LocalTimerPhase.IDLE
            self.deadline = 0.0

    # self: Completed timer acknowledged by the Done action.
    def dismiss(self) -> None:
        if self.phase is LocalTimerPhase.FINISHED:
            self.phase = LocalTimerPhase.IDLE
            self.deadline = 0.0

    # self: Preview timer advanced independently of the displayed route.
    # now: Monotonic time sampled by the preview scheduler.
    # Returns True only when this tick finishes the timer.
    def observe(self, now: float) -> bool:
        if self.phase is LocalTimerPhase.RUNNING and now >= self.deadline:
            self.phase = LocalTimerPhase.FINISHED
            return True
        return False

    # self: Timer configuration and elapsed state borrowed by presentation.
    # now: Monotonic time shared with the current preview frame.
    def remaining_seconds(self, now: float) -> int:
        if self.phase is LocalTimerPhase.IDLE:
            return self.duration_minutes * 60
        if self.phase is LocalTimerPhase.RUNNING:
            return max(0, ceil(self.deadline - now))
        return 0
