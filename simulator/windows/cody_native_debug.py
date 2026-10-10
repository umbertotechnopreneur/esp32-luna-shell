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

"""Transport the shared C++ Debug views; no Python layout or raster fallback."""

import ctypes as C
from PIL import Image
from cody_native import checked


# native: Already loaded actual C++ graphics owner.
# values: Current native-composed face values, in the exact native catalog order.
# first_channel: Absolute first list row, checked by C++ against the real count.
# image: None for channel list, or current native character image for Back overlay only.
# Raises RuntimeError for original firmware layout/raster errors.
# Raises ValueError for invalid ctypes boundary data, before truncation or pixel mutation.
def render_debug_channels(native, values, first_channel, image=None):
    if type(first_channel) is not int or not 0 <= first_channel <= 255:
        raise ValueError("Native Debug first row must fit uint8 without truncation")
    if len(values) != int(native.api.cody_host_channel_count()):
        raise ValueError("Native Debug values must match the actual C++ channel count")
    if image is not None and (image.mode != "RGB" or image.size != (466, 466)):
        raise ValueError("Native Debug overlay requires a logical 466x466 RGB frame")
    function = native.api.cody_host_debug_channels
    function.argtypes = [C.POINTER(C.c_float), C.c_uint32, C.c_uint8,
                         C.POINTER(C.c_uint16), C.c_uint32, C.c_bool]
    function.restype = C.c_int
    pixels = (C.c_uint16 * (466 * 466))()
    if image is not None:
        for index, (red, green, blue) in enumerate(image.convert("RGB").getdata()):
            pixels[index] = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
    checked("debug_channel_list", function(native.floats(values), len(values), first_channel,
                                           pixels, len(pixels), image is None))
    rgb = bytearray(len(pixels) * 3)
    for index, color in enumerate(pixels):
        rgb[index * 3] = ((color >> 11) & 31) * 255 // 31
        rgb[index * 3 + 1] = ((color >> 5) & 63) * 255 // 63
        rgb[index * 3 + 2] = (color & 31) * 255 // 31
    return Image.frombytes("RGB", (466, 466), bytes(rgb))
