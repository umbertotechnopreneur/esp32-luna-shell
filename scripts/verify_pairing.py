"""Hardware-free checks for BLE provisioning and the current System Core contract."""

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
GUI_RES = ROOT / "components/qsee_app/package/res"

EXPECTED_RESIDENT_SCREENS = {
    "qsee_background_screen",
    "qsee_welcome",
    "qsee_content_idle",
}
EXPECTED_LAZY_SCREEN_DOCUMENTS = {
    "screen_logs.json": "qsee_logs",
    "screen_hardware.json": "qsee_hardware",
    "screen_hardware_detail.json": "qsee_hardware_detail",
    "screen_audio.json": "qsee_audio",
    "screen_audio_fft.json": "qsee_audio_fft",
    "screen_audio_detail.json": "qsee_audio_detail",
    "screen_imu.json": "qsee_imu",
    "screen_settings.json": "qsee_settings",
    "screen_about.json": "qsee_about",
    "screen_pairing.json": "qsee_pairing",
}
EXPECTED_ROUTE_SCREENS = {
    "qsee_background_screen",
    "qsee_welcome",
    "qsee_logs",
    "qsee_hardware",
    "qsee_hardware_detail",
    "qsee_audio",
    "qsee_audio_detail",
    "qsee_audio_fft",
    "qsee_imu",
    "qsee_settings",
    "qsee_about",
    "qsee_pairing",
}
EXPECTED_BOOTSTRAP_FLOWS = {
    "qsee_background_flow",
    "qsee_chrome_flow",
    "qsee_content",
}
PAIRING_WIZARD_ROUTES = (
    "PairIntro",
    "PairBluetooth",
    "PairApp",
    "AppStoreQr",
    "PairQr",
    "PairResult",
)
SETTINGS_ACTIONS = {
    "qsee.settings.diagnostic.changed",
    "qsee.settings.imu_smoothing.changed",
    "qsee.settings.storage.mirror.changed",
    "qsee.settings.performance.changed",
    "qsee.settings.radios.start",
    "qsee.settings.display.brightness.changed",
    "qsee.settings.display.rotation.changed",
}
OBSOLETE_SETTINGS_ACTIONS = {
    "qsee.settings.back",
    "qsee.settings.debug.maximum",
    "qsee.settings.diagnostic.faster",
    "qsee.settings.diagnostic.slower",
    "qsee.settings.diagnostic.reset",
    "qsee.settings.imu_smoothing.less",
    "qsee.settings.imu_smoothing.more",
    "qsee.settings.storage.mirror.toggle",
    "qsee.settings.display.brightness_down",
    "qsee.settings.display.brightness_up",
    "qsee.settings.display.rotation.toggle",
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


def load_package_json() -> dict[str, Any]:
    documents: dict[str, Any] = {}
    for path in sorted(GUI_RES.rglob("*.json")):
        relative = path.relative_to(GUI_RES).as_posix()
        try:
            documents[relative] = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise AssertionError(
                f"invalid GUI JSON {relative}:{error.lineno}:{error.colno}: "
                f"{error.msg}"
            ) from error
    if not documents:
        raise AssertionError("QSee GUI package contains no JSON documents")
    return documents


def document_nodes(document: Any, source: str) -> list[Any]:
    if not isinstance(document, dict) or not isinstance(
        document.get("assets"), list
    ):
        raise AssertionError(f"{source} is not a GUI runtime document")
    return list(walk_json(document))


def verify_asset_references(
    runtime_documents: dict[str, Any],
    package_documents: dict[str, Any],
) -> None:
    for source, document in runtime_documents.items():
        for asset in document["assets"]:
            if not isinstance(asset, str):
                continue
            if asset not in package_documents:
                raise AssertionError(
                    f"{source} references missing GUI asset {asset}"
                )


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
    navigation_header = read(
        "components/qsee_app/include/qsee_navigation_service.hpp"
    )
    package_documents = load_package_json()
    app_root = package_documents.get("root.json")
    if app_root is None:
        raise AssertionError("resident GUI bootstrap root.json is missing")
    lazy_document_names = {
        path
        for path in package_documents
        if path.startswith("screen_") and "/" not in path
    }
    expected_lazy_document_names = set(EXPECTED_LAZY_SCREEN_DOCUMENTS)
    if lazy_document_names != expected_lazy_document_names:
        raise AssertionError(
            "unexpected lazy GUI document inventory: "
            f"expected {sorted(expected_lazy_document_names)}, "
            f"got {sorted(lazy_document_names)}"
        )
    runtime_documents = {
        "root.json": app_root,
        **{
            path: package_documents[path]
            for path in sorted(expected_lazy_document_names)
        },
    }
    runtime_nodes = {
        path: document_nodes(document, path)
        for path, document in runtime_documents.items()
    }
    verify_asset_references(runtime_documents, package_documents)
    chrome = package_documents["templates/qsee_chrome.json"]
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
    descriptor_start = app.index("App::lazy_screen_descriptor")
    descriptor_end = app.index("bool App::shares_lazy_document", descriptor_start)
    descriptor_source = app[descriptor_start:descriptor_end]
    for route in PAIRING_WIZARD_ROUTES:
        require(
            navigation_header,
            f"    {route},",
            "pairing wizard route inventory",
        )
        require(
            descriptor_source,
            f"case Screen::{route}:",
            "pairing lazy-document route grouping",
        )
    if descriptor_source.count('"res/screen_pairing.json"') != 1:
        raise AssertionError(
            "all pairing wizard routes must share one screen_pairing.json descriptor"
        )
    require(
        app,
        "shares_lazy_document(screen_, screen)",
        "pairing in-document navigation",
    )
    for step in range(1, 6):
        require(app, f'step = "{step} OF 5";', "pairing wizard stage labels")
    require(app, "case Screen::PairResult:", "pairing result state")
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

    root_nodes = runtime_nodes["root.json"]
    resident_screen_ids = {
        node.get("id")
        for node in root_nodes
        if isinstance(node, dict) and node.get("type") == "viewScreen"
    }
    if resident_screen_ids != EXPECTED_RESIDENT_SCREENS:
        raise AssertionError(
            "unexpected resident GUI screen inventory: "
            f"expected {sorted(EXPECTED_RESIDENT_SCREENS)}, "
            f"got {sorted(resident_screen_ids)}"
        )

    screen_owners: dict[str, str] = {}
    for source, nodes in runtime_nodes.items():
        for node in nodes:
            if not isinstance(node, dict) or node.get("type") != "viewScreen":
                continue
            screen_id = node.get("id")
            if not isinstance(screen_id, str):
                raise AssertionError(f"{source} contains a viewScreen without an id")
            previous_owner = screen_owners.get(screen_id)
            if previous_owner is not None:
                raise AssertionError(
                    f"duplicate GUI screen {screen_id}: "
                    f"{previous_owner} and {source}"
                )
            screen_owners[screen_id] = source

    for document_name, screen_id in EXPECTED_LAZY_SCREEN_DOCUMENTS.items():
        document_screen_ids = {
            node.get("id")
            for node in runtime_nodes[document_name]
            if isinstance(node, dict) and node.get("type") == "viewScreen"
        }
        if document_screen_ids != {screen_id}:
            raise AssertionError(
                f"{document_name} must own only {screen_id}: "
                f"{sorted(document_screen_ids)}"
            )
        require(
            app,
            f'"res/{document_name}"',
            f"lazy route registration for {screen_id}",
        )
        require(
            app,
            f'"/{screen_id}"',
            f"lazy route screen path for {screen_id}",
        )

    route_screen_ids = set(screen_owners) - {"qsee_content_idle"}
    if route_screen_ids != EXPECTED_ROUTE_SCREENS:
        raise AssertionError(
            "unexpected System Core route inventory: "
            f"expected {sorted(EXPECTED_ROUTE_SCREENS)}, "
            f"got {sorted(route_screen_ids)}"
        )

    flow_ids = {
        node.get("id")
        for node in root_nodes
        if isinstance(node, dict) and node.get("type") == "screenFlow"
    }
    if flow_ids != EXPECTED_BOOTSTRAP_FLOWS:
        raise AssertionError(
            "unexpected resident GUI flow inventory: "
            f"expected {sorted(EXPECTED_BOOTSTRAP_FLOWS)}, "
            f"got {sorted(flow_ids)}"
        )
    content_flow = next(
        node
        for node in root_nodes
        if isinstance(node, dict)
        and node.get("type") == "screenFlow"
        and node.get("id") == "qsee_content"
    )
    if content_flow.get("screens") != ["qsee_welcome", "qsee_content_idle"]:
        raise AssertionError(
            "qsee_content must retain only resident Welcome and idle screens"
        )
    transition_targets = {
        transition.get("action"): transition.get("to")
        for transition in content_flow.get("transitions", [])
        if isinstance(transition, dict)
    }
    expected_transition_targets = {
        "qsee.open.welcome": "qsee_welcome",
        "qsee.content.release": "qsee_content_idle",
    }
    if transition_targets != expected_transition_targets:
        raise AssertionError(
            "unexpected resident content transitions: "
            f"{transition_targets}"
        )

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
    for source, document in runtime_documents.items():
        if "templates/qsee_adaptive_command_bar.json" in document.get(
            "assets", []
        ):
            raise AssertionError(
                "action-bearing command bar must be instantiated directly in "
                f"shared chrome, not registered by {source}"
            )

    root_command_refs = [
        node
        for nodes in runtime_nodes.values()
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
        for nodes in [*runtime_nodes.values(), chrome_nodes]
        for node in nodes
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

    require(app_header, "struct LazyScreenDescriptor", "lazy GUI descriptor")
    require(
        app_header,
        "active_screen_document_id_",
        "single active lazy-document cache",
    )
    require(app, "context.gui().load_file(", "lazy GUI document load")
    require(app, "context.gui().mount_screen(", "lazy GUI screen mount")
    require(app, "unload_active_screen_document(", "lazy GUI document release")
    require(
        app,
        "loaded_page_documents(%u)",
        "lazy GUI measured cache telemetry",
    )

    for legacy_path in (
        "components/qsee_app/qsee_navigation.cpp",
        "components/qsee_app/include/qsee_navigation.hpp",
    ):
        if ROOT.joinpath(legacy_path).exists():
            raise AssertionError(f"legacy Phone navigation file still exists: {legacy_path}")

    print(
        "pairing static verification: PASS "
        f"(BLE Security 1 backend, {len(route_screen_ids)} routes, "
        f"{len(EXPECTED_LAZY_SCREEN_DOCUMENTS)} lazy documents, "
        "5 provisioning stages + result in one shared document)"
    )


if __name__ == "__main__":
    main()
