"""Transport a retired native device framebuffer to the host window.

This module does not draw, resize, or manufacture device observations. The
native display owner must first receive and retire a complete device frame.
"""

import ctypes

from cody_native_rgb565 import LOGICAL_PIXEL_COUNT, native_rgb565_to_image
from cody_native_runtime import load_runtime_library


class NativeDisplayError(RuntimeError):
    """Preserve the native display owner's exact error for the host caller."""

    def __init__(self, error):
        self.error = error
        super().__init__(f"Native display frame unavailable: esp_err_t={error}")


class StudioFrameIdentity(ctypes.Structure):
    _fields_ = [("abi", ctypes.c_uint32), ("frame_revision", ctypes.c_uint32),
                ("at_ms", ctypes.c_uint32), ("source_animation_sample_ms", ctypes.c_uint32),
                ("call_count", ctypes.c_uint32), ("target", ctypes.c_char * 48),
                ("scenario_revision", ctypes.c_char * 65), ("rendering_mode", ctypes.c_char * 24),
                ("source_id", ctypes.c_uint64), ("chat_id", ctypes.c_uint64), ("turn_id", ctypes.c_uint64),
                ("call_ids", ctypes.c_uint64 * 3), ("service_error", ctypes.c_int32),
                ("service_outcome", ctypes.c_char * 24)]


class StudioRouteReceipt(ctypes.Structure):
    _fields_ = [("abi", ctypes.c_uint32), ("origin_frame_revision", ctypes.c_uint32),
                ("destination_frame_revision", ctypes.c_uint32), ("origin_scenario_revision", ctypes.c_char * 65),
                ("route_id", ctypes.c_char * 32)]


class StudioWorkerReceipt(ctypes.Structure):
    _fields_ = [("abi", ctypes.c_uint32), ("frame_revision", ctypes.c_uint32),
                ("requested_ms", ctypes.c_uint32), ("frame_at_ms", ctypes.c_uint32),
                ("tick_count", ctypes.c_uint32), ("target", ctypes.c_char * 48),
                ("scenario_revision", ctypes.c_char * 65)]


# identity: Same-frame native receipt copied under the display owner's lock.
# Raises ValueError or UnicodeDecodeError for an unsupported or corrupt receipt.
def decode_studio_identity(identity):
    target = identity.target.decode("ascii")
    revision = identity.scenario_revision.decode("ascii")
    mode = identity.rendering_mode.decode("ascii")
    if (identity.abi != 2 or identity.frame_revision == 0 or not target.startswith("cody.") or
            mode not in ("runtime-full", "presenter-isolated") or identity.call_count > 3 or
            len(revision) != 64 or any(character not in "0123456789abcdef" for character in revision)):
        raise ValueError("Invalid native Studio completed-frame identity")
    return {
        "abi": identity.abi, "target": target, "scenario_revision": revision, "at_ms": identity.at_ms,
        "revision": identity.frame_revision, "frame_revision": identity.frame_revision,
        "width": 466, "height": 466, "rendering_mode": mode,
        "source_animation_sample_ms": identity.source_animation_sample_ms,
        "call_count": identity.call_count,
        "context": {"source_id": f"{identity.source_id:016x}", "chat_id": f"{identity.chat_id:016x}",
                    "turn_id": f"{identity.turn_id:016x}",
                    "call_ids": [f"{identity.call_ids[index]:016x}" for index in range(identity.call_count)]},
        "service_error": identity.service_error, "service_outcome": identity.service_outcome.decode("ascii"),
        "clock_mode": "static-settled", "deterministic": True,
    }


# No parameters: reads pixels and semantic identity from one native completed-frame lock.
# Raises native transport, ABI, encoding or identity errors without guessing a route.
def completed_studio_frame():
    if ctypes.sizeof(StudioFrameIdentity) != 240:
        raise ValueError("Unsupported host Studio frame identity ABI geometry")
    api = load_runtime_library("graphics-device")
    native_size = api.cody_host_studio_frame_identity_size
    native_size.argtypes, native_size.restype = [], ctypes.c_size_t
    if native_size() != ctypes.sizeof(StudioFrameIdentity):
        raise ValueError("Native Studio frame identity capacity differs from this transport; restart/update Studio")
    snapshot = api.cody_host_display_studio_snapshot
    snapshot.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_size_t,
                         ctypes.POINTER(StudioFrameIdentity), ctypes.c_size_t]
    snapshot.restype = ctypes.c_int
    pixels = (ctypes.c_uint16 * LOGICAL_PIXEL_COUNT)()
    identity = StudioFrameIdentity()
    error = snapshot(pixels, LOGICAL_PIXEL_COUNT, ctypes.byref(identity), ctypes.sizeof(identity))
    if error:
        raise NativeDisplayError(error)
    return native_rgb565_to_image(pixels), decode_studio_identity(identity)


