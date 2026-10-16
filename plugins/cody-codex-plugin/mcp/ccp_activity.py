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

"""Follow the latest user-prompt hook; retain activity metadata, never chat contents."""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from enum import Enum
import hashlib
from pathlib import PureWindowsPath
import re
import sqlite3
import sys
import threading
import time
from typing import Any
from ccp_pricing import PriceCache, PriceProjection
from ccp_usage import NativeUsageReader, safe_usage_error
from ccp_chat_title import ChatTitleObservation, NativeChatTitleReader, TITLE_LEASE_SECONDS, TitleState
from ccp_tool_labels import MAX_TOOL_LABEL_LENGTH, tool_display_label


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


class Compaction(str, Enum):
    NONE = "none"
    ACTIVE = "active"
    REFRESHED = "refreshed"
    ORPHANED = "orphaned"
    INTERRUPTED = "interrupted"
    STALE = "stale"


EVENTS = frozenset({
    "SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse",
    "PermissionRequest", "Stop", "Interrupt", "SessionEnd", "PreCompact", "PostCompact",
})
MAX_SESSIONS = 64
ACTIVITY_LEASE_SECONDS = 120.0
MAX_DESCRIPTION_LENGTH = MAX_TOOL_LABEL_LENGTH
OPAQUE_ID = re.compile(r"^[A-Za-z0-9_.:-]{1,128}$")


# value: Optional genuine hook model slug; absence remains unavailable.
# Raises ValueError for unsupported or unrepresentable reported model metadata.
def hook_model(value: Any) -> str:
    if value is None:
        return ""
    if (not isinstance(value, str) or not 1 <= len(value) <= 128
            or any(not 33 <= ord(character) <= 126 for character in value)):
        raise ValueError("Hook model must contain 1-128 printable ASCII characters without spaces")
    return value


# seconds: Nonnegative duration or age measured only within the PC monotonic clock domain.
# Returns an explicit unavailable token when the protocol's uint32 millisecond range cannot represent it.
def wire_milliseconds(seconds: float) -> str:
    milliseconds = int(seconds * 1000)
    if not 0 <= milliseconds <= 0xffffffff:
        print("CCP technical duration unavailable: outside uint32 milliseconds", file=sys.stderr, flush=True)
        return "-"
    return str(milliseconds)


# tool_name: Genuine hook tool identity, never command text or model-generated intent.
# Raises ValueError when the native tool identity is empty or outside its bounded ASCII contract.
def tool_phase(tool_name: Any) -> Phase:
    if (not isinstance(tool_name, str) or not tool_name or len(tool_name) > 256
            or any(not 32 <= ord(character) <= 126 for character in tool_name)):
        raise ValueError("Hook tool_name must be bounded printable ASCII")
    # Namespaces are native tool identities, not clues extracted from arguments.
    leaf = tool_name.rsplit(".", 1)[-1]
    if leaf == "apply_patch":
        return Phase.EDITING
    if leaf in {"Bash", "exec_command", "write_stdin", "shell_command"}:
        return Phase.RUNNING
    return Phase.WORKING


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
    if not leaf:
        print("CCP activity: workspace root uses the Codex display label", file=sys.stderr)
        return "Codex"
    if len(leaf) > 64 or any(not 32 <= ord(character) <= 126 for character in leaf):
        print("CCP activity: workspace label unavailable outside 1-64 printable ASCII characters", file=sys.stderr)
        return "UNAVAILABLE"
    return leaf


@dataclass
class Session:
    session_id: str
    cwd: str
    project: str
    branch: str = ""
    phase: Phase = Phase.ACTIVE
    turn_id: str = ""
    last_event_ns: int = 0
    observed_at: float = 0.0
    tools: dict[str, Phase] = field(default_factory=dict)
    tool_labels: dict[str, str] = field(default_factory=dict)
    interrupted: bool = False
    description: str = ""
    description_observed_at: float = 0.0
    model: str = ""
    model_observed_at: float = 0.0
    turn_started_at: float = 0.0
    turn_ended_at: float = 0.0
    compaction: Compaction = Compaction.NONE
    compact_sequence: int = 0
    compact_observed_at: float = 0.0
    compact_trigger: str = ""
    compact_last_event: str = ""
    transcript_path: str = ""
    reply_notice_turn: str = ""
    chat_title: ChatTitleObservation = field(default_factory=ChatTitleObservation)


