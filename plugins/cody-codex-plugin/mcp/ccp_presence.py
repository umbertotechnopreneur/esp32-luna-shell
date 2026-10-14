"""Bounded Codex MCP transport presence, independent of chat hooks and LAN liveness."""

from dataclasses import dataclass
from enum import Enum
import math
import threading
import time


LEASE_SECONDS = 60.0
MAX_TRANSPORTS = 64


class AppState(str, Enum):
    UNAVAILABLE = "unavailable"
    OPEN = "open"
    CLOSED = "closed"


@dataclass(frozen=True)
class PresenceObservation:
    state: AppState = AppState.UNAVAILABLE
    reason: str = "unobserved"
    age_ms: int | None = None


class AppPresence:
    # self: One volatile owner shared by the broker and its existing UDP responder.
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._leases: dict[str, float] = {}
        self._state = AppState.UNAVAILABLE
        self._reason = "unobserved"
        self._observed_at: float | None = None
        self._uncertain_loss = False

    # self: Locked lease owner; expiry is absence of evidence, never a normal close.
    # now: Actual monotonic time, already validated by the public boundary.
    def _expire(self, now: float) -> None:
        expired = [identity for identity, observed in self._leases.items()
                   if not 0 <= now - observed < LEASE_SECONDS]
        for identity in expired:
            del self._leases[identity]
        if expired:
            # Another client may still be running after an ambiguous transport loss.
            # Keep this uncertainty bounded to one bit for this broker lifetime.
            self._uncertain_loss = True
            if not self._leases:
                self._state = AppState.UNAVAILABLE
                self._reason = "expired"
                self._observed_at = None

    # self: Existing owner receiving an actual MCP start or renewal, never a chat hook.
    # identity: Validated unique transport identity from the authenticated local envelope.
    # now: Explicit monotonic time; omitted only for production's clock read.
    # Raises ValueError for invalid time or exhausting the bounded active-client budget.
    def lease(self, identity: str, now: float | None = None) -> bool:
        now = time.monotonic() if now is None else now
        if not math.isfinite(now) or now < 0:
            raise ValueError("CCP app presence lease clock is invalid")
        with self._lock:
            self._expire(now)
            new_transport = identity not in self._leases
            if new_transport and len(self._leases) >= MAX_TRANSPORTS:
                raise ValueError("CCP active transport limit reached")
            self._leases[identity] = now
            self._state = AppState.OPEN
            self._reason = "none"
            self._observed_at = now
            return new_transport

    # self: Existing owner releasing only the requesting transport.
    # identity: Validated transport identity; unknown or expired releases cannot close the app.
    # normal: True only after the MCP input stream reaches normal EOF.
    # now: Explicit monotonic time; omitted only for production's clock read.
    # Raises ValueError for invalid input or a release with no current owned lease.
    def release(self, identity: str, normal: bool, now: float | None = None) -> None:
        now = time.monotonic() if now is None else now
        if type(normal) is not bool or not math.isfinite(now) or now < 0:
            raise ValueError("CCP app presence release fields are invalid")
        with self._lock:
            self._expire(now)
            if identity not in self._leases:
                raise ValueError("CCP app presence release has no live owned lease")
            del self._leases[identity]
            if not normal:
                self._uncertain_loss = True
            if self._leases:
                self._observed_at = max(self._leases.values())
                return
            self._state = AppState.CLOSED if normal and not self._uncertain_loss else AppState.UNAVAILABLE
            self._reason = "none" if self._state == AppState.CLOSED else "transport"
            self._observed_at = now if self._state == AppState.CLOSED else None

    # self: Existing owner projected by the broker loop or an authenticated device exchange.
    # now: Explicit monotonic time; omitted only for production's clock read.
    # Raises ValueError for an invalid observation clock.
    def snapshot(self, now: float | None = None) -> PresenceObservation:
        now = time.monotonic() if now is None else now
        if not math.isfinite(now) or now < 0:
            raise ValueError("CCP app presence observation clock is invalid")
        with self._lock:
            self._expire(now)
            if self._state == AppState.UNAVAILABLE:
                return PresenceObservation(self._state, self._reason)
            observed = max(self._leases.values()) if self._leases else self._observed_at
            if observed is None or not 0 <= now - observed <= 4294967.295:
                return PresenceObservation(AppState.UNAVAILABLE, "expired")
            return PresenceObservation(self._state, "none", int((now - observed) * 1000))

