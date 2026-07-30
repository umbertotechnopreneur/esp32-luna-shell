#!/usr/bin/env python3
"""Static contract checks for the Battery UI and its navigation routes."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CANONICAL_GUI = ROOT / "components/qsee_app/gui/root.json"
GENERATED_BATTERY = (
    ROOT / "components/qsee_app/package/res/screen_battery.json"
)
POWER_METRIC_TEMPLATE = (
    ROOT / "components/qsee_app/package/res/templates/qsee_power_metric.json"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_contains(text: str, fragment: str, source: str) -> None:
    require(fragment in text, f"{fragment!r} missing from {source}")


def forbid_contains(text: str, fragment: str, source: str) -> None:
    require(fragment not in text, f"{fragment!r} must not appear in {source}")


def require_regex(text: str, pattern: str, source: str) -> re.Match[str]:
    match = re.search(pattern, text, flags=re.DOTALL)
    require(match is not None, f"/{pattern}/ missing from {source}")
    return match


def read(relative: str) -> str:
    return ROOT.joinpath(relative).read_text(encoding="utf-8")


def load_json(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        relative = path.relative_to(ROOT)
        raise AssertionError(
            f"invalid JSON {relative}:{error.lineno}:{error.colno}: "
            f"{error.msg}"
        ) from error
    require(isinstance(document, dict), f"{path.relative_to(ROOT)} is not an object")
    return document


def walk_dicts(value: Any):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from walk_dicts(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk_dicts(child)


def find_asset(document: dict[str, Any], asset_id: str, source: str) -> dict:
    matches = [
        asset
        for asset in document["assets"]
        if isinstance(asset, dict) and asset.get("id") == asset_id
    ]
    require(bool(matches), f"{asset_id!r} missing from {source}")
    require(len(matches) == 1, f"{asset_id!r} is duplicated in {source}")
    return matches[0]


def find_node(root: dict[str, Any], node_id: str, source: str) -> dict:
    matches = [
        node for node in walk_dicts(root) if node.get("id") == node_id
    ]
    require(bool(matches), f"{node_id!r} missing from {source}")
    require(len(matches) == 1, f"{node_id!r} is duplicated in {source}")
    return matches[0]


def require_binding(
    node: dict[str, Any],
    property_name: str,
    binding_name: str,
    source: str,
) -> None:
    actual = node.get("bindings", {}).get(property_name)
    require(
        actual == binding_name,
        f"{source} binding {property_name!r}: "
        f"{actual!r} != {binding_name!r}",
    )


def function_text(text: str, signature: str, source: str) -> str:
    signature_at = text.find(signature)
    require(signature_at >= 0, f"{signature!r} missing from {source}")
    body_at = text.find("{", signature_at + len(signature))
    require(body_at >= 0, f"body for {signature!r} missing from {source}")

    depth = 0
    for index in range(body_at, len(text)):
        token = text[index]
        if token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
            if depth == 0:
                return text[signature_at : index + 1]
    raise AssertionError(f"unterminated body for {signature!r} in {source}")


def initializer_text(text: str, name: str, source: str) -> str:
    match = re.search(
        rf"\b{re.escape(name)}\b\s*=\s*\{{\{{(.*?)\}}\}};",
        text,
        flags=re.DOTALL,
    )
    require(match is not None, f"initializer {name!r} missing from {source}")
    return match.group(1)


def python_function_text(text: str, signature: str, source: str) -> str:
    lines = text.splitlines(keepends=True)
    start = next(
        (index for index, line in enumerate(lines) if signature in line),
        None,
    )
    require(start is not None, f"{signature!r} missing from {source}")
    indentation = len(lines[start]) - len(lines[start].lstrip())
    end = len(lines)
    for index in range(start + 1, len(lines)):
        stripped = lines[index].strip()
        if not stripped:
            continue
        current_indentation = len(lines[index]) - len(lines[index].lstrip())
        if current_indentation <= indentation:
            end = index
            break
    return "".join(lines[start:end])


def verify_documents() -> None:
    canonical = load_json(CANONICAL_GUI)
    generated = load_json(GENERATED_BATTERY)
    metric_document = load_json(POWER_METRIC_TEMPLATE)
    require(
        isinstance(canonical.get("assets"), list),
        "canonical GUI has no assets array",
    )
    require(
        isinstance(generated.get("assets"), list),
        "generated Battery document has no assets array",
    )

    canonical_screen = find_asset(
        canonical,
        "qsee_battery",
        "canonical GUI",
    )
    generated_screen = find_asset(
        generated,
        "qsee_battery",
        "generated Battery document",
    )
    require(
        canonical_screen == generated_screen,
        "generated qsee_battery screen has drifted from the canonical GUI",
    )
    require(
        canonical_screen.get("mountMode") == "dynamic",
        "qsee_battery must remain a dynamic lazy screen",
    )
    require(
        "templates/qsee_power_metric.json" in generated["assets"],
        "generated Battery document does not import qsee_power_metric",
    )

    content_flow = find_asset(canonical, "qsee_content", "canonical GUI")
    require(
        "qsee_battery" in content_flow.get("screens", []),
        "qsee_content does not register qsee_battery",
    )
    transitions = content_flow.get("transitions", [])
    require(
        any(
            isinstance(transition, dict)
            and transition.get("action") == "qsee.open.battery"
            and transition.get("to") == "qsee_battery"
            for transition in transitions
        ),
        "canonical qsee.open.battery transition is missing",
    )

    scroller = find_node(
        canonical_screen,
        "battery_scroller",
        "qsee_battery",
    )
    require(
        scroller.get("commonProps", {}).get("scrollable") is True,
        "battery_scroller must be scrollable",
    )
    require(
        any(
            event.get("type") == "scroll"
            and event.get("action") == "qsee.chrome.activity.battery"
            for event in scroller.get("events", [])
            if isinstance(event, dict)
        ),
        "battery_scroller does not forward activity to Adaptive Chrome",
    )

    state = find_node(canonical_screen, "state", "qsee_battery")
    percent = find_node(canonical_screen, "percent", "qsee_battery")
    charge = find_node(canonical_screen, "charge", "qsee_battery")
    level = find_node(canonical_screen, "level", "qsee_battery")
    health = find_node(canonical_screen, "health", "qsee_battery")
    require_binding(state, "labelProps.text", "text", "battery state")
    require_binding(state, "style.textColor", "color", "battery state")
    require_binding(percent, "labelProps.text", "text", "battery percent")
    require_binding(percent, "style.textColor", "color", "battery percent")
    require_binding(charge, "commonProps.hidden", "hidden", "charge glyph")
    require_binding(charge, "style.textColor", "color", "charge glyph")
    require_binding(level, "commonProps.hidden", "hidden", "battery level")
    require_binding(level, "rangeProps.value", "value", "battery level")
    require_binding(
        level,
        "partStyles.indicator.style.bgColor",
        "color",
        "battery level",
    )
    require_binding(health, "labelProps.text", "text", "PMIC health")
    require_binding(health, "style.textColor", "color", "PMIC health")
    require(
        level.get("type") == "progressBar"
        and level.get("rangeProps", {}).get("min") == 0
        and level.get("rangeProps", {}).get("max") == 100,
        "battery level must be a 0..100 progress bar",
    )

    expected_bands = {
        "critical_band": "#ff3859",
        "warning_band": "#f0b44d",
        "normal_band": "#20dfc1",
    }
    for band_id, expected_color in expected_bands.items():
        band = find_node(canonical_screen, band_id, "qsee_battery")
        require(
            band.get("style", {}).get("bgColor") == expected_color,
            f"{band_id} must use {expected_color}",
        )
        require(
            band.get("placement", {}).get("width"),
            f"{band_id} must occupy a visible range",
        )

    for metric_id in (
        "cell_voltage",
        "usb_input",
        "system_rail",
        "pmic_die",
        "charge_policy",
        "charger_phase",
        "pmic_flags",
        "device_power",
        "pwr_key",
        "sample",
    ):
        metric = find_node(canonical_screen, metric_id, "qsee_battery")
        require(
            metric.get("type") == "templateRef"
            and metric.get("templateId") == "qsee_power_metric",
            f"{metric_id} must use qsee_power_metric",
        )

    truth_note = find_node(canonical_screen, "truth_note", "qsee_battery")
    require(
        re.search(
            r"\bno\s+estimat(?:e|es|ed|ion|ions)\b",
            truth_note.get("labelProps", {}).get("text", ""),
            flags=re.IGNORECASE,
        )
        is not None,
        "Battery UI must state that values are not estimated",
    )

    require(
        metric_document.get("type") == "viewTemplate"
        and metric_document.get("id") == "qsee_power_metric",
        "qsee_power_metric template has the wrong type or id",
    )
    metric_template = metric_document
    template_root = metric_template.get("node")
    require(
        isinstance(template_root, dict),
        "qsee_power_metric template has no root node",
    )
    accent = find_node(template_root, "accent", "qsee_power_metric")
    value = find_node(template_root, "value", "qsee_power_metric")
    detail = find_node(template_root, "detail", "qsee_power_metric")
    require_binding(accent, "style.bgColor", "color", "power metric accent")
    require_binding(value, "labelProps.text", "text", "power metric value")
    require_binding(value, "style.textColor", "color", "power metric value")
    require_binding(detail, "labelProps.text", "text", "power metric detail")


def verify_firmware_contract() -> None:
    app = read("components/qsee_app/qsee_app.cpp")
    navigation = read(
        "components/qsee_app/include/qsee_navigation_service.hpp"
    )
    status_bar = read("components/qsee_app/qsee_status_bar.cpp")

    require_regex(
        navigation,
        r"enum\s+class\s+Route\s*\{[^}]*\bBattery\s*,",
        "navigation Route enum",
    )
    require_regex(
        app,
        r"case\s+Screen::Battery\s*:\s*"
        r"return\s+LazyScreenDescriptor\s*\{\s*"
        r'\.document_path\s*=\s*"res/screen_battery\.json"\s*,\s*'
        r'\.screen_path\s*=\s*"/qsee_battery"\s*,?\s*\};',
        "Battery lazy screen descriptor",
    )
    require_regex(
        app,
        r'if\s*\(\s*action\s*==\s*"qsee\.open\.battery"\s*\)\s*\{\s*'
        r"return\s+open_screen\(\s*context\s*,\s*Screen::Battery\s*,\s*"
        r"action\s*\);",
        "Battery action route",
    )

    welcome_commands = initializer_text(
        app,
        "kWelcomeCommands",
        "qsee_app.cpp",
    )
    require_contains(
        welcome_commands,
        '"qsee.open.battery"',
        "Welcome commands",
    )
    forbid_contains(
        welcome_commands,
        '"qsee.open.logs"',
        "Welcome commands",
    )

    settings_commands = initializer_text(
        app,
        "kSettingsCommands",
        "qsee_app.cpp",
    )
    require_contains(
        settings_commands,
        '"qsee.open.logs"',
        "Settings commands",
    )

    hardware_details = function_text(
        app,
        "std::expected<void, std::string> App::show_hardware_details(",
        "qsee_app.cpp",
    )
    require_regex(
        hardware_details,
        r"if\s*\(\s*component_index\s*==\s*0U\s*\)\s*\{\s*"
        r"return\s+open_screen\(\s*context\s*,\s*Screen::Battery\s*,\s*"
        r'"qsee\.open\.battery"\s*\);',
        "Battery hardware row redirect",
    )

    refresh = function_text(
        app,
        "std::expected<void, std::string> App::refresh_battery(",
        "qsee_app.cpp",
    )
    require_contains(
        refresh,
        "const BatterySnapshot &battery = snapshot.battery;",
        "refresh_battery",
    )
    require_contains(
        refresh,
        "const bool pmic_ready = component_ready(battery);",
        "refresh_battery",
    )
    require_regex(
        refresh,
        r"const\s+bool\s+level_valid\s*=\s*"
        r"pmic_ready\s*&&\s*battery\.battery_present\s*&&\s*"
        r"battery\.level_percent\s*>=\s*0\s*&&\s*"
        r"battery\.level_percent\s*<=\s*100",
        "PMIC percentage validity gate",
    )
    require_contains(
        refresh,
        'std::string_view(battery.power_state) == "charging"',
        "refresh_battery charging state",
    )
    require_contains(
        refresh,
        '"%d%%",',
        "PMIC percentage text",
    )
    require_contains(
        refresh,
        "battery.level_percent",
        "PMIC percentage source",
    )
    require_contains(
        refresh,
        "std::to_string(battery.level_percent)",
        "PMIC progress source",
    )
    require_contains(
        refresh,
        '"/qsee_battery/surface/battery_scroller"',
        "Battery binding root",
    )
    for wording in ("UNAVAILABLE", "ERROR", "STALE"):
        require_contains(
            refresh,
            wording,
            "Battery unavailable/error presentation",
        )
    for field in (
        "battery.battery_mv",
        "battery.usb_mv",
        "battery.system_mv",
        "battery.die_temperature_c",
        "battery.last_error",
        "battery.last_success_at_us",
        "battery.data_stale",
        "battery.charger_phase",
        "battery.vbus_good",
        "battery.batfet_enabled",
        "battery.battery_active_mode",
        "battery.thermal_regulation_active",
        "battery.input_current_limit_active",
        "snapshot.power.state",
        "snapshot.power.reason",
        "snapshot.input.power_short_press_count",
        "snapshot.input.power_long_press_count",
    ):
        require_contains(refresh, field, "Battery PMIC telemetry")

    forbidden_derivations = (
        r"\blevel_percent\s*=\s*[^;]*battery_mv",
        r"\b(?:percent|level)[A-Za-z0-9_]*\s*=\s*"
        r"[^;]*battery\.battery_mv\s*[-+*/%]",
        r"\bbattery\.battery_mv\s*[-+*/%]\s*[^;]*"
        r"(?:percent|level)",
        r"\b(?:map|interpolat|estimat|deriv)[A-Za-z0-9_]*\s*\("
        r"[^;]*battery\.battery_mv",
    )
    for pattern in forbidden_derivations:
        require(
            re.search(pattern, refresh, flags=re.IGNORECASE | re.DOTALL)
            is None,
            f"refresh_battery derives percentage from voltage: /{pattern}/",
        )

    battery_charging = function_text(
        status_bar,
        "bool battery_charging(",
        "qsee_status_bar.cpp",
    )
    require_contains(
        battery_charging,
        'std::string_view(battery.power_state) == "charging"',
        "status-bar charging predicate",
    )
    require_contains(
        battery_charging,
        "battery.battery_present",
        "status-bar charging predicate",
    )
    forbid_contains(
        battery_charging,
        ".find(",
        "status-bar charging predicate",
    )
    forbid_contains(
        battery_charging,
        "strstr(",
        "status-bar charging predicate",
    )


def verify_simulator_contract() -> None:
    simulator = read("simulator/windows/qsee_simulator.py")
    battery_demo = python_function_text(
        simulator,
        "    def show_battery_demo(",
        "Windows simulator",
    )
    logs_demo = python_function_text(
        simulator,
        "    def show_logs_demo(",
        "Windows simulator",
    )
    on_click = python_function_text(
        simulator,
        "    def _on_click(",
        "Windows simulator",
    )
    redraw = python_function_text(
        simulator,
        "    def _redraw_current_screen(",
        "Windows simulator",
    )
    back = python_function_text(
        simulator,
        "    def back(",
        "Windows simulator",
    )
    main = python_function_text(
        simulator,
        "def main() -> None:",
        "Windows simulator",
    )

    require_contains(
        battery_demo,
        'self.screen = "battery"',
        "show_battery_demo",
    )
    require_contains(
        battery_demo,
        "SYNTHETIC",
        "visible Battery simulator disclosure",
    )
    require_contains(
        battery_demo,
        "AXP2101",
        "Battery simulator PMIC identity",
    )
    require_contains(
        battery_demo,
        "self.canvas.create_text",
        "Battery simulator telemetry rendering",
    )
    require_contains(logs_demo, 'self.screen = "logs"', "show_logs_demo")

    require(
        len(re.findall(r"self\.show_battery_demo\(", on_click)) >= 2,
        "Welcome and Hardware row 0 must both route to show_battery_demo",
    )
    require_regex(
        on_click,
        r'self\.screen\s*==\s*"home"[\s\S]{0,500}'
        r"self\.show_battery_demo\(",
        "Welcome Battery click route",
    )
    require_regex(
        on_click,
        r'self\.screen\s*==\s*"hardware"[\s\S]{0,900}'
        r"self\.show_battery_demo\(",
        "Hardware Battery row click route",
    )
    require_regex(
        on_click,
        r'self\.screen\s*==\s*"settings"[\s\S]{0,1500}'
        r"self\.show_logs_demo\(\)",
        "Settings Logs click route",
    )

    require_regex(
        redraw,
        r'["\']battery["\']\s*:\s*self\.show_battery_demo',
        "Battery redraw route",
    )
    require_regex(
        redraw,
        r'["\']logs["\']\s*:\s*self\.show_logs_demo',
        "Logs redraw route",
    )
    require_contains(back, '"battery"', "simulator Back routes")
    require_contains(back, '"logs"', "simulator Back routes")

    require_contains(main, '"battery",', "simulator --screen choices")
    require_contains(main, '"logs",', "simulator --screen choices")
    require_regex(
        main,
        r'["\']battery["\']\s*:\s*simulator\.show_battery_demo',
        "Battery capture route",
    )
    require_regex(
        main,
        r'["\']logs["\']\s*:\s*simulator\.show_logs_demo',
        "Logs capture route",
    )


def main() -> int:
    try:
        verify_documents()
        verify_firmware_contract()
        verify_simulator_contract()
    except (AssertionError, OSError) as error:
        print(f"battery UI verification: FAIL: {error}", file=sys.stderr)
        return 1

    print("battery UI verification: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
