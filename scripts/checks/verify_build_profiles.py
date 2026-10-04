#!/usr/bin/env python3
"""Validate the versioned firmware-profile and dependency-closure contract."""

import json
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common.source_contracts import read_component_source
import re


ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = ROOT / "config" / "sdkconfig" / "v1" / "catalog.json"


# relative: Requested source; split implementation families are read together.
# Raises OSError or UnicodeError when a required source is unavailable.
def read(relative: str) -> str:
    return read_component_source(ROOT / relative)


def require(text: str, fragment: str, label: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{label} is missing {fragment!r}")


def resolve_relative(relative: object, label: str) -> Path:
    if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
        raise AssertionError(f"{label} must be a non-empty repository-relative path")
    resolved = (ROOT / relative).resolve(strict=False)
    try:
        resolved.relative_to(ROOT)
    except ValueError as error:
        raise AssertionError(f"{label} escapes the repository: {relative}") from error
    return resolved


# Raises AssertionError when a profile or its resolved dependency lock violates the contract.
def main() -> None:
    catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if catalog.get("schema_version") != 1:
        raise AssertionError("profile catalog schema_version must be 1")
    if catalog.get("default_variant") != "cody-companion":
        raise AssertionError("cody-companion must be the default firmware variant in this branch")
    variants = catalog.get("variants")
    if not isinstance(variants, dict) or set(variants) != {
        "headless",
        "display-touch",
        "cody-companion",
        "leg-edition",
        "ui-minimal",
        "connected",
        "full",
        "full-distance",
        "cody-companion-wake-word",
        "full-wake-word",
    }:
        raise AssertionError("profile catalog variants are incomplete or unexpected")

    identities: set[tuple[str, str]] = set()
    locks: set[Path] = set()
    sdkconfigs: set[Path] = set()
    build_dirs: set[str] = set()
    for variant_name, variant in variants.items():
        if not isinstance(variant, dict):
            raise AssertionError(f"variant {variant_name} must be an object")
        profile_name = variant.get("profile")
        identity = (profile_name, variant.get("feature"))
        expected_name = (
            identity[0] if identity[1] == "none" else f"{identity[0]}-{identity[1]}"
        )
        if expected_name != variant_name or identity in identities:
            raise AssertionError(f"variant identity mismatch: {variant_name}")
        identities.add(identity)

        defaults = variant.get("defaults")
        if not isinstance(defaults, list) or len(defaults) < 4:
            raise AssertionError(f"variant {variant_name} has no complete defaults chain")
        for index, default in enumerate(defaults):
            path = resolve_relative(default, f"{variant_name}.defaults[{index}]")
            if not path.is_file() or "config/sdkconfig/v1/" not in path.as_posix():
                raise AssertionError(f"missing versioned defaults file: {default}")

        roots = variant.get("component_roots")
        if not isinstance(roots, list) or "main" not in roots or len(roots) != len(set(roots)):
            raise AssertionError(f"variant {variant_name} has invalid component roots")
        if profile_name == "headless" and roots != ["main"]:
            raise AssertionError("headless must admit only the main transitive root")
        if profile_name in {"connected", "full"}:
            for required_root in ("esp_http_server", "espressif__network_provisioning"):
                if required_root not in roots:
                    raise AssertionError(f"{variant_name} is missing root {required_root}")
        if profile_name == "cody-companion":
            if "espressif__network_provisioning" not in roots or "esp_http_server" in roots:
                raise AssertionError("Cody rig PoC requires provisioning with HTTP diagnostics excluded")
        if profile_name not in {"headless", "display-touch", "cody-companion"} and "espressif__esp_board_manager" not in roots:
            raise AssertionError(f"{variant_name} is missing the HAL board-manager root")
        if profile_name in {"display-touch", "cody-companion"}:
            if "esp32_s3_touch_amoled_1_75" not in roots:
                raise AssertionError(f"{variant_name} must admit the physical BSP")
            if any("lvgl" in root or "brookesia" in root or root == "LUNASHELL_app" for root in roots):
                raise AssertionError(f"{variant_name} must not admit a GUI root")
        if profile_name == "cody-companion" and not {"CODY2D", "CODY_companion"}.issubset(roots):
            raise AssertionError("cody-companion must admit its local renderer and consumer")

        component_dirs = variant.get("component_dirs")
        if not isinstance(component_dirs, list) or len(component_dirs) != len(
            set(component_dirs)
        ):
            raise AssertionError(f"variant {variant_name} has invalid component dirs")
        resolved_component_dirs = [
            resolve_relative(path, f"{variant_name}.component_dirs")
            for path in component_dirs
        ]
        if any(not (path / "CMakeLists.txt").is_file() for path in resolved_component_dirs):
            raise AssertionError(f"variant {variant_name} has a non-component directory")
        expected_base_dirs = {
            (ROOT / "main").resolve(),
            (ROOT / "components/LUNASHELL_logger").resolve(),
            (ROOT / "components/LUNASHELL_platform").resolve(),
        }
        if not expected_base_dirs.issubset(resolved_component_dirs):
            raise AssertionError(f"variant {variant_name} is missing a base component dir")
        app_dir = (ROOT / "components/LUNASHELL_app").resolve()
        bsp_dir = (ROOT / "components/esp32_s3_touch_amoled_1_75").resolve()
        if profile_name == "headless" and (
            app_dir in resolved_component_dirs or bsp_dir in resolved_component_dirs
        ):
            raise AssertionError("headless must not discover app or BSP manifests")
        if profile_name in {"display-touch", "cody-companion"} and (
            bsp_dir not in resolved_component_dirs or app_dir in resolved_component_dirs
        ):
            raise AssertionError(f"{variant_name} must discover the BSP without the Luna app")

        lock = resolve_relative(variant.get("dependency_lock"), f"{variant_name}.lock")
        if not lock.is_file() or lock.stat().st_size == 0:
            raise AssertionError(f"missing canonical dependency lock: {lock}")
        lock_text = lock.read_text(encoding="utf-8")
        lock_dependencies = set(
            re.findall(r"(?m)^  ([^ \r\n][^:]*):\s*$", lock_text)
        )
        if profile_name == "headless":
            if lock_dependencies != {"idf"}:
                raise AssertionError("headless dependency lock must contain only ESP-IDF")
        elif profile_name in {"display-touch", "cody-companion"}:
            if not {"idf", "espressif/esp_lcd_co5300", "waveshare/esp_lcd_touch_cst9217"}.issubset(lock_dependencies):
                raise AssertionError(f"{variant_name} lock must contain display and touch drivers")
            if any("lvgl" in dependency or "brookesia" in dependency for dependency in lock_dependencies):
                raise AssertionError(f"{variant_name} lock must not contain the GUI stack")
            if profile_name == "cody-companion" and not {
                "espressif/network_provisioning", "espressif/esp_codec_dev",
                "waveshare/qmi8658", "cube32esp/xpowerslib",
            }.issubset(lock_dependencies):
                raise AssertionError("cody-companion lock is missing selected connected hardware")
        else:
            required_dependencies = {
                "espressif/esp_board_manager",
                "joltwallet/littlefs",
            }
            if not required_dependencies.issubset(lock_dependencies):
                raise AssertionError(f"{variant_name} lock is missing its UI closure")
            connected_dependency = "espressif/network_provisioning"
            full_dependencies = {"waveshare/qmi8658", "cube32esp/xpowerslib"}
            if profile_name == "ui-minimal" and (
                connected_dependency in lock_dependencies
                or full_dependencies.intersection(lock_dependencies)
            ):
                raise AssertionError("ui-minimal lock contains connected/full hardware")
            if profile_name == "connected":
                if connected_dependency not in lock_dependencies:
                    raise AssertionError("connected lock is missing provisioning")
                if full_dependencies.intersection(lock_dependencies):
                    raise AssertionError("connected lock contains full hardware")
            if profile_name == "full" and not (
                {connected_dependency} | full_dependencies
            ).issubset(lock_dependencies):
                raise AssertionError(f"{variant_name} lock is missing full hardware")
        if variant.get("feature") == "wake-word":
            if "espressif__esp-sr" not in roots or "espressif/esp-sr" not in lock_dependencies:
                raise AssertionError(f"{variant_name} must resolve ESP-SR in its isolated dependency closure")
            if "config/sdkconfig/v1/features/wake-word.defaults" not in defaults:
                raise AssertionError(f"{variant_name} must use the shared wake-word defaults")
        elif "espressif__esp-sr" in roots:
            raise AssertionError(f"{variant_name} must keep opt-in ESP-SR out of its root list")
        sdkconfig = resolve_relative(
            variant.get("generated_sdkconfig"), f"{variant_name}.sdkconfig"
        )
        build_dir = variant.get("default_build_dir")
        if not isinstance(build_dir, str) or not (
            build_dir == "artifacts"
            or build_dir.startswith("artifacts/")
            or build_dir.startswith("artifacts\\")
        ):
            raise AssertionError(
                f"variant {variant_name} build output must remain under artifacts/"
            )
        if lock in locks or sdkconfig in sdkconfigs or build_dir in build_dirs:
            raise AssertionError(f"variant {variant_name} reuses mutable build state")
        locks.add(lock)
        sdkconfigs.add(sdkconfig)
        build_dirs.add(build_dir)

    for removed in ("sdkconfig.defaults", "sdkconfig.distance.defaults"):
        if (ROOT / removed).exists():
            raise AssertionError(f"obsolete monolithic defaults remain: {removed}")

    standalone_defaults = read("config/sdkconfig/v1/profiles/display-touch.defaults")
    for flag in (
        "CONFIG_LUNASHELL_BUILD_PROFILE_DISPLAY_TOUCH=y",
        "CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY=y",
        "CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH=y",
        "CONFIG_LUNASHELL_BUILD_ENABLE_UI=n",
    ):
        require(standalone_defaults, flag, "standalone mandatory display/touch contract")

    cody_defaults = read("config/sdkconfig/v1/profiles/cody-companion.defaults")
    for flag in (
        "CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION=y",
        "CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY=y",
        "CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH=y",
        "CONFIG_LUNASHELL_BUILD_ENABLE_WIFI=y",
        "CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH=y",
        "CONFIG_LUNASHELL_BUILD_ENABLE_UI=n",
        "CONFIG_LUNASHELL_BUILD_ENABLE_GNSS=n",
    ):
        require(cody_defaults, flag, "Cody mandatory hardware and GUI exclusion contract")

    selectors = {
        "full": "CONFIG_LUNASHELL_BUILD_PROFILE_FULL=y",
        "connected": "CONFIG_LUNASHELL_BUILD_PROFILE_CONNECTED=y",
        "ui-minimal": "CONFIG_LUNASHELL_BUILD_PROFILE_UI_MINIMAL=y",
        "headless": "CONFIG_LUNASHELL_BUILD_PROFILE_HEADLESS=y",
    }
    for profile, selector in selectors.items():
        text = read(f"config/sdkconfig/v1/profiles/{profile}.defaults")
        if [line for line in text.splitlines() if line.startswith("CONFIG_")] != [selector]:
            raise AssertionError(f"profile {profile} must select exactly {selector}")

    base_defaults = read("config/sdkconfig/v1/base.defaults")
    require(
        base_defaults,
        "CONFIG_COMPILER_CXX_EXCEPTIONS=y",
        "shared settings allocation-error policy",
    )

    device = read("scripts/commands/firmware.ps1")
    for fragment in (
        "config/sdkconfig/v1/catalog.json",
        "$SelectedProfileVariant.defaults",
        "$SelectedProfileVariant.component_roots",
        "$SelectedProfileVariant.component_dirs",
        "$SelectedProfileVariant.dependency_lock",
        "$SelectedProfileVariant.generated_sdkconfig",
        "$SelectedProfileVariant.default_build_dir",
        "$env:LUNASHELL_PROFILE_COMPONENT_ROOTS",
        "$env:LUNASHELL_PROFILE_COMPONENT_DIRS",
        '"-DSDKCONFIG=$ProfileSdkconfigPath"',
        '"-DLUNASHELL_DEPENDENCIES_LOCK=',
        "Initialize-WorkingDependencyLock",
        "Invoke-ProfileConfiguration",
        "Invoke-ProfileConfiguration $BuildDir $true",
        '"-DIDF_TARGET=esp32s3"',
        "Assert-WorkingDependencyLock",
        "Publish-WorkingDependencyLock",
        '"schema=1"',
        '"profile=$Profile"',
        '"feature=$Feature"',
    ):
        require(device, fragment, "catalog-driven profile wrapper")
    if "ExtraDefaults" in device:
        raise AssertionError("free-form defaults can alias canonical profile state")

    kconfig = read("main/Kconfig.projbuild")
    for symbol in (
        "LUNASHELL_BUILD_PROFILE_SCHEMA_VERSION",
        "LUNASHELL_BUILD_BOARD_PROFILE_WAVESHARE_ESP32S3_TOUCH_AMOLED_1_75",
        "LUNASHELL_BUILD_PROFILE_FULL",
        "LUNASHELL_BUILD_PROFILE_CONNECTED",
        "LUNASHELL_BUILD_PROFILE_UI_MINIMAL",
        "LUNASHELL_BUILD_PROFILE_HEADLESS",
        "LUNASHELL_BUILD_PROFILE_DISPLAY_TOUCH",
        "LUNASHELL_BUILD_PROFILE_CODY_COMPANION",
        "LUNASHELL_BUILD_ENABLE_DISPLAY",
        "LUNASHELL_BUILD_ENABLE_TOUCH",
        "LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE",
        "LUNASHELL_BUILD_ENABLE_HTTP_API",
    ):
        require(kconfig, symbol, "Kconfig profile contract")

    root_cmake = read("CMakeLists.txt")
    for fragment in (
        "LUNASHELL_PROFILE_COMPONENT_ROOTS",
        "LUNASHELL_PROFILE_COMPONENT_DIRS",
        'set(COMPONENTS "$ENV{LUNASHELL_PROFILE_COMPONENT_ROOTS}")',
        '"$ENV{IDF_PATH}/components;$ENV{LUNASHELL_PROFILE_COMPONENT_DIRS}"',
        "LUNASHELL_DEPENDENCIES_LOCK",
        "idf_build_set_property(\n    DEPENDENCIES_LOCK",
    ):
        require(root_cmake, fragment, "pre-Kconfig profile roots and lock")

    main_manifest = read("main/idf_component.yml")
    for dependency in (
        "esp_board_manager",
        "brookesia_hal_adaptor",
        "brookesia_service_device",
        "brookesia_gui_lvgl",
        "brookesia_system_core",
        "brookesia_lib_utils",
        "littlefs",
    ):
        match = re.search(
            rf"(?ms)^  [^\n]*{re.escape(dependency)}:[^\n]*\n"
            rf"(?:(?:    .*\n)*)?    rules:\n"
            rf"      - if: \"\$CONFIG\{{LUNASHELL_BUILD_ENABLE_UI\}} == True\"",
            main_manifest,
        )
        if match is None:
            raise AssertionError(f"main manifest dependency is not UI-gated: {dependency}")

    platform_cmake = read("components/LUNASHELL_platform/CMakeLists.txt")
    for fragment in (
        "LUNASHELL_platform_uses_waveshare_bsp OFF",
        '"src/storage/LUNASHELL_storage_stub.cpp"',
        "CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE",
        "CONFIG_LUNASHELL_BUILD_ENABLE_HTTP_API",
    ):
        require(platform_cmake, fragment, "platform profile closure")
    platform_manifest = read("components/LUNASHELL_platform/idf_component.yml")
    require(
        platform_manifest,
        '$CONFIG{LUNASHELL_BUILD_ENABLE_PROVISIONING} == True',
        "provisioning dependency gate",
    )

    storage_stub = read("components/LUNASHELL_platform/src/storage/LUNASHELL_storage_stub.cpp")
    require(storage_stub, "reportStorage(report)", "disabled storage snapshot")
    if storage_stub.count("ESP_ERR_NOT_SUPPORTED") < 3:
        raise AssertionError("disabled storage operations must remain explicitly unsupported")

    power_stub = read("components/LUNASHELL_platform/src/power/LUNASHELL_power_stub.cpp")
    require(
        power_stub,
        "LUNASHELL_power_manager_note_ui_standby_result",
        "disabled power UI acknowledgement",
    )
    provisioning_stub = read(
        "components/LUNASHELL_platform/src/network/LUNASHELL_provisioning_stub.cpp"
    )
    require(
        provisioning_stub,
        "LUNASHELL_provisioning_failure_reason",
        "disabled provisioning failure state",
    )

    hardware = read("components/LUNASHELL_platform/src/snapshot/LUNASHELL_hardware.cpp")
    require(hardware, "Shared BSP I2C: not required by build profile", "headless I2C gate")
    require(
        hardware,
        "unavailable_publication_error",
        "disabled IMU publication scope",
    )

    time_source = read("components/LUNASHELL_platform/src/runtime/LUNASHELL_time.cpp")
    for fragment in (
        "#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI\n#include \"esp_netif.h\"",
        "#if !CONFIG_LUNASHELL_BUILD_ENABLE_WIFI",
        "return ESP_ERR_NOT_SUPPORTED;",
    ):
        require(time_source, fragment, "headless time-service network gate")

    print("Firmware build profile verification passed")


if __name__ == "__main__":
    main()
