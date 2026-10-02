#!/usr/bin/env python3
"""Validate the Waveshare board YAML before selecting its compiler profile."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[2]
EXPECTED_MODULES = {
    "display": ("co5300", "ui"),
    "touch": ("cst9217", "service"),
    "i2c": ("esp-idf-master", "service"),
    "internal_storage": ("littlefs-nvs", "service"),
    "pmu": ("axp2101", "off"),
    "battery": ("axp2101-battery", "off"),
    "rtc": ("pcf85063", "off"),
    "imu": ("qmi8658", "off"),
    "gnss": ("lc76g", "off"),
    "audio_output": ("es8311", "off"),
    "microphones": ("es7210-dual", "off"),
    "sd": ("sdmmc", "off"),
    "wifi": ("esp32s3", "off"),
    "bluetooth": ("esp32s3-ble", "off"),
    "distance": ("tof400f-dual-uart", "off"),
}
OPTIONAL_MODULES = frozenset({
    "pmu", "battery", "imu", "gnss", "audio_output", "microphones", "sd"
})


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
    return validate_board_config_data(data)


def validate_board_config_data(data: object) -> dict[str, str]:
    """Validate the reviewed build contract, including bundled simulator input."""
    if not isinstance(data, dict) or set(data) != {
        "schema_version", "name", "board", "variant", "modules", "ui"
    }:
        raise ValueError("board YAML has missing or unknown top-level fields")
    if data["schema_version"] != 2 or type(data["schema_version"]) is not int:
        raise ValueError("unsupported board YAML schema_version")
    if data["board"] != "waveshare-esp32s3-touch-amoled-1.75":
        raise ValueError("this board has no registered build adapter")
    if data["name"] == "leg-edition":
        if data["variant"] != "leg-edition":
            raise ValueError("Leg Edition requires its dedicated compiler profile")
        modules = data["modules"]
        if not isinstance(modules, dict) or set(modules) != set(EXPECTED_MODULES):
            raise ValueError("Leg Edition board YAML has missing or unknown modules")
        enabled = {"pmu", "battery", "imu", "audio_output", "sd"}
        for name, (driver, default_mode) in EXPECTED_MODULES.items():
            expected_mode = (
                "service" if name in enabled else default_mode
            )
            if name in {"rtc", "gnss", "microphones"}:
                expected_mode = "off"
            module = modules[name]
            if (not isinstance(module, dict) or
                    module != {"driver": driver, "mode": expected_mode}):
                raise ValueError(
                    f"Leg Edition {name} must be {driver}/{expected_mode}"
                )
        if data["ui"] != {
            "app": "leg-edition",
            "screens": [
                "accelerometer", "gyroscope", "combo", "movement", "settings"
            ],
        }:
            raise ValueError("Leg Edition UI must declare its five views")
        return {
            "board": data["board"],
            "profile": "leg-edition",
            "feature": "none",
            "modules": {name: modules[name]["mode"] for name in modules},
        }
    if data["name"] != "ui-bootstrap":
        raise ValueError("unsupported board build name")
    if data["variant"] != "ui-minimal":
        raise ValueError("board build variant has no compiled contract")
    modules = data["modules"]
    if not isinstance(modules, dict) or set(modules) != set(EXPECTED_MODULES):
        raise ValueError("board YAML has missing or unknown modules")
    for name, expected in EXPECTED_MODULES.items():
        module = modules[name]
        if not isinstance(module, dict) or set(module) != {"driver", "mode"}:
            raise ValueError(f"{name} must declare exactly driver and mode")
        driver, mode = module["driver"], module["mode"]
        allowed_modes = {"off", "service"} if name in OPTIONAL_MODULES else {expected[1]}
        if driver != expected[0] or mode not in allowed_modes:
            raise ValueError(
                f"{name} configuration {(driver, mode)!r} has no reviewed compiler variant"
            )
    if modules["battery"]["mode"] == "service" and modules["pmu"]["mode"] != "service":
        raise ValueError("battery service requires pmu service")
    if data["ui"] != {"app": "shell", "screens": []}:
        raise ValueError("ui-bootstrap must compile the empty shell app")
    modes = {name: modules[name]["mode"] for name in sorted(OPTIONAL_MODULES)}
    return {
        "board": data["board"],
        "profile": "ui-minimal",
        "feature": "none",
        "modules": modes,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    print(json.dumps(load_board_config(args.path), separators=(",", ":")))


if __name__ == "__main__":
    main()
