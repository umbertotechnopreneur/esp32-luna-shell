"""Thin SIMULATED input transport to the production Cody C++ presentation model."""
from __future__ import annotations

import ctypes as C
from dataclasses import dataclass

from ccp_interactions import Tone
from cody_native_runtime import load_runtime_library


class Choice(C.Structure):
    _fields_ = [("identity", C.c_char * 13), ("label", C.c_char * 13),
                ("detail", C.c_char * 64), ("tone", C.c_uint8)]


class Input(C.Structure):
    _fields_ = [("kind", C.c_uint8), ("state", C.c_char * 12), ("context", C.c_char * 25),
                ("title", C.c_char * 25), ("body", C.c_char * 64), ("choice_count", C.c_uint8),
                ("choices", Choice * 3), ("selected_id", C.c_char * 13),
                ("failure_count", C.c_uint8), ("failures", (C.c_char * 49) * 6),
                ("progress_valid", C.c_bool), ("passed", C.c_uint16), ("total", C.c_uint16),
                ("last_error", C.c_int32)]


class Navigation(C.Structure):
    _fields_ = [("stage", C.c_uint8), ("selected_id", C.c_char * 13), ("failure_page", C.c_uint8)]


class Context(C.Structure):
    _fields_ = [("online", C.c_bool), ("reply_admitted", C.c_bool), ("dismiss_admitted", C.c_bool),
                ("observation_error", C.c_int32), ("ui_error", C.c_int32), ("retry", C.c_uint8)]


class Bounds(C.Structure):
    _fields_ = [("x", C.c_uint16), ("y", C.c_uint16), ("width", C.c_uint16), ("height", C.c_uint16)]


class Line(C.Structure):
    _fields_ = [("text", C.c_char * 64), ("top", C.c_uint16), ("instant", C.c_bool)]


class Action(C.Structure):
    _fields_ = [("identity", C.c_uint8), ("choice_id", C.c_char * 13), ("label", C.c_char * 16),
                ("tone", C.c_uint8), ("bounds", Bounds), ("enabled", C.c_bool), ("card", C.c_bool),
                ("colors", C.c_uint16 * 4), ("icon", C.c_uint8)]


class Plan(C.Structure):
    _fields_ = [("route", C.c_uint8), ("scene", C.c_char * 24), ("title", C.c_char * 32),
                ("lines", Line * 8), ("line_count", C.c_uint8), ("actions", Action * 3),
                ("action_count", C.c_uint8), ("back", C.c_bool)]


@dataclass(frozen=True)
class EventAction:
    identity: str
    label: str
    tone: Tone
    bounds: tuple[int, int, int, int]
    enabled: bool = True
    native_action: bytes = b""


# text: Complete printable ASCII transport field.
# capacity: Fixed native ABI size including the terminating NUL.
# Raises ValueError instead of allowing ctypes to truncate data.
def ascii_field(text: str, capacity: int) -> bytes:
    value = text.encode("ascii")
    if len(value) >= capacity or any(character < 32 or character > 126 for character in value):
        raise ValueError("Native event field exceeds its printable ASCII bound")
    return value


# operation: Transport boundary name identifying the failing C++ operation.
# error: Original native model error code.
# Raises RuntimeError preserving the exact error value.
def checked(operation: str, error: int) -> None:
    if error != 0:
        raise RuntimeError(f"Native event {operation} failed: 0x{error:08x}")


