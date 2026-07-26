"""Hardware-free checks for BLE provisioning and the current System Core contract."""

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]

EXPECTED_SCREENS = {
    "qsee_background_screen",
    "qsee_welcome",
    "qsee_logs",
    "qsee_hardware",
    "qsee_audio",
    "qsee_audio_detail",
    "qsee_audio_fft",
    "qsee_imu",
    "qsee_settings",
    "qsee_about",
    "qsee_pairing",
}
SETTINGS_ACTIONS = {
    "qsee.settings.diagnostic.faster",
    "qsee.settings.diagnostic.slower",
    "qsee.settings.diagnostic.reset",
    "qsee.settings.imu_smoothing.less",
    "qsee.settings.imu_smoothing.more",
    "qsee.settings.storage.mirror.toggle",
    "qsee.settings.radios.start",
    "qsee.settings.display.brightness_down",
    "qsee.settings.display.brightness_up",
    "qsee.settings.display.rotation.toggle",
}
OBSOLETE_SETTINGS_ACTIONS = {
    "qsee.settings.back",
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
    edge_navigation = read("components/qsee_app/qsee_edge_navigation.cpp")
    navigation_service = read(
        "components/qsee_app/qsee_navigation_service.cpp"
    )
    app_root = json.loads(read("components/qsee_app/package/res/root.json"))
    chrome = json.loads(
        read("components/qsee_app/package/res/templates/qsee_chrome.json")
    )
    status_bar_source = read("components/qsee_app/qsee_status_bar.cpp")
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
    require(app, "qsee.open.pairing", "pairing wizard entry")
    require(app, "qsee.next", "pairing wizard forward action")
    require(app, "qsee_provisioning_start()", "pairing wizard provisioning start")
    require(
        edge_navigation,
        '"/qsee_chrome_screen/chrome/back"',
        "edge navigation Back path",
    )
    require(
        edge_navigation,
        '"/qsee_chrome_screen/chrome/next"',
        "edge navigation Next path",
    )
    require(
        navigation_service,
        "NavigationService::commit_back",
        "navigation history Back",
    )
    require(
        navigation_service,
        "NavigationService::commit_forward",
        "navigation history Forward",
    )
    require(
        navigation_service,
        "NavigationService::replace",
        "navigation history route replacement",
    )
    require(
        navigation_service,
        "NavigationService::clear_history",
        "navigation history clear",
    )
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

    chrome_nodes = list(walk_json(chrome))
    chrome_template_ids = [
        node.get("templateId")
        for node in chrome_nodes
        if isinstance(node, dict) and node.get("type") == "templateRef"
    ]
    if chrome_template_ids.count("qsee_status_bar") != 1:
        raise AssertionError(
            "shared chrome must own exactly one qsee_status_bar"
        )
    if "templates/qsee_adaptive_command_bar.json" in app_root.get(
        "assets", []
    ):
        raise AssertionError(
            "action-bearing command bar must be instantiated directly in "
            "shared chrome, not registered as a top-level template"
        )

    root_command_refs = [
        node
        for node in nodes
        if isinstance(node, dict)
        and node.get("templateId") == "qsee_command_button"
    ]
    if root_command_refs:
        raise AssertionError(
            "route screens must not instantiate command buttons directly"
        )

    command_slots = [
        node
        for node in chrome_nodes
        if isinstance(node, dict)
        and node.get("templateId") == "qsee_command_button"
    ]
    if len(command_slots) != 5:
        raise AssertionError(
            f"adaptive command bar must own five reusable slots: {len(command_slots)}"
        )

    require(
        status_bar_source,
        "const DeviceSnapshot &snapshot",
        "persistent status bar",
    )
    forbid(status_bar_source, "qsee_platform.h", "persistent status bar")

    edge_buttons = {
        node.get("id")
        for node in chrome_nodes
        if isinstance(node, dict)
        and node.get("type") == "button"
        and node.get("id") in {"back", "next"}
    }
    if edge_buttons != {"back", "next"}:
        raise AssertionError(
            f"shared chrome must own one Back and one Next button: {edge_buttons}"
        )

    actions = {
        node["action"]
        for node in nodes + chrome_nodes
        if isinstance(node, dict) and isinstance(node.get("action"), str)
    }
    missing_actions = SETTINGS_ACTIONS - actions
    if missing_actions:
        raise AssertionError(
            f"System Core Settings actions missing: {sorted(missing_actions)}"
        )
    obsolete_actions = OBSOLETE_SETTINGS_ACTIONS & actions
    if obsolete_actions:
        raise AssertionError(
            "obsolete System Core Settings actions still exposed: "
            f"{sorted(obsolete_actions)}"
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
    ):
        if ROOT.joinpath(legacy_path).exists():
            raise AssertionError(f"legacy Phone navigation file still exists: {legacy_path}")

    print(
        "pairing static verification: PASS "
        f"(BLE Security 1 backend, {len(screen_ids)} System Core screens)"
    )


if __name__ == "__main__":
    main()
