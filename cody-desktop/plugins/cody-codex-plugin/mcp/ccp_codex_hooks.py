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

"""Admission and translation for the documented Codex command-hook payloads."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import re
from typing import Any


class HookEvent(str, Enum):
    SESSION_START = "SessionStart"
    PROMPT = "UserPromptSubmit"
    TOOL_START = "PreToolUse"
    TOOL_END = "PostToolUse"
    PERMISSION = "PermissionRequest"
    STOP = "Stop"
    INTERRUPT = "Interrupt"
    SESSION_END = "SessionEnd"
    COMPACT_START = "PreCompact"
    COMPACT_END = "PostCompact"


class ToolKind(str, Enum):
    EDIT = "edit"
    COMMAND = "command"
    OTHER = "other"


EVENTS = frozenset(event.value for event in HookEvent)
OPAQUE_ID = re.compile(r"^[A-Za-z0-9_.:-]{1,128}$")
QUESTION_REQUEST_ID = re.compile(r"[0-9a-f]{32}")
QUESTION_TOOL_NAME = "mcp__ccp__ccp_ask_question"
COMMON_FIELDS = ("hook_event_name", "session_id", "cwd", "turn_id", "model", "transcript_path")
EVENT_FIELDS = {
    HookEvent.SESSION_START: ("source",),
    HookEvent.TOOL_START: ("tool_name", "tool_use_id"),
    HookEvent.TOOL_END: ("tool_name", "tool_use_id"),
    HookEvent.STOP: ("stop_hook_active",),
    HookEvent.COMPACT_START: ("trigger",),
    HookEvent.COMPACT_END: ("trigger",),
}


@dataclass(frozen=True)
class CodexHook:
    event: HookEvent
    session_id: str
    cwd: str
    branch: str
    emitted_ns: int
    turn_id: str
    model: str
    transcript_path: str
    compaction_trigger: str
    tool_use_id: str
    tool_name: str
    tool_kind: ToolKind
    session_source: str
    stop_hook_active: bool | None
    question_request_id: str = ""


# value: Caller-supplied stable identity, never a storage index or display name.
# label: Non-secret field name used in rejection diagnostics.
# Raises ValueError for unsupported or ambiguous identities.
def require_id(value: Any, label: str) -> str:
    if not isinstance(value, str) or OPAQUE_ID.fullmatch(value) is None:
        raise ValueError(f"{label} must be a bounded opaque identifier")
    return value


# value: Optional genuine hook model slug; absence remains unavailable.
# Raises ValueError for unsupported or unrepresentable reported model metadata.
def hook_model(value: Any) -> str:
    if value is None:
        return ""
    if (not isinstance(value, str) or not 1 <= len(value) <= 128
            or any(not 33 <= ord(character) <= 126 for character in value)):
        raise ValueError("Hook model must contain 1-128 printable ASCII characters without spaces")
    return value


# value: Genuine hook tool identity, never command text or model-generated intent.
# Raises ValueError when the native tool identity is empty or outside its bounded ASCII contract.
def tool_kind(value: Any) -> ToolKind:
    if (not isinstance(value, str) or not 1 <= len(value) <= 256
            or any(not 32 <= ord(character) <= 126 for character in value)):
        raise ValueError("Hook tool_name must be bounded printable ASCII")
    leaf = value.removeprefix("functions.")
    if leaf == "apply_patch":
        return ToolKind.EDIT
    if leaf in {"Bash", "exec_command", "write_stdin", "shell_command"}:
        return ToolKind.COMMAND
    return ToolKind.OTHER


# pairs: Original members of a native hook object.
# Raises ValueError for ambiguous duplicate keys without echoing any source text.
def unique_hook_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    fields: dict[str, Any] = {}
    for key, value in pairs:
        if key in fields:
            raise ValueError("Codex hook JSON contains duplicate keys")
        fields[key] = value
    return fields


# payload: Native JSON hook object before the authenticated broker boundary.
# Raises ValueError for an unsupported event or non-object payload.
def hook_metadata(payload: Any) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise ValueError("CCP hook input must be an object")
    name = payload.get("hook_event_name")
    if not isinstance(name, str) or name not in EVENTS:
        raise ValueError("CCP hook event is not supported")
    event = HookEvent(name)
    fields = COMMON_FIELDS + EVENT_FIELDS.get(event, ())
    if event in {HookEvent.SESSION_START, HookEvent.SESSION_END}:
        fields = tuple(field for field in fields if field != "turn_id")
    metadata = {field: payload[field] for field in fields if field in payload}
    if event is HookEvent.TOOL_END and payload.get("tool_name") == QUESTION_TOOL_NAME:
        # Forward only the broker-generated request identity. Prompt, choices,
        # capability and every other part of the tool result stay local.
        response = payload.get("tool_response")
        if isinstance(response, dict) and response.get("isError") is False:
            result = response.get("structuredContent")
            if isinstance(result, dict) and result.get("kind") == "question" and result.get("state") == "pending":
                request_id = result.get("request_id")
                if isinstance(request_id, str) and QUESTION_REQUEST_ID.fullmatch(request_id):
                    metadata["question_request_id"] = request_id
    return metadata


# payload: Allowlisted native hook metadata plus local branch and emission time.
# now_ns: Current value of the same desktop monotonic clock used by hook producers.
# Raises ValueError for unsupported, unbounded or malformed input.
def decode_hook(payload: dict[str, Any], now_ns: int) -> CodexHook:
    try:
        event = HookEvent(payload.get("hook_event_name"))
    except ValueError:
        raise ValueError("Unsupported CCP hook event") from None

    session_id = require_id(payload.get("session_id"), "session_id")
    cwd = payload.get("cwd")
    if not isinstance(cwd, str) or not 1 <= len(cwd) <= 4096 or "\x00" in cwd:
        raise ValueError("Hook cwd must be a bounded nonempty path")
    branch = payload.get("branch", "")
    if (not isinstance(branch, str) or len(branch) > 64
            or any(not 32 <= ord(character) <= 126 for character in branch)):
        raise ValueError("Hook branch must be bounded printable ASCII checkout metadata")
    emitted_ns = payload.get("emitted_ns")
    if type(emitted_ns) is not int or not 0 < emitted_ns <= now_ns:
        raise ValueError("Hook emission timestamp is invalid")

    turn_id = ""
    if event not in {HookEvent.SESSION_START, HookEvent.SESSION_END}:
        turn_id = require_id(payload.get("turn_id"), "turn_id")
    model = hook_model(payload.get("model"))
    locator = payload.get("transcript_path")
    if locator is None:
        locator = ""
    if not isinstance(locator, str) or len(locator) > 4096 or "\x00" in locator:
        raise ValueError("Native transcript locator is invalid")

    trigger = ""
    if event in {HookEvent.COMPACT_START, HookEvent.COMPACT_END}:
        trigger = payload.get("trigger")
        if not isinstance(trigger, str) or trigger not in {"auto", "manual"}:
            raise ValueError("Compaction trigger must be the genuine auto or manual hook value")

    tool_id = ""
    tool_name = ""
    kind = ToolKind.OTHER
    if event in {HookEvent.TOOL_START, HookEvent.TOOL_END}:
        tool_id = require_id(payload.get("tool_use_id"), "tool_use_id")
        # A completion's invocation ID already carries its association. Check
        # its native name against the start when the source supplies a name.
        if event is HookEvent.TOOL_START or "tool_name" in payload:
            tool_name = payload.get("tool_name")
            kind = tool_kind(tool_name)

    session_source = ""
    if event is HookEvent.SESSION_START and "source" in payload:
        session_source = payload["source"]
        if not isinstance(session_source, str) or session_source not in {
            "startup", "resume", "clear", "compact", "fork",
        }:
            raise ValueError("Unsupported Codex SessionStart source")
    stop_hook_active = None
    if event is HookEvent.STOP and "stop_hook_active" in payload:
        stop_hook_active = payload["stop_hook_active"]
        if type(stop_hook_active) is not bool:
            raise ValueError("Codex stop_hook_active must be a boolean")

    question_request_id = ""
    if "question_request_id" in payload:
        candidate = payload["question_request_id"]
        if (event is not HookEvent.TOOL_END or tool_name != QUESTION_TOOL_NAME
                or not isinstance(candidate, str) or QUESTION_REQUEST_ID.fullmatch(candidate) is None):
            raise ValueError("Question request identity requires a successful Cody question tool completion")
        question_request_id = candidate

    return CodexHook(event, session_id, cwd, branch, emitted_ns, turn_id, model,
                     locator, trigger, tool_id, tool_name, kind,
                     session_source, stop_hook_active, question_request_id)
