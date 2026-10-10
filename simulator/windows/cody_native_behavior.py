"""Synthetic desktop transport to the actual firmware caress and mood models."""
from __future__ import annotations

import ctypes as C
from dataclasses import dataclass, replace
from enum import IntEnum
import weakref

from cody_native import bounded_integer, checked
from cody_petting import PettingZone
from cody_happiness import PersonalityEvent
from cody_personality import PersonalityCue


class CharacterWorkKind(IntEnum):
    NONE = 0
    THINKING = 1
    EDITING = 2
    RUNNING = 3


@dataclass(frozen=True)
class CharacterActivity:
    chat: str = ""
    sequence: int = 0
    current: bool = False
    working: bool = False
    attention: bool = False
    compacting: bool = False
    refreshed: bool = False
    turn: str = ""
    work_kind: CharacterWorkKind = CharacterWorkKind.NONE
    ready: bool = False
    interrupted: bool = False


class HostCharacterInputs(C.Structure):
    """Transport only; firmware C++ owns Codex acting and completion continuity."""
    _fields_ = [("chat", C.c_char_p), ("sequence", C.c_uint64), ("turn", C.c_char_p),
        ("placement", C.c_float * 3), ("gaze", C.c_float * 2)] + [
        (name, C.c_bool) for name in ("current", "working", "attention", "compacting", "refreshed",
            "ready", "interrupted", "enabled", "paused", "rebase", "gaze_visible", "affection_allowed")
    ] + [("work_kind", C.c_uint8)]


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
        self.contour_names = tuple(native.contour_names)
        self.channel_count = len(native.rig["channels"])
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
            "power_cue": (C.c_int, (pointer, C.c_bool, C.c_bool, C.c_bool, C.c_float,
                                   C.c_bool, C.POINTER(C.c_uint8), C.POINTER(C.c_bool))),
            "character_compose": (C.c_int, (pointer, C.POINTER(HostCharacterInputs), C.c_int64,
                C.POINTER(C.c_float), C.c_uint32, C.POINTER(C.c_float), C.POINTER(C.c_uint8))),
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
        self.power_low_active = False

    # self: Existing isolated native character owner and its shared battery hysteresis.
    # current: Original synthetic sample freshness and validity.
    # present: Explicit synthetic battery-presence selection.
    # percentage_valid: Whether this source supplied a percentage.
    # percentage: Original charge value; native validation retains any actual rejection.
    # charging: Explicit synthetic charger state, never inferred from percentage.
    # Raises ValueError for non-boolean admission; RuntimeError for actual native power errors.
    def power_cue(self, *, current, present, percentage_valid, percentage, charging):
        if any(type(value) is not bool for value in (current, present, percentage_valid, charging)):
            raise ValueError("Native power admission requires actual booleans")
        cue, low_active = C.c_uint8(), C.c_bool()
        checked("power_cue", self.api.cody_host_power_cue(self.handle, current, present,
            percentage_valid, percentage, charging, C.byref(cue), C.byref(low_active)))
        self.power_low_active = low_active.value
        return PersonalityCue(cue.value)

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

    # self: Existing native mood/caress owner, also retaining bounded artistic direction state.
    # frame: Already composed native response, or None to consume hidden-route events.
    # placement: Nominal logical x/y/scale before motion and breathing.
    # elapsed_us: Nonnegative active animation duration, without pause/standby dwell.
    # enabled: Whether ordinary companion acting owns the displayed face.
    # paused: Freeze artistic time while consuming observations.
    # rebase: Discard completion continuity across source, orientation or hidden-route changes.
    # gaze: Actual visible logical target, or None for a forward-facing expression.
    # affection_allowed: Existing route, power and eye-reaction admission decision.
    # activity: Coherence-qualified CCP facts or explicit rendering-only preview facts.
    # Raises ValueError for rejected transport values; RuntimeError for original native failures.
    def direct(self, frame, placement, elapsed_us, *, enabled=False, paused=False, rebase=False,
               gaze=None, affection_allowed=False, activity=CharacterActivity()):
        bounded_integer(elapsed_us, 0, 0x7fffffffffffffff, "character.elapsed_us")
        if not isinstance(activity, CharacterActivity):
            raise ValueError("Character direction requires typed activity observations")
        bounded_integer(activity.sequence, 0, 0xffffffffffffffff, "character.sequence")
        if (not isinstance(activity.chat, str) or len(activity.chat) > 64 or
                (activity.current and (len(activity.chat) != 64 or
                    any(value not in "0123456789abcdef" for value in activity.chat)))):
            raise ValueError("Character direction requires an exact current chat identity")
        if (not isinstance(activity.turn, str) or (activity.turn and
                (len(activity.turn) != 64 or any(value not in "0123456789abcdef" for value in activity.turn)))):
            raise ValueError("Character direction requires an exact observed turn identity or absence")
        if not isinstance(activity.work_kind, CharacterWorkKind):
            raise ValueError("Character direction requires a typed Codex work kind")
        flags = (activity.current, activity.working, activity.attention, activity.compacting,
                 activity.refreshed, activity.ready, activity.interrupted,
                 enabled, paused, rebase, gaze is not None, affection_allowed)
        if any(type(value) is not bool for value in flags):
            raise ValueError("Character direction admission requires actual booleans")
        if len(placement) != 3 or (gaze is not None and len(gaze) != 2) or (enabled and frame is None):
            raise ValueError("Character direction requires complete displayed geometry")
        if frame is not None and (len(frame.face) != self.channel_count or set(frame.contour) != set(self.contour_names)):
            raise ValueError("Character direction requires the complete native response")
        wire = HostCharacterInputs(activity.chat.encode("ascii"), activity.sequence, activity.turn.encode("ascii"),
            (C.c_float * 3)(*placement), (C.c_float * 2)(*(gaze if gaze is not None else (0, 0))),
            *flags, int(activity.work_kind))
        face = (C.c_float * self.channel_count)(*frame.face) if frame is not None else None
        contour = (C.c_float * 4)(*(frame.contour[name] for name in self.contour_names)) if frame is not None else None
        cue = C.c_uint8()
        checked("character_compose", self.api.cody_host_character_compose(self.handle, C.byref(wire),
            elapsed_us, face, self.channel_count if frame is not None else 0, contour, C.byref(cue)))
        if frame is not None:
            # Root motion, audio/protective channels and readouts remain the native response's own values.
            frame = replace(frame, face=list(face), contour=dict(zip(self.contour_names, contour)))
        return frame, PersonalityCue(cue.value)
