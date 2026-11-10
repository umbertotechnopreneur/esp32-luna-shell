"""Bounded source registry for the broker's authenticated Cody state producer."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, replace
import hashlib
import json
import re
import secrets
import threading
import time
from typing import TYPE_CHECKING, Any

from ccp_codex_hooks import CodexHook, HookEvent, ToolKind, require_id
from ccp_state_storage import RegistryStorage, RegistryStorageError
from ccp_tool_labels import tool_display_label

if TYPE_CHECKING:
    from ccp_interactions import InteractionStore, QuestionProjection


SCHEMA = "cody.state/1"
ZERO = "0000000000000000"
PROVIDER = "codex"
NAMESPACE = "codex.command-hooks"
CAPABILITIES = {
    "chats": "supported", "turns": "supported", "tool_calls": "supported",
    "agents": "unsupported", "external_operations": "unsupported",
    "questions": "unsupported", "recipient_acknowledgement": "unsupported",
    "resume_questions": "unsupported",
}
SUPPORTED_KINDS = ("source", "chat", "turn", "tool_definition", "tool_call")
QUESTION_KINDS = frozenset({"question", "choice", "answer_attempt"})
QUESTION_CAPABILITIES = {
    "questions": "supported", "recipient_acknowledgement": "supported",
}
QUESTION_LIMITS = {"open_questions": 64, "resolved_questions": 32, "choices": 288, "answer_attempts": 192}
ENTITY_KINDS = SUPPORTED_KINDS + (
    "agent_task", "external_operation", "question", "choice", "answer_attempt", "wait",
)
SOURCE_LIMITS = {
    "sources": 1, "chats": 64, "turns": 192, "tool_definitions": 128,
    "active_calls": 256, "terminal_calls": 128, "agents": 0, "operations": 0,
    **QUESTION_LIMITS, "waits": 0, "evidence": 4096, "tombstones": 512,
    "text_bytes": 128 * 1024, "decode_scratch_bytes": 64 * 1024,
    "persistence_scratch_bytes": 32 * 1024,
}
CORE_LIMITS = {**SOURCE_LIMITS, **dict.fromkeys(QUESTION_LIMITS, 0)}
POOL_KIND = {
    "chats": "chat", "turns": "turn", "tool_definitions": "tool_definition",
    "active_calls": "tool_call", "terminal_calls": "tool_call",
}
POOL_KIND_BY_KIND = {"chat": "chats", "turn": "turns", "tool_definition": "tool_definitions"}
MAX_JOURNAL_TRANSACTIONS = 256
MAX_JOURNAL_BYTES = 512 * 1024
MAX_AGE_MS = 0xffffffff
MAX_COUNTER = (1 << 64) - 1
CHECKPOINT_VERSION = 1
HEX_ID = re.compile(r"[0-9a-f]{16}")
LEASE_MS = 120000
TOOL_CATEGORIES = {ToolKind.COMMAND: "command", ToolKind.EDIT: "file_edit"}
HISTORICAL_EVENTS = frozenset({
    HookEvent.TOOL_START, HookEvent.TOOL_END, HookEvent.STOP, HookEvent.INTERRUPT, HookEvent.SESSION_END,
})

Key = tuple[str, str]
Recipient = tuple[str, str]


class RegistryRejection(ValueError):
    """An observation or request could not change the current source generation."""

    # code: Protocol error code for the original admission failure.
    # detail: Bounded public diagnostic without native identities or content.
    # pool: Capacity limit when the failure is specific to a pool.
    def __init__(self, code: str, detail: str, pool: str | None = None) -> None:
        super().__init__(detail)
        self.code = code
        self.pool = pool


@dataclass(frozen=True)
class Observation:
    identity: str
    origin_epoch: str
    revision: int
    observed_ns: int
    provenance: str = "native_event"


@dataclass(frozen=True)
class Fact:
    value: str | None = None
    observation: Observation | None = None


@dataclass(frozen=True)
class Record:
    native_id: tuple[str, ...]
    body: dict[str, str | int | Fact]
    created_ns: int = 0
    created_epoch: str = ""
    audience: Recipient | None = None


@dataclass(frozen=True)
class JournalEntry:
    revision: int
    before: dict[Key, Record | None]
    removed: tuple[Key, ...]
    charge: int


# value: An already bounded unsigned source counter or timestamp.
def hex64(value: int) -> str:
    return f"{value:016x}"


# kind: Domain identity namespace, disjoint for chats, turns, calls and definitions.
# native_id: Exact native identity components, with no delimiters embedded.
def token(kind: str, native_id: tuple[str, ...]) -> str:
    parts = ("cody-state-v1", kind, *native_id)
    digest = hashlib.sha256("\x00".join(parts).encode("ascii")).digest()
    return hex64(int.from_bytes(digest[:8], "big") or 1)


# kind: Domain identity namespace.
# native_id: Original composite native identity.
def key_for(kind: str, native_id: tuple[str, ...]) -> Key:
    return kind, token(kind, native_id)


# record: Retained row in the producer's private immutable-by-convention view.
# name: Fact field in that row.
def fact_at(record: Record, name: str) -> Fact:
    fact = record.body[name]
    if not isinstance(fact, Fact):
        raise RuntimeError("Cody state registry field is not a fact")
    return fact


# fact: Last accepted field observation, if available.
# observed: New observation with its exact broker clock domain.
def newer(fact: Fact, observed: Observation) -> bool:
    prior = fact.observation
    return (prior is None or observed.origin_epoch != prior.origin_epoch
            or observed.observed_ns > prior.observed_ns)


# row: Exact retained row and associated internal observation handles.
# key: Stable kind/token used to identify the row on the wire.
def record_wire(key: Key, row: Record) -> dict[str, Any]:
    result: dict[str, Any] = {"type": key[0], "id": key[1]}
    for name, field in row.body.items():
        if isinstance(field, Fact):
            result[name] = {
                "state": "known" if field.value is not None else "unknown",
                "value": field.value,
                "evidence": field.observation.identity if field.observation else None,
            }
        else:
            result[name] = field
    return result


class StateRegistry:
    """Provider facts have one revision order independent of foreground selection."""

    # storage: Optional exclusive broker checkpoint owner. In-memory clients omit it.
    # owner_id: Retained PC ID that must match any durable registry contents.
    # Raises RegistryStorageError for invalid or inaccessible retained state.
    def __init__(self, storage: RegistryStorage | None = None, owner_id: str | None = None) -> None:
        self._lock = threading.Lock()
        self._epoch = hex64(secrets.randbits(64) or 1)
        self._started_ns = time.monotonic_ns()
        self._revision = 1
        self._storage = storage
        self._questions: InteractionStore | None = None
        self._owner_id = owner_id
        self._storage_failure: RegistryRejection | None = None
        self._retired: dict[Key, None] = {}
        self._records: dict[Key, Record] = {}
        self._journal: deque[JournalEntry] = deque()
        self._journal_bytes = 0
        self._tombstones: dict[Key, int] = {}
        self._unobserved_capacity: dict[str, RegistryRejection] = {}
        if storage is not None:
            if owner_id is None:
                raise RegistryStorageError("Durable registry requires its retained PC identity")
            document = storage.load()
            if document is None:
                storage.save(self._checkpoint_document(self._records, self._revision,
                                                       self._retired, self._unobserved_capacity))
            else:
                self._restore(document)

    # questions: Interaction owner attached once during broker construction.
    def attach_questions(self, questions: InteractionStore) -> None:
        self._questions = questions

    # request_id: Original successful question ticket from the native hook.
    # chat_id: Native chat ID from that exact invocation.
    # turn_id: Native turn ID from that exact invocation.
    # Raises ValueError on a conflicting association and preserves storage failures.
    def associate_question(self, request_id: str, chat_id: str, turn_id: str) -> None:
        # Keep an admitted turn pinned before another hook can reclaim it.
        with self._lock:
            if self._questions is None:
                raise ValueError("The question ledger is not attached")
            self._questions.associate_question(request_id, chat_id, turn_id)

    # pc_id: Retained broker identity, never a user credential or UI label.
    # questions: The authenticated bootstrap profile explicitly admits question entities.
    # Raises ValueError if the caller uses a different owner for a durable registry.
    def binding(self, pc_id: str, *, questions: bool = False) -> dict[str, Any]:
        if self._owner_id is not None and pc_id != self._owner_id:
            raise ValueError("Registry binding does not match the durable owner")
        return {
            "source_id": token("source", (pc_id, NAMESPACE)),
            "epoch": self._epoch, "provider": PROVIDER, "native_namespace": NAMESPACE,
            "capabilities": {**CAPABILITIES, **(QUESTION_CAPABILITIES if questions else {})},
        }

    # records: Complete candidate generation, already admitted as one graph.
    # revision: Exact target revision this generation will publish.
    # retired: Bounded oldest-first native identity tombstones.
    # coverage: Latest omission diagnosis per affected entity kind.
    def _checkpoint_document(self, records: dict[Key, Record], revision: int, retired: dict[Key, None],
                             coverage: dict[str, RegistryRejection]) -> dict[str, Any]:
        # The interaction ledger owns all question persistence, including the
        # recipient binding and exposure. Do not duplicate it in this checkpoint.
        rows = [(key, row) for key, row in records.items() if row.audience is None]
        return {
            "version": CHECKPOINT_VERSION, "owner": self._owner_id,
            "last_epoch": self._epoch, "revision": revision,
            "records": [
                {"native": list(row.native_id), "record": record_wire(key, row),
                 "created": [row.created_epoch, row.created_ns] if key[0] == "turn" else None}
                for key, row in rows
            ],
            "evidence": [
                [item.identity, item.origin_epoch, item.revision, item.observed_ns, item.provenance]
                for item in self._referenced(rows).values()
            ],
            "retired": [list(key) for key in retired],
            "coverage": [error.pool for error in coverage.values()],
        }

    # records: Complete candidate graph; publication is still pending.
    # revision: Revision reserved for this exact candidate.
    # retired: Retained terminal identity history for the candidate.
    # coverage: Coverage diagnostics persisted with the same generation.
    # Raises RegistryRejection and latches the storage fault on an ambiguous commit.
    def _save(self, records: dict[Key, Record], revision: int, retired: dict[Key, None],
              coverage: dict[str, RegistryRejection]) -> None:
        if self._storage is None:
            return
        try:
            self._storage.save(self._checkpoint_document(records, revision, retired, coverage))
        except RegistryStorageError as error:
            self._storage_failure = RegistryRejection("persistence", str(error))
            raise self._storage_failure from error

    # value: Checkpoint value expected to be a nonzero canonical 64-bit token.
    # Raises ValueError for zero, uppercase, or noncanonical values.
    def _stored_id(self, value: Any) -> str:
        if not isinstance(value, str) or HEX_ID.fullmatch(value) is None or value == ZERO:
            raise ValueError("Invalid checkpoint identity")
        return value

    # value: Exact positive integer persisted without conversion or truncation.
    # maximum: Inclusive bound for this counter or original monotonic sample.
    # Raises ValueError for bool, fraction, negative, zero or overflow.
    def _stored_counter(self, value: Any, maximum: int) -> int:
        if type(value) is not int or not 0 < value <= maximum:
            raise ValueError("Invalid checkpoint counter")
        return value

    # value: Authenticated evidence table from the checkpoint.
    # revision: Last committed source revision across all retained epochs.
    # Raises ValueError for malformed, duplicate or altered origin evidence.
    def _stored_evidence(self, value: Any, revision: int) -> dict[str, Observation]:
        if not isinstance(value, list) or len(value) > SOURCE_LIMITS["evidence"]:
            raise ValueError("Invalid checkpoint evidence table")
        result: dict[str, Observation] = {}
        for item in value:
            if not isinstance(item, list) or len(item) != 5:
                raise ValueError("Invalid checkpoint evidence fields")
            identity = self._stored_id(item[0])
            epoch = self._stored_id(item[1])
            original_revision = self._stored_counter(item[2], revision)
            observed_ns = self._stored_counter(item[3], MAX_COUNTER)
            provenance = item[4]
            if (identity in result or identity != token("observation", (epoch, hex64(original_revision)))
                    or provenance not in ("native_event", "native_read")):
                raise ValueError("Invalid checkpoint evidence origin")
            result[identity] = Observation(identity, epoch, original_revision, observed_ns, provenance)
        return result

    # field: Exact stored wire fact (checkpoint and wire share fact identity).
    # name: Fact member name within the supported registry records.
    # evidence: Validated retained observation table.
    # Raises ValueError for unsupported values, shape, or an absent evidence origin.
    def _stored_fact(self, field: Any, name: str, evidence: dict[str, Observation]) -> Fact:
        if not isinstance(field, dict) or set(field) != {"state", "value", "evidence"}:
            raise ValueError("Invalid checkpoint fact shape")
        if field["state"] == "unknown" and field["value"] is None and field["evidence"] is None:
            if name in {"lifecycle", "execution", "state"}:
                raise ValueError("Checkpoint is missing required lifecycle evidence")
            return Fact()
        if field["state"] != "known" or not isinstance(field["value"], str):
            raise ValueError("Invalid checkpoint fact availability")
        origin = evidence.get(self._stored_id(field["evidence"]))
        if origin is None:
            raise ValueError("Checkpoint fact does not resolve to its evidence")
        value = field["value"]
        if name in {"title", "display_label"}:
            limit = 255 if name == "title" else 96
            if len(value) > limit or any(not 32 <= ord(character) <= 126 for character in value):
                raise ValueError("Invalid checkpoint display text")
        elif name == "current_turn":
            self._stored_id(value)
        else:
            allowed = {
                "lifecycle": ("open", "closed"), "execution": ("running", "stopped", "interrupted"),
                "category": ("command", "file_edit"), "state": ("running", "returned"),
            }
            if value not in allowed.get(name, ()):
                raise ValueError("Unsupported checkpoint fact value")
        return Fact(value, origin)

    # value: One retained record and its exact native correlation metadata.
    # evidence: Validated proof table; wire references must resolve within it.
    # Raises ValueError for ambiguous identity, record shape, or field contents.
    def _stored_row(self, value: Any, evidence: dict[str, Observation]) -> tuple[Key, Record]:
        if not isinstance(value, dict) or set(value) != {"native", "record", "created"}:
            raise ValueError("Invalid checkpoint row shape")
        native = value["native"]
        row = value["record"]
        if not isinstance(row, dict) or not isinstance(native, list):
            raise ValueError("Invalid checkpoint row identity")
        kind = row.get("type")
        if kind not in ("chat", "turn", "tool_definition", "tool_call"):
            raise ValueError("Unsupported checkpoint record kind")
        layout = {
            "chat": (1, (), ("title", "lifecycle", "current_turn")),
            "turn": (2, ("chat",), ("execution", "outcome", "reply")),
            "tool_definition": (1, ("original_name",), ("display_label", "category")),
            "tool_call": (3, ("turn", "definition"), ("executing_agent", "state", "outcome")),
        }
        native_count, attributes, facts = layout[kind]
        if len(native) != native_count or set(row) != {"type", "id", *attributes, *facts}:
            raise ValueError("Invalid checkpoint record fields")
        if kind == "tool_definition":
            name = native[0]
            if (not isinstance(name, str) or not 1 <= len(name) <= 128 or name != row["original_name"]
                    or any(not 32 <= ord(character) <= 126 for character in name)):
                raise ValueError("Invalid checkpoint tool identity")
        else:
            for identity in native:
                require_id(identity, "Checkpoint native identity")
        native_id = tuple(native)
        key = key_for(kind, native_id)
        if key[1] != row["id"]:
            raise ValueError("Checkpoint native correlation does not match its wire token")
        body: dict[str, str | Fact] = {
            name: row[name] if name == "original_name" else self._stored_id(row[name])
            for name in attributes
        }
        body.update((name, self._stored_fact(row[name], name, evidence)) for name in facts)
        created = value["created"]
        if kind == "turn":
            if not isinstance(created, list) or len(created) != 2:
                raise ValueError("Invalid checkpoint turn origin")
            epoch = self._stored_id(created[0])
            observed_ns = self._stored_counter(created[1], MAX_COUNTER)
            return key, Record(native_id, body, observed_ns, epoch)
        if created is not None:
            raise ValueError("Unexpected checkpoint creation origin")
        return key, Record(native_id, body)

    # records: Complete decoded candidate, before it can replace the live graph.
    # Raises ValueError when retained references disagree with native correlations.
    def _validate_checkpoint_graph(self, records: dict[Key, Record]) -> None:
        for (kind, _), row in records.items():
            if kind == "chat":
                current = fact_at(row, "current_turn").value
                if current is not None:
                    turn = records.get(("turn", current))
                    if turn is None or turn.native_id[:1] != row.native_id:
                        raise ValueError("Checkpoint current turn does not belong to its chat")
            elif kind == "turn":
                chat = key_for("chat", row.native_id[:1])
                if row.body["chat"] != chat[1] or chat not in records:
                    raise ValueError("Checkpoint turn parent is missing")
            elif kind == "tool_call":
                turn = key_for("turn", row.native_id[:2])
                if row.body["turn"] != turn[1] or turn not in records:
                    raise ValueError("Checkpoint tool turn is missing")
                if ("tool_definition", row.body["definition"]) not in records:
                    raise ValueError("Checkpoint tool definition is missing")

    # document: Authenticated checkpoint, bound to the original broker PC identity.
    # Raises RegistryStorageError for unsupported, partial or inconsistent recovered state.
    def _restore(self, document: dict[str, Any]) -> None:
        try:
            if (set(document) != {"version", "owner", "last_epoch", "revision", "records",
                                  "evidence", "retired", "coverage"}
                    or type(document["version"]) is not int or document["version"] != CHECKPOINT_VERSION
                    or document["owner"] != self._owner_id):
                raise ValueError("Checkpoint version, owner or fields are invalid")
            prior_epoch = self._stored_id(document["last_epoch"])
            revision = self._stored_counter(document["revision"], MAX_COUNTER)
            evidence = self._stored_evidence(document["evidence"], revision)
            rows = document["records"]
            max_rows = sum(SOURCE_LIMITS[pool] for pool in POOL_KIND)
            if not isinstance(rows, list) or len(rows) > max_rows:
                raise ValueError("Invalid checkpoint registry size")
            records: dict[Key, Record] = {}
            for value in rows:
                key, row = self._stored_row(value, evidence)
                if key in records:
                    raise ValueError("Duplicate checkpoint record")
                records[key] = row
            self._validate_checkpoint_graph(records)
            self._check_limits(list(records.items()), SOURCE_LIMITS)
            if self._referenced(list(records.items())) != evidence:
                raise ValueError("Checkpoint includes unused observation evidence")

            retired_rows = document["retired"]
            if not isinstance(retired_rows, list) or len(retired_rows) > SOURCE_LIMITS["tombstones"]:
                raise ValueError("Invalid checkpoint tombstone table")
            retired: dict[Key, None] = {}
            for row in retired_rows:
                if (not isinstance(row, list) or len(row) != 2
                        or row[0] not in ("chat", "turn", "tool_definition", "tool_call")):
                    raise ValueError("Invalid checkpoint tombstone")
                key = (row[0], self._stored_id(row[1]))
                if key in records or key in retired:
                    raise ValueError("Checkpoint tombstone conflicts with a retained identity")
                retired[key] = None

            pools = document["coverage"]
            if not isinstance(pools, list) or len(pools) > len(POOL_KIND):
                raise ValueError("Invalid checkpoint coverage table")
            coverage: dict[str, RegistryRejection] = {}
            for pool in pools:
                if not isinstance(pool, str) or pool not in POOL_KIND or POOL_KIND[pool] in coverage:
                    raise ValueError("Invalid checkpoint coverage pool")
                coverage[POOL_KIND[pool]] = RegistryRejection(
                    "capacity_exceeded", "A previously observed source pool was exhausted", pool)
        except ValueError as error:
            raise RegistryStorageError(f"Registry checkpoint validation failed: {error}") from error

        # The retained graph is the baseline for the new epoch. Replay starts
        # after the next live observation, while all old observations keep
        # their identity, origin and provenance.
        prior_clocks = {item.origin_epoch for item in evidence.values()}
        prior_clocks.update(row.created_epoch for row in records.values() if row.created_epoch)
        prior_clocks.add(prior_epoch)
        while self._epoch in prior_clocks:
            self._epoch = hex64(secrets.randbits(64) or 1)
        self._revision = revision
        self._records = records
        self._retired = retired
        self._unobserved_capacity = coverage

    # staged: Isolated transaction candidate.
    # key: Typed wire identity derived from the exact provider key.
    # native_id: Full provider key, retained for collision detection.
    # Raises RegistryRejection for a conflicting identity or replayed tombstone.
    def _lookup(self, staged: dict[Key, Record], key: Key, native_id: tuple[str, ...]) -> Record | None:
        row = staged.get(key)
        if row is not None and row.native_id != native_id:
            raise RegistryRejection("invalid_transition", "Conflicting native identity token")
        if row is None and key in self._retired:
            raise RegistryRejection("invalid_transition", "Native identity belongs to retired history")
        return row

    # staged: Isolated transaction candidate.
    # key: Exact row being revised.
    # row: Existing row whose identity and unchanged fields remain intact.
    # fields: Whole replacement values for just the affected internal fields.
    def _replace(self, staged: dict[Key, Record], key: Key, row: Record, **fields: str | Fact) -> None:
        staged[key] = Record(row.native_id, {**row.body, **fields}, row.created_ns, row.created_epoch)

    # staged: Candidate generation.
    # turn_token: The turn selected for retention inspection.
    def _turn_calls(self, staged: dict[Key, Record], turn_token: str) -> list[Key]:
        return [key for key, row in staged.items()
                if key[0] == "tool_call" and row.body["turn"] == turn_token]

    # staged: Candidate generation.
    # key: Turn whose history may be reclaimed.
    # row: Exact retained turn. Only explicitly stopped/interrupted turns qualify.
    def _evictable_turn(self, staged: dict[Key, Record], key: Key, row: Record) -> bool:
        return (
            fact_at(row, "execution").value in {"stopped", "interrupted"}
            and (self._questions is None or row.native_id not in self._questions.associated_question_turns())
            and not any(candidate_key[0] == "question" and fact_at(candidate, "turn").value == key[1]
                        for candidate_key, candidate in staged.items())
            and all(fact_at(staged[call], "state").value == "returned"
                    for call in self._turn_calls(staged, key[1]))
        )

    # staged: Candidate generation.
    # key: A previously checked unpinned turn and its terminal call history.
    def _remove_turn(self, staged: dict[Key, Record], key: Key) -> None:
        for call in self._turn_calls(staged, key[1]):
            del staged[call]
        del staged[key]

    # staged: Candidate generation.
    # chat_token: Chat receiving the new current turn, allowing its former current turn to retire.
    # Raises RegistryRejection if every retained turn remains pinned or lacks terminal evidence.
    def _reserve_turn(self, staged: dict[Key, Record], chat_token: str) -> None:
        turns = [(key, row) for key, row in staged.items() if key[0] == "turn"]
        if len(turns) < SOURCE_LIMITS["turns"]:
            return
        pinned = {fact_at(row, "current_turn").value for key, row in staged.items()
                  if key[0] == "chat" and key[1] != chat_token}
        candidates = [(fact_at(row, "execution").observation.revision, key)
                      for key, row in turns
                      if key[1] not in pinned and self._evictable_turn(staged, key, row)]
        if not candidates:
            raise RegistryRejection("capacity_exceeded", "No unpinned terminal turn can be reclaimed", "turns")
        self._remove_turn(staged, min(candidates)[1])

    # staged: Candidate generation.
    # Raises RegistryRejection if every retained chat is open or has unresolved work.
    def _reserve_chat(self, staged: dict[Key, Record]) -> None:
        chats = [(key, row) for key, row in staged.items() if key[0] == "chat"]
        if len(chats) < SOURCE_LIMITS["chats"]:
            return
        candidates = sorted((fact_at(row, "lifecycle").observation.revision, key)
                            for key, row in chats if fact_at(row, "lifecycle").value == "closed")
        for _, candidate in candidates:
            turns = [(key, row) for key, row in staged.items()
                     if key[0] == "turn" and row.body["chat"] == candidate[1]]
            if all(self._evictable_turn(staged, key, row) for key, row in turns):
                for key, _ in turns:
                    self._remove_turn(staged, key)
                del staged[candidate]
                return
        raise RegistryRejection("capacity_exceeded", "No closed chat with resolved work can be reclaimed", "chats")

    # staged: Candidate generation.
    # native: Accepted provider observation, already stripped of contents.
    # observed: Original evidence for every fact changed by this observation.
    # Raises RegistryRejection on unknown association or unresolved bounded capacity.
    def _apply_tool(self, staged: dict[Key, Record], native: CodexHook, observed: Observation) -> None:
        turn_id = (native.session_id, native.turn_id)
        turn_key = key_for("turn", turn_id)
        turn = self._lookup(staged, turn_key, turn_id)
        if turn is None:
            raise RegistryRejection("missing_reference", "Tool observation has no admitted native turn")
        if observed.origin_epoch == turn.created_epoch and native.emitted_ns <= turn.created_ns:
            raise RegistryRejection("invalid_transition", "Tool observation precedes its admitted turn")
        call_id = (*turn_id, native.tool_use_id)
        call_key = key_for("tool_call", call_id)
        call = self._lookup(staged, call_key, call_id)
        ended = native.event is HookEvent.TOOL_END
        tool_name = native.tool_name
        if call is not None:
            existing_definition = staged[("tool_definition", call.body["definition"])]
            if tool_name and existing_definition.body["original_name"] != tool_name:
                raise RegistryRejection("invalid_transition", "Tool observation conflicts with its original name")
            if not ended or fact_at(call, "state").value == "returned":
                raise RegistryRejection("invalid_transition", "Tool invocation already has that boundary")
            if not newer(fact_at(call, "state"), observed):
                raise RegistryRejection("invalid_transition", "Tool completion precedes its admitted start")
        else:
            if not tool_name:
                raise RegistryRejection("missing_reference", "Tool completion without its start requires the original tool name")
            if not ended and fact_at(turn, "execution").value == "interrupted":
                if newer(fact_at(turn, "execution"), observed):
                    raise RegistryRejection("invalid_transition", "Interrupted turn cannot start another tool")
            if not ended and sum(key[0] == "tool_call" and fact_at(row, "state").value == "running"
                                 for key, row in staged.items()) >= SOURCE_LIMITS["active_calls"]:
                raise RegistryRejection("capacity_exceeded", "Source active tool calls are full", "active_calls")
            if len(tool_name) > 128:
                raise RegistryRejection("capacity_exceeded", "Native tool name exceeds the state protocol limit", "tool_definitions")
            definition_id = (tool_name,)
            definition_key = key_for("tool_definition", definition_id)
            definition = self._lookup(staged, definition_key, definition_id)
            if definition is None:
                if sum(key[0] == "tool_definition" for key in staged) >= SOURCE_LIMITS["tool_definitions"]:
                    raise RegistryRejection("capacity_exceeded", "Source tool definitions are full", "tool_definitions")
                label = tool_display_label(tool_name)
                category = TOOL_CATEGORIES.get(native.tool_kind)
                staged[definition_key] = Record(definition_id, {
                    "original_name": tool_name,
                    "display_label": Fact(label, observed) if len(label) <= 96 else Fact(),
                    "category": Fact(category, observed) if category else Fact(),
                })
            call = Record(call_id, {
                "turn": turn_key[1], "definition": definition_key[1],
                "executing_agent": Fact(), "outcome": Fact(),
            })
        self._replace(staged, call_key, call, state=Fact("returned" if ended else "running", observed))
        if ended:
            terminal = [(fact_at(row, "state").observation.revision, key)
                        for key, row in staged.items()
                        if key[0] == "tool_call" and fact_at(row, "state").value == "returned"]
            if len(terminal) > SOURCE_LIMITS["terminal_calls"]:
                del staged[min(terminal)[1]]
        else:
            execution = fact_at(turn, "execution")
            if newer(execution, observed):
                self._replace(staged, turn_key, turn, execution=Fact("running", observed))

    # staged: Candidate generation.
    # native: Exact native hook envelope, used only for supported domain transitions.
    # observed: Original evidence for newly established facts.
    # Raises RegistryRejection on invalid transitions and missing or exhausted identities.
    def _apply(self, staged: dict[Key, Record], native: CodexHook, observed: Observation) -> None:
        chat_id = (native.session_id,)
        chat_key = key_for("chat", chat_id)
        chat = self._lookup(staged, chat_key, chat_id)
        event = native.event
        if chat is None:
            if event not in {HookEvent.SESSION_START, HookEvent.PROMPT}:
                raise RegistryRejection("missing_reference", "Hook has no admitted native chat")
            self._reserve_chat(staged)
            chat = Record(chat_id, {"title": Fact(), "lifecycle": Fact("open", observed),
                                    "current_turn": Fact()})
            staged[chat_key] = chat

        if event in {HookEvent.SESSION_START, HookEvent.SESSION_END, HookEvent.PROMPT}:
            lifecycle = fact_at(chat, "lifecycle")
            value = "closed" if event is HookEvent.SESSION_END else "open"
            historical = lifecycle.observation is not None and lifecycle.observation.origin_epoch != self._epoch
            if (value != lifecycle.value or historical) and newer(lifecycle, observed):
                self._replace(staged, chat_key, chat, lifecycle=Fact(value, observed))
                chat = staged[chat_key]

        if event in {HookEvent.TOOL_START, HookEvent.TOOL_END}:
            lifecycle = fact_at(chat, "lifecycle")
            if event is HookEvent.TOOL_START and lifecycle.value == "closed" and newer(lifecycle, observed):
                raise RegistryRejection("invalid_transition", "Closed chat cannot start another tool")
            self._apply_tool(staged, native, observed)
            return

        if event not in {HookEvent.PROMPT, HookEvent.STOP, HookEvent.INTERRUPT}:
            return
        turn_id = (native.session_id, native.turn_id)
        turn_key = key_for("turn", turn_id)
        turn = self._lookup(staged, turn_key, turn_id)
        current = fact_at(chat, "current_turn")
        if event is HookEvent.PROMPT:
            if turn is None:
                becomes_current = newer(current, observed)
                self._reserve_turn(staged, chat_key[1] if becomes_current else "")
                staged[turn_key] = Record(turn_id, {
                    "chat": chat_key[1], "execution": Fact("running", observed),
                    "outcome": Fact(), "reply": Fact(),
                }, native.emitted_ns, observed.origin_epoch)
                if becomes_current:
                    self._replace(staged, chat_key, chat, current_turn=Fact(turn_key[1], observed))
                return
            value = "running"
        else:
            if turn is None:
                raise RegistryRejection("missing_reference", "Turn boundary has no admitted native turn")
            value = "stopped" if event is HookEvent.STOP else "interrupted"

        execution = fact_at(turn, "execution")
        if execution.value == "interrupted" and value != "interrupted":
            raise RegistryRejection("invalid_transition", "Interrupted turn cannot reopen or become stopped")
        if newer(execution, observed) and (value != execution.value or event is HookEvent.PROMPT):
            self._replace(staged, turn_key, turn, execution=Fact(value, observed))

    # observed_ns: Original observation time, on the broker's monotonic clock.
    # provenance: Exact acquisition boundary producing the new facts.
    def _new_observation(self, observed_ns: int, provenance: str) -> Observation:
        if self._revision == MAX_COUNTER:
            raise RegistryRejection("capacity_exceeded", "Source revision range is exhausted")
        revision = self._revision + 1
        # Epoch qualifies the origin even when a new source starts at revision one.
        identity = token("observation", (self._epoch, hex64(revision)))
        return Observation(identity, self._epoch, revision, observed_ns, provenance)

    # native: Allowlisted, validated metadata from one documented command hook.
    # Raises RegistryRejection when a source transition fails atomically.
    def observe(self, native: CodexHook) -> bool:
        with self._lock:
            if self._storage_failure is not None:
                raise self._storage_failure
            if (self._storage is not None and native.emitted_ns < self._started_ns
                    and native.event not in {HookEvent.SESSION_START, HookEvent.PROMPT}):
                raise RegistryRejection("invalid_transition", "Hook predates this durable broker epoch")
            staged = self._records.copy()
            try:
                self._apply(staged, native, self._new_observation(native.emitted_ns, "native_event"))
            except RegistryRejection as error:
                if error.code == "capacity_exceeded" and error.pool in POOL_KIND:
                    kind = POOL_KIND[error.pool]
                    previous = self._unobserved_capacity.get(kind)
                    if previous is None or previous.pool != error.pool:
                        coverage = {**self._unobserved_capacity, kind: error}
                        self._save(self._records, self._revision, self._retired, coverage)
                        self._unobserved_capacity = coverage
                raise
            return self._commit(staged)

    # session_id: Exact native chat returned by the existing title source.
    # title: Complete reported title in the protocol's printable ASCII range.
    # observed_at: Original native read time on the local monotonic clock.
    # Raises RegistryRejection if durable storage can no longer accept facts.
    def observe_title(self, session_id: str, title: str, observed_at: float) -> None:
        if not 0 <= len(title) <= 255 or any(not 32 <= ord(character) <= 126 for character in title):
            return
        observed_ns = int(observed_at * 1_000_000_000)
        if not 0 < observed_ns <= time.monotonic_ns():
            return
        with self._lock:
            if self._storage_failure is not None:
                raise self._storage_failure
            native_id = (session_id,)
            key = key_for("chat", native_id)
            row = self._records.get(key)
            if row is None or row.native_id != native_id:
                return
            prior = fact_at(row, "title")
            observed = self._new_observation(observed_ns, "native_read")
            if not newer(prior, observed):
                return
            staged = self._records.copy()
            self._replace(staged, key, row, title=Fact(title, observed))
            self._commit(staged)

    # rows: Records whose evidence must be retained or supplied.
    # Raises RegistryRejection if different observations collide in the token space.
    def _referenced(self, rows: list[tuple[Key, Record]]) -> dict[str, Observation]:
        evidence: dict[str, Observation] = {}
        for _, row in rows:
            for field in row.body.values():
                if isinstance(field, Fact) and field.observation is not None:
                    original = field.observation
                    if evidence.setdefault(original.identity, original) != original:
                        raise RegistryRejection("invalid_transition", "Conflicting source evidence token")
        return evidence

    # observed: Immutable original native or broker fact evidence.
    # now_ns: Same-clock freeze time for the source age, not a renewal.
    def _evidence_wire(self, observed: Observation, now_ns: int) -> dict[str, Any]:
        elapsed_ns = now_ns - observed.observed_ns
        age_ms = (elapsed_ns + 999_999) // 1_000_000
        restored = observed.provenance == "restored"
        historical = restored or observed.origin_epoch != self._epoch
        age_ms = age_ms if not historical and elapsed_ns >= 0 and age_ms <= MAX_AGE_MS else None
        freshness = "historical" if historical else (
            "unknown" if age_ms is None else ("current" if age_ms < LEASE_MS else "stale")
        )
        return {
            "id": observed.identity, "origin_epoch": observed.origin_epoch,
            "origin_revision": hex64(observed.revision),
            "observed_at": None if restored else {
                "basis": "monotonic_microseconds", "clock_id": observed.origin_epoch,
                "microseconds": hex64(observed.observed_ns // 1000),
            },
            "source_age_ms": age_ms, "lease_ms": LEASE_MS,
            "freshness": freshness,
            "provenance": observed.provenance, "error": None,
        }

    # staged: Coherent candidate generation. Caller holds _lock.
    def _commit(self, staged: dict[Key, Record]) -> bool:
        previous = self._records
        upserts = [(key, row) for key, row in staged.items() if previous.get(key) != row]
        removed = tuple(key for key in previous if key not in staged)
        if not upserts and not removed:
            return False
        revision = self._revision + 1
        before = {key: previous.get(key) for key, _ in upserts}
        before.update((key, previous[key]) for key in removed)
        # Charge a worst-width source age for each fact and bound the fixed
        # envelope/coverage overhead independently from the retained record data.
        undo_rows = [(key, row) for key, row in before.items() if row is not None]
        evidence = [self._evidence_wire(item, item.observed_ns + MAX_AGE_MS * 1_000_000)
                    for item in self._referenced(upserts + undo_rows).values()]
        charge = 2048 + len(json.dumps({
            "evidence": evidence,
            "upserts": [record_wire(key, row) for key, row in upserts],
            "undo": [record_wire(key, row) for key, row in undo_rows],
            "remove": [{"type": key[0], "id": key[1], "revision": hex64(revision)} for key in removed],
        }, ensure_ascii=True, separators=(",", ":")))
        entry = JournalEntry(revision, before, removed, charge)
        durable_removed = tuple(key for key in removed if previous[key].audience is None)
        retired = {**self._retired, **dict.fromkeys(durable_removed)}
        while len(retired) > SOURCE_LIMITS["tombstones"]:
            del retired[next(iter(retired))]
        if self._storage is None:
            self._referenced(list(staged.items()))
        if durable_removed or any(row.audience is None for _, row in upserts):
            self._save(staged, revision, retired, self._unobserved_capacity)
        self._retired = retired
        self._records = staged
        self._revision = revision
        self._journal.append(entry)
        self._journal_bytes += charge
        self._tombstones.update((key, revision) for key in removed)
        while (len(self._journal) > MAX_JOURNAL_TRANSACTIONS or self._journal_bytes > MAX_JOURNAL_BYTES
               or len(self._tombstones) > SOURCE_LIMITS["tombstones"]):
            expired = self._journal.popleft()
            self._journal_bytes -= expired.charge
            for key in expired.removed:
                del self._tombstones[key]
        return True

    # item: Single committed question from the interaction ledger.
    # staged: Candidate graph used to resolve original native IDs.
    # observed: This exact question change's source evidence.
    def _question_rows(self, item: QuestionProjection, staged: dict[Key, Record],
                       observed: Observation) -> dict[Key, Record]:
        native = (item.question_id,)
        question_key = key_for("question", native)
        chat_native = (item.chat_id,)
        turn_native = (item.chat_id, item.turn_id)
        chat_key = key_for("chat", chat_native) if item.chat_id else None
        turn_key = key_for("turn", turn_native) if item.turn_id else None
        chat = staged.get(chat_key) if chat_key is not None else None
        turn = staged.get(turn_key) if turn_key is not None else None
        linked = (chat is not None and chat.native_id == chat_native and turn is not None
                  and turn.native_id == turn_native and turn.body["chat"] == chat_key[1])
        question_state = "answered" if item.question_state == "resolved" else item.question_state
        ticket_state = ("available" if item.ticket_state in {"pending", "received"}
                        else "expired" if item.ticket_state == "expired" else "unavailable")
        audience = (item.device_id, item.association_id)
        rows = {
            question_key: Record(native, {
                "chat": Fact(chat_key[1], observed) if linked else Fact(),
                "turn": Fact(turn_key[1], observed) if linked else Fact(),
                "kind": Fact("answerable", observed), "state": Fact(question_state, observed),
                "prompt": Fact(item.title, observed), "ticket": Fact(ticket_state, observed),
            }, audience=audience),
        }
        for order, choice in enumerate(item.choices):
            choice_native = (*native, choice.choice_id)
            choice_key = key_for("choice", choice_native)
            if choice_key in rows:
                raise RegistryRejection("invalid_transition", "Conflicting question choice identity")
            rows[choice_key] = Record(choice_native, {
                "question": question_key[1], "display_order": order,
                "label": Fact(choice.label, observed), "detail": Fact(choice.detail, observed),
            }, audience=audience)
        if item.answer_id:
            answer_native = (item.answer_id,)
            state = ("recipient_delivered" if item.ticket_state == "delivered"
                     else "broker_received" if item.ticket_state == "received"
                     else "cancelled" if item.question_state == "cancelled" else "unconfirmed")
            rows[key_for("answer_attempt", answer_native)] = Record(answer_native, {
                "question": question_key[1], "choice": token("choice", (*native, item.selected_id)),
                "state": Fact(state, observed),
            }, audience=audience)
        return rows

    # recipient: Exact authenticated device and active key digest.
    # Caller holds the registry lock. The interaction owner never calls back into it.
    # Raises RegistryRejection for a collision or unavailable durable ledger.
    def _sync_questions(self, recipient: Recipient) -> None:
        if self._questions is None:
            return
        try:
            questions = self._questions.state_questions(*recipient)
        except RegistryStorageError as error:
            raise RegistryRejection("persistence", "Durable question storage is unavailable") from error
        staged = {key: row for key, row in self._records.items()
                  if row.audience is None or row.audience[0] != recipient[0]}
        for item in questions:
            # Exact evidence for this observation, even if several ledger
            # transitions are collected under the same registry revision.
            observed = self._new_observation(item.observed_ns, "restored" if item.restored else "broker")
            observed = replace(observed, identity=token("question-observation", (observed.identity, item.question_id)))
            for key, row in self._question_rows(item, staged, observed).items():
                prior = self._records.get(key)
                if key in staged or key in self._tombstones or (prior is not None and
                        (prior.native_id != row.native_id or prior.audience != row.audience)):
                    raise RegistryRejection("invalid_transition", "Conflicting question state identity")
                if prior is not None:
                    body = row.body.copy()
                    for name, field in body.items():
                        old = prior.body[name]
                        if isinstance(field, Fact) and isinstance(old, Fact) and field.value == old.value:
                            body[name] = old
                    row = replace(row, body=body)
                staged[key] = row
        self._commit(staged)

    # rows: The full resulting registry, before selection of delta upserts.
    # limits: Exact admitted receiver pool limits from authenticated negotiation.
    # Raises RegistryRejection if the resulting graph exceeds an admitted pool.
    def _check_limits(self, rows: list[tuple[Key, Record]], limits: dict[str, int]) -> None:
        sizes = {"sources": 1, "chats": 0, "turns": 0, "tool_definitions": 0,
                 "active_calls": 0, "terminal_calls": 0, **dict.fromkeys(QUESTION_LIMITS, 0)}
        text_bytes = len(PROVIDER) + len(NAMESPACE)
        for key, row in rows:
            kind = key[0]
            if kind == "tool_call":
                pool = "terminal_calls" if fact_at(row, "state").value == "returned" else "active_calls"
                sizes[pool] += 1
            elif kind == "question":
                pool = "open_questions" if fact_at(row, "state").value == "open" else "resolved_questions"
                sizes[pool] += 1
                text_bytes += len(fact_at(row, "prompt").value or "")
            elif kind == "choice":
                sizes["choices"] += 1
                text_bytes += len(fact_at(row, "label").value or "") + len(fact_at(row, "detail").value or "")
            elif kind == "answer_attempt":
                sizes["answer_attempts"] += 1
            else:
                sizes[POOL_KIND_BY_KIND[kind]] += 1
            if kind == "chat":
                text_bytes += len(fact_at(row, "title").value or "")
            if kind == "tool_definition":
                text_bytes += len(row.body["original_name"]) + len(fact_at(row, "display_label").value or "")
        sizes["evidence"] = len(self._referenced(rows))
        sizes["text_bytes"] = text_bytes
        for pool, size in sizes.items():
            if size > limits[pool]:
                raise RegistryRejection("capacity_exceeded", f"Negotiated {pool} cannot hold {size} records or bytes", pool)

    # request_id: Validated ID of the original authenticated request.
    # error: Exact producer-side cause; caller holds _lock.
    def _error(self, request_id: str, error: RegistryRejection) -> dict[str, Any]:
        detail = "Durable registry storage is unavailable" if error.code == "persistence" else str(error)
        return {
            "schema": SCHEMA, "request_id": request_id, "kind": "error",
            "code": error.code, "pool": error.pool, "detail": detail,
            "revision": hex64(self._revision),
        }

    # pc_id: Retained broker identity used in the authenticated source binding.
    # request: Validated resync or next document from the current wire session.
    # limits: Exact negotiated schema pool limits.
    # recipient: Authenticated device and active-key digest on a question-enabled session.
    def respond(self, pc_id: str, request: dict[str, Any], limits: dict[str, int],
                *, recipient: Recipient | None = None) -> dict[str, Any]:
        request_id = request["request_id"]
        binding = self.binding(pc_id, questions=recipient is not None)
        source_id = binding["source_id"]
        with self._lock:
            if self._storage_failure is not None:
                return self._error(request_id, self._storage_failure)
            if request.get("source_id") != source_id or request.get("epoch") != self._epoch:
                return self._error(request_id, RegistryRejection("wrong_binding", "Request names a different source or epoch"))
            snapshot = request["kind"] == "resync"
            if not snapshot and request["kind"] != "next":
                return self._error(request_id, RegistryRejection("malformed", "Source expects resync or next"))
            if recipient is not None:
                try:
                    self._sync_questions(recipient)
                except RegistryRejection as error:
                    return self._error(request_id, error)
            base = int(request["revision"] if snapshot else request["base_revision"], 16)
            revision = self._revision
            envelope = {"schema": SCHEMA, "request_id": request_id, "source_id": source_id,
                        "epoch": self._epoch, "revision": hex64(revision)}
            if base > revision:
                return self._error(request_id, RegistryRejection("revision_conflict", "Request revision is newer than the source"))
            if not snapshot and base == revision:
                if self._unobserved_capacity:
                    return self._error(request_id, next(reversed(self._unobserved_capacity.values())))
                return {**envelope, "kind": "no_change"}
            removed: list[Key] = []
            try:
                if not snapshot or (base != 0 and self._can_replay(base)):
                    revision, changed = self._select_delta(base, recipient)
                    generation = self._generation_at(revision)
                    removed = [key for key, before in changed.items() if before is not None
                               and (before.audience is None or before.audience == recipient)
                               and key not in generation]
                else:
                    generation = self._records
                    changed = {}
                visible = {key: row for key, row in generation.items()
                           if row.audience is None or row.audience == recipient}
                rows = list(visible.items())
                self._check_limits(rows, limits)
            except RegistryRejection as error:
                return self._error(request_id, error)
            if snapshot:
                # Complete coverage already authorizes removal of absent
                # unpinned history. Partial coverage needs explicit removals.
                removed = [key for key in removed if key[0] in self._unobserved_capacity]
            if len(removed) > limits["tombstones"]:
                return self._error(request_id, RegistryRejection(
                    "need_snapshot", "Response exceeds the negotiated tombstone pool", "tombstones"))
            upserts = rows if snapshot else [(key, visible[key]) for key in changed if key in visible]
            envelope["revision"] = hex64(revision)

            counts = dict.fromkeys(ENTITY_KINDS, 0)
            counts["source"] = 1
            for key, _ in rows:
                counts[key[0]] += 1
            coverage = []
            for kind in ENTITY_KINDS:
                supported = kind in SUPPORTED_KINDS or (recipient is not None and kind in QUESTION_KINDS)
                partial = kind in self._unobserved_capacity
                state = "partial" if partial else ("complete" if supported else "unsupported")
                coverage.append({"kind": kind, "state": state, "admitted": counts[kind],
                                 "omitted": None if partial else 0})
            records = [record_wire(key, row) for key, row in upserts]
            if snapshot:
                records.insert(0, {
                    "type": "source", "id": source_id, "provider": PROVIDER,
                    "native_namespace": NAMESPACE, "capabilities": binding["capabilities"],
                    "link": {"state": "unknown", "value": None, "evidence": None},
                })
            now_ns = time.monotonic_ns()
            evidence = [self._evidence_wire(item, now_ns) for item in self._referenced(upserts).values()]
            return {
                **envelope, "kind": "snapshot" if snapshot else "delta",
                "base_revision": ZERO if snapshot else hex64(base),
                "coverage": coverage, "evidence": evidence, "upserts": records,
                "remove": [{"type": kind, "id": identity, "revision": hex64(revision)}
                           for kind, identity in removed],
                "cue_watermark": ZERO, "cues": [],
            }

    # base: Receiver's exact committed revision. Caller holds _lock.
    def _can_replay(self, base: int) -> bool:
        return base == self._revision or bool(
            base != 0 and self._journal and base >= self._journal[0].revision - 1
        )

    # key: Kind and token of the previously known row.
    # row: Original row as seen by the receiver at the requested base revision.
    def _known_live(self, key: Key, row: Record) -> bool:
        if key[0] == "tool_call":
            return fact_at(row, "state").value == "running"
        if key[0] == "turn":
            return fact_at(row, "execution").value not in {"stopped", "interrupted"}
        if key[0] == "chat":
            return fact_at(row, "lifecycle").value != "closed"
        if key[0] == "question":
            return fact_at(row, "state").value == "open"
        if key[0] == "answer_attempt":
            return fact_at(row, "state").value == "broker_received"
        return False

    # base: Receiver's exact committed revision. Caller holds _lock.
    # recipient: Exact question audience or None for a core-only client.
    # Raises RegistryRejection for a missing or unavailable replay baseline.
    def _select_delta(self, base: int, recipient: Recipient | None) -> tuple[int, dict[Key, Record | None]]:
        if not self._can_replay(base):
            raise RegistryRejection("need_snapshot", "Requested base is outside retained source history")
        changed: dict[Key, Record | None] = {}
        target = base
        for entry in self._journal:
            if entry.revision <= base:
                continue
            # A receiver must observe the terminal transition before an
            # eviction can remove something that was live at its exact base.
            for key in entry.removed:
                original = changed.get(key, entry.before[key])
                if (original is not None and (original.audience is None or original.audience == recipient)
                        and self._known_live(key, original)):
                    return target, changed
            for key, original in entry.before.items():
                changed.setdefault(key, original)
            target = entry.revision
        return target, changed

    # revision: Retained generation target chosen by _select_delta.
    def _generation_at(self, revision: int) -> dict[Key, Record]:
        if revision == self._revision:
            return self._records
        generation = self._records.copy()
        for entry in reversed(self._journal):
            if entry.revision <= revision:
                break
            for key, original in entry.before.items():
                if original is None:
                    del generation[key]
                else:
                    generation[key] = original
        return generation
