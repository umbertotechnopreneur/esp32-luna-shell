"""Small procedural Studio preview of the device's dark, soft-color globe."""

from functools import lru_cache
from PIL import Image

from cody_preview_layout import SIZE


GRID = 65
EXTENT = 320
ORIGIN = (SIZE - EXTENT) // 2
WAVE = (0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25,
        0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25)
FALLOFF = tuple(((255 - index) ** 2 + 127) // 255 for index in range(256))


# phase: Orbital phase in twenty-fifths of an eight-bit step, shared with firmware.
def wave(phase: int) -> int:
    phase %= 256 * 25
    index, fraction = divmod(phase, 200)
    return int((WAVE[index] * (200 - fraction) + WAVE[(index + 1) & 31] * fraction) / 200)


# tick: Quantized 25-frame-per-second animation time from the preview owner.
# Returns an RGB image borrowed by the current frame; no route or device data is mutated.
@lru_cache(maxsize=1)
def render_screensaver(tick: int) -> Image.Image:
    subpixel = 16
    phase_time = (tick % 800) * 8
    drift_x = int(wave(phase_time) * (2 * subpixel) / 127)
    drift_y = int(wave(phase_time + 64 * 25) * (2 * subpixel) / 127)
    lobes = tuple((32 * subpixel + drift_x + int(wave(phase_time * 2 + phase * 25) * (5 * subpixel) / 127),
                   32 * subpixel + drift_y + int(wave(phase_time * 2 + (phase + 64) * 25) * (5 * subpixel) / 127),
                   radius * subpixel, color)
                  for phase, radius, color in ((0, 25, (35, 145, 220)),
                                               (85, 24, (160, 45, 180)),
                                               (170, 23, (30, 155, 120))))
    pixels = []
    for y in range(GRID):
        for x in range(GRID):
            channels = [0, 0, 0]
            for cx, cy, radius, color in lobes:
                distance = (x * subpixel - cx) ** 2 + (y * subpixel - cy) ** 2
                if distance >= radius * radius:
                    continue
                weight = FALLOFF[distance * 255 // (radius * radius)]
                for channel in range(3):
                    channels[channel] += color[channel] * weight
            red, green, blue = (min(255, value // 255) for value in channels)
            pixels.append(((red >> 3) * 255 // 31, (green >> 2) * 255 // 63, (blue >> 3) * 255 // 31))
    image = Image.new("RGB", (GRID, GRID))
    image.putdata(pixels)
    frame = Image.new("RGB", (SIZE, SIZE))
    frame.paste(image.resize((EXTENT, EXTENT), Image.Resampling.BILINEAR), (ORIGIN, ORIGIN))
    return frame
