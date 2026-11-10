"""State-v1 UDP framing and bounded streaming on the existing CCP socket."""

from __future__ import annotations

from collections import deque
from collections.abc import Callable, Iterator
from dataclasses import dataclass, field
from enum import IntEnum
import hashlib
import hmac
import json
import re
import secrets
import struct
import time
from typing import Any, Protocol

from ccp_state_registry import SOURCE_LIMITS


SCHEMA = "cody.state/1"
STATE_MAGIC = b"CDS1"
BOOTSTRAP_MAGIC = b"CCP2|"
MAX_PACKET = 1200
MAX_CONTROL = 4096
CHUNK = 1024
HEADER = struct.Struct("!4sBBH6s16s16sQ16sQQQIIIH32sI")
ACK = struct.Struct("!HBBQ")
TAG = 32
MAX_SEQUENCE = (1 << 64) - 1
ZERO_HASH = bytes(32)
ZERO_TRANSFER = bytes(16)
PROOF_DOMAIN = b"cody.state.datagram/1\0"
DEVICE_PORT = 42117
MAX_CHALLENGES = 128
MAX_PEERS = 16
CHALLENGE_SECONDS = 2.0
LEASE_SECONDS = 8.0
PROBE_SECONDS = 2.0
RETRY_SECONDS = 0.5
MAX_RETRIES = 5
IDLE_SECONDS = 5.0
TRANSFER_SECONDS = 120.0
DATA_INTERVAL = 1 / 12
# The one-MiB cap fits the absolute deadline at the admitted send cadence.
OFFER_TRANSPORT = {"document_bytes": 1024 * 1024, "window_chunks": 16}
HEX64 = re.compile(r"[0-9a-f]{16}")
FIND = re.compile(
    rb"CCP2\|FIND\|device=([0-9a-f]{12})\|pc=([0-9a-f]{32})\|nonce=([0-9a-f]{32})"
    rb"\|state=1\|proof=([0-9a-f]{64})"
)
COMMITTED = re.compile(
    rb"CCP2\|COMMITTED\|device=([0-9a-f]{12})\|pc=([0-9a-f]{32})\|nonce=([0-9a-f]{32})"
    rb"\|challenge=([0-9a-f]{32})\|address=([0-9.]{7,15})\|port=(42117)"
    rb"\|state=1\|proof=([0-9a-f]{64})"
)
RESYNC_REASONS = frozenset({
    "initial", "gap", "epoch_changed", "reboot", "capacity", "journal_overflow",
    "integrity", "clock_reset", "reconciliation",
})
Endpoint = tuple[str, int]


class Registry(Protocol):
    # pc_id: Stable retained broker identity.
    def binding(self, pc_id: str) -> dict[str, Any]: ...

    # pc_id: Stable retained broker identity.
    # request: Authenticated and admitted request.
    # limits: Exact capacities selected for this peer.
    def respond(self, pc_id: str, request: dict[str, Any], limits: dict[str, int]) -> dict[str, Any]: ...


class PacketKind(IntEnum):
    DATA = 1
    ACK = 2
    PING = 3
    PONG = 4


class AckState(IntEnum):
    RECEIVING = 1
    COMMITTED = 2
    REJECTED = 3


class RejectCode(IntEnum):
    MALFORMED = 1
    UNSUPPORTED_VERSION = 2
    UNSUPPORTED_CAPABILITY = 3
    WRONG_BINDING = 4
    REVISION_CONFLICT = 5
    NEED_SNAPSHOT = 6
    MISSING_REFERENCE = 7
    CAPACITY_EXCEEDED = 8
    TRANSFER_TIMEOUT = 9
    INTEGRITY = 10
    BUSY = 11
    PERSISTENCE = 12
    INVALID_TRANSITION = 13


class Rejected(ValueError):
    # code: Authenticated error safe to send to the exact bound peer.
    # detail: Static explanation without input values.
    def __init__(self, code: RejectCode, detail: str) -> None:
        super().__init__(detail)
        self.code = code


# length: Nonzero count of cryptographic random bytes.
def _nonzero_random(length: int) -> bytes:
    value = secrets.token_bytes(length)
    while not any(value):
        value = secrets.token_bytes(length)
    return value


# key: Active retained link key.
# domain: Exact bootstrap authentication domain.
# values: Ordered ASCII fields, excluding field names and proof.
def _bootstrap_proof(key: bytes, domain: str, *values: str) -> str:
    return hmac.digest(key, "|".join((domain, *values)).encode("ascii"), "sha256").hex()


# key: Active retained association key.
# role: Direction marker for a device (0) or broker (1).
# body: Exact header and payload bytes.
def _tag(key: bytes, role: int, body: bytes) -> bytes:
    return hmac.digest(key, PROOF_DOMAIN + bytes((role,)) + body, "sha256")


# value: Fixed-width lowercase identifier or revision.
# allow_zero: True only for a no-baseline revision.
def _hex64(value: Any, allow_zero: bool = False) -> bool:
    return isinstance(value, str) and HEX64.fullmatch(value) is not None and (
        allow_zero or value != "0000000000000000"
    )


