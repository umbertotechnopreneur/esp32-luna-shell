"""Hardware-free checks for BLE provisioning and the current System Core UI."""

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]

EXPECTED_SCREENS = {
    "qsee_welcome",
    "qsee_logs",
    "qsee_hardware",
    "qsee_audio",
    "qsee_imu",
    "qsee_settings",
    "qsee_about",
}
SETTINGS_ACTIONS = {
    "qsee.open.settings",
    "qsee.settings.back",
    "qsee.settings.diagnostic.faster",
    "qsee.settings.diagnostic.slower",
    "qsee.settings.diagnostic.reset",
    "qsee.settings.imu_smoothing.less",
    "qsee.settings.imu_smoothing.more",
    "qsee.settings.storage.mirror.toggle",
    "qsee.settings.radios.start",
    "qsee.settings.display.brightness_down",
    "qsee.settings.display.brightness_up",
}


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, fragment: str, source: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{fragment!r} missing from {source}")


def forbid(text: str, fragment: str, source: str) -> None:
    if fragment in text:
        raise AssertionError(f"{fragment!r} must not appear in {source}")


def walk_json(value: Any):
    yield value
    if isinstance(value, dict):
        for child in value.values():
            yield from walk_json(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk_json(child)


def main() -> None:
    provisioning = read("components/qsee_platform/qsee_provisioning.cpp")
    provisioning_header = read("components/qsee_platform/include/qsee_provisioning.hpp")
    radios = read("components/qsee_platform/qsee_radios.cpp")
    platform_header = read("components/qsee_platform/include/qsee_platform.h")
    platform_cmake = read("components/qsee_platform/CMakeLists.txt")
    app_header = read("components/qsee_app/include/qsee_app.hpp")
    app = read("components/qsee_app/qsee_app.cpp")
    app_root = json.loads(read("components/qsee_app/package/res/root.json"))
    main_source = read("main/main.cpp")
    sdkconfig = read("sdkconfig.defaults")

    require(provisioning_header, "class ProvisioningService final", "provisioning header")
    require(provisioning, "network_prov_scheme_ble", "provisioning implementation")
    require(provisioning, "NETWORK_PROV_SECURITY_1", "provisioning implementation")
    require(provisioning, '\\"pop\\":\\"\\"', "provisioning QR payload")
    require(provisioning, '\\"transport\\":\\"ble\\"', "provisioning QR payload")
    require(provisioning, '\\"security\\":1', "provisioning QR payload")
    forbid(provisioning, "network_prov_scheme_softap", "provisioning implementation")
    forbid(provisioning, "NETWORK_PROV_SECURITY_0", "provisioning implementation")

    init_position = radios.find("qsee_provisioning_init()")
    start_position = radios.find("qsee_provisioning_start()")
    if init_position < 0 or start_position < 0 or init_position > start_position:
        raise AssertionError(
            "qsee_radios_init must initialize BLE provisioning before starting it"
        )
    require(radios, "qsee_wifi_init()", "radio startup")
    require(platform_header, "esp_err_t qsee_radios_init(void);", "platform API")
    require(platform_cmake, '"qsee_provisioning.cpp"', "platform CMake")
    require(sdkconfig, "CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI=y", "sdkconfig defaults")
    require(
        sdkconfig,
        "CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1=y",
        "sdkconfig defaults",
    )

    # Radio allocation is intentionally deferred until the user requests it
    # from Settings; boot must remain usable without Wi-Fi or BLE.
    forbid(main_source, "qsee_radios_init()", "ordered startup")
    require(app_header, "system::core::IApp", "System Core app header")
    require(app, "qsee.settings.radios.start", "System Core Settings actions")
    require(app, "qsee_radios_init()", "System Core Settings radio action")
    require(
        app,
        "BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL",
        "System Core provider",
    )

    nodes = list(walk_json(app_root))
    screen_ids = {
        node.get("id")
        for node in nodes
        if isinstance(node, dict) and node.get("type") == "viewScreen"
    }
    if screen_ids != EXPECTED_SCREENS:
        raise AssertionError(
            "unexpected System Core screen inventory: "
            f"expected {sorted(EXPECTED_SCREENS)}, got {sorted(screen_ids)}"
        )

    flow_ids = {
        node.get("id")
        for node in nodes
        if isinstance(node, dict) and node.get("type") == "screenFlow"
    }
    if "qsee_content" not in flow_ids:
        raise AssertionError("qsee_content screenFlow missing from System Core UI")

    actions = {
        node["action"]
        for node in nodes
        if isinstance(node, dict) and isinstance(node.get("action"), str)
    }
    missing_actions = SETTINGS_ACTIONS - actions
    if missing_actions:
        raise AssertionError(
            f"System Core Settings actions missing: {sorted(missing_actions)}"
        )
    unhandled_actions = {action for action in actions if action not in app}
    if unhandled_actions:
        raise AssertionError(
            f"System Core UI actions not subscribed by the native app: "
            f"{sorted(unhandled_actions)}"
        )

    for legacy_path in (
        "components/qsee_app/qsee_navigation.cpp",
        "components/qsee_app/include/qsee_navigation.hpp",
        "components/qsee_app/qsee_edge_navigation.cpp",
        "components/qsee_app/include/qsee_edge_navigation.hpp",
        "components/qsee_app/qsee_status_bar.cpp",
        "components/qsee_app/include/qsee_status_bar.hpp",
    ):
        if ROOT.joinpath(legacy_path).exists():
            raise AssertionError(f"legacy Phone navigation file still exists: {legacy_path}")

    print(
        "pairing static verification: PASS "
        f"(BLE Security 1 backend, {len(screen_ids)} System Core screens)"
    )


if __name__ == "__main__":
    main()
