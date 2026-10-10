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

"""Explicit synthetic resource transport to the firmware's actual standby plan."""
from __future__ import annotations

import ctypes as C
from enum import IntEnum
import threading
import weakref

from cody_native_runtime import load_runtime_library


class StandbyResource(IntEnum):
    FULL_SNAPSHOTS = 0
    UI_SAMPLING = 1
    GPS = 2
    DISTANCE = 3
    STORAGE = 4
    IMU = 5
    MOTION_WAKE = 6
    VOICE_AFE = 7
    BASE_AUDIO = 8
    WIRELESS = 9
    STANDALONE_DISPLAY = 10
    DISPLAY = 11
    LVGL = 12
    PREPARE_DARK_FRAME = 13
    PREPARE_WAKE_FRAME = 14


RESOURCE_NAMES = {resource.name.lower().replace("_", "-"): resource for resource in StandbyResource}
ERROR_CODES = {"ESP_OK": 0, "ESP_FAIL": -1, "ESP_ERR_NO_MEM": 0x101,
               "ESP_ERR_INVALID_ARG": 0x102, "ESP_ERR_INVALID_STATE": 0x103,
               "ESP_ERR_INVALID_SIZE": 0x104, "ESP_ERR_NOT_FOUND": 0x105,
               "ESP_ERR_NOT_SUPPORTED": 0x106, "ESP_ERR_TIMEOUT": 0x107,
               "ESP_ERR_INVALID_RESPONSE": 0x108}


# code: Original numeric ESP error returned by the native owner.
# Unknown errors retain their original identity; they never become success.
def error_name(code: int) -> str:
    return next((name for name, value in ERROR_CODES.items() if value == code), f"ESP_ERROR_{code}")


# operation: Native ABI operation whose result must be successful.
# error: Original result, retained numerically if no standard name exists.
# Raises RuntimeError for real native transport/configuration failure.
def _checked(operation: str, error: int) -> None:
    if error != 0:
        raise RuntimeError(f"Native synthetic standby {operation}: {error_name(error)} ({error})")