# pairs: Original JSON properties, preserving duplicates for rejection.
# Raises Rejected for an ambiguous object.
def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise Rejected(RejectCode.MALFORMED, "Duplicate state control property")
        result[key] = value
    return result


# value: JSON token representing a non-finite number.
# Raises Rejected for every input.
def _reject_constant(value: str) -> None:
    raise Rejected(RejectCode.MALFORMED, "Non-finite state control number")


# value: Parsed control value from at most a 4 KiB source document.
# depth: Number of parent object/array containers.
# Raises Rejected for non-printable strings or excessive nesting.
def _check_values(value: Any, depth: int = 0) -> None:
    if isinstance(value, str) and any(not " " <= char <= "~" for char in value):
        raise Rejected(RejectCode.MALFORMED, "Non-ASCII state control text")
    if isinstance(value, (dict, list)):
        if depth >= 8:
            raise Rejected(RejectCode.MALFORMED, "State control depth exceeded")
        if isinstance(value, dict):
            for key in value:
                _check_values(key, depth + 1)
        for child in value.values() if isinstance(value, dict) else value:
            _check_values(child, depth + 1)


# payload: Exact bounded, authenticated and hash-verified control bytes.
# Raises Rejected for invalid JSON, encodings, or schema.
def _decode_control(payload: bytearray) -> dict[str, Any]:
    try:
        document = json.loads(payload.decode("utf-8"), object_pairs_hook=_unique_object,
                              parse_constant=_reject_constant)
    except Rejected:
        raise
    except (ValueError, RecursionError) as error:
        raise Rejected(RejectCode.MALFORMED, "Invalid state control JSON") from error
    if not isinstance(document, dict):
        raise Rejected(RejectCode.MALFORMED, "State control must be an object")
    _check_values(document)
    if document.get("schema") != SCHEMA:
        raise Rejected(RejectCode.UNSUPPORTED_VERSION, "Unsupported state control schema")
    if not _hex64(document.get("request_id")):
        raise Rejected(RejectCode.MALFORMED, "Invalid state request identity")
    return document


@dataclass(frozen=True)
class Transfer:
    identity: bytes
    source: int
    epoch: int
    revision: int
    count: int
    total: int
    digest: bytes


@dataclass(frozen=True)
class Packet:
    kind: PacketKind
    sequence: int
    transfer: Transfer
    index: int
    payload: bytes


@dataclass
class ReplayWindow:
    largest: int = 0
    bitmap: int = 0

    # sequence: Authenticated nonzero sequence in this peer's receive direction.
    # Returns "new", "duplicate", or "old"; only "new" changes the window.
    def admit(self, sequence: int) -> str:
        if sequence > self.largest:
            gap = sequence - self.largest
            self.bitmap = ((self.bitmap << gap) | 1) & MAX_SEQUENCE if gap < 64 else 1
            self.largest = sequence
            return "new"
        gap = self.largest - sequence
        if gap >= 64:
            return "old"
        bit = 1 << gap
        if self.bitmap & bit:
            return "duplicate"
        self.bitmap |= bit
        return "new"


# document: Detached, immutable-for-transfer producer envelope.
def _encode_pieces(document: dict[str, Any]) -> Iterator[bytes]:
    encoder = json.JSONEncoder(ensure_ascii=True, allow_nan=False, separators=(",", ":"))
    for piece in encoder.iterencode(document):
        yield piece.encode("ascii")


# document: Same detached envelope used by the hash pass.
def _encode_chunks(document: dict[str, Any]) -> Iterator[bytes]:
    chunk = bytearray()
    for piece in _encode_pieces(document):
        offset = 0
        while offset < len(piece):
            take = min(CHUNK - len(chunk), len(piece) - offset)
            chunk.extend(piece[offset:offset + take])
            offset += take
            if len(chunk) == CHUNK:
                yield bytes(chunk)
                chunk.clear()
    if chunk:
        yield bytes(chunk)


@dataclass
class SentChunk:
    payload: bytes
    sent_at: float | None = None
    retries: int = 0


class PeerRejection(RuntimeError):
    # code: Authenticated receiver's original reject code.
    def __init__(self, code: RejectCode) -> None:
        super().__init__(f"State transfer rejected by device ({code.name})")


