"""Strict, portable synthetic scenario contracts for the native Studio runner."""

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re

from cody_native_runtime import ROOT
from cody_studio_catalog import inventory_label, entry_for, scenario_contract_for

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


# value: One exact object from the selected catalog contract.
# fields: Machine-readable field specifications, including nested object boundaries.
# name: Human-readable path for precise input failures.
# Raises ValueError for unknown fields, types, ranges, choices or unsupported text.
def validate_fields(value, fields, name):
    exact_fields(value, fields, name)
    for key, specification in fields.items():
        item, path = value[key], f"{name}.{key}"
        kind = specification["type"]
        if kind == "object":
            validate_fields(item, specification["fields"], path)
        elif kind == "array":
            if not isinstance(item, list):
                raise ValueError(f"{path} must be a list")
            bounded_int(len(item), specification["minimum_items"], specification["maximum_items"], f"{path} length")
            for index, row in enumerate(item):
                validate_fields(row, specification["fields"], f"{path}[{index}]")
        elif kind == "int-array":
            if not isinstance(item, list):
                raise ValueError(f"{path} must be a list of integers")
            bounded_int(len(item), specification["minimum_items"], specification["maximum_items"], f"{path} length")
            for index, number in enumerate(item):
                bounded_int(number, specification["minimum"], specification["maximum"], f"{path}[{index}]")
        elif kind == "int":
            bounded_int(item, specification["minimum"], specification["maximum"], path)
        elif kind == "bool":
            if type(item) is not bool:
                raise ValueError(f"{path} must be boolean")
        elif kind == "str":
            if (not isinstance(item, str) or not specification.get("min_length", 0) <= len(item) <= specification["max_length"] or
                    any(not 32 <= ord(character) <= 126 for character in item)):
                raise ValueError(f"{path} requires bounded printable ASCII text")
            if "pattern" in specification and not re.fullmatch(specification["pattern"], item):
                raise ValueError(f"{path} has an invalid semantic identity")
        else:
            raise ValueError(f"Unsupported catalog field type at {path}: {kind}")
        if "values" in specification and item not in specification["values"]:
            raise ValueError(f"{path} must be one of {specification['values']}")


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
    bounded_int(scenario["seed"], 0, 0, "seed (controlled pilot samples use the fixed seed zero)")
    context = scenario["context"]
    exact_fields(context, {"target", "selection", "caller", "return_to"}, "context")
    entry = entry_for(context["target"])
    if context["target"] != entry["id"]:
        raise ValueError("Scenario context.target requires a qualified stable source identity")
    contract = entry.get("scenario_contract")
    if entry["availability"] not in ("runtime-full", "presenter-isolated", "profile-conditional") or not isinstance(contract, dict):
        raise ValueError(f"Native Studio preparation is unsupported: {context['target']!r}")
    if entry["availability"] == "profile-conditional":
        required = entry.get("required_capabilities")
        if not isinstance(required, list) or not required or any(not isinstance(name, str) or not name for name in required):
            raise ValueError("A profile-conditional scenario requires explicit native capability names")
    if scenario["variant"] not in entry["variants"]:
        raise ValueError(f"Unsupported variant for {entry['id']}: {scenario['variant']!r}")
    contract = scenario_contract_for(entry, scenario["variant"])
    if any(context[key] != contract["context"][key] for key in ("selection", "caller", "return_to")):
        raise ValueError(f"{inventory_label(entry)} requires its declared selection and caller/return context")
    validate_fields(scenario["page"], contract["page"], "page")
    validate_fields(scenario["services"], contract["services"], "services")
    if entry["native_target"] in ("cody.settings.quiet-edit", "cody.settings.quiet-hours"):
        page = scenario["page"]
        if page["quiet_hours_enabled"] and page["quiet_start_minute"] == page["quiet_end_minute"]:
            raise ValueError("Enabled quiet hours require distinct saved start and end minutes")
    if entry["native_target"] in ("cody.codex.hub", "cody.codex.chats", "cody.codex.turns", "cody.codex.calls",
                                 "cody.codex.active", "cody.codex.questions"):
        domain = scenario["services"]["domain"]
        questions = domain.get("questions", [])
        if domain.get("questions_outcome") != "supported" and questions:
            raise ValueError("Unavailable Codex questions cannot carry populated rows")
        question_ids = set()
        for question in questions:
            if (question["id"] in question_ids or question["chat_id"] != domain["chat_id"] or
                    question["turn_id"] != domain["turn_id"]):
                raise ValueError("Codex questions require unique IDs and the exact selected chat and turn")
            question_ids.add(question["id"])
        if any(scenario["page"][key] != domain[key] for key in ("source_id", "chat_id", "turn_id")):
            raise ValueError("Codex query scope must match its source, chat and turn fixture")
        identities = set()
        definitions = {}
        for call in domain["calls"]:
            if call["id"] in identities or call["turn_id"] != domain["turn_id"]:
                raise ValueError("Codex calls require unique IDs and the exact selected turn")
            identities.add(call["id"])
            definition = (call["category"], call["label"])
            if call["tool_name"] in definitions and definitions[call["tool_name"]] != definition:
                raise ValueError("Codex repeated tool names require the same category and display label")
            definitions[call["tool_name"]] = definition
            expected = {"queued": "unknown", "running": "unknown", "failed": "failed", "cancelled": "cancelled"}
            if call["state"] in expected and call["outcome"] != expected[call["state"]]:
                raise ValueError("Codex call state and outcome contradict one another")
    exact_fields(scenario["time"], {"mode", "initial_ms", "advances_ms", "capture_at_ms"}, "time")
    timing = scenario["time"]
    if (type(timing["initial_ms"]) is not int or not isinstance(timing["advances_ms"], list) or
            not isinstance(timing["capture_at_ms"], list) or
            any(type(value) is not int for value in timing["advances_ms"] + timing["capture_at_ms"])):
        raise ValueError("Scenario time requires explicit integer advances and capture instants")
    if timing["mode"] == "controlled-static":
        if timing != {"mode": "controlled-static", "initial_ms": 0, "advances_ms": [], "capture_at_ms": [0]}:
            raise ValueError("A controlled static sample requires t=0 without advances")
    elif timing["mode"] == "controlled-worker":
        capability = entry.get("worker_replay")
        if (not isinstance(capability, dict) or capability.get("clock") != "display-worker" or
                type(capability.get("max_ms")) is not int or type(capability.get("step_ms")) is not int or
                capability["max_ms"] < 0 or capability["step_ms"] <= 0):
            raise ValueError("Controlled worker replay is unavailable for this Studio destination")
        maximum, step = capability["max_ms"], capability["step_ms"]
        advances, captures = timing["advances_ms"], timing["capture_at_ms"]
        if (timing["initial_ms"] != 0 or len(advances) > 256 or not 1 <= len(captures) <= 257 or
                any(value <= 0 or value % step for value in advances) or
                any(value < 0 or value > maximum or value % step for value in captures) or
                any(left >= right for left, right in zip(captures, captures[1:])) or
                scenario["page"].get("sample_at_ms") != 0):
            raise ValueError("Invalid controlled-worker timeline or initial native source frame")
        boundaries, elapsed = {0}, 0
        for advance in advances:
            elapsed += advance
            if elapsed > maximum:
                raise ValueError("Controlled-worker advances exceed the supported duration")
            boundaries.add(elapsed)
        if captures[-1] != elapsed or any(value not in boundaries for value in captures):
            raise ValueError("Worker captures must be declared forward-advance boundaries through the final instant")
    else:
        raise ValueError("Unsupported Studio clock mode")
    common = scenario["common"]
    exact_fields(common, {"settings", "observations"}, "common")
    settings = common["settings"]
    standard = {"brightness_percent", "auto_rotate", "orientation_degrees"}
    audio_fields = {"sounds_enabled", "playback_volume"}
    present_audio = audio_fields.intersection(settings) if isinstance(settings, dict) else set()
    if present_audio and present_audio != audio_fields:
        raise ValueError("common.settings requires both sounds_enabled and playback_volume")
    exact_fields(settings, standard | set(contract["settings"]) | present_audio, "common.settings")
    if present_audio:
        if type(settings["sounds_enabled"]) is not bool:
            raise ValueError("sounds_enabled must be boolean")
        bounded_int(settings["playback_volume"], 0, 100, "playback_volume")
    validate_fields({key: settings[key] for key in contract["settings"]}, contract["settings"], "common.settings")
    bounded_int(settings["brightness_percent"], 10, 100, "brightness_percent")
    if type(settings["auto_rotate"]) is not bool:
        raise ValueError("auto_rotate must be boolean")
    if type(settings["orientation_degrees"]) is not int or settings["orientation_degrees"] not in (0, 180):
        raise ValueError("orientation_degrees supports only 0 or 180 in this host profile")
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
    if (entry["native_target"] in ("cody.dashboard.reset", "cody.dashboard.limits") and
            not network["connected"]):
        raise ValueError("Current statistics require connected synthetic Wi-Fi and scripted Codex READY; historical states are not supported by this pilot")
    clock = observations["clock"]
    exact_fields(clock, {"epoch_seconds", "utc_offset_minutes"}, "clock")
    bounded_int(clock["epoch_seconds"], 1704067200, 4102444799, "clock.epoch_seconds")
    bounded_int(clock["utc_offset_minutes"], -840, 840, "clock.utc_offset_minutes")
    return deepcopy(scenario)


