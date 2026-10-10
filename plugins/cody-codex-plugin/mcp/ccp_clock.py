"""Authenticated, single-transmission clock preferences and first-connection setup."""

from __future__ import annotations

import hashlib
import hmac
import os
import re
import secrets
import sys
import threading
import time
from typing import Any


TIMEZONES = {"UTC": 0, "Europe/Rome": 1, "Asia/Ho_Chi_Minh": 2}
ZONE_NAMES = {value: name for name, value in TIMEZONES.items()}
COMMAND_NAMES = ("device", "pc", "nonce", "id", "op", "timezone", "enabled", "start", "end")
ACK_NAMES = ("device", "pc", "nonce", "id", "op", "status", "error", "initialized", "timezone", "enabled", "start", "end")
REQUEST_SECONDS = 4.0
MAX_REQUESTS = 16


# No parameters: reads the operating system's configured zone, never its transient UTC offset.
# Raises OSError or ValueError if the Windows zone is unreadable or unsupported by Cody.
def host_timezone() -> str:
    if os.name != "nt":
        raise ValueError("Automatic Cody timezone setup currently requires Windows; select a supported timezone explicitly")
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\TimeZoneInformation") as key:
        zone, zone_type = winreg.QueryValueEx(key, "TimeZoneKeyName")
        if zone_type != winreg.REG_SZ or not isinstance(zone, str):
            raise ValueError("Windows TimeZoneKeyName has an invalid type")
        try:
            disabled, disabled_type = winreg.QueryValueEx(key, "DynamicDaylightTimeDisabled")
        except FileNotFoundError:
            disabled, disabled_type = 0, winreg.REG_DWORD
        if disabled_type != winreg.REG_DWORD or disabled not in (0, 1):
            raise ValueError("Windows daylight-saving preference has an invalid type or value")
    mapping = {"UTC": "UTC", "W. Europe Standard Time": "Europe/Rome", "SE Asia Standard Time": "Asia/Ho_Chi_Minh"}
    selected = mapping.get(zone.rstrip("\0"))
    if selected is None:
        raise ValueError(f"Windows timezone {zone!r} is not supported by Cody; select UTC, Europe/Rome or Asia/Ho_Chi_Minh explicitly")
    if selected == "Europe/Rome" and disabled:
        raise ValueError("Windows disables daylight-saving time for Europe/Rome; Cody requires that zone's normal daylight-saving rules")
    return selected


# value: Explicit local wall-clock text for the selected device timezone.
# label: Non-secret field name used in rejection diagnostics.
# Raises ValueError for a value outside strict HH:MM with a valid hour and minute.
def minute_of_day(value: Any, label: str) -> int:
    if not isinstance(value, str) or re.fullmatch(r"(?:[01][0-9]|2[0-3]):[0-5][0-9]", value) is None:
        raise ValueError(f"{label} must be HH:MM between 00:00 and 23:59")
    hour, minute = value.split(":")
    return int(hour) * 60 + int(minute)


