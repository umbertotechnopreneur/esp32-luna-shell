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
    "screen_tof.json": "qsee_tof",
    "screen_tof_settings.json": "qsee_tof_settings",
    "screen_settings.json": "qsee_settings",
    "screen_storage.json": "qsee_storage",
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
    "qsee_tof",
    "qsee_tof_settings",
    "qsee_settings",
    "qsee_storage",
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
    "qsee.open.storage",
    "qsee.storage.format",
}
TOF_ACTIONS = {
    "qsee.hardware.detail.12",
    "qsee.chrome.activity.tof_settings",
    "qsee.tof.settings.samples.changed",
    "qsee.tof.settings.interval.changed",
    "qsee.tof.settings.reference.preview",
    "qsee.tof.settings.reference.commit",
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
    "qsee.settings.chrome.timeout.next",
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
    simulator = read("simulator/windows/qsee_simulator.py")
    simulator_runner = read("simulator/windows/run-simulator.ps1")
    edge_navigation = read("components/qsee_app/qsee_edge_navigation.cpp")
    simulator = read("simulator/windows/qsee_simulator.py")
    simulator_runner = read("simulator/windows/run-simulator.ps1")
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
    require(
        provisioning,
        '"Network: %.*s\\nIP: "',
        "English provisioning connection summary",
    )
    forbid(
        provisioning,
        '"Network: %.*s\\\\nIP: "',
        "provisioning connection summary",
    )
    require(
        provisioning,
        "static_cast<int>(sizeof(config.sta.ssid))",
        "bounded provisioning SSID rendering",
    )
    forbid(provisioning, '"Rete:', "provisioning connection summary")
    forbid(provisioning, "network_prov_scheme_softap", "provisioning implementation")
    forbid(provisioning, "NETWORK_PROV_SECURITY_0", "provisioning implementation")
    require(
        provisioning_header,
        "bool isRunning() const;",
        "provisioning running-state contract",
    )
    active_start = provisioning.index(
        'extern "C" bool qsee_provisioning_active(void)'
    )
    active_end = provisioning.index(
        'extern "C" qsee_provisioning_state_t qsee_provisioning_state(void)',
        active_start,
    )
    active_source = provisioning[active_start:active_end]
    require(
        active_source,
        "ProvisioningService::instance().isRunning()",
        "provisioning active-session contract",
    )
    forbid(
        active_source,
        "ProvisioningState::",
        "provisioning active-session contract",
    )
    credential_failure_start = provisioning.index(
        "case NETWORK_PROV_WIFI_CRED_FAIL:"
    )
    credential_failure_end = provisioning.index(
        "case NETWORK_PROV_END:",
        credential_failure_start,
    )
    credential_failure_source = provisioning[
        credential_failure_start:credential_failure_end
    ]
    require(
        credential_failure_source,
        "_state = ProvisioningState::Failed;",
        "credential-failure lifecycle",
    )
    forbid(
        credential_failure_source,
        "_running = false;",
        "credential-failure lifecycle",
    )
    require(
        provisioning_header,
        "enum class ProvisioningFailureReason",
        "typed provisioning failure reason",
    )
    require(
        platform_header,
        "qsee_provisioning_failure_reason_t qsee_provisioning_failure_reason(void);",
        "public provisioning failure reason",
    )
    require(
        credential_failure_source,
        "if (event_data == nullptr)",
        "credential-failure reason validation",
    )
    require(
        credential_failure_source,
        "NETWORK_PROV_WIFI_STA_AUTH_ERROR",
        "credential authentication failure",
    )
    require(
        credential_failure_source,
        "NETWORK_PROV_WIFI_STA_AP_NOT_FOUND",
        "credential access-point failure",
    )
    forbid(
        provisioning,
        "(void)event_data;",
        "provisioning event failure details",
    )

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
        require(app, f'step = "STEP {step} OF 5";', "pairing wizard stage labels")
        require(
            app,
            f"progress_value = {step};",
            "pairing wizard progress values",
        )
    for state in (
        "IDLE",
        "STARTING",
        "WAITING_FOR_APP",
        "CREDENTIALS_RECEIVED",
        "CONNECTING",
        "SUCCESS",
        "FAILED",
        "STOPPING",
        "DEINITIALIZING",
        "STOPPED",
        "POISONED",
        "UNAVAILABLE",
    ):
        require(
            app,
            f"case QSEE_PROVISIONING_STATE_{state}:",
            "pairing provisioning state coverage",
        )
    require(app, "case Screen::PairResult:", "pairing result state")
    require(
        app,
        'message = "Closing Bluetooth setup...";',
        "pairing closing state",
    )
    require(
        app,
        '"Bluetooth setup ended before Wi-Fi connected."',
        "pairing stopped state",
    )
    result_start = app.index("App::show_pairing_result")
    result_end = app.index("App::pairing_next", result_start)
    result_source = app[result_start:result_end]
    active_check = result_source.find("qsee_provisioning_active()")
    stop_call = result_source.find("qsee_provisioning_stop()")
    result_transition = result_source.find("Screen::PairResult")
    if not (
        0 <= active_check < stop_call < result_transition
    ):
        raise AssertionError(
            "pairing result must stop an active provisioning session before "
            "entering PairResult"
        )
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
    missing_tof_actions = TOF_ACTIONS - actions
    if missing_tof_actions:
        raise AssertionError(
            f"System Core TOF actions missing: {sorted(missing_tof_actions)}"
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

    storage_nodes = runtime_nodes["screen_storage.json"]
    format_buttons = [
        node
        for node in storage_nodes
        if isinstance(node, dict)
        and node.get("type") == "button"
        and node.get("id") == "format_action"
    ]
    if len(format_buttons) != 1:
        raise AssertionError("Storage must expose exactly one inline format button")
    format_button = format_buttons[0]
    if format_button.get("commonProps", {}).get("disabled") is not True:
        raise AssertionError("Storage format button must default to disabled")
    if (
        format_button.get("bindings", {}).get("commonProps.disabled")
        != "format_disabled"
    ):
        raise AssertionError(
            "Storage format button disabled state must be runtime-bound"
        )

    require(app, "qsee.tof.calibration.start", "TOF calibration command")
    require(app, '"res/screen_tof.json"', "lazy TOF screen descriptor")
    require(
        app,
        '"res/screen_tof_settings.json"',
        "lazy TOF settings screen descriptor",
    )
    require(app, '"qsee.open.tof"', "TOF navigation action")
    require(app, '"qsee.open.tof_settings"', "TOF settings navigation action")
    require(app, "kTofCommands", "TOF settings gear")
    require(app, "kTofSettingsCommands", "TOF calibration command")
    forbid(app, "kImuCommands", "IMU command behavior")
    require(
        app_header,
        "#if CONFIG_QSEE_BUILD_ENABLE_DISTANCE && CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART",
        "TOF app declarations combined build guard",
    )
    require(simulator, "def show_tof_demo", "Windows TOF simulator")
    require(
        simulator,
        "def show_tof_settings_demo",
        "Windows TOF settings simulator",
    )
    require(
        simulator,
        '"tof": simulator.show_tof_demo',
        "Windows TOF capture route",
    )
    require(
        simulator,
        '"tof-settings": simulator.show_tof_settings_demo',
        "Windows TOF settings capture route",
    )
    require(
        simulator_runner,
        '"tof"',
        "Windows simulator wrapper TOF route",
    )
    require(
        simulator_runner,
        '"tof-settings"',
        "Windows simulator wrapper TOF settings route",
    )
    require(
        simulator,
        "SYNTHETIC VERTICAL DISTANCE",
        "Windows TOF synthetic-data label",
    )
    require(
        simulator,
        '(94, "SAMPLES", "500")',
        "Windows TOF default precision sample count",
    )
    require(
        simulator,
        '(240, "INTERVAL", "250 ms")',
        "Windows TOF default precision sample interval",
    )

    pairing_template = package_documents["templates/qsee_pairing_stage.json"]
    pairing_template_nodes = list(walk_json(pairing_template))
    pairing_by_id = {
        node["id"]: node
        for node in pairing_template_nodes
        if isinstance(node, dict) and isinstance(node.get("id"), str)
    }
    pairing_layout = pairing_template.get("node", {}).get("layout", {})
    if pairing_layout.get("mainAlign") != "center":
        raise AssertionError("Pairing content must be centered in its viewport")
    if pairing_layout.get("gap") != "4dp":
        raise AssertionError("Pairing content must retain its bounded 4dp rhythm")
    for label_id in ("step", "title", "message", "detail"):
        label = pairing_by_id.get(label_id)
        if label is None or label.get("type") != "label":
            raise AssertionError(f"Pairing label {label_id!r} is missing")
        if label.get("placement", {}).get("height") != "wrap":
            raise AssertionError(
                f"Pairing label {label_id!r} must wrap to its measured text height"
            )
    if (
        pairing_by_id["message"].get("style", {}).get("fontSize")
        != "${constant.typography.detail}"
    ):
        raise AssertionError("Pairing primary message must use detail typography")
    if pairing_by_id["step"].get("bindings", {}).get("style.textColor") != "color":
        raise AssertionError("Pairing status accent must be runtime-bound")
    progress = pairing_by_id.get("progress")
    if progress is None or progress.get("type") != "progressBar":
        raise AssertionError("Pairing must expose one bounded progress bar")
    if progress.get("bindings", {}).get("rangeProps.value") != "value":
        raise AssertionError("Pairing progress value must be runtime-bound")
    if progress.get("bindings", {}).get("commonProps.hidden") != "hidden":
        raise AssertionError("Pairing progress visibility must be runtime-bound")
    qr = pairing_by_id.get("qr")
    if qr is None or qr.get("type") != "image":
        raise AssertionError("Pairing must expose one QR code")

    def dp(value: Any) -> int:
        if not isinstance(value, str) or not value.endswith("dp"):
            raise AssertionError(f"Expected a dp value, got {value!r}")
        return int(value.removesuffix("dp"))

    pairing_root = pairing_template["node"]
    pairing_viewport_height = (
        dp(pairing_root["placement"]["height"])
        - dp(pairing_root["style"]["paddingTop"])
        - dp(pairing_root["style"]["paddingBottom"])
    )
    conservative_qr_stack_height = (
        12  # 10sp micro line
        + dp(progress["placement"]["height"])
        + 29  # 24sp heading line
        + (2 * 19)  # two wrapped 16sp detail lines
        + dp(qr["placement"]["height"])
        + 14  # one 12sp caption line
        + (5 * dp(pairing_layout["gap"]))
    )
    if conservative_qr_stack_height > pairing_viewport_height:
        raise AssertionError(
            "Pairing QR layout exceeds its usable viewport: "
            f"{conservative_qr_stack_height}dp > {pairing_viewport_height}dp"
        )
    require(app, '.key = "color"', "pairing status accent update")
    require(app, ".value = accent_color", "pairing status accent update")
    require(app, '.key = "value"', "pairing progress update")
    require(app, ".value = std::to_string(progress_value)", "pairing progress update")
    require(app, '.key = "hidden"', "pairing progress visibility update")
    require(
        app,
        '.value = progress_hidden ? "true" : "false"',
        "pairing progress visibility update",
    )

    simulator_pairing_screens = (
        "pairing-intro",
        "pairing-bluetooth",
        "pairing-app",
        "pairing-app-qr",
        "pairing-device-qr",
        "pairing-credentials",
        "pairing-connecting",
        "pairing-success",
        "pairing-failed",
    )
    require(
        simulator,
        "*sorted(PAIRING_SCREENS),",
        "pairing simulator command-line choices",
    )
    require(
        simulator,
        "if content_height > viewport_height:",
        "pairing simulator viewport guard",
    )
    require(
        simulator,
        '"pairing-failed": "pairing-device-qr",',
        "pairing simulator failed retry",
    )
    require(
        simulator,
        '"pairing-failed": "pairing-app-qr",',
        "pairing simulator result Back target",
    )
    require(
        simulator,
        'if self.screen == "pairing-success":',
        "pairing simulator connected status",
    )
    require(
        simulator,
        "EDGE_COMMAND_DIAMETER = 60",
        "pairing simulator shared chrome geometry",
    )
    for screen in simulator_pairing_screens:
        require(simulator, f'"{screen}"', "pairing simulator screen inventory")
        require(
            simulator_runner,
            f'"{screen}"',
            "pairing simulator runner inventory",
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
