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
from PIL import Image
from cody_native import checked


# native: Already loaded actual C++ graphics owner.
# image: Current native character image receiving the shared Back control.
# Raises RuntimeError for original firmware layout/raster errors.
# Raises ValueError for invalid ctypes boundary data, before truncation or pixel mutation.
def render_debug_back(native, image):
    if image.mode != "RGB" or image.size != (466, 466):
        raise ValueError("Native Debug overlay requires a logical 466x466 RGB frame")
    function = native.api.cody_host_debug_back
    function.argtypes = [C.POINTER(C.c_uint16), C.c_uint32]
    function.restype = C.c_int
    pixels = (C.c_uint16 * (466 * 466))()
    for index, (red, green, blue) in enumerate(image.getdata()):
        pixels[index] = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
    checked("debug_back", function(pixels, len(pixels)))
    rgb = bytearray(len(pixels) * 3)
    for index, color in enumerate(pixels):
        rgb[index * 3] = ((color >> 11) & 31) * 255 // 31
        rgb[index * 3 + 1] = ((color >> 5) & 63) * 255 // 63
        rgb[index * 3 + 2] = (color & 31) * 255 // 31
    return Image.frombytes("RGB", (466, 466), bytes(rgb))
