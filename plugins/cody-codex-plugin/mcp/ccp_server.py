"""CCP: Cody's Codex Plugin MCP server and bounded local-network responder."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import hmac
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import socket
import sys
import threading
import time
from typing import Any

from ccp_activity import ActivityStore
import ccp_interactions as interactions
import ccp_reset as reset
from ccp_windows import configure_exclusive_socket, protect_secret, unprotect_secret, runtime_directory


PROTOCOL = "CCP1"
DEVICE_PORT = 42117
PLUGIN_PORT = 42118
MAX_DATAGRAM_BYTES = 512
PBKDF2_ITERATIONS = 50_000
SERVER_VERSION = "0.6.2"
LIVENESS_TIMEOUT_SECONDS = 10.0
CODE_PATTERN = re.compile(r"^[A-HJ-NP-Z2-9]{6}$")
HEX_PATTERNS = {
    "request": re.compile(r"^[0-9a-f]{16}$"),
    "device": re.compile(r"^[0-9a-f]{12}$"),
    "session": re.compile(r"^[0-9a-f]{8}$"),
    "salt": re.compile(r"^[0-9a-f]{32}$"),
    "tag": re.compile(r"^[0-9a-f]{64}$"),
    "pc": re.compile(r"^[0-9a-f]{32}$"),
    "nonce": re.compile(r"^[0-9a-f]{32}$"),
    "proof": re.compile(r"^[0-9a-f]{64}$"),
}


@dataclass(frozen=True)
class Offer:
    address: str
    request_id: str
    device_id: str
    session_id: str
    salt: str
    tag: str
    received_at: float


@dataclass
class PairingRecord:
    device_id: str
    protected_link_key: str
    paired_at: float
    last_seen_at: float = 0.0
    last_address: str = ""
    last_seen_monotonic: float | None = None


@dataclass(frozen=True)
class RetainedKeys:
    active: bytes | None
    pending: bytes | None


@dataclass(frozen=True)
class CommitChallenge:
    nonce: str
    key: bytes
    pending_key: bytes | None
    source: tuple[str, int]
    observed_at: float


@dataclass(frozen=True)
class EventPeer:
    nonce: str
    key: bytes
    source: tuple[str, int]
    observed_at: float


# address: Socket-observed IP address, or an empty value before fresh observation.
# Raises ValueError for an invalid observed address.
def observation_source(address: str) -> str:
    if not address:
        return "UNOBSERVED"
    return "SIMULATED" if ipaddress.ip_address(address).is_loopback else "LAN"


# values: Ordered lowercase ASCII fields covered by one protocol proof.
def canonical_text(*values: str) -> bytes:
    return "|".join(values).encode("ascii")


# key: Thirty-two byte verifier or link key.
# values: Ordered protocol fields included in the proof.
def make_proof(key: bytes, *values: str) -> str:
    return hmac.new(key, canonical_text(*values), hashlib.sha256).hexdigest()


# code: Normalized six-character code visible on the explicitly selected target display.
# salt_hex: Sixteen-byte session salt encoded as lowercase hexadecimal.
# Raises ValueError for an invalid code or salt.
def derive_verifier(code: str, salt_hex: str) -> bytes:
    normalized = code.strip().upper()
    if CODE_PATTERN.fullmatch(normalized) is None:
        raise ValueError("The Cody code must contain six characters from A-H, J-N, P-Z, or 2-9")
    if HEX_PATTERNS["salt"].fullmatch(salt_hex) is None:
        raise ValueError("The Cody offer contains an invalid salt")
    return hashlib.pbkdf2_hmac(
        "sha256", normalized.encode("ascii"), bytes.fromhex(salt_hex), PBKDF2_ITERATIONS, 32
    )


# message: Printable ASCII CCP1 datagram; only EVENT uses the larger extension envelope.
# Raises ValueError for malformed, ambiguous, oversized, or duplicate fields.
def parse_datagram(message: bytes) -> tuple[str, dict[str, str]]:
    limit = interactions.MAX_EVENT_DATAGRAM_BYTES if message.startswith(b"CCP1|EVENT|") else MAX_DATAGRAM_BYTES
    if not message or len(message) > limit:
        raise ValueError("CCP datagram size rejected")
    try:
        text = message.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("CCP datagram is not printable ASCII") from error
    parts = text.split("|")
    if len(parts) < 2 or parts[0] != PROTOCOL or not parts[1].isupper():
        raise ValueError("CCP datagram header rejected")
    fields: dict[str, str] = {}
    for field in parts[2:]:
        if "=" not in field:
            raise ValueError("CCP datagram field is malformed")
        name, value = field.split("=", 1)
        if not name or not value or name in fields:
            raise ValueError("CCP datagram field is missing or duplicated")
        fields[name] = value
    return parts[1], fields


# kind: Uppercase CCP1 message identity; only EVENT may exceed the original 512-byte envelope.
# fields: Ordered field names and already validated printable ASCII values.
# Raises ValueError when the encoded datagram exceeds the protocol bound.
def encode_datagram(kind: str, fields: list[tuple[str, str]]) -> bytes:
    text = "|".join([PROTOCOL, kind, *[f"{name}={value}" for name, value in fields]])
    payload = text.encode("ascii")
    limit = interactions.MAX_EVENT_DATAGRAM_BYTES if kind == "EVENT" else MAX_DATAGRAM_BYTES
    if len(payload) > limit:
        raise ValueError("CCP datagram exceeds its fixed bound")
    return payload


# fields: Parsed offer fields.
# address: Source IPv4 address observed by the receiving socket.
# request_id: Exact request identity generated for this discovery pass.
# Raises ValueError when a required field is missing, unexpected, or malformed.
def offer_from_fields(fields: dict[str, str], address: str, request_id: str) -> Offer:
    expected = {"request", "device", "session", "salt", "tag"}
    if set(fields) != expected or fields.get("request") != request_id:
        raise ValueError("CCP offer fields do not match the active request")
    for name in expected:
        if HEX_PATTERNS[name].fullmatch(fields[name]) is None:
            raise ValueError(f"CCP offer field {name!r} is malformed")
    return Offer(
        address=address,
        request_id=fields["request"],
        device_id=fields["device"],
        session_id=fields["session"],
        salt=fields["salt"],
        tag=fields["tag"],
        received_at=time.time(),
    )


# pairs: Persisted JSON object members before dictionary conversion can hide duplicates.
# Raises ValueError when a stored object contains an ambiguous repeated field.
def unique_state_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, value in pairs:
        if name in result:
            raise ValueError(f"CCP state contains duplicate field {name!r}")
        result[name] = value
    return result


class StateStore:
    # self: Loads or creates the current Windows user's CCP identity and separate candidates.
    # Raises RuntimeError for corrupt, partial, inaccessible, or unprotectable state.
    def __init__(self) -> None:
        self._path = runtime_directory() / "state.json"
        self._pending_path = self._path.with_name("pending-pairings.json")
        self._lock = threading.Lock()
        self.pc_id = ""
        self.records: dict[str, PairingRecord] = {}
        self.pending_records: dict[str, PairingRecord] = {}
        self._load()
        self._load_pending()

    # self: Reads one exact versioned document or creates a genuinely absent identity.
    # Raises RuntimeError for invalid types, versions, identities, records, or filesystem failures.
    def _load(self) -> None:
        try:
            saved_text = self._path.read_text(encoding="utf-8")
        except FileNotFoundError:
            try:
                self._pending_path.stat()
            except FileNotFoundError:
                pass
            except OSError as error:
                raise RuntimeError(f"CCP pending-state presence check failed: {error}") from error
            else:
                raise RuntimeError("CCP active identity is missing while candidate state exists; explicit repair is required")
            # Only a missing file grants first-use identity creation. A read
            # error must leave an existing identity and its keys untouched.
            print("CCP state absent: first use; persist a new PC identity", file=sys.stderr, flush=True)
            self.pc_id = secrets.token_hex(16)
            self._save_locked()
            return
        except (OSError, UnicodeError) as error:
            raise RuntimeError(f"CCP state read failed: {error}") from error
        try:
            document = json.loads(saved_text, object_pairs_hook=unique_state_object)
        except ValueError as error:
            raise RuntimeError(f"CCP state read failed: {error}") from error
        if not isinstance(document, dict) or set(document) != {"version", "pc_id", "records"}:
            raise RuntimeError("CCP state fields are missing or unexpected")
        if type(document["version"]) is not int or document["version"] != 1:
            raise RuntimeError("CCP state version is invalid or unsupported")
        pc_id = document.get("pc_id")
        records = document.get("records")
        if not isinstance(pc_id, str) or HEX_PATTERNS["pc"].fullmatch(pc_id) is None:
            raise RuntimeError("CCP state contains an invalid PC identity")
        if not isinstance(records, list):
            raise RuntimeError("CCP state records are not a list")
        loaded: dict[str, PairingRecord] = {}
        for item in records:
            expected_fields = {"device_id", "protected_link_key", "paired_at"}
            if not isinstance(item, dict) or set(item) != expected_fields:
                raise RuntimeError("CCP state contains a malformed pairing record")
            device_id = item["device_id"]
            protected_link_key = item["protected_link_key"]
            paired_at = item["paired_at"]
            # A UTC epoch must be finite and representable through year 9999;
            # the same range check rejects NaN/infinity without substitution.
            if (
                not isinstance(device_id, str)
                or not isinstance(protected_link_key, str)
                or not isinstance(paired_at, (int, float))
                or isinstance(paired_at, bool)
                or not 0 < paired_at <= 253402300799
            ):
                raise RuntimeError("CCP state contains invalid pairing record values")
            record = PairingRecord(device_id, protected_link_key, float(paired_at))
            if HEX_PATTERNS["device"].fullmatch(record.device_id) is None:
                raise RuntimeError("CCP state contains an invalid device identity")
            if record.device_id in loaded or not record.protected_link_key:
                raise RuntimeError("CCP state contains a duplicate or incomplete pairing record")
            unprotect_secret(record.protected_link_key)
            loaded[record.device_id] = record
        self.pc_id = pc_id
        self.records = loaded

    # self: Existing PC identity owner; pending records never replace the active state file.
    # Raises RuntimeError for corrupt, mismatched, unreadable or unprotectable candidate state.
    def _load_pending(self) -> None:
        try:
            raw = self._pending_path.read_text(encoding="utf-8")
        except FileNotFoundError:
            print("CCP pending store absent: persist an empty first-use candidate set", file=sys.stderr, flush=True)
            self._save_pending_locked()
            return
        except (OSError, UnicodeError) as error:
            raise RuntimeError(f"CCP pending state read failed: {error}") from error
        try:
            document = json.loads(raw, object_pairs_hook=unique_state_object)
        except ValueError as error:
            raise RuntimeError(f"CCP pending state decode failed: {error}") from error
        if (not isinstance(document, dict) or set(document) != {"version", "pc_id", "records"} or
                type(document["version"]) is not int or document["version"] != 1 or
                document["pc_id"] != self.pc_id or not isinstance(document["records"], list)):
            raise RuntimeError("CCP pending state identity/version/fields are invalid")
        loaded: dict[str, PairingRecord] = {}
        for item in document["records"]:
            if not isinstance(item, dict) or set(item) != {"device_id", "protected_link_key", "paired_at"}:
                raise RuntimeError("CCP pending pairing record is malformed")
            device, protected, paired_at = item["device_id"], item["protected_link_key"], item["paired_at"]
            if (not isinstance(device, str) or HEX_PATTERNS["device"].fullmatch(device) is None or device in loaded or
                    not isinstance(protected, str) or not protected or type(paired_at) not in (int, float) or
                    not 0 < paired_at <= 253402300799):
                raise RuntimeError("CCP pending pairing values are invalid")
            if len(unprotect_secret(protected)) != 32:
                raise RuntimeError("CCP pending key length is invalid")
            loaded[device] = PairingRecord(device, protected, float(paired_at))
        self.pending_records = loaded

    # self: Sole current-user owner of active and candidate relationship files.
    # path: One of the two private state documents.
    # records: Complete current identity map, already validated and DPAPI protected.
    # Raises RuntimeError for directory, write, flush, permission or atomic-replacement failure.
    def _write_records_locked(self, path: Path, records: dict[str, PairingRecord]) -> None:
        document = {
            "version": 1,
            "pc_id": self.pc_id,
            "records": [
                {
                    "device_id": record.device_id,
                    "protected_link_key": record.protected_link_key,
                    "paired_at": record.paired_at,
                }
                for record in sorted(records.values(), key=lambda value: value.device_id)
            ],
        }
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = path.with_suffix(".tmp")
            with temporary.open("w", encoding="utf-8") as output:
                output.write(json.dumps(document, indent=2) + "\n")
                output.flush()
                os.fsync(output.fileno())
            os.chmod(temporary, 0o600)
            os.replace(temporary, path)
        except OSError as error:
            raise RuntimeError(f"CCP state write failed: {error}") from error

    # self: Locked active-state owner; its versioned schema remains unchanged.
    # Raises RuntimeError for the original persistence failure.
    def _save_locked(self) -> None:
        self._write_records_locked(self._path, self.records)

    # self: Locked candidate-state owner; active associations remain untouched.
    # Raises RuntimeError for the original persistence failure.
    def _save_pending_locked(self) -> None:
        self._write_records_locked(self._pending_path, self.pending_records)

    # self: PC state owner checking an explicit new commissioning request before PAIR is sent.
    # device_id: Exact cryptographically selected offer identity.
    # replace_pending: Explicit permission to supersede an unresolved candidate, never inferred from a timeout.
    # Raises ValueError if an unresolved candidate would be discarded implicitly.
    def admit_pairing(self, device_id: str, replace_pending: bool) -> None:
        if type(replace_pending) is not bool:
            raise ValueError("replace_pending must be a boolean")
        with self._lock:
            if device_id in self.pending_records and not replace_pending:
                raise ValueError("CCP has an unresolved pairing candidate; resume Cody or explicitly replace that candidate with a new visible code")

    # self: Candidate owner; the previous active key remains available to Cancel recovery.
    # device_id: Exact offer identity selected by the visible display code.
    # link_key: Candidate key protected before persistence.
    # address: Socket-observed address for non-secret status.
    # replace_pending: Explicitly admitted replacement of an unresolved candidate.
    # Raises ValueError or RuntimeError without silently replacing an ambiguous candidate.
    def stage_pairing(self, device_id: str, link_key: bytes, address: str, replace_pending: bool = False) -> None:
        self.admit_pairing(device_id, replace_pending)
        protected = protect_secret(link_key, f"CCP candidate key for Cody {device_id}")
        with self._lock:
            previous = self.pending_records.get(device_id)
            if previous is not None and not replace_pending:
                raise ValueError("CCP candidate changed before persistence")
            self.pending_records[device_id] = PairingRecord(device_id, protected, time.time(), last_address=address)
            try:
                self._save_pending_locked()
            except RuntimeError:
                if previous is None:
                    self.pending_records.pop(device_id, None)
                else:
                    self.pending_records[device_id] = previous
                raise

    # self: Locked active/candidate owner exposing keys only to the authenticated responder.
    # device_id: Socket-provided identity, accepted only if already retained.
    # Raises RuntimeError for DPAPI failure; neither secret is returned through public status.
    def retained_keys(self, device_id: str) -> RetainedKeys:
        with self._lock:
            active, pending = self.records.get(device_id), self.pending_records.get(device_id)
            return RetainedKeys(unprotect_secret(active.protected_link_key) if active else None,
                                unprotect_secret(pending.protected_link_key) if pending else None)

    # self: Two-file transaction owner; new-key ACTIVE/COMMITTED proves candidate commit.
    # device_id: Exact authenticated device identity.
    # key: Device-confirmed key, either the new candidate or the previous active association.
    # expected_pending: Candidate present when the challenge/exchange began; rejects stale confirmations.
    # Raises ValueError for stale proofs, or RuntimeError for persistence failures.
    def confirm_device_choice(self, device_id: str, key: bytes, expected_pending: bytes | None) -> None:
        with self._lock:
            pending = self.pending_records.get(device_id)
            active = self.records.get(device_id)
            active_key = unprotect_secret(active.protected_link_key) if active else None
            if pending is None:
                if active_key is None or not hmac.compare_digest(active_key, key):
                    raise ValueError("CCP commit has no matching retained association")
                return
            pending_key = unprotect_secret(pending.protected_link_key)
            if expected_pending is None or not hmac.compare_digest(expected_pending, pending_key):
                raise ValueError("CCP commit belongs to a superseded candidate")
            if hmac.compare_digest(pending_key, key):
                self.records[device_id] = pending
                try:
                    self._save_locked()
                except RuntimeError:
                    if active is None:
                        self.records.pop(device_id, None)
                    else:
                        self.records[device_id] = active
                    raise
            elif active_key is None or not hmac.compare_digest(active_key, key):
                raise ValueError("CCP commit key differs from both retained associations")
            else:
                # The previous key keeps working after Cancel, but its recorded
                # FIND/COMMITTED messages could be replayed. They cannot authorize
                # destruction of a newer candidate; only explicit replacement can.
                return
            # A reset between active save and pending removal is recoverable:
            # both files name the same proven key, and a later COMMITTED retries cleanup.
            self.pending_records.pop(device_id)
            try:
                self._save_pending_locked()
            except RuntimeError:
                self.pending_records[device_id] = pending
                raise
            print("CCP device-confirmed pairing transaction completed", file=sys.stderr, flush=True)

    # device_id: Stable device identity confirmed by the pairing response.
    # link_key: Newly derived link key, protected before any state replacement.
    # address: Source IPv4 address of the confirmed device.
    # Raises RuntimeError when protection or atomic persistence fails.
    def remember_pairing(self, device_id: str, link_key: bytes, address: str) -> None:
        protected = protect_secret(link_key, f"CCP link key for Cody {device_id}")
        with self._lock:
            previous = self.records.get(device_id)
            # Persistence and ACTIVE confirm commissioning, not renewable
            # presence. Only an authenticated FIND/READY exchange marks seen.
            record = PairingRecord(device_id, protected, time.time(), last_address=address)
            self.records[device_id] = record
            try:
                self._save_locked()
            except RuntimeError:
                if previous is None:
                    self.records.pop(device_id, None)
                else:
                    self.records[device_id] = previous
                raise

    # device_id: Stable device identity requested by an authenticated FIND packet.
    # Returns None when this PC has no retained relationship with that device.
    # Raises RuntimeError when Windows cannot unprotect the stored key.
    def link_key(self, device_id: str) -> bytes | None:
        with self._lock:
            record = self.records.get(device_id)
            if record is None:
                return None
            return unprotect_secret(record.protected_link_key)

    # self: Current active/candidate owner retaining presence observations in memory only.
    # device_id: Stable identity that produced a verified liveness request.
    # address: Current source IPv4 address, used for status only and never as identity.
    def mark_seen(self, device_id: str, address: str) -> None:
        with self._lock:
            for record in (self.records.get(device_id), self.pending_records.get(device_id)):
                if record is None:
                    continue
                record.last_seen_at = time.time()
                record.last_seen_monotonic = time.monotonic()
                record.last_address = address

    # self: State owner returning a bounded non-secret copy for MCP results.
    def public_status(self) -> list[dict[str, Any]]:
        with self._lock:
            # Wall time is retained for display; clock corrections cannot
            # extend or prematurely expire the authenticated liveness lease.
            now = time.monotonic()
            observed = {**self.records, **self.pending_records}
            return [
                {
                    "device_id": record.device_id,
                    "paired_at": record.paired_at,
                    "last_seen_at": record.last_seen_at,
                    "last_address": record.last_address or None,
                    "source": observation_source(record.last_address),
                    "pairing_pending": record.device_id in self.pending_records,
                    "retained_pairing": record.device_id in self.records,
                    "connected": (
                        record.last_seen_monotonic is not None
                        and 0 <= now - record.last_seen_monotonic <= LIVENESS_TIMEOUT_SECONDS
                    ),
                }
                for record in sorted(observed.values(), key=lambda value: value.device_id)
            ]


# No parameters: acquires the one lifetime UDP listener before any identity read/write.
# Raises OSError when exclusive ownership of UDP port 42118 cannot be acquired.
def open_companion_listener() -> socket.socket:
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        if os.name == "nt" and hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            # One PC identity has one liveness owner. Ambiguous same-host CCP
            # processes must fail visibly instead of sharing the fixed port.
            configure_exclusive_socket(listener)
        listener.bind(("", PLUGIN_PORT))
        listener.settimeout(0.25)
    except BaseException:
        try:
            listener.close()
        except OSError as cleanup_error:
            print(f"CCP listener cleanup failed: {cleanup_error}", file=sys.stderr, flush=True)
        raise
    return listener


class CompanionResponder:
    # self: Sole UDP responder with a bounded transient commit-challenge cache.
    # state: Persistent PC/device relationships shared with pairing tool calls.
    # listener: Exclusively bound socket transferred by the service after state loads.
    # activity: Volatile user-prompt context owned by this broker.
    # requests: Separate volatile request owner; an activity snapshot cannot answer or replace a question.
    def __init__(self, state: StateStore, listener: socket.socket, activity: ActivityStore,
                 requests: interactions.InteractionStore | None = None) -> None:
        self._state = state
        self._activity = activity
        self._requests = requests if requests is not None else interactions.InteractionStore()
        self._stop = threading.Event()
        self._socket = listener
        self._receive_error: OSError | None = None
        self._commit_challenges: dict[str, CommitChallenge] = {}
        self._event_peers: dict[str, EventPeer] = {}
        self._event_lock = threading.Lock()
        self._resets = reset.ResetRequests()
        self._debug_peers: dict[str, EventPeer] = {}
        self._debug_requests: dict[str, dict[str, Any]] = {}
        self._debug_used_nonce: dict[str, str] = {}
        self._thread = threading.Thread(target=self._run, name="CCP-responder", daemon=True)

    # No parameters: starts exactly one background authenticated liveness responder.
    def start(self) -> None:
        self._thread.start()

    # No parameters: requests shutdown and closes the owned socket after the worker exits.
    # Raises RuntimeError if the worker still owns the socket after the shutdown deadline.
    # Raises OSError if socket closure fails.
    def close(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1.0)
        if self._thread.is_alive():
            raise RuntimeError("CCP responder did not stop before the shutdown deadline")
        self._socket.close()

    # No parameters: requires the lifetime listener to remain alive for every tool operation.
    # Raises the original receive OSError or RuntimeError for an unexpectedly stopped worker.
    def require_running(self) -> None:
        if self._receive_error is not None:
            raise self._receive_error
        if not self._thread.is_alive():
            raise RuntimeError("CCP responder is not running")

    # self: Sole socket worker receiving bounded FIND/COMMITTED/REPLY/RESETACK messages without MCP stdout output.
    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                payload, source = self._socket.recvfrom(MAX_DATAGRAM_BYTES + 1)
            except socket.timeout:
                continue
            except OSError as error:
                if not self._stop.is_set():
                    self._receive_error = error
                    print(f"CCP UDP receive failed: {error}", file=sys.stderr, flush=True)
                return
            try:
                self._handle_datagram(payload, source)
            except (OSError, RuntimeError, ValueError) as error:
                print(f"CCP presence message rejected from {source[0]}: {error}", file=sys.stderr, flush=True)

    # self: Authenticated responder; retained candidate keys are never promoted from FIND alone.
    # payload: Candidate CCP1 FIND, COMMITTED, REPLY or RESETACK datagram.
    # source: Observed UDP source address and port.
    # Raises ValueError, RuntimeError, or OSError for rejected identity, proof, state, or send failures.
    def _handle_datagram(self, payload: bytes, source: tuple[str, int]) -> None:
        kind, fields = parse_datagram(payload)
        if kind == "COMMITTED":
            self._accept_committed(fields, source)
            return
        if kind == "REPLY":
            self._accept_reply(fields, source)
            return
        if kind == "EVENTACK":
            self._accept_event_ack(fields, source)
            return
        if kind in {"RESETACK", "FACTORYRESETACK"}:
            operation = reset.ResetOperation.RESTART if kind == "RESETACK" else reset.ResetOperation.FACTORY_RESET
            self._accept_reset_ack(fields, source, operation)
            return
        if kind == "DEBUGACK":
            self._accept_debug_ack(fields, source)
            return
        if kind == "DEBUGCAP":
            self._accept_debug_capability(fields, source)
            return
        if kind != "FIND":
            return
        with_events = set(fields) == {"device", "pc", "nonce", "activity", "events", "current", "state", "proof"}
        with_activity = with_events or set(fields) == {"device", "pc", "nonce", "activity", "proof"}
        if not with_activity and set(fields) != {"device", "pc", "nonce", "proof"}:
            raise ValueError("CCP FIND has unexpected or missing fields")
        if with_activity and fields["activity"] != "1":
            raise ValueError("Unsupported CCP activity capability")
        if with_events:
            if fields["events"] != "2":
                raise ValueError("Unsupported CCP events capability")
            if (interactions.IDENTITY.fullmatch(fields["current"]) is None
                    or fields["state"] not in interactions.DEVICE_STATES
                    or (fields["current"] == "0" * 32) != (fields["state"] == "none")):
                raise ValueError("CCP FIND question observation is incoherent")
        for name in ("device", "pc", "nonce", "proof"):
            if HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError(f"CCP FIND field {name!r} is malformed")
        if fields["pc"] != self._state.pc_id:
            return
        retained = self._state.retained_keys(fields["device"])
        if retained.active is None and retained.pending is None:
            return
        link_key = None
        for candidate in (retained.pending, retained.active):
            if candidate is None:
                continue
            domain = "find-events-v2" if with_events else "find-activity" if with_activity else "find"
            values = [fields["device"], fields["pc"], fields["nonce"]]
            if with_events:
                values.extend((fields["current"], fields["state"]))
            expected = make_proof(candidate, domain, *values)
            if hmac.compare_digest(expected, fields["proof"]):
                link_key = candidate
                break
        if link_key is None:
            raise ValueError("CCP FIND proof does not match the retained identity")
        proof = make_proof(
            link_key, "ready", fields["device"], fields["pc"], fields["nonce"]
        )
        response = encode_datagram(
            "READY",
            [
                ("device", fields["device"]),
                ("pc", fields["pc"]),
                ("nonce", fields["nonce"]),
                ("proof", proof),
            ],
        )
        self._socket.sendto(response, source)
        # Only the newest authenticated challenge per retained identity is live.
        # A strict cap also bounds memory if a user accumulates many associations.
        now = time.monotonic()
        self._commit_challenges = {device: value for device, value in self._commit_challenges.items()
                                   if 0 <= now - value.observed_at <= LIVENESS_TIMEOUT_SECONDS}
        if len(self._commit_challenges) >= 128 and fields["device"] not in self._commit_challenges:
            oldest = min(self._commit_challenges, key=lambda device: self._commit_challenges[device].observed_at)
            del self._commit_challenges[oldest]
        self._commit_challenges[fields["device"]] = CommitChallenge(
            fields["nonce"], link_key, retained.pending, source, now)
        self._state.mark_seen(fields["device"], source[0])
        if with_activity:
            activity = self._activity.snapshot()
            values = [fields["device"], fields["pc"], fields["nonce"],
                      activity["chat"], activity["revision"], activity["phase"],
                      activity["project"] or "-"]
            activity_proof = make_proof(link_key, "activity", *values)
            payload = encode_datagram("ACTIVITY", list(zip(
                ("device", "pc", "nonce", "chat", "revision", "phase", "project"), values
            )) + [("proof", activity_proof)])
            self._socket.sendto(payload, source)
        if with_events and retained.active is not None and hmac.compare_digest(link_key, retained.active):
            # The old active link remains usable during commissioning. A candidate
            # receives interactions only after COMMITTED proves durable promotion.
            with self._event_lock:
                self._event_peers = {identity: peer for identity, peer in self._event_peers.items()
                                     if 0 <= now - peer.observed_at <= LIVENESS_TIMEOUT_SECONDS}
                if len(self._event_peers) >= 128 and fields["device"] not in self._event_peers:
                    raise ValueError("CCP authenticated event peer cache is full")
                self._event_peers[fields["device"]] = EventPeer(fields["nonce"], link_key, source, now)
                self._debug_peers = {identity: peer for identity, peer in self._debug_peers.items()
                                     if 0 <= now - peer.observed_at <= LIVENESS_TIMEOUT_SECONDS}
            request_id, state, ttl, data = self._requests.snapshot(
                fields["device"], hashlib.sha256(link_key).hexdigest(), fields["current"], fields["state"])
            values = [fields["device"], fields["pc"], fields["nonce"], request_id, state, ttl, data]
            event_proof = make_proof(link_key, "event", *values)
            payload = encode_datagram("EVENT", list(zip(
                ("device", "pc", "nonce", "id", "state", "ttl", "data"), values)) + [("proof", event_proof)])
            self._socket.sendto(payload, source)
            request = self._resets.claim(fields["device"], hashlib.sha256(link_key).hexdigest(),
                                         fields["nonce"], source)
            if request is not None:
                # This request is already marked sent: even a socket error has
                # an ambiguous outcome and must never cause an automatic retry.
                kind, _, domain, _ = reset.COMMANDS[request.operation]
                values = [fields["device"], fields["pc"], fields["nonce"], request.request_id, reset.CONFIRMATION_CODE]
                try:
                    payload = encode_datagram(kind, list(zip(("device", "pc", "nonce", "id", "code"), values))
                                              + [("proof", make_proof(link_key, domain, *values))])
                    self._socket.sendto(payload, source)
                except (OSError, ValueError, RuntimeError) as error:
                    self._resets.fail_send(request.request_id, error)
                    raise

    # self: Responder keeping an optional capability separate from the unchanged FIND protocol.
    # fields: Exact signed DEBUGCAP envelope emitted only by Debug firmware after READY.
    # source: Must match the current active authenticated event peer.
    # Raises ValueError for malformed, stale or forged capability advertisements.
    def _accept_debug_capability(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        if set(fields) != {"device", "pc", "nonce", "debug", "proof"} or fields["debug"] != "1":
            raise ValueError("Debug capability fields malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError("Debug capability identity/proof malformed")
        with self._event_lock:
            peer = self._event_peers.get(fields["device"])
            active = self._state.retained_keys(fields["device"]).active
            if (peer is None or active is None or source != peer.source or fields["nonce"] != peer.nonce
                    or fields["pc"] != self._state.pc_id or not hmac.compare_digest(active, peer.key)
                    or not 0 <= time.monotonic() - peer.observed_at <= LIVENESS_TIMEOUT_SECONDS
                    or not hmac.compare_digest(make_proof(peer.key, "debug-cap", fields["device"],
                        fields["pc"], fields["nonce"], "1"), fields["proof"])):
                raise ValueError("Debug capability authentication failed")
            if self._debug_used_nonce.get(fields["device"]) != fields["nonce"]:
                self._debug_peers[fields["device"]] = peer

    # self: Authenticated responder receiving transient Debug requests from MCP.
    # device_id: Explicit retained target with a recent authenticated Debug capability.
    # mode: face_channels, character or normal; no arbitrary route/command is accepted.
    # scroll: Absolute first channel row; firmware checks its real channel count.
    # include_simulator: Explicit authorization to target loopback, never inferred.
    # Raises ValueError or OSError for invalid arguments, missing capability or send failure.
    def debug_screen(self, device_id: str, mode: str, scroll: int, include_simulator: bool) -> dict[str, Any]:
        if not isinstance(device_id, str) or HEX_PATTERNS["device"].fullmatch(device_id) is None:
            raise ValueError("Debug requires an explicit device_id")
        if not isinstance(mode, str) or mode not in {"face_channels", "character", "normal"}:
            raise ValueError("Debug mode must be face_channels, character or normal")
        if type(scroll) is not int or not 0 <= scroll <= 255 or (mode != "face_channels" and scroll != 0):
            raise ValueError("Only face_channels accepts a scroll offset between 0 and 255")
        if type(include_simulator) is not bool:
            raise ValueError("include_simulator must be a boolean")
        with self._event_lock:
            peer = self._debug_peers.get(device_id)
            current = self._event_peers.get(device_id)
            if (peer is None or current is None or peer.nonce != current.nonce
                    or not 0 <= time.monotonic() - peer.observed_at <= LIVENESS_TIMEOUT_SECONDS):
                raise ValueError("No recent authenticated Debug capability; Release firmware does not support this tool")
            source_kind = observation_source(peer.source[0])
            if source_kind == "SIMULATED" and not include_simulator:
                raise ValueError("SIMULATED debug requires explicit include_simulator=true")
            active = self._state.retained_keys(device_id).active
            if active is None or not hmac.compare_digest(active, peer.key):
                raise ValueError("Debug peer belongs to a retired association")
            now = time.monotonic()
            self._debug_requests = {identity: request for identity, request in self._debug_requests.items()
                                    if request["deadline"] > now}
            if device_id in self._debug_requests or len(self._debug_requests) >= 16:
                raise ValueError("Debug request already pending or registry full")
            request = {"id": secrets.token_hex(16), "mode": mode, "scroll": str(scroll),
                       "peer": peer, "deadline": now + 4.0, "event": threading.Event(), "result": None}
            self._debug_requests[device_id] = request
            self._debug_used_nonce[device_id] = peer.nonce
            self._debug_used_nonce = {identity: nonce for identity, nonce in self._debug_used_nonce.items()
                                      if identity == device_id or identity in self._event_peers}
            self._debug_peers.pop(device_id) # A new DEBUGCAP is required before another command.
        values = [device_id, self._state.pc_id, peer.nonce, request["id"], mode, str(scroll)]
        packet = encode_datagram("DEBUG", list(zip(("device", "pc", "nonce", "id", "mode", "scroll"), values))
                                 + [("proof", make_proof(peer.key, "debug", *values))])
        try:
            self._socket.sendto(packet, peer.source)
            request["event"].wait(4.0)
        finally:
            with self._event_lock:
                if self._debug_requests.get(device_id) is request:
                    self._debug_requests.pop(device_id)
        result = request["result"]
        return {"device_id": device_id, "request_id": request["id"], "source": source_kind,
                "mode": mode, "scroll": scroll, "pixels_verified": False,
                **(result if result is not None else {"status": "unknown"})}

    # self: UDP owner authenticating ACK against the exact outstanding private lease.
    # fields: Complete bounded DEBUGACK fields with status and real numeric error.
    # source: Must match the authenticated Debug peer's endpoint.
    # Raises ValueError for malformed, expired, duplicate or forged acknowledgements.
    def _accept_debug_ack(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        names = ("device", "pc", "nonce", "id", "mode", "scroll", "status", "error", "count")
        if set(fields) != {*names, "proof"}:
            raise ValueError("Debug ACK fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError("Debug ACK identity/proof malformed")
        if (re.fullmatch(r"[0-9a-f]{32}", fields["id"]) is None
                or fields["status"] not in {"applied", "rejected"}
                or re.fullmatch(r"-?[0-9]{1,10}", fields["error"]) is None
                or not -(2 ** 31) <= int(fields["error"]) < 2 ** 31
                or re.fullmatch(r"[0-9]{1,3}", fields["count"]) is None
                or not 1 <= int(fields["count"]) <= 255
                or (fields["status"] == "applied") != (int(fields["error"]) == 0)):
            raise ValueError("Debug ACK result malformed")
        with self._event_lock:
            request = self._debug_requests.get(fields["device"])
            if (request is None or request["deadline"] <= time.monotonic() or request["result"] is not None
                    or fields["id"] != request["id"] or fields["mode"] != request["mode"]
                    or fields["scroll"] != request["scroll"] or fields["pc"] != self._state.pc_id):
                raise ValueError("Debug ACK has no matching live request")
            peer = request["peer"]
            active = self._state.retained_keys(fields["device"]).active
            if (source != peer.source or fields["nonce"] != peer.nonce or active is None
                    or not hmac.compare_digest(active, peer.key)
                    or not hmac.compare_digest(make_proof(peer.key, "debug-ack", *(fields[name] for name in names)), fields["proof"])):
                raise ValueError("Debug ACK authentication failed")
            request["result"] = {"status": fields["status"], "error": int(fields["error"]),
                                  "channel_count": int(fields["count"])}
            request["event"].set()

    # self: UDP owner validating a terminal observation against its active event peer.
    # fields: Exact nonce-bound EVENTACK identities and actual terminal device state.
    # source: Must match the authenticated event-capable FIND address and port.
    # Raises ValueError or RuntimeError for schema, freshness, proof, association or lifecycle errors.
    def _accept_event_ack(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        names = ("device", "pc", "nonce", "id", "state")
        if set(fields) != {*names, "proof"}:
            raise ValueError("CCP EVENTACK fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError(f"CCP EVENTACK field {name!r} is malformed")
        if interactions.IDENTITY.fullmatch(fields["id"]) is None:
            raise ValueError("CCP EVENTACK request identity is malformed")
        with self._event_lock:
            peer = self._event_peers.get(fields["device"])
        if (fields["pc"] != self._state.pc_id or peer is None or source != peer.source
                or fields["nonce"] != peer.nonce
                or not 0 <= time.monotonic() - peer.observed_at <= LIVENESS_TIMEOUT_SECONDS):
            raise ValueError("CCP EVENTACK does not match a recent authenticated FIND")
        active = self._state.retained_keys(fields["device"]).active
        if active is None or not hmac.compare_digest(active, peer.key):
            raise ValueError("CCP EVENTACK belongs to a retired association")
        if not hmac.compare_digest(make_proof(active, "event-ack", *(fields[name] for name in names)), fields["proof"]):
            raise ValueError("CCP EVENTACK authentication failed")
        self._requests.acknowledge_terminal(fields["device"], fields["id"], fields["state"],
                                            hashlib.sha256(active).hexdigest())

    # self: Responder sharing only non-secret current extension capability with MCP.
    # device_id: Retained target whose last FIND must advertise events with an active key.
    def supports_events(self, device_id: str) -> bool:
        with self._event_lock:
            peer = self._event_peers.get(device_id)
            return peer is not None and 0 <= time.monotonic() - peer.observed_at <= LIVENESS_TIMEOUT_SECONDS

    # self: Responder admitting a device restart only after an authenticated current-peer acknowledgement.
    # fields: Exact RESETACK fields, without any caller-supplied return address.
    # source: Observed UDP address and port of the paired Cody.
    # operation: Distinct restart or factory-reset acknowledgement domain; never interchangeable.
    # Raises ValueError or RuntimeError for malformed, stale, forged or unrelated acknowledgements.
    def _accept_reset_ack(self, fields: dict[str, str], source: tuple[str, int], operation: reset.ResetOperation) -> None:
        if set(fields) != {"device", "pc", "nonce", "id", "status", "proof"} or fields.get("status") != "accepted":
            raise ValueError("CCP RESETACK fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError(f"CCP RESETACK field {name!r} is malformed")
        if reset.IDENTITY.fullmatch(fields["id"]) is None:
            raise ValueError("CCP RESETACK request identity is invalid")
        with self._event_lock:
            peer = self._event_peers.get(fields["device"])
        if (fields["pc"] != self._state.pc_id or peer is None or source != peer.source
                or fields["nonce"] != peer.nonce
                or not 0 <= time.monotonic() - peer.observed_at <= LIVENESS_TIMEOUT_SECONDS):
            raise ValueError("CCP RESETACK does not match the current authenticated peer")
        active = self._state.retained_keys(fields["device"]).active
        if active is None or not hmac.compare_digest(active, peer.key):
            raise ValueError("CCP RESETACK belongs to a retired association")
        values = [fields[name] for name in ("device", "pc", "nonce", "id", "status")]
        if not hmac.compare_digest(make_proof(active, reset.COMMANDS[operation][3], *values), fields["proof"]):
            raise ValueError("CCP RESETACK authentication failed")
        self._resets.acknowledge(fields["id"], fields["device"], hashlib.sha256(active).hexdigest(), fields["nonce"], source, operation)

    # self: Sole socket worker; a UDP receipt never substitutes for the MCP recipient acknowledgement.
    # fields: Complete REPLY fields, bound to the newest authenticated event-capable FIND.
    # source: Must match that FIND's observed address and port.
    # Raises ValueError, RuntimeError or OSError for invalid/replayed/untrusted replies or send failure.
    def _accept_reply(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        if set(fields) != {"device", "pc", "nonce", "id", "choice", "proof"}:
            raise ValueError("CCP REPLY fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError(f"CCP REPLY field {name!r} is malformed")
        if interactions.IDENTITY.fullmatch(fields["id"]) is None or interactions.CHOICE_ID.fullmatch(fields["choice"]) is None:
            raise ValueError("CCP REPLY ticket or choice identity is invalid")
        with self._event_lock:
            peer = self._event_peers.get(fields["device"])
        if (fields["pc"] != self._state.pc_id or peer is None or source != peer.source
                or fields["nonce"] != peer.nonce
                or not 0 <= time.monotonic() - peer.observed_at <= LIVENESS_TIMEOUT_SECONDS):
            raise ValueError("CCP REPLY does not match a recent authenticated event FIND")
        active = self._state.retained_keys(fields["device"]).active
        if active is None or not hmac.compare_digest(active, peer.key):
            raise ValueError("CCP REPLY belongs to a retired association")
        values = [fields[name] for name in ("device", "pc", "nonce", "id", "choice")]
        if not hmac.compare_digest(make_proof(peer.key, "reply", *values), fields["proof"]):
            raise ValueError("CCP REPLY authentication failed")
        status = self._requests.answer(fields["device"], fields["id"], fields["choice"], hashlib.sha256(active).hexdigest())
        if status in {"expired", "cancelled", "conflict", "unknown"}:
            print(f"CCP reply rejected for device {fields['device']}: {status}", file=sys.stderr, flush=True)
        proof = make_proof(peer.key, "reply-ack", *values, status)
        payload = encode_datagram("REPLYACK", list(zip(("device", "pc", "nonce", "id", "choice"), values))
                                 + [("status", status), ("proof", proof)])
        self._socket.sendto(payload, source)

    # self: Responder owning the bounded, transient challenge cache.
    # fields: Exact nonce-bound device acknowledgement after durable active commit.
    # source: Must equal the authenticated FIND source, including its UDP port.
    # Raises ValueError for stale/forged/ambiguous confirmation, or RuntimeError for persistence failure.
    def _accept_committed(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        if set(fields) != {"device", "pc", "nonce", "proof"}:
            raise ValueError("CCP COMMITTED has unexpected or missing fields")
        for name in fields:
            if HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError(f"CCP COMMITTED field {name!r} is malformed")
        challenge = self._commit_challenges.get(fields["device"])
        if (fields["pc"] != self._state.pc_id or challenge is None or source != challenge.source or
                fields["nonce"] != challenge.nonce or
                not 0 <= time.monotonic() - challenge.observed_at <= LIVENESS_TIMEOUT_SECONDS):
            raise ValueError("CCP COMMITTED does not match a recent authenticated FIND")
        expected = make_proof(challenge.key, "committed", fields["device"], fields["pc"], fields["nonce"])
        if not hmac.compare_digest(expected, fields["proof"]):
            raise ValueError("CCP COMMITTED proof is invalid")
        self._state.confirm_device_choice(fields["device"], challenge.key, challenge.pending_key)
        # Remove only after persistence succeeds; the next READY also requests a new proof.
        self._commit_challenges.pop(fields["device"], None)


class CcpService:
    # self: Loads protected state and starts the local authenticated responder.
    # Raises RuntimeError or OSError when state or the fixed UDP listener is unavailable.
    def __init__(self) -> None:
        if os.name != "nt":
            raise RuntimeError("CCP requires Windows for protected pairing persistence")
        # Acquire ownership first: a second process must fail without creating,
        # reading or overwriting the first process's PC identity and records.
        listener = open_companion_listener()
        try:
            self.state = StateStore()
            self.activity = ActivityStore()
            self.interactions = interactions.InteractionStore()
            self.notifications_error: str | None = None
            self.responder = CompanionResponder(self.state, listener, self.activity, self.interactions)
            self.responder.start()
        except BaseException:
            try:
                listener.close()
            except OSError as cleanup_error:
                print(f"CCP startup cleanup failed: {cleanup_error}", file=sys.stderr, flush=True)
            raise
        self._offers: dict[tuple[str, str], Offer] = {}
        self._offers_lock = threading.Lock()
        self._pairing_lock = threading.Lock()

    # self: Live service admitting an explicit tool request without manufacturing chat identity.
    # arguments: Strict question/notification arguments validated at the MCP boundary.
    # question: Fixed tool purpose; native approvals cannot be requested by an MCP caller.
    # Raises ValueError for an unsupported/unobserved target, implicit simulator use or invalid content.
    def enqueue_interaction(self, arguments: dict[str, Any], question: bool) -> dict[str, Any]:
        device_id = arguments.get("device_id")
        if not isinstance(device_id, str) or HEX_PATTERNS["device"].fullmatch(device_id) is None:
            raise ValueError("device_id must identify the intended retained Cody")
        include_simulator = arguments.get("include_simulator", False)
        if type(include_simulator) is not bool:
            raise ValueError("include_simulator must be a boolean")
        target = next((item for item in self.state.public_status() if item["device_id"] == device_id), None)
        if target is None or not target["retained_pairing"]:
            raise ValueError("The requested Cody has no confirmed retained pairing")
        if target["source"] == "UNOBSERVED" or not self.responder.supports_events(device_id):
            raise ValueError("Cody has not recently advertised the authenticated events extension; wake or update it")
        if target["source"] == "SIMULATED" and not include_simulator:
            raise ValueError("A SIMULATED interaction requires explicit include_simulator=true")
        payload = interactions.payload_from_arguments(arguments, question)
        active = self.state.retained_keys(device_id).active
        if active is None:
            raise ValueError("The requested Cody active association changed before enqueue")
        return self.interactions.enqueue(device_id, payload, target["source"], arguments.get("ttl_seconds", 180),
                                         hashlib.sha256(active).hexdigest())

    # self: Service requesting one explicitly selected restart or factory reset; ACK proves admission only.
    # arguments: Exact MCP arguments; code comes from explicit current-message user intent, never a default.
    # operation: Explicit tool purpose; a restart can never escalate to erasing settings.
    # Raises ValueError for absent confirmation, an unobserved/unpaired target or implicit simulator use.
    def reset_device(self, arguments: dict[str, Any], operation: reset.ResetOperation) -> dict[str, Any]:
        reset.validate_confirmation(arguments.get("confirmation_code"))
        device_id = arguments.get("device_id")
        if not isinstance(device_id, str) or HEX_PATTERNS["device"].fullmatch(device_id) is None:
            raise ValueError("device_id must identify the paired Cody intended for this reset operation")
        include_simulator = arguments.get("include_simulator", False)
        if type(include_simulator) is not bool:
            raise ValueError("include_simulator must be a boolean")
        target = next((item for item in self.state.public_status() if item["device_id"] == device_id), None)
        if target is None or not target["retained_pairing"] or target["source"] == "UNOBSERVED":
            raise ValueError("Device reset requires an observed, confirmed retained pairing")
        if target.get("pairing_pending"):
            raise ValueError("Complete or explicitly cancel the pending pairing before requesting a reset operation")
        if target["source"] == "SIMULATED" and not include_simulator:
            raise ValueError("A SIMULATED reset operation requires explicit include_simulator=true")
        if not self.responder.supports_events(device_id):
            raise ValueError("Device reset requires a recent authenticated CCP 0.6 peer; wake or update Cody")
        active = self.state.retained_keys(device_id).active
        if active is None:
            raise ValueError("The active pairing changed before reset admission")
        identity = self.responder._resets.enqueue(device_id, target["source"], hashlib.sha256(active).hexdigest(),
                                                 arguments["confirmation_code"], operation)
        return self.responder._resets.wait(identity)

    # self: Service accepting only genuine hook metadata after ActivityStore validates it.
    # event: Metadata from the authenticated hook endpoint, never model-created lifecycle data.
    # Raises ValueError for a full bounded notification queue; the native prompt still owns authorization.
    def notify_native_permission(self, event: dict[str, Any]) -> None:
        if event.get("hook_event_name") == "UserPromptSubmit":
            self.interactions.retire_native(None)
        elif event.get("hook_event_name") in {"Stop", "Interrupt", "SessionEnd"}:
            self.interactions.retire_native(event["session_id"])
        if event.get("hook_event_name") != "PermissionRequest":
            return
        activity = self.activity.snapshot()
        digest = hashlib.sha256(event["session_id"].encode("ascii")).hexdigest()
        if activity["chat"] != digest or activity["phase"] != "paused":
            return
        context = bytes.fromhex(activity["project"]).decode("ascii") if activity["project"] else ""
        payload = interactions.EventPayload(interactions.EventKind.APPROVAL_ON_PC, context,
                                            "Approval needed", "Continue on your PC")
        for target in self.state.public_status():
            # Automatic native notices are physical-only; a simulator requires an
            # explicit interaction request and never receives native approval data.
            if (target["retained_pairing"] and target["source"] == "LAN"
                    and self.responder.supports_events(target["device_id"])):
                active = self.state.retained_keys(target["device_id"]).active
                if active is None:
                    raise ValueError("Native notice target lost its active association")
                self.interactions.enqueue(target["device_id"], payload, "LAN",
                                           association_id=hashlib.sha256(active).hexdigest(),
                                           native_session=event["session_id"])

    # No parameters: stops the background UDP listener.
    # Raises RuntimeError or OSError when responder shutdown or socket closure fails.
    def close(self) -> None:
        self.responder.close()

    # self: Service replacing its current, non-secret discovery cache.
    # timeout_ms: Bounded LAN collection window requested by the MCP caller.
    # include_simulator: Explicitly admit the separately labelled loopback simulator.
    # Raises ValueError or OSError for invalid bounds or socket failures.
    def discover(self, timeout_ms: int, include_simulator: bool = False) -> list[Offer]:
        if timeout_ms < 250 or timeout_ms > 3000:
            raise ValueError("timeout_ms must be between 250 and 3000")
        if type(include_simulator) is not bool:
            raise ValueError("include_simulator must be a boolean")
        request_id = secrets.token_hex(8)
        query = encode_datagram("DISCOVER", [("request", request_id)])
        found: dict[tuple[str, str], Offer] = {}
        deadline = time.monotonic() + timeout_ms / 1000.0
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as transport:
            transport.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            transport.bind(("", 0))
            transport.settimeout(0.1)
            transport.sendto(query, ("255.255.255.255", DEVICE_PORT))
            if include_simulator:
                transport.sendto(query, ("127.0.0.1", DEVICE_PORT))
            while time.monotonic() < deadline:
                try:
                    payload, source = transport.recvfrom(MAX_DATAGRAM_BYTES + 1)
                except socket.timeout:
                    continue
                if observation_source(source[0]) == "SIMULATED" and not include_simulator:
                    continue
                try:
                    kind, fields = parse_datagram(payload)
                    if kind != "OFFER":
                        continue
                    offer = offer_from_fields(fields, source[0], request_id)
                except ValueError:
                    continue
                found[(offer.device_id, offer.session_id)] = offer
        with self._offers_lock:
            self._offers = found
        return sorted(found.values(), key=lambda value: (value.device_id, value.address))

    # code: Six-character value supplied from the selected physical or explicitly simulated display.
    # offers: Bounded offers from one discovery window.
    # Returns every cryptographically matching offer and its derived verifier.
    def _matching_offers(self, code: str, offers: list[Offer]) -> list[tuple[Offer, bytes]]:
        matches: list[tuple[Offer, bytes]] = []
        for offer in offers:
            verifier = derive_verifier(code, offer.salt)
            expected = make_proof(
                verifier,
                "offer",
                offer.request_id,
                offer.device_id,
                offer.session_id,
                offer.salt,
            )
            if hmac.compare_digest(expected, offer.tag):
                matches.append((offer, verifier))
        return matches

    # self: Service serializing commissioning operations against one candidate per device.
    # code: Six-character value read from the selected physical or explicitly simulated display.
    # replace_pending: Explicit permission to supersede an unresolved PC candidate with this fresh code.
    # Raises ValueError, RuntimeError, TimeoutError, or OSError for invalid, ambiguous, rejected, or failed pairing.
    def pair(self, code: str, replace_pending: bool = False) -> dict[str, Any]:
        if type(replace_pending) is not bool:
            raise ValueError("replace_pending must be a boolean")
        with self._pairing_lock:
            return self._pair_locked(code, replace_pending)

    # self: Service with exclusive commissioning ownership for this request.
    # code: Explicit display code used to select one cryptographically matching offer.
    # replace_pending: Permission to supersede an unresolved candidate; never inferred from timeout.
    # Raises ValueError, RuntimeError, TimeoutError or OSError for discovery, protocol or storage failures.
    def _pair_locked(self, code: str, replace_pending: bool) -> dict[str, Any]:
        normalized = code.strip().upper()
        if CODE_PATTERN.fullmatch(normalized) is None:
            raise ValueError("The Cody code must contain six characters from A-H, J-N, P-Z, or 2-9")
        with self._offers_lock:
            offers = list(self._offers.values())
        used_cached_offers = bool(offers)
        if not offers:
            offers = self.discover(1200)
        matches = self._matching_offers(normalized, offers)
        if not matches and used_cached_offers:
            # A display-code retry rotates the device session. Refresh once so
            # an older MCP discovery result cannot hide the current offer.
            include_simulator = any(observation_source(value.address) == "SIMULATED" for value in offers)
            matches = self._matching_offers(normalized, self.discover(1200, include_simulator))
        if not matches:
            raise ValueError("No current Cody offer matches the displayed code")
        if len(matches) != 1:
            raise RuntimeError("The displayed code matches more than one Cody offer; restart pairing")
        offer, verifier = matches[0]
        self.state.admit_pairing(offer.device_id, replace_pending)
        nonce = secrets.token_hex(16)
        port = str(PLUGIN_PORT)
        proof = make_proof(
            verifier,
            "pair",
            offer.device_id,
            offer.session_id,
            self.state.pc_id,
            port,
            nonce,
        )
        request = encode_datagram(
            "PAIR",
            [
                ("device", offer.device_id),
                ("session", offer.session_id),
                ("pc", self.state.pc_id),
                ("port", port),
                ("nonce", nonce),
                ("proof", proof),
            ],
        )
        link_key = hmac.new(
            verifier,
            canonical_text(
                "link", offer.device_id, offer.session_id, self.state.pc_id, nonce
            ),
            hashlib.sha256,
        ).digest()
        # Persist before PAIR: losing every PAIRED reply must still leave the
        # key available for authenticated rediscovery of Cody's candidate.
        self.state.stage_pairing(offer.device_id, link_key, offer.address, replace_pending)
        self._exchange_pairing_request(offer, request, nonce, link_key)
        confirmed_at = self._exchange_confirmation(offer, nonce, link_key)
        self.state.confirm_device_choice(offer.device_id, link_key, link_key)
        return {
            "paired": True,
            "device_id": offer.device_id,
            "address": offer.address,
            "session_id": offer.session_id,
            "confirmed_at": confirmed_at,
            "source": observation_source(offer.address),
        }

    # offer: Exact cached offer selected by the user's code.
    # request: Bounded PAIR datagram containing no clear-text code.
    # nonce: Pairing nonce that binds the confirmation.
    # link_key: Derived key used to authenticate the confirmation.
    # Raises TimeoutError, ValueError, or OSError when no valid confirmation arrives.
    def _exchange_pairing_request(
        self, offer: Offer, request: bytes, nonce: str, link_key: bytes
    ) -> float:
        deadline = time.monotonic() + 3.0
        next_send = 0.0
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as transport:
            transport.bind(("", 0))
            transport.settimeout(0.15)
            while time.monotonic() < deadline:
                if time.monotonic() >= next_send:
                    transport.sendto(request, (offer.address, DEVICE_PORT))
                    next_send = time.monotonic() + 0.5
                try:
                    payload, source = transport.recvfrom(MAX_DATAGRAM_BYTES + 1)
                except socket.timeout:
                    continue
                if source[0] != offer.address:
                    continue
                try:
                    kind, fields = parse_datagram(payload)
                except ValueError:
                    continue
                expected_fields = {"device", "session", "pc", "nonce", "proof"}
                if kind != "PAIRED" or set(fields) != expected_fields:
                    continue
                if (
                    fields["device"] != offer.device_id
                    or fields["session"] != offer.session_id
                    or fields["pc"] != self.state.pc_id
                    or fields["nonce"] != nonce
                ):
                    continue
                expected = make_proof(
                    link_key,
                    "paired",
                    offer.device_id,
                    offer.session_id,
                    self.state.pc_id,
                    nonce,
                )
                if not hmac.compare_digest(expected, fields["proof"]):
                    raise ValueError("Cody returned an invalid pairing confirmation proof")
                return time.time()
        raise TimeoutError("Cody did not confirm pairing before the bounded timeout")

    # offer: Exact offer whose device-side persistence was authenticated.
    # nonce: Pairing nonce shared by PAIR, PAIRED, CONFIRM, and ACTIVE.
    # link_key: Persisted key used to authenticate the final two-way commit.
    # Raises TimeoutError, ValueError, or OSError when Cody cannot activate the persisted link.
    def _exchange_confirmation(self, offer: Offer, nonce: str, link_key: bytes) -> float:
        proof = make_proof(
            link_key,
            "confirm",
            offer.device_id,
            offer.session_id,
            self.state.pc_id,
            nonce,
        )
        request = encode_datagram(
            "CONFIRM",
            [
                ("device", offer.device_id),
                ("session", offer.session_id),
                ("pc", self.state.pc_id),
                ("nonce", nonce),
                ("proof", proof),
            ],
        )
        deadline = time.monotonic() + 3.0
        next_send = 0.0
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as transport:
            transport.bind(("", 0))
            transport.settimeout(0.15)
            while time.monotonic() < deadline:
                if time.monotonic() >= next_send:
                    transport.sendto(request, (offer.address, DEVICE_PORT))
                    next_send = time.monotonic() + 0.5
                try:
                    payload, source = transport.recvfrom(MAX_DATAGRAM_BYTES + 1)
                except socket.timeout:
                    continue
                if source[0] != offer.address:
                    continue
                try:
                    kind, fields = parse_datagram(payload)
                except ValueError:
                    continue
                expected_fields = {"device", "session", "pc", "nonce", "proof"}
                if kind != "ACTIVE" or set(fields) != expected_fields:
                    continue
                if (
                    fields["device"] != offer.device_id
                    or fields["session"] != offer.session_id
                    or fields["pc"] != self.state.pc_id
                    or fields["nonce"] != nonce
                ):
                    continue
                expected = make_proof(
                    link_key,
                    "active",
                    offer.device_id,
                    offer.session_id,
                    self.state.pc_id,
                    nonce,
                )
                if not hmac.compare_digest(expected, fields["proof"]):
                    raise ValueError("Cody returned an invalid activation proof")
                return time.time()
        raise TimeoutError("Cody did not activate the persisted pairing before timeout")


# offers: Current unique offers returned by one discovery request.
def public_offers(offers: list[Offer]) -> list[dict[str, Any]]:
    return [
        {
            "device_id": offer.device_id,
            "address": offer.address,
            "session_id": offer.session_id,
            "received_at": offer.received_at,
            "source": observation_source(offer.address),
        }
        for offer in offers
    ]


# payload: Structured data visible to Codex.
# summary: Concise non-secret description for the conversational result.
# is_error: Whether the tool completed with an operation error.
def tool_result(payload: dict[str, Any], summary: str, is_error: bool = False) -> dict[str, Any]:
    return {
        "content": [{"type": "text", "text": summary}],
        "structuredContent": payload,
        "isError": is_error,
    }


# service: Live CCP service used by all tool handlers in this MCP process.
# name: MCP tool name selected by Codex.
# arguments: Validated-at-boundary JSON object supplied by Codex.
# Raises ValueError for unknown tools or invalid argument types.
# Raises OSError or RuntimeError for responder, network or persistence failures.
def call_tool(service: CcpService, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    service.responder.require_running()
    if name == "ccp_discover_devices":
        if set(arguments) - {"timeout_ms", "include_simulator"}:
            raise ValueError("Discovery arguments contain unexpected fields")
        timeout_ms = arguments.get("timeout_ms", 1200)
        if not isinstance(timeout_ms, int) or isinstance(timeout_ms, bool):
            raise ValueError("timeout_ms must be an integer")
        include_simulator = arguments.get("include_simulator", False)
        if type(include_simulator) is not bool:
            raise ValueError("include_simulator must be a boolean")
        offers = public_offers(service.discover(timeout_ms, include_simulator))
        simulated_count = sum(item["source"] == "SIMULATED" for item in offers)
        return tool_result(
            {"devices": offers, "count": len(offers)},
            f"Discovered {len(offers)} Cody pairing offer(s); {simulated_count} SIMULATED on loopback.",
        )
    if name == "ccp_pair_device":
        if "code" not in arguments or set(arguments) - {"code", "replace_pending"}:
            raise ValueError("Pairing requires code and accepts only optional replace_pending")
        code = arguments.get("code")
        if not isinstance(code, str):
            raise ValueError("code must be a string")
        replace_pending = arguments.get("replace_pending", False)
        if type(replace_pending) is not bool:
            raise ValueError("replace_pending must be a boolean")
        paired = service.pair(code, replace_pending)
        return tool_result(
            paired,
            f"{paired['source']} Cody {paired['device_id']} confirmed pairing with this CCP instance.",
        )
    if name == "ccp_connection_status":
        if arguments:
            raise ValueError("Connection status does not accept arguments")
        devices = service.state.public_status()
        activity = service.activity.snapshot()
        return tool_result(
            {"pc_id": service.state.pc_id, "devices": devices, "count": len(devices),
             "follow_mode": "latest_user_prompt",
             "notifications_error": service.notifications_error,
             "activity": {item["device_id"]: activity for item in devices}},
            f"CCP retains {len(devices)} Cody pairing record(s).",
        )
    if name == "ccp_list_sessions":
        if arguments:
            raise ValueError("Session listing does not accept arguments")
        sessions = service.activity.list_sessions()
        return tool_result({"sessions": sessions}, f"Observed {len(sessions)} chat(s) through Codex hooks.")
    if name == "ccp_debug_screen":
        if set(arguments) - {"device_id", "mode", "scroll", "include_simulator"}:
            raise ValueError("Debug arguments contain unexpected fields")
        result = service.responder.debug_screen(arguments.get("device_id"), arguments.get("mode"),
                                                 arguments.get("scroll", 0), arguments.get("include_simulator", False))
        return tool_result(result, f"{result['source']} debug request {result['status']}; display pixels were not verified.",
                           result["status"] == "rejected")
    if name in {"ccp_reset_device", "ccp_factory_reset_device"}:
        if set(arguments) - {"device_id", "confirmation_code", "include_simulator"}:
            raise ValueError("Reset operation arguments contain unexpected fields")
        operation = reset.ResetOperation.RESTART if name == "ccp_reset_device" else reset.ResetOperation.FACTORY_RESET
        result = service.reset_device(arguments, operation)
        return tool_result(result, f"{result['source']} Cody {result['operation']} is {result['state']}; completion is not verified.",
                           is_error=not result["accepted"])
    if name in {"ccp_ask_question", "ccp_publish_notification"}:
        question = name == "ccp_ask_question"
        allowed = {"device_id", "context", "title", "body", "ttl_seconds", "include_simulator"}
        allowed |= {"choices"} if question else {"kind", "pr"}
        if set(arguments) - allowed:
            raise ValueError("Interaction arguments contain unexpected fields")
        result = service.enqueue_interaction(arguments, question)
        return tool_result(result, f"{result['source']} Cody request queued; no response has been selected.")
    if name == "ccp_poll_request":
        if set(arguments) - {"request_id", "capability", "wait_ms", "acknowledge_answer_id"}:
            raise ValueError("Request polling arguments contain unexpected fields")
        result = service.interactions.poll(arguments.get("request_id"), arguments.get("capability"),
            arguments.get("wait_ms", 0), arguments.get("acknowledge_answer_id"))
        return tool_result(result, f"{result['source']} Cody request is {result['state']}.")
    if name == "ccp_cancel_request":
        if set(arguments) != {"request_id", "capability"}:
            raise ValueError("Cancellation requires only request_id and capability")
        result = service.interactions.cancel(arguments["request_id"], arguments["capability"])
        return tool_result(result, f"{result['source']} Cody request is {result['state']}; no option was inferred.")
    raise ValueError(f"Unknown CCP tool: {name}")


# request: Decoded MCP JSON-RPC request.
# service: Live CCP tool implementation.
# Raises ValueError for missing or malformed methods and parameters.
def handle_request(request: dict[str, Any], service: CcpService) -> dict[str, Any] | None:
    method = request.get("method")
    request_id = request.get("id")
    if method == "notifications/initialized":
        return None
    if method == "initialize":
        params = request.get("params")
        if not isinstance(params, dict):
            raise ValueError("initialize params must be an object")
        protocol_version = params.get("protocolVersion", "2025-06-18")
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {
                "protocolVersion": protocol_version,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {
                    "name": "cody-codex-plugin",
                    "title": "CCP",
                    "version": SERVER_VERSION,
                },
                "instructions": (
                    "Discover before pairing. Pair only with the six-character code visible "
                    "on the physical Cody display, or on Cody Studio only when SIMULATED pairing "
                    "was explicitly requested and discovery used include_simulator=true. "
                    "Always identify SIMULATED results. replace_pending requires explicit permission "
                    "to discard an unresolved PC candidate and a new visible code. Never reveal cryptographic protocol fields. "
                    "Trusted hooks automatically follow the latest user-prompt chat. "
                    "Do not ask the user to select a chat on Cody or fabricate activity events. "
                    "Explicit Cody questions are separate from native permissions and chat focus. "
                    "Keep the returned request capability private. Poll for a reply, then poll again "
                    "with its answer_id as acknowledge_answer_id before using that decision. "
                    "Only that acknowledgement confirms Reply sent. Cancelled, expired, offline or "
                    "pending states never select an answer. PR notifications are read-only."
                ),
            },
        }
    if method == "ping":
        return {"jsonrpc": "2.0", "id": request_id, "result": {}}
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": request_id, "result": {"tools": tool_catalog()}}
    if method == "tools/call":
        params = request.get("params")
        if not isinstance(params, dict) or not isinstance(params.get("name"), str):
            raise ValueError("tools/call params must include a tool name")
        arguments = params.get("arguments", {})
        if not isinstance(arguments, dict):
            raise ValueError("tools/call arguments must be an object")
        try:
            result = call_tool(service, params["name"], arguments)
        except (OSError, RuntimeError, TimeoutError, ValueError) as error:
            print(f"CCP tool operation failed: {error}", file=sys.stderr, flush=True)
            result = tool_result({"error": str(error)}, str(error), True)
        return {"jsonrpc": "2.0", "id": request_id, "result": result}
    if request_id is None:
        return None
    return {
        "jsonrpc": "2.0",
        "id": request_id,
        "error": {"code": -32601, "message": f"Method not found: {method}"},
    }


# No parameters: returns stable tool schemas and accurate safety annotations.
def tool_catalog() -> list[dict[str, Any]]:
    return [
        {
            "name": "ccp_discover_devices",
            "title": "Discover Cody devices",
            "description": "Find current Cody LAN pairing offers; include_simulator explicitly adds labelled loopback offers.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "timeout_ms": {
                        "type": "integer",
                        "minimum": 250,
                        "maximum": 3000,
                        "default": 1200,
                    },
                    "include_simulator": {
                        "type": "boolean",
                        "default": False,
                        "description": "Only enable for an explicitly requested Cody Studio simulator session.",
                    },
                },
                "additionalProperties": False,
            },
            "annotations": {
                "readOnlyHint": True,
                "destructiveHint": False,
                "openWorldHint": False,
            },
        },
        {
            "name": "ccp_pair_device",
            "title": "Pair Cody",
            "description": (
                "Pair the single current Cody offer matching the six-character code visible "
                "on the physical device or the explicitly requested SIMULATED Cody Studio. "
                "This changes the matched target and local plugin state."
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "code": {
                        "type": "string",
                        "pattern": "^[A-HJ-NP-Z2-9]{6}$",
                        "description": "Six-character code currently visible on Cody.",
                    },
                    "replace_pending": {
                        "type": "boolean",
                        "default": False,
                        "description": "Explicitly replace an unresolved PC candidate using this new visible code; never infer permission from timeout.",
                    },
                },
                "required": ["code"],
                "additionalProperties": False,
            },
            "annotations": {
                "readOnlyHint": False,
                "destructiveHint": False,
                "openWorldHint": False,
            },
        },
        {
            "name": "ccp_connection_status",
            "title": "Show Cody connection status",
            "description": "Read retained Cody identities and current authenticated liveness.",
            "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
            "annotations": {
                "readOnlyHint": True,
                "destructiveHint": False,
                "openWorldHint": False,
            },
        },
    ] + activity_tool_catalog() + interactions.tool_catalog() + [debug_tool_definition()] + [reset.tool_definition(operation) for operation in reset.ResetOperation]


# No parameters: private bounded Debug screen tool, omitted from public manuals.
def debug_tool_definition() -> dict[str, Any]:
    return {"name": "ccp_debug_screen", "title": "Cody Debug screen",
        "description": "Request live face_channels, character with snapshot HUD, or normal on an authenticated Debug Cody. ACK confirms worker application only, never display pixels.",
        "inputSchema": {"type": "object", "additionalProperties": False,
            "properties": {"device_id": {"type": "string", "pattern": "^[0-9a-f]{12}$"},
                "mode": {"type": "string", "enum": ["face_channels", "character", "normal"]},
                "scroll": {"type": "integer", "minimum": 0, "maximum": 255, "default": 0},
                "include_simulator": {"type": "boolean", "default": False}},
            "required": ["device_id", "mode"]},
        "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False}}


# No parameters: exposes observed context without any model-callable selection or activity setter.
def activity_tool_catalog() -> list[dict[str, Any]]:
    return [{"name": "ccp_list_sessions",
             "description": "List hook-observed chats and the automatically followed chat; no transcript contents.",
             "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
             "annotations": {"readOnlyHint": True, "destructiveHint": False, "openWorldHint": False}}]


# response: JSON-RPC response written as exactly one stdout line.
def write_response(response: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
    sys.stdout.flush()


# No parameters: attaches this MCP transport to the single local background broker.
# Raises RuntimeError or OSError when the protected broker cannot be reached.
def run_stdio() -> int:
    from ccp_runtime import BrokerClient
    client = BrokerClient()
    try:
        for line in sys.stdin:
            try:
                request = json.loads(line)
                if not isinstance(request, dict):
                    raise ValueError("MCP request must be a JSON object")
                response = client.request("mcp", request)
            except (json.JSONDecodeError, ValueError) as error:
                response = {
                    "jsonrpc": "2.0",
                    "id": None,
                    "error": {"code": -32700, "message": str(error)},
                }
            if response is not None:
                write_response(response)
    finally:
        client.close()
    return 0


# argv: Optional explicit command-line values used by packaging or diagnostics.
# Raises OSError, RuntimeError or ValueError when the requested runtime operation fails.
def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="CCP - Cody's Codex Plugin MCP server")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--stdio", action="store_true", help="run the MCP STDIO transport")
    mode.add_argument("--broker", action="store_true", help="own the local CCP runtime")
    mode.add_argument("--hook", action="store_true", help="ingest one trusted Codex hook from stdin")
    arguments = parser.parse_args(argv)
    if arguments.stdio:
        return run_stdio()
    from ccp_runtime import run_broker, run_hook
    return run_broker() if arguments.broker else run_hook()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"CCP startup failed: {error}", file=sys.stderr, flush=True)
        raise SystemExit(1) from error
