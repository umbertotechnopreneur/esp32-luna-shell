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

"""Read the canonical versioned firmware-profile composition."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = ROOT / "config" / "sdkconfig" / "v1" / "catalog.json"


def load_catalog() -> dict[str, Any]:
    """Return the validated profile catalog used by the build wrapper."""
    catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if catalog.get("schema_version") != 1:
        raise ValueError(
            f"unsupported firmware profile schema in {CATALOG_PATH}: "
            f"{catalog.get('schema_version')!r}"
        )
    variants = catalog.get("variants")
    if not isinstance(variants, dict) or not variants:
        raise ValueError(f"firmware profile catalog has no variants: {CATALOG_PATH}")
    return catalog


def variant_key(profile: str = "full", feature: str = "none") -> str:
    """Map the public wrapper selectors to their catalog variant key."""
    return profile if feature == "none" else f"{profile}-{feature}"


# profile: Hardware/product profile declared in the versioned catalog.
# feature: Optional feature identity for that profile.
# build_mode: Debug by default; Release must be selected by the caller.
# Raises ValueError, FileNotFoundError, OSError or UnicodeError for invalid inputs.
def profile_defaults_paths(
    profile: str = "full",
    feature: str = "none",
    build_mode: str = "Debug",
) -> tuple[Path, ...]:
    """Resolve the ordered defaults layers for one legal catalog variant."""
    catalog = load_catalog()
    key = variant_key(profile, feature)
    variant = catalog["variants"].get(key)
    if not isinstance(variant, dict):
        raise ValueError(f"unknown firmware profile variant: {key}")
    if variant.get("profile") != profile or variant.get("feature") != feature:
        raise ValueError(f"firmware profile identity mismatch for variant: {key}")

    modes = catalog.get("build_modes")
    if catalog.get("default_build_mode") != "Debug" or not isinstance(modes, dict):
        raise ValueError("firmware build-mode catalog must explicitly default to Debug")
    if set(modes) != {"Debug", "Release"} or build_mode not in modes:
        raise ValueError(f"unknown or ambiguous firmware build mode: {build_mode}")
    profile_defaults = variant.get("defaults")
    if not isinstance(profile_defaults, list) or not profile_defaults:
        raise ValueError(f"firmware profile variant has no defaults: {key}")
    mode = modes[build_mode]
    if not isinstance(mode, dict):
        raise ValueError(f"invalid firmware build-mode declaration: {build_mode}")
    mode_defaults = mode.get("defaults")
    if not isinstance(mode_defaults, list) or not mode_defaults:
        raise ValueError(f"firmware build mode has no defaults: {build_mode}")

    resolved: list[Path] = []
    # Mode policy is always the final layer, as in the public PowerShell wrapper.
    for relative in [*profile_defaults, *mode_defaults]:
        if not isinstance(relative, str) or not relative:
            raise ValueError(f"invalid firmware defaults path for {key}/{build_mode}")
        path = (ROOT / relative).resolve()
        try:
            path.relative_to(ROOT)
        except ValueError as error:
            raise ValueError(
                f"firmware profile path escapes the repository: {relative}"
            ) from error
        if not path.is_file():
            raise FileNotFoundError(f"firmware profile defaults missing: {path}")
        resolved.append(path)
    if not resolved:
        raise ValueError(f"firmware profile variant has no defaults: {key}")
    return tuple(resolved)


# profile: Hardware/product profile declared in the versioned catalog.
# feature: Optional feature identity for that profile.
# build_mode: Explicit mode whose final defaults layer is included.
# Raises ValueError, FileNotFoundError, OSError or UnicodeError for invalid inputs.
def read_profile_defaults(
    profile: str = "full",
    feature: str = "none",
    build_mode: str = "Debug",
) -> str:
    """Concatenate one profile exactly in the wrapper's declared order."""
    return "\n".join(
        path.read_text(encoding="utf-8").rstrip("\n")
        for path in profile_defaults_paths(profile, feature, build_mode)
    ) + "\n"
