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

"""Verified transport to production C++ logic; host hardware inputs stay synthetic."""
from __future__ import annotations

import ctypes
import hashlib
import json
from functools import lru_cache
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


# No parameters: opens the current source-proven native graphics/runtime library.
# Raises OSError, ValueError or AttributeError for missing, stale or incompatible output.
@lru_cache(maxsize=1)
def load_runtime_library():
    directory = ROOT / "artifacts/build-cody-simulator-graphics"
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != "cody.host-graphics.v1":
        raise ValueError("Unsupported native runtime manifest; generate cody-simulator-graphics through device.ps1")
    inputs = manifest.get("inputs")
    required = {"scripts/generators/generate_cody_simulator_graphics.py",
                "simulator/native/CODY_host_snapshot.cpp",
                "simulator/native/CODY_host_snapshot.hpp"}
    if not isinstance(inputs, dict) or not required.issubset(inputs):
        raise ValueError("Native runtime provenance is incomplete; regenerate cody-simulator-graphics")
    for source, expected in inputs.items():
        path = (ROOT / source).resolve()
        if not path.is_relative_to(ROOT) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Stale native runtime input {source}; regenerate cody-simulator-graphics")
    library = directory / "cody_host_graphics.dll"
    if hashlib.sha256(library.read_bytes()).hexdigest() != manifest.get("library_sha256"):
        raise ValueError("Native runtime library hash mismatch")
    api = ctypes.CDLL(str(library))
    api.cody_host_runtime_abi.argtypes = ()
    api.cody_host_runtime_abi.restype = ctypes.c_uint32
    if api.cody_host_runtime_abi() != 2:
        raise ValueError("Unsupported native runtime ABI")
    return api
