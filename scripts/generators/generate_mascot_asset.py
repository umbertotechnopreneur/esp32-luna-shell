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

"""Generate the firmware's front-facing idle head layer from its preserved PNG."""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/mascot/source/luna-idle-head.png"
OUTPUT = ROOT / "assets/mascot/generated"
SIZE = (170, 198)


def main() -> None:
    source = Image.open(SOURCE).convert("RGBA")
    # Ignore nearly transparent generation noise outside the actual character.
    bounds = source.getchannel("A").point(
        lambda alpha: 255 if alpha > 8 else 0
    ).getbbox()
    if bounds is None:
        raise ValueError(f"Mascot source is fully transparent: {SOURCE}")
    sprite = source.crop(bounds).resize(SIZE, Image.Resampling.NEAREST)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    sprite.save(OUTPUT / "luna-idle-head-170x198.png")

    # LVGL's ARGB8888 byte layout on this little-endian target is B, G, R, A.
    (OUTPUT / "luna-idle-head-170x198.bgra8888.bin").write_bytes(
        sprite.tobytes("raw", "BGRA")
    )


if __name__ == "__main__":
    main()
