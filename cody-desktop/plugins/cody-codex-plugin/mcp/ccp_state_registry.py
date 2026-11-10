"""Bounded source registry for the broker's authenticated Cody state producer."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import hashlib
import json
import secrets
import threading
import time
from typing import Any

from ccp_codex_hooks import CodexHook, HookEvent, ToolKind
from ccp_tool_labels import tool_display_label


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
ENTITY_KINDS = SUPPORTED_KINDS + (
    "agent_task", "external_operation", "question", "choice", "answer_attempt", "wait",
)
SOURCE_LIMITS = {
    "sources": 1, "chats": 64, "turns": 192, "tool_definitions": 128,
    "active_calls": 256, "terminal_calls": 128, "agents": 0, "operations": 0,
    "open_questions": 0, "resolved_questions": 0, "choices": 0,
    "answer_attempts": 0, "waits": 0, "evidence": 4096, "tombstones": 512,
    "text_bytes": 128 * 1024, "decode_scratch_bytes": 64 * 1024,
    "persistence_scratch_bytes": 32 * 1024,
}
POOL_KIND = {
    "chats": "chat", "turns": "turn", "tool_definitions": "tool_definition",
    "active_calls": "tool_call", "terminal_calls": "tool_call",
}
MAX_JOURNAL_TRANSACTIONS = 256
MAX_JOURNAL_BYTES = 512 * 1024
MAX_AGE_MS = 0xffffffff
LEASE_MS = 120000
TOOL_CATEGORIES = {ToolKind.COMMAND: "command", ToolKind.EDIT: "file_edit"}
HISTORICAL_EVENTS = frozenset({
    HookEvent.TOOL_START, HookEvent.TOOL_END, HookEvent.STOP, HookEvent.INTERRUPT,
})

Key = tuple[str, str]


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
    body: dict[str, str | Fact]
    created_ns: int = 0


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
# observed_ns: New observation from the same broker monotonic clock.
def newer(fact: Fact, observed_ns: int) -> bool:
    return fact.observation is None or observed_ns > fact.observation.observed_ns


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

    # No parameters: creates one process epoch with an empty observed registry.
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._epoch = hex64(secrets.randbits(64) or 1)
        self._revision = 1
        self._records: dict[Key, Record] = {}
        self._journal: deque[JournalEntry] = deque()
        self._journal_bytes = 0
        self._tombstones: dict[Key, int] = {}
        self._unobserved_capacity: dict[str, RegistryRejection] = {}

    # pc_id: Retained broker identity, never a user credential or UI label.
    def binding(self, pc_id: str) -> dict[str, Any]:
        return {
            "source_id": token("source", (pc_id, NAMESPACE)),
            "epoch": self._epoch, "provider": PROVIDER, "native_namespace": NAMESPACE,
            "capabilities": dict(CAPABILITIES),
        }

    # staged: Isolated transaction candidate.
    # key: Typed wire identity derived from the exact provider key.
    # native_id: Full provider key, retained for collision detection.
    # Raises RegistryRejection for a conflicting identity or replayed tombstone.
    def _lookup(self, staged: dict[Key, Record], key: Key, native_id: tuple[str, ...]) -> Record | None:
        row = staged.get(key)
        if row is not None and row.native_id != native_id:
            raise RegistryRejection("invalid_transition", "Conflicting native identity token")
        if row is None and key in self._tombstones:
            raise RegistryRejection("invalid_transition", "Native identity belongs to retired history")
        return row

    # staged: Isolated transaction candidate.
    # key: Exact row being revised.
    # row: Existing row whose identity and unchanged fields remain intact.
    # fields: Whole replacement values for just the affected internal fields.
    def _replace(self, staged: dict[Key, Record], key: Key, row: Record, **fields: str | Fact) -> None:
        staged[key] = Record(row.native_id, {**row.body, **fields}, row.created_ns)

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
        if native.emitted_ns <= turn.created_ns:
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
            if not newer(fact_at(call, "state"), native.emitted_ns):
                raise RegistryRejection("invalid_transition", "Tool completion precedes its admitted start")
        else:
            if not tool_name:
                raise RegistryRejection("missing_reference", "Tool completion without its start requires the original tool name")
            if not ended and fact_at(turn, "execution").value == "interrupted":
                if newer(fact_at(turn, "execution"), native.emitted_ns):
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
            if newer(execution, native.emitted_ns):
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
            if value != lifecycle.value and newer(lifecycle, native.emitted_ns):
                self._replace(staged, chat_key, chat, lifecycle=Fact(value, observed))
                chat = staged[chat_key]

        if event in {HookEvent.TOOL_START, HookEvent.TOOL_END}:
            lifecycle = fact_at(chat, "lifecycle")
            if event is HookEvent.TOOL_START and lifecycle.value == "closed" and newer(lifecycle, native.emitted_ns):
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
                becomes_current = newer(current, native.emitted_ns)
                self._reserve_turn(staged, chat_key[1] if becomes_current else "")
                staged[turn_key] = Record(turn_id, {
                    "chat": chat_key[1], "execution": Fact("running", observed),
                    "outcome": Fact(), "reply": Fact(),
                }, native.emitted_ns)
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
        if newer(execution, native.emitted_ns) and (value != execution.value or event is HookEvent.PROMPT):
            self._replace(staged, turn_key, turn, execution=Fact(value, observed))

    # observed_ns: Original observation time, on the broker's monotonic clock.
    # provenance: Exact acquisition boundary producing the new facts.
    def _new_observation(self, observed_ns: int, provenance: str) -> Observation:
        revision = self._revision + 1
        # Revisions restart at a new epoch, while observation identity must
        # remain distinct from old evidence retained by the same receiver.
        identity = token("observation", (self._epoch, hex64(revision)))
        return Observation(identity, revision, observed_ns, provenance)

    # native: Allowlisted, validated metadata from one documented command hook.
    # Raises RegistryRejection when a source transition fails atomically.
    def observe(self, native: CodexHook) -> bool:
        with self._lock:
            staged = self._records.copy()
            try:
                self._apply(staged, native, self._new_observation(native.emitted_ns, "native_event"))
            except RegistryRejection as error:
                if error.code == "capacity_exceeded" and error.pool in POOL_KIND:
                    self._unobserved_capacity[POOL_KIND[error.pool]] = error
                raise
            return self._commit(staged)

    # session_id: Exact native chat returned by the existing title source.
    # title: Complete reported title in the protocol's printable ASCII range.
    # observed_at: Original native read time on the local monotonic clock.
    def observe_title(self, session_id: str, title: str, observed_at: float) -> None:
        if not 0 <= len(title) <= 255 or any(not 32 <= ord(character) <= 126 for character in title):
            return
        observed_ns = int(observed_at * 1_000_000_000)
        if not 0 < observed_ns <= time.monotonic_ns():
            return
        with self._lock:
            native_id = (session_id,)
            key = key_for("chat", native_id)
            row = self._records.get(key)
            if row is None or row.native_id != native_id:
                return
            prior = fact_at(row, "title")
            if not newer(prior, observed_ns):
                return
            staged = self._records.copy()
            observed = self._new_observation(observed_ns, "native_read")
            self._replace(staged, key, row, title=Fact(title, observed))
            self._commit(staged)

    # rows: Records to upsert in one transaction.
    def _referenced(self, rows: list[tuple[Key, Record]]) -> dict[int, Observation]:
        return {field.observation.revision: field.observation
                for _, row in rows for field in row.body.values()
                if isinstance(field, Fact) and field.observation is not None}

    # observed: Immutable original native or broker fact evidence.
    # now_ns: Same-clock freeze time for the source age, not a renewal.
    def _evidence_wire(self, observed: Observation, now_ns: int) -> dict[str, Any]:
        elapsed_ns = now_ns - observed.observed_ns
        age_ms = (elapsed_ns + 999_999) // 1_000_000
        age_ms = age_ms if elapsed_ns >= 0 and age_ms <= MAX_AGE_MS else None
        return {
            "id": observed.identity, "origin_epoch": self._epoch,
            "origin_revision": hex64(observed.revision),
            "observed_at": {
                "basis": "monotonic_microseconds", "clock_id": self._epoch,
                "microseconds": hex64(observed.observed_ns // 1000),
            },
            "source_age_ms": age_ms, "lease_ms": LEASE_MS,
            "freshness": "unknown" if age_ms is None else ("current" if age_ms < LEASE_MS else "stale"),
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

    # rows: The full resulting registry, before selection of delta upserts.
    # limits: Exact admitted receiver pool limits from authenticated negotiation.
    # Raises RegistryRejection if the resulting graph exceeds an admitted pool.
    def _check_limits(self, rows: list[tuple[Key, Record]], limits: dict[str, int]) -> None:
        sizes = {"sources": 1, "chats": 0, "turns": 0, "tool_definitions": 0,
                 "active_calls": 0, "terminal_calls": 0}
        text_bytes = len(PROVIDER) + len(NAMESPACE)
        for key, row in rows:
            kind = key[0]
            if kind == "tool_call":
                pool = "terminal_calls" if fact_at(row, "state").value == "returned" else "active_calls"
                sizes[pool] += 1
            else:
                sizes[{"chat": "chats", "turn": "turns", "tool_definition": "tool_definitions"}[kind]] += 1
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
        return {
            "schema": SCHEMA, "request_id": request_id, "kind": "error",
            "code": error.code, "pool": error.pool, "detail": str(error),
            "revision": hex64(self._revision),
        }

    # pc_id: Retained broker identity used in the authenticated source binding.
    # request: Validated resync or next document from the current wire session.
    # limits: Exact negotiated schema pool limits.
    def respond(self, pc_id: str, request: dict[str, Any], limits: dict[str, int]) -> dict[str, Any]:
        request_id = request["request_id"]
        binding = self.binding(pc_id)
        source_id = binding["source_id"]
        with self._lock:
            if request.get("source_id") != source_id or request.get("epoch") != self._epoch:
                return self._error(request_id, RegistryRejection("wrong_binding", "Request names a different source or epoch"))
            snapshot = request["kind"] == "resync"
            if not snapshot and request["kind"] != "next":
                return self._error(request_id, RegistryRejection("malformed", "Source expects resync or next"))
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
                    revision, changed = self._select_delta(base)
                    generation = self._generation_at(revision)
                    removed = [key for key, before in changed.items()
                               if before is not None and key not in generation]
                else:
                    generation = self._records
                    changed = {}
                rows = list(generation.items())
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
            upserts = rows if snapshot else [(key, generation[key]) for key in changed if key in generation]
            envelope["revision"] = hex64(revision)

            counts = dict.fromkeys(ENTITY_KINDS, 0)
            counts["source"] = 1
            for key, _ in rows:
                counts[key[0]] += 1
            coverage = []
            for kind in ENTITY_KINDS:
                supported = kind in SUPPORTED_KINDS
                partial = kind in self._unobserved_capacity
                state = "partial" if partial else ("complete" if supported else "unsupported")
                coverage.append({"kind": kind, "state": state, "admitted": counts[kind],
                                 "omitted": None if partial else 0})
            records = [record_wire(key, row) for key, row in upserts]
            if snapshot:
                records.insert(0, {
                    "type": "source", "id": source_id, "provider": PROVIDER,
                    "native_namespace": NAMESPACE, "capabilities": dict(CAPABILITIES),
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
        return False

    # base: Receiver's exact committed revision. Caller holds _lock.
    # Raises RegistryRejection for a missing or unavailable replay baseline.
    def _select_delta(self, base: int) -> tuple[int, dict[Key, Record | None]]:
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
                if original is not None and self._known_live(key, original):
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
