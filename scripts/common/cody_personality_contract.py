"""Strict authoring contract for Cody's single-value MVP mood rules."""

from pathlib import Path
import yaml

from scripts.common.cody_rig_contract import RigLoader


# mapping: Authored section whose field set must be exact.
# expected: Supported named fields, independent of YAML order.
# label: Human-readable schema location.
# Raises ValueError for missing, unknown or incorrectly typed sections.
def fields(mapping, expected, label):
    if not isinstance(mapping, dict) or set(mapping) != set(expected):
        raise ValueError(f"{label}: requires exactly {sorted(expected)}")


# value: Authored integer, never silently corrected.
# minimum: Inclusive supported lower bound.
# maximum: Inclusive supported upper bound.
# label: Named authoring field used in errors.
# Raises ValueError for booleans, nonintegers or unsupported values.
def integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{label}: expected integer {minimum}..{maximum}, got {value!r}")
    return value


# path: Canonical personality authoring YAML.
# Raises OSError for unreadable source or ValueError for invalid/ambiguous authoring.
def load_personality_rules(path):
    rules = yaml.load(Path(path).read_text(encoding="utf-8"), Loader=RigLoader)
    fields(rules, ("schema", "scope", "face_binding", "happiness", "sadness"), "personality")
    if rules["schema"] != "cody.personality.v1":
        raise ValueError("Unsupported personality schema")
    if rules["scope"] != "session" or rules["face_binding"] != "disabled":
        raise ValueError("MVP personality requires session scope and disabled face binding")
    happiness = rules["happiness"]
    fields(happiness, ("minimum", "maximum", "initial", "on_caress", "decay"), "happiness")
    integer(happiness["minimum"], 0, 0, "happiness.minimum")
    integer(happiness["maximum"], 100, 100, "happiness.maximum")
    integer(happiness["initial"], 0, 100, "happiness.initial")
    fields(happiness["on_caress"], ("add",), "happiness.on_caress")
    integer(happiness["on_caress"]["add"], 1, 100, "happiness.on_caress.add")
    decay = happiness["decay"]
    fields(decay, ("subtract", "every_seconds", "clock"), "happiness.decay")
    integer(decay["subtract"], 1, 100, "happiness.decay.subtract")
    integer(decay["every_seconds"], 1, 86400, "happiness.decay.every_seconds")
    if decay["clock"] != "awake":
        raise ValueError("MVP happiness decay requires the awake clock")
    fields(rules["sadness"], ("rule",), "sadness")
    if rules["sadness"]["rule"] != "complement_of_happiness":
        raise ValueError("Sadness must be derived as maximum minus happiness")
    return rules
