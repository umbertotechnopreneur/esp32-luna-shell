"""Generate Cody's compile-time RGB565 constants from the semantic theme."""

from __future__ import annotations

import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
THEME = ROOT / "cody-device/assets/cody/palette/theme.json"
OUTPUT = ROOT / "cody-device/components/CODY_companion/include/CODY_theme_generated.hpp"
HEX_WORD = re.compile(r"0x[0-9a-fA-F]{4}\Z")
IDENTIFIER = re.compile(r"[a-z][a-z0-9_]*\Z")


# node: Current device palette value or collection.
# path: Semantic key path, including array indices.
# colors: Destination of unique C++ constant names and native RGB565 values.
# Raises ValueError on malformed names, words or duplicate generated identifiers.
def collect(node, path, colors):
    if isinstance(node, dict):
        if "rgb565" in node:
            collect(node["rgb565"], path, colors)
            return
        for key, value in sorted(node.items()):
            if not IDENTIFIER.fullmatch(key):
                raise ValueError(f"Invalid theme key {'.'.join((*path, key))}")
            collect(value, (*path, key), colors)
    elif isinstance(node, list):
        for index, value in enumerate(node):
            collect(value, (*path, str(index)), colors)
    elif isinstance(node, str) and node.startswith("0x"):
        if HEX_WORD.fullmatch(node) is None:
            raise ValueError(f"Invalid RGB565 value at {'.'.join(path)}")
        name = "_".join(path)
        if name in colors:
            raise ValueError(f"Duplicate generated theme identifier {name}")
        colors[name] = node.upper().replace("0X", "0x")


# No parameters: generate the source input only when its content actually changes.
# Raises OSError, ValueError or JSONDecodeError when the source is unavailable or invalid.
def main():
    theme = json.loads(THEME.read_text(encoding="utf-8"))
    if theme.get("schema_version") != 1 or not isinstance(theme.get("device"), dict):
        raise ValueError("Unsupported or missing Cody device theme")
    colors = {}
    collect(theme["device"], (), colors)
    if not colors:
        raise ValueError("The Cody device theme has no RGB565 colors")
    lines = ["#pragma once", "", "// Generated from assets/cody/palette/theme.json; do not edit.",
             "#include <cstdint>", "", "namespace CODY::Theme {"]
    lines.extend(f"inline constexpr uint16_t {name} = {value};" for name, value in colors.items())
    lines.extend(["", "} // namespace CODY::Theme", ""])
    content = "\n".join(lines)
    if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != content:
        OUTPUT.write_text(content, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
