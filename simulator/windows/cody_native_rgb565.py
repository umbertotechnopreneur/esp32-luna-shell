# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; consolidated native RGB565 image transport on 2026-10-17.
# Human guidance: Umberto Giacobbi; requested duplicate removal, clear ownership and readable code.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Convert logical Studio images at the native RGB565 boundary, without drawing policy."""

import ctypes as C
from PIL import Image


LOGICAL_FRAME_SIZE = (466, 466)
LOGICAL_PIXEL_COUNT = LOGICAL_FRAME_SIZE[0] * LOGICAL_FRAME_SIZE[1]


# image: Complete unrotated RGB image; callers retain ownership throughout the native operation.
# Returns an owned native-endian uint16 buffer that remains alive until the synchronous call returns.
# Raises ValueError before mutation if the image does not match the logical display.
def image_to_native_rgb565(image):
    if image.mode != "RGB" or image.size != LOGICAL_FRAME_SIZE:
        raise ValueError("Native drawing requires a logical 466x466 RGB frame")

    pixels = (C.c_uint16 * LOGICAL_PIXEL_COUNT)()
    for index, (red, green, blue) in enumerate(image.getdata()):
        red_5bit = red >> 3
        green_6bit = green >> 2
        blue_5bit = blue >> 3
        pixels[index] = (red_5bit << 11) | (green_6bit << 5) | blue_5bit

    return pixels


# pixels: Complete native-endian RGB565 array, read only after the native operation succeeds.
# Returns a new RGB image; conversion never publishes or changes the caller's existing image.
# Raises ValueError for a buffer that does not cover the logical display exactly.
def native_rgb565_to_image(pixels):
    if len(pixels) != LOGICAL_PIXEL_COUNT:
        raise ValueError("Native RGB565 output must cover the complete logical display")

    rgb = bytearray(LOGICAL_PIXEL_COUNT * 3)
    for index, color in enumerate(pixels):
        destination = index * 3
        rgb[destination] = ((color >> 11) & 31) * 255 // 31
        rgb[destination + 1] = ((color >> 5) & 63) * 255 // 63
        rgb[destination + 2] = (color & 31) * 255 // 31

    return Image.frombytes("RGB", LOGICAL_FRAME_SIZE, bytes(rgb))
