"""Transport to the unchanged firmware C++ rig and RGB565 renderer on this PC."""
from __future__ import annotations

import ctypes as C
import hashlib
import json
import math
from pathlib import Path
from types import SimpleNamespace
from enum import IntEnum
import weakref

from PIL import Image
from cody_personality import PersonalityCue, PersonalityFrame, PersonalityPresentation, PersonalityPresentationStatus
from scripts.common.cody_response import ResponseState, ResponseFrame, SampleStamp, SampleHealth, InputOrigin, EyeId, IdleSceneId

F = C.POINTER(C.c_float)
U16 = C.POINTER(C.c_uint16)
U32 = C.POINTER(C.c_uint32)
P = C.c_void_p

class NativeActionIcon(IntEnum):
    NONE = 0
    LINK = 1
    WIFI = 2
    GEAR = 3
    CHECK = 4
    CLOSE = 5
    ARROW = 6
    QR = 7
    RETRY = 8
    MOTION = 9
    ROTATE = 10
    SOUND = 11
    ORIENTATION = 12


# value: Original transport integer, checked before ctypes could truncate or wrap it.
# lower: Minimum representable/domain value.
# upper: Maximum representable/domain value.
# name: Exact operation field identity retained in the rejection message.
# Raises ValueError for a rejected original value; no repaired or substituted value is returned.
def bounded_integer(value, lower, upper, name):
    if type(value) is not int or not lower <= value <= upper:
        raise ValueError(f'Cody native transport {name} rejected: {value!r}; requires integer [{lower},{upper}]')
    return value

class HostResponseInputs(C.Structure):
    """Versioned transport of synthetic driver facts; never a firmware snapshot."""
    _fields_ = [("health", C.c_int32 * 6), ("errors", C.c_int32 * 6), ("timestamps", C.c_int64 * 6),
        ("last_interaction", C.c_int64), ("gesture_started", C.c_int64),
        ("acceleration", C.c_float * 3), ("power", C.c_float), ("drag", C.c_float * 2),
        ("microphone_mean", C.c_uint32 * 2), ("microphone_peak", C.c_uint32 * 2),
        ("playback_mean", C.c_uint32), ("playback_cue", C.c_uint32),
        ("rotation", C.c_uint16), ("touch_rotation", C.c_uint16),
        ("microphone_count", C.c_uint8), ("charging", C.c_uint8), ("playback_active", C.c_uint8),
        ("pressed", C.c_uint8), ("idle_allowed", C.c_uint8), ("eye", C.c_uint8)]


class NativePanelMetric(C.Structure):
    """Original uint64 observation and independent UNAVAILABLE/CURRENT/STALE/INVALID state."""
    _fields_ = [("value", C.c_uint64), ("state", C.c_uint8)]

    # self: Typed native metric transport owner.
    # value: Original count or duration, never a substituted missing value.
    # state: Explicit unavailable/current/stale/invalid identity.
    # Raises ValueError before ctypes could narrow invalid original integers.
    def __init__(self, value=0, state=0):
        super().__init__(bounded_integer(value, 0, 0xffffffffffffffff, "panel.metric.value"),
                         bounded_integer(state, 0, 3, "panel.metric.state"))


class NativePanelInputs(C.Structure):
    """Borrowed PC facts only; native firmware owns all formatting, layout and bar arithmetic."""
    _fields_ = [(name, C.c_char_p) for name in ("project", "branch", "description", "chat_id", "model", "effort")] + [
        ("phase", C.c_uint8), ("compaction", C.c_uint8),
        ("link", C.c_uint8), ("activity", C.c_uint8), ("model_state", C.c_uint8), ("effort_state", C.c_uint8),
        ("connected", C.c_bool), ("simulated", C.c_bool), ("context_coherent", C.c_bool),
        ("context_estimated", C.c_bool), ("counters_pending", C.c_bool)] + [
        (name, NativePanelMetric) for name in ("used", "capacity", "threshold", "cumulative",
            "activity_age_us", "usage_age_us", "turn_elapsed_us", "compaction_elapsed_us")]

    # self: Bounded PC projection retained through its synchronous native preparation call.
    # Raises no exceptions; absent strings and metrics remain explicitly unavailable.
    def __init__(self):
        super().__init__()
        for name in ("project", "branch", "description", "chat_id", "model", "effort"):
            setattr(self, name, b"")


class NativePanelObservation(C.Structure):
    """Raw authenticated PC metadata; shared C++ owns receipt/source aging and presentation."""
    _fields_ = [(name, C.c_char_p) for name in ("project", "branch", "description", "chat_id",
        "technical_chat_id", "model", "effort")] + [("phase", C.c_uint8), ("compaction", C.c_uint8)] + [
        (name, C.c_bool) for name in ("connected", "link_valid", "link_stale", "activity_valid",
            "activity_stale", "technical_valid", "model_valid", "effort_valid", "compaction_valid",
            "compaction_age_valid", "activity_age_valid", "turn_elapsed_valid")] + [
        (name, C.c_uint64) for name in ("model_age_us", "compaction_age_us", "activity_age_us",
            "turn_elapsed_us", "receipt_age_us")]

    # self: Raw source projection consumed synchronously by the native scratch owner.
    # Raises no exceptions; absent fields retain unavailable flags rather than numeric claims.
    def __init__(self):
        super().__init__()
        for name in ("project", "branch", "description", "chat_id", "technical_chat_id", "model", "effort"):
            setattr(self, name, b"")


# operation: Native call being checked.
# result: Original esp_err_t returned across the transport boundary.
# Raises RuntimeError with the real numeric firmware error, never a Python fallback.
def checked(operation, result):
    if result:
        raise RuntimeError(f"Cody C++ {operation} failed: esp_err_t=0x{result & 0xffffffff:08x}")


class NativeScene:
    # self: Independent C++ frame owner retained with its Python inspector dictionary.
    # api: Loaded transport library, kept alive until the frame is released.
    # Raises MemoryError if the C++ allocation failed.
    def __init__(self, api):
        self.api = api
        self.handle = api.cody_host_scene_create()
        if not self.handle:
            raise MemoryError("Cody C++ scene allocation failed")
        self.release = weakref.finalize(self, api.cody_host_scene_destroy, self.handle)


