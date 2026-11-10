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

"""Bounded Codex requests, recipient acknowledgements and CCP1 event payloads."""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import IntEnum
import hashlib
import hmac
import math
import re
import secrets
import sys
import threading
import time
from typing import Any

from ccp_codex_hooks import require_id
from ccp_state_registry import QUESTION_LIMITS, RegistryRejection, token
from ccp_state_storage import RegistryStorage, RegistryStorageError


MAX_EVENT_DATAGRAM_BYTES = 4096
MAX_TICKETS = 128
MAX_TTL_SECONDS = 180
INBOX_PAGE_SIZE = 3
TERMINAL_RETENTION_SECONDS = 600.0
TERMINAL_DISPLAY_SECONDS = 8.0
IDENTITY = re.compile(r"^[0-9a-f]{32}$")
CAPABILITY = re.compile(r"^[0-9a-f]{64}$")
DEVICE_ID = re.compile(r"^[0-9a-f]{12}$")
CHOICE_ID = re.compile(r"^[a-z][a-z0-9_]{0,11}$")
HEAD_ID = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")
STATES = frozenset(("pending", "received", "delivered", "cancelled", "expired", "none", "unavailable"))
DEVICE_STATES = frozenset(("none", "pending", "sending", "received", "delivered", "cancelled", "expired", "rejected"))
OPEN_DEVICE_STATES = frozenset(("pending", "sending", "received"))
ACK_STATES = frozenset(("received", "delivered", "expired", "cancelled", "conflict", "unknown"))
TERMINAL_STATES = frozenset(("delivered", "cancelled", "expired"))
CLOSED_TICKETS = TERMINAL_STATES | {"unavailable"}
DEVICE_TERMINAL_STATES = TERMINAL_STATES | {"rejected"}
ZERO_TICKET = "0" * 32
CHECK_STATES = frozenset(("pending", "running", "passed", "failed", "cancelled", "skipped", "unknown"))


class EventKind(IntEnum):
    NOTICE = 1
    QUESTION = 2
    APPROVAL_ON_PC = 3
    PR_PENDING = 4
    CHECKS_RUNNING = 5
    CHECKS_FAILED = 6
    CHECKS_PASSED = 7
    REPLY_READY = 8


class Tone(IntEnum):
    NEUTRAL = 0
    POSITIVE = 1
    NEGATIVE = 2
    INFO = 3


KIND_NAMES = {
    "notice": EventKind.NOTICE, "question": EventKind.QUESTION,
    "approval_on_pc": EventKind.APPROVAL_ON_PC, "pr_pending": EventKind.PR_PENDING,
    "checks_running": EventKind.CHECKS_RUNNING, "checks_failed": EventKind.CHECKS_FAILED,
    "checks_passed": EventKind.CHECKS_PASSED, "reply_ready": EventKind.REPLY_READY,
}
TONE_NAMES = {"neutral": Tone.NEUTRAL, "positive": Tone.POSITIVE,
              "negative": Tone.NEGATIVE, "info": Tone.INFO}


@dataclass(frozen=True)
class Choice:
    choice_id: str
    label: str
    detail: str
    tone: Tone


@dataclass(frozen=True)
class PendingQuestion:
    request_id: str
    context: str
    title: str


@dataclass(frozen=True)
class EventPayload:
    kind: EventKind
    context: str
    title: str
    body: str
    choices: tuple[Choice, ...] = ()
    selected_id: str = ""
    failures: tuple[str, ...] = ()
    progress_valid: bool = False
    passed: int = 0
    total: int = 0
    pending_question_count: int = 0
    pending_questions: tuple[PendingQuestion, ...] = ()


@dataclass
class Ticket:
    request_id: str
    capability: str
    device_id: str
    payload: EventPayload
    source: str
    created_at: float
    expires_at: float
    state: str = "pending"
    answer_id: str = ""
    selected_id: str = ""
    terminal_at: float | None = None
    association_id: str = ""
    native_session: str = ""
    published: bool = False
    device_terminal_state: str = ""
    active_on_device: bool = False
    question_id: str = ""
    question_state: str = ""
    capability_digest: str = ""
    device_closure_evidence: str = ""
    chat_id: str = ""
    turn_id: str = ""
    state_exposed: bool = False
    state_terminal_seen: bool = False
    state_observed_ns: int = 0
    state_restored: bool = False


@dataclass(frozen=True)
class QuestionProjection:
    question_id: str
    device_id: str
    association_id: str
    chat_id: str
    turn_id: str
    title: str
    choices: tuple[Choice, ...]
    question_state: str
    ticket_state: str
    answer_id: str
    selected_id: str
    observed_ns: int
    restored: bool


# ticket: Committed question from the private ledger.
def retains_question_state(ticket: Ticket) -> bool:
    return ticket.question_state == "open" or (ticket.state_exposed and not ticket.state_terminal_seen)


# device_id: Exact retained recipient.
# association_id: Digest of that recipient's current paired key.
# ticket: Committed ticket whose intended audience is checked.
def owns_question(device_id: str, association_id: str, ticket: Ticket) -> bool:
    return (ticket.payload.kind == EventKind.QUESTION and ticket.device_id == device_id
            and hmac.compare_digest(ticket.association_id, association_id))


# value: Caller-supplied text, retained exactly rather than projected or truncated.
# label: Non-secret field identity for rejection diagnostics.
# maximum: Maximum ASCII bytes admitted by the wire and display contract.
# empty: Whether an absent display label is permitted.
# Raises ValueError for an invalid type, length or non-printable/non-ASCII byte.
def bounded_text(value: Any, label: str, maximum: int, empty: bool = False) -> str:
    if not isinstance(value, str) or not (0 if empty else 1) <= len(value) <= maximum:
        raise ValueError(f"{label} must contain {'0' if empty else '1'}..{maximum} printable ASCII characters; use Codex for longer text")
    if any(not 32 <= ord(character) <= 126 for character in value):
        raise ValueError(f"{label} must be printable ASCII; use Codex for unsupported text")
    if not empty and not value.strip():
        raise ValueError(f"{label} must contain visible text")
    return value


