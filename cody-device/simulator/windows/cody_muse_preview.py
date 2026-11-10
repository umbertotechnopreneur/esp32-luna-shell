"""Studio transport for the Muse character rendered by Cody's native avatar owner."""

from array import array
import ctypes
import sys

from PIL import Image

from cody_native_runtime import load_runtime_library


DISPLAY_SIZE = 320
NO_REACTION = (1 << 32) - 1
SCREEN_OFFSETS_MS = {"boot-sleep": 0, "boot-waking": 900, "boot-ready": 1500}


class NativeMusePreview:
    # self: Tk-thread owner for the native avatar frame and its reusable RGB565 output.
    # Raises the original native loader or export resolution error.
    def __init__(self):
        self.render = load_runtime_library().cody_host_muse_frame
        self.render.argtypes = (
            ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint16), ctypes.c_size_t,
        )
        self.render.restype = ctypes.c_int32
        self.pixels = (ctypes.c_uint16 * (DISPLAY_SIZE * DISPLAY_SIZE))()

    # self: Studio owner calling the native renderer from its serialized UI thread.
    # time_ms: Active animation time since startup.
    # reaction_ms: Time since the last accepted touch, or NO_REACTION.
    # Raises RuntimeError if the production renderer rejects the output surface.
    def image(self, time_ms, reaction_ms=NO_REACTION):
        error = self.render(time_ms, reaction_ms, self.pixels, len(self.pixels))
        if error:
            raise RuntimeError(f"Muse avatar raster rejected: esp_err_t=0x{error & 0xffffffff:08x}")
        # Pillow uses BGR;16 for little-endian RGB565 with masks F800/07E0/001F.
        pixels = bytes(self.pixels)
        if sys.byteorder == "big":
            words = array("H")
            words.frombytes(pixels)
            words.byteswap()
            pixels = words.tobytes()
        return Image.frombytes("RGB", (DISPLAY_SIZE, DISPLAY_SIZE), pixels, "raw", "BGR;16")
