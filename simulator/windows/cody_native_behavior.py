"""Synthetic desktop transport to the actual firmware caress and mood models."""
from __future__ import annotations

import ctypes as C
from dataclasses import dataclass
import weakref

from cody_native import checked
from cody_petting import PettingZone
from cody_happiness import PersonalityEvent


@dataclass(frozen=True)
class NativeHappinessSnapshot:
    happiness: int
    sadness: int
    decay_elapsed_us: int


class NativeBehavior:
    # self: Independent C++ caress and mood owner for ordinary Studio.
    # native: Already validated DLL transport; authoring studies use their explicit Python owner.
    # Raises RuntimeError for real native initialization failure or AttributeError for missing ABI.
    def __init__(self, native):
        self.api = native.api
        pointer = C.c_void_p
        signatures = {
            "behavior_create": (C.c_int, (C.POINTER(pointer),)),
            "behavior_destroy": (None, (pointer,)),
            "happiness_read": (C.c_int, (pointer, C.POINTER(C.c_uint16), C.POINTER(C.c_int64))),
            "happiness_advance": (C.c_int, (pointer, C.c_int64)),
            "happiness_event": (C.c_int, (pointer, C.c_uint8)),
            "petting_cancel": (C.c_int, (pointer,)),
            "petting_observe": (C.c_int, (pointer, pointer, C.c_float, C.c_float, C.c_bool,
                                          C.c_bool, C.c_int64, C.POINTER(C.c_uint8))),
            "petting_affection_us": (C.c_int64, ()),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.api, "cody_host_" + name)
            function.restype, function.argtypes = result, arguments
        handle = pointer()
        checked("behavior_create", self.api.cody_host_behavior_create(C.byref(handle)))
        if not handle.value:
            raise RuntimeError("Cody C++ behavior owner was absent after initialization")
        self.handle = handle
        self.release = weakref.finalize(self, self.api.cody_host_behavior_destroy, handle)
        self.config = {"affection_us": self.api.cody_host_petting_affection_us()}

    # self: Native artistic mood owner, with no duplicated desktop arithmetic.
    # Raises RuntimeError for actual native state/configuration errors.
    def snapshot(self):
        values, remainder = (C.c_uint16 * 2)(), C.c_int64()
        checked("happiness_read", self.api.cody_host_happiness_read(self.handle, values, C.byref(remainder)))
        return NativeHappinessSnapshot(values[0], values[1], remainder.value)

    # self: Native mood owner retaining the partial decay interval.
    # elapsed_us: Nonnegative original awake desktop duration.
    # Raises ValueError for an unrepresentable duration; RuntimeError for actual firmware errors.
    def advance(self, elapsed_us):
        if type(elapsed_us) is not int or not 0 <= elapsed_us <= 0x7fffffffffffffff:
            raise ValueError(f"Native happiness interval rejected: {elapsed_us!r}")
        checked("advance_happiness", self.api.cody_host_happiness_advance(self.handle, elapsed_us))
        return self.snapshot()

    # self: Native mood owner whose event binding is defined by compiled authoring.
    # event: Typed semantic catalog identity of the accepted interaction.
    # Raises ValueError for an untyped identity; RuntimeError for the actual rejected firmware event.
    def apply_event(self, event):
        if not isinstance(event, PersonalityEvent):
            raise ValueError(f"Native happiness event rejected: {event!r}")
        checked("apply_happiness_event", self.api.cody_host_happiness_event(self.handle, int(event)))
        return self.snapshot()

    # self: Native contact owner; the accepted firmware cooldown survives cancellation.
    # Raises RuntimeError for an absent or invalid native owner.
    def cancel(self):
        checked("cancel_petting", self.api.cody_host_petting_cancel(self.handle))

    # self: Native recognizer without a duplicated Python classifier.
    # scene: Last presented inspector dictionary retaining its actual native RigScene.
    # point: Logical synthetic desktop pointer coordinate after rotation mapping.
    # pressed: Actual acquired desktop button state.
    # pressed_edge: First acquired report of this contact.
    # sampled_us: Original positive monotonic desktop event time.
    # Raises ValueError for missing native geometry or time; RuntimeError for firmware rejection.
    def observe(self, scene, point, pressed, pressed_edge, sampled_us):
        if type(sampled_us) is not int or not 0 < sampled_us <= 0x7fffffffffffffff:
            raise ValueError(f"Native petting timestamp rejected: {sampled_us!r}")
        native_scene = scene.get("_native_scene")
        if native_scene is None or native_scene.api is not self.api:
            raise ValueError("Native petting requires the current frame from the same C++ library")
        zone = C.c_uint8()
        checked("observe_petting", self.api.cody_host_petting_observe(self.handle, native_scene.handle,
            point[0], point[1], pressed, pressed_edge, sampled_us, C.byref(zone)))
        return PettingZone(zone.value)
