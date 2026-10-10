#!/usr/bin/env python3
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

"""Verify that package and LittleFS generation remain timestamp-driven."""

from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common.source_contracts import read_component_source


ROOT = Path(__file__).resolve().parents[2]


# relative: Requested source; split implementation families are read together.
# Raises OSError or UnicodeError when a required source is unavailable.
def read(relative: str) -> str:
    return read_component_source(ROOT / relative)


def require(text: str, fragment: str, label: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{label} is missing {fragment!r}")


def main() -> None:
    app_cmake = read("components/LUNASHELL_app/CMakeLists.txt")
    for forbidden in (
        "LUNASHELL_verify_lazy_gui_documents ALL",
        "LUNASHELL_prepare_runtime_app_package ALL",
    ):
        if forbidden in app_cmake:
            raise AssertionError(f"unconditional package target returned: {forbidden}")
    for fragment in (
        "GLOB_RECURSE",
        "CONFIGURE_DEPENDS",
        "LUNASHELL_gui_verification_stamp",
        "LUNASHELL_app_package_input_manifest",
        "LUNASHELL_app_package_profile_signature",
        "LUNASHELL_app_raw_stage_stamp",
        "LUNASHELL_app_package_prepare_stamp",
        "BYPRODUCTS",
        "LUNASHELL_RUNTIME_APP_PREPARE_STAMP",
    ):
        require(app_cmake, fragment, "incremental package graph")

    root_cmake = read("CMakeLists.txt")
    if "littlefs_create_partition_image(" in root_cmake or "littlefs_create_partition_image(" in read("main/CMakeLists.txt"):
        raise AssertionError("the upstream unconditional LittleFS target is still used")
    for fragment in (
        "BROOKESIA_SYSTEM_CORE_ESP_RESOURCE_STAGE_ROOT",
        '"${CMAKE_BINARY_DIR}/littlefs"',
        "LUNASHELL_RUNTIME_APP_PREPARE_STAMP",
        "lunashell_create_incremental_littlefs_image(",
        "littlefs_littlefs_data_bin",
        "LUNASHELL_prepare_runtime_app_package",
    ):
        require(root_cmake, fragment, "incremental LittleFS graph")

    helper = read("cmake/LUNASHELLIncrementalLittleFS.cmake")
    for fragment in (
        "joltwallet__littlefs",
        'OUTPUT "${littlefs_venv_stamp}"',
        'OUTPUT "${image_file}"',
        "BYPRODUCTS",
        "littlefs_${partition}_bin ALL",
        'DEPENDS "${image_file}"',
        "add_dependencies(flash littlefs_${partition}_bin)",
    ):
        require(helper, fragment, "incremental LittleFS helper")
    target_block = helper.split("add_custom_target(\n        littlefs_${partition}_bin ALL", 1)[1]
    target_block = target_block.split(")", 1)[0]
    if "COMMAND" in target_block:
        raise AssertionError("LittleFS ALL target must be command-free")

    print("Incremental build graph verification passed")


if __name__ == "__main__":
    main()
