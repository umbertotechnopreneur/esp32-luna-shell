"""Explicit preferred-name settings through bounded authenticated CCP commands."""

from __future__ import annotations

import hmac
import re
import secrets
import sys
import threading
import time
from typing import Any


MAX_NAME_BYTES = 24
COMMAND_NAMES = ("device", "pc", "nonce", "id", "op", "name")
ACK_NAMES = ("device", "pc", "nonce", "id", "op", "status", "error", "available", "name")


# value: Exact user-provided name; an empty string is an explicit clear.
# Raises ValueError for oversized, non-ASCII, invisible or padded names; never normalizes input.
def validate_name(value: Any) -> str:
    if not isinstance(value, str) or len(value) > MAX_NAME_BYTES:
        raise ValueError("preferred_name must be a string with at most 24 printable ASCII characters")
    if any(not 32 <= ord(character) <= 126 for character in value) or (value and value != value.strip()):
        raise ValueError("preferred_name must be printable ASCII without leading or trailing spaces")
    return value


class NameRequests:
    # responder: Existing authenticated UDP and pairing owner.
    # protocol: Native identity/encoding/proof helpers; no credentials enter public results.
    def __init__(self, responder: Any, protocol: Any) -> None:
        self._owner = responder
        self._protocol = protocol
        self._lock = threading.Lock()
        self._peers: dict[str, Any] = {}
        self._used_nonce: dict[str, str] = {}
        self._requests: dict[str, dict[str, Any]] = {}

    # fields: Exact authenticated NAMECAP emitted only after the current READY.
    # source: Must match the current active event-capable device endpoint.
    # Raises ValueError for malformed, stale, forged or retired capability fields.
    def accept_capability(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        names = ("device", "pc", "nonce", "name")
        if set(fields) != {*names, "proof"} or fields["name"] != "1":
            raise ValueError("NAMECAP fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if self._protocol.HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError("NAMECAP identity/proof is malformed")
        with self._owner._event_lock:
            peer = self._owner._event_peers.get(fields["device"])
        active = self._owner._state.retained_keys(fields["device"]).active
        if (peer is None or active is None or source != peer.source or peer.nonce != fields["nonce"]
                or fields["pc"] != self._owner._state.pc_id or not hmac.compare_digest(active, peer.key)
                or not 0 <= time.monotonic() - peer.observed_at <= self._protocol.LIVENESS_TIMEOUT_SECONDS
                or not hmac.compare_digest(self._protocol.make_proof(peer.key, "name-cap", *(fields[name] for name in names)), fields["proof"])):
            raise ValueError("NAMECAP authentication failed")
        with self._lock:
            now = time.monotonic()
            self._peers = {identity: value for identity, value in self._peers.items()
                           if 0 <= now - value.observed_at <= self._protocol.LIVENESS_TIMEOUT_SECONDS}
            if self._used_nonce.get(fields["device"]) != fields["nonce"]:
                if len(self._peers) >= 128 and fields["device"] not in self._peers:
                    raise ValueError("Preferred-name capability registry is full")
                self._peers[fields["device"]] = peer

    # arguments: Explicit selected target and user-provided preferred_name for a write.
    # configure: False reads; true writes exactly the supplied name or empty explicit clear.
    # Raises ValueError for invalid arguments, missing capability or a retired association.
    def request(self, arguments: dict[str, Any], configure: bool) -> dict[str, Any]:
        allowed = {"device_id", "include_simulator"} | ({"preferred_name"} if configure else set())
        if set(arguments) - allowed or (configure and "preferred_name" not in arguments):
            raise ValueError("Preferred-name arguments are missing required fields or contain unexpected fields")
        identity = arguments.get("device_id")
        if not isinstance(identity, str) or self._protocol.HEX_PATTERNS["device"].fullmatch(identity) is None:
            raise ValueError("Preferred-name settings require an explicit device_id")
        include_simulator = arguments.get("include_simulator", False)
        if type(include_simulator) is not bool:
            raise ValueError("include_simulator must be a boolean")
        value = validate_name(arguments["preferred_name"]) if configure else ""
        encoded = value.encode("ascii").hex() if value else "-"
        with self._lock:
            now = time.monotonic()
            self._requests = {target: request for target, request in self._requests.items() if request["deadline"] > now}
            peer = self._peers.get(identity)
            with self._owner._event_lock:
                current = self._owner._event_peers.get(identity)
                active_identities = set(self._owner._event_peers)
            active = self._owner._state.retained_keys(identity).active
            if (peer is None or current is None or current.nonce != peer.nonce or active is None
                    or not hmac.compare_digest(active, peer.key)
                    or not 0 <= now - peer.observed_at <= self._protocol.LIVENESS_TIMEOUT_SECONDS):
                raise ValueError("No fresh authenticated NAMECAP; wake Cody and wait for its next heartbeat")
            source_kind = self._protocol.observation_source(peer.source[0])
            if source_kind == "SIMULATED" and not include_simulator:
                raise ValueError("SIMULATED name settings require explicit include_simulator=true")
            if identity in self._requests or len(self._requests) >= 16:
                raise ValueError("Preferred-name request is pending or its registry is full")
            request = {"id": secrets.token_hex(16), "op": "set" if configure else "read", "peer": peer,
                       "deadline": now + 4.0, "event": threading.Event(), "result": None}
            self._requests[identity] = request
            self._used_nonce[identity] = peer.nonce
            self._used_nonce = {target: nonce for target, nonce in self._used_nonce.items() if target in active_identities}
            self._peers.pop(identity)
        values = [identity, self._owner._state.pc_id, peer.nonce, request["id"], request["op"], encoded]
        packet = self._protocol.encode_datagram("NAME", list(zip(COMMAND_NAMES, values))
            + [("proof", self._protocol.make_proof(peer.key, "name", *values))])
        try:
            self._owner._socket.sendto(packet, peer.source)
        except OSError as error:
            with self._lock:
                if request["result"] is None:
                    request["result"] = {"status": "ambiguous", "error": str(error)}
                    request["event"].set()
            print(f"CCP preferred-name send failed for {identity}: {error}; not retried", file=sys.stderr, flush=True)
        request["event"].wait(4.0)
        with self._lock:
            result = request["result"]
            if self._requests.get(identity) is request:
                del self._requests[identity]
        return {"device_id": identity, "request_id": request["id"], "source": source_kind,
                **(result if result is not None else {"status": "ambiguous", "error": "No authenticated NAMEACK before the deadline"})}

    # fields: Exact signed result, actual error and readback-confirmed name availability.
    # source: Must match the outstanding command's observed endpoint.
    # Raises ValueError for malformed, forged, stale, replayed or unrelated acknowledgements.
    def accept_ack(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        if set(fields) != {*ACK_NAMES, "proof"}:
            raise ValueError("NAMEACK fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if self._protocol.HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError("NAMEACK identity/proof is malformed")
        if (re.fullmatch(r"[0-9a-f]{32}", fields["id"]) is None or fields["op"] not in {"read", "set"}
                or fields["status"] not in {"applied", "rejected"} or fields["available"] not in {"0", "1"}
                or re.fullmatch(r"-?[0-9]{1,10}", fields["error"]) is None
                or not -(2 ** 31) <= int(fields["error"]) < 2 ** 31
                or (fields["status"] == "applied") != (int(fields["error"]) == 0)
                or (fields["status"] == "applied" and fields["available"] != "1")
                or (fields["available"] == "0" and fields["name"] != "-")):
            raise ValueError("NAMEACK result is malformed")
        name = ""
        if fields["name"] != "-":
            if re.fullmatch(r"(?:[0-9a-f]{2}){1,24}", fields["name"]) is None:
                raise ValueError("NAMEACK preferred-name encoding is malformed")
            try:
                name = bytes.fromhex(fields["name"]).decode("ascii")
            except UnicodeDecodeError as error:
                raise ValueError("NAMEACK preferred name is not ASCII") from error
            validate_name(name)
        with self._lock:
            request = self._requests.get(fields["device"])
            if (request is None or request["result"] is not None or request["deadline"] <= time.monotonic()
                    or request["id"] != fields["id"] or request["op"] != fields["op"]
                    or fields["pc"] != self._owner._state.pc_id):
                raise ValueError("NAMEACK has no matching live request")
            peer = request["peer"]
            active = self._owner._state.retained_keys(fields["device"]).active
            if (source != peer.source or fields["nonce"] != peer.nonce or active is None
                    or not hmac.compare_digest(active, peer.key)
                    or not hmac.compare_digest(self._protocol.make_proof(peer.key, "name-ack", *(fields[name] for name in ACK_NAMES)), fields["proof"])):
                raise ValueError("NAMEACK authentication failed")
            request["result"] = {"status": fields["status"], "error": int(fields["error"]),
                                 "name_available": fields["available"] == "1"}
            if fields["available"] == "1":
                request["result"]["preferred_name"] = name
            request["event"].set()


# No parameters: exposes explicit profile reads/writes, separate from clock and destructive reset.
def tool_catalog() -> list[dict[str, Any]]:
    target = {"device_id": {"type": "string", "pattern": "^[0-9a-f]{12}$"},
              "include_simulator": {"type": "boolean", "default": False}}
    return [{"name": "ccp_read_name", "title": "Read Cody preferred name",
             "description": "Read the selected Cody's authenticated canonical preferred name and actual availability.",
             "inputSchema": {"type": "object", "properties": target, "required": ["device_id"], "additionalProperties": False},
             "annotations": {"readOnlyHint": True, "openWorldHint": False}},
            {"name": "ccp_set_name", "title": "Set Cody preferred name",
             "description": "Persist an explicitly user-provided preferred name (up to 24 printable ASCII characters). Empty string explicitly clears it. Preserve all other preferences; never guess the user's identity.",
             "inputSchema": {"type": "object", "properties": {**target, "preferred_name": {"type": "string", "maxLength": 24}},
                             "required": ["device_id", "preferred_name"], "additionalProperties": False},
             "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": False, "openWorldHint": False}}]
