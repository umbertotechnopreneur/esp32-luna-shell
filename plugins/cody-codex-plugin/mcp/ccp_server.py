"""Cody's Codex Plugin MCP server and bounded local-network responder."""

from __future__ import annotations

import argparse
import base64
import ctypes
from ctypes import wintypes
from dataclasses import dataclass
import hashlib
import hmac
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


PROTOCOL = "CCP1"
DEVICE_PORT = 42117
PLUGIN_PORT = 42118
MAX_DATAGRAM_BYTES = 512
PBKDF2_ITERATIONS = 50_000
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


class DataBlob(ctypes.Structure):
    _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_ubyte))]


# No parameters: returns Windows cryptography libraries with exact pointer signatures.
# Raises RuntimeError when called outside Windows.
def windows_cryptography_libraries() -> tuple[Any, Any]:
    if os.name != "nt":
        raise RuntimeError("CCP local link-key persistence requires Windows DPAPI")
    crypt32 = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    crypt32.CryptProtectData.argtypes = [
        ctypes.POINTER(DataBlob),
        wintypes.LPCWSTR,
        ctypes.POINTER(DataBlob),
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(DataBlob),
    ]
    crypt32.CryptProtectData.restype = wintypes.BOOL
    crypt32.CryptUnprotectData.argtypes = [
        ctypes.POINTER(DataBlob),
        ctypes.POINTER(wintypes.LPWSTR),
        ctypes.POINTER(DataBlob),
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(DataBlob),
    ]
    crypt32.CryptUnprotectData.restype = wintypes.BOOL
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p
    return crypt32, kernel32


# kernel32: Loaded Windows kernel library with the LocalFree signature configured.
# pointer: DPAPI-owned pointer returned through a DATA_BLOB or description field.
# label: Non-secret diagnostic name for the owned allocation.
# Raises RuntimeError when Windows cannot release the allocation.
def release_windows_buffer(kernel32: Any, pointer: Any, label: str) -> None:
    if pointer and kernel32.LocalFree(ctypes.cast(pointer, ctypes.c_void_p)):
        raise RuntimeError(f"Windows DPAPI {label} release failed: {ctypes.get_last_error()}")


# value: Secret bytes protected for the current Windows user.
# description: Non-secret label shown only by Windows credential diagnostics.
# Raises RuntimeError when DPAPI cannot protect or release the value.
def protect_secret(value: bytes, description: str) -> str:
    source_buffer = ctypes.create_string_buffer(value)
    source = DataBlob(len(value), ctypes.cast(source_buffer, ctypes.POINTER(ctypes.c_ubyte)))
    protected = DataBlob()
    crypt32, kernel32 = windows_cryptography_libraries()
    if not crypt32.CryptProtectData(
        ctypes.byref(source), description, None, None, None, 0x1, ctypes.byref(protected)
    ):
        raise RuntimeError(f"Windows DPAPI protection failed: {ctypes.get_last_error()}")
    try:
        payload = ctypes.string_at(protected.pbData, protected.cbData)
        return base64.b64encode(payload).decode("ascii")
    finally:
        release_windows_buffer(kernel32, protected.pbData, "protected buffer")


# encoded: Base64 DPAPI blob previously created for the current Windows user.
# Raises RuntimeError when the blob is malformed, belongs to another user, or cannot be released.
def unprotect_secret(encoded: str) -> bytes:
    try:
        payload = base64.b64decode(encoded, validate=True)
    except (ValueError, TypeError) as error:
        raise RuntimeError("CCP protected link key is not valid base64") from error
    source_buffer = ctypes.create_string_buffer(payload)
    source = DataBlob(len(payload), ctypes.cast(source_buffer, ctypes.POINTER(ctypes.c_ubyte)))
    clear = DataBlob()
    description = wintypes.LPWSTR()
    crypt32, kernel32 = windows_cryptography_libraries()
    if not crypt32.CryptUnprotectData(
        ctypes.byref(source), ctypes.byref(description), None, None, None, 0x1, ctypes.byref(clear)
    ):
        raise RuntimeError(f"Windows DPAPI unprotect failed: {ctypes.get_last_error()}")
    try:
        value = ctypes.string_at(clear.pbData, clear.cbData)
        if len(value) != 32:
            raise RuntimeError("CCP protected link key has an invalid length")
        return value
    finally:
        release_error: RuntimeError | None = None
        try:
            release_windows_buffer(kernel32, description, "description")
        except RuntimeError as error:
            release_error = error
        try:
            release_windows_buffer(kernel32, clear.pbData, "clear buffer")
        except RuntimeError as error:
            if release_error is None:
                release_error = error
        if release_error is not None:
            raise release_error


