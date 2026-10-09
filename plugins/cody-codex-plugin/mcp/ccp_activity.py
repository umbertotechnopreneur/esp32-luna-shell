"""Follow the latest user-prompt hook; retain activity metadata, never chat contents."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
import hashlib
from pathlib import PureWindowsPath
import re
import sys
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
    INTERRUPTED = "interrupted"
    ENDED = "ended"
    UNAVAILABLE = "unavailable"


EVENTS = frozenset({
    "SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse",
    "PermissionRequest", "Stop", "Interrupt", "SessionEnd",
})
MAX_SESSIONS = 64
ACTIVITY_LEASE_SECONDS = 120.0
OPAQUE_ID = re.compile(r"^[A-Za-z0-9_.:-]{1,128}$")


# value: Caller-supplied stable identity, never a storage index or display name.
# label: Non-secret field name used in rejection diagnostics.
# Raises ValueError for unsupported or ambiguous identities.
def require_id(value: Any, label: str) -> str:
    if not isinstance(value, str) or OPAQUE_ID.fullmatch(value) is None:
        raise ValueError(f"{label} must be a bounded opaque identifier")
    return value


# cwd: Validated hook working directory, used only as a display label.
def project_label(cwd: str) -> str:
    # This bounded ASCII projection is not an identity or a chat title. The full
    # session digest distinguishes chats even when their display labels match.
    leaf = PureWindowsPath(cwd).name
    label = "".join(char if 32 <= ord(char) <= 126 else "?" for char in leaf).strip()
    if not label:
        print("CCP activity: workspace root uses the Codex display label", file=sys.stderr)
        return "Codex"
    if label != leaf or len(label) > 24:
        print("CCP activity: workspace label projected to bounded ASCII", file=sys.stderr)
    return label if len(label) <= 24 else label[:21].rstrip() + "..."


@dataclass
class Session:
    session_id: str
    cwd: str
    project: str
    phase: Phase = Phase.ACTIVE
    turn_id: str = ""
    last_event_ns: int = 0
    observed_at: float = 0.0
    tools: dict[str, Phase] = field(default_factory=dict)
    interrupted: bool = False


class ActivityStore:
    # No parameters: a new broker waits for a genuine prompt before following a chat.
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._sessions: dict[str, Session] = {}
        self._followed_session_id = ""
        self._last_prompt_ns = 0
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
            if name == "UserPromptSubmit" and emitted_ns == self._last_prompt_ns:
                raise ValueError("Duplicate or ambiguous prompt timestamp rejected")
            session = self._sessions.get(session_id)
            if session is None:
                # Only documented lifecycle boundaries may introduce a chat.
                if name not in {"SessionStart", "UserPromptSubmit"}:
                    raise ValueError("Chat has not been observed at SessionStart or UserPromptSubmit")
                if len(self._sessions) >= MAX_SESSIONS:
                    expired = next((identity for identity, item in self._sessions.items()
                                    if item.phase == Phase.ENDED
                                    and identity != self._followed_session_id), None)
                    if expired is None:
                        raise ValueError("CCP chat registry is full; close unused Codex chats")
                    del self._sessions[expired]
                session = Session(session_id, cwd, project_label(cwd))
                self._sessions[session_id] = session
            if emitted_ns <= session.last_event_ns:
                raise ValueError("Late or duplicate hook event rejected")
            if name == "UserPromptSubmit":
                session.turn_id = turn_id
                session.tools.clear()
                session.interrupted = False
                session.phase = Phase.WORKING
                # Only a user's prompt changes the followed chat. Completion,
                # tools and resume events from background chats cannot steal it.
                # Delayed delivery of an older prompt cannot reverse a switch.
                if emitted_ns > self._last_prompt_ns:
                    self._followed_session_id = session_id
                    self._last_prompt_ns = emitted_ns
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
                    session.phase = Phase.INTERRUPTED if name == "Interrupt" else Phase.PAUSED
                    session.interrupted = name == "Interrupt"
                elif name == "Stop":
                    session.phase = Phase.READY
            if session.cwd != cwd:
                session.project = project_label(cwd)
                session.cwd = cwd
            session.last_event_ns = emitted_ns
            session.observed_at = time.monotonic()
            self._revision += 1

    # No parameters: exposes identities and local context only, never chat contents.
    def list_sessions(self) -> list[dict[str, Any]]:
        with self._lock:
            return [{"session_id": item.session_id, "cwd": item.cwd,
                     "phase": self._effective_phase(item).value,
                     "followed": item.session_id == self._followed_session_id}
                    for item in self._sessions.values()]

    # session: Last genuine hook state, whose active lease must not outlive observation.
    def _effective_phase(self, session: Session) -> Phase:
        if (session.phase in {Phase.WORKING, Phase.EDITING, Phase.RUNNING}
                and time.monotonic() - session.observed_at > ACTIVITY_LEASE_SECONDS):
            return Phase.UNAVAILABLE
        return session.phase

    # No parameters: paired devices mirror this PC's latest user-prompt context.
    def snapshot(self) -> dict[str, str]:
        with self._lock:
            session = self._sessions.get(self._followed_session_id)
            # A full digest preserves opaque chat identity without sending Codex IDs.
            return {
                "chat": hashlib.sha256(session.session_id.encode("ascii")).hexdigest() if session else "0" * 64,
                "revision": f"{self._revision:016x}",
                "phase": self._effective_phase(session).value if session else Phase.NONE.value,
                "project": session.project.encode("ascii").hex() if session else "",
            }
