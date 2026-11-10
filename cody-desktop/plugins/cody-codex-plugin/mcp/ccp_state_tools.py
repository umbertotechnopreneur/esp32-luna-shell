
"""Bounded MCP views over the authenticated state-v1 source registry."""

from __future__ import annotations

import re
from typing import Any

from ccp_state_registry import SCHEMA, SOURCE_LIMITS, ZERO, StateRegistry


QUERY_SCHEMA = "cody.state.query/1"
VIEWS = ("summary", "chats", "turns", "tool_calls")
HEX_ID = re.compile(r"[0-9a-f]{16}")
STAMP_FIELDS = ("source_id", "epoch", "revision")
MAX_ROWS = 8


# value: A public state-v1 source, entity or revision token.
def _id(value: Any) -> bool:
    return isinstance(value, str) and value != ZERO and HEX_ID.fullmatch(value) is not None


# No parameters: exposes a read-only source projection without selecting a chat.
def tool_definition() -> dict[str, Any]:
    identity = {"type": "string", "pattern": "^[0-9a-f]{16}$"}
    return {
        "name": "ccp_read_state",
        "title": "Read Cody vNext state",
        "description": (
            "Read the local vNext broker's state-v1 source, chat, turn or tool-call metadata. "
            "Use exact source-qualified IDs and the returned cursor for paging. Restored evidence is historical. "
            "This reports broker observations and coverage, independently of what Cody has displayed."
        ),
        "inputSchema": {
            "type": "object",
            "additionalProperties": False,
            "properties": {
                "view": {"type": "string", "enum": list(VIEWS), "default": "summary"},
                "chat_id": {**identity, "description": "Exact chat for a chat or turn query."},
                "turn_id": {**identity, "description": "Exact turn for a tool-call query."},
                "limit": {"type": "integer", "minimum": 1, "maximum": MAX_ROWS, "default": 3},
                "cursor": {
                    "type": "object", "additionalProperties": False,
                    "properties": {
                        "source_id": identity, "epoch": identity, "revision": identity, "after": identity,
                        "view": {"type": "string", "enum": list(VIEWS[1:])},
                        "chat_id": {"anyOf": [identity, {"type": "null"}]},
                        "turn_id": {"anyOf": [identity, {"type": "null"}]},
                    },
                    "required": [*STAMP_FIELDS, "after", "view", "chat_id", "turn_id"],
                },
            },
        },
        "annotations": {
            "readOnlyHint": True, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False,
        },
    }


# row: One schema-valid source record. Return observation IDs used by its facts.
def _observations(row: dict[str, Any]) -> set[str]:
    return {
        field["evidence"] for field in row.values()
        if isinstance(field, dict) and isinstance(field.get("evidence"), str)
    }


# registry: Existing source registry owner supplying one detached generation.
# pc_id: Stable local broker identity, used only to resolve the public source ID.
# arguments: MCP request for one bounded view.
# Raises ValueError for invalid filters, cursor or unavailable producer result.
def read_state(registry: StateRegistry, pc_id: str, arguments: dict[str, Any]) -> dict[str, Any]:
    if set(arguments) - {"view", "chat_id", "turn_id", "limit", "cursor"}:
        raise ValueError("State query arguments contain unexpected fields")
    view = arguments.get("view", "summary")
    limit = arguments.get("limit", 3)
    chat = arguments.get("chat_id")
    turn = arguments.get("turn_id")
    cursor = arguments.get("cursor")
    if not isinstance(view, str) or view not in VIEWS or type(limit) is not int or not 1 <= limit <= MAX_ROWS:
        raise ValueError("State query requires a supported view and a limit from 1 through 8")
    if "chat_id" in arguments and (view not in ("chats", "turns") or not _id(chat)):
        raise ValueError("chat_id requires a valid chats or turns query")
    if "turn_id" in arguments and (view != "tool_calls" or not _id(turn)):
        raise ValueError("turn_id requires a valid tool_calls query")
    scope = {"view": view, "chat_id": chat, "turn_id": turn}
    if "cursor" in arguments and (
        view == "summary" or not isinstance(cursor, dict)
        or set(cursor) != {*STAMP_FIELDS, "after", *scope}
        or any(not _id(cursor[key]) for key in (*STAMP_FIELDS, "after"))
        or any(cursor[key] != value for key, value in scope.items())
    ):
        raise ValueError("State cursor must come from the same query and source generation")

    binding = registry.binding(pc_id)
    snapshot = registry.respond(pc_id, {
        "schema": SCHEMA, "kind": "resync", "request_id": "0000000000000001",
        "source_id": binding["source_id"], "epoch": binding["epoch"], "revision": ZERO,
        "reason": "initial",
    }, SOURCE_LIMITS)
    if snapshot["kind"] == "error":
        return {
            "schema": QUERY_SCHEMA, "state_schema": SCHEMA, "status": "unavailable",
            "code": snapshot["code"], "detail": snapshot["detail"], "pool": snapshot["pool"],
        }
    stamp = {field: snapshot[field] for field in STAMP_FIELDS}
    if cursor is not None and any(cursor[field] != stamp[field] for field in STAMP_FIELDS):
        raise ValueError("State generation changed. Restart this query without its cursor.")

    freshness = dict.fromkeys(("current", "historical", "stale", "unknown"), 0)
    for evidence in snapshot["evidence"]:
        freshness[evidence["freshness"]] += 1
    result = {
        "schema": QUERY_SCHEMA, "state_schema": SCHEMA, "status": "available",
        "evidence_counts": freshness,
        **stamp, **scope, "coverage": snapshot["coverage"], "capabilities": binding["capabilities"],
        "rows": [], "definitions": [], "evidence": [], "next": None,
    }
    if view == "summary":
        return result

    kind = {"chats": "chat", "turns": "turn", "tool_calls": "tool_call"}[view]
    rows = [row for row in snapshot["upserts"] if row["type"] == kind]
    if chat:
        rows = [row for row in rows if row["id" if view == "chats" else "chat"] == chat]
    if turn:
        rows = [row for row in rows if row["turn"] == turn]
    rows.sort(key=lambda row: row["id"])
    if cursor is not None:
        rows = [row for row in rows if row["id"] > cursor["after"]]
    page = rows[:limit]
    definitions = {row["definition"] for row in page} if view == "tool_calls" else set()
    joined = [row for row in snapshot["upserts"] if row["type"] == "tool_definition" and row["id"] in definitions]
    observations = set().union(*(_observations(row) for row in (*page, *joined)))
    result.update(
        rows=page,
        definitions=joined,
        evidence=[row for row in snapshot["evidence"] if row["id"] in observations],
        next={**stamp, **scope, "after": page[-1]["id"]} if len(rows) > limit else None,
    )
    return result
