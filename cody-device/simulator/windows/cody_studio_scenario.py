"""Strict, portable synthetic scenario contracts for the native Studio runner."""

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re

from cody_native_runtime import ROOT
from cody_studio_catalog import entry_for

SCHEMA = "cody.studio.scenario.v1"
ARTIFACT_ROOT = (ROOT / "artifacts/simulator").resolve()
DEFAULT_PATH = Path(__file__).with_name("scenarios") / "cody-studio-p04.json"


# pairs: JSON object's ordered key/value pairs, before dictionary construction.
# Raises ValueError for duplicate keys instead of silently accepting the last value.
def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate scenario field: {key}")
        result[key] = value
    return result


# value: Proposed mapping at one schema boundary.
# fields: Exact keys admitted at this boundary.
# name: Human-readable field path for failures.
# Raises ValueError for missing, extra or non-object values.
def exact_fields(value, fields, name):
    if not isinstance(value, dict) or set(value) != set(fields):
        raise ValueError(f"{name} requires exactly: {', '.join(sorted(fields))}")


# value: Proposed integer value; booleans are deliberately rejected.
# minimum: Inclusive lower bound.
# maximum: Inclusive upper bound.
# name: Human-readable field path for failures.
# Raises ValueError when the integer does not satisfy the contract.
def bounded_int(value, minimum, maximum, name):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{name} must be an integer from {minimum} to {maximum}")


# scenario: Complete explicit synthetic case; no implicit PC input is admitted.
# Raises ValueError for unsupported identities, variants, times, fields or services.
def validate_scenario(scenario):
    exact_fields(scenario, {"schema", "id", "title", "variant", "seed", "common", "page",
                            "context", "time", "services"}, "Scenario")
    if scenario["schema"] != SCHEMA:
        raise ValueError(f"Unsupported Studio scenario schema: {scenario['schema']!r}")
    if not isinstance(scenario["id"], str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", scenario["id"]):
        raise ValueError("Scenario id must be a stable 1..64 character ASCII key")
    if not isinstance(scenario["title"], str) or not scenario["title"].strip() or len(scenario["title"]) > 120:
        raise ValueError("Scenario title must contain 1..120 characters")
    bounded_int(scenario["seed"], 0, 0, "seed (P04 does not use randomness)")
    context = scenario["context"]
    exact_fields(context, {"target", "selection", "caller", "return_to"}, "context")
    entry = entry_for(context["target"])
    if context["target"] != entry["id"]:
        raise ValueError("Scenario context.target requires a qualified stable source identity")
    if entry["id"] != "ui-v1:display" or entry["availability"] != "runtime-full":
        raise ValueError(f"Native Studio preparation is unsupported: {context['target']!r}")
    if scenario["variant"] not in entry["variants"]:
        raise ValueError(f"Unsupported variant for {entry['id']}: {scenario['variant']!r}")
    if context["selection"] is not None or context["caller"] != "atlas-v1:10" or context["return_to"] != context["caller"]:
        raise ValueError("P04 requires no selection and the Settings root caller/return context")
    if scenario["page"] != {} or scenario["services"] != {}:
        raise ValueError("P04 has no page payload or asynchronous service responses")
    exact_fields(scenario["time"], {"mode", "initial_ms", "advances_ms", "capture_at_ms"}, "time")
    timing = scenario["time"]
    if (type(timing["initial_ms"]) is not int or not isinstance(timing["advances_ms"], list) or
            not isinstance(timing["capture_at_ms"], list) or
            any(type(value) is not int for value in timing["advances_ms"] + timing["capture_at_ms"]) or
            timing != {"mode": "controlled-static", "initial_ms": 0, "advances_ms": [], "capture_at_ms": [0]}):
        raise ValueError("P04 supports only a controlled static sample at t=0; timeline replay is unavailable")
    common = scenario["common"]
    exact_fields(common, {"settings", "observations"}, "common")
    settings = common["settings"]
    exact_fields(settings, {"brightness_percent", "auto_rotate", "orientation_degrees"}, "common.settings")
    bounded_int(settings["brightness_percent"], 10, 100, "brightness_percent")
    if type(settings["auto_rotate"]) is not bool:
        raise ValueError("auto_rotate must be boolean")
    if type(settings["orientation_degrees"]) is not int or settings["orientation_degrees"] not in (0, 180):
        raise ValueError("P04 orientation_degrees supports only 0 or 180 in this host profile")
    observations = common["observations"]
    exact_fields(observations, {"battery", "network", "clock"}, "common.observations")
    exact_fields(observations["battery"], {"level_percent"}, "battery")
    bounded_int(observations["battery"]["level_percent"], 0, 100, "battery.level_percent")
    network = observations["network"]
    exact_fields(network, {"connected", "ssid", "rssi"}, "network")
    if type(network["connected"]) is not bool:
        raise ValueError("network.connected must be boolean")
    if not isinstance(network["ssid"], str) or len(network["ssid"]) > 32 or any(not 32 <= ord(char) <= 126 for char in network["ssid"]):
        raise ValueError("network.ssid must be at most 32 printable ASCII characters")
    if network["connected"]:
        if not network["ssid"]:
            raise ValueError("A connected synthetic network requires an SSID")
        bounded_int(network["rssi"], -100, -1, "network.rssi")
    elif network["ssid"] or network["rssi"] is not None:
        raise ValueError("A disconnected synthetic network requires an empty SSID and null RSSI")
    clock = observations["clock"]
    exact_fields(clock, {"epoch_seconds", "utc_offset_minutes"}, "clock")
    bounded_int(clock["epoch_seconds"], 1704067200, 4102444799, "clock.epoch_seconds")
    bounded_int(clock["utc_offset_minutes"], -840, 840, "clock.utc_offset_minutes")
    return deepcopy(scenario)


# path: Scenario JSON path, read without creating files or native state.
# Raises I/O, JSON or contract errors with no fallback to a different scenario.
def load_scenario(path):
    return validate_scenario(json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=unique_object))


# No parameters: return a fresh copy of the versioned P04 fixture.
# Raises its original file/schema error if the checked-in fixture is invalid.
def default_scenario():
    return load_scenario(DEFAULT_PATH)


# scenario: Complete validated scenario whose canonical content defines its revision.
# Raises ValueError for an invalid scenario; ordering and formatting do not change identity.
def scenario_revision(scenario):
    canonical = json.dumps(validate_scenario(scenario), sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(canonical.encode("ascii")).hexdigest()


# scenario: Complete synthetic scenario; copied and validated before persistence.
# path: New JSON path beneath artifacts/simulator; existing files are preserved.
# Raises I/O, FileExistsError or ValueError for unsafe output or invalid content.
def save_scenario(scenario, path):
    value = validate_scenario(scenario)
    destination = Path(path).resolve()
    if not destination.is_relative_to(ARTIFACT_ROOT) or destination.suffix.lower() != ".json":
        raise ValueError("Studio scenarios must be saved as JSON beneath artifacts/simulator/")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("x", encoding="utf-8") as output:
        json.dump(value, output, indent=2, ensure_ascii=True)
        output.write("\n")
    return destination