# path: Scenario JSON path, read without creating files or native state.
# Raises I/O, JSON or contract errors with no fallback to a different scenario.
def load_scenario(path):
    return validate_scenario(json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=unique_object))


# identity: Explicit inventory ID or stable source identity; P04 remains the default.
# variant: Optional declared variant; None preserves the entry's original fixture.
# Raises ValueError for unknown variants, unsafe paths or mismatched fixture identities.
# Raises the original I/O, JSON or schema error if the selected fixture is invalid.
def default_scenario(identity="P04", *, variant=None):
    entry = entry_for(identity)
    filename = entry.get("scenario")
    if variant is not None:
        scenario_contract_for(entry, variant)
        filename = entry.get("variant_scenarios", {}).get(variant, filename)
    if not isinstance(filename, str):
        raise ValueError(f"No native Studio scenario for {identity!r}")
    directory = DEFAULT_PATH.parent.resolve()
    path = (directory / filename).resolve(strict=True)
    if not path.is_relative_to(directory):
        raise ValueError("Studio scenario fixture escaped the source scenario directory")
    scenario = load_scenario(path)
    if scenario["context"]["target"] != entry["id"]:
        raise ValueError(f"Studio scenario fixture targets a different source: {identity!r}")
    if variant is not None and scenario["variant"] != variant:
        raise ValueError(f"Studio scenario fixture does not match variant: {identity!r}/{variant}")
    return scenario


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
