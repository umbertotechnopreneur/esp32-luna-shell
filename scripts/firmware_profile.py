"""Read the canonical versioned firmware-profile composition."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
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


def profile_defaults_paths(
    profile: str = "full",
    feature: str = "none",
) -> tuple[Path, ...]:
    """Resolve the ordered defaults layers for one legal catalog variant."""
    catalog = load_catalog()
    key = variant_key(profile, feature)
    variant = catalog["variants"].get(key)
    if not isinstance(variant, dict):
        raise ValueError(f"unknown firmware profile variant: {key}")
    if variant.get("profile") != profile or variant.get("feature") != feature:
        raise ValueError(f"firmware profile identity mismatch for variant: {key}")

    resolved: list[Path] = []
    for relative in variant.get("defaults", []):
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


def read_profile_defaults(
    profile: str = "full",
    feature: str = "none",
) -> str:
    """Concatenate one profile exactly in the wrapper's declared order."""
    return "\n".join(
        path.read_text(encoding="utf-8").rstrip("\n")
        for path in profile_defaults_paths(profile, feature)
    ) + "\n"