# payload: Complete immutable content, including a separately selected answer if present.
# Raises ValueError for contradictory kinds, choices, check names or progress metadata.
def validate_payload(payload: EventPayload) -> None:
    if not isinstance(payload.kind, EventKind):
        raise ValueError("EVENT kind is invalid")
    bounded_text(payload.context, "context", 24, True)
    bounded_text(payload.title, "title", 24)
    bounded_text(payload.body, "body", 255 if payload.kind == EventKind.APPROVAL_ON_PC else 63)
    if payload.kind == EventKind.QUESTION:
        if len(payload.choices) not in (2, 3):
            raise ValueError("A Cody question requires exactly two or three choices")
    elif payload.choices or payload.selected_id:
        raise ValueError("Only a question may contain choices or an answer")
    identities = set()
    labels = set()
    for choice in payload.choices:
        if not isinstance(choice.choice_id, str) or CHOICE_ID.fullmatch(choice.choice_id) is None:
            raise ValueError("Choice IDs must match [a-z][a-z0-9_]{0,11}")
        if choice.choice_id in identities:
            raise ValueError("Choice IDs must be unique")
        identities.add(choice.choice_id)
        bounded_text(choice.label, "choice label", 63)
        if choice.label in labels:
            raise ValueError("Choice labels must be unambiguous and unique")
        labels.add(choice.label)
        bounded_text(choice.detail, "choice detail", 255)
        if not isinstance(choice.tone, Tone):
            raise ValueError("Choice tone is invalid")
    if payload.selected_id and payload.selected_id not in identities:
        raise ValueError("Selected choice is not part of this immutable question")
    if payload.kind == EventKind.CHECKS_FAILED:
        if not 1 <= len(payload.failures) <= 6:
            raise ValueError("Cody can show one to six complete failure names; inspect the remaining failures in Codex")
    elif payload.failures:
        raise ValueError("Failure names are only valid for checks_failed")
    if len(set(payload.failures)) != len(payload.failures):
        raise ValueError("Failure names are ambiguous duplicates; inspect them in Codex")
    for name in payload.failures:
        bounded_text(name, "failure name", 48)
    if type(payload.progress_valid) is not bool or type(payload.passed) is not int or type(payload.total) is not int:
        raise ValueError("Check progress types are invalid")
    if payload.progress_valid:
        if payload.kind not in (EventKind.CHECKS_RUNNING, EventKind.CHECKS_FAILED, EventKind.CHECKS_PASSED):
            raise ValueError("Check progress is not valid for this event kind")
        if not 0 <= payload.passed <= payload.total <= 65535 or payload.total == 0:
            raise ValueError("Check progress counts are invalid")
        if payload.kind == EventKind.CHECKS_FAILED and len(payload.failures) > payload.total - payload.passed:
            raise ValueError("Failure names contradict the complete check counts")
    elif payload.passed != 0 or payload.total != 0:
        raise ValueError("Unavailable progress must not contain numeric measurements")
    if payload.kind == EventKind.CHECKS_PASSED and (not payload.progress_valid or payload.passed != payload.total):
        raise ValueError("checks_passed requires a complete current all-passed check list")
    if (type(payload.pending_question_count) is not int or not 0 <= payload.pending_question_count <= MAX_TICKETS or
            len(payload.pending_questions) > 2 or len(payload.pending_questions) > payload.pending_question_count or
            (payload.kind != EventKind.QUESTION and (payload.pending_question_count or payload.pending_questions))):
        raise ValueError("EVENT pending-question projection is invalid")
    pending_ids = set()
    for entry in payload.pending_questions:
        if (not isinstance(entry, PendingQuestion) or IDENTITY.fullmatch(entry.request_id) is None or
                entry.request_id == "0" * 32 or entry.request_id in pending_ids):
            raise ValueError("EVENT pending-question identity is invalid or ambiguous")
        pending_ids.add(entry.request_id)
        bounded_text(entry.context, "pending context", 24, True)
        bounded_text(entry.title, "pending title", 24)


# payload: Validated immutable interaction data and optional recipient-confirmed choice.
# Returns lowercase hexadecimal bytes with length-prefixed bounded ASCII strings.
# Raises ValueError for incoherent content or an oversized encoded payload.
def encode_payload(payload: EventPayload) -> str:
    validate_payload(payload)
    result = bytearray((2, int(payload.kind)))
    for text in (payload.context, payload.title, payload.body):
        encoded = text.encode("ascii")
        result.append(len(encoded))
        result.extend(encoded)
    result.append(len(payload.choices))
    for choice in payload.choices:
        for text in (choice.choice_id, choice.label, choice.detail):
            encoded = text.encode("ascii")
            result.append(len(encoded))
            result.extend(encoded)
        result.append(int(choice.tone))
    selected = payload.selected_id.encode("ascii")
    result.append(len(selected))
    result.extend(selected)
    result.append(len(payload.failures))
    for name in payload.failures:
        encoded = name.encode("ascii")
        result.append(len(encoded))
        result.extend(encoded)
    result.append(int(payload.progress_valid))
    result.extend(payload.passed.to_bytes(2, "big"))
    result.extend(payload.total.to_bytes(2, "big"))
    result.extend((payload.pending_question_count, len(payload.pending_questions)))
    for entry in payload.pending_questions:
        for value in (entry.request_id, entry.context, entry.title):
            encoded = value.encode("ascii")
            result.append(len(encoded))
            result.extend(encoded)
    if len(result) > 1536:
        raise ValueError("EVENT content exceeds its bounded binary envelope; use Codex")
    return result.hex()


class PayloadReader:
    # self: Strict bounded decoder; no partially decoded payload is published.
    # value: Candidate lowercase hexadecimal EVENT data.
    # Raises ValueError for malformed encoding or an oversized binary envelope.
    def __init__(self, value: str) -> None:
        if not isinstance(value, str) or not value or len(value) > 3072 or len(value) % 2:
            raise ValueError("EVENT payload length is invalid")
        if any(character not in "0123456789abcdef" for character in value):
            raise ValueError("EVENT payload encoding is invalid")
        self.data = bytes.fromhex(value)
        self.offset = 0

    # self: Decoder cursor whose result is unavailable until the full payload validates.
    # count: Exact number of required bytes.
    # Raises ValueError for a truncated field.
    def take(self, count: int) -> bytes:
        if self.offset + count > len(self.data):
            raise ValueError("EVENT payload is truncated")
        value = self.data[self.offset:self.offset + count]
        self.offset += count
        return value

    # self: Decoder cursor at a required unsigned byte.
    # Raises ValueError for a truncated byte.
    def byte(self) -> int:
        return self.take(1)[0]

    # self: Decoder cursor at a length-prefixed ASCII value.
    # maximum: Per-field wire bound.
    # empty: Whether a zero-length field is meaningful.
    # Raises ValueError for a truncated, oversized or non-ASCII value.
    def text(self, maximum: int, empty: bool = False) -> str:
        count = self.byte()
        if count > maximum:
            raise ValueError("EVENT text exceeds its field bound")
        try:
            value = self.take(count).decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError("EVENT text is not ASCII") from error
        return bounded_text(value, "EVENT text", maximum, empty)


# value: Complete hex data authenticated by the enclosing EVENT before decoding.
# Raises ValueError for unknown versions, kinds, tones, trailing bytes or semantic contradictions.
def decode_payload(value: str) -> EventPayload:
    reader = PayloadReader(value)
    if reader.byte() != 2:
        raise ValueError("EVENT payload version is unsupported")
    kind = EventKind(reader.byte())
    context, title = reader.text(24, True), reader.text(24)
    body = reader.text(255 if kind == EventKind.APPROVAL_ON_PC else 63)
    count = reader.byte()
    if count > 3:
        raise ValueError("EVENT contains too many choices")
    choices = tuple(Choice(reader.text(12), reader.text(63), reader.text(255), Tone(reader.byte()))
                    for _ in range(count))
    selected = reader.text(12, True)
    failure_count = reader.byte()
    if failure_count > 6:
        raise ValueError("EVENT contains too many failure names")
    failures = tuple(reader.text(48) for _ in range(failure_count))
    progress = reader.byte()
    if progress not in (0, 1):
        raise ValueError("EVENT progress validity is invalid")
    passed, total = int.from_bytes(reader.take(2), "big"), int.from_bytes(reader.take(2), "big")
    pending_count, pending_entries = reader.byte(), reader.byte()
    if pending_entries > 2:
        raise ValueError("EVENT pending viewport contains more than two requests")
    pending = tuple(PendingQuestion(reader.text(32), reader.text(24, True), reader.text(24))
                    for _ in range(pending_entries))
    if reader.offset != len(reader.data):
        raise ValueError("EVENT payload contains trailing bytes")
    payload = EventPayload(kind, context, title, body, choices, selected, failures, bool(progress), passed, total,
                           pending_count, pending)
    validate_payload(payload)
    return payload


