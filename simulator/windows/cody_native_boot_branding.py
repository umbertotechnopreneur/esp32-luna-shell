"""Artistic boot preview using the firmware's actual decoder, typography and row compositor."""
from __future__ import annotations

import ctypes as C
import threading
import weakref

from PIL import Image

from cody_native_runtime import load_runtime_library


# operation: Actual native preparation, clock or raster operation.
# error: Original ESP error returned by the shared production logic.
# Raises RuntimeError for a real failure; no Pillow typography or artwork fallback is supplied.
def _checked(operation, error):
    if error:
        raise RuntimeError(f"Cody boot branding {operation} failed: esp_err_t=0x{error & 0xffffffff:08x}")


class NativeBootBranding:
    # self: Independent serialized host renderer with heap-owned row scratch and RGB565 output.
    # Raises original resource, runtime provenance, font or allocation errors.
    def __init__(self):
        self.api = load_runtime_library()
        self.lock = threading.RLock()
        pointer = C.c_void_p
        signatures = {
            "create": (C.c_int32, (C.POINTER(pointer),)),
            "destroy": (None, (pointer,)),
            "frame_index": (C.c_int32, (C.c_double, C.POINTER(C.c_uint16))),
            "frame": (C.c_int32, (pointer, C.c_uint16, C.POINTER(C.c_uint16), C.c_size_t)),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.api, "cody_host_boot_branding_" + name)
            function.restype, function.argtypes = result, arguments
        handle = pointer()
        _checked("create", self.api.cody_host_boot_branding_create(C.byref(handle)))
        if not handle.value:
            raise RuntimeError("Native boot branding initialized without an owner")
        self.handle = handle
        try:
            self.release = weakref.finalize(self, self.api.cody_host_boot_branding_destroy, handle)
        except BaseException:
            self.api.cody_host_boot_branding_destroy(handle)
            raise
        self.pixels = (C.c_uint16 * (466 * 466))()

    # self: Serialized host renderer sharing the compiled GIF's original timing table.
    # elapsed_ms: Explicit artistic animation clock; no device measurement is inferred.
    # Raises TypeError or native rejection for invalid/nonfinite/negative clocks.
    # Raises OverflowError if the artistic clock cannot cross the double-precision ABI.
    def frame_index(self, elapsed_ms):
        if type(elapsed_ms) not in (int, float):
            raise TypeError("Boot preview time must be a number")
        with self.lock:
            index = C.c_uint16()
            _checked("frame clock", self.api.cody_host_boot_branding_frame_index(elapsed_ms, C.byref(index)))
            return index.value

    # self: Serialized owner, retaining RGB565 staging on the host heap.
    # elapsed_ms: Explicit artistic preview time, independent of all sensor state.
    # Raises original native clock or compositor errors without substituting an image.
    def image(self, elapsed_ms):
        with self.lock:
            index = self.frame_index(elapsed_ms)
            _checked("frame", self.api.cody_host_boot_branding_frame(self.handle, index,
                self.pixels, len(self.pixels)))
            # Expand the actual RGB565 output exactly as the existing native rig
            # transport does; no font or artwork placement is repeated in Python.
            rgb = bytearray(len(self.pixels) * 3)
            for pixel, value in enumerate(self.pixels):
                rgb[pixel * 3] = ((value >> 11) & 31) * 255 // 31
                rgb[pixel * 3 + 1] = ((value >> 5) & 63) * 255 // 63
                rgb[pixel * 3 + 2] = (value & 31) * 255 // 31
            return Image.frombytes("RGB", (466, 466), bytes(rgb))
