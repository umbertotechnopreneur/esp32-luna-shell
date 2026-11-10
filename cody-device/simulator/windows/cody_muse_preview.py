"""Studio transport for the Muse character rendered by Cody's native avatar owner."""

from array import array
import ctypes
from enum import IntEnum
import sys

from PIL import Image

from cody_native_runtime import load_runtime_library


DISPLAY_SIZE = 320
NO_REACTION = (1 << 32) - 1
SCREEN_OFFSETS_MS = {"boot-sleep": 0, "boot-waking": 900, "boot-ready": 1500}


class MusePose(IntEnum):
    IDLE = 1  # CODY::PixelAvatarPose::Idle in the host renderer ABI.


class MuseEyeStyle(IntEnum):
    AUTOMATIC = 0
    NORMAL = 1
    WIDE = 2
    HAPPY = 3
    CROSS = 4
    CLOSED = 5


class MuseMouthStyle(IntEnum):
    AUTOMATIC = 0
    SMILE = 1
    ROUND = 2
    THOUGHTFUL = 3
    TALKING = 4
    GRIN = 5
    FLAT = 6


class NativeMusePreview:
    # self: Tk-thread owner for the native avatar frame and its reusable RGB565 output.
    # Raises the original native loader or export resolution error.
    def __init__(self):
        library = load_runtime_library()
        self.reset_native = library.cody_host_muse_reset
        self.reset_native.argtypes = (ctypes.c_uint32,)
        self.reset_native.restype = None
        self.reset_motion_native = library.cody_host_muse_motion_reset
        self.reset_motion_native.argtypes = ()
        self.reset_motion_native.restype = None
        self.motion_native = library.cody_host_muse_motion
        self.motion_native.argtypes = (
            ctypes.c_float, ctypes.c_float, ctypes.c_int64, ctypes.c_int64,
            ctypes.c_uint16, ctypes.c_bool,
        )
        self.motion_native.restype = None
        self.render = library.cody_host_muse_frame
        self.render.argtypes = (
            ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint16), ctypes.c_size_t,
        )
        self.render.restype = ctypes.c_int32
        self.render_face = library.cody_host_muse_face_frame
        self.render_face.argtypes = (
            ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32,
            ctypes.c_float, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint16),
            ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32,
            ctypes.c_float, ctypes.c_uint32,
            ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float,
            ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float,
        )
        self.render_face.restype = ctypes.c_int32
        self.pixels = (ctypes.c_uint16 * (DISPLAY_SIZE * DISPLAY_SIZE))()

    # self: Serialized owner of the shared native showcase.
    # seed: Artistic sequence seed for this simulated boot.
    def reset(self, seed):
        self.reset_native(seed)

    # self: Serialized host owner of synthetic motion response.
    def reset_motion(self):
        self.reset_motion_native()

    # self: Serialized host owner; values are simulated and never sent to firmware.
    # acceleration_x: Simulated board X acceleration in m/s2.
    # acceleration_y: Simulated board Y acceleration in m/s2.
    # sampled_at_us: Original synthetic sample time.
    # now_us: Current host monotonic time.
    # rotation: Applied display rotation.
    # valid: Whether the synthetic sample is available.
    def motion(self, acceleration_x, acceleration_y, sampled_at_us, now_us, rotation, valid):
        self.motion_native(acceleration_x, acceleration_y, sampled_at_us, now_us, rotation, valid)

    # self: Studio owner calling the native renderer from its serialized UI thread.
    # time_ms: Active animation time since startup.
    # reaction_ms: Time since the last accepted touch, or NO_REACTION.
    # Raises RuntimeError if the production renderer rejects the output surface.
    def image(self, time_ms, reaction_ms=NO_REACTION):
        error = self.render(time_ms, reaction_ms, self.pixels, len(self.pixels))
        if error:
            raise RuntimeError(f"Muse avatar raster rejected: esp_err_t=0x{error & 0xffffffff:08x}")
        return self._image_from_pixels()

    # self: Serialized host preview owner and reusable RGB565 surface.
    # pose: Numeric PixelAvatarPose identity.
    # time_ms: Active animation time.
    # pose_ms: Time since the pose started.
    # left_eye: Independent MuseEyeStyle for the left eye.
    # right_eye: Independent MuseEyeStyle for the right eye.
    # mouth: Independent MuseMouthStyle.
    # talking_openness: Explicit Talking mouth opening, from zero to one.
    # symbol: Numeric MuseSymbol, independent of the selected face.
    # reaction_ms: Time since the accepted touch, or NO_REACTION.
    # audio_level: Optional simulated audio level.
    # working: Whether Thinking uses the work animation.
    # body_x: Horizontal body offset in Muse grid pixels.
    # body_y: Vertical body offset in Muse grid pixels.
    # left_hand_x: Horizontal left-hand offset in Muse grid pixels.
    # left_hand_y: Vertical left-hand offset in Muse grid pixels.
    # right_hand_x: Horizontal right-hand offset in Muse grid pixels.
    # right_hand_y: Vertical right-hand offset in Muse grid pixels.
    # gaze_x: Authored horizontal gaze from -1 to 1.
    # gaze_y: Authored vertical gaze from -1 to 1.
    # eye_openness: Authored eye openness from zero to one.
    # Raises RuntimeError for invalid native selectors or composition values.
    def face_image(self, pose, time_ms, pose_ms, left_eye=MuseEyeStyle.AUTOMATIC,
                   right_eye=MuseEyeStyle.AUTOMATIC, mouth=MuseMouthStyle.AUTOMATIC,
                   talking_openness=0.5, symbol=0, reaction_ms=NO_REACTION,
                   audio_level=0.0, working=False, body_x=0.0, body_y=0.0,
                   left_hand_x=0.0, left_hand_y=0.0, right_hand_x=0.0, right_hand_y=0.0,
                   gaze_x=0.0, gaze_y=0.0, eye_openness=1.0):
        error = self.render_face(pose, time_ms, pose_ms, reaction_ms, audio_level, int(working),
                                 DISPLAY_SIZE, self.pixels, len(self.pixels), left_eye, right_eye,
                                 mouth, talking_openness, symbol, body_x, body_y,
                                 left_hand_x, left_hand_y, right_hand_x, right_hand_y,
                                 gaze_x, gaze_y, eye_openness)
        if error:
            raise RuntimeError(f"Muse face raster rejected: esp_err_t=0x{error & 0xffffffff:08x}")
        return self._image_from_pixels()

    # self: Preview owner whose most recent native frame fills the reusable surface.
    def _image_from_pixels(self):
        # Pillow uses BGR;16 for little-endian RGB565 with masks F800/07E0/001F.
        pixels = bytes(self.pixels)
        if sys.byteorder == "big":
            words = array("H")
            words.frombytes(pixels)
            words.byteswap()
            pixels = words.tobytes()
        return Image.frombytes("RGB", (DISPLAY_SIZE, DISPLAY_SIZE), pixels, "raw", "BGR;16")