# kind: Requested read-only PR presentation, never inferred from lifecycle events.
# metadata: Exact head and explicit completeness/currentness of the caller's real check list.
# Returns whole failure names and progress only when its input proves current completeness.
# Raises ValueError for missing, stale, ambiguous or contradictory check metadata.
def check_metadata(kind: EventKind, metadata: Any) -> tuple[tuple[str, ...], bool, int, int]:
    fields = {"head", "checks_head", "complete", "current", "checks"}
    if not isinstance(metadata, dict) or set(metadata) != fields:
        raise ValueError("PR metadata requires exact head, checks_head, complete, current and checks")
    for name in ("head", "checks_head"):
        if not isinstance(metadata[name], str) or HEAD_ID.fullmatch(metadata[name]) is None:
            raise ValueError("PR head must be an exact lowercase 40- or 64-character commit identity")
    if type(metadata["complete"]) is not bool or type(metadata["current"]) is not bool:
        raise ValueError("PR completeness and currentness must be explicit booleans")
    checks = metadata["checks"]
    if not isinstance(checks, list) or len(checks) > 256:
        raise ValueError("PR check list exceeds its bounded input")
    names = set()
    failures = []
    passed = 0
    for check in checks:
        if not isinstance(check, dict) or set(check) != {"name", "state"}:
            raise ValueError("Every PR check requires its real name and state")
        name = bounded_text(check["name"], "check name", 256)
        if name in names:
            raise ValueError("Duplicate PR check names are ambiguous; inspect them in Codex")
        names.add(name)
        if not isinstance(check["state"], str) or check["state"] not in CHECK_STATES:
            raise ValueError("PR check state is unsupported")
        if check["state"] == "passed":
            passed += 1
        elif check["state"] == "failed":
            failures.append(name)
    exact = metadata["current"] and metadata["head"] == metadata["checks_head"]
    if not exact and kind in (EventKind.CHECKS_FAILED, EventKind.CHECKS_PASSED):
        raise ValueError("PR result does not describe the exact current head; inspect it in Codex")
    if kind == EventKind.PR_PENDING and checks:
        raise ValueError("pr_pending cannot contain a check result list")
    valid = bool(exact and metadata["complete"] and checks and kind != EventKind.PR_PENDING)
    if kind == EventKind.CHECKS_FAILED:
        if not 1 <= len(failures) <= 6:
            raise ValueError("Cody requires one to six complete failure names; inspect this result in Codex")
        for name in failures:
            bounded_text(name, "failure name", 48)
    else:
        failures = []
    if kind == EventKind.CHECKS_PASSED and (not valid or passed != len(checks)):
        raise ValueError("checks_passed requires all checks from the complete current head to have passed")
    return tuple(failures), valid, passed if valid else 0, len(checks) if valid else 0


# arguments: Exact MCP input for either an explicit question or a read-only notification.
# question: Selects the fixed tool purpose; the caller cannot request native approvals.
# Returns a fully validated immutable wire payload.
# Raises ValueError for unsupported fields, action kinds, text, choices or PR evidence.
def payload_from_arguments(arguments: dict[str, Any], question: bool) -> EventPayload:
    context = bounded_text(arguments.get("context", ""), "context", 24, True)
    title = bounded_text(arguments.get("title"), "title", 24)
    body = bounded_text(arguments.get("body"), "body", 63)
    if question:
        raw_choices = arguments.get("choices")
        if not isinstance(raw_choices, list) or len(raw_choices) not in (2, 3):
            raise ValueError("A Cody question requires exactly two or three choices")
        choices = []
        for item in raw_choices:
            if not isinstance(item, dict) or set(item) != {"id", "label", "detail", "tone"}:
                raise ValueError("Every choice requires id, label, detail and tone")
            if not isinstance(item["tone"], str) or item["tone"] not in TONE_NAMES:
                raise ValueError("Choice tone must be neutral, positive, negative or info")
            choices.append(Choice(item["id"], item["label"], item["detail"], TONE_NAMES[item["tone"]]))
        payload = EventPayload(EventKind.QUESTION, context, title, body, tuple(choices))
    else:
        name = arguments.get("kind", "notice")
        if not isinstance(name, str) or name not in KIND_NAMES or name in {"question", "approval_on_pc", "reply_ready"}:
            raise ValueError("Notification kind must be notice or a read-only PR state")
        kind = KIND_NAMES[name]
        if kind == EventKind.NOTICE:
            if "pr" in arguments:
                raise ValueError("A general notice does not accept PR metadata")
            payload = EventPayload(kind, context, title, body)
        else:
            failures, valid, passed, total = check_metadata(kind, arguments.get("pr"))
            payload = EventPayload(kind, context, title, body, failures=failures,
                                   progress_valid=valid, passed=passed, total=total)
    encode_payload(payload)
    return payload


