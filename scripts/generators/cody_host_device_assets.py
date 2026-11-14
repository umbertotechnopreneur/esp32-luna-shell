"""Bind the unchanged firmware image assets to their original linker symbols.

This module is a host build input only. It never changes the assets or invents
pixel data; output assembly is written under the simulator artifacts directory.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

# The first/last symbols are the exact names used by the unmodified firmware.
IMAGES = (
    ("cody_onboarding_button", "cody-device/assets/cody/onboarding/generated/button.rgb565", 19008),
    ("cody_onboarding_sofia", "cody-device/assets/cody/onboarding/generated/sofia.rgb565", 217668),
    ("cody_surprise_portrait", "cody-device/assets/cody/surprise/generated/portrait.indexed565", 217668),
    ("cody_surprise_vietnam", "cody-device/assets/cody/surprise/generated/vietnam.indexed565", 217668),
    ("cody_surprise_italy", "cody-device/assets/cody/surprise/generated/italy.indexed565", 217668),
)


# output: Artifact-owned assembler source for a GNU-compatible host compiler.
# Raises ValueError or OSError for invalid paths, unexpected asset sizes or I/O failure.
def write_device_image_assembler(output):
    output = Path(output).resolve()
    if not output.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Host image linker input must remain inside artifacts/")
    lines = ['.section .rdata,"dr"' if sys.platform == "win32" else '.section .rodata,"a",@progbits']
    for symbol, relative, expected_size in IMAGES:
        asset = (ROOT / relative).resolve(strict=True)
        if not asset.is_relative_to(ROOT) or asset.stat().st_size != expected_size:
            raise ValueError(f"Unexpected firmware image asset: {relative}")
        path = asset.as_posix()
        if any(character in path for character in ('"', '\r', '\n')):
            raise ValueError(f"Unsupported assembler asset path: {relative}")
        start, end = f"{symbol}_start", f"{symbol}_end"
        lines.extend((".balign 4", f".global {start}", f"{start}:", f'.incbin "{path}"',
                      f".global {end}", f"{end}:"))
    # Preserve previously generated materials; caller chooses an unused path.
    with output.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines) + "\n")
