#!/usr/bin/env python3
"""Static contract checks for adaptive chrome and the full-screen viewport."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GUI_RES = ROOT / "components/qsee_app/package/res"
DISPLAY_SIZE = 466
CHROME_INSET = 73
SAFE_CONTENT_SIZE = 320


def read(path: str) -> str:
    return ROOT.joinpath(path).read_text(encoding="utf-8")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"{context}: missing {token!r}")


def load_runtime_documents() -> dict[str, dict]:
    paths = [GUI_RES / "root.json", *sorted(GUI_RES.glob("screen_*.json"))]
    documents: dict[str, dict] = {}
    for path in paths:
        relative = path.relative_to(GUI_RES).as_posix()
        try:
            document = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise AssertionError(
                f"invalid GUI JSON {relative}:{error.lineno}:{error.colno}: "
                f"{error.msg}"
            ) from error
        if not isinstance(document, dict) or not isinstance(
            document.get("assets"), list
        ):
            raise AssertionError(f"{relative} is not a GUI runtime document")
        documents[relative] = document
    return documents


def find_asset(documents: dict[str, dict], asset_id: str) -> dict:
    matches: list[tuple[str, dict]] = []
    for source, document in documents.items():
        for asset in document["assets"]:
            if isinstance(asset, dict) and asset.get("id") == asset_id:
                matches.append((source, asset))
    if not matches:
        raise AssertionError(f"missing asset {asset_id}")
    if len(matches) != 1:
        owners = ", ".join(source for source, _ in matches)
        raise AssertionError(f"duplicate asset {asset_id}: {owners}")
    return matches[0][1]


def find_child(node: dict, child_id: str) -> dict:
    for child in node.get("children", []):
        if child.get("id") == child_id:
            return child
    raise AssertionError(f"{node.get('id', '<node>')}: missing child {child_id}")


def verify_geometry_matrix() -> None:
    expected = {
        (True, True): (CHROME_INSET, SAFE_CONTENT_SIZE),
        (True, False): (CHROME_INSET, DISPLAY_SIZE - CHROME_INSET),
        (False, True): (0, DISPLAY_SIZE - CHROME_INSET),
        (False, False): (0, DISPLAY_SIZE),
    }
    for visibility, expected_geometry in expected.items():
        status_visible, command_visible = visibility
        y = CHROME_INSET if status_visible else 0
        height = (
            DISPLAY_SIZE
            - y
            - (CHROME_INSET if command_visible else 0)
        )
        if (y, height) != expected_geometry:
            raise AssertionError(
                f"viewport {visibility}: {(y, height)} != {expected_geometry}"
            )


def verify_boot_fullscreen_model() -> None:
    class Policy:
        def __init__(self) -> None:
            self.device_state = "active"
            self.display_stage = "active"
            self.fullscreen = False
            self.command_visible = True
            self.status_visible = True
            self.armed = False

        def release_for(self, elapsed_ms: int) -> None:
            self.armed = elapsed_ms >= 500

        def short_press(self) -> None:
            if not self.armed or self.device_state != "active":
                return
            if self.display_stage != "active":
                self.display_stage = "active"
                self.fullscreen = False
                self.command_visible = True
                self.status_visible = True
                return
            if self.fullscreen or (
                not self.command_visible and not self.status_visible
            ):
                self.fullscreen = False
                self.command_visible = True
                self.status_visible = True
                return
            self.fullscreen = True
            self.command_visible = False
            self.status_visible = False

        def light_sleep_wake(self) -> None:
            self.armed = False

    policy = Policy()
    policy.release_for(499)
    policy.short_press()
    if policy.fullscreen:
        raise AssertionError("BOOT armed before the 500 ms release boundary")

    policy.release_for(500)
    policy.short_press()
    if not policy.fullscreen or policy.command_visible or policy.status_visible:
        raise AssertionError("first BOOT short press did not enter Fullscreen")
    policy.short_press()
    if policy.fullscreen or not policy.command_visible or not policy.status_visible:
        raise AssertionError("second BOOT short press did not restore both arcs")

    policy.display_stage = "off"
    policy.short_press()
    if policy.display_stage != "active" or policy.fullscreen:
        raise AssertionError("BOOT did not use wake-first from display off")

    policy.command_visible = False
    policy.status_visible = False
    policy.short_press()
    if not policy.command_visible or not policy.status_visible:
        raise AssertionError("BOOT did not restore automatically hidden chrome")

    policy.device_state = "standby"
    policy.command_visible = False
    policy.status_visible = False
    policy.short_press()
    if policy.command_visible or policy.status_visible:
        raise AssertionError("BOOT changed chrome while in standby")

    policy.device_state = "active"
    policy.light_sleep_wake()
    policy.short_press()
    if policy.fullscreen:
        raise AssertionError("light-sleep wake did not rearm the release guard")


def verify_package() -> None:
    documents = load_runtime_documents()
    screens = (
        "qsee_welcome",
        "qsee_battery",
        "qsee_logs",
        "qsee_hardware",
        "qsee_hardware_detail",
        "qsee_audio",
        "qsee_audio_fft",
        "qsee_audio_detail",
        "qsee_imu",
        "qsee_gps",
        "qsee_tof",
        "qsee_tof_settings",
        "qsee_settings",
        "qsee_storage",
        "qsee_about",
    )
    padded_screens = set(screens) - {"qsee_about"}

    for screen_id in screens:
        surface = find_child(find_asset(documents, screen_id), "surface")
        placement = surface["placement"]
        bindings = surface["bindings"]
        expected_placement = {
            "mode": "absolute",
            "x": "73dp",
            "y": "73dp",
            "width": "320dp",
            "height": "320dp",
        }
        for key, expected in expected_placement.items():
            if placement.get(key) != expected:
                raise AssertionError(
                    f"{screen_id}/surface placement.{key}: "
                    f"{placement.get(key)!r} != {expected!r}"
                )
        if bindings.get("placement.y") != "viewport_y":
            raise AssertionError(f"{screen_id}: viewport y is not bound")
        if bindings.get("placement.height") != "viewport_height":
            raise AssertionError(f"{screen_id}: viewport height is not bound")
        if screen_id in padded_screens:
            if bindings.get("style.paddingTop") != "viewport_padding_top":
                raise AssertionError(f"{screen_id}: top padding is not bound")
            if (
                bindings.get("style.paddingBottom")
                != "viewport_padding_bottom"
            ):
                raise AssertionError(f"{screen_id}: bottom padding is not bound")

    expanding_scrollers = {
        "qsee_battery": "battery_scroller",
        "qsee_logs": "log_scroller",
        "qsee_hardware": "hardware_scroller",
        "qsee_hardware_detail": "detail_scroller",
        "qsee_tof_settings": "tof_settings_scroller",
        "qsee_settings": "settings_scroller",
        "qsee_storage": "storage_scroller",
    }
    for screen_id, scroller_id in expanding_scrollers.items():
        surface = find_child(find_asset(documents, screen_id), "surface")
        scroller = find_child(surface, scroller_id)
        if scroller["placement"].get("flexGrow") != 1:
            raise AssertionError(f"{screen_id}/{scroller_id}: flexGrow must be 1")
        if "height" in scroller["placement"]:
            raise AssertionError(
                f"{screen_id}/{scroller_id}: fixed height blocks adaptation"
            )

    pairing = json.loads(
        read(
            "components/qsee_app/package/res/templates/"
            "qsee_pairing_stage.json"
        )
    )["node"]
    if pairing["placement"].get("x") != "73dp":
        raise AssertionError("pairing viewport is not horizontally centered")
    for target, binding in (
        ("placement.y", "viewport_y"),
        ("placement.height", "viewport_height"),
        ("style.paddingTop", "viewport_padding_top"),
        ("style.paddingBottom", "viewport_padding_bottom"),
    ):
        if pairing["bindings"].get(target) != binding:
            raise AssertionError(f"pairing: {target} is not bound to {binding}")


def verify_firmware_contract() -> None:
    snapshot = read(
        "components/qsee_platform/include/qsee_device_snapshot.hpp"
    )
    power = read("components/qsee_platform/qsee_power.cpp")
    chrome = read("components/qsee_app/qsee_adaptive_chrome.cpp")
    command_bar = read(
        "components/qsee_app/qsee_adaptive_command_bar.cpp"
    )
    command_bar_header = read(
        "components/qsee_app/include/qsee_adaptive_command_bar.hpp"
    )
    simulator = read("simulator/windows/qsee_simulator.py")
    app = read("components/qsee_app/qsee_app.cpp")
    http = read("components/qsee_platform/qsee_http_json.cpp")

    require(snapshot, "bool fullscreen_active = false;", "snapshot")
    require(power, "handle_boot_short_press()", "BOOT toggle")
    require(power, "s_fullscreen_active = true;", "BOOT toggle")
    require(power, "kBootArmReleaseTicks", "BOOT release guard")
    require(power, "s_pending_wakeup_cause", "light-sleep BOOT rearm")
    require(power, "!s_fullscreen_active &&", "idle policy")
    require(chrome, "!fullscreen_active_ &&", "wake surface")
    require(command_bar, "if (!visible)", "Command Arc hide boundary")
    require(
        command_bar_header,
        "kMainDiameterDp = 61",
        "Command Arc enlarged diameter",
    )
    require(
        command_bar,
        "kMainCenterYDp = 410",
        "Command Arc lowered center",
    )
    require(
        command_bar,
        "((first_diameter + second_diameter) / 2)",
        "Command Arc constant edge gap",
    )
    require(
        simulator,
        "COMMAND_MAIN_DIAMETER = 61",
        "simulator Command Arc diameter",
    )
    require(
        simulator,
        "COMMAND_MAIN_CENTER_Y = 410",
        "simulator Command Arc lowered center",
    )
    require(
        command_bar,
        "set_hit_testing(gui, false)",
        "Command Arc hide boundary",
    )
    require(app, "sync_content_viewport(context)", "viewport synchronization")
    require(app, "kCircularWideContentInsetDp", "round-display padding")
    require(app, '"/qsee_tof/surface"', "TOF fixed canvas")
    require(app, '"/qsee_gps/surface"', "GPS fixed canvas")
    require(
        app,
        '"/qsee_tof_settings/surface"',
        "TOF settings circular scroller",
    )
    require(http, '\\"fullscreen_active\\"', "HTTP snapshot")


def main() -> None:
    verify_geometry_matrix()
    verify_boot_fullscreen_model()
    verify_package()
    verify_firmware_contract()
    print("Fullscreen layout verification passed")


if __name__ == "__main__":
    main()
