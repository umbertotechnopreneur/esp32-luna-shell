#!/usr/bin/env python3
"""Hardware-free contract checks for the Windows LUNASHELL simulator.

The verifier intentionally imports only the deterministic device model and the
loopback fake-device tests.  It never opens a serial port, invokes ESP-IDF, or
talks to physical hardware.
"""

from __future__ import annotations

from pathlib import Path
import sys

# Direct command execution resolves the shared repository package explicitly.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from contextlib import redirect_stderr
import io
import json
import traceback
from typing import Callable
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SIMULATOR_DIR = REPOSITORY_ROOT / "simulator" / "windows"
SIMULATOR_TEST_DIR = SIMULATOR_DIR / "tests"
sys.path.insert(0, str(SIMULATOR_DIR))

from device_model import (  # noqa: E402
    ScenarioEngine,
    ScenarioValidationError,
    SemanticActionError,
    VirtualClock,
    load_scenario,
    scenario_names,
    validate_snapshot,
)
from board_shell import ShellContract  # noqa: E402
from scripts.common.board_config import validate_board_config_data  # noqa: E402
import yaml  # noqa: E402


Check = Callable[[], str]


class VerificationFailure(AssertionError):
    """Raised when a simulator contract check does not hold."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationFailure(message)


def require_raises(
    exception_type: type[BaseException],
    operation: Callable[[], object],
    message: str,
) -> BaseException:
    try:
        operation()
    except exception_type as error:
        return error
    except Exception as error:  # pragma: no cover - diagnostic path
        raise VerificationFailure(
            f"{message}: expected {exception_type.__name__}, got "
            f"{type(error).__name__}: {error}"
        ) from error
    raise VerificationFailure(
        f"{message}: expected {exception_type.__name__}, but it succeeded"
    )


def check_all_scenario_fixtures() -> str:
    names = scenario_names()
    require(names, "the built-in scenario catalog is empty")
    require(tuple(sorted(names)) == names, "scenario catalog is not deterministic")
    require(len(names) == len(set(names)), "scenario catalog contains duplicates")

    endpoint_paths = (
        "/api/v1/health",
        "/api/v1/capabilities",
        "/api/v1/info",
        "/api/v1/diagnostics",
        "/api/v1/rtc",
        "/api/v1/gps",
        "/api/v1/status",
    )
    event_count = 0
    for name in names:
        definition = load_scenario(name)
        require(definition.name == name, f"scenario {name} has a mismatched name")
        require(definition.source.is_file(), f"scenario {name} source is missing")

        engine = ScenarioEngine.from_scenario(name)
        initial = engine.generation
        require(initial.simulator["synthetic"] is True, f"scenario {name} is not labelled synthetic")
        require(initial.simulator["scenario"] == name, f"scenario {name} metadata is inconsistent")
        validate_snapshot(engine.snapshot(), now_us=engine.clock.now_us)

        previous_sequence = initial.sequence
        for event in definition.timeline:
            generation = engine.advance_to_ms(event.at_ms)
            event_count += 1
            require(
                generation.at_us == definition.initial_time_us + event.at_ms * 1000,
                f"scenario {name} did not stop at event {event.at_ms} ms",
            )
            require(
                generation.sequence == previous_sequence + 1,
                f"scenario {name} did not publish one sequence for {event.reason}",
            )
            require(
                generation.snapshot["reason"] == event.reason,
                f"scenario {name} did not publish reason {event.reason}",
            )
            validate_snapshot(engine.snapshot(), now_us=engine.clock.now_us)
            previous_sequence = generation.sequence

        json.loads(engine.dump_snapshot())
        for path in endpoint_paths:
            payload = engine.api_payload(path)
            require(payload["simulator"]["synthetic"] is True, f"{name} {path} lost its synthetic marker")
            json.dumps(payload, allow_nan=False)

    invalid = ScenarioEngine.from_scenario("healthy")
    corrupt_snapshot = invalid.snapshot()
    corrupt_snapshot["battery"]["level_percent"] = 101
    require_raises(
        ScenarioValidationError,
        lambda: validate_snapshot(corrupt_snapshot, now_us=invalid.clock.now_us),
        "an incoherent battery percentage was accepted",
    )

    return f"{len(names)} fixtures and {event_count} timeline events"


def check_deterministic_clock_and_timeline() -> str:
    definitions = [load_scenario(name) for name in scenario_names()]
    definition = next((item for item in definitions if len(item.timeline) >= 2), None)
    require(definition is not None, "no multi-event scenario exists for clock verification")

    checkpoints = {0}
    for event in definition.timeline:
        if event.at_ms > 1:
            checkpoints.add(event.at_ms - 1)
        checkpoints.add(event.at_ms)
        checkpoints.add(event.at_ms + 1)

    left = ScenarioEngine.from_scenario(definition.name)
    right = ScenarioEngine.from_scenario(definition.name)
    for at_ms in sorted(checkpoints):
        left_generation = left.advance_to_ms(at_ms)
        right_generation = right.advance_to_ms(at_ms)
        require(
            left_generation.to_dict() == right_generation.to_dict(),
            f"scenario {definition.name} diverged at {at_ms} ms",
        )
        require(
            left.clock.now_us == definition.initial_time_us + at_ms * 1000,
            f"scenario clock is incorrect at {at_ms} ms",
        )

    clock = VirtualClock(1_000_000)
    require(clock.advance_ms(25) == 1_025_000, "virtual clock millisecond advance is incorrect")
    require(clock.advance_to_us(1_030_000) == 1_030_000, "virtual clock target advance is incorrect")
    require_raises(
        ScenarioValidationError,
        lambda: clock.advance_to_us(1_029_999),
        "virtual clock moved backwards",
    )

    return f"{definition.name} at {len(checkpoints)} deterministic checkpoints"


def check_immutable_generations() -> str:
    engine = ScenarioEngine.from_scenario("healthy")
    first = engine.generation
    first_value = first.to_dict()

    def mutate_top_level() -> None:
        first.snapshot["sequence"] = 999  # type: ignore[index]

    def mutate_nested() -> None:
        first.snapshot["battery"]["level_percent"] = 0  # type: ignore[index]

    require_raises(TypeError, mutate_top_level, "generation top level is mutable")
    require_raises(TypeError, mutate_nested, "generation nested state is mutable")

    detached = engine.snapshot()
    detached["battery"]["level_percent"] = 0
    require(
        engine.generation.snapshot["battery"]["level_percent"] != 0,
        "snapshot() returned an alias to live generation state",
    )

    engine.advance_ms(10)
    require(first.to_dict() == first_value, "an older generation changed after publication")
    require(engine.generation is not first, "clock advance reused the same generation object")
    require(engine.generation.at_us == first.at_us + 10_000, "new generation has the wrong timestamp")

    return "deep freeze, detached copies, and stable prior generation"


def check_semantic_actions() -> str:
    wireless = ScenarioEngine.from_scenario("healthy")
    started = wireless.action("start:wireless").snapshot["radios"]["wifi"]
    require(started["started"] and started["connected"] and started["ip_acquired"], "wireless did not start and connect")
    require_raises(
        SemanticActionError,
        lambda: wireless.action("start:wireless"),
        "wireless start ignored its running-state precondition",
    )
    stopped = wireless.action("stop:wireless").snapshot["radios"]["wifi"]
    require(not stopped["started"] and not stopped["connected"], "wireless did not stop cleanly")
    toggled = wireless.action("toggle:wireless").snapshot["radios"]["wifi"]
    require(toggled["started"] and toggled["connected"], "wireless toggle did not restart")

    storage = ScenarioEngine.from_scenario("sd-unformatted")
    formatted = storage.action("confirm:format-sd").snapshot["storage"]
    require(formatted["authorization_consumed"], "SD authorization was not consumed")
    require(formatted["mounted"] and formatted["formatted"], "SD format did not produce a mounted formatted card")
    require(not formatted["format_required"], "SD still requests formatting after success")
    require_raises(
        SemanticActionError,
        lambda: storage.action("confirm:format-sd"),
        "single-use SD authorization was accepted twice",
    )
    removed = storage.action("remove:sd").snapshot["storage"]
    require(not removed["card_response_verified"] and not removed["mounted"], "SD removal kept the card present")
    inserted = storage.action("insert:sd").snapshot["storage"]
    require(inserted["format_required"] and not inserted["authorization_consumed"], "new SD insertion did not require fresh authorization")

    distance = ScenarioEngine.from_scenario("healthy")
    calibrated = distance.action("calibrate:tof").snapshot["distance"]["calibration"]
    require(calibrated["state"] == "succeeded" and calibrated["record_valid"], "TOF calibration did not succeed")
    missing_tof = ScenarioEngine.from_scenario("tof-one-missing")
    require_raises(
        SemanticActionError,
        lambda: missing_tof.action("calibrate:tof"),
        "TOF calibration accepted a missing sensor",
    )
    missing_tof.advance_to_ms(4000)
    recovered = missing_tof.action("calibrate:tof").snapshot["distance"]["calibration"]
    require(recovered["state"] == "succeeded", "TOF calibration did not recover with both sensors current")

    return "wireless, single-use SD format, and TOF preconditions"


def check_info_redaction() -> str:
    engine = ScenarioEngine.from_scenario("healthy")
    engine.action("start:wireless")
    source = engine.snapshot()
    info = engine.api_payload("/api/v1/info")
    diagnostics = engine.api_payload("/api/v1/diagnostics")

    require(info["schema"] == "LUNASHELL.device.info.v1", "info schema is incorrect")
    require(diagnostics["schema"] == "LUNASHELL.device.diagnostics.v1", "diagnostics schema is incorrect")
    require("diagnostics" not in info["data"], "info leaked the diagnostics component")
    require("diagnostics" in diagnostics["data"], "diagnostics omitted the diagnostics component")
    require("first_sentence" not in info["data"]["gps"], "info leaked the raw NMEA sentence")
    require(
        diagnostics["data"]["gps"]["first_sentence"] == source["gps"]["first_sentence"],
        "diagnostics did not preserve the raw NMEA sentence",
    )

    for field in ("ssid", "ip_address", "mac_address"):
        require(field not in info["data"]["radios"]["wifi"], f"info leaked Wi-Fi {field}")
        require(
            diagnostics["data"]["radios"]["wifi"][field] == source["radios"]["wifi"][field],
            f"diagnostics did not preserve Wi-Fi {field}",
        )

    serialized_info = json.dumps(info, sort_keys=True)
    for sensitive_value in (
        source["gps"]["first_sentence"],
        source["radios"]["wifi"]["ssid"],
        source["radios"]["wifi"]["ip_address"],
        source["radios"]["wifi"]["mac_address"],
    ):
        require(sensitive_value not in serialized_info, f"redacted value {sensitive_value!r} remains in info")

    return "info redacts NMEA and Wi-Fi identity; diagnostics retains them"


def check_fake_http_transport() -> str:
    require(SIMULATOR_TEST_DIR.is_dir(), "simulator test directory is missing")
    suite = unittest.defaultTestLoader.discover(
        str(SIMULATOR_TEST_DIR),
        pattern="test_fake_device_server.py",
    )
    test_count = suite.countTestCases()
    require(test_count > 0, "no fake HTTP tests were discovered")
    output = io.StringIO()
    transport_log = io.StringIO()
    with redirect_stderr(transport_log):
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(suite)
    if not result.wasSuccessful():
        raise VerificationFailure(
            "fake HTTP loopback suite failed:\n"
            + output.getvalue().rstrip()
            + "\n"
            + transport_log.getvalue().rstrip()
        )
    return f"{test_count} loopback/auth/control-plane tests"


def check_ui_bootstrap_shell() -> str:
    contract = ShellContract.load()
    initial = contract.status()
    require(initial["synthetic"] is True, "ui-bootstrap preview lost its synthetic label")
    require(initial["profile"] == "ui-minimal", "ui-bootstrap selected the wrong compiler profile")
    require(initial["screens"] == ["LUNASHELL_shell"], "ui-bootstrap gained device pages")
    require(initial["surface"] == {"x": 73, "y": 73, "width": 320, "height": 320},
            "ui-bootstrap touch region differs from the firmware package")
    require(initial["mascot"]["pose"] == "front_idle", "shell idle pose differs")
    require(initial["mascot"]["face"] == "rigged", "shell face rig is missing")
    require(initial["mascot"]["breathing"] is True, "shell idle breathing is missing")
    require(contract.status(2)["touch_count"] == 2, "shell touch count differs")
    require(contract.contains_touch(73, 73) and contract.contains_touch(392, 392),
            "shell rejects points inside the touch surface")
    require(not contract.contains_touch(72, 73) and not contract.contains_touch(393, 392),
            "shell accepts points outside the touch surface")

    board_path = REPOSITORY_ROOT / "config" / "boards" / contract.board / "ui-bootstrap.yaml"
    source = yaml.safe_load(board_path.read_text(encoding="utf-8"))
    for module in ("touch", "audio_output", "sd"):
        changed = json.loads(json.dumps(source))
        changed["modules"][module]["mode"] = "ui" if module != "touch" else "off"
        require_raises(ValueError, lambda changed=changed: validate_board_config_data(changed),
                       f"unsupported {module} mode was accepted")
    for module in ("pmu", "imu", "gnss", "audio_output", "microphones", "sd"):
        changed = json.loads(json.dumps(source))
        changed["modules"][module]["mode"] = "service"
        result = validate_board_config_data(changed)
        require(result["modules"][module] == "service", f"{module} service was rejected")
    changed = json.loads(json.dumps(source))
    changed["modules"]["battery"]["mode"] = "service"
    require_raises(ValueError, lambda: validate_board_config_data(changed),
                   "battery service was accepted without PMU")
    changed["modules"]["pmu"]["mode"] = "service"
    require(validate_board_config_data(changed)["modules"]["battery"] == "service",
            "battery service with PMU was rejected")
    services_path = board_path.with_name("ui-services.yaml")
    all_services = yaml.safe_load(services_path.read_text(encoding="utf-8"))
    selected = validate_board_config_data(all_services)["modules"]
    require(all(selected[name] == "service" for name in (
        "pmu", "battery", "imu", "gnss", "audio_output", "microphones", "sd"
    )), "all-services example does not enable every selectable module")
    return "source YAML, shell JSON, touch bounds, and independent service switches"


def main() -> int:
    checks: tuple[tuple[str, Check], ...] = (
        ("scenario fixtures", check_all_scenario_fixtures),
        ("deterministic clock and timeline", check_deterministic_clock_and_timeline),
        ("immutable generations", check_immutable_generations),
        ("semantic actions", check_semantic_actions),
        ("info redaction", check_info_redaction),
        ("fake HTTP transport", check_fake_http_transport),
        ("ui-bootstrap shell", check_ui_bootstrap_shell),
    )

    failures = 0
    for label, check in checks:
        try:
            detail = check()
        except Exception as error:  # pragma: no cover - command-line diagnostic path
            failures += 1
            print(f"FAIL  {label}: {error}", file=sys.stderr)
            if not isinstance(error, VerificationFailure):
                traceback.print_exc()
        else:
            print(f"PASS  {label}: {detail}")

    if failures:
        print(
            f"FAIL  simulator verification: {failures} of {len(checks)} checks failed",
            file=sys.stderr,
        )
        return 1

    print(f"PASS  simulator verification: all {len(checks)} checks passed (hardware-free)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
