"""Transport for native guide captions; this does not render a complete screen.

The display buffer and all text pixels come from the device C++ implementation.
This adapter must not be presented as a complete onboarding preview: title,
mascot, controls, status and navigation are owned by other device components.
"""

import ctypes

from cody_native_rgb565 import image_to_native_rgb565, native_rgb565_to_image
from cody_native_runtime import load_runtime_library


# image: Caller-owned 466x466 RGB framebuffer, changed only after native success.
# page: Stable OptionsPage identity from the device onboarding catalog.
# first_run: Whether the firmware presenter describes the first-run footer.
# save_error: Simulated original esp_err_t from a settings operation.
# Raises ValueError for invalid transport and RuntimeError for a native failure.
def draw_guide_lines(image, page, first_run=False, save_error=0):
    if type(page) is not int or not 0 <= page <= 255:
        raise ValueError("Guide page must be an OptionsPage uint8 identity")
    if type(first_run) is not bool or type(save_error) is not int or not -(2**31) <= save_error < 2**31:
        raise ValueError("Invalid first-run or esp_err_t value")
    pixels = image_to_native_rgb565(image)
    api = load_runtime_library()
    draw = api.cody_host_guide_lines
    draw.argtypes = [ctypes.c_uint32, ctypes.c_bool, ctypes.c_int32,
                     ctypes.POINTER(ctypes.c_uint16), ctypes.c_uint32]
    draw.restype = ctypes.c_int
    error = draw(page, first_run, save_error, pixels, len(pixels))
    if error:
        raise RuntimeError(f"Native guide rendering failed: esp_err_t={error}")
    image.paste(native_rgb565_to_image(pixels))
