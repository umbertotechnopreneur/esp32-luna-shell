# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Read Cody Studio's authored colors from the shared semantic theme."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys


_BASE = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[2]))
_THEME_PATH = _BASE / "assets/cody/palette/theme.json"
_THEME = json.loads(_THEME_PATH.read_text(encoding="utf-8"))
if _THEME.get("schema_version") != 1:
    raise ValueError(f"Unsupported Cody theme schema in {_THEME_PATH}")


# path: Keys under studio_preview identifying a single authored RGB888 color.
# Raises KeyError, TypeError or ValueError for an absent or invalid theme value.
def studio_hex(*path: str) -> str:
    value = _THEME["studio_preview"]
    for part in path:
        value = value[part]
    if not isinstance(value, str) or re.fullmatch(r"#[0-9A-Fa-f]{6}", value) is None:
        raise ValueError(f"Invalid Studio theme color at {'.'.join(path)}")
    return value


# path: Keys under studio_preview identifying a single authored RGB888 color.
# Raises KeyError, TypeError or ValueError for an absent or invalid theme value.
def studio_rgb(*path: str) -> tuple[int, int, int]:
    value = studio_hex(*path)
    return tuple(int(value[offset:offset + 2], 16) for offset in (1, 3, 5))


# path: Keys under device identifying a palette token with a native RGB565 word.
# Raises KeyError, TypeError or ValueError for an absent or invalid theme value.
def device_rgb565(*path: str) -> int:
    value = _THEME["device"]
    for part in path:
        value = value[part]
    word = value["rgb565"]
    if not isinstance(word, str) or re.fullmatch(r"0x[0-9A-Fa-f]{4}", word) is None:
        raise ValueError(f"Invalid device theme color at {'.'.join(path)}")
    return int(word, 16)


# path: Keys under device identifying a palette token with a native RGB565 word.
# Raises KeyError, TypeError or ValueError for an absent or invalid theme value.
def device_rgb(*path: str) -> tuple[int, int, int]:
    word = device_rgb565(*path)
    red, green, blue = (word >> 11) & 31, (word >> 5) & 63, word & 31
    return (red << 3 | red >> 2, green << 2 | green >> 4, blue << 3 | blue >> 2)
