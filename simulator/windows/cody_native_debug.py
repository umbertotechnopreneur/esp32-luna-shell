# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Transport the shared C++ diagnostic Back control without substitute geometry."""

import ctypes as C
from enum import IntEnum
from PIL import Image
from cody_native import checked


class DiagnosticPage(IntEnum):
    OVERVIEW = 0
    MEMORY = 1


# native: Already loaded actual C++ graphics owner.
# image: Current native character image receiving native diagnostic controls.
# page: None for Back only, or the typed diagnostic page with shared layout.
# Raises RuntimeError for original firmware layout/raster errors.
# Raises ValueError for invalid ctypes boundary data, before truncation or pixel mutation.
def render_debug_controls(native, image, page=None):
    if image.mode != "RGB" or image.size != (466, 466):
        raise ValueError("Native Debug overlay requires a logical 466x466 RGB frame")
    if page is not None and not isinstance(page, DiagnosticPage):
        raise ValueError("Native diagnostic page must have a typed identity")
    function = native.api.cody_host_debug_back if page is None else native.api.cody_host_diagnostic_page
    function.argtypes = [C.POINTER(C.c_uint16), C.c_uint32] + ([] if page is None else [C.c_uint8])
    function.restype = C.c_int
    pixels = (C.c_uint16 * (466 * 466))()
    for index, (red, green, blue) in enumerate(image.getdata()):
        pixels[index] = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
    arguments = [pixels, len(pixels)] + ([] if page is None else [int(page)])
    checked("debug_controls", function(*arguments))
    rgb = bytearray(len(pixels) * 3)
    for index, color in enumerate(pixels):
        rgb[index * 3] = ((color >> 11) & 31) * 255 // 31
        rgb[index * 3 + 1] = ((color >> 5) & 63) * 255 // 63
        rgb[index * 3 + 2] = (color & 31) * 255 // 31
    return Image.frombytes("RGB", (466, 466), bytes(rgb))


# native: Already loaded shared C++ graphics owner.
# point: Acquired logical display point, or None outside the circular display.
# Raises ValueError for coordinates that cannot cross the typed native boundary.
def debug_memory_button_contains(native, point):
    if point is None:
        return False
    if len(point) != 2 or any(type(value) is not int or not 0 <= value < 466 for value in point):
        raise ValueError("Native diagnostic touch must be a logical display pixel")
    function = native.api.cody_host_debug_memory_contains
    function.argtypes = [C.c_int16, C.c_int16]
    function.restype = C.c_bool
    return bool(function(*point))
