#!/usr/bin/env python3
"""Validate the first board YAML before selecting its compiler profile."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_MODULES = {
    "display": ("co5300", "ui"),
    "touch": ("cst9217", "service"),
    "i2c": ("esp-idf-master", "service"),
    "internal_storage": ("littlefs-nvs", "service"),
    "pmu": ("axp2101", "off"),
    "rtc": ("pcf85063", "off"),
    "imu": ("qmi8658", "off"),
    "gnss": ("lc76g", "off"),
    "audio": ("es8311-es7210", "off"),
    "sd": ("sdmmc", "off"),
    "wifi": ("esp32s3", "off"),
    "bluetooth": ("esp32s3-ble", "off"),
    "distance": ("tof400f-dual-uart", "off"),
}


class UniqueKeyLoader(yaml.SafeLoader):
    pass


def construct_mapping(loader: UniqueKeyLoader, node: yaml.MappingNode) -> dict:
    result: dict = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node)
        if not isinstance(key, str):
            raise ValueError("board YAML mapping keys must be strings")
        if key in result:
            raise ValueError(f"duplicate board YAML key: {key}")
        result[key] = loader.construct_object(value_node)
    return result


UniqueKeyLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,
    construct_mapping,
)


def load_board_config(path: Path) -> dict[str, str]:
    resolved = path.resolve(strict=True)
    if not resolved.is_relative_to(ROOT / "config" / "boards"):
        raise ValueError("board YAML must be under config/boards/")
    data = yaml.load(resolved.read_text(encoding="utf-8"), Loader=UniqueKeyLoader)
    if not isinstance(data, dict) or set(data) != {
        "schema_version", "name", "board", "variant", "modules", "ui"
    }:
        raise ValueError("board YAML has missing or unknown top-level fields")
    if data["schema_version"] != 1 or type(data["schema_version"]) is not int:
        raise ValueError("unsupported board YAML schema_version")
    if data["name"] != "ui-bootstrap":
        raise ValueError("unsupported board build name")
    if data["board"] != "waveshare-esp32s3-touch-amoled-1.75":
        raise ValueError("this board has no registered build adapter")
    if data["variant"] != "ui-minimal":
        raise ValueError("board build variant has no compiled contract")
    modules = data["modules"]
    if not isinstance(modules, dict) or set(modules) != set(EXPECTED_MODULES):
        raise ValueError("board YAML has missing or unknown modules")
    for name, expected in EXPECTED_MODULES.items():
        module = modules[name]
        if not isinstance(module, dict) or set(module) != {"driver", "mode"}:
            raise ValueError(f"{name} must declare exactly driver and mode")
        actual = (module["driver"], module["mode"])
        if actual != expected:
            raise ValueError(
                f"{name} configuration {actual!r} has no reviewed compiler variant"
            )
    if data["ui"] != {"app": "shell", "screens": []}:
        raise ValueError("ui-bootstrap must compile the empty shell app")
    return {"board": data["board"], "profile": "ui-minimal", "feature": "none"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    print(json.dumps(load_board_config(args.path), separators=(",", ":")))


if __name__ == "__main__":
    main()
