"""Inspect native source-animation frames with an isolated scenario per sample.

The native monotonic clock remains frozen. These are authored renderer samples,
not playback of boot workers, service startup, wall-clock time or sound.
"""

from copy import deepcopy
from dataclasses import dataclass

from cody_studio_catalog import entry_for
from cody_studio_scenario import scenario_revision, validate_scenario


@dataclass(frozen=True)
class TimelinePlan:
    """Native frame boundaries and the exact synthetic scenario they belong to."""

    target: str
    duration_ms: int
    keyframes_ms: tuple
    scenario_revision: str
    rendering_mode: str
    limitation: str
    clock_mode: str = "source-animation"


# runner: Existing serialized Studio runner whose native build owns the frames.
# scenario: Complete synthetic input retained as the timeline's base revision.
# Raises ValueError for unsupported, stale or inconsistent native frame boundaries.
def timeline_plan(runner, scenario):
    from cody_studio_runner import verify_current_build

    value = validate_scenario(scenario)
    entry = entry_for(value["context"]["target"])
    field = entry["scenario_contract"]["page"].get("sample_at_ms")
    if not isinstance(field, dict) or field.get("type") != "int":
        raise ValueError("Native source animation is unavailable for this Studio destination")
    build = verify_current_build()
    if getattr(runner.device.api, "_cody_library_sha256", None) != build["library_sha256"]:
        raise ValueError("A different native build is loaded in Studio; restart this session")
    duration, frames = runner.device.studio_animation_keyframes(entry["native_target"])
    if (type(duration) is not int or duration != field["maximum"] + 1 or
            not isinstance(frames, tuple) or not frames or frames[0] != 0 or
            any(type(sample) is not int or not 0 <= sample < duration for sample in frames) or
            any(left >= right for left, right in zip(frames, frames[1:]))):
        raise ValueError("Native animation frame boundaries disagree with the Studio contract")
    if value["time"]["mode"] == "controlled-worker":
        requested = tuple(value["time"]["capture_at_ms"])
        return TimelinePlan(entry["id"], duration, requested, scenario_revision(value), entry["rendering_mode"],
                            "Fresh-process native display-worker replay; other workers and audio remain uncontrolled",
                            "display-worker")
    return TimelinePlan(entry["id"], duration, frames, scenario_revision(value), entry["rendering_mode"],
                        "Native source-frame samples; monotonic worker replay and audio are unavailable")


# runner: Native Studio runner used to query compiled source-frame boundaries.
# scenario: Validated source-frame or existing worker scenario.
# Returns a complete immutable worker sequence from the compiled frame boundaries.
# Raises validation, unsupported profile, provenance or native metadata errors.
def worker_timeline_scenario(runner, scenario):
    value = validate_scenario(scenario)
    entry = entry_for(value["context"]["target"])
    if not isinstance(entry.get("worker_replay"), dict):
        raise ValueError("Native display-worker replay is unavailable for this destination")
    if value["time"]["mode"] == "controlled-worker":
        return value
    plan = timeline_plan(runner, value)
    frames = list(plan.keyframes_ms)
    if not frames or frames[0] != 0:
        raise ValueError("Native animation has no initial keyframe")
    value["page"]["sample_at_ms"] = 0
    value["time"] = {"mode": "controlled-worker", "initial_ms": 0,
                     "advances_ms": [right - left for left, right in zip(frames, frames[1:])],
                     "capture_at_ms": frames}
    return validate_scenario(value)


# scenario: Validated worker or source-frame scenario.
# Returns a controlled static scenario at source sample zero for the same destination.
# Raises scenario errors without changing the input object.
def source_timeline_scenario(scenario):
    value = validate_scenario(scenario)
    if "sample_at_ms" not in value["page"]:
        raise ValueError("Native source animation is unavailable for this Studio destination")
    value["page"]["sample_at_ms"] = 0
    value["time"] = {"mode": "controlled-static", "initial_ms": 0, "advances_ms": [], "capture_at_ms": [0]}
    return validate_scenario(value)


# runner: Existing serialized Studio runner; owns all native preparation and capture.
# scenario: Immutable base scenario for the sequence, including fixed observations.
# at_ms: Source-animation instant from zero through duration minus one millisecond.
# Raises native, scenario, time, provenance or filesystem failures without fallback.
def timeline_capture(runner, scenario, at_ms):
    value = validate_scenario(scenario)
    plan = timeline_plan(runner, value)
    if type(at_ms) is not int or not 0 <= at_ms < plan.duration_ms:
        raise ValueError("Source-animation sample lies outside the native animation duration")
    if value["time"]["mode"] == "controlled-worker":
        if at_ms not in value["time"]["capture_at_ms"]:
            raise ValueError("Worker sample is not a declared capture boundary")
        runner.prepare(value, requested_at_ms=at_ms)
        return runner.capture(timeline_base=value)
    sample = deepcopy(value)
    sample["page"]["sample_at_ms"] = at_ms
    # Native prepare resets its owner state for every static source sample. It
    # never moves a running worker's clock backwards or simulates its lifecycle.
    runner.prepare(sample)
    return runner.capture(timeline_base=value)
