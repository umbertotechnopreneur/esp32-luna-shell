# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Thin transport to the firmware CCP decoder and deterministic transaction engine."""
from __future__ import annotations

import ctypes as C
import math
import weakref
from types import SimpleNamespace

from cody_native_runtime import load_runtime_library


class NativeChoice(C.Structure):
    _fields_ = [("id", C.c_char * 13), ("label", C.c_char * 13), ("detail", C.c_char * 64), ("tone", C.c_uint8)]


class NativePayload(C.Structure):
    _fields_ = [("kind", C.c_uint8), ("context", C.c_char * 25), ("title", C.c_char * 25),
        ("body", C.c_char * 64), ("choice_count", C.c_uint8), ("choices", NativeChoice * 3),
        ("selected_id", C.c_char * 13), ("failure_count", C.c_uint8), ("failed_checks", (C.c_char * 49) * 6),
        ("progress_valid", C.c_bool), ("passed", C.c_uint16), ("total", C.c_uint16)]


class NativeCcpError(ValueError):
    # self: Original native failure crossing the Python endpoint boundary.
    # operation: Actual firmware decoder/transaction operation.
    # error: Original signed esp_err_t, retained for structured presentation state.
    def __init__(self, operation, error):
        self.esp_err_t = error
        super().__init__(f"Cody C++ CCP {operation}: esp_err_t=0x{error & 0xffffffff:08x}")


# value: Original host monotonic seconds; narrowing is admitted before ctypes.
# Raises ValueError for non-finite, negative or unrepresentable clock observations.
def microseconds(value):
    if not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value < (2 ** 63 - 180000000) / 1000000:
        raise ValueError("Cody CCP monotonic clock is invalid")
    return int(value * 1000000)


# operation: Actual firmware operation name.
# error: Original esp_err_t across the C ABI.
# Raises ValueError with the real numeric error; no Python transition substitute.
def checked(operation, error):
    if error:
        raise NativeCcpError(operation, error)


