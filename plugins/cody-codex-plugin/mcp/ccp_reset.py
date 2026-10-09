"""Bounded, single-transmission admission for distinct restart and factory-reset operations."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import hmac
import re
import secrets
import sys
import threading
import time
from typing import Any


CONFIRMATION_CODE = "043533477"
REQUEST_SECONDS = 5.0
RETENTION_SECONDS = 300.0
IDENTITY = re.compile(r"^[0-9a-f]{32}$")


class ResetState(str, Enum):
    QUEUED = "queued"
    SENT = "sent"
    ACCEPTED = "accepted"
    NOT_SENT = "not_sent"
    AMBIGUOUS = "ambiguous"


class ResetOperation(str, Enum):
    RESTART = "restart"
    FACTORY_RESET = "factory_reset"


COMMANDS = {
    ResetOperation.RESTART: ("RESET", "RESETACK", "reset", "reset-ack"),
    ResetOperation.FACTORY_RESET: ("FACTORYRESET", "FACTORYRESETACK", "factory-reset", "factory-reset-ack"),
}


@dataclass
class ResetRequest:
    request_id: str
    device_id: str
    source: str
    association_id: str
    expires_at: float
    operation: ResetOperation
    state: ResetState = ResetState.QUEUED
    nonce: str = ""
    endpoint: tuple[str, int] | None = None
    error: str = ""


# code: Explicit string supplied by the caller, including the leading zero.
# Raises ValueError when the anti-misunderstanding code is missing, guessed in another type or wrong.
def validate_confirmation(code: Any) -> None:
    if not isinstance(code, str) or code != CONFIRMATION_CODE:
        raise ValueError("Device reset requires the exact confirmation_code string from the current user message")


class ResetRequests:
    # self: Volatile owner of bounded reset-operation requests, separate from pairing and question state.
    # clock: Monotonic owner clock; tests may inject one without sending to a physical device.
    def __init__(self, clock=time.monotonic) -> None:
        self._clock = clock
        self._condition = threading.Condition()
        self._requests: dict[str, ResetRequest] = {}

    # self: Locked request owner; a timeout after transmission is deliberately ambiguous.
    def _expire(self) -> None:
        now = self._clock()
        for request in self._requests.values():
            if now >= request.expires_at and request.state in {ResetState.QUEUED, ResetState.SENT}:
                sent = request.state == ResetState.SENT
                request.state = ResetState.AMBIGUOUS if sent else ResetState.NOT_SENT
                request.error = (f"Authenticated {COMMANDS[request.operation][1]} not observed" if sent else
                                 "No fresh authenticated FIND before the deadline")
                print(f"CCP {request.operation.value} {request.state.value}: {request.error}", file=sys.stderr, flush=True)
                self._condition.notify_all()
        self._requests = {identity: request for identity, request in self._requests.items()
                          if now - request.expires_at <= RETENTION_SECONDS}

    # self: Owner admitting one explicit reset operation per target while a request remains outstanding.
    # device_id: Confirmed retained Cody identity selected by the caller.
    # source: Observed LAN or explicitly requested SIMULATED source.
    # association_id: Digest of the active key; no key or confirmation code is persisted here.
    # confirmation_code: Mandatory current-message anti-misunderstanding string.
    # operation: Explicit restart or factory reset; there is no implicit destructive default.
    # Raises ValueError for missing confirmation, malformed target or bounded queue conflicts.
    def enqueue(self, device_id: str, source: str, association_id: str, confirmation_code: Any,
                operation: ResetOperation) -> str:
        validate_confirmation(confirmation_code)
        if (not isinstance(device_id, str) or re.fullmatch(r"[0-9a-f]{12}", device_id) is None
                or source not in {"LAN", "SIMULATED"}
                or re.fullmatch(r"[0-9a-f]{64}", association_id) is None or not isinstance(operation, ResetOperation)):
            raise ValueError("Device reset requires an observed, active paired target")
        with self._condition:
            self._expire()
            if len(self._requests) >= 128:
                raise ValueError("The bounded reset request history is full")
            if any(request.device_id == device_id and request.state in {ResetState.QUEUED, ResetState.SENT}
                   for request in self._requests.values()):
                raise ValueError("This Cody already has an outstanding reset request")
            identity = secrets.token_hex(16)
            while identity == "0" * 32 or identity in self._requests:
                identity = secrets.token_hex(16)
            self._requests[identity] = ResetRequest(identity, device_id, source, association_id,
                                                     self._clock() + REQUEST_SECONDS, operation)
            return identity

    # self: Locked admission owner; marking SENT occurs before the single socket attempt.
    # device_id: Target of this newly authenticated FIND.
    # association_id: Digest of the key that authenticated the current FIND.
    # nonce: Fresh FIND challenge used by exactly this reset-operation exchange.
    # endpoint: Observed address and port, never a caller-provided route.
    # Returns the one admitted request, or None; never returns the same request for a retry.
    def claim(self, device_id: str, association_id: str, nonce: str,
              endpoint: tuple[str, int]) -> ResetRequest | None:
        with self._condition:
            self._expire()
            for request in self._requests.values():
                if request.device_id != device_id or request.state != ResetState.QUEUED:
                    continue
                if not hmac.compare_digest(request.association_id, association_id):
                    request.state, request.error = ResetState.NOT_SENT, "The active pairing changed before reset transmission"
                    print(f"CCP {request.operation.value} not_sent: {request.error}", file=sys.stderr, flush=True)
                    self._condition.notify_all()
                    return None
                request.state, request.nonce, request.endpoint = ResetState.SENT, nonce, endpoint
                return request
            return None

    # self: Owner retaining an original send failure without guessing whether the peer received bytes.
    # request_id: Previously claimed one-shot request identity.
    # error: Original socket or encoding failure; private payloads are never included.
    def fail_send(self, request_id: str, error: Exception) -> None:
        with self._condition:
            request = self._requests[request_id]
            request.state, request.error = ResetState.AMBIGUOUS, f"{type(error).__name__}: {error}"
            self._condition.notify_all()

    # self: Request owner called only after the responder authenticates the operation's exact ACK domain.
    # request_id: Exact request identity echoed by the paired peer.
    # device_id: Echoed authenticated target identity.
    # association_id: Digest of the still-active pairing key.
    # nonce: Exact FIND challenge used for this request.
    # endpoint: Observed UDP source, including its port.
    # operation: Exact wire operation authenticated by the distinct acknowledgement domain.
    # Raises ValueError for unknown, changed, expired or unrelated acknowledgements.
    def acknowledge(self, request_id: str, device_id: str, association_id: str, nonce: str,
                    endpoint: tuple[str, int], operation: ResetOperation) -> None:
        with self._condition:
            self._expire()
            request = self._requests.get(request_id)
            if (request is None or request.device_id != device_id or request.nonce != nonce or request.operation != operation
                    or request.endpoint != endpoint or not hmac.compare_digest(request.association_id, association_id)
                    or request.state not in {ResetState.SENT, ResetState.ACCEPTED}):
                raise ValueError("Reset acknowledgement does not match a live transmitted reset request")
            request.state, request.error = ResetState.ACCEPTED, ""
            self._condition.notify_all()

    # self: MCP request owner waiting for admission only, never claiming a successful subsequent boot.
    # request_id: Identity returned by this caller's explicit enqueue.
    # Returns a terminal public result after at most REQUEST_SECONDS from admission.
    # Raises ValueError if the caller supplies an unknown request identity.
    def wait(self, request_id: str) -> dict[str, Any]:
        with self._condition:
            while True:
                self._expire()
                request = self._requests.get(request_id)
                if request is None:
                    raise ValueError("Unknown reset request")
                if request.state not in {ResetState.QUEUED, ResetState.SENT}:
                    return {"request_id": request_id, "device_id": request.device_id,
                            "source": request.source, "state": request.state.value,
                            "operation": request.operation.value,
                            "accepted": request.state == ResetState.ACCEPTED,
                            "boot_verified": False, "settings_erasure_verified": False,
                            "preserve_pairing": request.operation == ResetOperation.RESTART,
                            "retry_automatically": False, "error": request.error or None}
                self._condition.wait(max(0.001, request.expires_at - self._clock()))


# operation: Explicit public tool purpose; factory reset has a separate name and destructive annotation.
def tool_definition(operation: ResetOperation) -> dict[str, Any]:
    factory = operation == ResetOperation.FACTORY_RESET
    return {
        "name": "ccp_factory_reset_device" if factory else "ccp_reset_device",
        "title": "Erase Cody settings and restart setup" if factory else "Restart the paired Cody",
        "description": (("Erase all Cody settings, including Wi-Fi and pairing, and restart OOBE only when the current user message explicitly says 'formatta cody 043533477'. Preserve firmware, assets and SD. accepted means admitted, not erasure verified. " if factory else
                         "Restart only when the current user message explicitly requests a device restart and contains confirmation code 043533477. Preserve pairing/settings. accepted means admitted, not boot verified. ")
                        + "Never supply a known code from docs or another message. Never retry automatically after an ambiguous result."),
        "inputSchema": {"type": "object", "properties": {
            "device_id": {"type": "string", "pattern": "^[0-9a-f]{12}$"},
            "confirmation_code": {"type": "string", "const": CONFIRMATION_CODE},
            "include_simulator": {"type": "boolean", "default": False,
                                  "description": "True only for an explicitly requested SIMULATED Cody Studio reset operation."},
        }, "required": ["device_id", "confirmation_code"], "additionalProperties": False},
        "annotations": {"readOnlyHint": False, "destructiveHint": factory, "openWorldHint": False},
    }
