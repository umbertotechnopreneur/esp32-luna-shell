#!/usr/bin/env python3
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

"""Minify staged JSON files without rewriting their tokens."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


JSON_WHITESPACE = frozenset(b" \t\r\n")


def reject_non_json_constant(value: str) -> None:
    raise ValueError(f"non-JSON numeric constant: {value}")


def validate_json(data: bytes, path: Path) -> None:
    try:
        json.loads(
            data.decode("utf-8"),
            parse_constant=reject_non_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"{path}: invalid JSON: {error}") from error


def minify_json(data: bytes) -> bytes:
    output = bytearray()
    in_string = False
    escaped = False

    for byte in data:
        if in_string:
            output.append(byte)
            if escaped:
                escaped = False
            elif byte == ord("\\"):
                escaped = True
            elif byte == ord('"'):
                in_string = False
            continue

        if byte == ord('"'):
            in_string = True
            output.append(byte)
        elif byte not in JSON_WHITESPACE:
            output.append(byte)

    return bytes(output) + b"\n"


def minify_tree(root: Path) -> tuple[int, int, int]:
    if not root.is_dir():
        raise ValueError(f"staged JSON root is not a directory: {root}")

    json_paths = sorted(path for path in root.rglob("*.json") if path.is_file())
    if not json_paths:
        raise ValueError(f"no staged JSON files found under: {root}")

    original_bytes = 0
    minified_bytes = 0
    for path in json_paths:
        source = path.read_bytes()
        validate_json(source, path)
        minified = minify_json(source)
        validate_json(minified, path)
        if minified != source:
            path.write_bytes(minified)
        original_bytes += len(source)
        minified_bytes += len(minified)

    return len(json_paths), original_bytes, minified_bytes


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Minify every staged JSON file below one directory."
    )
    parser.add_argument("root", type=Path)
    arguments = parser.parse_args()

    count, original_bytes, minified_bytes = minify_tree(arguments.root.resolve())
    print(
        "Minified staged JSON: "
        f"{count} files, {original_bytes} -> {minified_bytes} bytes "
        f"({original_bytes - minified_bytes} bytes saved)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as error:
        raise SystemExit(str(error)) from error
