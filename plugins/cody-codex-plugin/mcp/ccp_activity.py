"""Bounded chat selection and hook-derived activity; no transcript or prompt storage."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
import hashlib
import re
import threading
import time
from typing import Any


class Phase(str, Enum):
    NONE = "none"
    ACTIVE = "active"
    WORKING = "working"
    EDITING = "editing"
    RUNNING = "running"
    READY = "ready"
    PAUSED = "paused"
    ENDED = "ended"
    UNAVAILABLE = "unavailable"


EVENTS = frozenset({
    "SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse",
    "PermissionRequest", "Stop", "Interrupt", "SessionEnd",
})
MAX_SESSIONS = 64
ACTIVITY_LEASE_SECONDS = 120.0
OPAQUE_ID = re.compile(r"^[A-Za-z0-9_.:-]{1,128}$")
DEVICE_ID = re.compile(r"^[0-9a-f]{12}$")


# value: Caller-supplied stable identity, never a storage index or display name.
# label: Non-secret field name used in rejection diagnostics.
# Raises ValueError for unsupported or ambiguous identities.
def require_id(value: Any, label: str) -> str:
    if not isinstance(value, str) or OPAQUE_ID.fullmatch(value) is None:
        raise ValueError(f"{label} must be a bounded opaque identifier")
    return value


# value: Explicit user-facing label for the physical display.
# label: Field name used in rejection diagnostics.
# Raises ValueError for empty, non-ASCII or overlong display labels.
def require_label(value: Any, label: str) -> str:
    if (not isinstance(value, str) or not 1 <= len(value) <= 24
            or value != value.strip() or any(not 32 <= ord(char) <= 126 for char in value)):
        raise ValueError(f"{label} must contain 1-24 printable ASCII characters without edge spaces")
    return value


@dataclass
class Session:
    session_id: str
    cwd: str
    phase: Phase = Phase.ACTIVE
    turn_id: str = ""
    last_event_ns: int = 0
    observed_at: float = 0.0
    tools: dict[str, Phase] = field(default_factory=dict)
    interrupted: bool = False


@dataclass(frozen=True)
class Binding:
    session_id: str
    project: str


class ActivityStore:
    # No parameters: volatile selection is deliberately not restored after broker restart.
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._sessions: dict[str, Session] = {}
        self._bindings: dict[str, Binding] = {}
        self._revision = 0

    # event: Allowlisted metadata from one genuine Codex hook invocation.
    # Raises ValueError for malformed, stale or contradictory event metadata.
    def observe(self, event: dict[str, Any]) -> None:
        name = event.get("hook_event_name")
        if name not in EVENTS:
            raise ValueError("Unsupported CCP hook event")
        session_id = require_id(event.get("session_id"), "session_id")
        cwd = event.get("cwd")
        if not isinstance(cwd, str) or not cwd or len(cwd) > 4096:
            raise ValueError("Hook cwd must be a bounded nonempty path")
        emitted_ns = event.get("emitted_ns")
        if type(emitted_ns) is not int or not 0 < emitted_ns <= time.monotonic_ns():
            raise ValueError("Hook emission timestamp is invalid")
        turn_id = ""
        if name not in {"SessionStart", "SessionEnd"}:
            turn_id = require_id(event.get("turn_id"), "turn_id")
        with self._lock:
            session = self._sessions.get(session_id)
            if session is None:
                # Only documented lifecycle boundaries may introduce a chat.
                if name not in {"SessionStart", "UserPromptSubmit"}:
                    raise ValueError("Chat has not been observed at SessionStart or UserPromptSubmit")
                if len(self._sessions) >= MAX_SESSIONS:
                    expired = next((identity for identity, item in self._sessions.items()
                                    if item.phase == Phase.ENDED and all(
                                        binding.session_id != identity for binding in self._bindings.values())), None)
                    if expired is None:
                        raise ValueError("CCP chat registry is full; restart after detaching unused chats")
                    del self._sessions[expired]
                session = Session(session_id, cwd)
                self._sessions[session_id] = session
            if emitted_ns <= session.last_event_ns:
                raise ValueError("Late or duplicate hook event rejected")
            if name == "UserPromptSubmit":
                session.turn_id = turn_id
                session.tools.clear()
                session.interrupted = False
                session.phase = Phase.WORKING
            elif name == "SessionStart":
                # Resume/compaction does not erase an in-progress or completed turn.
                if session.phase == Phase.ENDED:
                    session.phase = Phase.ACTIVE
                    session.turn_id = ""
                    session.tools.clear()
            elif name == "SessionEnd":
                session.phase = Phase.ENDED
                session.tools.clear()
            else:
                if turn_id != session.turn_id or session.phase == Phase.ENDED:
                    raise ValueError("Hook turn does not match the current chat turn")
                if session.interrupted and name not in {"PostToolUse", "Interrupt"}:
                    raise ValueError("Interrupted turn cannot resume without a new prompt")
                if name == "PreToolUse":
                    tool_id = require_id(event.get("tool_use_id"), "tool_use_id")
                    if tool_id in session.tools or len(session.tools) >= 64:
                        raise ValueError("Duplicate tool identity or too many concurrent tools")
                    tool_name = event.get("tool_name")
                    phase = (Phase.EDITING if tool_name == "apply_patch" else
                             Phase.RUNNING if tool_name == "Bash" else Phase.WORKING)
                    session.tools[tool_id] = phase
                    session.phase = phase
                elif name == "PostToolUse":
                    tool_id = require_id(event.get("tool_use_id"), "tool_use_id")
                    if tool_id not in session.tools:
                        raise ValueError("Tool completion has no matching start")
                    del session.tools[tool_id]
                    # A late command completion cannot undo Stop/Interrupt.
                    if session.phase != Phase.READY and not session.interrupted:
                        session.phase = next(reversed(session.tools.values()), Phase.WORKING)
                elif name == "PermissionRequest" or name == "Interrupt":
                    session.phase = Phase.PAUSED
                    session.interrupted = name == "Interrupt"
                elif name == "Stop":
                    session.phase = Phase.READY
            session.cwd = cwd
            session.last_event_ns = emitted_ns
            session.observed_at = time.monotonic()
            self._revision += 1

    # No parameters: exposes identities and local context only, never chat contents.
    def list_sessions(self) -> list[dict[str, Any]]:
        with self._lock:
            return [{"session_id": item.session_id, "cwd": item.cwd,
                     "phase": self._effective_phase(item).value}
                    for item in self._sessions.values()]

    # session: Last genuine hook state, whose active lease must not outlive observation.
    def _effective_phase(self, session: Session) -> Phase:
        if (session.phase in {Phase.WORKING, Phase.EDITING, Phase.RUNNING}
                and time.monotonic() - session.observed_at > ACTIVITY_LEASE_SECONDS):
            return Phase.UNAVAILABLE
        return session.phase

    # device_id: Exact paired Cody identity already checked by the service.
    # session_id: Exact observed chat selected explicitly by the user.
    # project: Explicit ASCII display label; never used as identity.
    # Raises ValueError when either identity or the display label is invalid.
    def bind(self, device_id: str, session_id: str, project: str) -> None:
        if DEVICE_ID.fullmatch(device_id) is None:
            raise ValueError("Malformed Cody identity")
        require_id(session_id, "session_id")
        require_label(project, "project")
        with self._lock:
            if session_id not in self._sessions or self._sessions[session_id].phase == Phase.ENDED:
                raise ValueError("Select an observed, non-ended chat")
            self._bindings[device_id] = Binding(session_id, project)
            self._revision += 1

    # device_id: Exact paired Cody whose explicit association is being removed.
    # Raises ValueError when no association exists.
    def unbind(self, device_id: str) -> None:
        with self._lock:
            if device_id not in self._bindings:
                raise ValueError("Cody has no selected chat")
            del self._bindings[device_id]
            self._revision += 1

    # device_id: Authenticated Cody requesting its latest selected-chat snapshot.
    def snapshot(self, device_id: str) -> dict[str, str]:
        with self._lock:
            binding = self._bindings.get(device_id)
            session = self._sessions[binding.session_id] if binding else None
            # A full digest preserves opaque chat identity without sending Codex IDs.
            return {
                "chat": hashlib.sha256(binding.session_id.encode("ascii")).hexdigest() if binding else "0" * 64,
                "revision": f"{self._revision:016x}",
                "phase": self._effective_phase(session).value if session else Phase.NONE.value,
                "project": binding.project.encode("ascii").hex() if binding else "",
            }
