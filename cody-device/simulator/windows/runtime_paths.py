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

"""Shared output ownership and native driver paths for desktop simulators."""

from __future__ import annotations

import sys
from pathlib import Path


# No parameters: source runs use checkout/artifacts; packaged runs use the executable location.
def simulator_artifacts_root() -> Path:
    if getattr(sys, "frozen", False):
        executable = Path(sys.executable).resolve()
        for directory in executable.parents:
            if directory.name.casefold() == "artifacts":
                return directory
        # A distributed executable owns artifacts beside itself, never inside extraction temp.
        return (executable.parent / "artifacts").resolve()
    return (Path(__file__).resolve().parents[3] / "artifacts").resolve()


# root: Checkout that owns the simulator's artifacts.
# driver: Host library identity, either graphics or audio.
# Raises RuntimeError for an unsupported host and ValueError for an unknown driver.
def simulator_library_path(root, driver):
    if driver not in ("graphics", "audio"):
        raise ValueError(f"Unknown Cody Studio driver: {driver}")
    if sys.platform == "win32":
        build_suffix, extension = "", ".dll"
    elif sys.platform == "linux":
        build_suffix, extension = "-linux", ".so"
    else:
        raise RuntimeError(f"Cody Studio native drivers support Windows and Linux, found {sys.platform}.")
    stem = "cody_host_graphics" if driver == "graphics" else "cody_fake_audio"
    return Path(root) / "artifacts" / f"build-cody-simulator-{driver}{build_suffix}" / (stem + extension)


# requested is an output name, explicit artifacts-relative path, or absolute path within artifacts.
# Raises ValueError for drive-relative paths, traversal outside artifacts, or a directory destination.
# Raises OSError or RuntimeError if the filesystem cannot resolve the requested path.
def resolve_runtime_output(requested: Path) -> Path:
    requested = Path(requested)
    artifacts = simulator_artifacts_root()
    if requested.drive and not requested.is_absolute():
        raise ValueError(f"Simulator output must not use a drive-relative path: {requested}")
    if requested.is_absolute():
        destination = requested.resolve()
    elif requested.parts and requested.parts[0].casefold() == "artifacts":
        destination = (artifacts.parent / requested).resolve()
    else:
        destination = (artifacts / "simulator" / requested).resolve()
    if not destination.is_relative_to(artifacts) or destination == artifacts:
        raise ValueError(f"Simulator output must stay inside {artifacts}: {requested}")
    if destination.is_dir():
        raise ValueError(f"Simulator output must name a file: {requested}")
    return destination