# No parameters: atomically copies native destination pixels and observed route evidence.
# Raises native, ABI, encoding or identity errors without fabricating a transition.
def completed_studio_route_frame():
    if ctypes.sizeof(StudioFrameIdentity) != 240:
        raise ValueError("Unsupported host Studio frame identity ABI geometry")
    api = load_runtime_library("graphics-device")
    identity_size = api.cody_host_studio_frame_identity_size
    identity_size.argtypes, identity_size.restype = [], ctypes.c_size_t
    route_size = api.cody_host_studio_route_receipt_size
    route_size.argtypes, route_size.restype = [], ctypes.c_size_t
    if identity_size() != ctypes.sizeof(StudioFrameIdentity) or route_size() != ctypes.sizeof(StudioRouteReceipt):
        raise ValueError("Native Studio route output capacity differs from this transport")
    snapshot = api.cody_host_display_studio_route_snapshot
    snapshot.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_size_t, ctypes.POINTER(StudioFrameIdentity),
                         ctypes.c_size_t, ctypes.POINTER(StudioRouteReceipt), ctypes.c_size_t]
    snapshot.restype = ctypes.c_int
    pixels = (ctypes.c_uint16 * LOGICAL_PIXEL_COUNT)()
    identity, route = StudioFrameIdentity(), StudioRouteReceipt()
    error = snapshot(pixels, LOGICAL_PIXEL_COUNT, ctypes.byref(identity), ctypes.sizeof(identity),
                     ctypes.byref(route), ctypes.sizeof(route))
    if error:
        raise NativeDisplayError(error)
    decoded = decode_studio_identity(identity)
    observed = {"abi": route.abi, "origin_frame_revision": route.origin_frame_revision,
                "destination_frame_revision": route.destination_frame_revision,
                "origin_scenario_revision": route.origin_scenario_revision.decode("ascii"),
                "route_id": route.route_id.decode("ascii")}
    if (observed["abi"] != 1 or not 0 < observed["origin_frame_revision"] < decoded["revision"] or
            observed["destination_frame_revision"] != decoded["revision"] or
            len(observed["origin_scenario_revision"]) != 64 or
            any(character not in "0123456789abcdef" for character in observed["origin_scenario_revision"]) or
            not observed["route_id"] or len(observed["route_id"]) > 31 or
            any(character not in "abcdefghijklmnopqrstuvwxyz0123456789-" for character in observed["route_id"])):
        raise ValueError("Invalid native observed-route receipt")
    return native_rgb565_to_image(pixels), decoded, observed


# No parameters: copy a worker acknowledgement and its completed frame atomically.
# Raises native, capacity or identity errors without guessing a retirement time.
def completed_studio_worker_frame():
    api = load_runtime_library("graphics-device")
    frame_size = api.cody_host_studio_frame_identity_size
    frame_size.argtypes, frame_size.restype = [], ctypes.c_size_t
    receipt_size = api.cody_host_studio_worker_receipt_size
    receipt_size.argtypes, receipt_size.restype = [], ctypes.c_size_t
    if frame_size() != ctypes.sizeof(StudioFrameIdentity) or receipt_size() != ctypes.sizeof(StudioWorkerReceipt):
        raise ValueError("Native Studio worker output capacity differs from this transport")
    snapshot = api.cody_host_display_studio_worker_snapshot
    snapshot.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_size_t, ctypes.POINTER(StudioFrameIdentity),
                         ctypes.c_size_t, ctypes.POINTER(StudioWorkerReceipt), ctypes.c_size_t]
    snapshot.restype = ctypes.c_int
    pixels = (ctypes.c_uint16 * LOGICAL_PIXEL_COUNT)()
    identity, receipt = StudioFrameIdentity(), StudioWorkerReceipt()
    error = snapshot(pixels, LOGICAL_PIXEL_COUNT, ctypes.byref(identity), ctypes.sizeof(identity),
                     ctypes.byref(receipt), ctypes.sizeof(receipt))
    if error:
        raise NativeDisplayError(error)
    decoded = decode_studio_identity(identity)
    decoded["clock_mode"] = "controlled-worker"
    decoded["deterministic_scope"] = "display-worker"
    observed = {"abi": receipt.abi, "target": receipt.target.decode("ascii"),
                "scenario_revision": receipt.scenario_revision.decode("ascii"),
                "requested_ms": receipt.requested_ms, "frame_at_ms": receipt.frame_at_ms,
                "tick_count": receipt.tick_count, "frame_revision": receipt.frame_revision}
    if (observed["abi"] != 1 or observed["frame_revision"] != decoded["revision"] or
            observed["target"] != decoded["target"] or observed["scenario_revision"] != decoded["scenario_revision"] or
            observed["frame_at_ms"] != decoded["at_ms"] or observed["frame_at_ms"] > observed["requested_ms"] or
            observed["tick_count"] == 0):
        raise ValueError("Invalid native display-worker acknowledgement")
    return native_rgb565_to_image(pixels), decoded, observed


# api: Optional already verified runtime for live preview; captures reverify provenance.
# Raises RuntimeError when the owner has not yet published a valid frame or fails.
def completed_frame(api=None):
    if api is None:
        api = load_runtime_library("graphics-device")
    snapshot = api.cody_host_display_snapshot
    snapshot.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_size_t,
                         ctypes.POINTER(ctypes.c_uint32)]
    snapshot.restype = ctypes.c_int
    pixels = (ctypes.c_uint16 * LOGICAL_PIXEL_COUNT)()
    revision = ctypes.c_uint32()
    error = snapshot(pixels, LOGICAL_PIXEL_COUNT, ctypes.byref(revision))
    if error:
        raise NativeDisplayError(error)
    return native_rgb565_to_image(pixels), revision.value
