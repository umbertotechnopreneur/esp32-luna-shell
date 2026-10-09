"""Bounded Codex requests, recipient acknowledgements and CCP1 event payloads."""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import IntEnum
import hmac
import math
import re
import secrets
import threading
import time
from typing import Any


MAX_EVENT_DATAGRAM_BYTES = 1280
MAX_TICKETS = 128
MAX_TTL_SECONDS = 180
TERMINAL_RETENTION_SECONDS = 600.0
TERMINAL_DISPLAY_SECONDS = 8.0
IDENTITY = re.compile(r"^[0-9a-f]{32}$")
CAPABILITY = re.compile(r"^[0-9a-f]{64}$")
DEVICE_ID = re.compile(r"^[0-9a-f]{12}$")
CHOICE_ID = re.compile(r"^[a-z][a-z0-9_]{0,11}$")
HEAD_ID = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")
STATES = frozenset(("pending", "received", "delivered", "cancelled", "expired", "none"))
ACK_STATES = frozenset(("received", "delivered", "expired", "cancelled", "conflict", "unknown"))
TERMINAL_STATES = frozenset(("delivered", "cancelled", "expired"))
CHECK_STATES = frozenset(("pending", "running", "passed", "failed", "cancelled", "skipped", "unknown"))


class EventKind(IntEnum):
    NOTICE = 1
    QUESTION = 2
    APPROVAL_ON_PC = 3
    PR_PENDING = 4
    CHECKS_RUNNING = 5
    CHECKS_FAILED = 6
    CHECKS_PASSED = 7


class Tone(IntEnum):
    NEUTRAL = 0
    POSITIVE = 1
    NEGATIVE = 2
    INFO = 3


