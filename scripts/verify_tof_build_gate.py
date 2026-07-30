#!/usr/bin/env python3
"""Verify the default-off TOF build gate and generated UI variant."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    path = REPO_ROOT / relative_path
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise RuntimeError(f"cannot read {relative_path}: {error}") from error


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def count_node_id(value: Any, target_id: str) -> int:
    if isinstance(value, dict):
        return (1 if value.get("id") == target_id else 0) + sum(
            count_node_id(child, target_id) for child in value.values()
        )
    if isinstance(value, list):
        return sum(count_node_id(child, target_id) for child in value)
    return 0


def load_json(relative_path: str) -> Any:
    path = REPO_ROOT / relative_path
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read JSON {relative_path}: {error}") from error


def main() -> int:
    kconfig = read("main/Kconfig.projbuild")
    enable_match = re.search(
        r"(?ms)^config QSEE_BUILD_ENABLE_DISTANCE\n"
        r"(?P<body>.*?)(?=^(?:choice|config|menu|endmenu)\b)",
        kconfig,
    )
    require(enable_match is not None, "missing QSEE_BUILD_ENABLE_DISTANCE")
    require(
        re.search(r"(?m)^\s+default n\s*$", enable_match.group("body"))
        is not None,
        "QSEE_BUILD_ENABLE_DISTANCE must default to n",
    )

    driver_choice = re.search(
        r"(?ms)^choice QSEE_BUILD_DISTANCE_DRIVER\n"
        r"(?P<body>.*?)^endchoice\s*$",
        kconfig,
    )
    require(driver_choice is not None, "missing distance-driver choice")
    require(
        "depends on QSEE_BUILD_ENABLE_DISTANCE" in driver_choice.group("body"),
        "distance-driver choice must depend on the distance feature",
    )
    require(
        "default QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART"
        in driver_choice.group("body"),
        "TOF400F must be the selected driver when the feature is enabled",
    )

    defaults = read("sdkconfig.defaults")
    require(
        re.search(
            r"(?m)^CONFIG_QSEE_BUILD_ENABLE_DISTANCE=y\s*$",
            defaults,
        )
        is None,
        "sdkconfig.defaults must not enable distance support",
    )
    require(
        "# CONFIG_QSEE_BUILD_ENABLE_DISTANCE is not set" in defaults,
        "sdkconfig.defaults must record the default-off distance profile",
    )

    platform_cmake = read("components/qsee_platform/CMakeLists.txt")
    require(
        '"qsee_distance_service.cpp"' in platform_cmake
        and '"qsee_distance_service_stub.cpp"' in platform_cmake,
        "qsee_platform must select full and stub distance services",
    )
    require(
        "CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART"
        in platform_cmake,
        "qsee_platform must bind the service to the selected TOF driver",
    )
    require(
        "idf_component_optional_requires(PRIVATE qsee_tof400f)"
        in platform_cmake,
        "the full distance service must use a private qsee_tof400f dependency",
    )

    hardware = read("components/qsee_platform/qsee_hardware.cpp")
    require(
        "#if CONFIG_QSEE_BUILD_ENABLE_DISTANCE && "
        "CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART"
        in hardware,
        "hardware startup must use the same feature-and-driver gate",
    )
    require(
        "set_component(snapshot.distance, false, ESP_ERR_NOT_SUPPORTED);"
        in hardware,
        "disabled or driverless distance must be explicitly unavailable",
    )

    app_cmake = read("components/qsee_app/CMakeLists.txt")
    for required in (
        "CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART",
        "screen_tof.json",
        "screen_tof_settings.json",
        "qsee_settings_slider.json",
        'res/.variants"',
        "qsee_prepare_runtime_app_package",
        "LittleFS -> prepare -> stage + verify",
    ):
        require(required in app_cmake, f"missing UI staging gate: {required}")
    require(
        re.search(
            r"if\(CONFIG_QSEE_BUILD_ENABLE_DISTANCE AND\s+"
            r"CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART\)",
            app_cmake,
        )
        is not None,
        "distance UI staging must use the same feature-and-driver gate",
    )
    require(
        "DEPENDS\n            ${qsee_app_package_stage_targets}\n"
        "            qsee_verify_lazy_gui_documents" in app_cmake,
        "package preparation must wait for raw staging and GUI verification",
    )
    require(
        "set(qsee_runtime_stage_targets ${qsee_previous_runtime_stage_targets})"
        in app_cmake
        and "list(APPEND qsee_runtime_stage_targets "
        "qsee_prepare_runtime_app_package)" in app_cmake,
        "LittleFS stage property must replace the raw app target with prepare",
    )
    require(
        "GLOBAL APPEND PROPERTY" not in app_cmake,
        "raw and prepared package targets must not remain unordered peers",
    )

    generator = read("scripts/generate_qsee_gui_documents.py")
    require(
        "distance_disabled_hardware_document" in generator
        and "DISTANCE_DISABLED_HARDWARE_PATH" in generator,
        "GUI generator must derive the distance-disabled hardware variant",
    )

    canonical_hardware = load_json(
        "components/qsee_app/package/res/screen_hardware.json"
    )
    disabled_hardware = load_json(
        "components/qsee_app/package/res/.variants/"
        "distance_disabled/screen_hardware.json"
    )
    require(
        count_node_id(canonical_hardware, "tof") == 1,
        "canonical hardware document must contain exactly one TOF card",
    )
    require(
        count_node_id(disabled_hardware, "tof") == 0,
        "distance-disabled hardware variant still contains the TOF card",
    )

    for relative_path in (
        "components/qsee_app/package/res/screen_tof.json",
        "components/qsee_app/package/res/screen_tof_settings.json",
        "components/qsee_app/package/res/templates/qsee_settings_slider.json",
    ):
        require(
            (REPO_ROOT / relative_path).is_file(),
            f"missing opt-in distance UI artifact {relative_path}",
        )

    print("TOF build-gate static checks passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"TOF build-gate check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
