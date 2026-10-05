"""Package the approved floppy GIF for native and LVGL boot, without a GIF decoder."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile

from PIL import Image

from generate_boot_assets import enter_asset_package_lock, exit_asset_package_lock

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets/branding/VibeWare"
SOURCE = ASSETS / "source/vibeware-floppy-insert-transparent.gif"
OUTPUT = ASSETS / "generated"
SCALE = 2
CANVAS = 466


# values: Immutable unsigned values to emit in a C array.
# digits: Hexadecimal width of each literal.
# columns: Maximum literals per readable source line.
def array_text(values: list[int], digits: int, columns: int = 16) -> str:
    return "\n".join(
        "    " + ", ".join(f"0x{value:0{digits}x}" for value in values[start:start + columns]) + ","
        for start in range(0, len(values), columns)
    )


# frame: Fully composited RGB source with transparency already resolved against black.
# Raises ValueError when an exact frame exceeds the indexed palette capacity.
def encode_frame(frame: Image.Image) -> tuple[list[int], list[int], list[int]]:
    palette = [0]
    identities = {0: 0}
    pixels = []
    rgb_bytes = frame.tobytes()
    for offset in range(0, len(rgb_bytes), 3):
        red, green, blue = rgb_bytes[offset:offset + 3]
        color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        if color not in identities:
            if len(palette) == 256:
                raise ValueError("Exact RGB565 frame requires more than 256 palette colors")
            identities[color] = len(palette)
            palette.append(color)
        pixels.append(identities[color])
    runs = []
    offsets = [0]
    for row in range(frame.height):
        start = row * frame.width
        position = 0
        while position < frame.width:
            color = pixels[start + position]
            end = position + 1
            while end < frame.width and pixels[start + end] == color:
                end += 1
            count = end - position
            runs.extend((count & 255, count >> 8, color))
            position = end
        offsets.append(len(runs))
    return palette, offsets, runs


# No parameters: preserve the GIF, disposal-composited frames and original frame durations.
# Raises OSError for source/output I/O failures and ValueError for unsupported GIF geometry/timing.
def generate() -> None:
    frames = []
    durations = []
    bounds = None
    with Image.open(SOURCE) as source:
        if source.format != "GIF" or source.n_frames == 0:
            raise ValueError("Boot source must be a nonempty GIF")
        original_size = list(source.size)
        for index in range(source.n_frames):
            source.seek(index)
            duration = source.info.get("duration")
            if not isinstance(duration, int) or not 1 <= duration <= 10000:
                raise ValueError(f"Frame {index} has an invalid duration: {duration!r}")
            # Pillow applies GIF disposal before exposing the next RGBA frame.
            frame = source.convert("RGBA")
            visible = frame.getbbox()
            if visible is not None:
                bounds = visible if bounds is None else (
                    min(bounds[0], visible[0]), min(bounds[1], visible[1]),
                    max(bounds[2], visible[2]), max(bounds[3], visible[3]),
                )
            frames.append(frame)
            durations.append(duration)
    if bounds is None:
        raise ValueError("Boot GIF has no visible artwork")
    width = (bounds[2] - bounds[0]) * SCALE
    height = (bounds[3] - bounds[1]) * SCALE
    if width > 320 or height > 320 or width % 2 or height % 2:
        raise ValueError(f"Twofold pixel artwork must fit the safe square: {width}x{height}")
    left = (CANVAS - width) // 2
    top = (CANVAS - height) // 2
    declarations = []
    entries = []
    unique_resources = {}
    resource_bytes = 0
    for index, frame in enumerate(frames):
        crop = frame.crop(bounds).resize((width, height), Image.Resampling.NEAREST)
        opaque = Image.new("RGBA", crop.size, (0, 0, 0, 255))
        opaque.alpha_composite(crop)
        palette, offsets, runs = encode_frame(opaque.convert("RGB"))
        identity = (tuple(palette), tuple(offsets), bytes(runs))
        name = unique_resources.get(identity)
        if name is None:
            name = f"image_{index:02d}"
            unique_resources[identity] = name
            declarations.append(
                f"static const uint16_t {name}_palette[] = {{\n{array_text(palette, 4)}\n}};\n"
                f"static const uint32_t {name}_offsets[] = {{\n{array_text(offsets, 8, 8)}\n}};\n"
                f"static const uint8_t {name}_runs[] = {{\n{array_text(runs, 2, 24)}\n}};\n"
            )
            resource_bytes += len(palette) * 2 + len(offsets) * 4 + len(runs)
        entries.append(
            f"    {{{durations[index]}, {len(palette)}, {name}_palette, {name}_offsets, "
            f"{name}_runs, sizeof({name}_runs)}},"
        )
    header = f'''// Generated by device.ps1 generate -Generator vibeware-boot.
#pragma once
#include "VIBEWARE_boot_animation.h"
#define VIBEWARE_BOOT_WIDTH {width}
#define VIBEWARE_BOOT_HEIGHT {height}
#define VIBEWARE_BOOT_LEFT {left}
#define VIBEWARE_BOOT_TOP {top}
#define VIBEWARE_BOOT_FRAME_COUNT {len(frames)}
#define VIBEWARE_BOOT_DURATION_MS {sum(durations)}
#ifdef __cplusplus
extern "C" {{
#endif
extern const VIBEWARE_boot_frame_t VIBEWARE_boot_frames[VIBEWARE_BOOT_FRAME_COUNT];
#ifdef __cplusplus
}}
#endif
'''
    data = (
        '// Generated from the unchanged approved GIF; immutable resources remain in flash.\n'
        '#include "vibeware_boot_data.h"\n\n' + "\n".join(declarations) +
        "\nconst VIBEWARE_boot_frame_t VIBEWARE_boot_frames[VIBEWARE_BOOT_FRAME_COUNT] = {\n" +
        "\n".join(entries) + "\n};\n"
    )
    manifest = {
        "source": SOURCE.relative_to(ROOT).as_posix(),
        "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        "source_size": original_size, "crop_bounds": list(bounds), "pixel_scale": SCALE,
        "width": width, "height": height, "left": left, "top": top,
        "frame_durations_ms": durations, "cycle_duration_ms": sum(durations),
        "frame_count": len(frames), "unique_images": len(unique_resources),
        "encoding": "exact-rgb565-row-indexed-rle", "background": "black",
        "resource_data_bytes": resource_bytes,
        "manifesto": "https://umbertogiacobbi.biz/vibeware/manifesto",
    }
    outputs = {"vibeware_boot_data.h": header, "vibeware_boot_data.c": data,
               "manifest.json": json.dumps(manifest, indent=2) + "\n"}
    staging_root = ROOT / "artifacts/tmp"
    staging_root.mkdir(parents=True, exist_ok=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vibeware-boot-", dir=staging_root) as directory:
        staging = Path(directory)
        previous = {name: (OUTPUT / name).read_bytes() if (OUTPUT / name).exists() else None
                    for name in outputs}
        for name, content in outputs.items():
            (staging / name).write_text(content, encoding="utf-8", newline="\n")
        replaced = []
        try:
            for name in outputs:
                os.replace(staging / name, OUTPUT / name)
                replaced.append(name)
        except OSError:
            for name in reversed(replaced):
                if previous[name] is None:
                    (OUTPUT / name).unlink()
                else:
                    (OUTPUT / name).write_bytes(previous[name])
            raise
    print(f"VibeWare boot: {len(frames)} frames / {sum(durations)} ms / {resource_bytes} flash data bytes")


# No parameters: serialize generation with the canonical asset-package owner.
# Raises OSError, RuntimeError or ValueError for locking, source or package failures.
def main() -> None:
    lock = enter_asset_package_lock(ROOT, "vibeware-boot")
    try:
        generate()
    finally:
        exit_asset_package_lock(lock)


if __name__ == "__main__":
    main()
