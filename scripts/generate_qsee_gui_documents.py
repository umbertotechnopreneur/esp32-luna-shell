#!/usr/bin/env python3
"""Generate the QSee bootstrap and lazy screen documents.

The editable source remains one complete document so template and screen-flow
relationships stay easy to review. The firmware package receives a small
bootstrap plus one independently loadable document for every content screen
other than Welcome.
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import sys
from collections import deque
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = REPO_ROOT / "components" / "qsee_app" / "gui" / "root.json"
OUTPUT_DIR = REPO_ROOT / "components" / "qsee_app" / "package" / "res"
BOOTSTRAP_PATH = OUTPUT_DIR / "root.json"
VARIANT_ROOT = OUTPUT_DIR / ".variants"
DISTANCE_DISABLED_VARIANT_DIR = VARIANT_ROOT / "distance_disabled"
DISTANCE_DISABLED_HARDWARE_PATH = (
    DISTANCE_DISABLED_VARIANT_DIR / "screen_hardware.json"
)
CONTENT_FLOW_ID = "qsee_content"
WELCOME_SCREEN_ID = "qsee_welcome"
IDLE_SCREEN_ID = "qsee_content_idle"
RELEASE_CONTENT_ACTION = "qsee.content.release"
BACKGROUND_SCREEN_ID = "qsee_background_screen"
HARDWARE_SCREEN_ID = "qsee_hardware"
TOF_SCREEN_ID = "qsee_tof"
TOF_SETTINGS_SCREEN_ID = "qsee_tof_settings"
TOF_HARDWARE_CARD_ID = "tof"
TOF_SETTINGS_SLIDER_PATH = "templates/qsee_settings_slider.json"
BOOTSTRAP_FLOW_IDS = {
    "qsee_background_flow",
    "qsee_chrome_flow",
    CONTENT_FLOW_ID,
}
GENERATED_SCREEN_GLOB = "screen_*.json"
LAZY_SCREEN_ID_PATTERN = re.compile(r"qsee_[a-z0-9]+(?:_[a-z0-9]+)*")
SUPPORTED_EXTERNAL_ASSET_TYPES = {
    "interactionTemplate",
    "viewScreen",
    "viewTemplate",
}


class GenerationError(RuntimeError):
    """Raised when the canonical GUI cannot be split without ambiguity."""


def load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise GenerationError(f"cannot read JSON object {path}: {error}") from error
    if not isinstance(value, dict):
        raise GenerationError(f"expected a JSON object in {path}")
    return value


def asset_id(asset: dict[str, Any], source: str) -> str:
    value = asset.get("id")
    if not isinstance(value, str) or not value:
        raise GenerationError(f"asset in {source} has no non-empty id")
    return value


def collect_external_refs(value: Any) -> set[str]:
    refs: set[str] = set()

    def visit(node: Any) -> None:
        if isinstance(node, dict):
            if node.get("type") == "templateRef":
                template_id = node.get("templateId")
                if not isinstance(template_id, str) or not template_id:
                    raise GenerationError("templateRef has no non-empty templateId")
                refs.add(template_id)
            interaction_refs = node.get("interactionRefs")
            if interaction_refs is not None:
                if not isinstance(interaction_refs, list) or not all(
                    isinstance(interaction_id, str) and interaction_id
                    for interaction_id in interaction_refs
                ):
                    raise GenerationError("interactionRefs must contain non-empty ids")
                refs.update(interaction_refs)
            for child in node.values():
                visit(child)
        elif isinstance(node, list):
            for child in node:
                visit(child)

    visit(value)
    return refs


def lazy_screen_filename(screen_id: str) -> str:
    if LAZY_SCREEN_ID_PATTERN.fullmatch(screen_id) is None:
        raise GenerationError(
            f"lazy screen id {screen_id!r} cannot be mapped to a safe filename"
        )
    return f"screen_{screen_id.removeprefix('qsee_')}.json"


def screen_flow_screen_ids(flow: dict[str, Any]) -> set[str]:
    screens = flow.get("screens")
    if not isinstance(screens, list) or not all(
        isinstance(screen, str) and screen for screen in screens
    ):
        raise GenerationError(
            f"screenFlow {asset_id(flow, 'canonical document')} has invalid screens"
        )
    return set(screens)


def canonical_content_flow(flow: dict[str, Any]) -> dict[str, Any]:
    initial = flow.get("initial")
    if initial != WELCOME_SCREEN_ID:
        raise GenerationError(
            f"{CONTENT_FLOW_ID} must start at {WELCOME_SCREEN_ID}, found {initial!r}"
        )

    transitions = flow.get("transitions")
    if not isinstance(transitions, list):
        raise GenerationError(f"{CONTENT_FLOW_ID} has invalid transitions")
    if any(
        isinstance(transition, dict)
        and transition.get("action") == RELEASE_CONTENT_ACTION
        for transition in transitions
    ):
        raise GenerationError(
            f"{CONTENT_FLOW_ID} uses reserved action {RELEASE_CONTENT_ACTION}"
        )
    welcome_transitions = [
        transition
        for transition in transitions
        if isinstance(transition, dict)
        and transition.get("to") == WELCOME_SCREEN_ID
    ]
    if not welcome_transitions:
        raise GenerationError(
            f"{CONTENT_FLOW_ID} has no transition back to {WELCOME_SCREEN_ID}"
        )

    result = dict(flow)
    result["screens"] = [WELCOME_SCREEN_ID, IDLE_SCREEN_ID]
    result["initial"] = WELCOME_SCREEN_ID
    result["transitions"] = [
        *welcome_transitions,
        {
            "from": [],
            "action": RELEASE_CONTENT_ACTION,
            "to": IDLE_SCREEN_ID,
        },
    ]
    return result


def idle_content_screen() -> dict[str, Any]:
    return {
        "type": "viewScreen",
        "id": IDLE_SCREEN_ID,
        "mountMode": "dynamic",
        "commonProps": {
            "clickable": False,
            "scrollable": False,
        },
        "style": {
            "bgOpacity": 0,
            "padding": 0,
        },
        "children": [],
    }


def render(document: dict[str, Any]) -> str:
    return json.dumps(document, indent=4, ensure_ascii=False) + "\n"


def remove_nodes_by_id(value: Any, target_id: str) -> int:
    """Remove matching descendant nodes in place and return the match count."""
    removed = 0
    if isinstance(value, list):
        retained: list[Any] = []
        for child in value:
            if isinstance(child, dict) and child.get("id") == target_id:
                removed += 1
                continue
            removed += remove_nodes_by_id(child, target_id)
            retained.append(child)
        value[:] = retained
    elif isinstance(value, dict):
        for child in value.values():
            removed += remove_nodes_by_id(child, target_id)
    return removed


def distance_disabled_hardware_document(
    document: dict[str, Any],
) -> dict[str, Any]:
    variant = copy.deepcopy(document)
    removed = remove_nodes_by_id(variant, TOF_HARDWARE_CARD_ID)
    if removed != 1:
        raise GenerationError(
            "distance-disabled hardware variant must remove exactly one "
            f"{TOF_HARDWARE_CARD_ID!r} card, removed {removed}"
        )
    return variant


def build_documents() -> dict[Path, str]:
    canonical = load_object(SOURCE_PATH)
    version = canonical.get("version")
    assets = canonical.get("assets")
    if not isinstance(version, str) or not version:
        raise GenerationError("canonical GUI has no non-empty version")
    if not isinstance(assets, list):
        raise GenerationError("canonical GUI has no assets array")

    constants: list[dict[str, Any]] = []
    inline_screens: dict[str, dict[str, Any]] = {}
    flows: dict[str, dict[str, Any]] = {}
    external_paths: list[str] = []

    for asset in assets:
        if isinstance(asset, str):
            if not asset:
                raise GenerationError("canonical GUI contains an empty asset path")
            external_paths.append(asset)
            continue
        if not isinstance(asset, dict):
            raise GenerationError("canonical GUI contains a non-object asset")

        asset_type = asset.get("type")
        if asset_type == "constant":
            constants.append(asset)
        elif asset_type == "viewScreen":
            identifier = asset_id(asset, "canonical document")
            if identifier in inline_screens:
                raise GenerationError(f"duplicate inline screen id {identifier}")
            inline_screens[identifier] = asset
        elif asset_type == "screenFlow":
            identifier = asset_id(asset, "canonical document")
            if identifier in flows:
                raise GenerationError(f"duplicate screen flow id {identifier}")
            flows[identifier] = asset
        else:
            raise GenerationError(
                "unsupported inline shared asset type "
                f"{asset_type!r}; move it to an external asset before splitting"
            )

    required_bootstrap_screens = {WELCOME_SCREEN_ID, BACKGROUND_SCREEN_ID}
    missing_bootstrap_screens = required_bootstrap_screens - inline_screens.keys()
    if missing_bootstrap_screens:
        raise GenerationError(
            "canonical GUI is missing bootstrap screens: "
            + ", ".join(sorted(missing_bootstrap_screens))
        )
    missing_bootstrap_flows = BOOTSTRAP_FLOW_IDS - flows.keys()
    if missing_bootstrap_flows:
        raise GenerationError(
            "canonical GUI is missing bootstrap flows: "
            + ", ".join(sorted(missing_bootstrap_flows))
        )
    if IDLE_SCREEN_ID in inline_screens:
        raise GenerationError(
            f"canonical GUI uses reserved screen id {IDLE_SCREEN_ID}"
        )

    expected_content_screens = set(inline_screens) - {BACKGROUND_SCREEN_ID}
    actual_content_screens = screen_flow_screen_ids(flows[CONTENT_FLOW_ID])
    if actual_content_screens != expected_content_screens:
        missing = expected_content_screens - actual_content_screens
        unexpected = actual_content_screens - expected_content_screens
        details: list[str] = []
        if missing:
            details.append("missing " + ", ".join(sorted(missing)))
        if unexpected:
            details.append("unexpected " + ", ".join(sorted(unexpected)))
        raise GenerationError(
            f"{CONTENT_FLOW_ID} must own every content screen: "
            + "; ".join(details)
        )

    external_by_id: dict[str, tuple[str, dict[str, Any]]] = {}
    for relative_path in external_paths:
        external_path = OUTPUT_DIR / relative_path
        external = load_object(external_path)
        identifier = asset_id(external, relative_path)
        if (
            identifier == IDLE_SCREEN_ID
            or identifier in external_by_id
            or identifier in inline_screens
        ):
            raise GenerationError(f"duplicate GUI asset id {identifier}")
        asset_type = external.get("type")
        if asset_type not in SUPPORTED_EXTERNAL_ASSET_TYPES:
            raise GenerationError(
                f"external asset {relative_path} has unsupported type {asset_type!r}"
            )
        external_by_id[identifier] = (relative_path, external)

    all_screen_ids = set(inline_screens)
    all_screen_ids.add(IDLE_SCREEN_ID)
    all_screen_ids.update(
        identifier
        for identifier, (_, asset) in external_by_id.items()
        if asset.get("type") == "viewScreen"
    )

    def external_dependencies(nodes: list[dict[str, Any]]) -> list[str]:
        pending = deque(
            sorted(set().union(*(collect_external_refs(node) for node in nodes)))
        )
        required_ids: set[str] = set()

        for node in nodes:
            if node.get("type") == "screenFlow":
                for screen_id in screen_flow_screen_ids(node):
                    if screen_id not in all_screen_ids:
                        raise GenerationError(
                            f"screenFlow {asset_id(node, 'canonical document')} "
                            f"references unknown screen {screen_id}"
                        )
                    if screen_id in external_by_id:
                        pending.append(screen_id)

        while pending:
            identifier = pending.popleft()
            if identifier in required_ids:
                continue
            external_entry = external_by_id.get(identifier)
            if external_entry is None:
                raise GenerationError(
                    f"document references unknown external asset {identifier}"
                )
            required_ids.add(identifier)
            pending.extend(sorted(collect_external_refs(external_entry[1])))

        required_paths = {
            external_by_id[identifier][0]
            for identifier in required_ids
        }
        return [
            relative_path
            for relative_path in external_paths
            if relative_path in required_paths
        ]

    bootstrap_nodes: list[dict[str, Any]] = [
        inline_screens[BACKGROUND_SCREEN_ID],
        flows["qsee_background_flow"],
        inline_screens[WELCOME_SCREEN_ID],
        idle_content_screen(),
        flows["qsee_chrome_flow"],
        canonical_content_flow(flows[CONTENT_FLOW_ID]),
    ]
    bootstrap_assets: list[Any] = [*constants]
    bootstrap_assets.extend(external_dependencies(bootstrap_nodes))
    bootstrap_assets.extend(bootstrap_nodes)

    documents: dict[Path, str] = {
        BOOTSTRAP_PATH: render({"version": version, "assets": bootstrap_assets})
    }
    for screen_id, screen in sorted(inline_screens.items()):
        if screen_id in required_bootstrap_screens:
            continue
        if screen.get("mountMode") != "dynamic":
            raise GenerationError(
                f"lazy screen {screen_id} must declare mountMode 'dynamic'"
            )
        screen_assets: list[Any] = [*constants]
        screen_assets.extend(external_dependencies([screen]))
        screen_assets.append(screen)
        filename = lazy_screen_filename(screen_id)
        screen_document = {"version": version, "assets": screen_assets}
        documents[OUTPUT_DIR / filename] = render(screen_document)
        if screen_id == HARDWARE_SCREEN_ID:
            documents[DISTANCE_DISABLED_HARDWARE_PATH] = render(
                distance_disabled_hardware_document(screen_document)
            )

    required_distance_paths = {
        OUTPUT_DIR / lazy_screen_filename(TOF_SCREEN_ID),
        OUTPUT_DIR / lazy_screen_filename(TOF_SETTINGS_SCREEN_ID),
        DISTANCE_DISABLED_HARDWARE_PATH,
    }
    missing_distance_paths = required_distance_paths - documents.keys()
    if missing_distance_paths:
        raise GenerationError(
            "canonical GUI is missing distance feature documents: "
            + ", ".join(
                str(path.relative_to(REPO_ROOT))
                for path in sorted(missing_distance_paths)
            )
        )

    slider_consumers = {
        path
        for path, contents in documents.items()
        if f'"{TOF_SETTINGS_SLIDER_PATH}"' in contents
    }
    expected_slider_consumers = {
        OUTPUT_DIR / lazy_screen_filename(TOF_SETTINGS_SCREEN_ID)
    }
    if slider_consumers != expected_slider_consumers:
        raise GenerationError(
            f"{TOF_SETTINGS_SLIDER_PATH} must be exclusive to "
            f"{TOF_SETTINGS_SCREEN_ID}, found "
            + ", ".join(
                str(path.relative_to(REPO_ROOT))
                for path in sorted(slider_consumers)
            )
        )

    return documents


def check_documents(documents: dict[Path, str]) -> bool:
    ok = True
    expected_paths = set(documents)
    actual_screen_paths = set(OUTPUT_DIR.glob(GENERATED_SCREEN_GLOB))
    expected_variant_paths = {
        path for path in expected_paths if VARIANT_ROOT in path.parents
    }
    actual_variant_paths = (
        set(VARIANT_ROOT.rglob("*.json")) if VARIANT_ROOT.exists() else set()
    )

    for stale_path in sorted(actual_screen_paths - expected_paths):
        print(f"stale lazy GUI document: {stale_path.relative_to(REPO_ROOT)}")
        ok = False
    for stale_path in sorted(actual_variant_paths - expected_variant_paths):
        print(f"stale GUI variant document: {stale_path.relative_to(REPO_ROOT)}")
        ok = False
    for path, expected in sorted(documents.items()):
        try:
            actual = path.read_text(encoding="utf-8")
        except OSError as error:
            print(f"missing generated GUI document {path.relative_to(REPO_ROOT)}: {error}")
            ok = False
            continue
        if actual != expected:
            print(f"out-of-date generated GUI document: {path.relative_to(REPO_ROOT)}")
            ok = False
    return ok


def write_documents(documents: dict[Path, str]) -> None:
    resolved_output = OUTPUT_DIR.resolve()
    if resolved_output != (
        REPO_ROOT / "components" / "qsee_app" / "package" / "res"
    ).resolve():
        raise GenerationError(f"refusing to write unexpected output directory {resolved_output}")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    expected_paths = set(documents)
    expected_variant_paths = {
        path for path in expected_paths if VARIANT_ROOT in path.parents
    }
    for stale_path in OUTPUT_DIR.glob(GENERATED_SCREEN_GLOB):
        if stale_path not in expected_paths:
            stale_path.unlink()
    if VARIANT_ROOT.exists():
        for stale_path in VARIANT_ROOT.rglob("*.json"):
            if stale_path not in expected_variant_paths:
                stale_path.unlink()
    for path, contents in documents.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify generated files without modifying the workspace",
    )
    args = parser.parse_args()

    try:
        documents = build_documents()
        if args.check:
            return 0 if check_documents(documents) else 1
        write_documents(documents)
    except GenerationError as error:
        print(f"QSee GUI generation failed: {error}", file=sys.stderr)
        return 2

    print(
        f"generated {len(documents)} QSee GUI documents "
        f"from {SOURCE_PATH.relative_to(REPO_ROOT)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