class NativeEventPresentation:
    # self: UI-owned native navigation/context and fixed ABI buffer.
    # Raises OSError, ValueError or AttributeError for absent/stale native output.
    def __init__(self) -> None:
        self.api = load_runtime_library()
        self.navigation = Navigation()
        self.context = Context()
        self.output = Plan()
        self.readable_at = C.c_int64(-1)
        self.api.cody_host_event_prepare.argtypes = [C.POINTER(Input), C.POINTER(Navigation),
                                                     C.POINTER(Context), C.POINTER(Plan)]
        self.api.cody_host_event_prepare.restype = C.c_int
        self.api.cody_host_event_press.argtypes = [C.POINTER(Input), C.POINTER(Navigation),
                                                  C.POINTER(Context), C.c_bool, C.c_float, C.c_float,
                                                  C.POINTER(C.c_uint8)]
        self.api.cody_host_event_press.restype = C.c_int
        self.api.cody_host_event_scroll.argtypes = [C.POINTER(Input), C.POINTER(Navigation), C.c_bool,
                                                   C.c_int, C.c_int, C.POINTER(C.c_bool)]
        self.api.cody_host_event_scroll.restype = C.c_int
        self.api.cody_host_event_controls_ready.argtypes = [C.POINTER(Plan), C.c_uint32, C.c_uint32]
        self.api.cody_host_event_controls_ready.restype = C.c_bool
        self.api.cody_host_event_result.argtypes = [C.POINTER(Navigation), C.POINTER(Context), C.c_uint8, C.c_int]
        self.api.cody_host_event_result.restype = C.c_int
        self.api.cody_host_event_wrap.argtypes = [C.c_char_p, C.c_uint32, C.POINTER(Plan)]
        self.api.cody_host_event_wrap.restype = C.c_int
        self.api.cody_host_event_dwell.argtypes = [C.POINTER(Input), C.POINTER(Context), C.c_bool,
                                                  C.c_int64, C.POINTER(C.c_int64), C.POINTER(C.c_bool)]
        self.api.cody_host_event_dwell.restype = C.c_int

    # self: Owner of reversible view state, separate from the endpoint's lifecycle.
    def reset(self) -> None:
        self.navigation = Navigation()
        self.context = Context()
        self.readable_at = C.c_int64(-1)

    # self: Native transport owner.
    # snapshot: Clearly SIMULATED immutable endpoint observation.
    # Raises ValueError for unsupported provenance or ABI bounds before any policy runs.
    def project(self, snapshot) -> Input:
        if snapshot.source != "SIMULATED" or snapshot.event_payload is None:
            raise ValueError("Native event presentation requires an isolated SIMULATED event")
        event = snapshot.event_payload
        if len(event.choices) > 3 or len(event.failures) > 6:
            raise ValueError("Native event collection exceeds its bounded firmware ABI")
        self.context.online = snapshot.event_fresh
        value = Input()
        value.kind = int(event.kind)
        value.state = ascii_field(snapshot.event_state, 12)
        value.context = ascii_field(event.context, 25)
        value.title = ascii_field(event.title, 25)
        value.body = ascii_field(event.body, 64)
        value.choice_count = len(event.choices)
        for index, choice in enumerate(event.choices):
            value.choices[index] = Choice(ascii_field(choice.choice_id, 13), ascii_field(choice.label, 13),
                                         ascii_field(choice.detail, 64), int(choice.tone))
        value.selected_id = ascii_field(event.selected_id, 13)
        value.failure_count = len(event.failures)
        for index, failure in enumerate(event.failures):
            value.failures[index].value = ascii_field(failure, 49)
        value.progress_valid = event.progress_valid
        if not 0 <= event.passed <= 65535 or not 0 <= event.total <= 65535:
            raise ValueError("Native event progress exceeds the firmware ABI")
        value.passed, value.total = event.passed, event.total
        value.last_error = snapshot.event_error
        return value

    # self: Native presentation transport, retaining the last complete plan.
    # snapshot: Latest immutable SIMULATED event.
    # Raises RuntimeError for actual production presentation rejection.
    def prepare(self, snapshot):
        value = self.project(snapshot)
        checked("prepare", self.api.cody_host_event_prepare(C.byref(value), C.byref(self.navigation),
                                                            C.byref(self.context), C.byref(self.output)))
        plan = self.output
        identities = {1: "open", 2: "choose", 3: "send", 4: "dismiss", 5: "retry", 6: "failures"}
        actions = []
        for action in plan.actions[:plan.action_count]:
            identity = identities[action.identity]
            if action.choice_id:
                identity += ":" + action.choice_id.decode("ascii")
            bounds = action.bounds
            actions.append(EventAction(identity, action.label.decode("ascii"), Tone(action.tone),
                                       (bounds.x, bounds.y, bounds.width, bounds.height), action.enabled,
                                       C.string_at(C.byref(action), C.sizeof(Action))))
        return (plan.scene.decode("ascii"), tuple((line.text.decode("ascii"), line.top)
                for line in plan.lines[:plan.line_count]), tuple(actions), plan.back)

    # self: Native output plan with exact status/narrative distinction.
    # visible: Current narrative typewriter character budget.
    # title_length: Actual authored or overridden scene title length.
    def controls_ready(self, visible: int, title_length: int) -> bool:
        return bool(self.api.cody_host_event_controls_ready(C.byref(self.output), visible, title_length))

    # self: Native presentation timer transport.
    # snapshot: Isolated readonly result and actual endpoint state.
    # now_us: Existing UI clock represented without truncation as microseconds.
    # controls_visible: Complete native narrative gate.
    # Raises RuntimeError for original projection errors.
    def dismiss_due(self, snapshot, now_us: int, controls_visible: bool) -> bool:
        value, due = self.project(snapshot), C.c_bool()
        checked("dwell", self.api.cody_host_event_dwell(C.byref(value), C.byref(self.context),
                                                       controls_visible, now_us, C.byref(self.readable_at), C.byref(due)))
        return due.value

    # self: Native navigation owner.
    # snapshot: Latest complete event and freshness, rechecked before touch admission.
    # point: Logical pointer matching the displayed orientation.
    # controls_visible: Real completed narrative gate.
    # Raises RuntimeError for production transition rejection.
    def press(self, snapshot, point, controls_visible: bool) -> int:
        value, effect = self.project(snapshot), C.c_uint8()
        checked("press", self.api.cody_host_event_press(C.byref(value), C.byref(self.navigation),
                                                        C.byref(self.context), controls_visible,
                                                        point[0], point[1], C.byref(effect)))
        return effect.value

    # self: Native owner of actual admission outcomes.
    # effect: Previously reviewed Reply or Dismiss intent.
    # error: Exact native code, or ESP_FAIL for a host endpoint exception reported to the user.
    # Returns the same admission error after recording it; caller preserves its original exception.
    # Raises RuntimeError if recording itself fails with a different native error.
    def result(self, effect: int, error: int) -> int:
        observed = self.api.cody_host_event_result(C.byref(self.navigation), C.byref(self.context), effect, error)
        if observed != error:
            checked("result", observed)
        return observed

    # self: Shared readonly page navigation transport.
    # snapshot: Latest isolated failure collection.
    # delta: Released logical displacement.
    # controls_visible: Drawing owner's reveal gate.
    # Raises RuntimeError for the original projection failure.
    def scroll(self, snapshot, delta, controls_visible: bool) -> bool:
        value, changed = self.project(snapshot), C.c_bool()
        checked("scroll", self.api.cody_host_event_scroll(C.byref(value), C.byref(self.navigation),
                                                          controls_visible, int(delta[0]), int(delta[1]), C.byref(changed)))
        return changed.value