class NativeRig:
    # self: Host transport owner; has no independent geometry or raster implementation.
    # root: Checkout containing the built host library and authoring source.
    # rig: Loaded authoring schema for inspector labels and named bounds only.
    # Raises OSError or ValueError for missing/stale native output or incompatible catalogs.
    def __init__(self, root, rig):
        directory = Path(root) / "artifacts/build-cody-simulator-graphics"
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
        if manifest.get("schema") != "cody.host-graphics.v1":
            raise ValueError("Unsupported native graphics manifest; use device.ps1 generate -Generator cody-simulator-graphics")
        for source, expected in manifest["inputs"].items():
            if hashlib.sha256((Path(root) / source).read_bytes()).hexdigest() != expected:
                raise ValueError(f"Stale Cody C++ simulator input {source}; regenerate cody-simulator-graphics")
        generated_rig = json.loads((Path(root) / 'assets/cody/rig/generated/manifest.json').read_text(encoding='utf-8'))
        if generated_rig['source_path'] != rig['source_path']:
            raise ValueError(f"Native Cody library was generated for {generated_rig['source_path']}, selected {rig['source_path']}; regenerate the selected rig and native library")
        if hashlib.sha256((Path(root) / rig['source_path']).read_text(encoding='utf-8').encode('utf-8')).hexdigest() != generated_rig['source_sha256']:
            raise ValueError('Native Cody rig tables are stale; generate cody-rig before compiling the PC driver')
        library = directory / "cody_host_graphics.dll"
        if hashlib.sha256(library.read_bytes()).hexdigest() != manifest["library_sha256"]:
            raise ValueError("Cody C++ simulator library hash mismatch")
        self.api = C.CDLL(str(library))
        signatures = {
            "abi": (C.c_uint32, ()), "channel_count": (C.c_uint32, ()),
            "channel_name": (C.c_char_p, (C.c_uint32,)), "point_count": (C.c_uint32, ()),
            "point_name": (C.c_char_p, (C.c_uint32,)), "path_count": (C.c_uint32, ()),
            "path_name": (C.c_char_p, (C.c_uint32,)), "scene_create": (P, ()),
            "scene_destroy": (None, (P,)), "state_create": (P, ()), "state_destroy": (None, (P,)),
            "evaluate": (C.c_int, (P, F, C.c_uint32, F, F, C.c_bool)),
            "points": (C.c_int, (P, F, C.c_uint32)),
            "path": (C.c_int, (P, C.c_uint32, U32, F, C.c_uint32)),
            "scale": (C.c_int, (P, F)),
            "draw": (C.c_int, (P, U16, C.c_uint32, C.c_bool, C.c_char_p, C.c_bool, C.c_bool, C.c_uint8, C.c_uint16)),
            "activity_overlay": (C.c_int, (P, C.c_uint8, C.c_uint32)),
            "activity_description": (C.c_int, (P, C.c_char_p, C.c_uint32, C.c_int64)),
            "activity_identity": (C.c_int, (P, C.c_char_p, C.c_char_p, C.c_int64, C.c_bool, C.c_int64, C.c_bool)),
            "activity_observe": (C.c_int, (P, C.c_uint8, C.c_char_p, C.c_bool, C.c_int64, C.POINTER(C.c_uint8), C.POINTER(C.c_int64))),
            "activity_pause": (C.c_int, (P,)),
            "panel_next": (C.c_int, (C.c_uint8, C.POINTER(C.c_uint8))),
            "panel_transform": (C.c_int, (F,)),
            "panel_prepare": (C.c_int, (P, C.c_uint8, C.POINTER(NativePanelInputs), C.c_int64)),
            "panel_observation": (C.c_int, (P, C.c_uint8, C.POINTER(NativePanelObservation), C.c_int64)),
            "panel_tap": (C.c_int, (P, C.c_int16, C.c_int16, C.c_bool, C.c_bool, C.c_int64,
                C.c_uint16, C.c_bool, C.c_bool, C.POINTER(C.c_bool))),
            "panel_cancel_tap": (C.c_int, (P,)),
            "panel_shake": (C.c_int, (P, C.c_float, C.c_float, C.c_float, C.c_int64, C.c_int64,
                C.c_bool, C.POINTER(C.c_bool))),
            "panel_suspend_shake": (C.c_int, (P, C.c_int64)),
            "connection": (C.c_int, (P, C.c_bool, C.c_bool, C.c_bool, C.c_bool, C.c_bool, C.c_int64, C.POINTER(C.c_uint8))),
            "scene_cue": (C.c_int, (C.c_uint16, C.c_uint8, C.POINTER(C.c_uint8))),
            "scene_id": (C.c_int, (C.c_char_p, U16)),
            "advance_face": (C.c_int, (F, F, C.c_uint32, C.c_float, F)),
            "animate": (C.c_int, (F, C.c_uint32, C.c_uint32, F)),
            "advance_contour": (C.c_int, (F, F, C.c_float, F)),
            "personality": (C.c_int, (P, P, C.c_uint8, C.c_float, C.c_uint32, C.c_uint16, C.c_uint8, C.c_int64, C.c_bool, U16, C.c_uint8, U16)),
            "personality_clear": (None, (P,)),
            "response_reset": (C.c_int, (P, C.c_uint32)),
            "set_away": (C.c_int, (P, C.c_bool)),
            "response_rebase": (C.c_int, (P,)),
            "response_readout": (C.c_int, (P, C.POINTER(C.c_double), C.c_uint32)),
            "response_timestamps": (C.c_int, (P, C.POINTER(C.c_int64), C.c_uint32)),
            "response_advance": (C.c_int, (P, C.POINTER(HostResponseInputs), C.c_float, C.c_float, C.c_int64, C.c_bool)),
            "response_compose": (C.c_int, (P, F, C.c_uint32, F, F, C.c_uint16, C.c_bool, F, C.c_uint32)),
            "hit": (C.c_int, (P, C.c_float, C.c_float, C.c_float, C.POINTER(C.c_uint8), C.POINTER(C.c_bool))),
            "select_skin": (C.c_int, (C.c_char_p,)),
            "text": (C.c_int, (U16, C.c_uint32, C.c_char_p, C.c_uint16, C.c_uint16, C.c_uint16,
                C.c_bool, C.c_uint16, C.c_uint16, C.c_uint16)),
            "scene_timing": (C.c_int, (C.c_uint16, C.c_int64, C.c_uint16, C.c_uint16, F)),
            "capsule": (C.c_int, (U16, C.c_uint32, U16, U16, C.c_uint8, C.c_int16, C.c_int16, C.c_uint16, C.c_bool)),
            "select_target": (C.c_int, (C.c_uint8, C.c_bool, C.c_bool, C.c_uint8, C.POINTER(C.c_uint8), C.POINTER(C.c_bool))),
            "fade": (C.c_int, (U16, C.c_uint32, C.c_uint8)),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.api, "cody_host_" + name)
            function.restype, function.argtypes = result, arguments
        if self.api.cody_host_abi() != 1:
            raise ValueError("Incompatible Cody C++ graphics ABI")
        channels = [self.api.cody_host_channel_name(index).decode("ascii")
                    for index in range(self.api.cody_host_channel_count())]
        points = [self.api.cody_host_point_name(index).decode("ascii")
                  for index in range(self.api.cody_host_point_count())]
        paths = [self.api.cody_host_path_name(index).decode("ascii")
                 for index in range(self.api.cody_host_path_count())]
        if (channels != [item["name"] for item in rig["channels"]] or
                points != [item["name"] for item in rig["points"]] or
                paths != [item["name"] for item in rig["paths"]]):
            raise ValueError("Cody C++ and authoring identities differ; regenerate rig and host library")
        self.rig, self.points = rig, points
        self.contour_names = [item["name"] for item in rig["contour"]["controls"]]
        self.state = self.api.cody_host_state_create()
        if not self.state:
            raise MemoryError("Cody C++ state allocation failed")
        self.release = weakref.finalize(self, self.api.cody_host_state_destroy, self.state)

    # self: Typed native transport owner.
    # values: Python iterable of finite float controls.
    # Raises ValueError for values that cannot cross the native finite-float boundary.
    def floats(self, values):
        values = list(values)
        if any(not math.isfinite(value) for value in values):
            raise ValueError("Native Cody input must contain finite float values")
        return (C.c_float * len(values))(*values)

    # self: Shared native transport and inspector authoring tables.
    # values: Current facial controls in checked canonical order.
    # transform: Logical x/y/scale.
    # contour: Optional named control state, including phase.
    # deform_face: Apply the exact native pearl warp to face and hit coordinates.
    # Raises RuntimeError for original native errors; MemoryError for frame allocation failure.
    def evaluate(self, values, transform, contour=None, deform_face=True):
        if len(values) != len(self.rig['channels']) or len(transform) != 3:
            raise ValueError("Native pose requires the exact channel count and three transform values")
        scene = NativeScene(self.api)
        shape = self.floats([contour[name] for name in self.contour_names] + [contour["phase"]]) if contour is not None else None
        checked("evaluate_rig", self.api.cody_host_evaluate(scene.handle, self.floats(values), len(values),
            self.floats(transform), shape, deform_face))
        coordinates = (C.c_float * (len(self.points) * 4))()
        checked("points", self.api.cody_host_points(scene.handle, coordinates, len(coordinates)))
        local = {name: tuple(coordinates[index * 4:index * 4 + 2]) for index, name in enumerate(self.points)}
        world = {name: tuple(coordinates[index * 4 + 2:index * 4 + 4]) for index, name in enumerate(self.points)}
        paths = []
        for index, definition in enumerate(self.rig["paths"]):
            metadata, vertices = (C.c_uint32 * 3)(), (C.c_float * (2 * (64 * 12 + 16)))()
            checked("path", self.api.cody_host_path(scene.handle, index, metadata, vertices, len(vertices)))
            paths.append(dict(definition=definition, vertices=[tuple(vertices[offset:offset + 2])
                for offset in range(0, metadata[0] * 2, 2)], visible=bool(metadata[1]), opacity=metadata[2] / 255))
        if contour is None:
            rest = next(path["vertices"] for path in paths if path["definition"]["name"] == self.rig["contour"]["path"])
        else:
            resting = self.evaluate(values, transform, deform_face=deform_face)
            rest = resting["rest_outline"]
        scale = C.c_float()
        checked('scale', self.api.cody_host_scale(scene.handle, C.byref(scale)))
        return dict(local=local, world=world, paths=paths, scale=scale.value,
            rest_outline=rest, _native_scene=scene)

    # self: Native library and validated frame transport.
    # scene: Inspector dictionary retaining its independently owned C++ RigScene.
    # debug: Draw native skeleton/handles.
    # selected: Optional exact authored point name.
    # paused: Native toolbar state.
    # navigation_visible: Scene navigation permission.
    # navigation_opacity: Explicit shared toolbar opacity.
    # background: Native RGB565 scene color.
    # Raises RuntimeError for native draw failures; no Python raster fallback exists.
    def draw(self, scene, debug, selected, paused, navigation_visible, navigation_opacity, background):
        pixels = (C.c_uint16 * (466 * 466))()
        checked("draw_rig_rows", self.api.cody_host_draw(scene["_native_scene"].handle, pixels, len(pixels), debug,
            selected.encode("ascii") if selected else None, paused, navigation_visible, navigation_opacity, background))
        # Expand actual RGB565 words into PC RGB; no geometry or compositing is repeated here.
        rgb = bytearray(len(pixels) * 3)
        for index, value in enumerate(pixels):
            rgb[index * 3] = ((value >> 11) & 31) * 255 // 31
            rgb[index * 3 + 1] = ((value >> 5) & 63) * 255 // 63
            rgb[index * 3 + 2] = (value & 31) * 255 // 31
        return Image.frombytes("RGB", (466, 466), bytes(rgb))

    # self: Native graphics owner; all font layout, overflow and marquee rastering stay in shared C++.
    # scene: Actual evaluated rig retaining its independent native frame owner.
    # overlay: Shared ActivityOverlayState numeric identity, derived from genuine isolated CCP metadata.
    # overlay_elapsed_us: Active time since the observed phase changed, bounded to uint32.
    # description: Actual purpose or truthful phase caption in at most 192 printable ASCII characters.
    # description_elapsed_us: Active time since this exact description changed; no protocol freshness is renewed.
    # Raises ValueError for unsupported metadata; RuntimeError for real native preparation failures.
    def prepare_activity(self, scene, overlay, overlay_elapsed_us, description, description_elapsed_us):
        if type(overlay) is not int or overlay not in (0, 1, 2, 3, 4):
            raise ValueError("Native activity overlay identity is invalid")
        if (type(overlay_elapsed_us) is not int or not 0 <= overlay_elapsed_us <= 0xffffffff
                or type(description_elapsed_us) is not int or not 0 <= description_elapsed_us <= 0x7fffffffffffffff):
            raise ValueError("Native activity clocks are invalid")
        if (not isinstance(description, str) or len(description) > 192
                or any(not 32 <= ord(character) <= 126 for character in description)):
            raise ValueError("Native activity description must be bounded printable ASCII")
        handle = scene["_native_scene"].handle
        encoded = description.encode("ascii")
        checked("activity_overlay", self.api.cody_host_activity_overlay(handle, overlay, overlay_elapsed_us))
        checked("activity_description", self.api.cody_host_activity_description(
            handle, encoded, len(encoded), description_elapsed_us))

    # self: Persistent native policy owner, independent of transient rig frames.
    # phase: Genuine isolated hook phase or visibly SIMULATED preview state.
    # chat: Exact observed SHA-256 identity; empty is admitted only before current activity exists.
    # current: Whether this complete isolated link/activity observation is current.
    # active_time_us: Monotonic pausable UI clock, unaffected by text and route repaint.
    # Raises ValueError for unsupported fields; RuntimeError for original native policy errors.
    def observe_activity(self, phase, chat, current, active_time_us):
        phases = ("none", "active", "working", "editing", "running", "ready", "paused", "interrupted", "ended", "unavailable")
        if (phase not in phases or not isinstance(chat, str) or type(current) is not bool
                or type(active_time_us) is not int or not 0 <= active_time_us <= 0x7fffffffffffffff):
            raise ValueError("Native activity observation metadata is invalid")
        presented, elapsed = C.c_uint8(), C.c_int64()
        checked("activity_observe", self.api.cody_host_activity_observe(self.state, phases.index(phase),
            chat.encode("ascii"), current, active_time_us, C.byref(presented), C.byref(elapsed)))
        if presented.value >= len(phases):
            raise RuntimeError("Native activity policy returned an unknown phase")
        return phases[presented.value], elapsed.value

    # self: Persistent native observation owner while a genuine interaction owns the view.
    # Raises RuntimeError for original native state errors.
    def pause_activity(self):
        checked("activity_pause", self.api.cody_host_activity_pause(self.state))

    # self: Actual native identity raster transport, without Python text truncation or fading.
    # scene: Independent evaluated rig frame whose native owner borrows its identity buffers.
    # project: Actual bounded ASCII project label, empty when no context is available.
    # branch: Actual bounded ASCII checkout branch, empty when unavailable.
    # elapsed_us: Active time since the identity or attention state changed.
    # attention: Genuine paused/question state freezing the project caption.
    # operation_elapsed_us: Shared native observed work interval, negative when unavailable.
    # compacting: Authoritative compaction lifecycle; elapsed then means actual observed start duration.
    # Raises ValueError for unsupported metadata; RuntimeError for original native preparation errors.
    def prepare_activity_identity(self, scene, project, branch, elapsed_us, attention, operation_elapsed_us, compacting):
        if (any(not isinstance(value, str) or len(value) > 64 or
                    any(not 32 <= ord(character) <= 126 for character in value) for value in (project, branch))
                or type(elapsed_us) is not int or not 0 <= elapsed_us <= 0x7fffffffffffffff
                or type(attention) is not bool or type(compacting) is not bool or type(operation_elapsed_us) is not int):
            raise ValueError("Native activity identity metadata is invalid")
        checked("activity_identity", self.api.cody_host_activity_identity(scene["_native_scene"].handle,
            project.encode("ascii"), branch.encode("ascii"), elapsed_us, attention, operation_elapsed_us, compacting))

    # self: Shared native renderer transport owner.
    # current: Explicit Home=0/Activity=1/ModelContext=2/SessionLink=3 identity.
    # Raises ValueError for unknown identities; RuntimeError for the original native failure.
    def next_companion_panel(self, current):
        bounded_integer(current, 0, 3, "panel.current")
        selected = C.c_uint8()
        checked("panel_next", self.api.cody_host_panel_next(current, C.byref(selected)))
        return selected.value

    # self: Shared native renderer transport owner.
    # scene: Existing native frame retaining its bounded technical caption owner.
    # panel: Typed Home or technical page identity.
    # inputs: Original PC facts with independent source freshness, never a firmware snapshot.
    # elapsed_us: Nonnegative local presentation time for the indeterminate lifecycle accent.
    # Raises ValueError for invalid transport shape; RuntimeError for the native source/layout rejection.
    def prepare_companion_panel(self, scene, panel, inputs, elapsed_us):
        bounded_integer(panel, 0, 3, "panel.identity")
        bounded_integer(elapsed_us, 0, 0x7fffffffffffffff, "panel.elapsed_us")
        if not isinstance(inputs, NativePanelInputs):
            raise ValueError("Technical panels require the typed native metadata projection")
        checked("panel_prepare", self.api.cody_host_panel_prepare(scene["_native_scene"].handle,
            panel, C.byref(inputs), elapsed_us))

    # self: Shared native transport owner.
    # Raises RuntimeError for the actual native layout boundary error.
    def companion_panel_transform(self):
        transform = (C.c_float * 3)()
        checked("panel_transform", self.api.cody_host_panel_transform(transform))
        return tuple(transform)

    # self: Shared native transport owner.
    # scene: Existing native frame retaining its canonical-shaped PC-only source scratch.
    # panel: Explicit Home or selected technical identity.
    # observation: Original hook metadata and measured duration ages, with availability flags.
    # elapsed_us: Actual active presentation clock; never a fake compaction percentage.
    # Raises ValueError for invalid shape or clocks; RuntimeError for real native source/layout errors.
    def prepare_companion_observation(self, scene, panel, observation, elapsed_us):
        bounded_integer(panel, 0, 3, "panel.identity")
        bounded_integer(elapsed_us, 0, 0x7fffffffffffffff, "panel.elapsed_us")
        if not isinstance(observation, NativePanelObservation):
            raise ValueError("Technical panels require original typed hook observations")
        checked("panel_observation", self.api.cody_host_panel_observation(scene["_native_scene"].handle,
            panel, C.byref(observation), elapsed_us))

    # self: Persistent recognizer owner, isolated from physical snapshots.
    # x: Original logical acquired touch coordinate.
    # y: Original logical acquired touch coordinate.
    # pressed: Acquired contact or release state.
    # pressed_edge: Original new-contact edge.
    # sampled_at_us: Actual local acquisition time.
    # rotation: Current logical display rotation.
    # eligible: Passive content is visible and eligible.
    # consumed: Explicit action or other gesture owns this contact.
    # Raises ValueError for invalid original integers; RuntimeError for native rejection.
    def observe_companion_tap(self, x, y, pressed, pressed_edge, sampled_at_us, rotation, eligible, consumed):
        bounded_integer(x, -32768, 32767, "panel.tap.x")
        bounded_integer(y, -32768, 32767, "panel.tap.y")
        bounded_integer(sampled_at_us, 1, 0x7fffffffffffffff, "panel.tap.timestamp")
        bounded_integer(rotation, 0, 270, "panel.tap.rotation")
        if any(type(value) is not bool for value in (pressed, pressed_edge, eligible, consumed)):
            raise ValueError("Native passive tap requires original typed acquisition flags")
        accepted = C.c_bool()
        checked("panel_tap", self.api.cody_host_panel_tap(self.state, x, y, pressed, pressed_edge,
            sampled_at_us, rotation, eligible, consumed, C.byref(accepted)))
        return accepted.value

    # self: Persistent shared recognizer owner.
    # Raises RuntimeError for the original invalid-owner native error.
    def cancel_companion_tap(self):
        checked("panel_cancel_tap", self.api.cody_host_panel_cancel_tap(self.state))

    # self: Persistent shared motion recognizer owner.
    # x: Explicit SIMULATED acquired X acceleration in m/s2.
    # y: Explicit SIMULATED acquired Y acceleration in m/s2.
    # z: Explicit SIMULATED acquired Z acceleration in m/s2.
    # sampled_at_us: Original synthetic acquisition timestamp.
    # now_us: Local observation time, used by shared native freshness policy.
    # active: Visible awake companion navigation admission.
    # Raises ValueError for invalid clocks or flags; RuntimeError for native rejection.
    def observe_companion_shake(self, x, y, z, sampled_at_us, now_us, active):
        bounded_integer(sampled_at_us, 1, 0x7fffffffffffffff, "panel.shake.timestamp")
        bounded_integer(now_us, sampled_at_us, 0x7fffffffffffffff, "panel.shake.now")
        if type(active) is not bool or any(not math.isfinite(value) for value in (x, y, z)):
            raise ValueError("Native shake requires explicit finite acquired synthetic samples")
        accepted = C.c_bool()
        checked("panel_shake", self.api.cody_host_panel_shake(self.state, x, y, z,
            sampled_at_us, now_us, active, C.byref(accepted)))
        return accepted.value

    # self: Persistent shared motion recognizer owner.
    # now_us: Actual local clock at wake or view suspension.
    # Raises ValueError for invalid clocks; RuntimeError for original native rejection.
    def suspend_companion_shake(self, now_us):
        bounded_integer(now_us, 0, 0x7fffffffffffffff, "panel.shake.suspend")
        checked("panel_suspend_shake", self.api.cody_host_panel_suspend_shake(self.state, now_us))

    # self: PC-only presentation mode owner; physical settings and observations are untouched.
    # away: Explicit SIMULATED Away selection; false restores authored Desk behavior.
    # Raises ValueError for untyped flags; RuntimeError for the original native owner error.
    def set_away(self, away):
        if type(away) is not bool:
            raise ValueError("SIMULATED companion mode requires an explicit boolean")
        checked("set_away", self.api.cody_host_set_away(self.state, away))

    # self: Native facial transition owner.
    # state: Current independent channels.
    # target: Requested channels.
    # delta_ms: Nonnegative active elapsed time.
    # Raises RuntimeError for real firmware transition errors.
    def advance_face(self, state, target, delta_ms):
        if len(state) != len(self.rig['channels']) or len(target) != len(state):
            raise ValueError("Native facial transition requires two complete channel arrays")
        output = (C.c_float * len(state))()
        checked("advance_face", self.api.cody_host_advance_face(self.floats(state), self.floats(target), len(state), delta_ms, output))
        return list(output)

    # self: Native breathing and blink transport.
    # values: Current independent channel controls.
    # elapsed_ms: Nonnegative firmware-sized active clock.
    # Raises ValueError for unsupported clocks, RuntimeError for native errors.
    def animate(self, values, elapsed_ms):
        if len(values) != len(self.rig['channels']):
            raise ValueError("Native animation requires the exact channel count")
        if not math.isfinite(elapsed_ms) or not 0 <= elapsed_ms <= 0xffffffff:
            raise ValueError("Native animation clock must fit uint32 milliseconds")
        output = (C.c_float * len(values))()
        checked("animate_rig", self.api.cody_host_animate(self.floats(values), len(values), int(elapsed_ms), output))
        return list(output)

    # self: Native contour integration transport.
    # state: Current named controls and phase.
    # target: Requested named controls.
    # delta_ms: Nonnegative active elapsed time.
    # Raises RuntimeError for real firmware contour transition errors.
    def advance_contour(self, state, target, delta_ms):
        previous = self.floats([state[name] for name in self.contour_names] + [state["phase"]])
        requested = self.floats([target[name] for name in self.contour_names])
        output = (C.c_float * 5)()
        checked("advance_contour", self.api.cody_host_advance_contour(previous, requested, delta_ms, output))
        return dict(zip(self.contour_names + ["phase"], output))

    # self: Native canonical route-identity transport.
    # name: Exact authored scene name; rig means the ordinary CompanionIdle route.
    # Raises RuntimeError for missing or ambiguous authored identities.
    def scene_id(self, name):
        if name == "rig":
            name = "companion-idle"
        identity = C.c_uint16()
        checked("scene_id", self.api.cody_host_scene_id(name.encode("ascii"), C.byref(identity)))
        return identity.value

    # self: Native state owner retained across synthetic PC observations.
    # wifi_current: Synthetic validity of the Wi-Fi sample.
    # wifi_searching: Synthetic Wi-Fi discovery activity.
    # pc_current: Synthetic validity of the PC link sample.
    # pc_connected: Synthetic current authenticated PC connection.
    # pc_searching: Synthetic PC discovery activity.
    # active_delta_us: Nonnegative active elapsed time, zero while paused.
    # Raises RuntimeError for the actual shared model's validation errors.
    def connection_personality(self, wifi_current, wifi_searching, pc_current, pc_connected, pc_searching, active_delta_us):
        if any(type(value) is not bool for value in (wifi_current, wifi_searching, pc_current, pc_connected, pc_searching)):
            raise ValueError('Native connection facts require explicitly typed synthetic booleans')
        bounded_integer(active_delta_us, 0, 0x7fffffffffffffff, 'connection.active_delta_us')
        cue = C.c_uint8()
        checked("connection_personality", self.api.cody_host_connection(self.state, wifi_current, wifi_searching,
            pc_current, pc_connected, pc_searching, active_delta_us, C.byref(cue)))
        return PersonalityCue(cue.value)

    # self: Native scene reaction resolver.
    # scene: Exact authored scene name.
    # connection: Typed cue returned by the shared connection model.
    # Raises RuntimeError for unresolved route identity; ValueError for invalid cue identities.
    def scene_personality(self, scene, connection=PersonalityCue.NONE):
        cue = C.c_uint8()
        checked('scene_personality', self.api.cody_host_scene_cue(self.scene_id(scene),
            int(PersonalityCue(connection)), C.byref(cue)))
        return PersonalityCue(cue.value)

    # self: Native session selection owner; this never touches firmware NVS.
    # identity: Exact authored pearl skin ID selected by an explicit Studio action.
    # Raises RuntimeError for the original native catalog error.
    def select_skin(self, identity):
        checked('select_jelly_skin', self.api.cody_host_select_skin(identity.encode('ascii')))

    # self: Native manga request selector with no duplicated host priority logic.
    # requested: Typed synthetic baseline cue.
    # sleeping: Explicit semantic sleep permission.
    # affection: Whether accepted host caress lifetime remains active.
    # eye_reaction: Current typed pointer-derived eye reaction.
    # Raises ValueError for untyped input; RuntimeError for the native selector's actual error.
    def select_target(self, requested, sleeping, affection, eye_reaction=PersonalityCue.NONE):
        if not isinstance(requested, PersonalityCue) or not isinstance(eye_reaction, PersonalityCue):
            raise ValueError('Native personality selection requires typed cue identities')
        if type(sleeping) is not bool or type(affection) is not bool:
            raise ValueError('Native personality selection requires explicit permission/lifetime booleans')
        desired, release = C.c_uint8(), C.c_bool()
        checked('select_personality_target', self.api.cody_host_select_target(int(requested), sleeping, affection,
            int(eye_reaction), C.byref(desired), C.byref(release)))
        return PersonalityCue(desired.value), release.value

    # self: Native response model owner supplying copied inspector snapshots.
    # Raises RuntimeError for a native transport failure.
    def response_readout(self):
        output = (C.c_double * 39)()
        checked('response_readout', self.api.cody_host_response_readout(self.state, output, len(output)))
        timestamps = (C.c_int64 * 7)()
        checked('response_timestamps', self.api.cody_host_response_timestamps(self.state, timestamps, len(timestamps)))
        names = ('stress', 'fatigue', 'fatigue_target', 'fatigue_known', 'attention', 'speaking',
            'idle_seconds', 'idle_scene', 'idle_scene_weight', 'idle_scene_generation', 'annoyance',
            'protective_eye_closure', 'consumed_eye_tap_at_us', 'breath_phase', 'breath_rate_hz',
            'blink_elapsed_ms', 'yawn_seconds', 'yawn_cooldown_seconds', 'yawn_weight', 'yawning', 'idle_random_state')
        values = dict(zip(names, output[:21]))
        for name in ('idle_scene_generation', 'consumed_eye_tap_at_us', 'idle_random_state'):
            values[name] = int(values[name])
        values['consumed_eye_tap_at_us'] = timestamps[0]
        values['idle_scene'] = IdleSceneId(int(values['idle_scene']))
        values['fatigue_known'], values['yawning'] = bool(values['fatigue_known']), bool(values['yawning'])
        health = tuple(SampleHealth)
        for index, name in enumerate(('motion_status', 'power_status', 'microphone_status',
                'playback_status', 'eye_tap_status', 'body_touch_status')):
            offset = 21 + index * 3
            values[name] = SampleStamp(health[int(output[offset])], InputOrigin.SYNTHETIC,
                timestamps[index + 1], int(output[offset + 1]))
        # ResponseState is a UI value object here; its equations are never invoked.
        return ResponseState(**values)

    # self: Native response owner reset only by explicit Studio initialization/action.
    # seed: Nonzero artistic idle random seed; not a sensor value.
    # Raises RuntimeError for a rejected reset.
    def reset_response(self, seed):
        bounded_integer(seed, 1, 0xffffffff, 'response.seed')
        checked('response_reset', self.api.cody_host_response_reset(self.state, seed))
        return self.response_readout()

    # self: Native response owner crossing a route/pause boundary.
    # Raises RuntimeError for an unavailable native state.
    def rebase_response(self):
        checked('rebase_response', self.api.cody_host_response_rebase(self.state))
        return self.response_readout()

    # self: Native dynamics owner executing the actual firmware response math.
    # inputs: One synthetic driver observation retaining original sample timestamps/errors.
    # sensitivity: Explicit Studio sensitivity override; other gains use the compiled authored table.
    # delta_ms: Nonnegative active elapsed time.
    # now_us: Host monotonic freshness clock.
    # animate: Whether the simulated route advances breath/blink.
    # Raises ValueError for non-synthetic origins or invalid transport integers; RuntimeError for native errors.
    def advance_response(self, inputs, sensitivity, delta_ms, now_us, animate):
        from scripts.common.cody_response_contract import Axis
        samples = (inputs.motion, inputs.power, inputs.microphone, inputs.playback, inputs.eye_tap, inputs.body_touch)
        if any(sample.stamp.origin != InputOrigin.SYNTHETIC for sample in samples):
            raise ValueError('PC response transport accepts explicitly SYNTHETIC driver observations only')
        for sample in samples:
            bounded_integer(sample.stamp.error, -0x80000000, 0x7fffffff, 'sample.error')
            bounded_integer(sample.stamp.sampled_at_us, 0, 0x7fffffffffffffff, 'sample.sampled_at_us')
        for value in (*inputs.microphone.mean_level, *inputs.microphone.peak_level,
                inputs.playback.mean_level, inputs.playback.cue_id):
            bounded_integer(value, 0, 0xffffffff, 'audio.value')
        bounded_integer(inputs.microphone.channel_count, 0, 2, 'microphone.channel_count')
        bounded_integer(inputs.last_interaction_at_us, 0, 0x7fffffffffffffff, 'last_interaction_at_us')
        bounded_integer(inputs.body_touch.gesture_started_at_us, 0, 0x7fffffffffffffff, 'gesture_started_at_us')
        bounded_integer(now_us, 0, 0x7fffffffffffffff, 'response.now_us')
        if inputs.rendering_rotation_degrees not in (0, 90, 180, 270) or inputs.body_touch.rendering_rotation_degrees not in (0, 90, 180, 270):
            raise ValueError('Native response requires shared cardinal rotations')
        if any(type(value) is not bool for value in (inputs.power.charging, inputs.playback.active,
                inputs.body_touch.pressed, inputs.idle_scene_allowed, animate)):
            raise ValueError('Native response admission fields require actual booleans')
        health = {value: index for index, value in enumerate(SampleHealth)}
        wire = HostResponseInputs()
        wire.health[:] = [health[sample.stamp.health] for sample in samples]
        wire.errors[:] = [sample.stamp.error for sample in samples]
        wire.timestamps[:] = [sample.stamp.sampled_at_us for sample in samples]
        wire.last_interaction, wire.gesture_started = inputs.last_interaction_at_us, inputs.body_touch.gesture_started_at_us
        wire.acceleration[:] = [inputs.motion.acceleration_mps2[axis] for axis in Axis]
        wire.power, wire.drag[:] = inputs.power.level_percent, inputs.body_touch.displacement_px
        wire.microphone_mean[:], wire.microphone_peak[:] = inputs.microphone.mean_level, inputs.microphone.peak_level
        wire.playback_mean, wire.playback_cue = inputs.playback.mean_level, inputs.playback.cue_id
        wire.rotation, wire.touch_rotation = inputs.rendering_rotation_degrees, inputs.body_touch.rendering_rotation_degrees
        wire.microphone_count = inputs.microphone.channel_count
        wire.charging, wire.playback_active = inputs.power.charging, inputs.playback.active
        wire.pressed, wire.idle_allowed = inputs.body_touch.pressed, inputs.idle_scene_allowed
        wire.eye = tuple(EyeId).index(inputs.eye_tap.eye)
        checked('advance_response', self.api.cody_host_response_advance(self.state, C.byref(wire),
            sensitivity, delta_ms, now_us, animate))
        return self.response_readout()

    # self: Native response composer operating on the previously committed model.
    # values: Independent authored face controls.
    # targets: Independent contour targets.
    # placement: Resting logical x/y/scale.
    # rotation: Shared cardinal orientation.
    # animate: Whether the frame includes native breathing/blink.
    # Raises ValueError for boundary-array lengths; RuntimeError for native composition errors.
    def compose_response(self, values, targets, placement, rotation, animate):
        if len(values) != len(self.rig['channels']) or len(placement) != 3:
            raise ValueError('Native response composition requires complete channels and x/y/scale')
        count = len(values)
        output = (C.c_float * (count + 14))()
        checked('compose_response', self.api.cody_host_response_compose(self.state, self.floats(values), count,
            self.floats([targets[name] for name in self.contour_names]), self.floats(placement), rotation, animate, output, len(output)))
        offset = count + 4
        return ResponseFrame(list(output[:count]), dict(zip(self.contour_names, output[count:offset])),
            tuple(output[offset:offset + 3]), tuple(output[offset + 3:offset + 5]), tuple(output[offset + 5:offset + 7]),
            tuple(output[offset + 7:offset + 9]), output[offset + 9])

    # self: Native hit-testing transport using exactly the displayed frame.
    # scene: Inspector dictionary retaining its native RigScene.
    # point: Logical pointer x/y after the shared display rotation mapping.
    # radius: Authored normalized eye hit radius.
    # Raises ValueError for pointer length; RuntimeError for real native hit errors.
    def hit(self, scene, point, radius):
        if len(point) != 2:
            raise ValueError('Native hit test requires logical x/y')
        eye, body = C.c_uint8(), C.c_bool()
        checked('hit_test', self.api.cody_host_hit(scene['_native_scene'].handle, *point, radius, C.byref(eye), C.byref(body)))
        return tuple(EyeId)[eye.value], body.value

    # self: Native scene owner selecting the shared timing/layout code.
    # scene: Exact authored scene name.
    # elapsed_ms: Nonnegative active scene clock.
    # length: Optional content line length.
    # preceding: Earlier title/body characters in the same sequence.
    # Raises ValueError for unrepresentable clocks/counts; RuntimeError for native scene errors.
    def scene_timing(self, scene, elapsed_ms, length=0, preceding=0):
        if not math.isfinite(elapsed_ms) or not 0 <= elapsed_ms <= 0x7fffffffffffffff / 1000:
            raise ValueError('Native scene timing must fit nonnegative int64 microseconds')
        if any(type(value) is not int or not 0 <= value <= 65535 for value in (length, preceding)):
            raise ValueError('Native scene content counts must fit uint16')
        output = (C.c_float * 8)()
        checked('scene_timing', self.api.cody_host_scene_timing(self.scene_id(scene), round(elapsed_ms * 1000),
            length, preceding, output))
        return tuple(output)

    # self: PC graphics transport owner; text pixels come from shared C++ glyph/raster code.
    # image: Existing logical RGB framebuffer, updated in place after successful native draw.
    # text: Bounded printable ASCII line.
    # top: Logical top row.
    # color: Native RGB565 ink.
    # body: Select the actual shared caption font.
    # visible: Optional complete visible prefix count.
    # center_x: Horizontal center of the authored content slot.
    # max_width: Maximum line width for the circular chord.
    # Raises ValueError for invalid transport data; RuntimeError for native layout/glyph errors.
    def paint_text(self, image, text, top, color=65535, *, body=False, visible=None, center_x=233, max_width=None):
        if image.mode != 'RGB' or image.size != (466, 466) or not isinstance(text, str):
            raise ValueError('Native text requires a 466x466 RGB framebuffer and ASCII string')
        if any(not 32 <= ord(character) <= 126 for character in text) or len(text) > 65535:
            raise ValueError('Native text requires bounded printable ASCII')
        if not text:
            return
        visible = len(text) if visible is None else visible
        max_width = (232 if body else 320) if max_width is None else max_width
        if any(type(value) is not int or not 0 <= value <= 65535 for value in (top, color, visible, center_x, max_width)):
            raise ValueError('Native text geometry/counts/color must fit uint16')
        if not 0 <= visible <= len(text):
            raise ValueError('Native text visible prefix exceeds its bounded content')
        pixels = (C.c_uint16 * (466 * 466))()
        for index, (red, green, blue) in enumerate(image.getdata()):
            pixels[index] = (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3
        checked('draw_text_rows', self.api.cody_host_text(pixels, len(pixels), text.encode('ascii'),
            len(text), top, color, body, visible, center_x, max_width))
        output = bytearray(len(pixels) * 3)
        for index, value in enumerate(pixels):
            output[index * 3] = ((value >> 11) & 31) * 255 // 31
            output[index * 3 + 1] = ((value >> 5) & 63) * 255 // 63
            output[index * 3 + 2] = (value & 31) * 255 // 31
        image.paste(Image.frombytes('RGB', (466, 466), bytes(output)))

    # self: PC framebuffer transport using the actual shared RGB565 scene fade.
    # image: Complete logical RGB framebuffer updated after successful native composition.
    # opacity: Explicit scene opacity in [0,255], never silently clamped.
    # Raises ValueError for invalid transport data; RuntimeError for original native fade errors.
    def fade_to_black(self, image, opacity):
        if image.mode != 'RGB' or image.size != (466, 466):
            raise ValueError('Native fade requires a 466x466 RGB framebuffer')
        bounded_integer(opacity, 0, 255, 'fade.opacity')
        pixels = (C.c_uint16 * (466 * 466))()
        for index, (red, green, blue) in enumerate(image.getdata()):
            pixels[index] = (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3
        checked('fade_to_black', self.api.cody_host_fade(pixels, len(pixels), opacity))
        output = bytearray(len(pixels) * 3)
        for index, value in enumerate(pixels):
            output[index * 3] = ((value >> 11) & 31) * 255 // 31
            output[index * 3 + 1] = ((value >> 5) & 63) * 255 // 63
            output[index * 3 + 2] = (value & 31) * 255 // 31
        image.paste(Image.frombytes('RGB', (466, 466), bytes(output)))

    # self: PC driver transport selecting the actual shared CODY2D capsule raster.
    # image: Logical RGB framebuffer updated only after successful native drawing.
    # bounds: Driver layout x/y/width/height within the authored circle.
    # label: Complete printable ASCII control caption, never part of teletype timing.
    # tone: Driver semantic palette choice, Neutral/Positive/Negative/Info (0..3).
    # enabled: Explicit action admission state.
    # Raises ValueError for invalid driver data; RuntimeError for real C++ layout/raster errors.
    def paint_action(self, image, bounds, label, tone, enabled=True):
        if image.mode != 'RGB' or image.size != (466, 466) or len(bounds) != 4:
            raise ValueError('Native action requires a logical RGB framebuffer and four bounds values')
        if any(type(value) is not int or not 0 <= value <= 466 for value in bounds) or type(enabled) is not bool:
            raise ValueError('Native action bounds/admission rejected')
        if int(tone) not in (0, 1, 2, 3):
            raise ValueError('Native action tone must be Neutral/Positive/Negative/Info')
        # Driver composition supplies theme/layout data; all coverage, gradient, border,
        # icon and glyph pixel operations execute unchanged CODY2D production functions.
        palettes = ((0x18E5, 0x10A3, 0x4A8C, 0xBDF7), (0x1147, 0x08C4, 0x2B4F, 0x8F1D),
            (0x28A4, 0x1883, 0x796B, 0xED53), (0x1106, 0x08A4, 0x3291, 0xA71F))
        x, y, width, height = bounds
        card = height > 80
        icon_x = x + width // 2 if card else x + 31
        icon_y = y + 33 if card else y + (height - 25) // 2 + 25 // 2
        icons = (NativeActionIcon.ARROW, NativeActionIcon.CHECK, NativeActionIcon.CLOSE, NativeActionIcon.ARROW)
        pixels = (C.c_uint16 * (466 * 466))()
        for index, (red, green, blue) in enumerate(image.getdata()):
            pixels[index] = (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3
        checked('draw_capsule_rows', self.api.cody_host_capsule(pixels, len(pixels), (C.c_uint16 * 4)(*bounds),
            (C.c_uint16 * 4)(*palettes[int(tone) if enabled else 0]), icons[int(tone)], icon_x, icon_y,
            28 if card else 0, enabled))
        output = bytearray(len(pixels) * 3)
        for index, value in enumerate(pixels):
            output[index * 3] = ((value >> 11) & 31) * 255 // 31
            output[index * 3 + 1] = ((value >> 5) & 63) * 255 // 63
            output[index * 3 + 2] = (value & 31) * 255 // 31
        prepared = Image.frombytes('RGB', (466, 466), bytes(output))
        center = x + width // 2 if card else x + (width + 40) // 2
        top = y + 64 if card else y + (height - 25) // 2
        caption_width = width - (20 if card else 64)
        if card and len(label) > caption_width // 11:
            # Driver composition preserves complete choice labels on two lines.
            # Each line still uses the actual native font and its chord checks.
            limit = caption_width // 11
            split = label.rfind(' ', 0, limit + 1)
            split = split if split > 0 and len(label) - split - 1 <= limit else limit
            first = label[:split]
            second = label[split + 1:] if label[split:split + 1] == ' ' else label[split:]
            if len(first) > limit or len(second) > limit:
                raise ValueError(f'Native action caption does not fit two complete lines: {label!r}')
            self.paint_text(prepared, first, y + 48, body=True, center_x=center, max_width=caption_width)
            self.paint_text(prepared, second, y + 75, body=True, center_x=center, max_width=caption_width)
        else:
            self.paint_text(prepared, label, top, body=True, center_x=center, max_width=caption_width)
        image.paste(prepared)


class NativePersonalityPainter:
    # self: Interface adapter retaining the Studio's inspector/audio properties.
    # native: Actual C++ model and frame transport.
    def __init__(self, native):
        self.native = native
        self.sampled_us = 0
        self.last_frame = PersonalityFrame()
        self.channel = SimpleNamespace(cue=PersonalityCue.NONE)
        self.active = False

    # self: Native personality channel owner.
    # scene: Actual evaluated native rig frame.
    # cue: Typed requested manga identity.
    # nominal_scale: Authored character size before breathing.
    # screen: Exact scene name.
    # rotation: Shared logical cardinal rotation.
    # elapsed_ms: Active cue clock; excludes pause/standby.
    # exclusions: Reserved half-open chrome rectangles.
    # body_top: Legacy study metadata, unused by the real native geometry model.
    # presentation: Native Disabled/Main/Sleep permission.
    # paused: Freeze channel and follow time.
    # Raises RuntimeError for native geometry/identity errors.
    def prepare(self, scene, cue, nominal_scale, screen, rotation, elapsed_ms, exclusions, body_top=None,
                *, presentation=PersonalityPresentation.MAIN, paused=False):
        if not math.isfinite(elapsed_ms) or not 0 <= elapsed_ms <= 0x7fffffffffffffff / 1000:
            raise ValueError(f'Native personality clock rejected: {elapsed_ms!r}')
        if rotation not in (0, 90, 180, 270) or not isinstance(presentation, PersonalityPresentation):
            raise ValueError('Native personality requires a checked presentation and cardinal rotation')
        if len(exclusions) > 6 or any(len(rectangle) != 4 for rectangle in exclusions):
            raise ValueError('Native personality permits at most six complete exclusion rectangles')
        for rectangle in exclusions:
            for value in rectangle:
                bounded_integer(value, 0, 466, 'personality.exclusion')
        current_us = round(elapsed_ms * 1000)
        delta_us = max(0, current_us - self.sampled_us)
        self.sampled_us = current_us
        output = (C.c_uint16 * 7)()
        bounds = (C.c_uint16 * (len(exclusions) * 4))(*(value for rectangle in exclusions for value in rectangle))
        checked("prepare_personality_frame", self.native.api.cody_host_personality(self.native.state,
            scene["_native_scene"].handle, int(cue), nominal_scale, self.native.scene_id(screen), rotation,
            int(presentation), delta_us, paused, bounds, len(exclusions), output))
        self.last_frame = PersonalityFrame(PersonalityCue(output[0]), output[1], output[2],
            output[3] - output[1], output[4] - output[2], output[5], PersonalityPresentationStatus(output[6]))
        self.channel.cue = self.last_frame.cue
        self.active = self.last_frame.cue != PersonalityCue.NONE
        return self.last_frame

    # self: Native channel owner whose sprite has already been drawn by draw_rig_rows.
    # image: Native RGB565-expanded output, unchanged at this adapter boundary.
    # frame: Native frame metadata shared with the audio/UI inspectors.
    def paint(self, image, frame):
        return None

    # self: Native channel owner explicitly leaving a presentation.
    # elapsed_ms: Current active clock retained for the next model update.
    def disable(self, elapsed_ms):
        self.native.api.cody_host_personality_clear(self.native.state)
        self.sampled_us = round(elapsed_ms * 1000)
        self.last_frame = PersonalityFrame()
        self.channel.cue = PersonalityCue.NONE
        self.active = False
