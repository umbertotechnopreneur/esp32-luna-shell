#!/usr/bin/env python3
"""Verify the default-off TOF build gate and generated UI variant."""

from __future__ import annotations

from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common.source_contracts import read_component_source

# Direct command execution resolves the shared repository package explicitly.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import json
import re
from typing import Any

from scripts.common.firmware_profile import read_profile_defaults


REPO_ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    path = REPO_ROOT / relative_path
    try:
        return read_component_source(path)
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
        r"(?ms)^config LUNASHELL_BUILD_ENABLE_DISTANCE\n"
        r"(?P<body>.*?)(?=^(?:choice|config|menu|endmenu)\b)",
        kconfig,
    )
    require(enable_match is not None, "missing LUNASHELL_BUILD_ENABLE_DISTANCE")
    require(
        re.search(r"(?m)^\s+default n\s*$", enable_match.group("body"))
        is not None,
        "LUNASHELL_BUILD_ENABLE_DISTANCE must default to n",
    )

    driver_choice = re.search(
        r"(?ms)^choice LUNASHELL_BUILD_DISTANCE_DRIVER\n"
        r"(?P<body>.*?)^endchoice\s*$",
        kconfig,
    )
    require(driver_choice is not None, "missing distance-driver choice")
    require(
        "depends on LUNASHELL_BUILD_ENABLE_DISTANCE" in driver_choice.group("body"),
        "distance-driver choice must depend on the distance feature",
    )
    require(
        "default LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART"
        in driver_choice.group("body"),
        "TOF400F must be the selected driver when the feature is enabled",
    )

    defaults = read_profile_defaults()
    require(
        re.search(
            r"(?m)^CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE=y\s*$",
            defaults,
        )
        is None,
        "standard full profile must not enable distance support",
    )
    require(
        "# CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE is not set" in defaults,
        "standard full profile must record distance support as default-off",
    )
    distance_defaults = read_profile_defaults("full", "distance")
    require(
        "CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE=y" in distance_defaults,
        "distance overlay must enable distance support",
    )
    require(
        "CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART=y"
        in distance_defaults,
        "distance overlay must select the dual-UART TOF400F driver",
    )

    platform_cmake = read("components/LUNASHELL_platform/CMakeLists.txt")
    require(
        '"src/distance/LUNASHELL_distance_service.cpp"' in platform_cmake
        and '"src/distance/LUNASHELL_distance_service_stub.cpp"' in platform_cmake,
        "LUNASHELL_platform must select full and stub distance services",
    )
    require(
        "CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART"
        in platform_cmake,
        "LUNASHELL_platform must bind the service to the selected TOF driver",
    )
    require(
        "idf_component_optional_requires(PRIVATE LUNASHELL_tof400f)"
        in platform_cmake,
        "the full distance service must use a private LUNASHELL_tof400f dependency",
    )

    hardware = read("components/LUNASHELL_platform/src/snapshot/LUNASHELL_hardware.cpp")
    require(
        "#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && "
        "CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART"
        in hardware,
        "hardware startup must use the same feature-and-driver gate",
    )
    require(
        "set_component(snapshot.distance, false, ESP_ERR_NOT_SUPPORTED);"
        in hardware,
        "disabled or driverless distance must be explicitly unavailable",
    )

    app_cmake = read("components/LUNASHELL_app/CMakeLists.txt")
    for required in (
        "CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART",
        "screen_tof.json",
        "screen_tof_settings.json",
        "LUNASHELL_settings_slider.json",
        'res/.variants"',
        "LUNASHELL_prepare_runtime_app_package",
        "LittleFS -> prepare -> stage + verify",
    ):
        require(required in app_cmake, f"missing UI staging gate: {required}")
    require(
        re.search(
            r"if\(CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE AND\s+"
            r"CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART\)",
            app_cmake,
        )
        is not None,
        "distance UI staging must use the same feature-and-driver gate",
    )
    require(
        "DEPENDS\n            ${LUNASHELL_app_package_stage_targets}\n"
        "            LUNASHELL_verify_lazy_gui_documents" in app_cmake,
        "package preparation must wait for raw staging and GUI verification",
    )
    require(
        "set(LUNASHELL_runtime_stage_targets ${LUNASHELL_previous_runtime_stage_targets})"
        in app_cmake
        and "list(APPEND LUNASHELL_runtime_stage_targets "
        "LUNASHELL_prepare_runtime_app_package)" in app_cmake,
        "LittleFS stage property must replace the raw app target with prepare",
    )
    require(
        "GLOBAL APPEND PROPERTY" not in app_cmake,
        "raw and prepared package targets must not remain unordered peers",
    )
    minify_command_pattern = (
        r'COMMAND\s+"\$\{PYTHON\}"\s+'
        r'"\$\{PROJECT_DIR\}/scripts/generators/minify_json_tree\.py"'
    )
    minify_positions = [
        match.start() for match in re.finditer(minify_command_pattern, app_cmake)
    ]
    require(
        len(minify_positions) == 2,
        "both runtime package profiles must minify their staged JSON",
    )
    variant_marker = '"${LUNASHELL_app_staged_package_dir}/res/.variants"'
    for position in minify_positions:
        target_start = app_cmake.rfind("add_custom_target(", 0, position)
        variant_removal = app_cmake.rfind(variant_marker, target_start, position)
        require(
            variant_removal >= 0,
            "staged JSON must be minified only after profile variants are filtered",
        )

    project_cmake = read("CMakeLists.txt")
    require(
        re.search(
            r"add_dependencies\(\s*"
            r"littlefs_littlefs_data_bin\s+"
            r"LUNASHELL_prepare_runtime_app_package\s*\)",
            project_cmake,
        )
        is not None,
        "LittleFS must wait for filtered LUNASHELL package preparation",
    )
    require(
        "if(CONFIG_LUNASHELL_BUILD_ENABLE_UI)" in project_cmake
        and "if(NOT TARGET littlefs_littlefs_data_bin)" in project_cmake
        and "if(NOT TARGET LUNASHELL_prepare_runtime_app_package)"
        in project_cmake,
        "UI package ordering must be guarded and fail during configuration",
    )

    generator = read("scripts/generators/generate_LUNASHELL_gui_documents.py")
    require(
        "distance_disabled_hardware_document" in generator
        and "DISTANCE_DISABLED_HARDWARE_PATH" in generator,
        "GUI generator must derive the distance-disabled hardware variant",
    )

    canonical_hardware = load_json(
        "components/LUNASHELL_app/package/res/screen_hardware.json"
    )
    disabled_hardware = load_json(
        "components/LUNASHELL_app/package/res/.variants/"
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
        "components/LUNASHELL_app/package/res/screen_tof.json",
        "components/LUNASHELL_app/package/res/screen_tof_settings.json",
        "components/LUNASHELL_app/package/res/templates/LUNASHELL_settings_slider.json",
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
