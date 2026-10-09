"""SIMULATED Cody endpoint using real CCP1 authentication on loopback only."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from enum import Enum
import hashlib
import hmac
import json
import math
import os
from pathlib import Path
import secrets
import socket
import sys
import threading
import time
from typing import Callable

from runtime_paths import simulator_artifacts_root

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))
import ccp_server as protocol  # noqa: E402

LOOPBACK = "127.0.0.1"
CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
PAIRING_SECONDS = 300.0
FIND_SECONDS = 2.0
PRESENCE_SECONDS = 8.0
PHASES = frozenset(("none", "active", "working", "editing", "running", "ready",
                    "paused", "interrupted", "ended", "unavailable"))
Send = Callable[[bytes, tuple[str, int]], object]


class LinkState(str, Enum):
    STOPPED = "Stopped"
    UNPAIRED = "Unpaired"
    PAIRING = "Pairing"
    INCOMPLETE = "Pairing incomplete"
    PAIRED = "Paired"
    DISCOVERING = "Looking for PC"
    CONNECTED = "Connected"
    NETWORK_DOWN = "No connection"
    TIMED_OUT = "Pairing expired"


@dataclass(frozen=True)
class SimulatorSnapshot:
    source: str = "SIMULATED"
    device_id: str = ""
    state: LinkState = LinkState.STOPPED
    retained_pairing: bool = False
    pending_pairing: bool = False
    network_ready: bool = True
    phase: str = "none"
    project: str = ""
    chat: str = ""
    activity_valid: bool = False
    accepted_packets: int = 0
    rejected_packets: int = 0
    last_error: str = ""


# values: Required hexadecimal values, keyed by their protocol field identity.
# Raises ValueError for missing, unsupported or malformed protocol fields.
def require_hex(values: dict[str, str]) -> None:
    for name, value in values.items():
        if protocol.HEX_PATTERNS[name].fullmatch(value) is None:
            raise ValueError(f"Malformed CCP {name} field")


class SimulatorStore:
    # self: Isolated simulator identity and confirmed/candidate associations.
    # path: Exact persistent simulator file under artifacts, separate from PC and firmware state.
    # Raises OSError, RuntimeError or ValueError for unavailable or invalid persisted state.
    def __init__(self, path: Path) -> None:
        self.path = Path(path).resolve()
        if not self.path.is_relative_to(simulator_artifacts_root()):
            raise ValueError("SIMULATED CCP state must remain under artifacts")
        try:
            raw = self.path.read_text(encoding="utf-8")
        except FileNotFoundError:
            self.document = {"version": 1, "source": "SIMULATED",
                             "device_id": "02" + secrets.token_hex(5), "active": None, "pending": None}
            self._write(self.document)
            print("SIMULATED CCP identity absent: created isolated first-use state", file=sys.stderr, flush=True)
            return
        document = json.loads(raw, object_pairs_hook=protocol.unique_state_object)
        self._validate(document)
        self.document = document

    # self: Simulator state owner; validation does not repair any field.
    # document: Exact decoded persisted document.
    # Raises ValueError or RuntimeError for invalid state or failed DPAPI authentication.
    def _validate(self, document: object) -> None:
        if not isinstance(document, dict) or set(document) != {"version", "source", "device_id", "active", "pending"}:
            raise ValueError("SIMULATED CCP state fields are invalid")
        if type(document["version"]) is not int or document["version"] != 1 or document["source"] != "SIMULATED":
            raise ValueError("SIMULATED CCP state identity/version is invalid")
        device = document["device_id"]
        if not isinstance(device, str) or not device.startswith("02"):
            raise ValueError("SIMULATED CCP device identity is invalid")
        require_hex({"device": device})
        for name in ("active", "pending"):
            record = document[name]
            if record is None:
                continue
            expected = {"pc_id", "protected_link_key"}
            if name == "pending":
                expected |= {"session", "nonce", "expires_at"}
            if not isinstance(record, dict) or set(record) != expected:
                raise ValueError("SIMULATED CCP association fields are invalid")
            if not isinstance(record["pc_id"], str) or not isinstance(record["protected_link_key"], str):
                raise ValueError("SIMULATED CCP association types are invalid")
            require_hex({"pc": record["pc_id"]})
            if len(protocol.unprotect_secret(record["protected_link_key"])) != 32:
                raise ValueError("SIMULATED CCP protected key length is invalid")
            if name == "pending":
                if not isinstance(record["session"], str) or not isinstance(record["nonce"], str):
                    raise ValueError("SIMULATED CCP candidate identity is invalid")
                require_hex({"session": record["session"], "nonce": record["nonce"]})
                expiry = record["expires_at"]
                if type(expiry) not in (int, float) or not math.isfinite(expiry) or expiry <= 0:
                    raise ValueError("SIMULATED CCP candidate expiry is invalid")

    # self: Sole writer of this simulator-owned document.
    # document: Complete candidate document; memory is committed only after atomic replacement.
    # Raises OSError on directory creation, write, flush or replacement failure.
    def _write(self, document: dict) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(".tmp")
        with temporary.open("w", encoding="utf-8") as output:
            json.dump(document, output, separators=(",", ":"))
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, self.path)

    # self: Isolated active/candidate transaction owner.
    # active: Confirmed association, or None after explicit Forget.
    # pending: Durable unconfirmed candidate, or None after commit/cancel/Forget; expiry never deletes it.
    # Raises OSError, RuntimeError or ValueError without replacing the prior memory state.
    def save(self, active: dict | None, pending: dict | None) -> None:
        document = deepcopy(self.document)
        document.update(active=deepcopy(active), pending=deepcopy(pending))
        self._validate(document)
        self._write(document)
        self.document = document


class SimulatorLink:
    # self: Pure protocol owner, independent of Tk, sockets and firmware snapshots.
    # store: Dedicated simulator persistence owner.
    # clock: Monotonic source used for active code windows and presence freshness.
    # wall_clock: UTC source recording the original commissioning window for diagnostics.
    def __init__(self, store: SimulatorStore, clock=time.monotonic, wall_clock=time.time) -> None:
        self.store, self.clock, self.wall_clock = store, clock, wall_clock
        self.state = LinkState.INCOMPLETE if self.pending else LinkState.DISCOVERING if self.active else LinkState.UNPAIRED
        self.network_ready = True
        self._code = ""
        self._salt = ""
        self._session = ""
        self._verifier = b""
        self._deadline = 0.0
        self._nonce = ""
        self._next_find = 0.0
        self._ready_at: float | None = None
        self._activity_at: float | None = None
        self._activity_nonce = ""
        self._activation: tuple[str, str, str, float] | None = None
        self.phase, self.project, self.chat = "none", "", ""
        self.accepted_packets = self.rejected_packets = 0
        self.last_error = ""
        self._logged_error = ""
        self._logged_error_at = 0.0
        self._duplicate_errors = 0

    @property
    # self: Owner whose active record changes only after durable CONFIRM/READY commit.
    def active(self) -> dict | None:
        return self.store.document["active"]

    @property
    # self: Owner of a durable candidate that does not replace the active association.
    def pending(self) -> dict | None:
        return self.store.document["pending"]

    @property
    # self: Simulator owner exposing only its synthetic protocol identity.
    def device_id(self) -> str:
        return self.store.document["device_id"]

    # self: Current owner; the result is consumed only by the visible simulator display.
    def display_code(self) -> str:
        return self._code if not self.pending and self.clock() < self._deadline else ""

    # self: Current simulator state without code, salt, verifier or protected values.
    def snapshot(self) -> SimulatorSnapshot:
        valid = self._activity_at is not None and self.clock() - self._activity_at < PRESENCE_SECONDS
        return SimulatorSnapshot(device_id=self.device_id, state=self.state,
            retained_pairing=self.active is not None, pending_pairing=self.pending is not None,
            network_ready=self.network_ready, phase=self.phase, project=self.project, chat=self.chat,
            activity_valid=valid and self.state == LinkState.CONNECTED,
            accepted_packets=self.accepted_packets, rejected_packets=self.rejected_packets,
            last_error=self.last_error)

    # self: Explicit user commissioning target; never called by discovery.
    # replace_confirmed: User confirmation for an existing saved association.
    # Raises ValueError when replacement is implicit, offline, or another window is active.
    def begin_pairing(self, replace_confirmed: bool = False) -> None:
        if not self.network_ready:
            raise ValueError("SIMULATED network is offline; pairing was not started")
        if self.active is not None and not replace_confirmed:
            raise ValueError("Already paired: explicit replacement confirmation required")
        if self._code or self.pending:
            raise ValueError("Cancel the current or incomplete pairing before opening a new code")
        self._code = "".join(secrets.choice(CODE_ALPHABET) for _ in range(6))
        self._salt, self._session = secrets.token_hex(16), secrets.token_hex(4)
        self._verifier = protocol.derive_verifier(self._code, self._salt)
        self._deadline = self.clock() + PAIRING_SECONDS
        self.state = LinkState.PAIRING
        self._nonce = ""
        self._ready_at = self._activity_at = None
        self.last_error = ""

    # self: Owner retiring all ephemeral display-only commissioning values.
    def _clear_code(self) -> None:
        self._code, self._salt, self._session, self._verifier = "", "", "", b""
        self._deadline = 0.0

    # self: Explicit Cancel target; the confirmed association remains intact.
    # Raises OSError or RuntimeError if the candidate cannot be discarded durably.
    def cancel_pairing(self) -> None:
        self.store.save(self.active, None)
        self._clear_code()
        self._activation = None
        self._next_find = 0.0
        self.state = (LinkState.NETWORK_DOWN if not self.network_ready else
                      LinkState.DISCOVERING if self.active else LinkState.UNPAIRED)
        self.last_error = ""

    # self: Explicit Forget target, restricted to the simulator-owned record.
    # confirmed: Single explicit UI confirmation; absence is rejected.
    # Raises ValueError for absent confirmation, or the original persistence failure.
    def forget_pairing(self, confirmed: bool) -> None:
        if confirmed is not True:
            raise ValueError("SIMULATED Forget pairing requires explicit confirmation")
        self.store.save(None, None)
        self._clear_code()
        self._activation = None
        self._nonce = ""
        self._ready_at = self._activity_at = None
        self.phase, self.project, self.chat = "none", "", ""
        self.state = LinkState.UNPAIRED if self.network_ready else LinkState.NETWORK_DOWN
        self.last_error = ""

    # self: Synthetic network-policy owner; no physical radio is changed.
    # ready: Explicit simulator connectivity toggle.
    # Raises ValueError for a non-boolean policy.
    def set_network_ready(self, ready: bool) -> None:
        if type(ready) is not bool:
            raise ValueError("SIMULATED network state must be boolean")
        self.network_ready = ready
        self._next_find = 0.0
        if not ready:
            self._nonce = ""
            self._ready_at = self._activity_at = None
            self.last_error = "SIMULATED network is offline"
            if not self._code and not self.pending:
                self.state = LinkState.NETWORK_DOWN
        elif not self._code and not self.pending:
            self.state = LinkState.DISCOVERING if self.active else LinkState.UNPAIRED

    # self: Active owner retaining the original rejection without logging datagrams.
    # operation: Fixed non-secret operation label.
    # error: Original protocol, storage or socket error.
    def record_error(self, operation: str, error: Exception) -> None:
        self.rejected_packets += 1
        self.last_error = f"{operation}: {type(error).__name__}: {error}"
        now = self.clock()
        if self.last_error == self._logged_error and now - self._logged_error_at < 60:
            self._duplicate_errors += 1
            return
        print(f"SIMULATED CCP {self.last_error}; repeated={self._duplicate_errors}", file=sys.stderr, flush=True)
        self._logged_error, self._logged_error_at, self._duplicate_errors = self.last_error, now, 0

    # self: Simulator owner producing bounded liveness requests only for retained identities.
    # send: Socket-owner callback; failures remain real and do not mark presence.
    # Raises the original crypto, storage or transport error.
    def tick(self, send: Send) -> None:
        now = self.clock()
        if self._code and now >= self._deadline:
            self._clear_code()
            self.state = LinkState.INCOMPLETE if self.pending else LinkState.TIMED_OUT
            self.last_error = "Waiting for saved candidate confirmation" if self.pending else "Pairing code expired"
        record = self.pending or self.active
        if not self.network_ready or (self._code and not self.pending) or record is None:
            return
        if self._ready_at is not None and now - self._ready_at >= PRESENCE_SECONDS:
            self.state = LinkState.INCOMPLETE if self.pending else LinkState.DISCOVERING
            self.last_error = "No authenticated READY for eight seconds"
        if now < self._next_find:
            return
        self._next_find = now + FIND_SECONDS
        nonce = secrets.token_hex(16)
        key = protocol.unprotect_secret(record["protected_link_key"])
        pc = record["pc_id"]
        fields = [("device", self.device_id), ("pc", pc), ("nonce", nonce), ("activity", "1")]
        proof = protocol.make_proof(key, "find-activity", self.device_id, pc, nonce)
        send(protocol.encode_datagram("FIND", fields + [("proof", proof)]), (LOOPBACK, protocol.PLUGIN_PORT))
        self._nonce = nonce
        if self.state != LinkState.CONNECTED:
            self.state = LinkState.INCOMPLETE if self.pending else LinkState.DISCOVERING

    # self: Protocol owner accepting only the real loopback CCP peer.
    # payload: Bounded CCP1 bytes, never logged or published as UI data.
    # source: Socket-observed loopback address and sender port.
    # send: Sole socket-owner callback for protocol responses.
    # Raises ValueError, RuntimeError or OSError for every rejected field/proof or failed operation.
    def handle_datagram(self, payload: bytes, source: tuple[str, int], send: Send) -> None:
        if source[0] != LOOPBACK:
            raise ValueError("SIMULATED CCP accepts loopback peers only")
        if not self.network_ready:
            return
        kind, fields = protocol.parse_datagram(payload)
        if kind == "DISCOVER":
            if set(fields) != {"request"}:
                raise ValueError("Malformed DISCOVER fields")
            require_hex(fields)
            if not self.display_code():
                return
            values = [fields["request"], self.device_id, self._session, self._salt]
            tag = protocol.make_proof(self._verifier, "offer", *values)
            send(protocol.encode_datagram("OFFER", list(zip(("request", "device", "session", "salt"), values))
                                          + [("tag", tag)]), source)
        elif kind == "PAIR":
            self._accept_pair(fields, source, send)
        elif kind == "CONFIRM":
            self._accept_confirm(fields, source, send)
        elif kind in ("READY", "ACTIVITY"):
            if source[1] != protocol.PLUGIN_PORT:
                raise ValueError("CCP presence sender port is invalid")
            self._accept_presence(kind, fields, source, send)
        else:
            raise ValueError("Unsupported CCP datagram kind")
        self.accepted_packets += 1

    # self: Owner of the currently displayed commissioning code.
    # fields: Exact PAIR field set from the socket parser.
    # source: Observed source to receive the durable-candidate acknowledgement.
    # send: Callback that sends the PAIRED response after successful persistence.
    # Raises ValueError or the original crypto/storage/send failure.
    def _accept_pair(self, fields: dict[str, str], source: tuple[str, int], send: Send) -> None:
        if set(fields) != {"device", "session", "pc", "port", "nonce", "proof"}:
            raise ValueError("Malformed PAIR fields")
        require_hex({name: value for name, value in fields.items() if name != "port"})
        if (not self._code or self.clock() >= self._deadline or
                fields["device"] != self.device_id or fields["session"] != self._session):
            raise ValueError("PAIR does not match the active display window")
        if fields["port"] != str(protocol.PLUGIN_PORT):
            raise ValueError("PAIR plugin port is invalid")
        values = [fields[key] for key in ("device", "session", "pc", "nonce")]
        expected = protocol.make_proof(self._verifier, "pair", *values[:3], fields["port"], values[3])
        if not hmac.compare_digest(expected, fields["proof"]):
            raise ValueError("PAIR authentication failed")
        if self.pending and any(self.pending[key] != fields[field] for key, field in
                                (("pc_id", "pc"), ("session", "session"), ("nonce", "nonce"))):
            raise ValueError("A different confirmed-code candidate already owns this window")
        key = hmac.new(self._verifier, protocol.canonical_text("link", *values), hashlib.sha256).digest()
        if not self.pending:
            candidate = {"pc_id": fields["pc"], "session": fields["session"], "nonce": fields["nonce"],
                         "protected_link_key": protocol.protect_secret(key, "SIMULATED Cody CCP candidate"),
                         "expires_at": self.wall_clock() + max(0.0, self._deadline - self.clock())}
            self.store.save(self.active, candidate)
            self.state = LinkState.INCOMPLETE
            self._next_find = 0.0
        proof = protocol.make_proof(key, "paired", *values)
        send(protocol.encode_datagram("PAIRED", list(zip(("device", "session", "pc", "nonce"), values))
                                      + [("proof", proof)]), source)

    # self: Candidate transaction owner; a valid CONFIRM proves the PC persisted its counterpart.
    # fields: Exact CONFIRM field set.
    # source: Observed sender receiving ACTIVE only after durable active commit.
    # send: Callback whose success permits the UI to report Paired.
    # Raises ValueError or the original crypto/storage/send failure.
    def _accept_confirm(self, fields: dict[str, str], source: tuple[str, int], send: Send) -> None:
        if set(fields) != {"device", "session", "pc", "nonce", "proof"}:
            raise ValueError("Malformed CONFIRM fields")
        require_hex(fields)
        if fields["device"] != self.device_id:
            raise ValueError("CONFIRM device identity differs")
        identity = (fields["session"], fields["pc"], fields["nonce"])
        candidate = self.pending
        replay = self._activation is not None and identity == self._activation[:3] and self.clock() < self._activation[3]
        if candidate is not None:
            if identity != (candidate["session"], candidate["pc_id"], candidate["nonce"]):
                raise ValueError("CONFIRM candidate is mismatched")
            record = candidate
        elif replay and self.active:
            record = self.active
        else:
            raise ValueError("CONFIRM has no matching durable candidate")
        key = protocol.unprotect_secret(record["protected_link_key"])
        values = [fields[name] for name in ("device", "session", "pc", "nonce")]
        if not hmac.compare_digest(protocol.make_proof(key, "confirm", *values), fields["proof"]):
            raise ValueError("CONFIRM authentication failed")
        if candidate:
            self.store.save({name: candidate[name] for name in ("pc_id", "protected_link_key")}, None)
            self._activation = (*identity, self.clock() + 10.0)
        proof = protocol.make_proof(key, "active", *values)
        send(protocol.encode_datagram("ACTIVE", list(zip(("device", "session", "pc", "nonce"), values))
                                      + [("proof", proof)]), source)
        self._clear_code()
        if candidate:
            self._ready_at = self._activity_at = None
            self._activity_nonce = ""
            self.phase, self.project, self.chat = "none", "", ""
        if self.state != LinkState.CONNECTED:
            self.state = LinkState.PAIRED
        self._next_find = 0.0
        self.last_error = ""

    # self: Retained-link owner; only the current FIND nonce can produce a fresh observation.
    # kind: READY or ACTIVITY protocol identity.
    # fields: Strict fields authenticated before any presentation state changes.
    # source: Authenticated PC endpoint receiving the post-commit acknowledgement.
    # send: Sole socket callback, used only after the active state is durable.
    # Raises ValueError, RuntimeError or OSError for malformed input or crypto/storage/send failures.
    def _accept_presence(self, kind: str, fields: dict[str, str], source: tuple[str, int], send: Send) -> None:
        expected = {"device", "pc", "nonce", "proof"}
        if kind == "ACTIVITY":
            expected |= {"chat", "revision", "phase", "project"}
        if set(fields) != expected:
            raise ValueError("Malformed CCP presence fields")
        require_hex({name: fields[name] for name in ("device", "pc", "nonce", "proof")})
        record = self.pending or self.active
        if (record is None or (self._code and not self.pending) or
                not self._nonce or fields["nonce"] != self._nonce):
            raise ValueError("CCP presence belongs to an inactive or stale request")
        if fields["device"] != self.device_id or fields["pc"] != record["pc_id"]:
            raise ValueError("CCP presence identity differs")
        key = protocol.unprotect_secret(record["protected_link_key"])
        names = ("device", "pc", "nonce") if kind == "READY" else ("device", "pc", "nonce", "chat", "revision", "phase", "project")
        if not hmac.compare_digest(protocol.make_proof(key, kind.lower(), *(fields[name] for name in names)), fields["proof"]):
            raise ValueError("CCP presence authentication failed")
        if kind == "READY":
            if self.pending:
                # A fresh authenticated reply proves that the PC retained this candidate key.
                candidate = self.pending
                self.store.save({name: candidate[name] for name in ("pc_id", "protected_link_key")}, None)
                self._activation = (candidate["session"], candidate["pc_id"], candidate["nonce"], self.clock() + 10.0)
                self._clear_code()
            self.state, self._ready_at, self.last_error = LinkState.CONNECTED, self.clock(), ""
            values = [fields[name] for name in ("device", "pc", "nonce")]
            proof = protocol.make_proof(key, "committed", *values)
            # Resend even after prior activation: lost COMMITTED must not strand
            # the host candidate or require another commissioning code.
            send(protocol.encode_datagram("COMMITTED", list(zip(("device", "pc", "nonce"), values))
                                          + [("proof", proof)]), source)
            return
        if self.state != LinkState.CONNECTED or self._activity_nonce == fields["nonce"]:
            raise ValueError("ACTIVITY requires READY and one reply per nonce")
        if fields["phase"] not in PHASES or len(fields["chat"]) != 64 or len(fields["revision"]) != 16:
            raise ValueError("ACTIVITY metadata is malformed")
        for name in ("chat", "revision"):
            if any(character not in "0123456789abcdef" for character in fields[name]):
                raise ValueError("ACTIVITY identity encoding is malformed")
        if fields["project"] == "-":
            project = ""
        else:
            encoded = fields["project"]
            if len(encoded) % 2 or len(encoded) > 48 or any(character not in "0123456789abcdef" for character in encoded):
                raise ValueError("ACTIVITY project encoding is malformed")
            project = bytes.fromhex(encoded).decode("ascii")
            if not project or any(not 32 <= ord(character) <= 126 for character in project):
                raise ValueError("ACTIVITY project is not bounded printable ASCII")
        empty = fields["phase"] == "none"
        if empty != (fields["chat"] == "0" * 64) or empty != (project == ""):
            raise ValueError("ACTIVITY empty context is incoherent")
        self.phase, self.project, self.chat = fields["phase"], project, fields["chat"]
        self._activity_at, self._activity_nonce = self.clock(), fields["nonce"]


class SimulatorEndpoint:
    # self: Optional socket owner; construction performs no network or state-file access.
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._stop = threading.Event()
        self._socket: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._link: SimulatorLink | None = None
        self._fatal_error: Exception | None = None

    # self: Interactive simulator owner explicitly admitting loopback networking.
    # Raises the original socket, persisted-state or thread-start error.
    def start(self) -> None:
        if self._socket is not None:
            raise RuntimeError("SIMULATED CCP endpoint already started")
        transport = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            transport.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            transport.bind((LOOPBACK, protocol.DEVICE_PORT))
            transport.settimeout(0.1)
            # Acquire the listener before reading/creating an identity, just like CCP.
            store = SimulatorStore(simulator_artifacts_root() / "cody-simulator-state" / "device.json")
            self._link = SimulatorLink(store)
            self._socket = transport
            self._thread = threading.Thread(target=self._run, name="SIMULATED-Cody-CCP", daemon=True)
            self._thread.start()
        except BaseException:
            transport.close()
            self._socket = None
            raise

    # self: Sole socket owner; rejected packets retain real errors without stopping rendering.
    def _run(self) -> None:
        maintenance_after = 0.0
        maintenance_failures = 0
        while not self._stop.is_set():
            try:
                payload, source = self._socket.recvfrom(protocol.MAX_DATAGRAM_BYTES + 1)
            except socket.timeout:
                payload, source = None, None
            except OSError as error:
                with self._lock:
                    self._fatal_error = error
                    self._link.record_error("receive", error)
                return
            with self._lock:
                if payload is not None:
                    try:
                        self._link.handle_datagram(payload, source, self._socket.sendto)
                    except (OSError, RuntimeError, ValueError) as error:
                        self._link.record_error("packet rejected", error)
                if time.monotonic() >= maintenance_after:
                    try:
                        self._link.tick(self._socket.sendto)
                        if maintenance_failures:
                            print("SIMULATED CCP maintenance recovered", file=sys.stderr, flush=True)
                        maintenance_failures = 0
                    except (OSError, RuntimeError, ValueError) as error:
                        self._link.record_error("maintenance", error)
                        delay = (1.0, 3.0, 10.0, 30.0)[min(maintenance_failures, 3)]
                        maintenance_after = time.monotonic() + delay
                        maintenance_failures += 1

    # self: Simulator view reading one synchronized, secret-free state and display-only code.
    def observe(self) -> tuple[SimulatorSnapshot, str]:
        with self._lock:
            if self._link is None:
                return SimulatorSnapshot(), ""
            snapshot = self._link.snapshot()
            if self._fatal_error is not None:
                snapshot = SimulatorSnapshot(**{**snapshot.__dict__, "state": LinkState.STOPPED,
                    "activity_valid": False, "last_error": f"Endpoint stopped: {self._fatal_error}"})
            return snapshot, self._link.display_code()

    # self: Socket lifetime owner accepting explicit simulator UI operations only.
    # action: Fixed action name; this is never a hook or chat-selection endpoint.
    # confirmed: Explicit replacement/forget confirmation, or synthetic network readiness.
    # Raises ValueError or the original state/storage failure; caller displays it visibly.
    def act(self, action: str, confirmed: bool = False) -> None:
        with self._lock:
            if self._link is None or self._fatal_error is not None:
                raise RuntimeError("SIMULATED CCP endpoint is unavailable")
            try:
                if action == "pair":
                    self._link.begin_pairing(confirmed)
                elif action == "cancel":
                    self._link.cancel_pairing()
                elif action == "forget":
                    self._link.forget_pairing(confirmed)
                elif action == "network":
                    self._link.set_network_ready(confirmed)
                else:
                    raise ValueError("Unsupported SIMULATED CCP action")
            except (OSError, RuntimeError, ValueError) as error:
                self._link.record_error("action", error)
                raise

    # self: Socket owner stopped before its Tk view and model are released.
    # Raises RuntimeError or OSError if worker/socket ownership cannot be retired.
    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=3.0)
            if self._thread.is_alive():
                raise RuntimeError("SIMULATED CCP worker did not stop before its deadline")
        if self._socket is not None:
            self._socket.close()
            self._socket = None
