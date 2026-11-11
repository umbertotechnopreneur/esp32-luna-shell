"""Authenticated, single-transmission clock preferences and first-connection setup."""

from __future__ import annotations

import hashlib
import hmac
from datetime import datetime, timezone
import re
import secrets
import sys
import threading
import time
from typing import Any


COMMAND_NAMES = ("device", "pc", "nonce", "id", "op", "offset", "enabled", "start", "end")
ACK_NAMES = ("device", "pc", "nonce", "id", "op", "status", "error", "initialized", "offset", "enabled", "start", "end")
REQUEST_SECONDS = 4.0
MAX_REQUESTS = 16
TIME_REQUEST_NAMES = ("device", "pc", "nonce", "id", "mode")
TIME_RESPONSE_NAMES = (*TIME_REQUEST_NAMES, "status", "epoch", "offset")
TIME_MIN_EPOCH = 1704067200  # 2024-01-01 UTC, the first trusted RTC calendar.
TIME_MAX_EPOCH = 4102444799  # 2099-12-31 23:59:59 UTC, the final supported calendar.
MAX_TIME_IDS_PER_NONCE = 16
MAX_TIME_PEERS = 128


# value: Explicit local wall-clock text under the selected UTC offset.
# label: Non-secret field name used in rejection diagnostics.
# Raises ValueError for a value outside strict HH:MM with a valid hour and minute.
def minute_of_day(value: Any, label: str) -> int:
    if not isinstance(value, str) or re.fullmatch(r"(?:[01][0-9]|2[0-3]):[0-5][0-9]", value) is None:
        raise ValueError(f"{label} must be HH:MM between 00:00 and 23:59")
    hour, minute = value.split(":")
    return int(hour) * 60 + int(minute)


# value: Canonical signed UTC offset from an authenticated peer.
# Raises ValueError for a missing sign, negative zero, leading zeros or an out-of-range offset.
def parse_offset(value: str) -> int:
    if re.fullmatch(r"(?:\+0|[+-][1-9][0-9]{0,2})", value) is None or not -840 <= int(value) <= 840:
        raise ValueError("UTC offset is not a canonical signed minute value")
    return int(value)


# arguments: Strict caller fields; omitted settings preserve the device's actual values.
# configure: Whether this is a deliberate user preference change rather than a read.
# Raises ValueError for unknown, empty, unsupported or inconsistent settings.
def command_settings(arguments: dict[str, Any], configure: bool) -> tuple[str, str, str, str]:
    allowed = {"device_id", "include_simulator"}
    if configure:
        allowed |= {"utc_offset_minutes", "quiet_hours_enabled", "quiet_hours_start", "quiet_hours_end"}
    if set(arguments) - allowed:
        raise ValueError("Clock arguments contain unexpected fields")
    if not configure:
        return ("keep", "-1", "-1", "-1")
    if not any(name in arguments for name in allowed - {"device_id", "include_simulator"}):
        raise ValueError("Clock configuration requires at least one explicit preference")
    offset = arguments.get("utc_offset_minutes")
    if "utc_offset_minutes" in arguments and (type(offset) is not int or not -840 <= offset <= 840):
        raise ValueError("utc_offset_minutes must be an integer from -840 to 840")
    enabled = arguments.get("quiet_hours_enabled")
    if "quiet_hours_enabled" in arguments and type(enabled) is not bool:
        raise ValueError("quiet_hours_enabled must be a boolean")
    has_start, has_end = "quiet_hours_start" in arguments, "quiet_hours_end" in arguments
    if has_start != has_end:
        raise ValueError("Set quiet_hours_start and quiet_hours_end together")
    start = minute_of_day(arguments["quiet_hours_start"], "quiet_hours_start") if has_start else -1
    end = minute_of_day(arguments["quiet_hours_end"], "quiet_hours_end") if has_end else -1
    if has_start and start == end:
        raise ValueError("Quiet-hours start and end must differ")
    return (f"{offset:+d}" if offset is not None else "keep",
            str(int(enabled)) if enabled is not None else "-1", str(start), str(end))