# arguments: Strict caller fields; omitted settings preserve the device's actual values.
# configure: Whether this is a deliberate user preference change rather than a read.
# Raises ValueError for unknown, empty, unsupported or inconsistent settings.
def command_settings(arguments: dict[str, Any], configure: bool) -> tuple[str, str, str, str]:
    allowed = {"device_id", "include_simulator"}
    if configure:
        allowed |= {"timezone", "quiet_hours_enabled", "quiet_hours_start", "quiet_hours_end"}
    if set(arguments) - allowed:
        raise ValueError("Clock arguments contain unexpected fields")
    if not configure:
        return ("-1",) * 4
    if not any(name in arguments for name in allowed - {"device_id", "include_simulator"}):
        raise ValueError("Clock configuration requires at least one explicit preference")
    zone = arguments.get("timezone")
    if zone is not None and (not isinstance(zone, str) or zone not in TIMEZONES):
        raise ValueError("timezone must be UTC, Europe/Rome or Asia/Ho_Chi_Minh")
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
    if "timezone" in arguments and zone is None:
        raise ValueError("timezone must not be null")
    return (str(TIMEZONES[zone]) if zone is not None else "-1",
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

    # fields: Exact signed CLOCKCAP fields emitted after authenticated READY.
    # source: Socket-observed endpoint matching the current active event peer.
    # Raises ValueError for stale, forged, unsupported or malformed capabilities.
    def accept_capability(self, fields: dict[str, str], source: tuple[str, int]) -> None:
        names = ("device", "pc", "nonce", "clock", "initialized")
        if set(fields) != {*names, "proof"} or fields["clock"] != "1" or fields["initialized"] not in {"0", "1"}:
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
            setup = self._setup.get(fields["device"])
            if setup is not None and setup["association"] == association:
                return
            if len(self._setup) >= 128 and fields["device"] not in self._setup:
                raise ValueError("Clock setup registry is full")
            self._setup[fields["device"]] = {"association": association, "status": "initialized" if fields["initialized"] == "1" else "pending"}
        if fields["initialized"] == "1":
            return
        if self._protocol.observation_source(source[0]) == "SIMULATED":
            self._record_setup(fields["device"], {"status": "not_sent", "error": "Automatic host timezone setup is restricted to physical Cody"})
            return
        try:
            zone = host_timezone()
            request = self._transmit(fields["device"], "init", (str(TIMEZONES[zone]), "-1", "-1", "-1"), False)
            self._record_setup(fields["device"], {"request_id": request["id"], "timezone": zone,
                **(request["result"] if request["result"] is not None else {"status": "sent"})})
        except (OSError, RuntimeError, ValueError) as error:
            self._record_setup(fields["device"], {"status": "not_sent", "error": str(error)})

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
                    if request["automatic"] and self._setup.get(identity, {}).get("association") == request["association"]:
                        self._setup[identity].update(request["result"])
                        print(f"CCP automatic clock setup ambiguous for {identity}: no CLOCKACK; not retried", file=sys.stderr, flush=True)
                del self._requests[identity]

    # device_id: Non-secret target identity whose first-connection result is published.
    # result: Actual outcome; a timeout or send failure never becomes applied.
    def _record_setup(self, device_id: str, result: dict[str, Any]) -> None:
        with self._lock:
            self._setup[device_id].update(result)
        if result["status"] in {"not_sent", "ambiguous", "rejected"}:
            print(f"CCP automatic clock setup {result['status']} for {device_id}: {result.get('error')}", file=sys.stderr, flush=True)

    # device_id: Explicit active association target.
    # operation: read, init or set; init cannot overwrite the device's durable adoption marker.
    # settings: Four already validated zone/quiet numeric fields, -1 preserves each field.
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
                       "association": hashlib.sha256(active).hexdigest(),
                       "automatic": operation == "init", "event": threading.Event(), "result": None}
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

    # arguments: Explicit device and optional quiet-hours/timezone preferences from MCP.
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
        unavailable = all(fields[name] == "-1" for name in ("initialized", "timezone", "enabled", "start", "end"))
        valid_preferences = (fields["initialized"] in {"0", "1"} and fields["timezone"] in {"0", "1", "2"}
                and fields["enabled"] in {"0", "1"}
                and all(re.fullmatch(r"[0-9]{1,4}", fields[name]) is not None and 0 <= int(fields[name]) < 1440 for name in ("start", "end")))
        if (re.fullmatch(r"[0-9a-f]{32}", fields["id"]) is None or fields["op"] not in {"read", "init", "set"}
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
                result.update(initialized=fields["initialized"] == "1", timezone=ZONE_NAMES[int(fields["timezone"])],
                              quiet_hours_enabled=fields["enabled"] == "1")
                for wire, public in (("start", "quiet_hours_start"), ("end", "quiet_hours_end")):
                    minutes = int(fields[wire])
                    result[public] = f"{minutes // 60:02d}:{minutes % 60:02d}"
            request["result"] = result
            request["event"].set()
            if request["automatic"]:
                self._setup[fields["device"]].update(result)
                if result["status"] == "rejected":
                    print(f"CCP automatic clock setup rejected for {fields['device']}: {result['error']}", file=sys.stderr, flush=True)
                del self._requests[fields["device"]]

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
    preferences = {"timezone": {"type": "string", "enum": list(TIMEZONES)},
                   "quiet_hours_enabled": {"type": "boolean"},
                   "quiet_hours_start": {"type": "string", "pattern": "^(?:[01][0-9]|2[0-3]):[0-5][0-9]$"},
                   "quiet_hours_end": {"type": "string", "pattern": "^(?:[01][0-9]|2[0-3]):[0-5][0-9]$"}}
    return [{"name": "ccp_read_clock", "title": "Read Cody clock and quiet hours",
             "description": "Read authenticated durable timezone and quiet hours from the selected paired Cody.",
             "inputSchema": {"type": "object", "properties": target, "required": ["device_id"], "additionalProperties": False},
             "annotations": {"readOnlyHint": True, "openWorldHint": False}},
            {"name": "ccp_configure_clock", "title": "Set Cody timezone and quiet hours",
             "description": "Apply explicit timezone or quiet-hours preferences through authenticated CCP. Omitted fields, sounds and volume are preserved. No automatic mutation retries.",
             "inputSchema": {"type": "object", "properties": {**target, **preferences}, "required": ["device_id"], "additionalProperties": False},
             "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": False, "openWorldHint": False}}]
