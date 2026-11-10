"""Transient authenticated animation commands through the existing CCP owner."""

from __future__ import annotations

import hmac
import re
import secrets
import threading
import time
from typing import Any


SCENES = frozenset({
    "idle", "listening_question", "thinking", "working", "reply_idea",
    "error_exclamation", "sleep", "waking", "idle_heart", "idle_sweat",
    "error_tension", "thinking_question", "listening_exclamation",
})
OPERATIONS = frozenset({"select", "play", "pause", "restart", "normal"})
COMMAND_FIELDS = ("device", "pc", "nonce", "id", "scene", "op")
ACK_FIELDS = (*COMMAND_FIELDS, "status", "error")


class AnimationRequests:
    # responder: Existing exclusive CCP socket and association owner.
    # protocol: CCP encoding, identity and liveness helpers.
    def __init__(self, responder: Any, protocol: Any) -> None:
        self._owner = responder
        self._protocol = protocol
        self._condition = threading.Condition(responder._event_lock)
        self._peers: dict[str, Any] = {}
        self._used: dict[str, str] = {}
        self._requests: dict[str, dict[str, Any]] = {}

    # fields: Authenticated capability bound to a fresh READY.
    # source: Device's observed UDP endpoint.
    # Raises ValueError for invalid, stale or unauthenticated capability.
    def accept_capability(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        names = ("device", "pc", "nonce", "animation")
        if set(fields) != {*names, "proof"} or fields["animation"] != "1":
            raise ValueError("ANIMCAP fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if self._protocol.HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError("ANIMCAP identity or proof is malformed")
        with self._condition:
            peer = self._owner._event_peers.get(fields["device"])
            active = self._owner._state.retained_keys(fields["device"]).active
            if (peer is None or active is None or source != peer.source or fields["nonce"] != peer.nonce
                    or fields["pc"] != self._owner._state.pc_id or not hmac.compare_digest(active, peer.key)
                    or not 0 <= time.monotonic() - peer.observed_at <= self._protocol.LIVENESS_TIMEOUT_SECONDS
                    or not hmac.compare_digest(self._protocol.make_proof(peer.key, "anim-cap", *(fields[name] for name in names)), fields["proof"])):
                raise ValueError("ANIMCAP authentication failed")
            self._used = {device: nonce for device, nonce in self._used.items()
                          if device in self._owner._event_peers}
            self._peers = {device: current for device, current in self._peers.items()
                           if device in self._owner._event_peers}
            if self._used.get(fields["device"]) != fields["nonce"]:
                self._peers[fields["device"]] = peer
                self._condition.notify_all()

    # scene: Stable identifier for an animation in the firmware catalog.
    # operation: Transient select/play/pause/restart/normal action.
    # Raises ValueError or OSError for validation, missing target or transport failure.
    def request(self, scene: str, operation: str) -> dict[str, Any]:
        if not isinstance(scene, str) or scene not in SCENES:
            raise ValueError("Unknown animation scene")
        if not isinstance(operation, str) or operation not in OPERATIONS:
            raise ValueError("Unknown animation operation")
        deadline = time.monotonic() + 10.0
        with self._condition:
            while True:
                now = time.monotonic()
                eligible = []
                for device, peer in list(self._peers.items()):
                    current = self._owner._event_peers.get(device)
                    active = self._owner._state.retained_keys(device).active
                    if (current is None or current.nonce != peer.nonce or current.source != peer.source
                            or active is None or not hmac.compare_digest(active, peer.key)
                            or not 0 <= now - peer.observed_at <= self._protocol.LIVENESS_TIMEOUT_SECONDS):
                        self._peers.pop(device, None)
                        continue
                    if self._protocol.observation_source(peer.source[0]) != "SIMULATED":
                        eligible.append((device, peer))
                if len(eligible) > 1:
                    raise ValueError("Multiple physical Cody devices are ready; disconnect all but the intended target")
                if eligible:
                    device, peer = eligible[0]
                    if device in self._requests:
                        raise ValueError("An animation request is already pending for this Cody")
                    request = {"id": secrets.token_hex(16), "scene": scene, "op": operation,
                               "peer": peer, "deadline": now + 4.0,
                               "event": threading.Event(), "result": None}
                    self._requests[device] = request
                    self._used[device] = peer.nonce
                    self._peers.pop(device, None)
                    break
                remaining = deadline - now
                if remaining <= 0:
                    raise ValueError("No fresh animation capability from a paired physical Cody; keep it awake and connected")
                self._condition.wait(remaining)
        values = (device, self._owner._state.pc_id, peer.nonce, request["id"], scene, operation)
        try:
            packet = self._protocol.encode_datagram("ANIM", list(zip(COMMAND_FIELDS, values))
                      + [("proof", self._protocol.make_proof(peer.key, "anim", *values))])
            self._owner._socket.sendto(packet, peer.source)
            request["event"].wait(4.0)
        finally:
            with self._condition:
                if self._requests.get(device) is request:
                    del self._requests[device]
        result = request["result"]
        return {"device_id": device, "scene": scene, "op": operation,
                **(result if result is not None else {"status": "unknown", "error": "No authenticated acknowledgement"})}

    # fields: Complete authenticated acknowledgement of a single outstanding request.
    # source: Observed device endpoint.
    # Raises ValueError for malformed, stale or forged acknowledgement.
    def accept_ack(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        if set(fields) != {*ACK_FIELDS, "proof"}:
            raise ValueError("ANIMACK fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if self._protocol.HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError("ANIMACK identity or proof is malformed")
        if (re.fullmatch(r"[0-9a-f]{32}", fields["id"]) is None
                or fields["status"] not in {"applied", "rejected"}
                or re.fullmatch(r"-?[0-9]{1,10}", fields["error"]) is None
                or not -(2 ** 31) <= int(fields["error"]) < 2 ** 31
                or (fields["status"] == "applied") != (int(fields["error"]) == 0)):
            raise ValueError("ANIMACK result is malformed")
        with self._condition:
            request = self._requests.get(fields["device"])
            if (request is None or request["deadline"] <= time.monotonic() or request["result"] is not None
                    or fields["id"] != request["id"] or fields["scene"] != request["scene"]
                    or fields["op"] != request["op"] or fields["pc"] != self._owner._state.pc_id):
                raise ValueError("ANIMACK has no matching live request")
            peer = request["peer"]
            active = self._owner._state.retained_keys(fields["device"]).active
            if (source != peer.source or fields["nonce"] != peer.nonce or active is None
                    or not hmac.compare_digest(active, peer.key)
                    or not hmac.compare_digest(self._protocol.make_proof(peer.key, "anim-ack", *(fields[name] for name in ACK_FIELDS)), fields["proof"])):
                raise ValueError("ANIMACK authentication failed")
            request["result"] = {"status": fields["status"], "error": int(fields["error"])}
            request["event"].set()