class NativeTransaction:
    # self: Per-link owner, serialized by the endpoint lock.
    # Raises loader errors or MemoryError; the generated firmware DLL is mandatory.
    def __init__(self):
        self.api = load_runtime_library()
        signatures = {
            "create": ([], C.c_void_p), "destroy": ([C.c_void_p], None),
            "event": ([C.c_void_p, C.c_char_p, C.c_char_p, C.c_uint32, C.c_char_p, C.c_int64], C.c_int32),
            "ack": ([C.c_void_p, C.c_char_p, C.c_char_p, C.c_char_p, C.c_int64], C.c_int32),
            "tick": ([C.c_void_p, C.c_int64], C.c_int32),
            "submit": ([C.c_void_p, C.c_char_p, C.c_char_p, C.c_uint8, C.c_int64], C.c_int32),
            "reset": ([C.c_void_p], C.c_int32),
            "dismiss": ([C.c_void_p, C.c_char_p], C.c_int32),
            "terminal": ([C.c_void_p, C.c_void_p, C.POINTER(C.c_uint32)], C.c_int32),
            "read": ([C.c_void_p, C.POINTER(NativePayload), C.c_uint32, C.c_void_p,
                      C.POINTER(C.c_uint32), C.POINTER(C.c_int64), C.POINTER(C.c_int32)], C.c_int32),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.api, "cody_host_ccp_" + name)
            function.argtypes, function.restype = arguments, result
        self.handle = self.api.cody_host_ccp_create()
        if not self.handle:
            raise MemoryError("Cody C++ CCP owner allocation failed")
        self._finalizer = weakref.finalize(self, self.api.cody_host_ccp_destroy, self.handle)

    # self: Per-link native transaction owner.
    # fields: Already authenticated bounded EVENT envelope.
    # now: Original monotonic receive time in seconds.
    # Raises ValueError with the original decoder/transition error.
    def event(self, fields, now):
        lifetime = int(fields["ttl"])
        if str(lifetime) != fields["ttl"] or not 0 <= lifetime <= 180:
            raise ValueError("CCP lifetime is outside its original bound")
        checked("EVENT", self.api.cody_host_ccp_event(self.handle, fields["id"].encode("ascii"),
            fields["state"].encode("ascii"), lifetime, fields["data"].encode("ascii"), microseconds(now)))

    # self: Per-link native transaction owner.
    # fields: Already authenticated bounded REPLYACK envelope.
    # now: Original monotonic receive time in seconds.
    # Returns original esp_err_t; admitted retirement must be read before reporting an error.
    def ack(self, fields, now):
        return self.api.cody_host_ccp_ack(self.handle, fields["id"].encode("ascii"),
            fields["choice"].encode("ascii"), fields["status"].encode("ascii"), microseconds(now))

    # self: Per-link native transaction owner.
    # now: Original monotonic maintenance time in seconds.
    # Raises ValueError for an invalid time or native owner error.
    def tick(self, now):
        checked("expiry", self.api.cody_host_ccp_tick(self.handle, microseconds(now)))

    # self: Per-link native transaction owner.
    # request_id: Exact identity displayed at explicit Send.
    # choice: Authored semantic choice, independent of storage order.
    # fresh: Owner-confirmed transport freshness.
    # now: Original monotonic confirmation time in seconds.
    # Raises ValueError for a native admission error without mutation.
    def submit(self, request_id, choice, fresh, now):
        checked("Send", self.api.cody_host_ccp_submit(self.handle, request_id.encode("ascii"),
            choice.encode("ascii"), bool(fresh), microseconds(now)))

    # self: Per-link owner explicitly reset by commissioning/reset.
    # Raises ValueError for a missing native owner.
    def reset(self):
        checked("reset", self.api.cody_host_ccp_reset(self.handle))

    # self: Per-link native owner explicitly dismissed by the user.
    # request_id: Exact informational/terminal identity currently displayed.
    # Raises ValueError with the original dismissal admission failure.
    def dismiss(self, request_id):
        checked("dismiss", self.api.cody_host_ccp_dismiss(self.handle, request_id.encode("ascii")))

    # self: Per-link native owner retaining actual closure across explicit Done.
    # Raises ValueError for native read failure or transport mismatch.
    def terminal(self):
        identity, state = C.create_string_buffer(33), C.c_uint32()
        checked("terminal observation", self.api.cody_host_ccp_terminal(self.handle, identity, C.byref(state)))
        states = ("none", "pending", "sending", "received", "delivered", "cancelled", "expired", "rejected")
        if state.value >= len(states):
            raise ValueError("Cody C++ CCP terminal state violates its ABI")
        return None if state.value == 0 else (identity.value.decode("ascii"), states[state.value])

    # self: Per-link native owner; Python receives a projection, never decides transitions.
    # interactions: Broker DTO module used only to represent the C++ decoded fields.
    # Raises ValueError for ABI mismatch, invalid native output or read failure.
    def snapshot(self, interactions):
        payload, identity = NativePayload(), C.create_string_buffer(33)
        state, deadline, error = C.c_uint32(), C.c_int64(), C.c_int32()
        checked("read", self.api.cody_host_ccp_read(self.handle, C.byref(payload), C.sizeof(payload),
            identity, C.byref(state), C.byref(deadline), C.byref(error)))
        states = ("none", "pending", "sending", "received", "delivered", "cancelled", "expired", "rejected")
        if state.value >= len(states) or payload.choice_count > 3 or payload.failure_count > 6:
            raise ValueError("Cody C++ CCP output violates its transport bounds")
        decoded = None
        if payload.kind:
            decoded = interactions.EventPayload(kind=interactions.EventKind(payload.kind),
                context=payload.context.decode("ascii"), title=payload.title.decode("ascii"), body=payload.body.decode("ascii"),
                choices=tuple(interactions.Choice(choice.id.decode("ascii"), choice.label.decode("ascii"),
                    choice.detail.decode("ascii"), interactions.Tone(choice.tone)) for choice in payload.choices[:payload.choice_count]),
                selected_id=payload.selected_id.decode("ascii"),
                failures=tuple(bytes(value).split(b"\0", 1)[0].decode("ascii") for value in payload.failed_checks[:payload.failure_count]),
                progress_valid=payload.progress_valid, passed=payload.passed, total=payload.total)
        return SimpleNamespace(request_id=identity.value.decode("ascii"), state=states[state.value], payload=decoded,
            expires=deadline.value / 1000000, error=error.value)