# values: Ordered lowercase ASCII fields covered by one protocol proof.
def canonical_text(*values: str) -> bytes:
    return "|".join(values).encode("ascii")


# key: Thirty-two byte verifier or link key.
# values: Ordered protocol fields included in the proof.
def make_proof(key: bytes, *values: str) -> str:
    return hmac.new(key, canonical_text(*values), hashlib.sha256).hexdigest()


# code: Normalized six-character code visible on the physical display.
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


# message: Printable ASCII CCP1 datagram.
# Raises ValueError for malformed, ambiguous, oversized, or duplicate fields.
def parse_datagram(message: bytes) -> tuple[str, dict[str, str]]:
    if not message or len(message) > MAX_DATAGRAM_BYTES:
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


# kind: Uppercase CCP1 message identity.
# fields: Ordered field names and already validated printable ASCII values.
# Raises ValueError when the encoded datagram exceeds the protocol bound.
def encode_datagram(kind: str, fields: list[tuple[str, str]]) -> bytes:
    text = "|".join([PROTOCOL, kind, *[f"{name}={value}" for name, value in fields]])
    payload = text.encode("ascii")
    if len(payload) > MAX_DATAGRAM_BYTES:
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


class StateStore:
    # No parameters: loads or creates the current Windows user's CCP identity.
    # Raises RuntimeError for corrupt, partial, inaccessible, or unprotectable state.
    def __init__(self) -> None:
        local_app_data = os.environ.get("LOCALAPPDATA")
        if not local_app_data:
            raise RuntimeError("LOCALAPPDATA is unavailable; CCP state cannot be owned safely")
        self._path = Path(local_app_data) / "CodyCodexPlugin" / "state.json"
        self._lock = threading.Lock()
        self.pc_id = ""
        self.records: dict[str, PairingRecord] = {}
        self._load()

    # No parameters: reads one exact versioned document or creates a genuinely absent identity.
    # Raises RuntimeError for invalid types, versions, identities, records, or filesystem failures.
    def _load(self) -> None:
        if not self._path.exists():
            self.pc_id = secrets.token_hex(16)
            self._save_locked()
            return
        try:
            document = json.loads(self._path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            raise RuntimeError(f"CCP state read failed: {error}") from error
        if not isinstance(document, dict) or document.get("version") != 1:
            raise RuntimeError("CCP state version is missing or unsupported")
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
            if (
                not isinstance(device_id, str)
                or not isinstance(protected_link_key, str)
                or not isinstance(paired_at, (int, float))
                or isinstance(paired_at, bool)
                or paired_at <= 0
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

    # No parameters: atomically persists the current identity and protected records.
    # Raises RuntimeError when directory creation, file write, permission, or replacement fails.
    def _save_locked(self) -> None:
        document = {
            "version": 1,
            "pc_id": self.pc_id,
            "records": [
                {
                    "device_id": record.device_id,
                    "protected_link_key": record.protected_link_key,
                    "paired_at": record.paired_at,
                }
                for record in sorted(self.records.values(), key=lambda value: value.device_id)
            ],
        }
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self._path.with_suffix(".tmp")
            temporary.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
            os.chmod(temporary, 0o600)
            os.replace(temporary, self._path)
        except OSError as error:
            raise RuntimeError(f"CCP state write failed: {error}") from error

    # device_id: Stable device identity confirmed by the pairing response.
    # link_key: Newly derived link key, protected before any state replacement.
    # address: Source IPv4 address of the confirmed device.
    # Raises RuntimeError when protection or atomic persistence fails.
    def remember_pairing(self, device_id: str, link_key: bytes, address: str) -> None:
        protected = protect_secret(link_key, f"CCP link key for Cody {device_id}")
        with self._lock:
            previous = self.records.get(device_id)
            record = PairingRecord(device_id, protected, time.time(), time.time(), address)
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

    # device_id: Stable identity that produced a verified liveness request.
    # address: Current source IPv4 address, used for status only and never as identity.
    def mark_seen(self, device_id: str, address: str) -> None:
        with self._lock:
            record = self.records.get(device_id)
            if record is not None:
                record.last_seen_at = time.time()
                record.last_address = address

    # No parameters: returns a bounded non-secret copy for MCP results.
    def public_status(self) -> list[dict[str, Any]]:
        with self._lock:
            now = time.time()
            return [
                {
                    "device_id": record.device_id,
                    "paired_at": record.paired_at,
                    "last_seen_at": record.last_seen_at,
                    "last_address": record.last_address or None,
                    "connected": record.last_seen_at > 0 and now - record.last_seen_at <= 10.0,
                }
                for record in sorted(self.records.values(), key=lambda value: value.device_id)
            ]


class CompanionResponder:
    # state: Persistent PC/device relationships shared with pairing tool calls.
    # Raises OSError when UDP port 42118 cannot be bound.
    def __init__(self, state: StateStore) -> None:
        self._state = state
        self._stop = threading.Event()
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if os.name == "nt" and hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            # One PC identity has one liveness owner. Ambiguous same-host CCP
            # processes must fail visibly instead of sharing the fixed port.
            self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        self._socket.bind(("", PLUGIN_PORT))
        self._socket.settimeout(0.25)
        self._thread = threading.Thread(target=self._run, name="CCP-responder", daemon=True)

    # No parameters: starts exactly one background authenticated liveness responder.
    def start(self) -> None:
        self._thread.start()

    # No parameters: requests shutdown and closes the owned socket after the worker exits.
    def close(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1.0)
        self._socket.close()

    # No parameters: receives bounded FIND datagrams without writing to MCP stdout.
    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                payload, source = self._socket.recvfrom(MAX_DATAGRAM_BYTES + 1)
            except socket.timeout:
                continue
            except OSError as error:
                if not self._stop.is_set():
                    print(f"CCP UDP receive failed: {error}", file=sys.stderr, flush=True)
                return
            try:
                self._handle_find(payload, source)
            except (OSError, RuntimeError, ValueError) as error:
                print(f"CCP FIND rejected from {source[0]}: {error}", file=sys.stderr, flush=True)

    # payload: Candidate CCP1 FIND datagram.
    # source: Observed UDP source address and port.
    # Raises ValueError, RuntimeError, or OSError for rejected identity, proof, state, or send failures.
    def _handle_find(self, payload: bytes, source: tuple[str, int]) -> None:
        kind, fields = parse_datagram(payload)
        if kind != "FIND" or set(fields) != {"device", "pc", "nonce", "proof"}:
            return
        for name in ("device", "pc", "nonce", "proof"):
            if HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError(f"CCP FIND field {name!r} is malformed")
        if fields["pc"] != self._state.pc_id:
            return
        link_key = self._state.link_key(fields["device"])
        if link_key is None:
            return
        expected = make_proof(
            link_key, "find", fields["device"], fields["pc"], fields["nonce"]
        )
        if not hmac.compare_digest(expected, fields["proof"]):
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
        self._state.mark_seen(fields["device"], source[0])


class CcpService:
    # No parameters: loads protected state and starts the local authenticated responder.
    # Raises RuntimeError or OSError when state or the fixed UDP listener is unavailable.
    def __init__(self) -> None:
        self.state = StateStore()
        self.responder = CompanionResponder(self.state)
        self.responder.start()
        self._offers: dict[tuple[str, str], Offer] = {}
        self._offers_lock = threading.Lock()

    # No parameters: stops the background UDP listener.
    def close(self) -> None:
        self.responder.close()

    # timeout_ms: Bounded LAN collection window requested by the MCP caller.
    # Raises ValueError or OSError for invalid bounds or socket failures.
    def discover(self, timeout_ms: int) -> list[Offer]:
        if timeout_ms < 250 or timeout_ms > 3000:
            raise ValueError("timeout_ms must be between 250 and 3000")
        request_id = secrets.token_hex(8)
        query = encode_datagram("DISCOVER", [("request", request_id)])
        found: dict[tuple[str, str], Offer] = {}
        deadline = time.monotonic() + timeout_ms / 1000.0
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as transport:
            transport.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            transport.bind(("", 0))
            transport.settimeout(0.1)
            transport.sendto(query, ("255.255.255.255", DEVICE_PORT))
            while time.monotonic() < deadline:
                try:
                    payload, source = transport.recvfrom(MAX_DATAGRAM_BYTES + 1)
                except socket.timeout:
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

    # code: Normalized six-character value supplied from Cody's physical display.
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

    # code: Six-character value read by the user from Cody's physical display.
    # Raises ValueError, RuntimeError, TimeoutError, or OSError for invalid, ambiguous, rejected, or failed pairing.
    def pair(self, code: str) -> dict[str, Any]:
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
            matches = self._matching_offers(normalized, self.discover(1200))
        if not matches:
            raise ValueError("No current Cody offer matches the displayed code")
        if len(matches) != 1:
            raise RuntimeError("The displayed code matches more than one Cody offer; restart pairing")
        offer, verifier = matches[0]
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
        self._exchange_pairing_request(offer, request, nonce, link_key)
        # Cody's PAIRED response proves its NVS write. Protect the matching PC
        # record before sending CONFIRM so neither UI can report a one-sided link.
        self.state.remember_pairing(offer.device_id, link_key, offer.address)
        confirmed_at = self._exchange_confirmation(offer, nonce, link_key)
        return {
            "paired": True,
            "device_id": offer.device_id,
            "address": offer.address,
            "session_id": offer.session_id,
            "confirmed_at": confirmed_at,
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
def call_tool(service: CcpService, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    if name == "ccp_discover_devices":
        timeout_ms = arguments.get("timeout_ms", 1200)
        if not isinstance(timeout_ms, int) or isinstance(timeout_ms, bool):
            raise ValueError("timeout_ms must be an integer")
        offers = public_offers(service.discover(timeout_ms))
        return tool_result(
            {"devices": offers, "count": len(offers)},
            f"Discovered {len(offers)} Cody pairing offer(s) on the local network.",
        )
    if name == "ccp_pair_device":
        code = arguments.get("code")
        if not isinstance(code, str):
            raise ValueError("code must be a string")
        paired = service.pair(code)
        return tool_result(
            paired,
            f"Cody {paired['device_id']} confirmed pairing with this CCP instance.",
        )
    if name == "ccp_connection_status":
        devices = service.state.public_status()
        return tool_result(
            {"pc_id": service.state.pc_id, "devices": devices, "count": len(devices)},
            f"CCP retains {len(devices)} Cody pairing record(s).",
        )
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
                "serverInfo": {"name": "cody-codex-plugin", "version": "0.1.0"},
                "instructions": (
                    "Discover before pairing. Pair only with the six-character code visible "
                    "on the physical Cody display. Never reveal cryptographic protocol fields."
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
            "description": "Find current Cody pairing offers on the local LAN without changing device state.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "timeout_ms": {
                        "type": "integer",
                        "minimum": 250,
                        "maximum": 3000,
                        "default": 1200,
                    }
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
                "on the physical device. This changes device and local plugin state."
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "code": {
                        "type": "string",
                        "pattern": "^[A-HJ-NP-Z2-9]{6}$",
                        "description": "Six-character code currently visible on Cody.",
                    }
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
    ]


# response: JSON-RPC response written as exactly one stdout line.
def write_response(response: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
    sys.stdout.flush()


# No parameters: runs the MCP JSONL loop while the local responder remains active.
# Raises RuntimeError or OSError when CCP cannot initialize its protected state or UDP listener.
def run_stdio() -> int:
    service = CcpService()
    try:
        for line in sys.stdin:
            try:
                request = json.loads(line)
                if not isinstance(request, dict):
                    raise ValueError("MCP request must be a JSON object")
                response = handle_request(request, service)
            except (json.JSONDecodeError, ValueError) as error:
                response = {
                    "jsonrpc": "2.0",
                    "id": None,
                    "error": {"code": -32700, "message": str(error)},
                }
            if response is not None:
                write_response(response)
    finally:
        service.close()
    return 0


# argv: Optional explicit command-line values used by packaging or diagnostics.
def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Cody's Codex Plugin MCP server")
    parser.add_argument("--stdio", action="store_true", help="run the MCP STDIO transport")
    arguments = parser.parse_args(argv)
    if not arguments.stdio:
        parser.error("--stdio is required")
    return run_stdio()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"CCP startup failed: {error}", file=sys.stderr, flush=True)
        raise SystemExit(1) from error
