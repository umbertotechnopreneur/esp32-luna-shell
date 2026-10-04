"""Native-fixture comparison for the host-only Cody personality presentation."""

import math
from pathlib import Path
import sys


# root: Checkout containing the generated native rig and the host personality implementation.
# fixture: Parsed native JSON with sequential samples grouped by stable scenario identity.
# Raises AssertionError for malformed fixtures or a native/host presentation mismatch.
# Raises OSError or ValueError when the canonical authoring/assets cannot be loaded.
def compare_personality_fixture(root, fixture):
    repository = Path(root).resolve()
    for directory in (repository, repository / "simulator/windows"):
        path = str(directory)
        if path not in sys.path:
            sys.path.insert(0, path)
    from scripts.common.cody_rig_contract import evaluate, load_contract
    from cody_personality import PersonalityCue, PersonalityPainter, PersonalityPresentation

    assert isinstance(fixture, dict), "Personality fixture must be a JSON object"
    samples = fixture.get("samples")
    assert isinstance(samples, list) and samples, "Personality fixture must contain samples"
    rig, _ = load_contract(repository)
    values = [channel["default"] for channel in rig["channels"]]
    placement = rig["default_transform"]
    points = {point["name"]: point for point in rig["points"]}
    rest_lobe_span = (points["lobe.upper_right"]["position"][0] -
                      points["lobe.upper_left"]["position"][0])
    exclusions = [(106, 37, 362, 58), (10, 203, 70, 263)]
    required = {"cue", "opacity", "status", "left", "top", "right", "bottom", "phase", "desired",
                "x", "y", "width", "height"}
    geometry_fields = {"x", "y", "width", "height"}
    current_scenario = None
    seen_scenarios = set()
    sample_names = set()
    painter = None
    elapsed_us = 0
    for index, sample in enumerate(samples):
        assert isinstance(sample, dict), f"Personality sample {index} must be an object"
        scenario = sample.get("scenario")
        name = sample.get("name")
        assert isinstance(scenario, str) and scenario, f"Sample {index} needs a scenario identity"
        assert isinstance(name, str) and name, f"Sample {index} needs a sample identity"
        identity = (scenario, name)
        assert identity not in sample_names, f"Duplicate personality sample: {identity}"
        sample_names.add(identity)
        if scenario != current_scenario:
            assert scenario not in seen_scenarios, f"Noncontiguous personality scenario: {scenario}"
            seen_scenarios.add(scenario)
            current_scenario = scenario
            painter = PersonalityPainter(repository, rig)
            elapsed_us = 0
        assert not sample.get("reset", False), f"{identity}: use a new scenario for an explicit reset"
        delta_us = sample["dt_us"]
        paused = sample["paused"]
        assert type(delta_us) is int and delta_us >= 0, f"{identity}: invalid active delta"
        assert type(paused) is bool, f"{identity}: paused must be Boolean"
        if not paused:
            elapsed_us += delta_us
        scale = sample["scale"]
        scene = evaluate(rig, values, [placement["x"], placement["y"], scale])
        nose_x, nose_y = scene["world"]["nose"]
        scene["world"]["nose"] = (nose_x + sample["target_dx"], nose_y + sample["target_dy"])
        upper_right_x, upper_right_y = scene["world"]["lobe.upper_right"]
        scene["world"]["lobe.upper_right"] = (
            upper_right_x + sample["target_squash"] * rest_lobe_span * scene["scale"], upper_right_y)
        # Only explicit world anchors move; native vector body clearance remains unchanged.
        frame = painter.prepare(
            scene, PersonalityCue(sample["cue"]), scale, "rig", sample.get("rotation", 0),
            elapsed_us / 1000, exclusions,
            presentation=PersonalityPresentation(sample["presentation"]), paused=paused)
        actual = {
            "cue": int(frame.cue), "opacity": frame.opacity, "status": int(frame.status),
            "left": frame.left, "top": frame.top,
            "right": frame.left + frame.width, "bottom": frame.top + frame.height,
            "phase": int(painter.channel.phase), "desired": int(painter.channel.desired),
            "x": painter.lag.x, "y": painter.lag.y,
            "width": painter.lag.width, "height": painter.lag.height,
            "active_elapsed_us": painter.active_elapsed_us,
            "last_admitted_at_us": [painter.last_admitted_at_us[cue]
                for cue in sorted(PersonalityCue, key=int) if cue != PersonalityCue.NONE],
        }
        expected = sample["expected"]
        assert isinstance(expected, dict) and required <= expected.keys(), (
            f"{identity}: incomplete native expectation")
        compared_fields = required | ({"active_elapsed_us", "last_admitted_at_us"} & expected.keys())
        for field in sorted(compared_fields):
            observed = actual[field]
            wanted = expected[field]
            if field in geometry_fields:
                assert math.isfinite(observed) and math.isfinite(wanted), (
                    f"{identity}: nonfinite {field}: host={observed}, native={wanted}")
                assert math.isclose(observed, wanted, rel_tol=0, abs_tol=.003), (
                    f"{identity}: {field}: host={observed:.9f}, native={wanted:.9f}")
            else:
                assert observed == wanted, f"{identity}: {field}: host={observed}, native={wanted}"
    return len(samples)