class InteractionStore:
    # clock: Monotonic source for current-process deadlines and terminal retention.
    # storage: Optional atomic authenticated checkpoint provided by the exclusive broker.
    # owner_id: Retained PC identity binding the private question ledger.
    # Raises RegistryStorageError if a configured ledger cannot be safely opened.
    def __init__(self, clock=time.monotonic, *, storage: RegistryStorage | None = None,
                 owner_id: str | None = None) -> None:
        self._clock = clock
        self._condition = threading.Condition(threading.RLock())
        self._tickets: dict[str, Ticket] = {}
        self._storage = storage
        self._owner_id = owner_id
        self._storage_failure: RegistryStorageError | None = None
        if storage is not None:
            if not isinstance(owner_id, str) or IDENTITY.fullmatch(owner_id) is None:
                raise RegistryStorageError("Question ledger requires a retained PC identity")
            document = storage.load()
            if document is None:
                self._save(self._tickets)
            else:
                self._restore(document)

    # tickets: Complete candidate ticket set. Only questions are durable.
    # Raises RegistryStorageError if the authenticated commit does not complete.
    def _save(self, tickets: dict[str, Ticket]) -> None:
        if self._storage is None:
            return
        if self._storage_failure is not None:
            raise self._storage_failure
        rows = []
        for ticket in tickets.values():
            if ticket.payload.kind != EventKind.QUESTION:
                continue
            rows.append({
                "request_id": ticket.request_id, "question_id": ticket.question_id,
                "capability_digest": ticket.capability_digest,
                "device_id": ticket.device_id, "association": ticket.association_id,
                "payload": encode_payload(ticket.payload), "source": ticket.source,
                "state": ticket.state, "question_state": ticket.question_state,
                "answer_id": ticket.answer_id, "selected_id": ticket.selected_id,
                "published": ticket.published, "active": ticket.active_on_device,
                "device_terminal_state": ticket.device_terminal_state,
                "device_closure_evidence": ticket.device_closure_evidence,
                "chat_id": ticket.chat_id, "turn_id": ticket.turn_id,
                "state_exposed": ticket.state_exposed, "state_terminal_seen": ticket.state_terminal_seen,
            })
        try:
            self._storage.save({"version": 2, "owner": self._owner_id, "questions": rows})
        except RegistryStorageError as error:
            self._storage_failure = error
            raise

    # row: Exact retained semantic question, original ticket and optional answer.
    # now: New process clock, used only for closed retention, never for reviving deadlines.
    # version: Admitted authenticated checkpoint format.
    # Raises ValueError for inconsistent identity, immutable content or acknowledgement.
    def _stored_question(self, row: Any, now: float, version: int) -> Ticket:
        expected = {
            "request_id", "question_id", "capability_digest", "device_id", "association",
            "payload", "source", "state", "question_state", "answer_id", "selected_id",
            "published", "active", "device_terminal_state", "device_closure_evidence",
        }
        flags = {"published", "active"}
        if version == 2:
            flags |= {"state_exposed", "state_terminal_seen"}
            expected |= {"chat_id", "turn_id", *flags}
        if not isinstance(row, dict) or set(row) != expected:
            raise ValueError("Invalid question checkpoint fields")
        for name in expected - flags:
            if not isinstance(row[name], str):
                raise ValueError("Invalid question checkpoint value type")
        if any(type(row[name]) is not bool for name in flags):
            raise ValueError("Invalid question presentation flags")
        chat_id, turn_id = row.get("chat_id", ""), row.get("turn_id", "")
        if chat_id or turn_id:
            require_id(chat_id, "question chat_id")
            require_id(turn_id, "question turn_id")
        state_exposed = row.get("state_exposed", False)
        state_terminal_seen = row.get("state_terminal_seen", False)
        if state_terminal_seen and (not state_exposed or row["question_state"] == "open"):
            raise ValueError("Invalid question state acknowledgement")
        if (IDENTITY.fullmatch(row["request_id"]) is None or row["request_id"] == ZERO_TICKET
                or IDENTITY.fullmatch(row["question_id"]) is None or row["question_id"] == ZERO_TICKET
                or row["question_id"] == row["request_id"]
                or CAPABILITY.fullmatch(row["capability_digest"]) is None
                or DEVICE_ID.fullmatch(row["device_id"]) is None
                or CAPABILITY.fullmatch(row["association"]) is None
                or row["source"] not in {"LAN", "SIMULATED"}):
            raise ValueError("Invalid question checkpoint identity")
        payload = decode_payload(row["payload"])
        if (payload.kind != EventKind.QUESTION or payload.selected_id
                or payload.pending_question_count or payload.pending_questions
                or encode_payload(payload) != row["payload"]):
            raise ValueError("Question checkpoint payload is not an immutable original question")
        selected = row["selected_id"]
        answer_id = row["answer_id"]
        if bool(selected) != bool(answer_id) or (selected and (
                IDENTITY.fullmatch(answer_id) is None or answer_id == ZERO_TICKET
                or selected not in {choice.choice_id for choice in payload.choices})):
            raise ValueError("Question checkpoint answer has no exact immutable choice")
        state = row["state"]
        semantic = row["question_state"]
        device_state = row["device_terminal_state"]
        closure_evidence = row["device_closure_evidence"]
        if (state not in CLOSED_TICKETS | {"pending", "received"}
                or semantic not in {"open", "resolved", "cancelled"}
                or (state == "delivered") != (semantic == "resolved")
                or (state in {"pending", "received"} and semantic != "open")
                or (state == "cancelled" and semantic != "cancelled")
                or (state == "pending" and selected)
                or (state in {"received", "delivered"} and not selected)
                or (selected and not row["published"])
                or (row["active"] and not row["published"])
                or bool(device_state) != bool(closure_evidence)
                or (device_state and (
                    device_state not in DEVICE_TERMINAL_STATES or state not in CLOSED_TICKETS
                    or (device_state == "delivered" and state != "delivered")
                    or closure_evidence not in {"event_ack", "device_observation"}
                    or (closure_evidence == "device_observation" and state != "unavailable")
                    or not row["published"]))):
            raise ValueError("Question checkpoint lifecycle is inconsistent")
        # Monotonic ticket deadlines cannot reattach across broker processes.
        # Preserve the attempt and semantic identity, but close the old delivery path.
        if state in {"pending", "received"}:
            state = "unavailable"
        return Ticket(
            row["request_id"], "", row["device_id"], payload, row["source"], now, now,
            state=state, answer_id=answer_id, selected_id=selected, terminal_at=now,
            association_id=row["association"], published=row["published"],
            device_terminal_state=device_state, active_on_device=row["active"],
            question_id=row["question_id"], question_state=semantic,
            capability_digest=row["capability_digest"], device_closure_evidence=closure_evidence,
            chat_id=chat_id, turn_id=turn_id, state_exposed=state_exposed,
            state_terminal_seen=state_terminal_seen, state_restored=True,
        )

    # document: Authenticated complete checkpoint bound to the retained PC.
    # Raises RegistryStorageError for an untrustworthy ledger or restart transition.
    def _restore(self, document: dict[str, Any]) -> None:
        try:
            if (set(document) != {"version", "owner", "questions"}
                    or type(document["version"]) is not int or document["version"] not in {1, 2}
                    or document["owner"] != self._owner_id
                    or not isinstance(document["questions"], list)
                    or len(document["questions"]) > MAX_TICKETS):
                raise ValueError("Invalid question ledger version, owner or bounds")
            tickets: dict[str, Ticket] = {}
            identities: set[str] = set()
            active_associations: set[tuple[str, str]] = set()
            unavailable = False
            now = self._clock()
            for row in document["questions"]:
                ticket = self._stored_question(row, now, document["version"])
                present = {ticket.request_id, ticket.question_id}
                if ticket.answer_id:
                    present.add(ticket.answer_id)
                if len(present) != 2 + bool(ticket.answer_id) or present & identities:
                    raise ValueError("Duplicate question, ticket or answer checkpoint identity")
                identities.update(present)
                if ticket.active_on_device:
                    association = (ticket.device_id, ticket.association_id)
                    if association in active_associations:
                        raise ValueError("Question checkpoint has competing active presentations")
                    active_associations.add(association)
                tickets[ticket.request_id] = ticket
                unavailable |= row["state"] != ticket.state
        except ValueError as error:
            raise RegistryStorageError(f"Question ledger validation failed: {error}") from error
        if unavailable:
            self._save(tickets)
        self._tickets = tickets

    # changes: Whole replacements/deletions prepared under the condition lock.
    # Raises RegistryStorageError before publishing an uncommitted durable mutation.
    def _commit(self, changes: dict[str, Ticket | None]) -> None:
        if not changes:
            return
        staged = self._tickets.copy()
        durable = False
        for identity, candidate in changes.items():
            prior = staged.get(identity)
            if candidate == prior:
                continue
            ticket = candidate if candidate is not None else prior
            durable |= ticket is not None and ticket.payload.kind == EventKind.QUESTION
            if candidate is None:
                staged.pop(identity, None)
            else:
                if candidate.payload.kind == EventKind.QUESTION and (
                    prior is None or (prior.state, prior.question_state, prior.selected_id, prior.chat_id, prior.turn_id)
                    != (candidate.state, candidate.question_state, candidate.selected_id,
                        candidate.chat_id, candidate.turn_id)
                ):
                    candidate = replace(candidate, state_observed_ns=time.monotonic_ns(), state_restored=False)
                staged[identity] = candidate
        if durable:
            self._save(staged)
        self._tickets = staged
        self._condition.notify_all()

    # No parameters: expire delivery tickets without resolving their semantic questions.
    # Raises RegistryStorageError if the terminal transport transition cannot be retained.
    def _expire(self) -> None:
        now = self._clock()
        changes: dict[str, Ticket | None] = {}
        for identity, ticket in self._tickets.items():
            if ticket.state not in CLOSED_TICKETS and now >= ticket.expires_at:
                changes[identity] = replace(ticket, state="expired", terminal_at=now)
                continue
            if ticket.terminal_at is None or now - ticket.terminal_at < TERMINAL_RETENTION_SECONDS:
                continue
            if self._storage is not None and ticket.payload.kind == EventKind.QUESTION:
                if (ticket.question_state == "open"
                        or (ticket.active_on_device and not ticket.device_terminal_state)
                        or (ticket.state_exposed and not ticket.state_terminal_seen)):
                    continue
            changes[identity] = None
        self._commit(changes)

    # No parameters: produce a unique opaque identity in the bounded interaction registry.
    def _new_id(self) -> str:
        used = {identity for ticket in self._tickets.values()
                for identity in (ticket.request_id, ticket.question_id, ticket.answer_id)}
        identity = secrets.token_hex(16)
        while identity == ZERO_TICKET or identity in used:
            identity = secrets.token_hex(16)
        return identity

    # self: Sole interaction owner, independent of the activity snapshot.
    # device_id: Explicit retained target identity.
    # payload: Complete immutable request already admitted by the tool or genuine native hook.
    # source: LAN or SIMULATED source label derived from the authenticated target observation.
    # ttl_seconds: Bounded complete request/recipient-acknowledgement lifetime.
    # association_id: Internal digest binding this ticket to the active association, never a wire identity.
    # native_session: Genuine hook identity retiring notices at lifecycle or resumed-activity boundaries.
    # Raises ValueError if the target/content/TTL is invalid or a bounded queue is occupied.
    def enqueue(self, device_id: str, payload: EventPayload, source: str, ttl_seconds: int = 180,
                association_id: str = "", native_session: str = "") -> dict[str, Any]:
        if not isinstance(device_id, str) or DEVICE_ID.fullmatch(device_id) is None:
            raise ValueError("A retained Cody device_id is required")
        if source not in {"LAN", "SIMULATED", "UNOBSERVED"}:
            raise ValueError("Interaction target source is invalid")
        if type(ttl_seconds) is not int or not 15 <= ttl_seconds <= MAX_TTL_SECONDS:
            raise ValueError("ttl_seconds must be between 15 and 180")
        if association_id and CAPABILITY.fullmatch(association_id) is None:
            raise ValueError("Interaction association identity is invalid")
        question = payload.kind == EventKind.QUESTION
        if question and self._storage is not None and (not association_id or source == "UNOBSERVED"):
            raise ValueError("Durable question requires a confirmed active association")
        if question and (payload.selected_id or payload.pending_question_count or payload.pending_questions):
            raise ValueError("Question admission requires the original unanswered payload")
        encoded = encode_payload(payload)
        # Reserve the worst-case selected answer and envelope before mutating the queue.
        if len(encoded) + 740 > MAX_EVENT_DATAGRAM_BYTES:
            raise ValueError("EVENT exceeds the 4096-byte envelope including queue summaries; use Codex")
        with self._condition:
            self._expire()
            if len(self._tickets) >= MAX_TICKETS:
                raise ValueError("CCP request registry is full; wait for terminal records to expire")
            changes: dict[str, Ticket | None] = {}
            now = self._clock()
            if not question:
                for item in self._tickets.values():
                    if (item.device_id == device_id and item.state not in CLOSED_TICKETS
                            and item.payload.kind != EventKind.QUESTION
                            and ((native_session and item.native_session == native_session and item.payload.kind == payload.kind)
                                 or (not native_session and not item.native_session))):
                        changes[item.request_id] = replace(item, state="cancelled", terminal_at=now)
            # Every unsettled question may resolve while Cody is offline.
            # Keep enough room to publish all terminal states before removing them.
            if question and association_id and sum(
                owns_question(device_id, association_id, item) and retains_question_state(item)
                for item in self._tickets.values()
            ) >= QUESTION_LIMITS["resolved_questions"]:
                raise ValueError("Cody's unsettled question pool is full")
            request_id = self._new_id()
            question_id = self._new_id() if question else ""
            while question and question_id == request_id:
                question_id = self._new_id()
            capability = secrets.token_hex(32)
            durable = question and self._storage is not None
            ticket = Ticket(
                request_id, "" if durable else capability, device_id, payload, source, now, now + ttl_seconds,
                association_id=association_id, native_session=native_session,
                question_id=question_id, question_state="open" if question else "",
                capability_digest=hashlib.sha256(bytes.fromhex(capability)).hexdigest() if durable else "",
            )
            changes[request_id] = ticket
            self._commit(changes)
            result = self._public(ticket)
            result["capability"] = capability
            return result

    # request_id: Exact successful question ticket returned to the documented Codex tool hook.
    # chat_id: Native chat identity from that hook.
    # turn_id: Native turn identity from the same invocation.
    # Raises ValueError on a conflicting association and preserves storage failures.
    def associate_question(self, request_id: str, chat_id: str, turn_id: str) -> None:
        if not isinstance(request_id, str) or IDENTITY.fullmatch(request_id) is None:
            raise ValueError("Invalid Codex question ticket")
        require_id(chat_id, "question chat_id")
        require_id(turn_id, "question turn_id")
        with self._condition:
            ticket = self._tickets.get(request_id)
            if ticket is None or ticket.payload.kind != EventKind.QUESTION:
                raise ValueError("Codex hook question ticket is no longer retained")
            if self._storage_failure is not None:
                raise self._storage_failure
            if ticket.chat_id:
                if (ticket.chat_id, ticket.turn_id) != (chat_id, turn_id):
                    raise ValueError("Codex hook question already belongs to a different invocation")
                return
            self._commit({request_id: replace(ticket, chat_id=chat_id, turn_id=turn_id)})

    # No parameters: exact turn identities pinned by retained semantic questions.
    def associated_question_turns(self) -> frozenset[tuple[str, str]]:
        with self._condition:
            return frozenset((item.chat_id, item.turn_id) for item in self._tickets.values()
                             if item.chat_id and retains_question_state(item))

    # device_id: Authenticated retained recipient used by the state peer.
    # association_id: Digest of that peer's exact active key.
    # Raises RegistryStorageError on durable expiry or read failure.
    # Raises RegistryRejection if a legacy backlog cannot fit without losing unresolved state.
    def state_questions(self, device_id: str, association_id: str) -> tuple[QuestionProjection, ...]:
        with self._condition:
            if self._storage_failure is not None:
                raise self._storage_failure
            self._expire()
            owned = [item for item in self._tickets.values()
                     if owns_question(device_id, association_id, item) and retains_question_state(item)]
            if len(owned) > QUESTION_LIMITS["resolved_questions"]:
                raise RegistryRejection("capacity_exceeded",
                    "Retained question backlog exceeds safe terminal capacity", "resolved_questions")
            return tuple(QuestionProjection(
                item.question_id, item.device_id, item.association_id, item.chat_id, item.turn_id,
                item.payload.title, item.payload.choices, item.question_state, item.state,
                item.answer_id, item.selected_id, item.state_observed_ns, item.state_restored,
            ) for item in owned)

    # device_id: Authenticated retained recipient, scoped to the transfer.
    # association_id: Exact digest of the session's paired key.
    # open_ids: Public state IDs that will be put on the wire as open.
    # terminal_ids: Public IDs committed as terminal by the same device.
    # Raises RegistryStorageError if the conservative publication markers cannot be retained.
    # Raises RegistryRejection if a frozen open row can no longer be retained safely.
    def record_state_delivery(self, device_id: str, association_id: str,
                              open_ids: frozenset[str] = frozenset(),
                              terminal_ids: frozenset[str] = frozenset()) -> None:
        if not open_ids and not terminal_ids:
            return
        with self._condition:
            changes = {}
            unmatched_open = set(open_ids)
            unsettled = 0
            for request_id, item in self._tickets.items():
                if not owns_question(device_id, association_id, item):
                    continue
                public_id = token("question", (item.question_id,))
                if public_id in open_ids and not item.state_terminal_seen:
                    unmatched_open.discard(public_id)
                    if not item.state_exposed:
                        item = replace(item, state_exposed=True)
                        changes[request_id] = item
                elif (public_id in terminal_ids and item.state_exposed and not item.state_terminal_seen
                      and item.question_state != "open"):
                    item = replace(item, state_terminal_seen=True)
                    changes[request_id] = item
                unsettled += retains_question_state(item)
            if unmatched_open:
                raise RegistryRejection("need_snapshot", "An open question was retired before publication")
            if open_ids and unsettled > QUESTION_LIMITS["resolved_questions"]:
                # A frozen open ticket may have since closed. Retry with the
                # current graph instead of exceeding terminal capacity.
                raise RegistryRejection("need_snapshot", "Question admission changed before publication")
            self._commit(changes)

    # self: Locked request registry protecting the creator's response and cancellation capability.
    # request_id: Opaque ticket returned to one MCP caller.
    # capability: Caller-held 256-bit bearer capability, never sent to Cody or logs.
    # Raises ValueError for unknown, expired-retention or unauthorized lookup.
    def _owned(self, request_id: str, capability: str) -> Ticket:
        if not isinstance(request_id, str) or IDENTITY.fullmatch(request_id) is None:
            raise ValueError("Invalid request ticket")
        if not isinstance(capability, str) or CAPABILITY.fullmatch(capability) is None:
            raise ValueError("Invalid request capability")
        ticket = self._tickets.get(request_id)
        if ticket is None:
            raise ValueError("Unknown or unauthorized request ticket")
        expected, supplied = ticket.capability, capability
        if ticket.capability_digest:
            expected = ticket.capability_digest
            supplied = hashlib.sha256(bytes.fromhex(capability)).hexdigest()
        if not hmac.compare_digest(expected, supplied):
            raise ValueError("Unknown or unauthorized request ticket")
        if ticket.payload.kind == EventKind.QUESTION and self._storage_failure is not None:
            raise self._storage_failure
        return ticket

    # self: Owner returning the exact answer only to the original tool recipient.
    # ticket: Capability-authorized record, read while holding the condition lock.
    def _public(self, ticket: Ticket) -> dict[str, Any]:
        result: dict[str, Any] = {"request_id": ticket.request_id, "state": ticket.state,
                                  "source": ticket.source, "kind": ticket.payload.kind.name.lower(),
                                  "expires_in_seconds": max(0, math.ceil(ticket.expires_at - self._clock()))}
        if ticket.payload.kind == EventKind.QUESTION:
            if ticket.state == "delivered":
                delivery = "recipient_confirmed"
            elif ticket.state == "received":
                delivery = "broker_received"
            elif ticket.question_state == "cancelled":
                delivery = "cancelled"
            elif ticket.state == "pending":
                delivery = "pending"
            else:
                delivery = "unconfirmed" if ticket.selected_id else "unavailable"
            result.update(question_id=ticket.question_id, question_state=ticket.question_state,
                          delivery_state=delivery,
                          chat_id=token("chat", (ticket.chat_id,)) if ticket.chat_id else None,
                          turn_id=token("turn", (ticket.chat_id, ticket.turn_id)) if ticket.turn_id else None,
                          device_closure={"state": ticket.device_terminal_state,
                                          "evidence": ticket.device_closure_evidence}
                          if ticket.device_terminal_state else None)
        if ticket.selected_id and ticket.state in {"received", "delivered"}:
            choice = next(choice for choice in ticket.payload.choices if choice.choice_id == ticket.selected_id)
            result["answer"] = {"id": choice.choice_id, "label": choice.label, "detail": choice.detail}
            result["answer_id"] = ticket.answer_id
            result["recipient_acknowledged"] = ticket.state == "delivered"
        return result

    # self: Store whose condition permits UDP replies while a tool waits without the global MCP lock.
    # request_id: Immutable ticket owned by this caller.
    # capability: Creator capability protecting response contents and acknowledgements.
    # wait_ms: Optional bounded long-poll; zero returns the current state immediately.
    # acknowledge_answer_id: Previously read answer identity, never guessed or inferred from a UDP receipt.
    # Raises ValueError for invalid ownership, wait bounds or acknowledgement state.
    def poll(self, request_id: str, capability: str, wait_ms: int = 0,
             acknowledge_answer_id: str | None = None) -> dict[str, Any]:
        if type(wait_ms) is not int or not 0 <= wait_ms <= 15000:
            raise ValueError("wait_ms must be between 0 and 15000")
        deadline = time.monotonic() + wait_ms / 1000.0
        with self._condition:
            self._expire()
            ticket = self._owned(request_id, capability)
            if acknowledge_answer_id is not None:
                if (not isinstance(acknowledge_answer_id, str) or IDENTITY.fullmatch(acknowledge_answer_id) is None or not ticket.answer_id
                        or not hmac.compare_digest(ticket.answer_id, acknowledge_answer_id)):
                    raise ValueError("Recipient acknowledgement does not match the previously read answer")
                if ticket.state not in {"received", "delivered"}:
                    raise ValueError("The answer is no longer available for recipient acknowledgement")
                if ticket.state == "received":
                    ticket = replace(ticket, state="delivered", terminal_at=self._clock(), question_state="resolved")
                    self._commit({ticket.request_id: ticket})
            while ticket.state == "pending" and time.monotonic() < deadline:
                remaining = min(deadline - time.monotonic(), max(0.0, ticket.expires_at - self._clock()))
                if remaining <= 0:
                    break
                self._condition.wait(remaining)
                self._expire()
                ticket = self._owned(request_id, capability)
            self._expire()
            return self._public(self._owned(request_id, capability))

    # self: Request owner receiving an explicit cancellation from its original caller.
    # request_id: Ticket whose state may be retired, never implicitly answered.
    # capability: Creator capability; a background chat cannot cancel another caller's question.
    # Raises ValueError for invalid ownership or cancellation after confirmed delivery.
    def cancel(self, request_id: str, capability: str) -> dict[str, Any]:
        with self._condition:
            self._expire()
            ticket = self._owned(request_id, capability)
            if ticket.state == "delivered":
                raise ValueError("A recipient-confirmed answer cannot be cancelled")
            if ticket.payload.kind == EventKind.QUESTION and ticket.question_state == "open":
                state = ticket.state if ticket.state in CLOSED_TICKETS else "cancelled"
                ticket = replace(ticket, state=state, question_state="cancelled", terminal_at=self._clock())
                self._commit({ticket.request_id: ticket})
            elif ticket.state not in CLOSED_TICKETS:
                ticket = replace(ticket, state="cancelled", terminal_at=self._clock())
                self._commit({ticket.request_id: ticket})
            return self._public(ticket)

    # self: Authenticated UDP answer owner; no application work is executed by receiving this value.
    # device_id: Peer identity whose active key authenticated the reply.
    # request_id: Immutable request shown by that peer.
    # choice_id: Stable semantic option ID selected and explicitly confirmed on Cody.
    # association_id: Active peer-key digest; a new association cannot answer an old ticket.
    # Returns a receipt state; delivered requires a separate creator poll acknowledgement.
    def answer(self, device_id: str, request_id: str, choice_id: str, association_id: str = "") -> str:
        with self._condition:
            self._expire()
            ticket = self._tickets.get(request_id)
            if ticket is None or ticket.device_id != device_id or ticket.payload.kind != EventKind.QUESTION:
                return "unknown"
            if self._storage_failure is not None:
                raise self._storage_failure
            if (ticket.association_id and (association_id or self._storage is not None)
                    and not hmac.compare_digest(association_id, ticket.association_id)):
                if association_id and ticket.state not in CLOSED_TICKETS:
                    retired = self._retire_association(ticket, self._clock())
                    self._commit({ticket.request_id: retired})
                return "unknown"
            if choice_id not in {choice.choice_id for choice in ticket.payload.choices}:
                return "conflict"
            if ticket.selected_id and ticket.selected_id != choice_id:
                return "conflict"
            if ticket.state == "unavailable":
                return "unknown"
            if ticket.state in {"cancelled", "expired"}:
                return ticket.state
            if self._storage is not None and not ticket.published:
                return "unknown"
            changes: dict[str, Ticket | None] = {}
            if ticket.published:
                for item in self._tickets.values():
                    if (item.device_id == device_id and item.association_id == ticket.association_id
                            and item.payload.kind == EventKind.QUESTION):
                        active = item.request_id == ticket.request_id
                        if item.active_on_device != active:
                            changes[item.request_id] = replace(item, active_on_device=active)
                ticket = changes.get(ticket.request_id, ticket)
            if not ticket.selected_id:
                ticket = replace(ticket, selected_id=choice_id, answer_id=self._new_id(), state="received")
                changes[ticket.request_id] = ticket
            # Receipt is emitted only after the exact immutable answer was committed.
            self._commit(changes)
            return ticket.state

    # ticket: Ticket bound to the retired active key.
    # now: Same-process monotonic time of the observed association replacement.
    def _retire_association(self, ticket: Ticket, now: float) -> Ticket:
        if self._storage is not None and ticket.payload.kind == EventKind.QUESTION:
            state = ticket.state if ticket.state in CLOSED_TICKETS else "unavailable"
            return replace(ticket, state=state, terminal_at=ticket.terminal_at or now,
                           active_on_device=False)
        if ticket.state in CLOSED_TICKETS:
            return replace(ticket, active_on_device=False)
        return replace(ticket, state="cancelled", terminal_at=now)

    # self: Automatic-notice owner; retiring a hook context never affects caller-owned questions.
    # session_id: Genuine native session, or None to retire all native notices.
    # kind: Optional informational kind; resumed activity must not retire reply-ready notices.
    def retire_native(self, session_id: str | None, kind: EventKind | None = None) -> None:
        with self._condition:
            self._expire()
            for ticket in self._tickets.values():
                if (ticket.payload.kind != EventKind.QUESTION and ticket.native_session
                        and (session_id is None or ticket.native_session == session_id)
                        and (kind is None or ticket.payload.kind == kind)
                        and ticket.state not in CLOSED_TICKETS):
                    ticket.state, ticket.terminal_at = "cancelled", self._clock()
                    if ticket.payload.kind == EventKind.APPROVAL_ON_PC:
                        chat = hashlib.sha256(ticket.native_session.encode("ascii")).hexdigest()[:12]
                        print(f"CCP approval notice: state=retired reason=native_lifecycle chat={chat}",
                              file=sys.stderr, flush=True)
                    self._condition.notify_all()

    # self: Owner recording authenticated device retirement separately from recipient delivery.
    # device_id: Active peer identity authenticated by the UDP responder.
    # request_id: Exact ticket previously published to this device.
    # state: Actual terminal device state, including local expiry or rejection.
    # association_id: Current active-key digest; a replacement cannot close another association's ticket.
    # Raises ValueError for unknown, unrelated, unpublished or nonterminal acknowledgements.
    def acknowledge_terminal(self, device_id: str, request_id: str, state: str,
                             association_id: str) -> None:
        with self._condition:
            self._expire()
            ticket = self._tickets.get(request_id)
            if (ticket is None or ticket.device_id != device_id or not ticket.published
                    or ticket.payload.kind != EventKind.QUESTION or ticket.state not in CLOSED_TICKETS):
                raise ValueError("Terminal acknowledgement has no published closed question")
            if state not in DEVICE_TERMINAL_STATES:
                raise ValueError("Terminal acknowledgement cannot close a live device request")
            if state == "delivered" and ticket.state != "delivered":
                raise ValueError("Device closure cannot claim delivery without recipient confirmation")
            if not association_id or not hmac.compare_digest(association_id, ticket.association_id):
                raise ValueError("Terminal acknowledgement belongs to a different association")
            if ticket.device_terminal_state and ticket.device_terminal_state != state:
                raise ValueError("Conflicting device terminal acknowledgement")
            # An expired device can have missed Delivered. Preserve its actual
            # local closure independently of the recipient-confirmed result.
            closed = replace(ticket, device_terminal_state=state, device_closure_evidence="event_ack")
            self._commit({ticket.request_id: closed})

    # self: Informational queue owner retiring only a notice actually dismissed on Cody.
    # device_id: Authenticated peer identity.
    # request_id: Exact published informational ticket, never a question.
    # association_id: Active authenticated association; receipt alone cannot retire another peer's notice.
    # Raises ValueError for unknown, unpublished, unrelated or question tickets.
    def acknowledge_notice(self, device_id: str, request_id: str, association_id: str) -> None:
        with self._condition:
            self._expire()
            ticket = self._tickets.get(request_id)
            if (ticket is None or ticket.device_id != device_id or not ticket.published
                    or ticket.payload.kind == EventKind.QUESTION or not association_id
                    or not hmac.compare_digest(association_id, ticket.association_id)):
                raise ValueError("Informational dismissal has no published notice in this association")
            if ticket.state == "pending":
                ticket.state, ticket.terminal_at = "cancelled", self._clock()
                self._condition.notify_all()

    # self: Publisher choosing a complete request snapshot without changing caller ownership.
    # device_id: Authenticated active Cody peer.
    # association_id: Current active-key digest.
    # offset: Requested first row, clamped if the queue has shrunk.
    # dismiss: Exact previously published ready reply, or the all-zero no-op.
    # Raises ValueError for a foreign dismissal. Returns a bounded binary summary.
    def inbox_page(self, device_id: str, association_id: str, offset: int, dismiss: str) -> str:
        with self._condition:
            self._expire()
            if dismiss != "0" * 32:
                ticket = self._tickets.get(dismiss)
                if ticket is not None:
                    if ticket.payload.kind != EventKind.REPLY_READY:
                        raise ValueError("Only ready replies can be dismissed through Events")
                    self.acknowledge_notice(device_id, dismiss, association_id)
            questions = []
            replies = []
            for ticket in self._tickets.values():
                if (ticket.device_id != device_id or ticket.state != "pending"
                        or not hmac.compare_digest(ticket.association_id, association_id)):
                    continue
                if ticket.payload.kind == EventKind.QUESTION:
                    questions.append(ticket)
                elif ticket.payload.kind == EventKind.REPLY_READY:
                    replies.append(ticket)
            tickets = questions + replies
            total = len(tickets)
            start = min(offset, max(0, total - INBOX_PAGE_SIZE))
            page = tickets[start:start + INBOX_PAGE_SIZE]
            encoded = bytearray((total, start, len(page)))
            for ticket in page:
                payload = ticket.payload
                question = payload.kind == EventKind.QUESTION
                title = payload.context if question else payload.body
                if not title or (not question and title == "Open Codex to read the reply"):
                    title = "Chat title unavailable"
                preview = payload.body if question else ""
                title_bytes, preview_bytes = title.encode("ascii"), preview.encode("ascii")
                encoded.append(int(payload.kind))
                encoded.extend(bytes.fromhex(ticket.request_id))
                encoded.append(len(title_bytes))
                encoded.extend(title_bytes)
                encoded.append(len(preview_bytes))
                encoded.extend(preview_bytes)
                if not question:
                    ticket.published = True
            return encoded.hex()

    # device_id: Authenticated active Cody peer.
    # association_id: Current active-key digest; replacement retires old requests without exposing their payload.
    # current_id: Question identity reported by the authenticated device, or all zeros for absence.
    # current_state: Actual device state; an unknown open question is retired before publishing a successor.
    # selection_request_id: Explicit signed queue navigation; zero means retain current ownership.
    # Returns request identity, lifecycle state, remaining TTL and immutable encoded payload.
    # Raises ValueError for an incoherent device observation.
    def snapshot(self, device_id: str, association_id: str = "",
                 current_id: str = "0" * 32, current_state: str = "none",
                 selection_request_id: str = "0" * 32) -> tuple[str, str, str, str]:
        if (IDENTITY.fullmatch(current_id) is None or current_state not in DEVICE_STATES
                or (current_id == "0" * 32) != (current_state == "none")):
            raise ValueError("Device question observation is incoherent")
        if (IDENTITY.fullmatch(selection_request_id) is None or
                (selection_request_id != "0" * 32 and current_state not in {"none", "pending"})):
            raise ValueError("Device pending-question selection is incoherent")
        with self._condition:
            self._expire()
            now = self._clock()
            changes: dict[str, Ticket | None] = {}
            relevant: dict[str, Ticket] = {}
            for item in self._tickets.values():
                if item.device_id != device_id:
                    continue
                if (item.association_id and (association_id or self._storage is not None)
                        and not hmac.compare_digest(association_id, item.association_id)):
                    if association_id and (item.state not in CLOSED_TICKETS or item.active_on_device):
                        changes[item.request_id] = self._retire_association(item, now)
                    continue
                if item.payload.kind == EventKind.QUESTION and self._storage_failure is not None:
                    raise self._storage_failure
                if current_id == ZERO_TICKET and item.state == "unavailable" and item.active_on_device:
                    # The peer reports no displayed ticket. This clears the
                    # presentation claim without claiming an EVENTACK.
                    item = replace(item, active_on_device=False)
                    changes[item.request_id] = item
                relevant[item.request_id] = item

            current = relevant.get(current_id)
            if current_state in OPEN_DEVICE_STATES and (
                    current is None or not current.published or current.payload.kind != EventKind.QUESTION
                    or current.state == "unavailable"):
                self._commit(changes)
                return current_id, "unavailable", "0", "-"
            if current is not None and current.published and current.payload.kind == EventKind.QUESTION:
                if current.state == "unavailable":
                    # A recovered ticket has no live delivery path. Its
                    # authenticated terminal FIND can establish device closure,
                    # without creating recipient confirmation or replaying data.
                    if (current_state in DEVICE_TERMINAL_STATES - {"delivered"}
                            and not current.device_terminal_state):
                        current = replace(current, active_on_device=False, device_terminal_state=current_state,
                                          device_closure_evidence="device_observation")
                        changes[current_id] = current
                        relevant[current_id] = current
                else:
                    for identity, item in relevant.items():
                        if item.payload.kind == EventKind.QUESTION and item.active_on_device != (identity == current_id):
                            item = replace(item, active_on_device=identity == current_id)
                            changes[identity] = item
                            relevant[identity] = item
                    current = relevant[current_id]

            questions = [item for item in relevant.values() if item.payload.kind == EventKind.QUESTION
                         and item.state not in CLOSED_TICKETS]
            closing = [item for item in relevant.values() if item.payload.kind == EventKind.QUESTION
                       and item.active_on_device and item.published and item.state in TERMINAL_STATES
                       and not item.device_terminal_state]
            recent = [item for item in relevant.values() if item.payload.kind == EventKind.QUESTION
                      and item.active_on_device and item.state in TERMINAL_STATES and item.terminal_at is not None
                      and now - item.terminal_at < TERMINAL_DISPLAY_SECONDS]
            notices = [item for item in relevant.values() if item.payload.kind != EventKind.QUESTION
                       and item.state == "pending"]
            target = None
            if selection_request_id != ZERO_TICKET:
                if current_state == "none" or (current is not None and current.state == "pending" and not current.selected_id):
                    target = next((item for item in questions if item.request_id == selection_request_id
                                   and item.state == "pending" and not item.selected_id), None)
                if target is None:
                    print("CCP interactions: requested pending question is no longer selectable", file=sys.stderr)
            # A terminal question remains ahead of its successor until Cody
            # actually closes the retained presentation.
            active_current = current if current in questions else None
            ticket = (closing[0] if closing else target if target is not None else
                      active_current if active_current is not None else questions[0] if questions else
                      recent[-1] if recent else notices[0] if notices else None)
            if ticket is None:
                self._commit(changes)
                return ZERO_TICKET, "none", "0", "-"

            payload = replace(ticket.payload, selected_id=ticket.selected_id)
            if ticket.payload.kind == EventKind.QUESTION:
                if current_id == ZERO_TICKET:
                    for identity, item in relevant.items():
                        if item.payload.kind == EventKind.QUESTION and item.active_on_device != (identity == ticket.request_id):
                            changes[identity] = replace(item, active_on_device=identity == ticket.request_id)
                pending = [item for item in questions if item.state == "pending" and not item.selected_id]
                if ticket in pending:
                    first = pending.index(ticket)
                    pending = pending[first:] + pending[:first]
                entries = tuple(PendingQuestion(item.request_id, item.payload.context, item.payload.title)
                                for item in pending[:2])
                payload = replace(payload, pending_question_count=len(questions), pending_questions=entries)
            ticket = replace(changes.get(ticket.request_id, ticket), published=True)
            changes[ticket.request_id] = ticket
            # Persist original ticket ownership before publishing the offer.
            self._commit(changes)
            ttl = max(0, min(MAX_TTL_SECONDS, math.ceil(ticket.expires_at - now)))
            return ticket.request_id, ticket.state, str(ttl), encode_payload(payload)