@dataclass(frozen=True)
class HookObservation:
    event: str
    session_id: str
    turn_id: str
    project: str
    followed: bool
    phase: Phase
    reply_ready: bool
    title: str = ""


# title: Original canonical native metadata, including its exact observation timestamp.
# now: Current monotonic source clock, never a hook or presentation timestamp.
# Returns a complete displayable notice title or empty; never truncates or renews old metadata.
def reply_notice_title(title: ChatTitleObservation, now: float) -> str:
    age = now - title.observed_at if title.observed_at is not None else None
    return (title.title if title.state == TitleState.CURRENT and age is not None
            and 0 <= age < TITLE_LEASE_SECONDS and 1 <= len(title.title) <= 63 else "")


class ActivityStore:
    # self: New broker waits for a genuine prompt before following a chat.
    # pricing: Optional memory-only catalog owner; absence leaves the qualitative band Unknown.
    def __init__(self, pricing: PriceCache | None = None) -> None:
        self._lock = threading.Lock()
        self._pricing = pricing
        self._sessions: dict[str, Session] = {}
        self._followed_session_id = ""
        self._last_prompt_ns = 0
        self._revision = 0
        self._usage = NativeUsageReader()
        self._titles = NativeChatTitleReader()
        self._title_signature = ("", "", TitleState.UNAVAILABLE)
        self._usage_lock = threading.Lock()
        self._usage_error = ""

    # self: Canonical owner of bounded native hook identities and accepted lifecycle state.
    # event: Allowlisted metadata from one genuine Codex hook invocation.
    # Raises ValueError for malformed, stale or contradictory event metadata.
    def observe(self, event: dict[str, Any]) -> HookObservation:
        name = event.get("hook_event_name")
        if name not in EVENTS:
            raise ValueError("Unsupported CCP hook event")
        session_id = require_id(event.get("session_id"), "session_id")
        cwd = event.get("cwd")
        if not isinstance(cwd, str) or not cwd or len(cwd) > 4096:
            raise ValueError("Hook cwd must be a bounded nonempty path")
        branch = event.get("branch", "")
        if (not isinstance(branch, str) or len(branch) > 64
                or any(not 32 <= ord(character) <= 126 for character in branch)):
            raise ValueError("Hook branch must be bounded printable ASCII checkout metadata")
        emitted_ns = event.get("emitted_ns")
        if type(emitted_ns) is not int or not 0 < emitted_ns <= time.monotonic_ns():
            raise ValueError("Hook emission timestamp is invalid")
        turn_id = ""
        if name not in {"SessionStart", "SessionEnd"}:
            turn_id = require_id(event.get("turn_id"), "turn_id")
        model = hook_model(event.get("model"))
        locator = event.get("transcript_path", "")
        if not isinstance(locator, str) or len(locator) > 4096 or "\x00" in locator:
            raise ValueError("Native transcript locator is invalid")
        trigger = event.get("trigger")
        if name in {"PreCompact", "PostCompact"} and (not isinstance(trigger, str) or trigger not in {"auto", "manual"}):
            raise ValueError("Compaction trigger must be the genuine auto or manual hook value")
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
                new_turn = turn_id != session.turn_id
                if new_turn:
                    session.turn_started_at = emitted_ns / 1_000_000_000
                    session.turn_ended_at = 0.0
                    session.tools.clear()
                    session.tool_labels.clear()
                    session.model = ""
                    session.model_observed_at = 0.0
                    session.compaction = Compaction.NONE
                    session.compact_sequence = 0
                    session.compact_observed_at = 0.0
                    session.compact_trigger = ""
                    session.compact_last_event = ""
                    session.reply_notice_turn = ""
                session.turn_id = turn_id
                session.interrupted = False
                session.description = ""
                session.description_observed_at = 0.0
                session.phase = next(reversed(session.tools.values()), Phase.WORKING)
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
                    session.tool_labels.clear()
                    session.model = ""
                    session.model_observed_at = 0.0
                    session.turn_started_at = 0.0
                    session.turn_ended_at = 0.0
                    session.compaction = Compaction.NONE
                    session.compact_sequence = 0
                    session.compact_observed_at = 0.0
                    session.compact_trigger = ""
                    session.compact_last_event = ""
            elif name == "SessionEnd":
                session.phase = Phase.ENDED
                session.tools.clear()
                session.tool_labels.clear()
                session.description = ""
                if session.turn_started_at and not session.turn_ended_at:
                    session.turn_ended_at = emitted_ns / 1_000_000_000
                if session.compaction == Compaction.ACTIVE:
                    session.compaction = Compaction.INTERRUPTED
                    session.compact_observed_at = emitted_ns / 1_000_000_000
            else:
                if turn_id != session.turn_id or session.phase == Phase.ENDED:
                    # Hashes diagnose real hook correlation without persisting chat contents or IDs.
                    expected = hashlib.sha256(session.turn_id.encode("ascii")).hexdigest()[:12]
                    actual = hashlib.sha256(turn_id.encode("ascii")).hexdigest()[:12]
                    print(f"CCP activity rejected {name}: expected_turn={expected} observed_turn={actual}",
                          file=sys.stderr, flush=True)
                    raise ValueError("Hook turn does not match the current chat turn")
                if session.interrupted and name not in {"PostToolUse", "Interrupt"}:
                    raise ValueError("Interrupted turn cannot resume without a new prompt")
                if name == "PreToolUse":
                    tool_id = require_id(event.get("tool_use_id"), "tool_use_id")
                    if tool_id in session.tools or len(session.tools) >= 64:
                        raise ValueError("Duplicate tool identity or too many concurrent tools")
                    phase = tool_phase(event.get("tool_name"))
                    session.tools[tool_id] = phase
                    session.tool_labels[tool_id] = tool_display_label(event["tool_name"])
                    session.phase = phase
                elif name == "PostToolUse":
                    tool_id = require_id(event.get("tool_use_id"), "tool_use_id")
                    if tool_id not in session.tools:
                        raise ValueError("Tool completion has no matching start")
                    del session.tools[tool_id]
                    del session.tool_labels[tool_id]
                    # A late command completion cannot undo Stop/Interrupt.
                    if session.phase != Phase.READY and not session.interrupted:
                        session.phase = next(reversed(session.tools.values()), Phase.WORKING)
                elif name == "PermissionRequest" or name == "Interrupt":
                    session.phase = Phase.INTERRUPTED if name == "Interrupt" else Phase.PAUSED
                    session.interrupted = name == "Interrupt"
                    session.description = ""
                    if name == "Interrupt":
                        session.turn_ended_at = emitted_ns / 1_000_000_000
                        if session.compaction == Compaction.ACTIVE:
                            session.compaction = Compaction.INTERRUPTED
                            session.compact_observed_at = emitted_ns / 1_000_000_000
                elif name == "Stop":
                    session.phase = Phase.READY
                    session.description = ""
                    session.turn_ended_at = emitted_ns / 1_000_000_000
                    if session.compaction == Compaction.ACTIVE:
                        session.compaction = Compaction.ORPHANED
                        session.compact_observed_at = emitted_ns / 1_000_000_000
                        print("CCP compaction updates lost: turn stopped without PostCompact", file=sys.stderr, flush=True)
                elif name == "PreCompact":
                    if session.phase == Phase.READY:
                        raise ValueError("Completed turn cannot start compaction without a new prompt")
                    if session.compaction == Compaction.ACTIVE:
                        raise ValueError("Duplicate compaction start rejected for the current turn")
                    session.compact_sequence += 1
                    session.compaction = Compaction.ACTIVE
                    session.compact_trigger = trigger
                    session.compact_last_event = name
                    session.compact_observed_at = emitted_ns / 1_000_000_000
                elif name == "PostCompact":
                    if session.compact_last_event == name:
                        raise ValueError("Duplicate compaction completion rejected for the current turn")
                    if session.compaction != Compaction.ACTIVE:
                        session.compact_sequence += 1
                        session.compaction = Compaction.ORPHANED
                        print("CCP compaction completion unavailable: no matching observed start", file=sys.stderr, flush=True)
                    elif trigger != session.compact_trigger:
                        raise ValueError("Compaction completion trigger does not match its observed start")
                    else:
                        session.compaction = Compaction.REFRESHED
                    session.compact_last_event = name
                    session.compact_observed_at = emitted_ns / 1_000_000_000
                    # The final hook confirms compaction only; it supplies no refreshed usage counters.
            if model and (name == "UserPromptSubmit" or turn_id == session.turn_id and turn_id):
                session.model = model
                session.model_observed_at = emitted_ns / 1_000_000_000
            if session.cwd != cwd:
                session.project = project_label(cwd)
                session.cwd = cwd
                session.chat_title = ChatTitleObservation()
            session.last_event_ns = emitted_ns
            if locator:
                session.transcript_path = locator
            session.branch = branch
            session.observed_at = emitted_ns / 1_000_000_000
            self._revision += 1
            # The notice decision is tied to the accepted native turn, not a
            # later followed-chat snapshot or inferred response text.
            reply_ready = name == "Stop" and session.reply_notice_turn != session.turn_id
            if reply_ready:
                session.reply_notice_turn = session.turn_id
            if name in {"UserPromptSubmit", "Stop", "PreCompact", "PostCompact", "Interrupt", "SessionEnd"}:
                # Trace only genuine accepted lifecycle boundaries. Tool hooks
                # remain quiet; opaque digests keep native IDs and chat text local.
                chat_digest = hashlib.sha256(session_id.encode("ascii")).hexdigest()[:12]
                turn_digest = hashlib.sha256(session.turn_id.encode("ascii")).hexdigest()[:12]
                print(f"CCP lifecycle accepted: event={name} chat={chat_digest} turn={turn_digest} "
                      f"phase={session.phase.value} compaction={session.compaction.value} "
                      f"compactseq={session.compact_sequence:016x}", file=sys.stderr, flush=True)
            # Event acceptance performs no source IO. The delivery owner may
            # refresh this exact background completion through the title owner.
            notice_title = reply_notice_title(session.chat_title, time.monotonic())
            return HookObservation(name, session_id, session.turn_id, session.project,
                                   session_id == self._followed_session_id, session.phase, reply_ready, notice_title)

    # self: Canonical lifecycle owner; its existing source lock serializes the one metadata read.
    # observation: First genuine background Stop accepted for its exact native session and turn.
    # Returns refreshed immutable notice metadata, or cancels an obsolete completion after a race.
    # Source errors remain explicit in session.chat_title and are logged by the title acquisition owner.
    def refresh_reply_title(self, observation: HookObservation) -> HookObservation:
        if observation.event != "Stop" or not observation.reply_ready or observation.followed:
            return observation
        with self._lock:
            session = self._sessions.get(observation.session_id)
            if (session is None or session.turn_id != observation.turn_id or session.phase != Phase.READY
                    or session.session_id == self._followed_session_id):
                return replace(observation, reply_ready=False, title="")
            cwd = session.cwd
        # Do not hold the hook-state lock during SQLite IO; do not ask the
        # usage reader for a journal or change which session Cody follows.
        with self._usage_lock:
            title = self._titles.read_once(observation.session_id, cwd)
        with self._lock:
            current = self._sessions.get(observation.session_id)
            if (current is not session or current.turn_id != observation.turn_id or current.cwd != cwd
                    or current.phase != Phase.READY or current.session_id == self._followed_session_id):
                return replace(observation, reply_ready=False, title="")
            current.chat_title = title
            if title.state == TitleState.CURRENT and len(title.title) > 63:
                print("CCP reply title unavailable: complete native title exceeds the 63-byte notice field",
                      file=sys.stderr, flush=True)
            return replace(observation, title=reply_notice_title(title, time.monotonic()))

    # self: Existing lifecycle/title acquisition owner; no selected-chat metadata is substituted.
    # observation: Accepted PermissionRequest bound to its genuine native session and turn.
    # Returns the full originating ASCII title, or explicit absence; a raced permission is retired.
    # Source failures retain their original state and safe diagnostic in the acquisition owner.
    def refresh_approval_title(self, observation: HookObservation) -> HookObservation:
        if observation.event != "PermissionRequest" or not observation.followed or observation.phase != Phase.PAUSED:
            return observation
        with self._lock:
            session = self._sessions.get(observation.session_id)
            if (session is None or session.turn_id != observation.turn_id or session.phase != Phase.PAUSED
                    or session.session_id != self._followed_session_id):
                return replace(observation, followed=False, title="")
            cwd = session.cwd
        # Reuse the established metadata owner and release the lifecycle lock during IO.
        with self._usage_lock:
            title = self._titles.read_once(observation.session_id, cwd)
        with self._lock:
            current = self._sessions.get(observation.session_id)
            if (current is not session or current.turn_id != observation.turn_id or current.cwd != cwd
                    or current.phase != Phase.PAUSED or current.session_id != self._followed_session_id):
                return replace(observation, followed=False, title="")
            current.chat_title = title
            age = time.monotonic() - title.observed_at if title.observed_at is not None else None
            complete = (title.title if title.state == TitleState.CURRENT and age is not None
                        and 0 <= age < TITLE_LEASE_SECONDS else "")
            return replace(observation, title=complete)

    # No parameters: exposes identities and local context only, never chat contents.
    def list_sessions(self) -> list[dict[str, Any]]:
        with self._lock:
            return [{"session_id": item.session_id, "cwd": item.cwd,
                     "turn_id": item.turn_id,
                     "phase": self._effective_phase(item).value,
                     "followed": item.session_id == self._followed_session_id}
                    for item in self._sessions.values()]

    # session_id: Hook-observed chat identity; only the currently followed chat may report progress.
    # turn_id: Exact current hook turn identity, rejecting delayed and background reports.
    # description: Agent-authored actual purpose in bounded printable English ASCII, without commands or logs.
    # Raises ValueError for stale identities, inactive work or unsupported description bytes.
    def report_progress(self, session_id: Any, turn_id: Any, description: Any) -> dict[str, str]:
        session_id = require_id(session_id, "session_id")
        turn_id = require_id(turn_id, "turn_id")
        if (not isinstance(description, str) or not 1 <= len(description) <= MAX_DESCRIPTION_LENGTH
                or description != description.strip()
                or any(not 32 <= ord(character) <= 126 for character in description)):
            raise ValueError("Progress description must contain 1-192 printable ASCII characters")
        with self._lock:
            session = self._sessions.get(session_id)
            if (session is None or session_id != self._followed_session_id or session.turn_id != turn_id
                    or self._effective_phase(session) not in {Phase.WORKING, Phase.EDITING, Phase.RUNNING}):
                raise ValueError("Progress requires the followed chat's current active hook turn")
            session.description = description
            session.description_observed_at = time.monotonic()
            self._revision += 1
            return {"status": "reported", "description": description}

    # session: Last genuine hook state, whose active lease must not outlive observation.
    def _effective_phase(self, session: Session) -> Phase:
        if (session.phase in {Phase.WORKING, Phase.EDITING, Phase.RUNNING}
                and time.monotonic() - session.observed_at > ACTIVITY_LEASE_SECONDS):
            return Phase.UNAVAILABLE
        return session.phase

    # No parameters: paired devices mirror this PC's latest user-prompt context.
    def snapshot(self) -> dict[str, str]:
        return self.snapshot_bundle()[0]

    # self: Canonical hook owner; title and usage acquisition share its existing bounded source IO lock.
    # No other parameters: obtains coherent activity and technical projections for one authenticated FIND reply.
    def snapshot_bundle(self) -> tuple[dict[str, str], dict[str, str]]:
        # File IO has its own owner lock and never holds the activity/hook lock.
        with self._lock:
            followed = self._sessions.get(self._followed_session_id)
            source = ((followed.session_id, followed.cwd, followed.turn_id, followed.transcript_path)
                      if followed and followed.turn_id and followed.phase != Phase.ENDED else None)
        usage, context = None, None
        title = ChatTitleObservation()
        if source is not None:
            with self._usage_lock:
                title = self._titles.poll(source[0], source[1])
                try:
                    usage, context = self._usage.poll(*source)
                    self._usage_error = ""
                except (OSError, ValueError, KeyError, TypeError, sqlite3.Error) as error:
                    # Do not echo native file paths, JSON lines or chat content.
                    cause = safe_usage_error(error)
                    if cause != self._usage_error:
                        print(f"CCP native usage unavailable: {cause}", file=sys.stderr, flush=True)
                        self._usage_error = cause
        with self._lock:
            session = self._sessions.get(self._followed_session_id)
            if (session is None or source != (session.session_id, session.cwd, session.turn_id, session.transcript_path)
                    or session.phase == Phase.ENDED):
                usage, context = None, None
                title = ChatTitleObservation()
            now = time.monotonic()
            phase = self._effective_phase(session) if session else Phase.NONE
            description = (session.description if session and
                phase in {Phase.WORKING, Phase.EDITING, Phase.RUNNING} and
                now - session.description_observed_at <= ACTIVITY_LEASE_SECONDS else "")
            if not description and session and session.tools and phase in {Phase.WORKING, Phase.EDITING, Phase.RUNNING}:
                # Only the current native tool identity is exposed; arguments and shell commands stay on the PC.
                description = session.tool_labels[next(reversed(session.tools))]
            title_age = wire_milliseconds(now - title.observed_at) if title.observed_at is not None else "-"
            if title.state == TitleState.CURRENT and title_age == "-":
                title = ChatTitleObservation(state=TitleState.ERROR)
            if session is not None:
                session.chat_title = title
            title_signature = (session.session_id if session else "", title.title, title.state)
            if title_signature != self._title_signature:
                # Renaming a thread is a metadata change even without a new
                # prompt. Cached rereads and age alone do not restart the UI.
                self._title_signature = title_signature
                self._revision += 1
            # Full digests preserve opaque identity without exposing native Codex identifiers.
            identity = {"chat": hashlib.sha256(session.session_id.encode("ascii")).hexdigest() if session else "0" * 64,
                        "revision": f"{self._revision:016x}"}
            activity = {**identity, "phase": phase.value,
                        "project": session.project.encode("ascii").hex() if session else "",
                        "description": description.encode("ascii").hex(),
                        "branch": session.branch.encode("ascii").hex() if session else "",
                        "title": title.title.encode("ascii").hex() if title.state == TitleState.CURRENT else "-",
                        "titlestate": title.state.value,
                        "titleage": title_age}
            compact = session.compaction if session else Compaction.NONE
            # Replies preserve the genuine lifecycle timestamp. A broker heartbeat is not
            # a fresh Codex observation that compaction is still running beyond its source lease.
            if (compact == Compaction.ACTIVE and session and
                    now - session.compact_observed_at > ACTIVITY_LEASE_SECONDS):
                compact = Compaction.STALE
            technical = {**identity,
                "turn": hashlib.sha256(session.turn_id.encode("ascii")).hexdigest() if session and session.turn_id else "0" * 64,
                "model": session.model.encode("ascii").hex() if session and session.model else "-",
                "modelage": wire_milliseconds(now - session.model_observed_at) if session and session.model else "-",
                "compaction": compact.value,
                "compactseq": f"{session.compact_sequence:016x}" if session else "0" * 16,
                "compactage": wire_milliseconds(now - session.compact_observed_at) if session and session.compact_sequence else "-",
                "activityage": wire_milliseconds(now - session.observed_at) if session else "-",
                "turnelapsed": wire_milliseconds((session.turn_ended_at or now) - session.turn_started_at)
                    if session and session.turn_started_at else "-",
                # Missing native fields stay unavailable; no prompt-based estimates or defaults.
                **{key: "-" for key in ("effort", "used", "capacity", "cumulative", "input", "output", "threshold", "usageage")}}
            if (usage is not None and session is not None and session.compaction == Compaction.REFRESHED
                    and usage[1] < session.compact_observed_at):
                # PostCompact confirms completion, not renewed token counters.
                # Wait for a native observation after that genuine boundary.
                usage = None
            if usage is not None:
                values, observed = usage
                age = wire_milliseconds(now - observed)
                if age != "-":
                    technical.update({key: str(value) for key, value in values.items()})
                    technical["usageage"] = age
            effective_model = session.model if session else ""
            effective_model_observed = session.model_observed_at if session else 0.0
            if context is not None:
                native_model, effort, observed = context
                age = wire_milliseconds(now - observed)
                hook_is_current = bool(effective_model and
                                       0 <= now - effective_model_observed < ACTIVITY_LEASE_SECONDS)
                # An expired turn-context observation cannot hide a newer genuine
                # hook observation for this exact chat/turn. Keep each source's
                # original age; effort has no independent wire age and therefore
                # remains unavailable when the hook supplies the selected model.
                use_native = now - observed < ACTIVITY_LEASE_SECONDS or not hook_is_current
                if age != "-" and use_native:
                    effective_model, effective_model_observed = native_model, observed
                    technical.update(model=native_model.encode("ascii").hex(), modelage=age,
                                     effort=effort.encode("ascii").hex() if effort else "-")
            if technical["modelage"] == "-":
                technical["model"] = "-"
            price = (self._pricing.projection(effective_model)
                     if self._pricing is not None and session is not None and technical["model"] != "-"
                     and now - effective_model_observed < ACTIVITY_LEASE_SECONDS else PriceProjection())
            technical.update(cost=price.band.value,
                             costage=str(price.age_ms) if price.age_ms is not None else "-",
                             costttl=str(price.valid_for_ms) if price.valid_for_ms is not None else "-")
            return activity, technical