class ClockRequests:
    # responder: Existing socket/state owner; secrets remain private to this broker.
    # protocol: Native CCP encode/proof/identity helpers, injected to avoid a circular import.
    def __init__(self, responder: Any, protocol: Any) -> None:
        self._owner = responder
        self._protocol = protocol
        self._lock = threading.Lock()
        self._peers: dict[str, Any] = {}
        self._used_nonce: dict[str, str] = {}
        self._requests: dict[str, dict[str, Any]] = {}
        self._setup: dict[str, dict[str, Any]] = {}
        self._time_ids: dict[str, tuple[str, set[str]]] = {}

    # fields: Exact authenticated device-initiated time request.
    # source: Socket-observed endpoint of the active, fresh committed peer.
    # Raises ValueError for malformed, stale, forged, simulated or replayed requests;
    # OSError preserves a response transmission failure and is never retried.
    def accept_time_request(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        if (set(fields) != {*TIME_REQUEST_NAMES, "proof"} or fields["mode"] not in {"auto", "manual"}
                or re.fullmatch(r"[0-9a-f]{32}", fields["id"]) is None):
            raise ValueError("TIMEREQ fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if self._protocol.HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError("TIMEREQ identity/proof is malformed")
        if self._protocol.observation_source(source[0]) != "LAN":
            raise ValueError("PC time synchronization requires a physical Cody")
        with self._owner._event_lock:
            peer = self._owner._event_peers.get(fields["device"])
            active_identities = set(self._owner._event_peers)
        active = self._owner._state.retained_keys(fields["device"]).active
        now = time.monotonic()
        request_values = [fields[name] for name in TIME_REQUEST_NAMES]
        if (peer is None or active is None or source != peer.source or fields["nonce"] != peer.nonce
                or fields["pc"] != self._owner._state.pc_id or not hmac.compare_digest(active, peer.key)
                or not 0 <= now - peer.observed_at <= self._protocol.LIVENESS_TIMEOUT_SECONDS
                or not hmac.compare_digest(self._protocol.make_proof(peer.key, "time-req", *request_values), fields["proof"])):
            raise ValueError("TIMEREQ authentication failed")
        with self._lock:
            self._time_ids = {identity: entry for identity, entry in self._time_ids.items() if identity in active_identities}
            previous = self._time_ids.get(fields["device"])
            if previous is None or previous[0] != peer.nonce:
                if previous is None and len(self._time_ids) >= MAX_TIME_PEERS:
                    raise ValueError("Time request registry is full")
                previous = (peer.nonce, set())
                self._time_ids[fields["device"]] = previous
            used = previous[1]
            if fields["id"] in used or len(used) >= MAX_TIME_IDS_PER_NONCE:
                raise ValueError("TIMEREQ identity was replayed or nonce budget exhausted")
            # Consume before reading the clock or sending; ambiguous sends are not replayed.
            used.add(fields["id"])
        status, epoch, offset = "ok", "0", "na"
        try:
            observed_epoch = int(time.time())
            if not TIME_MIN_EPOCH <= observed_epoch <= TIME_MAX_EPOCH:
                raise ValueError("Host clock is outside Cody's trusted RTC calendar")
            # Resolve the system's actual UTC offset at this same UTC instant;
            # the wire carries minutes, not a timezone name or guessed DST rule.
            local_offset = datetime.fromtimestamp(observed_epoch, timezone.utc).astimezone().utcoffset()
            if local_offset is None or local_offset.microseconds != 0:
                raise ValueError("Host UTC offset is unavailable or not minute-aligned")
            total_seconds = local_offset.days * 86400 + local_offset.seconds
            if total_seconds % 60 != 0 or not -840 * 60 <= total_seconds <= 840 * 60:
                raise ValueError("Host UTC offset is outside Cody's supported whole-minute range")
            epoch = str(observed_epoch)
            offset = f"{total_seconds // 60:+d}"
        except (OSError, OverflowError, ValueError):
            status, epoch, offset = "unavailable", "0", "na"
        values = [*request_values, status, epoch, offset]
        packet = self._protocol.encode_datagram("TIME", list(zip(TIME_RESPONSE_NAMES, values))
            + [("proof", self._protocol.make_proof(peer.key, "time", *values))])
        self._owner._socket.sendto(packet, source)

    # fields: Exact signed CLOCKCAP fields emitted after authenticated READY.
    # source: Socket-observed endpoint matching the current active event peer.
    # Raises ValueError for stale, forged, unsupported or malformed capabilities.
    def accept_capability(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        names = ("device", "pc", "nonce", "clock", "initialized")
        if set(fields) != {*names, "proof"} or fields["clock"] != "2" or fields["initialized"] not in {"0", "1"}:
            raise ValueError("CLOCKCAP fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if self._protocol.HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError("CLOCKCAP identity/proof is malformed")
        with self._owner._event_lock:
            peer = self._owner._event_peers.get(fields["device"])
        active = self._owner._state.retained_keys(fields["device"]).active
        if (peer is None or active is None or source != peer.source or fields["nonce"] != peer.nonce
                or fields["pc"] != self._owner._state.pc_id or not hmac.compare_digest(active, peer.key)
                or not 0 <= time.monotonic() - peer.observed_at <= self._protocol.LIVENESS_TIMEOUT_SECONDS
                or not hmac.compare_digest(self._protocol.make_proof(peer.key, "clock-cap", *(fields[name] for name in names)), fields["proof"])):
            raise ValueError("CLOCKCAP authentication failed")
        association = hashlib.sha256(active).hexdigest()
        with self._lock:
            self._prune_locked()
            if self._used_nonce.get(fields["device"]) != fields["nonce"]:
                self._peers[fields["device"]] = peer
            if len(self._setup) >= 128 and fields["device"] not in self._setup:
                raise ValueError("Clock setup registry is full")
            self._setup[fields["device"]] = {"association": association, "status": "offset_supported"}

    # No parameters: called while holding the private registry lock to expire bounded leases.
    def _prune_locked(self) -> None:
        now = time.monotonic()
        self._peers = {identity: peer for identity, peer in self._peers.items()
                       if 0 <= now - peer.observed_at <= self._protocol.LIVENESS_TIMEOUT_SECONDS}
        for identity, request in list(self._requests.items()):
            if request["deadline"] <= now:
                if request["result"] is None:
                    request["result"] = {"status": "ambiguous", "error": "No authenticated CLOCKACK before the deadline"}
                    request["event"].set()
                del self._requests[identity]

    # device_id: Explicit active association target.
    # operation: Explicit read or set for the admitted clock preferences.
    # settings: Validated signed offset or keep, followed by quiet-hour fields where -1 preserves.
    # include_simulator: Whether the user explicitly chose a SIMULATED target.
    # Raises ValueError for missing capability, retired pairing or an occupied bounded registry.
    def _transmit(self, device_id: str, operation: str, settings: tuple[str, str, str, str], include_simulator: bool) -> dict[str, Any]:
        with self._lock:
            self._prune_locked()
            peer = self._peers.get(device_id)
            with self._owner._event_lock:
                current = self._owner._event_peers.get(device_id)
                active_identities = set(self._owner._event_peers)
            active = self._owner._state.retained_keys(device_id).active
            if (peer is None or current is None or current.nonce != peer.nonce or active is None
                    or not hmac.compare_digest(active, peer.key)
                    or not 0 <= time.monotonic() - peer.observed_at <= self._protocol.LIVENESS_TIMEOUT_SECONDS):
                raise ValueError("No fresh authenticated CLOCKCAP; wake Cody and wait for its next connection heartbeat")
            source_kind = self._protocol.observation_source(peer.source[0])
            if source_kind == "SIMULATED" and not include_simulator:
                raise ValueError("SIMULATED clock settings require explicit include_simulator=true")
            if device_id in self._requests or len(self._requests) >= MAX_REQUESTS:
                raise ValueError("Clock request already pending or registry full")
            request = {"id": secrets.token_hex(16), "op": operation, "peer": peer,
                       "source": source_kind, "deadline": time.monotonic() + REQUEST_SECONDS,
                       "event": threading.Event(), "result": None}
            self._requests[device_id] = request
            self._used_nonce[device_id] = peer.nonce
            self._used_nonce = {identity: nonce for identity, nonce in self._used_nonce.items() if identity in active_identities}
            self._peers.pop(device_id)
        values = [device_id, self._owner._state.pc_id, peer.nonce, request["id"], operation, *settings]
        packet = self._protocol.encode_datagram("CLOCK", list(zip(COMMAND_NAMES, values))
            + [("proof", self._protocol.make_proof(peer.key, "clock", *values))])
        try:
            # Once handed to UDP, an error cannot prove the command did not reach Cody.
            self._owner._socket.sendto(packet, peer.source)
        except OSError as error:
            with self._lock:
                if request["result"] is None:
                    request["result"] = {"status": "ambiguous", "error": str(error)}
                    request["event"].set()
            print(f"CCP CLOCK {operation} send failed for {device_id}: {error}; not retried", file=sys.stderr, flush=True)
        return request

    # arguments: Explicit device and optional quiet-hours/UTC-offset preferences from MCP.
    # configure: True for a user-requested mutation; false reads durable settings.
    # Raises ValueError for invalid fields, absent capability or an inactive association.
    def request(self, arguments: dict[str, Any], configure: bool) -> dict[str, Any]:
        settings = command_settings(arguments, configure)
        identity = arguments.get("device_id")
        if not isinstance(identity, str) or self._protocol.HEX_PATTERNS["device"].fullmatch(identity) is None:
            raise ValueError("Clock settings require an explicit device_id")
        include_simulator = arguments.get("include_simulator", False)
        if type(include_simulator) is not bool:
            raise ValueError("include_simulator must be a boolean")
        request = self._transmit(identity, "set" if configure else "read", settings, include_simulator)
        request["event"].wait(REQUEST_SECONDS)
        with self._lock:
            self._prune_locked()
            result = request["result"]
            if self._requests.get(identity) is request:
                del self._requests[identity]
        return {"device_id": identity, "request_id": request["id"], "source": request["source"],
                **(result if result is not None else {"status": "ambiguous", "error": "No authenticated CLOCKACK"})}

    # fields: Exact authenticated durable CLOCKACK state and original esp_err_t.
    # source: Must match the outstanding command's socket-observed endpoint.
    # Raises ValueError for a malformed, forged, stale, replayed or mismatched ACK.
    def accept_ack(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        if set(fields) != {*ACK_NAMES, "proof"}:
            raise ValueError("CLOCKACK fields are malformed")
        for name in ("device", "pc", "nonce", "proof"):
            if self._protocol.HEX_PATTERNS[name].fullmatch(fields[name]) is None:
                raise ValueError("CLOCKACK identity/proof is malformed")
        unavailable = fields["offset"] == "na" and all(fields[name] == "-1" for name in ("initialized", "enabled", "start", "end"))
        try:
            offset = parse_offset(fields["offset"])
        except ValueError:
            offset = None
        valid_preferences = (fields["initialized"] in {"0", "1"} and offset is not None
                and fields["enabled"] in {"0", "1"}
                and all(re.fullmatch(r"[0-9]{1,4}", fields[name]) is not None and 0 <= int(fields[name]) < 1440 for name in ("start", "end")))
        if (re.fullmatch(r"[0-9a-f]{32}", fields["id"]) is None or fields["op"] not in {"read", "set"}
                or fields["status"] not in {"applied", "rejected"}
                or re.fullmatch(r"-?[0-9]{1,10}", fields["error"]) is None
                or not -(2 ** 31) <= int(fields["error"]) < 2 ** 31
                or (fields["status"] == "applied") != (int(fields["error"]) == 0)
                or not (valid_preferences or (unavailable and fields["status"] == "rejected"))):
            raise ValueError("CLOCKACK settings/result are malformed")
        with self._lock:
            self._prune_locked()
            request = self._requests.get(fields["device"])
            if (request is None or request["result"] is not None or fields["id"] != request["id"]
                    or fields["op"] != request["op"] or fields["pc"] != self._owner._state.pc_id):
                raise ValueError("CLOCKACK has no matching live request")
            peer = request["peer"]
            active = self._owner._state.retained_keys(fields["device"]).active
            if (source != peer.source or fields["nonce"] != peer.nonce or active is None
                    or not hmac.compare_digest(active, peer.key)
                    or not hmac.compare_digest(self._protocol.make_proof(peer.key, "clock-ack", *(fields[name] for name in ACK_NAMES)), fields["proof"])):
                raise ValueError("CLOCKACK authentication failed")
            result = {"status": fields["status"], "error": int(fields["error"]), "preferences_available": not unavailable}
            if not unavailable:
                result.update(initialized=fields["initialized"] == "1", utc_offset_minutes=offset,
                              quiet_hours_enabled=fields["enabled"] == "1")
                for wire, public in (("start", "quiet_hours_start"), ("end", "quiet_hours_end")):
                    minutes = int(fields[wire])
                    result[public] = f"{minutes // 60:02d}:{minutes % 60:02d}"
            request["result"] = result
            request["event"].set()

    # No parameters: exposes non-secret first-connection outcomes without making a network request.
    def setup_status(self) -> dict[str, dict[str, Any]]:
        with self._lock:
            self._prune_locked()
            return {identity: {key: value for key, value in result.items() if key != "association"}
                    for identity, result in self._setup.items()}


# No parameters: defines chat-discoverable bounded preferences tools, separate from destructive reset.
def tool_catalog() -> list[dict[str, Any]]:
    target = {"device_id": {"type": "string", "pattern": "^[0-9a-f]{12}$"},
              "include_simulator": {"type": "boolean", "default": False}}
    preferences = {"utc_offset_minutes": {"type": "integer", "minimum": -840, "maximum": 840},
                   "quiet_hours_enabled": {"type": "boolean"},
                   "quiet_hours_start": {"type": "string", "pattern": "^(?:[01][0-9]|2[0-3]):[0-5][0-9]$"},
                   "quiet_hours_end": {"type": "string", "pattern": "^(?:[01][0-9]|2[0-3]):[0-5][0-9]$"}}
    return [{"name": "ccp_read_clock", "title": "Read Cody clock and quiet hours",
             "description": "Read the authenticated UTC offset and quiet hours from the selected paired Cody.",
             "inputSchema": {"type": "object", "properties": target, "required": ["device_id"], "additionalProperties": False},
             "annotations": {"readOnlyHint": True, "openWorldHint": False}},
            {"name": "ccp_configure_clock", "title": "Set Cody UTC offset and quiet hours",
             "description": "Apply an explicit UTC offset in minutes or quiet-hours preferences through authenticated CCP. Omitted fields, sounds and volume are preserved. No automatic mutation retries.",
             "inputSchema": {"type": "object", "properties": {**target, **preferences}, "required": ["device_id"], "additionalProperties": False},
             "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": False, "openWorldHint": False}}]