KIND_NAMES = {
    "notice": EventKind.NOTICE, "question": EventKind.QUESTION,
    "approval_on_pc": EventKind.APPROVAL_ON_PC, "pr_pending": EventKind.PR_PENDING,
    "checks_running": EventKind.CHECKS_RUNNING, "checks_failed": EventKind.CHECKS_FAILED,
    "checks_passed": EventKind.CHECKS_PASSED,
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
    bounded_text(payload.body, "body", 63)
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
        bounded_text(choice.label, "choice label", 12)
        if choice.label in labels:
            raise ValueError("Choice labels must be unambiguous and unique")
        labels.add(choice.label)
        bounded_text(choice.detail, "choice detail", 63)
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


# payload: Validated immutable interaction data and optional recipient-confirmed choice.
# Returns lowercase hexadecimal bytes with length-prefixed bounded ASCII strings.
# Raises ValueError for incoherent content or an oversized encoded payload.
def encode_payload(payload: EventPayload) -> str:
    validate_payload(payload)
    result = bytearray((1, int(payload.kind)))
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
    if len(result) > 512:
        raise ValueError("EVENT content exceeds its bounded binary envelope; use Codex")
    return result.hex()


class PayloadReader:
    # self: Strict bounded decoder; no partially decoded payload is published.
    # value: Candidate lowercase hexadecimal EVENT data.
    # Raises ValueError for malformed encoding or an oversized binary envelope.
    def __init__(self, value: str) -> None:
        if not isinstance(value, str) or not value or len(value) > 1024 or len(value) % 2:
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
    if reader.byte() != 1:
        raise ValueError("EVENT payload version is unsupported")
    kind = EventKind(reader.byte())
    context, title, body = reader.text(24, True), reader.text(24), reader.text(63)
    count = reader.byte()
    if count > 3:
        raise ValueError("EVENT contains too many choices")
    choices = tuple(Choice(reader.text(12), reader.text(12), reader.text(63), Tone(reader.byte()))
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
    if reader.offset != len(reader.data):
        raise ValueError("EVENT payload contains trailing bytes")
    payload = EventPayload(kind, context, title, body, choices, selected, failures, bool(progress), passed, total)
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
        if not isinstance(name, str) or name not in KIND_NAMES or name in {"question", "approval_on_pc"}:
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
    # self: Volatile ticket owner; reboot cannot replay a request or manufacture an answer.
    # clock: Monotonic source for expiry, recipient deadlines and bounded retention.
    def __init__(self, clock=time.monotonic) -> None:
        self._clock = clock
        self._condition = threading.Condition(threading.RLock())
        self._tickets: dict[str, Ticket] = {}

    # self: Locked store pruning only expired terminal records, never silently evicting a live question.
    def _expire(self) -> None:
        now = self._clock()
        for ticket in self._tickets.values():
            if ticket.state not in TERMINAL_STATES and now >= ticket.expires_at:
                ticket.state, ticket.terminal_at = "expired", now
        self._tickets = {identity: ticket for identity, ticket in self._tickets.items()
                         if ticket.terminal_at is None or now - ticket.terminal_at < TERMINAL_RETENTION_SECONDS}

    # self: Sole interaction owner, independent of the activity snapshot.
    # device_id: Explicit retained target identity.
    # payload: Complete immutable request already admitted by the tool or genuine native hook.
    # source: LAN or SIMULATED source label derived from the authenticated target observation.
    # ttl_seconds: Bounded complete request/recipient-acknowledgement lifetime.
    # association_id: Internal digest binding this ticket to the active association, never a wire identity.
    # native_session: Genuine hook identity used only to retire a native notice when that turn ends.
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
        encoded = encode_payload(payload)
        # Reserve the worst-case selected answer and envelope before mutating the queue.
        if len(encoded) + 400 > MAX_EVENT_DATAGRAM_BYTES:
            raise ValueError("EVENT exceeds the 1280-byte envelope; use Codex for this content")
        with self._condition:
            self._expire()
            if len(self._tickets) >= MAX_TICKETS:
                raise ValueError("CCP request registry is full; wait for terminal records to expire")
            active = [item for item in self._tickets.values() if item.device_id == device_id
                      and item.state not in TERMINAL_STATES]
            if payload.kind == EventKind.QUESTION and any(item.payload.kind == EventKind.QUESTION for item in active):
                raise ValueError("Cody already has a pending question; its caller must answer or cancel it")
            now = self._clock()
            if payload.kind != EventKind.QUESTION:
                # Informational snapshots supersede one another. Keeping an
                # older PR snapshot queued would let old failures reappear after
                # a newer passed snapshot expires. Questions retain ownership.
                for item in active:
                    if item.payload.kind != EventKind.QUESTION:
                        item.state, item.terminal_at = "cancelled", now
            ticket = Ticket(secrets.token_hex(16), secrets.token_hex(32), device_id, payload, source,
                            now, now + ttl_seconds, association_id=association_id, native_session=native_session)
            while ticket.request_id == "0" * 32 or ticket.request_id in self._tickets:
                ticket.request_id = secrets.token_hex(16)
            self._tickets[ticket.request_id] = ticket
            self._condition.notify_all()
            return {"request_id": ticket.request_id, "capability": ticket.capability,
                    "state": ticket.state, "source": source, "expires_in_seconds": ttl_seconds}

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
        if ticket is None or not hmac.compare_digest(ticket.capability, capability):
            raise ValueError("Unknown or unauthorized request ticket")
        return ticket

    # self: Owner returning the exact answer only to the original tool recipient.
    # ticket: Capability-authorized record, read while holding the condition lock.
    def _public(self, ticket: Ticket) -> dict[str, Any]:
        result: dict[str, Any] = {"request_id": ticket.request_id, "state": ticket.state,
                                  "source": ticket.source, "kind": ticket.payload.kind.name.lower(),
                                  "expires_in_seconds": max(0, math.ceil(ticket.expires_at - self._clock()))}
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
                    ticket.state, ticket.terminal_at = "delivered", self._clock()
                    self._condition.notify_all()
            while ticket.state == "pending" and time.monotonic() < deadline:
                remaining = min(deadline - time.monotonic(), max(0.0, ticket.expires_at - self._clock()))
                if remaining <= 0:
                    break
                self._condition.wait(remaining)
                self._expire()
            self._expire()
            return self._public(ticket)

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
            if ticket.state not in TERMINAL_STATES:
                ticket.state, ticket.terminal_at = "cancelled", self._clock()
                self._condition.notify_all()
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
            if association_id and ticket.association_id and not hmac.compare_digest(association_id, ticket.association_id):
                if ticket.state not in TERMINAL_STATES:
                    ticket.state, ticket.terminal_at = "cancelled", self._clock()
                    self._condition.notify_all()
                return "unknown"
            if ticket.state in {"cancelled", "expired"}:
                return ticket.state
            if choice_id not in {choice.choice_id for choice in ticket.payload.choices}:
                return "conflict"
            if ticket.selected_id and ticket.selected_id != choice_id:
                return "conflict"
            if not ticket.selected_id:
                ticket.selected_id = choice_id
                ticket.answer_id = secrets.token_hex(16)
                ticket.state = "received"
                self._condition.notify_all()
            return ticket.state

    # self: Automatic-notice owner; retiring a hook context never affects caller-owned questions.
    # session_id: Genuine ended/interrupted session, or None after a new followed user prompt.
    def retire_native(self, session_id: str | None) -> None:
        with self._condition:
            self._expire()
            for ticket in self._tickets.values():
                if (ticket.native_session and (session_id is None or ticket.native_session == session_id)
                        and ticket.state not in TERMINAL_STATES):
                    ticket.state, ticket.terminal_at = "cancelled", self._clock()
                    self._condition.notify_all()

    # self: Publisher choosing a complete request snapshot without changing caller ownership.
    # device_id: Authenticated active Cody peer.
    # association_id: Current active-key digest; replacement retires old requests without exposing their payload.
    # Returns request identity, lifecycle state, remaining TTL and immutable encoded payload.
    def snapshot(self, device_id: str, association_id: str = "") -> tuple[str, str, str, str]:
        with self._condition:
            self._expire()
            now = self._clock()
            relevant = []
            for item in self._tickets.values():
                if item.device_id != device_id:
                    continue
                if association_id and item.association_id and not hmac.compare_digest(association_id, item.association_id):
                    if item.state not in TERMINAL_STATES:
                        item.state, item.terminal_at = "cancelled", now
                        self._condition.notify_all()
                    continue
                relevant.append(item)
            questions = [item for item in relevant if item.payload.kind == EventKind.QUESTION
                         and item.state not in TERMINAL_STATES]
            recent = [item for item in relevant if item.payload.kind == EventKind.QUESTION
                      and item.terminal_at is not None and now - item.terminal_at < TERMINAL_DISPLAY_SECONDS]
            notices = [item for item in relevant if item.payload.kind != EventKind.QUESTION and item.state == "pending"]
            ticket = (questions[-1] if questions else recent[-1] if recent else notices[-1] if notices else None)
            if ticket is None:
                return "0" * 32, "none", "0", "-"
            payload = replace(ticket.payload, selected_id=ticket.selected_id)
            # Ceil prevents a live last-fraction-of-a-second request from being
            # published as Pending with ttl=0; the host deadline remains exact.
            ttl = max(0, min(MAX_TTL_SECONDS, math.ceil(ticket.expires_at - now)))
            return (ticket.request_id, ticket.state, str(ttl), encode_payload(payload))


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
                        "label": {"type": "string", "minLength": 1, "maxLength": 12, "pattern": "^[ -~]+$"},
                        "detail": text63,
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
            "description": "Read only the ticket owned by this capability. After receiving answer_id, call again with acknowledge_answer_id to confirm delivery and enable Reply sent. A timeout or expired/cancelled state is not a decision.",
            "inputSchema": {"type": "object", "properties": {**ownership,
                "wait_ms": {"type": "integer", "minimum": 0, "maximum": 15000, "default": 0},
                "acknowledge_answer_id": {"type": "string", "pattern": "^[0-9a-f]{32}$"},
            }, "required": ["request_id", "capability"], "additionalProperties": False},
            "annotations": annotations,
        },
        {
            "name": "ccp_cancel_request", "title": "Cancel an owned Cody request",
            "description": "Explicitly retire this caller's pending request without selecting an option. Recipient-confirmed answers cannot be cancelled.",
            "inputSchema": {"type": "object", "properties": ownership,
                            "required": ["request_id", "capability"], "additionalProperties": False},
            "annotations": annotations,
        },
    ]
