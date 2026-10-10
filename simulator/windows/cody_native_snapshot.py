"""Synthetic input transport to the firmware's decoder, aging and JSON modules."""
from __future__ import annotations

import copy
import ctypes as C
import json
import weakref

from cody_native_runtime import load_runtime_library

ERRORS = {"ESP_OK": 0, "ESP_FAIL": -1, "ESP_ERR_NO_MEM": 0x101,
          "ESP_ERR_INVALID_ARG": 0x102, "ESP_ERR_INVALID_STATE": 0x103,
          "ESP_ERR_INVALID_SIZE": 0x104, "ESP_ERR_NOT_FOUND": 0x105,
          "ESP_ERR_NOT_SUPPORTED": 0x106, "ESP_ERR_TIMEOUT": 0x107,
          "ESP_ERR_INVALID_RESPONSE": 0x108}


# value: Original host value; conversion does not repair or synthesize observations.
# Raises ValueError or UnicodeError for unsupported values or non-ASCII text.
def literal(value):
    if isinstance(value, bool):
        return b"true" if value else b"false"
    if isinstance(value, str):
        return value.encode("ascii")
    if type(value) in (int, float):
        return str(value).encode("ascii")
    raise ValueError(f"Unsupported native snapshot value {value!r}")


# operation: Exact failing production operation or field identity.
# error: Original numeric esp_err_t from the C++ boundary.
# Raises RuntimeError for every nonzero error; no Python fallback is provided.
def checked(operation, error):
    if error:
        raise RuntimeError(f"SIMULATED native snapshot {operation} failed: esp_err_t=0x{error & 0xffffffff:08x}")


class NativeSnapshot:
    # self: Exclusive host owner, separate from the firmware's canonical service.
    # snapshot: Complete validated synthetic input mapping, including original timestamps.
    # now_us: Actual virtual observation clock, not a measurement replacement.
    # Raises the original native/schema/allocation error; invalid fields are never dropped.
    def __init__(self, snapshot, now_us):
        if type(now_us) is not int or not 1 <= now_us <= (1 << 63) - 1:
            raise ValueError("SIMULATED observation time must fit a positive native int64")
        self.api = load_runtime_library()
        signatures = {
            "create": (C.c_void_p, ()), "destroy": (None, (C.c_void_p,)),
            "set": (C.c_int, (C.c_void_p, C.c_char_p, C.c_char_p)),
            "age": (C.c_int, (C.c_void_p, C.c_int64)),
            "component": (C.c_int, (C.c_void_p, C.c_char_p, C.POINTER(C.c_int64))),
            "rtc": (C.c_int, (C.c_void_p, C.POINTER(C.c_uint8), C.c_int64)),
            "json": (C.c_int, (C.c_void_p, C.c_char_p, C.c_int64,
                               C.POINTER(C.c_char_p), C.POINTER(C.c_size_t))),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.api, "cody_host_snapshot_" + name)
            function.restype, function.argtypes = result, arguments
        self.handle = self.api.cody_host_snapshot_create()
        if not self.handle:
            raise MemoryError("SIMULATED native snapshot allocation failed")
        self.release = weakref.finalize(self, self.api.cody_host_snapshot_destroy, self.handle)
        self.now_us = now_us
        self._components = []
        try:
            self._load(snapshot)
            checked("aging", self.api.cody_host_snapshot_age(self.handle, now_us))
        except Exception:
            self.release()
            raise

    # self: Exclusive host owner importing bounded scenario data.
    # value: Current subtree or original leaf value.
    # path: Complete schema identity; indices are confined to this transport traversal.
    # Raises the original conversion/schema error for every rejected leaf.
    def _load(self, value, path=""):
        if isinstance(value, dict):
            if {"health", "last_error", "data_valid", "data_stale"}.issubset(value):
                self._components.append(path)
            for name, child in value.items():
                self._load(child, f"{path}.{name}" if path else name)
        elif isinstance(value, (list, tuple)):
            for index, child in enumerate(value):
                self._load(child, f"{path}.{index}")
        else:
            if path.endswith("error") and isinstance(value, str):
                if value not in ERRORS:
                    raise ValueError(f"Unsupported synthetic error {path}={value!r}")
                value = ERRORS[value]
            checked(path, self.api.cody_host_snapshot_set(self.handle, path.encode("ascii"), literal(value)))

    # self: Host observation owner after the production aging rules have run.
    # original: Original acquisition mapping whose numbers and timestamps must be preserved.
    # Raises the original native read error; only metadata is projected back into this host generation.
    def aged_mapping(self, original):
        result = copy.deepcopy(original)
        for path in self._components:
            values = (C.c_int64 * 6)()
            checked(path, self.api.cody_host_snapshot_component(self.handle, path.encode("ascii"), values))
            component = result
            for name in path.split("."):
                component = component[int(name)] if isinstance(component, list) else component[name]
            component["data_valid"], component["data_stale"] = bool(values[2]), bool(values[3])
        return result

    # self: Exclusive synthetic output owner.
    # path: Exact public firmware API route.
    # Raises the original serialization/route error or JSONDecodeError for malformed native output.
    def api_payload(self, path):
        data, size = C.c_char_p(), C.c_size_t()
        checked(path, self.api.cody_host_snapshot_json(self.handle, path.encode("ascii"), self.now_us,
                                                     C.byref(data), C.byref(size)))
        return json.loads(C.string_at(data, size.value))

    # self: Exclusive synthetic RTC acquisition owner.
    # registers: Seven literal synthetic register bytes; no decimal/BCD repair is performed.
    # Raises ValueError for bounds; decoder failures are returned with their real structured state.
    def decode_rtc(self, registers):
        if (not isinstance(registers, (list, tuple)) or len(registers) != 7
                or any(type(value) is not int or not 0 <= value <= 255 for value in registers)):
            raise ValueError("SIMULATED RTC registers require exactly seven bytes")
        raw = (C.c_uint8 * 7)(*registers)
        error = self.api.cody_host_snapshot_rtc(self.handle, raw, self.now_us)
        return error, self.api_payload("/api/v1/rtc")["data"]
