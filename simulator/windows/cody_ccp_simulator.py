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
import re
from pathlib import Path
import secrets
import socket
import sys
import threading
import time
from typing import Callable

from runtime_paths import simulator_artifacts_root
from cody_native_ccp import NativeTransaction, checked as checked_transaction

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))
import ccp_server as protocol  # noqa: E402
import ccp_interactions as interactions  # noqa: E402
import ccp_reset as reset  # noqa: E402

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
    request_id: str = ""
    event_state: str = "none"
    event_payload: interactions.EventPayload | None = None
    event_fresh: bool = False
    event_remaining_seconds: int = 0
    event_error: int = 0
    reset_count: int = 0


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
                             "device_id": "02" + secrets.token_hex(5), "active": None, "pending": None,
                             "network_configured": True}
            self._write(self.document)
            print("SIMULATED CCP identity absent: created isolated first-use state", file=sys.stderr, flush=True)
            return
        document = json.loads(raw, object_pairs_hook=protocol.unique_state_object)
        self._validate(document)
        if "network_configured" not in document:
            # This key did not exist before simulated factory reset. Default
            # only its genuine absence, after all existing fields validate.
            document["network_configured"] = True
            self._write(document)
            print("SIMULATED network_configured absent: persisted initial configured LAN", file=sys.stderr, flush=True)
        self.document = document

    # self: Simulator state owner; validation does not repair any field.
    # document: Exact decoded persisted document.
    # Raises ValueError or RuntimeError for invalid state or failed DPAPI authentication.
    def _validate(self, document: object) -> None:
        required = {"version", "source", "device_id", "active", "pending"}
        if not isinstance(document, dict) or not required <= set(document) or set(document) - required - {"network_configured"}:
            raise ValueError("SIMULATED CCP state fields are invalid")
        if "network_configured" in document and type(document["network_configured"]) is not bool:
            raise ValueError("SIMULATED network_configured must be a persisted boolean")
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
    # network_configured: Explicit simulated network setup/reset, or None to preserve its existing value.
    # Raises OSError, RuntimeError or ValueError without replacing the prior memory state.
    def save(self, active: dict | None, pending: dict | None, network_configured: bool | None = None) -> None:
        document = deepcopy(self.document)
        document.update(active=deepcopy(active), pending=deepcopy(pending))
        if network_configured is not None:
            document["network_configured"] = network_configured
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
        self.network_ready = self.store.document["network_configured"]
        if not self.network_ready:
            self.state = LinkState.NETWORK_DOWN
        self._code = ""
        self._salt = ""
        self._session = ""
        self._verifier = b""
        self._deadline = 0.0
        self._nonce = ""
        self._ready_nonce = ""
        self._reply_nonce = ""
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
        self._transaction: NativeTransaction | None = None
        self._request_id = ""
        self._event_state = "none"
        self._event_payload: interactions.EventPayload | None = None
        self._event_at: float | None = None
        self._event_error = 0
        self._event_expires = 0.0
        self._event_nonce = ""
        self._event_peer: tuple[str, int] | None = None
        self._reply_choice = ""
        self._reply_after = 0.0
        self._reset_request: tuple[str, reset.ResetOperation, str] | None = None
        self._reset_due: float | None = None
        self._reset_attempted = False
        self._reset_error = ""
        self.reset_count = 0
        self._debug_request: dict | None = None
        self._debug_ready_nonce = ""
        self._debug_used_nonce = ""
        self.debug_capability = False

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

    # self: Endpoint owner serializing native state and its public projection.
    # Raises loader/native errors; there is no alternate Python transition engine.
    def _native_transaction(self) -> NativeTransaction:
        if self._transaction is None:
            self._transaction = NativeTransaction()
        return self._transaction

    # self: Owner copying a decoded native DTO; no transaction rules are evaluated here.
    # Raises the original native read or ABI error.
    def _sync_transaction(self) -> None:
        result = self._native_transaction().snapshot(interactions)
        self._request_id, self._event_payload, self._event_state = result.request_id, result.payload, result.state
        self._event_expires, self._event_error = result.expires, result.error
        # Retry eligibility is a projection of the engine's Sending state.
        self._reply_choice = result.payload.selected_id if result.payload is not None and result.state == "sending" else ""

    # self: Owner explicitly withdrawing transient state during commissioning/reset.
    # Raises the original native reset/read error if an event owner already exists.
    def _reset_transaction(self) -> None:
        if self._transaction is not None:
            self._transaction.reset()
            self._sync_transaction()
        else:
            self._request_id, self._event_state, self._event_payload = "", "none", None
        self._reply_choice, self._event_error = "", 0
        self._ready_nonce, self._reply_nonce = "", ""

    # self: Current owner; the result is consumed only by the visible simulator display.
    def display_code(self) -> str:
        return self._code if not self.pending and self.clock() < self._deadline else ""

    # self: Current simulator state without code, salt, verifier or protected values.
    def snapshot(self) -> SimulatorSnapshot:
        valid = self._activity_at is not None and self.clock() - self._activity_at < PRESENCE_SECONDS
        event_fresh = (self._event_at is not None and self.clock() - self._event_at < PRESENCE_SECONDS
                       and self.state == LinkState.CONNECTED and self.network_ready)
        return SimulatorSnapshot(device_id=self.device_id, state=self.state,
            retained_pairing=self.active is not None, pending_pairing=self.pending is not None,
            network_ready=self.network_ready, phase=self.phase, project=self.project, chat=self.chat,
            activity_valid=valid and self.state == LinkState.CONNECTED,
            accepted_packets=self.accepted_packets, rejected_packets=self.rejected_packets,
            last_error=self._reset_error or self.last_error, request_id=self._request_id, event_state=self._event_state,
            event_payload=self._event_payload, event_fresh=event_fresh,
            event_remaining_seconds=max(0, int(self._event_expires - self.clock())), event_error=self._event_error, reset_count=self.reset_count)

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
        self._event_at = None
        self._reset_transaction()
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
        self._ready_nonce, self._reply_nonce = "", ""
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
        self._reset_transaction()
        self._event_at, self._event_peer = None, None
        self._event_nonce, self._reply_choice = "", ""
        self.state = LinkState.UNPAIRED if self.network_ready else LinkState.NETWORK_DOWN
        self.last_error = ""

    # self: Synthetic network-policy owner; no physical radio is changed.
    # ready: Explicit simulator connectivity toggle.
    # Raises ValueError for a non-boolean policy, or the original store error while saving explicit setup.
    def set_network_ready(self, ready: bool) -> None:
        if type(ready) is not bool:
            raise ValueError("SIMULATED network state must be boolean")
        if ready and not self.store.document["network_configured"]:
            self.store.save(self.active, self.pending, network_configured=True)
        self.network_ready = ready
        self._next_find = 0.0
        if not ready:
            self._nonce = ""
            self._ready_nonce, self._reply_nonce = "", ""
            self._ready_at = self._activity_at = None
            self._event_at = None
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
        if self._reset_due is not None and now >= self._reset_due:
            self._apply_reset()
            return
        if self._transaction is not None:
            self._transaction.tick(now)
            self._sync_transaction()
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
        if (self._reply_choice and self._event_state == "sending"
                and self.state == LinkState.CONNECTED and now >= self._reply_after
                and self._event_peer is not None and self._ready_nonce):
            self._send_reply(send)
        if now < self._next_find:
            return
        self._next_find = now + FIND_SECONDS
        nonce = secrets.token_hex(16)
        key = protocol.unprotect_secret(record["protected_link_key"])
        pc = record["pc_id"]
        current_id, current_state = "0" * 32, "none"
        if self._event_payload is not None and self._event_payload.kind == interactions.EventKind.QUESTION:
            current_id, current_state = self._request_id, self._event_state
        fields = [("device", self.device_id), ("pc", pc), ("nonce", nonce), ("activity", "1"),
                  ("events", "2"), ("current", current_id), ("state", current_state)]
        proof = protocol.make_proof(key, "find-events-v2", self.device_id, pc, nonce, current_id, current_state)
        send(protocol.encode_datagram("FIND", fields + [("proof", proof)]), (LOOPBACK, protocol.PLUGIN_PORT))
        self._nonce = nonce
        # A retained Connected state does not authenticate this new FIND yet.
        self._ready_nonce = ""
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
        elif kind in ("READY", "ACTIVITY", "EVENT", "REPLYACK", "RESET", "FACTORYRESET", "DEBUG"):
            if source[1] != protocol.PLUGIN_PORT:
                raise ValueError("CCP presence sender port is invalid")
            if kind == "DEBUG":
                self._accept_debug(fields, source)
            elif kind == "EVENT":
                self._accept_event(fields, source)
                if fields["state"] not in {"none", "unavailable"}:
                    self._send_terminal_ack(send)
            elif kind == "REPLYACK":
                self._accept_reply_ack(fields)
                self._send_terminal_ack(send)
            elif kind in {"RESET", "FACTORYRESET"}:
                operation = reset.ResetOperation.RESTART if kind == "RESET" else reset.ResetOperation.FACTORY_RESET
                self._accept_reset(fields, source, send, operation)
            else:
                self._accept_presence(kind, fields, source, send)
        else:
            raise ValueError("Unsupported CCP datagram kind")
        self.accepted_packets += 1

    # self: Loopback protocol owner admitting only bounded transient Debug views.
    # fields: Exact DEBUG envelope; channel offsets are validated by the actual C++ UI.
    # source: Exact authenticated READY endpoint, never a caller-supplied destination.
    # Raises ValueError or RuntimeError for forged, expired, replayed or conflicting requests.
    def _accept_debug(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        names = ("device", "pc", "nonce", "id", "mode", "scroll")
        if set(fields) != {*names, "proof"} or not self.debug_capability:
            raise ValueError("SIMULATED debug capability unavailable or malformed fields")
        require_hex({name: fields[name] for name in ("device", "pc", "nonce", "proof")})
        if (interactions.IDENTITY.fullmatch(fields["id"]) is None
                or fields["mode"] not in {"face_channels", "character", "normal"}
                or re.fullmatch(r"0|[1-9][0-9]{0,2}", fields["scroll"]) is None
                or int(fields["scroll"]) > 255
                or (fields["mode"] != "face_channels" and fields["scroll"] != "0")):
            raise ValueError("SIMULATED Debug typed command rejected")
        if (self.active is None or self.pending or self.state != LinkState.CONNECTED
                or fields["device"] != self.device_id or fields["pc"] != self.active["pc_id"]
                or fields["nonce"] != self._debug_ready_nonce or fields["nonce"] == self._debug_used_nonce
                or self._ready_at is None or self.clock() - self._ready_at >= PRESENCE_SECONDS):
            raise ValueError("SIMULATED Debug command has no current active READY lease")
        key = protocol.unprotect_secret(self.active["protected_link_key"])
        if not hmac.compare_digest(protocol.make_proof(key, "debug", *(fields[name] for name in names)), fields["proof"]):
            raise ValueError("SIMULATED Debug command authentication failed")
        if self._debug_request is not None and self._debug_request["deadline"] > self.clock():
            raise ValueError("SIMULATED Debug request already pending")
        self._debug_used_nonce = fields["nonce"]
        self._debug_request = {"fields": fields.copy(), "source": source, "key": key,
                               "deadline": self.clock() + 4.0, "taken": False}

    # self: Protocol owner transferring a command once to the simulator's rendering thread.
    # Returns public bounded fields only; no keys/proofs leave the transport owner.
    def take_debug(self) -> dict | None:
        request = self._debug_request
        if request is None or request["taken"] or request["deadline"] <= self.clock():
            return None
        request["taken"] = True
        fields = request["fields"]
        return {"id": fields["id"], "mode": fields["mode"], "scroll": int(fields["scroll"])}

    # self: Transport owner acknowledging actual rendering-thread mode application.
    # request_id: Exact transient identity previously taken by that thread.
    # error: Original signed UI/native error; zero only after the mode is accepted.
    # count: Actual C++ channel catalog count.
    # send: Sole endpoint sender; ACK is attempted once, never auto-retried.
    # Raises ValueError, RuntimeError or OSError for retired leases or send failure.
    def complete_debug(self, request_id: str, error: int, count: int, send: Send) -> None:
        request = self._debug_request
        if (request is None or not request["taken"] or request["fields"]["id"] != request_id
                or request["deadline"] <= self.clock() or self.active is None or self.pending
                or self.state != LinkState.CONNECTED or self.active["pc_id"] != request["fields"]["pc"]
                or not hmac.compare_digest(protocol.unprotect_secret(self.active["protected_link_key"]), request["key"])):
            raise ValueError("SIMULATED Debug completion lease retired")
        if type(error) is not int or not -(2 ** 31) <= error < 2 ** 31 or not 1 <= count <= 255:
            raise ValueError("SIMULATED Debug result malformed")
        fields = request["fields"]
        values = [fields[name] for name in ("device", "pc", "nonce", "id", "mode", "scroll")]
        values += ["applied" if error == 0 else "rejected", str(error), str(count)]
        self._debug_request = None
        if error:
            self.record_error("optional Debug view", RuntimeError(f"esp_err_t={error}"))
        packet = protocol.encode_datagram("DEBUGACK", list(zip(
            ("device", "pc", "nonce", "id", "mode", "scroll", "status", "error", "count"), values))
            + [("proof", protocol.make_proof(request["key"], "debug-ack", *values))])
        send(packet, request["source"])

    # self: Simulated reset admission owner; no physical PC or device settings are touched.
    # fields: Exact reset command fields authenticated by the currently active pairing.
    # source: Current loopback peer endpoint already checked at the socket boundary.
    # send: Sole sender; ACK is attempted immediately after admission, before delayed execution.
    # operation: Explicit restart or factory reset, bound to a distinct HMAC domain.
    # Raises ValueError, RuntimeError or OSError for malformed/forged/conflicting requests or ACK failure.
    def _accept_reset(self, fields: dict[str, str], source: tuple[str, int], send: Send,
                      operation: reset.ResetOperation) -> None:
        if set(fields) != {"device", "pc", "nonce", "id", "code", "proof"}:
            raise ValueError("Malformed CCP reset command")
        reset.validate_confirmation(fields["code"])
        if self.pending is not None or self._code:
            raise ValueError("Reset is unavailable during incomplete commissioning")
        key = self._event_key(fields, self._ready_nonce)
        if fields["id"] == "0" * 32:
            raise ValueError("A reset command requires a nonzero request identity")
        names = ("device", "pc", "nonce", "id", "code")
        command, acknowledgement, domain, ack_domain = reset.COMMANDS[operation]
        if not hmac.compare_digest(protocol.make_proof(key, domain, *(fields[name] for name in names)), fields["proof"]):
            raise ValueError(f"CCP {command} authentication failed")
        identity = (fields["id"], operation, fields["nonce"])
        if self._reset_request is not None and self._reset_request != identity:
            raise ValueError("A different reset request was already admitted")
        self._reset_request = identity
        if self._reset_due is None and not self._reset_attempted:
            # Match firmware admission: even an ACK send failure can leave a
            # queued operation. The caller must treat that result as ambiguous.
            self._reset_due = self.clock() + 0.5
        values = [self.device_id, fields["pc"], fields["nonce"], fields["id"], "accepted"]
        send(protocol.encode_datagram(acknowledgement,
             list(zip(("device", "pc", "nonce", "id", "status"), values))
             + [("proof", protocol.make_proof(key, ack_domain, *values))]), source)

    # self: Simulator-only delayed executor; failed storage leaves the original model and error observable.
    # Raises the original storage failure without rebooting or retrying the factory reset automatically.
    def _apply_reset(self) -> None:
        self._reset_due, self._reset_attempted = None, True
        operation = self._reset_request[1]
        if operation == reset.ResetOperation.FACTORY_RESET:
            try:
                self.store.save(None, None, network_configured=False)
            except (OSError, RuntimeError, ValueError) as error:
                self._reset_error = f"Factory reset failed: {type(error).__name__}: {error}"
                raise
        count = self.reset_count + 1
        self.__init__(self.store, self.clock, self.wall_clock)
        self.reset_count = count

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
            self._ready_nonce = fields["nonce"]
            values = [fields[name] for name in ("device", "pc", "nonce")]
            proof = protocol.make_proof(key, "committed", *values)
            # Resend even after prior activation: lost COMMITTED must not strand
            # the host candidate or require another commissioning code.
            send(protocol.encode_datagram("COMMITTED", list(zip(("device", "pc", "nonce"), values))
                                          + [("proof", proof)]), source)
            if self.debug_capability:
                self._debug_ready_nonce = fields["nonce"]
                values = [fields[name] for name in ("device", "pc", "nonce")] + ["1"]
                send(protocol.encode_datagram("DEBUGCAP", list(zip(("device", "pc", "nonce", "debug"), values))
                    + [("proof", protocol.make_proof(key, "debug-cap", *values))]), source)
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

    # self: Active link owner validating every interaction against its purpose-specific nonce and PC identity.
    # fields: Common authenticated EVENT or REPLYACK envelope.
    # expected_nonce: Accepted current READY for events, or last successfully transmitted REPLY for its ACK.
    # Returns the retained active key after identity, nonce and presence validation.
    # Raises ValueError or RuntimeError for stale/unconfirmed/malformed envelopes or storage failure.
    def _event_key(self, fields: dict[str, str], expected_nonce: str) -> bytes:
        require_hex({name: fields[name] for name in ("device", "pc", "nonce", "proof")})
        if interactions.IDENTITY.fullmatch(fields["id"]) is None:
            raise ValueError("CCP interaction ticket identity is invalid")
        if (self.active is None or self.pending or self.state != LinkState.CONNECTED
                or fields["device"] != self.device_id or fields["pc"] != self.active["pc_id"]
                or not expected_nonce or fields["nonce"] != expected_nonce):
            raise ValueError("CCP interaction is not bound to the current active FIND")
        return protocol.unprotect_secret(self.active["protected_link_key"])

    # self: Authenticated simulator event owner, separate from automatic activity.
    # fields: Exact immutable request snapshot; no partial content is published.
    # source: Fixed loopback PC endpoint retained only after complete authentication.
    # Raises ValueError or RuntimeError for malformed, forged, replayed or contradictory snapshots.
    def _accept_event(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        if set(fields) != {"device", "pc", "nonce", "id", "state", "ttl", "data", "proof"}:
            raise ValueError("Malformed CCP EVENT fields")
        key = self._event_key(fields, self._ready_nonce)
        names = ("device", "pc", "nonce", "id", "state", "ttl", "data")
        if not hmac.compare_digest(protocol.make_proof(key, "event", *(fields[name] for name in names)), fields["proof"]):
            raise ValueError("CCP EVENT authentication failed")
        if fields["nonce"] == self._event_nonce:
            raise ValueError("EVENT permits one snapshot per FIND nonce")
        # UDP authenticity and nonce replay remain transport-owner concerns.
        # Decoding, immutable content, deadlines and retirement execute firmware C++.
        now = self.clock()
        self._native_transaction().event(fields, now)
        self._sync_transaction()
        self._event_at, self._event_nonce = now, fields["nonce"]
        self._event_peer = source

    # self: SIMULATED endpoint reporting actual retirement, separate from recipient delivery.
    # send: Sole authenticated socket callback; a lost ACK is retried on the next terminal EVENT.
    # Returns without sending while the current FIND awaits its authenticated READY.
    # Raises ValueError, RuntimeError or OSError for missing route, crypto or send failure.
    def _send_terminal_ack(self, send: Send) -> None:
        if self._transaction is None:
            return
        observation = self._transaction.terminal()
        if observation is None:
            return
        if not self._ready_nonce:
            # A delayed REPLYACK can retire the question while a new FIND awaits
            # READY. Its next authenticated terminal EVENT retries this observation.
            return
        if self.active is None or self._event_peer is None:
            raise ValueError("No authenticated route for terminal observation")
        key = protocol.unprotect_secret(self.active["protected_link_key"])
        request_id, state = observation
        values = [self.device_id, self.active["pc_id"], self._ready_nonce, request_id, state]
        send(protocol.encode_datagram("EVENTACK", list(zip(("device", "pc", "nonce", "id", "state"), values))
                                      + [("proof", protocol.make_proof(key, "event-ack", *values))]), self._event_peer)

    # self: Explicit simulator confirmation target; choosing a card alone never calls this method.
    # choice_id: Stable ID selected locally and reviewed before Send.
    # send: Sole endpoint callback; socket success does not mean recipient delivery.
    # Raises ValueError, RuntimeError or OSError for stale/invalid state or failed send.
    def submit_reply(self, choice_id: str, send: Send) -> None:
        self._native_transaction().submit(self._request_id, choice_id, self.snapshot().event_fresh, self.clock())
        self._sync_transaction()
        self._send_reply(send)

    # self: Explicit simulator Done target; never closes an unanswered question.
    # request_id: Immutable informational/terminal identity currently displayed.
    # Raises ValueError with the real native dismissal admission error.
    def dismiss_event(self, request_id: str) -> None:
        self._native_transaction().dismiss(request_id)
        self._sync_transaction()

    # self: Endpoint retaining one explicitly confirmed answer across retries and temporary offline periods.
    # send: Sole socket callback; only a later authenticated delivered acknowledgement shows Reply sent.
    # Raises ValueError, RuntimeError or OSError for absent authentication, cryptography or transport failure.
    def _send_reply(self, send: Send) -> None:
        if self.active is None or not self._ready_nonce or self._event_peer is None:
            raise ValueError("No active authenticated CCP reply route")
        key = protocol.unprotect_secret(self.active["protected_link_key"])
        values = [self.device_id, self.active["pc_id"], self._ready_nonce, self._request_id, self._reply_choice]
        proof = protocol.make_proof(key, "reply", *values)
        send(protocol.encode_datagram("REPLY", list(zip(("device", "pc", "nonce", "id", "choice"), values))
                                      + [("proof", proof)]), self._event_peer)
        # Failed sends retain the prior ACK admission nonce rather than admitting
        # an acknowledgement for a packet that this owner never transmitted.
        self._reply_nonce = self._ready_nonce
        self._reply_after = self.clock() + 1.0

    # self: Request owner whose received and delivered states have distinct UI meanings.
    # fields: Exact nonce-bound response acknowledgement from the active PC.
    # Raises ValueError or RuntimeError for malformed, unrelated or unauthenticated acknowledgements.
    def _accept_reply_ack(self, fields: dict[str, str]) -> None:
        if set(fields) != {"device", "pc", "nonce", "id", "choice", "status", "proof"}:
            raise ValueError("Malformed CCP REPLYACK fields")
        key = self._event_key(fields, self._reply_nonce)
        names = ("device", "pc", "nonce", "id", "choice", "status")
        if not hmac.compare_digest(protocol.make_proof(key, "reply-ack", *(fields[name] for name in names)), fields["proof"]):
            raise ValueError("CCP REPLYACK authentication failed")
        error = self._native_transaction().ack(fields, self.clock())
        self._sync_transaction()
        # Recipient errors can retire the native request. Publish its real state
        # before reporting that error; a subsequent EVENT retries terminal ACK.
        checked_transaction("REPLYACK", error)


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
                payload, source = self._socket.recvfrom(interactions.MAX_EVENT_DATAGRAM_BYTES + 1)
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

    # self: Actual loopback socket owner; this address is a PC fact, never a firmware snapshot.
    # Returns the real bound IPv4 address, or unavailable while stopped.
    # Raises OSError for an actual socket-query failure.
    def bound_address(self) -> str | None:
        with self._lock:
            return self._socket.getsockname()[0] if self._socket is not None else None

    # self: Simulator protocol owner enabling Debug only after its native UI exists.
    # enabled: Actual rendering availability; never a claim about physical firmware.
    def enable_debug(self, enabled: bool) -> None:
        with self._lock:
            if self._link is not None:
                self._link.debug_capability = enabled

    # self: Endpoint sharing a bounded request with the actual Tk rendering thread.
    def take_debug(self) -> dict | None:
        with self._lock:
            return self._link.take_debug() if self._link is not None else None

    # self: Endpoint preserving actual UI errors and authenticating its accepted view ACK.
    # request_id: Exact transient request identity.
    # error: Original signed C++ or UI error.
    # count: Actual native compiled catalog count.
    # Raises ValueError, RuntimeError or OSError for retired state or transport failure.
    def complete_debug(self, request_id: str, error: int, count: int) -> None:
        with self._lock:
            if self._link is None or self._socket is None or self._fatal_error is not None:
                raise RuntimeError("SIMULATED CCP endpoint unavailable")
            try:
                self._link.complete_debug(request_id, error, count, self._socket.sendto)
            except (OSError, RuntimeError, ValueError) as failure:
                self._link.record_error("Debug acknowledgement", failure)
                raise

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

    # self: Endpoint forwarding an explicit Done action under the transport owner lock.
    # request_id: Exact informational/terminal identity reviewed by the user.
    # Raises ValueError or the original native error, retained for the visible simulator UI.
    def dismiss_event(self, request_id: str) -> None:
        with self._lock:
            if self._link is None or self._fatal_error is not None:
                raise RuntimeError("SIMULATED CCP endpoint is unavailable")
            try:
                self._link.dismiss_event(request_id)
            except (OSError, RuntimeError, ValueError) as error:
                self._link.record_error("dismiss", error)
                raise

    # self: Simulator endpoint accepting only the user's explicit reviewed Send action.
    # choice_id: Stable semantic ID from the currently visible authenticated request.
    # Raises ValueError or the original transport/storage error, retained for the simulator UI.
    def reply(self, choice_id: str) -> None:
        with self._lock:
            if self._link is None or self._fatal_error is not None or self._socket is None:
                raise RuntimeError("SIMULATED CCP endpoint is unavailable")
            try:
                self._link.submit_reply(choice_id, self._socket.sendto)
            except (OSError, RuntimeError, ValueError) as error:
                self._link.record_error("reply", error)
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
