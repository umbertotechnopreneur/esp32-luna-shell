"""Small procedural Studio preview of the device's dark, soft-color globe."""

from functools import lru_cache
from PIL import Image

from cody_preview_layout import SIZE


GRID = 65
EXTENT = 256
ORIGIN = (SIZE - EXTENT) // 2
WAVE = (0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25,
        0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25)
FALLOFF = tuple(((255 - index) ** 3 + 32512) // 65025 for index in range(256))


# phase: Wrapping eight-bit orbital phase shared with the firmware's authored shape.
def wave(phase: int) -> int:
    phase &= 255
    index, fraction = divmod(phase, 8)
    return int((WAVE[index] * (8 - fraction) + WAVE[(index + 1) & 31] * fraction) / 8)


# tick: Quantized eight-frame-per-second animation time from the preview owner.
# Returns an RGB image borrowed by the current frame; no route or device data is mutated.
@lru_cache(maxsize=1)
def render_screensaver(tick: int) -> Image.Image:
    drift_x = int(wave(tick) * 3 / 127)
    drift_y = int(wave(tick + 64) * 3 / 127)
    lobes = tuple((32 + drift_x + int(wave(tick * 2 + phase) * 6 / 127),
                   32 + drift_y + int(wave(tick * 2 + phase + 64) * 5 / 127), radius, color)
                  for phase, radius, color in ((0, 23, (35, 145, 220)),
                                               (85, 21, (160, 45, 180)),
                                               (170, 20, (30, 155, 120))))
    pixels = []
    for y in range(GRID):
        for x in range(GRID):
            channels = [0, 0, 0]
            for cx, cy, radius, color in lobes:
                distance = (x - cx) ** 2 + (y - cy) ** 2
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
