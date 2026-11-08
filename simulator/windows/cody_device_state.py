"""Typed local preview state; no desktop measurements impersonate device telemetry."""

from dataclasses import dataclass
from enum import Enum, auto


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