class NativeStandby:
    # self: Independent, serialized C++ synthetic-resource owner.
    # standby: Initial explicit scenario state; later resource changes use native transitions.
    # Raises RuntimeError/OSError/ValueError for absent, stale or rejected native runtime.
    def __init__(self, *, standby: bool):
        self.api = load_runtime_library()
        self.lock = threading.RLock()
        pointer = C.c_void_p
        signatures = {
            "create": (C.c_int32, (C.POINTER(pointer), C.c_bool)),
            "destroy": (None, (pointer,)),
            "clone": (C.c_int32, (pointer, C.POINTER(pointer))),
            "profile": (C.c_int32, (pointer, C.c_bool, C.c_bool, C.c_bool, C.c_bool)),
            "fault": (C.c_int32, (pointer, C.c_uint8, C.c_bool, C.c_int32, C.c_uint32, C.c_uint32)),
            "transition": (C.c_int32, (pointer, C.c_bool)),
            "read": (C.c_int32, (pointer, C.POINTER(C.c_int32), C.c_size_t,
                                 C.POINTER(C.c_uint8), C.POINTER(C.c_uint8),
                                 C.POINTER(C.c_uint8), C.POINTER(C.c_uint32), C.c_size_t)),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.api, "cody_host_standby_" + name)
            function.restype, function.argtypes = result, arguments
        handle = pointer()
        _checked("create", self.api.cody_host_standby_create(C.byref(handle), standby))
        if not handle.value:
            raise RuntimeError("Native standby allocation succeeded without an owner")
        self.handle = handle
        self.release = weakref.finalize(self, self.api.cody_host_standby_destroy, handle)
        self.profile_values = {"deep_sleep_enabled": False, "retained_connection": False,
                               "voice_listening": False, "touch_wake_available": True}
        self.fault_configuration = []

    # self: Serialized committed synthetic owner; its resource state and counters remain unchanged.
    # Returns an independently owned native candidate, discarded if publication fails.
    # Raises native/allocation errors before any committed resource owner changes.
    def prepare(self) -> "NativeStandby":
        with self.lock:
            candidate = object.__new__(NativeStandby)
            candidate.api = self.api
            candidate.lock = threading.RLock()
            candidate.profile_values = dict(self.profile_values)
            candidate.fault_configuration = [dict(value) for value in self.fault_configuration]
            handle = C.c_void_p()
            _checked("clone", self.api.cody_host_standby_clone(self.handle, C.byref(handle)))
            if not handle.value:
                raise RuntimeError("Native standby clone succeeded without a candidate")
            candidate.handle = handle
            try:
                candidate.release = weakref.finalize(candidate, self.api.cody_host_standby_destroy, handle)
            except BaseException:
                self.api.cody_host_standby_destroy(handle)
                raise
            return candidate

    # self: Serialized synthetic owner; decisions remain entirely inside C++.
    # deep_sleep_enabled: Whether the explicit modeled build admits deep sleep.
    # retained_connection: Whether notification service requires its radio connection.
    # voice_listening: Whether wake speech requires continued audio acquisition.
    # touch_wake_available: Whether the profile admits the deep-sleep wake path.
    # Raises ValueError for nonboolean inputs; RuntimeError for actual native rejection.
    def configure(self, *, deep_sleep_enabled: bool, retained_connection: bool,
                  voice_listening: bool, touch_wake_available: bool) -> None:
        values = (deep_sleep_enabled, retained_connection, voice_listening, touch_wake_available)
        if any(type(value) is not bool for value in values):
            raise ValueError("Standby service/profile fields must be booleans")
        with self.lock:
            _checked("profile", self.api.cody_host_standby_profile(self.handle, *values))
            self.profile_values = {"deep_sleep_enabled": deep_sleep_enabled,
                "retained_connection": retained_connection, "voice_listening": voice_listening,
                "touch_wake_available": touch_wake_available}

    # self: Serialized synthetic owner; faults affect only its resource transport.
    # resource: Typed production resource identity.
    # target: Requested resource policy state on which to inject a failure.
    # error: Original named ESP error; zero cannot masquerade as a fault.
    # count: Number of failures, bounded at the native ABI.
    # skip: Matching operation calls permitted before failures begin.
    # Raises ValueError for invalid identities/values; RuntimeError for native rejection.
    def inject_fault(self, resource: StandbyResource, *, target: bool, error: str,
                     count: int = 1, skip: int = 0) -> None:
        if (not isinstance(resource, StandbyResource) or type(target) is not bool or
                not isinstance(error, str) or error not in ERROR_CODES or ERROR_CODES[error] == 0 or
                type(count) is not int or not 1 <= count <= 100 or
                type(skip) is not int or not 0 <= skip <= 100):
            raise ValueError("Synthetic standby fault identity or bounds rejected")
        with self.lock:
            _checked("fault", self.api.cody_host_standby_fault(
                self.handle, int(resource), target, ERROR_CODES[error], count, skip))
            self.fault_configuration.append({"resource": resource.name.lower().replace("_", "-"),
                "target": target, "error": error, "count": count, "skip": skip})

    # self: Serialized owner whose real logical result is published even on failure.
    # target: Requested lifecycle state; C++ owns ordering and rollback.
    # Raises ValueError for nonboolean input; RuntimeError for a result-read transport failure.
    def transition(self, target: bool) -> dict:
        if type(target) is not bool:
            raise ValueError("Standby target must be boolean")
        with self.lock:
            error = self.api.cody_host_standby_transition(self.handle, target)
            result = self.snapshot()
            # A rejected incomplete transaction can return a new admission error
            # without erasing the owner evidence of its original failed recovery.
            result["request_error_code"] = error
            result["request_error"] = error_name(error)
            return result

    # self: Serialized owner retaining separate state confidence and each failure cause.
    # Raises RuntimeError for an actual native ABI read failure.
    def snapshot(self) -> dict:
        with self.lock:
            count = len(StandbyResource)
            values = (C.c_int32 * 12)()
            applied, consistent = (C.c_uint8 * count)(), (C.c_uint8 * count)()
            unsupported = (C.c_uint8 * count)()
            calls = (C.c_uint32 * count)()
            _checked("read", self.api.cody_host_standby_read(self.handle, values, len(values),
                     applied, consistent, unsupported, calls, count))
            result = {"synthetic_resources": True, "implementation": "firmware-cpp",
                      "standby": bool(values[0]), "incomplete": bool(values[1]),
                      "sleep": "deep" if values[7] else "software",
                      "physical_sleep_executed": False, "audio_suspended": bool(values[8]),
                      "display_off": bool(values[9]), "display_consistent": bool(values[10]),
                      "profile": dict(self.profile_values), "resources": {},
                      "fault_configuration": [dict(value) for value in self.fault_configuration]}
            for name, position in (("error", 2), ("rollback_error", 3), ("recovery_error", 4),
                                   ("storage_error", 5), ("wireless_error", 6), ("display_rollback_error", 11)):
                result[name + "_code"] = values[position]
                result[name] = error_name(values[position])
            for name, resource in RESOURCE_NAMES.items():
                result["resources"][name] = {"applied": bool(applied[int(resource)]),
                    "consistent": bool(consistent[int(resource)]), "operation_calls": calls[int(resource)],
                    "available": not bool(unsupported[int(resource)])}
            return result