# text: Complete bounded canonical event text.
# Raises RuntimeError, ValueError or OSError when native production wrapping is unavailable.
def wrapped(text: str) -> tuple[str, ...]:
    native, output = NativeEventPresentation(), Plan()
    value = ascii_field(text, 64)
    checked("wrap", native.api.cody_host_event_wrap(value, len(value), C.byref(output)))
    return tuple(line.text.decode("ascii") for line in output.lines[:output.line_count])


# image: Existing logical SIMULATED RGB framebuffer updated only after native success.
# action: Immutable packed action returned by the actual shared model.
# api: Already verified actual Cody native graphics library, configured independently here.
# Raises ValueError or RuntimeError for missing plan provenance or actual C++ raster errors.
def paint_native_action(image, action: EventAction, api) -> None:
    from PIL import Image
    if image.mode != "RGB" or image.size != (466, 466) or len(action.native_action) != C.sizeof(Action):
        raise ValueError("Native event drawing requires a complete production plan and logical RGB framebuffer")
    native = Action.from_buffer_copy(action.native_action)
    function = api.cody_host_event_paint
    function.argtypes = [C.POINTER(C.c_uint16), C.c_uint32, C.POINTER(Action)]
    function.restype = C.c_int
    pixels = (C.c_uint16 * (466 * 466))()
    for index, (red, green, blue) in enumerate(image.getdata()):
        pixels[index] = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
    checked("paint", function(pixels, len(pixels), C.byref(native)))
    output = bytearray(len(pixels) * 3)
    for index, color in enumerate(pixels):
        output[index * 3] = ((color >> 11) & 31) * 255 // 31
        output[index * 3 + 1] = ((color >> 5) & 63) * 255 // 63
        output[index * 3 + 2] = (color & 31) * 255 // 31
    image.paste(Image.frombytes("RGB", (466, 466), bytes(output)))
