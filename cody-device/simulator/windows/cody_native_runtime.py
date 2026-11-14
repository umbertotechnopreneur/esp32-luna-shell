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
from pathlib import Path
from runtime_paths import simulator_library_path

ROOT = Path(__file__).resolve().parents[3]
_RUNTIME_LIBRARIES = {}


# driver: Selects the existing graphics bridge or the separate full-device runtime.
# Raises OSError, ValueError or AttributeError for missing, stale or incompatible output.
def load_runtime_library(driver="graphics"):
    if driver not in ("graphics", "graphics-device"):
        raise ValueError(f"Unknown graphics runtime: {driver}")
    library = simulator_library_path(ROOT, driver)
    directory = library.parent
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != "cody.host-graphics.v1":
        raise ValueError("Unsupported native runtime manifest; generate cody-simulator-graphics through device.ps1")
    inputs = manifest.get("inputs")
    required = {"scripts/generators/generate_cody_simulator_graphics.py",
                "cody-device/simulator/native/CODY_host_snapshot.cpp",
                "cody-device/simulator/native/CODY_host_snapshot.hpp"}
    if driver == "graphics-device":
        required |= {"cody-device/simulator/native/CODY_host_display.cpp",
                     "cody-device/simulator/native/CODY_host_input.cpp",
                     "cody-device/simulator/native/CODY_host_studio_domain.cpp",
                     "cody-device/simulator/native/platform/cody_host_studio.h",
                     "cody-device/components/CODY_companion/src/runtime/CODY_device_ui.cpp",
                     "cody-device/components/CODY_companion/src/scenes/CODY_onboarding.cpp"}
    if not isinstance(inputs, dict) or not required.issubset(inputs):
        raise ValueError("Native runtime provenance is incomplete; regenerate cody-simulator-graphics")
    for source, expected in inputs.items():
        path = (ROOT / source).resolve()
        if not path.is_relative_to(ROOT) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Stale native runtime input {source}; regenerate cody-simulator-graphics")
    if hashlib.sha256(library.read_bytes()).hexdigest() != manifest.get("library_sha256"):
        raise ValueError("Native runtime library hash mismatch")
    loaded = _RUNTIME_LIBRARIES.get(driver)
    if loaded is not None:
        if loaded._cody_library_sha256 != manifest["library_sha256"]:
            raise ValueError("Native runtime was rebuilt while loaded; restart this simulator process")
        return loaded
    api = ctypes.CDLL(str(library))
    api.cody_host_runtime_abi.argtypes = ()
    api.cody_host_runtime_abi.restype = ctypes.c_uint32
    if api.cody_host_runtime_abi() != 2:
        raise ValueError("Unsupported native runtime ABI")
    api._cody_library_sha256 = manifest["library_sha256"]
    _RUNTIME_LIBRARIES[driver] = api
    return api