@dataclass
class Outgoing:
    transfer: Transfer
    kind: str
    chunks: Iterator[bytes]
    width: int
    started_at: float
    progress_at: float
    floor: int = 0
    next_encode: int = 0
    sent_high: int = 0
    final_chunk: SentChunk | None = None
    window: dict[int, SentChunk] = field(default_factory=dict)

    # document: Frozen response; metadata and ages stay fixed throughout retries.
    # width: Selective-repeat window width.
    # limit: Negotiated encoded byte cap.
    # now: Time at original request admission.
    # Raises Rejected if the document exceeds its negotiated byte cap.
    @classmethod
    def freeze(cls, document: dict[str, Any], width: int, limit: int, now: float) -> Outgoing:
        kind = document["kind"]
        total = 0
        digest = hashlib.sha256()
        for piece in _encode_pieces(document):
            total += len(piece)
            if total > limit:
                raise Rejected(RejectCode.CAPACITY_EXCEEDED, "State response exceeds its byte budget")
            digest.update(piece)
        binding = (0, 0, 0)
        if kind in {"snapshot", "delta"}:
            binding = tuple(int(document[name], 16) for name in ("source_id", "epoch", "revision"))
        transfer = Transfer(_nonzero_random(16), *binding, (total + CHUNK - 1) // CHUNK, total, digest.digest())
        return cls(transfer, kind, _encode_chunks(document), width, now, now)

    # packet: ACK authenticated and bound to this transfer.
    # now: Current broker monotonic time.
    # revision: Previously committed receiver revision.
    # Raises Rejected for malformed ACKs and PeerRejection for authenticated device rejection.
    def acknowledge(self, packet: Packet, now: float, revision: int) -> bool:
        bitmap, state, error, committed = ACK.unpack(packet.payload)
        index = packet.index
        remaining = self.transfer.count - index
        if (index > self.sent_high or bitmap & 1 or bitmap >> min(self.width, remaining)
                or (bitmap and bitmap.bit_length() + index > self.sent_high)):
            raise Rejected(RejectCode.MALFORMED, "Invalid state ACK window")
        expected = revision if self.transfer.source else 0
        if state == AckState.REJECTED:
            if not RejectCode.MALFORMED <= error <= RejectCode.INVALID_TRANSITION or committed != expected:
                raise Rejected(RejectCode.MALFORMED, "Invalid state rejection ACK")
            raise PeerRejection(RejectCode(error))
        if state == AckState.COMMITTED:
            if error or index != self.transfer.count or bitmap or committed != self.transfer.revision:
                raise Rejected(RejectCode.MALFORMED, "Invalid state commit ACK")
            return True
        if state != AckState.RECEIVING or error or committed != expected:
            raise Rejected(RejectCode.MALFORMED, "Invalid state receiving ACK")
        if index > self.floor:
            self.floor = index
            self.progress_at = now
        for pending in tuple(self.window):
            relative = pending - index
            if relative < 0 or relative < self.width and bitmap & (1 << relative):
                del self.window[pending]
        return False

    # now: Current broker monotonic time.
    # Raises Rejected for stream inconsistency, exhausted retries, or expired transfers.
    def next_packet(self, now: float) -> tuple[int, bytes] | None:
        if now - self.started_at >= TRANSFER_SECONDS or now - self.progress_at >= IDLE_SECONDS:
            raise Rejected(RejectCode.TRANSFER_TIMEOUT, "State send deadline expired")
        if self.floor == self.transfer.count:
            # Receiving the final bytes does not commit the semantic generation.
            # Retain the last chunk until a retry obtains the cached final ACK.
            if self.final_chunk is None:
                raise Rejected(RejectCode.INTEGRITY, "Missing final state chunk")
            return self._admit_chunk(self.transfer.count - 1, self.final_chunk, now)
        edge = min(self.floor + self.width, self.transfer.count)
        while self.next_encode < edge:
            payload = next(self.chunks, None)
            expected = min(CHUNK, self.transfer.total - self.next_encode * CHUNK)
            if payload is None or len(payload) != expected:
                raise Rejected(RejectCode.INTEGRITY, "Frozen state encoding changed")
            self.window[self.next_encode] = SentChunk(payload)
            self.next_encode += 1
        for index, chunk in self.window.items():
            packet = self._admit_chunk(index, chunk, now)
            if packet is not None:
                return packet
        return None

    # index: Slot index within the current selective-repeat window.
    # chunk: Frozen payload plus mutable send timing and retry allowance.
    # now: Current broker monotonic time.
    # Raises Rejected when the per-chunk retry allowance is exhausted.
    def _admit_chunk(self, index: int, chunk: SentChunk, now: float) -> tuple[int, bytes] | None:
        if chunk.sent_at is not None:
            if now - chunk.sent_at < RETRY_SECONDS:
                return None
            if chunk.retries >= MAX_RETRIES:
                raise Rejected(RejectCode.TRANSFER_TIMEOUT, "State chunk retry budget expired")
            chunk.retries += 1
        chunk.sent_at = now
        self.sent_high = max(self.sent_high, index + 1)
        if index + 1 == self.transfer.count:
            self.final_chunk = chunk
        return index, chunk.payload


@dataclass
class Incoming:
    transfer: Transfer
    started_at: float
    progress_at: float
    next_missing: int = 0
    data: bytearray = field(default_factory=bytearray)
    status: AckState = AckState.RECEIVING
    error: int = 0


@dataclass(frozen=True)
class Challenge:
    nonce: str
    identity: bytes
    endpoint: Endpoint
    key: bytes = field(repr=False)
    key_stamp: str = field(repr=False)
    deadline: float
    used: bool = False


@dataclass
class Peer:
    device: bytes
    session: bytes
    endpoint: Endpoint
    key: bytes = field(repr=False)
    key_stamp: str = field(repr=False)
    binding: dict[str, Any]
    lease_until: float
    next_probe_at: float
    offer_id: str
    offer_digest: str = ""
    selection: dict[str, Any] | None = None
    accepted: bool = False
    send_sequence: int = 0
    receive_sequences: ReplayWindow = field(default_factory=ReplayWindow)
    incoming: Incoming | None = None
    outgoing: Outgoing | None = None
    probe_nonce: bytes = ZERO_TRANSFER
    probe_until: float = 0.0
    next_data_at: float = 0.0
    next_request_at: float = 0.0
    committed_revision: int = 0
    request_ids: deque[str] = field(default_factory=lambda: deque(maxlen=64))


class StateResponder:
    # pc_id: Stable retained local broker identity.
    # active_key: Fetches the active key and opaque generation stamp.
    # current_key: Compares the stamp with the active retained association.
    # send: UDP send operation on the already-owned port.
    # registry: Single synchronized broker registry owner.
    # mark_seen: Existing retained peer observation publisher.
    def __init__(self, pc_id: str, active_key: Callable[[str], tuple[bytes, str] | None],
                 current_key: Callable[[str, str], bool], send: Callable[[bytes, Endpoint, str, str], int],
                 registry: Registry, mark_seen: Callable[[str, str, str], None]) -> None:
        self._pc_id = pc_id
        self._pc = bytes.fromhex(pc_id)
        self._active_key = active_key
        self._current_key = current_key
        self._send = send
        self._registry = registry
        self._mark_seen = mark_seen
        self._challenges: dict[str, Challenge] = {}
        self._peers: dict[bytes, Peer] = {}

    # payload: Ingress-admitted CCP2 bootstrap or state datagram.
    # endpoint: Exact socket-observed address and port.
    # Raises OSError or RuntimeError for local delivery and persistence failures.
    def handle(self, payload: bytes, endpoint: Endpoint) -> None:
        now = time.monotonic()
        if endpoint[1] != DEVICE_PORT:
            return
        if payload.startswith(BOOTSTRAP_MAGIC):
            self._bootstrap(payload, endpoint, now)
            return
        authenticated = self._authenticate(payload, endpoint, now)
        if authenticated is not None:
            packet, peer = authenticated
            self._handle_packet(packet, peer, now)

    # now: Current broker monotonic time.
    # Raises the original send/transfer error after giving all peers a turn.
    def tick(self, now: float) -> None:
        first_failure = None
        for device, peer in tuple(self._peers.items()):
            if now >= peer.lease_until or not self._current_key(device.hex(), peer.key_stamp):
                del self._peers[device]
                continue
            try:
                self._drive(peer, now)
            except (OSError, RuntimeError, ValueError) as error:
                if first_failure is None:
                    first_failure = error
        if first_failure is not None:
            raise first_failure

    # payload: Bounded exact-order ASCII bootstrap.
    # endpoint: Address/port observed on the broker socket.
    # now: Current broker monotonic time.
    # Raises OSError for send failures and Rejected if the peer registry is full.
    def _bootstrap(self, payload: bytes, endpoint: Endpoint, now: float) -> None:
        if len(payload) > 512:
            return
        find = FIND.fullmatch(payload)
        if find is not None:
            device_id, pc, nonce, proof = (part.decode("ascii") for part in find.groups())
            if pc == self._pc_id and any(bytes.fromhex(nonce)):
                self._challenge(device_id, nonce, proof, endpoint, now)
            return
        committed = COMMITTED.fullmatch(payload)
        if committed is None:
            return
        device_id, pc, nonce, token, address, port, proof = (part.decode("ascii") for part in committed.groups())
        if pc != self._pc_id or (address, int(port)) != endpoint:
            return
        challenge = self._challenges.get(device_id)
        if (challenge is None or challenge.used or now >= challenge.deadline or endpoint != challenge.endpoint
                or nonce != challenge.nonce or token != challenge.identity.hex()
                or not self._current_key(device_id, challenge.key_stamp)):
            return
        expected = _bootstrap_proof(challenge.key, "committed-state-v1", device_id, pc, nonce, token, address, port, "1")
        if not hmac.compare_digest(proof, expected):
            return
        # Consume before constructing a generation or sending source data.
        self._challenges[device_id] = Challenge(nonce, challenge.identity, endpoint,
                                                challenge.key, challenge.key_stamp, challenge.deadline, True)
        self._open_peer(device_id, challenge, now)

    # device_id: Retained device identifier asserted by FIND.
    # nonce: Device-generated negotiation nonce.
    # proof: Device proof over the exact FIND identity and capability.
    # endpoint: Address/port observed on the broker socket.
    # now: Original challenge admission time.
    # Raises OSError for UDP send failure.
    def _challenge(self, device_id: str, nonce: str, proof: str, endpoint: Endpoint, now: float) -> None:
        active = self._active_key(device_id)
        if active is None:
            return
        key, stamp = active
        expected = _bootstrap_proof(key, "find-state-v1", device_id, self._pc_id, nonce, "1")
        if not hmac.compare_digest(proof, expected):
            return
        challenge = self._challenges.get(device_id)
        if challenge is not None and challenge.nonce == nonce:
            if challenge.used or now >= challenge.deadline or challenge.endpoint != endpoint or challenge.key_stamp != stamp:
                return
        else:
            if len(self._challenges) >= MAX_CHALLENGES and device_id not in self._challenges:
                retired = next((identity for identity, candidate in self._challenges.items()
                                if now >= candidate.deadline), None)
                if retired is None:
                    return
                del self._challenges[retired]
            challenge = Challenge(nonce, _nonzero_random(16), endpoint, key, stamp, now + CHALLENGE_SECONDS)
            self._challenges[device_id] = challenge
        values = (device_id, self._pc_id, nonce, challenge.identity.hex(), endpoint[0], str(endpoint[1]), "1")
        signed = _bootstrap_proof(key, "ready-state-v1", *values)
        names = ("device", "pc", "nonce", "challenge", "address", "port", "state")
        packet = "CCP2|READY|" + "|".join(f"{name}={value}" for name, value in zip(names, values)) + f"|proof={signed}"
        self._send(packet.encode("ascii"), endpoint, device_id, stamp)

    # device_id: Exact retained device identity from the consumed challenge.
    # challenge: Already consumed endpoint proof and active association stamp.
    # now: Start time for the initial link lease.
    # Raises Rejected when the peer budget is full and OSError if sending fails.
    def _open_peer(self, device_id: str, challenge: Challenge, now: float) -> None:
        device = bytes.fromhex(device_id)
        if len(self._peers) >= MAX_PEERS and device not in self._peers:
            raise Rejected(RejectCode.CAPACITY_EXCEEDED, "State peer registry is full")
        binding = self._registry.binding(self._pc_id)
        peer = Peer(device, challenge.identity, challenge.endpoint, challenge.key, challenge.key_stamp, binding,
                    now + LEASE_SECONDS, now + PROBE_SECONDS, _nonzero_random(8).hex())
        offer = {"schema": SCHEMA, "kind": "offer", "request_id": peer.offer_id,
                 "bindings": [binding], "limits": SOURCE_LIMITS, "transport": OFFER_TRANSPORT}
        peer.outgoing = Outgoing.freeze(offer, 1, MAX_CONTROL, now)
        peer.offer_digest = peer.outgoing.transfer.digest.hex()
        self._peers[device] = peer
        self._mark_seen(device_id, challenge.endpoint[0], challenge.key_stamp)
        self._drive(peer, now)

    # payload: Exact candidate UDP datagram already admitted by source budget.
    # endpoint: Observed address and port.
    # now: Broker clock used to check the current session lease.
    # Returns None for unauthenticated or malformed framing without reflecting it.
    def _authenticate(self, payload: bytes, endpoint: Endpoint, now: float) -> tuple[Packet, Peer] | None:
        if not HEADER.size + TAG <= len(payload) <= MAX_PACKET:
            return None
        (magic, version, kind, flags, device, pc, session, sequence, transfer_id,
         source, epoch, revision, index, count, total, length, digest, reserved) = HEADER.unpack_from(payload)
        peer = self._peers.get(device)
        if (magic != STATE_MAGIC or version != 1 or flags or reserved or not sequence or transfer_id == ZERO_TRANSFER
                or pc != self._pc or peer is None or endpoint != peer.endpoint or session != peer.session
                or now >= peer.lease_until or not self._current_key(device.hex(), peer.key_stamp)
                or len(payload) != HEADER.size + length + TAG or not PacketKind.DATA <= kind <= PacketKind.PONG):
            return None
        if not hmac.compare_digest(_tag(peer.key, 0, payload[:-TAG]), payload[-TAG:]):
            return None
        if kind in {PacketKind.PING, PacketKind.PONG}:
            if any((source, epoch, revision, index, count, total, length)) or digest != ZERO_HASH:
                return None
        else:
            state = all((source, epoch, revision))
            if (not state and any((source, epoch, revision))
                    or not 1 <= total <= (OFFER_TRANSPORT["document_bytes"] if state else MAX_CONTROL)
                    or count != (total + CHUNK - 1) // CHUNK):
                return None
            if kind == PacketKind.DATA and (state or index >= count or length != min(CHUNK, total - index * CHUNK)):
                return None
            if kind == PacketKind.ACK and (index > count or length != ACK.size):
                return None
        transfer = Transfer(transfer_id, source, epoch, revision, count, total, digest)
        return Packet(PacketKind(kind), sequence, transfer, index, payload[HEADER.size:-TAG]), peer
# packet: Authenticated packet for this link.
    # peer: Session with exact device, endpoint and retained-key generation.
    # now: Current monotonic receive time.
    # Raises OSError or Rejected for send failures or authenticated violations.
    def _handle_packet(self, packet: Packet, peer: Peer, now: float) -> None:
        replay = peer.receive_sequences.admit(packet.sequence)
        if replay != "new":
            incoming = peer.incoming
            if (replay == "duplicate" and packet.kind == PacketKind.DATA
                    and incoming is not None and incoming.transfer == packet.transfer):
                self._ack(peer, incoming)
            return
        if packet.kind == PacketKind.PING:
            self._emit(peer, PacketKind.PONG, packet.transfer, 0, b"")
        elif packet.kind == PacketKind.PONG:
            if peer.probe_nonce == packet.transfer.identity and now < peer.probe_until:
                peer.probe_nonce = ZERO_TRANSFER
                peer.lease_until = now + LEASE_SECONDS
                self._mark_seen(peer.device.hex(), peer.endpoint[0], peer.key_stamp)
        elif packet.kind == PacketKind.DATA:
            self._receive(peer, packet, now)
        elif peer.outgoing is not None and packet.transfer == peer.outgoing.transfer:
            try:
                committed = peer.outgoing.acknowledge(packet, now, peer.committed_revision)
            except PeerRejection:
                peer.outgoing = None
                raise
            if committed:
                if peer.outgoing.kind == "accepted":
                    peer.accepted = True
                if peer.outgoing.transfer.source:
                    peer.committed_revision = peer.outgoing.transfer.revision
                peer.outgoing = None

    # peer: Exact authenticated session owning the one inbound control transfer.
    # packet: Authenticated DATA with validated control chunk geometry.
    # now: Current monotonic time.
    # Raises OSError on ACK send failures.
    def _receive(self, peer: Peer, packet: Packet, now: float) -> None:
        transfer = packet.transfer
        incoming = peer.incoming
        if incoming is not None and incoming.transfer.identity == transfer.identity:
            if incoming.transfer != transfer and incoming.status == AckState.RECEIVING:
                self._reject_incoming(peer, incoming, RejectCode.INTEGRITY)
                return
            if incoming.status != AckState.RECEIVING:
                self._ack(peer, incoming)
                return
        else:
            if incoming is not None and incoming.status == AckState.RECEIVING:
                self._reject_incoming(peer, Incoming(transfer, now, now), RejectCode.BUSY)
                return
            if packet.index != 0:
                self._ack(peer, Incoming(transfer, now, now))
                return
            incoming = Incoming(transfer, now, now)
            peer.incoming = incoming
        if now - incoming.started_at >= TRANSFER_SECONDS or now - incoming.progress_at >= IDLE_SECONDS:
            self._reject_incoming(peer, incoming, RejectCode.TRANSFER_TIMEOUT)
            return
        if packet.index != incoming.next_missing:
            self._ack(peer, incoming)
            return
        try:
            incoming.data.extend(packet.payload)
        except MemoryError:
            self._reject_incoming(peer, incoming, RejectCode.CAPACITY_EXCEEDED)
            raise Rejected(RejectCode.CAPACITY_EXCEEDED, "State control staging allocation failed")
        incoming.next_missing += 1
        incoming.progress_at = now
        if incoming.next_missing < transfer.count:
            self._ack(peer, incoming)
            return
        if hashlib.sha256(incoming.data).digest() != transfer.digest:
            self._reject_incoming(peer, incoming, RejectCode.INTEGRITY)
            return
        try:
            self._control(peer, _decode_control(incoming.data), transfer.digest.hex(), incoming.started_at, now)
        except Rejected as error:
            self._reject_incoming(peer, incoming, error.code)
            raise
        except MemoryError:
            self._reject_incoming(peer, incoming, RejectCode.CAPACITY_EXCEEDED)
            raise Rejected(RejectCode.CAPACITY_EXCEEDED, "State response staging allocation failed")
        incoming.data.clear()
        incoming.status = AckState.COMMITTED
        self._ack(peer, incoming)

    # peer: Authenticated session with its exact offer and source binding.
    # request: Hash-verified control document with validated schema/request ID.
    # digest: SHA-256 of the exact control bytes.
    # started: Original transfer admission time, retained across retries.
    # now: Monotonic time for request pacing.
    # Raises Rejected for unsupported commands, invalid bindings or unavailable capacity.
    def _control(self, peer: Peer, request: dict[str, Any], digest: str, started: float, now: float) -> None:
        kind = request.get("kind")
        if kind == "select":
            self._select(peer, request, digest, started)
            return
        if kind not in ("resync", "next"):
            raise Rejected(RejectCode.MALFORMED, "Unsupported state control kind")
        if not peer.accepted or peer.selection is None:
            raise Rejected(RejectCode.INVALID_TRANSITION, "State negotiation has not committed")
        self._validate_request(peer, request, kind)
        request_id = request["request_id"]
        if request_id == peer.offer_id or request_id in peer.request_ids:
            raise Rejected(RejectCode.INVALID_TRANSITION, "State request was already handled")
        if kind == "next" and now < peer.next_request_at:
            raise Rejected(RejectCode.BUSY, "State next request rate exceeded")
        previous = peer.outgoing
        if previous is not None:
            if (kind != "next" or not previous.transfer.source or previous.sent_high != previous.transfer.count
                    or int(request["base_revision"], 16) != previous.transfer.revision):
                raise Rejected(RejectCode.BUSY, "Another state transfer is active")
            # An exact next base proves the final commit when the ACK was lost.
            peer.committed_revision = previous.transfer.revision
            peer.outgoing = None
        if kind == "next" and int(request["base_revision"], 16) != peer.committed_revision:
            document = self._response_error(peer, request_id, "need_snapshot", "State baseline does not match")
        else:
            # For a resync, receiving/rejection ACKs report the exact baseline
            # supplied by the receiver, even if an earlier commit ACK was lost.
            if kind == "resync":
                peer.committed_revision = int(request["revision"], 16)
            document = self._registry.respond(self._pc_id, request, peer.selection["limits"])
        self._validate_response(peer, request, document)
        is_state = document["kind"] in {"snapshot", "delta"}
        transport = peer.selection["transport"]
        try:
            transfer = Outgoing.freeze(document, transport["window_chunks"] if is_state else 1,
                                       transport["document_bytes"] if is_state else MAX_CONTROL, started)
        except Rejected as error:
            if error.code != RejectCode.CAPACITY_EXCEEDED:
                raise
            document = self._response_error(peer, request_id, "capacity_exceeded", str(error), "document_bytes")
            transfer = Outgoing.freeze(document, 1, MAX_CONTROL, started)
        peer.outgoing = transfer
        if kind == "next":
            peer.next_request_at = now + RETRY_SECONDS
        peer.request_ids.append(request_id)

    # peer: Current negotiated source manifest.
    # request: Admitted request whose identity and baseline cannot change.
    # response: Detached registry result before freezing its first wire byte.
    # Raises Rejected if the producer returned a mismatched envelope.
    @staticmethod
    def _validate_response(peer: Peer, request: dict[str, Any], response: dict[str, Any]) -> None:
        if response.get("schema") != SCHEMA or response.get("request_id") != request["request_id"]:
            raise Rejected(RejectCode.WRONG_BINDING, "State response does not match the request")
        kind = response.get("kind")
        if kind == "error":
            return
        if (response.get("source_id") != peer.binding["source_id"]
                or response.get("epoch") != peer.binding["epoch"]
                or not _hex64(response.get("revision"))):
            raise Rejected(RejectCode.WRONG_BINDING, "State response does not match the source epoch")
        if request["kind"] == "resync":
            valid = (kind == "snapshot" and response.get("base_revision") == "0000000000000000"
                     and response["revision"] >= request["revision"])
        elif kind == "delta":
            valid = (response.get("base_revision") == request["base_revision"]
                     and response["revision"] > request["base_revision"])
        else:
            valid = kind == "no_change" and response["revision"] == request["base_revision"]
        if not valid:
            raise Rejected(RejectCode.REVISION_CONFLICT, "State response continuity is invalid")

    # peer: Session with one immutable offered binding.
    # request: Hash-verified selection document.
    # digest: Exact digest of that selection, echoed in accepted.
    # started: Original request admission time.
    # Raises Rejected for mismatched manifest or capacities.
    def _select(self, peer: Peer, request: dict[str, Any], digest: str, started: float) -> None:
        if (peer.selection is not None or request["request_id"] != peer.offer_id
                or set(request) != {"schema", "kind", "request_id", "offer_digest", "sources", "limits", "transport"}
                or request["offer_digest"] != peer.offer_digest
                or request["sources"] != [peer.binding["source_id"]]):
            raise Rejected(RejectCode.WRONG_BINDING, "Selection does not match the current offer")
        limits = request["limits"]
        transport = request["transport"]
        if (not self._admit_limits(limits, SOURCE_LIMITS) or not self._admit_limits(transport, OFFER_TRANSPORT)
                or limits["sources"] != 1 or limits["chats"] != 64
                or not all(limits[field] for field in ("turns", "evidence", "text_bytes", "decode_scratch_bytes"))
                or ((limits["open_questions"] or limits["resolved_questions"]) and not limits["choices"])
                or transport["document_bytes"] < CHUNK or not transport["window_chunks"]):
            raise Rejected(RejectCode.UNSUPPORTED_CAPABILITY, "Selection cannot admit the required capacities")
        document = {"schema": SCHEMA, "kind": "accepted", "request_id": peer.offer_id,
                    "offer_digest": peer.offer_digest, "selection_digest": digest,
                    "sources": request["sources"], "limits": limits, "transport": transport}
        # Valid select proves offer receipt even when the final ACK was lost.
        peer.outgoing = Outgoing.freeze(document, 1, MAX_CONTROL, started)
        peer.selection = document

    # request: Candidate exact object of nonnegative integer limits.
    # offered: Hard caps advertised to the receiver.
    @staticmethod
    def _admit_limits(request: Any, offered: dict[str, int]) -> bool:
        return isinstance(request, dict) and request.keys() == offered.keys() and all(
            type(value) is int and 0 <= value <= offered[name] for name, value in request.items()
        )

    # peer: Negotiated source binding.
    # request: Candidate resync or next envelope.
    # kind: Already admitted envelope kind.
    # Raises Rejected for malformed or foreign requests.
    @staticmethod
    def _validate_request(peer: Peer, request: dict[str, Any], kind: str) -> None:
        common = {"schema", "kind", "request_id", "source_id", "epoch"}
        if kind == "resync":
            reason = request.get("reason")
            if (set(request) != common | {"revision", "reason"} or not _hex64(request["revision"], True)
                    or not isinstance(reason, str) or reason not in RESYNC_REASONS):
                raise Rejected(RejectCode.MALFORMED, "Malformed state resync request")
        elif set(request) != common | {"base_revision"} or not _hex64(request["base_revision"]):
            raise Rejected(RejectCode.MALFORMED, "Malformed state next request")
        if request["source_id"] != peer.binding["source_id"] or request["epoch"] != peer.binding["epoch"]:
            raise Rejected(RejectCode.WRONG_BINDING, "Request is outside its negotiated source epoch")

    # peer: Session with its last authenticated committed revision.
    # request_id: Exact originating request identity.
    # code: Schema error name.
    # detail: Static explanation without private inputs.
    # pool: Optional exhausted limit name.
    @staticmethod
    def _response_error(peer: Peer, request_id: str, code: str, detail: str,
                        pool: str | None = None) -> dict[str, Any]:
        return {"schema": SCHEMA, "kind": "error", "request_id": request_id, "code": code,
                "detail": detail, "pool": pool, "revision": f"{peer.committed_revision:016x}"}
# peer: Authenticated owner of current transfers and probe state.
    # now: Current monotonic time.
    # Raises OSError for UDP send failures and Rejected for transfer expiry.
    def _drive(self, peer: Peer, now: float) -> None:
        if now >= peer.next_probe_at:
            peer.probe_nonce = _nonzero_random(16)
            peer.probe_until = now + PROBE_SECONDS
            peer.next_probe_at = peer.probe_until
            probe = Transfer(peer.probe_nonce, 0, 0, 0, 0, 0, ZERO_HASH)
            self._emit(peer, PacketKind.PING, probe, 0, b"")
        incoming = peer.incoming
        if (incoming is not None and incoming.status == AckState.RECEIVING
                and (now - incoming.started_at >= TRANSFER_SECONDS or now - incoming.progress_at >= IDLE_SECONDS)):
            self._reject_incoming(peer, incoming, RejectCode.TRANSFER_TIMEOUT)
        outgoing = peer.outgoing
        if outgoing is None or now < peer.next_data_at:
            return
        try:
            pending = outgoing.next_packet(now)
        except Rejected:
            peer.outgoing = None
            raise
        if pending is not None:
            peer.next_data_at = max(now - DATA_INTERVAL / 2, peer.next_data_at) + DATA_INTERVAL
            self._emit(peer, PacketKind.DATA, outgoing.transfer, *pending)

    # peer: Exact authenticated peer.
    # incoming: Transfer that failed to admit bytes or its complete request.
    # code: Stable protocol rejection code preserving the original cause.
    # Raises OSError for UDP send failures.
    def _reject_incoming(self, peer: Peer, incoming: Incoming, code: RejectCode) -> None:
        incoming.data.clear()
        incoming.status = AckState.REJECTED
        incoming.error = code
        self._ack(peer, incoming)

    # peer: Authenticated session that sent the control.
    # incoming: Current control receive window or its cached final outcome.
    # Raises OSError for UDP send failures.
    def _ack(self, peer: Peer, incoming: Incoming) -> None:
        payload = ACK.pack(0, incoming.status, incoming.error, 0)
        self._emit(peer, PacketKind.ACK, incoming.transfer, incoming.next_missing, payload)

    # peer: Active authenticated link with sole ownership of the send sequence.
    # kind: Packet kind.
    # transfer: Immutable transfer metadata or a zero-bound probe with nonce.
    # index: Chunk index or next missing index.
    # payload: Exact wire payload excluding header and authentication tag.
    # Raises OSError for UDP send failures and Rejected when sequence is exhausted.
    def _emit(self, peer: Peer, kind: PacketKind, transfer: Transfer, index: int, payload: bytes) -> None:
        if peer.send_sequence == MAX_SEQUENCE:
            del self._peers[peer.device]
            raise Rejected(RejectCode.INVALID_TRANSITION, "State sequence exhausted, negotiate a new session")
        if not self._current_key(peer.device.hex(), peer.key_stamp):
            del self._peers[peer.device]
            raise Rejected(RejectCode.WRONG_BINDING, "State active association changed")
        peer.send_sequence += 1
        header = HEADER.pack(STATE_MAGIC, 1, kind, 0, peer.device, self._pc, peer.session, peer.send_sequence,
                             transfer.identity, transfer.source, transfer.epoch, transfer.revision, index,
                             transfer.count, transfer.total, len(payload), transfer.digest, 0)
        body = header + payload
        self._send(body + _tag(peer.key, 1, body), peer.endpoint, peer.device.hex(), peer.key_stamp)