# No parameters: exposes semantic requests, private response access and read-only PR notifications.
def tool_catalog() -> list[dict[str, Any]]:
    text24 = {"type": "string", "minLength": 1, "maxLength": 24, "pattern": "^[ -~]+$"}
    text63 = {"type": "string", "minLength": 1, "maxLength": 63, "pattern": "^[ -~]+$"}
    common = {
        "device_id": {"type": "string", "pattern": "^[0-9a-f]{12}$"},
        "context": {"type": "string", "maxLength": 24, "pattern": "^[ -~]*$",
                    "description": "Caller-declared label only; never an authenticated chat identity or focus claim."},
        "title": text24, "body": text63,
        "ttl_seconds": {"type": "integer", "minimum": 15, "maximum": 180, "default": 180},
        "include_simulator": {"type": "boolean", "default": False,
                              "description": "Enable only for an explicitly requested SIMULATED Cody Studio target."},
    }
    ownership = {
        "request_id": {"type": "string", "pattern": "^[0-9a-f]{32}$"},
        "capability": {"type": "string", "pattern": "^[0-9a-f]{64}$",
                       "description": "Private capability returned only to the creating tool call; never print it for the user."},
    }
    annotations = {"readOnlyHint": False, "destructiveHint": False, "openWorldHint": False}
    return [
        {
            "name": "ccp_ask_question", "title": "Ask a question on Cody",
            "description": "Send a self-contained semantic question with 2 or 3 choices. Not a native permission approval. Keep ticket/capability; poll and explicitly acknowledge the returned answer before acting.",
            "inputSchema": {"type": "object", "properties": {**common,
                "choices": {"type": "array", "minItems": 2, "maxItems": 3, "items": {
                    "type": "object", "properties": {
                        "id": {"type": "string", "pattern": "^[a-z][a-z0-9_]{0,11}$"},
                        "label": {"type": "string", "minLength": 1, "maxLength": 63, "pattern": "^[ -~]+$"},
                        "detail": {"type": "string", "minLength": 1, "maxLength": 255, "pattern": "^[ -~]+$"},
                        "tone": {"type": "string", "enum": list(TONE_NAMES)},
                    }, "required": ["id", "label", "detail", "tone"], "additionalProperties": False}}},
                "required": ["device_id", "title", "body", "choices"], "additionalProperties": False},
            "annotations": annotations,
        },
        {
            "name": "ccp_publish_notification", "title": "Notify Cody",
            "description": "Publish a real notification or read-only PR/check snapshot from known evidence. PR results require exact head and real check metadata; no merge, approval, retry, comments or logs. Never infer success from Stop.",
            "inputSchema": {"type": "object", "properties": {**common,
                "kind": {"type": "string", "enum": ["notice", "pr_pending", "checks_running", "checks_failed", "checks_passed"], "default": "notice"},
                "pr": {"type": "object", "properties": {
                    "head": {"type": "string", "pattern": "^(?:[0-9a-f]{40}|[0-9a-f]{64})$"},
                    "checks_head": {"type": "string", "pattern": "^(?:[0-9a-f]{40}|[0-9a-f]{64})$"},
                    "complete": {"type": "boolean"}, "current": {"type": "boolean"},
                    "checks": {"type": "array", "maxItems": 256, "items": {
                        "type": "object", "properties": {
                            "name": {"type": "string", "minLength": 1, "maxLength": 256, "pattern": "^[ -~]+$"},
                            "state": {"type": "string", "enum": sorted(CHECK_STATES)},
                        }, "required": ["name", "state"], "additionalProperties": False}},
                }, "required": ["head", "checks_head", "complete", "current", "checks"], "additionalProperties": False}},
                "required": ["device_id", "title", "body"], "additionalProperties": False},
            "annotations": annotations,
        },
        {
            "name": "ccp_poll_request", "title": "Read or acknowledge Cody's reply",
            "description": "Read the owned question, transport and device closure. After receiving answer_id, acknowledge that exact ID before acting. Only recipient_confirmed records a delivered answer. Unavailable or unconfirmed delivery can leave the question open.",
            "inputSchema": {"type": "object", "properties": {**ownership,
                "wait_ms": {"type": "integer", "minimum": 0, "maximum": 15000, "default": 0},
                "acknowledge_answer_id": {"type": "string", "pattern": "^[0-9a-f]{32}$"},
            }, "required": ["request_id", "capability"], "additionalProperties": False},
            "annotations": annotations,
        },
        {
            "name": "ccp_cancel_request", "title": "Cancel an owned Cody request",
            "description": "Explicitly cancel this caller's unresolved semantic question, including one whose transport expired. Recipient-confirmed answers cannot be cancelled.",
            "inputSchema": {"type": "object", "properties": ownership,
                            "required": ["request_id", "capability"], "additionalProperties": False},
            "annotations": annotations,
        },
    ]
