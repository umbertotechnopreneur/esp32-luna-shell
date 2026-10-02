"""Shared authoring contract for the Cody rig and its host preview; no device I/O."""

from __future__ import annotations

import json
import math
from pathlib import Path
import yaml

ROOT = Path(__file__).resolve().parents[1]


class RigLoader(yaml.SafeLoader):
    """Safe YAML with duplicate-key rejection for authored character configuration."""


# loader is the safe YAML parser; node is one mapping; deep controls nested construction.
# Raises ValueError on duplicate keys, including ambiguous style roles.
def yaml_mapping(loader, node, deep=False):
    return unique_object((loader.construct_object(key, deep=deep), loader.construct_object(value, deep=deep)) for key, value in node.value)


RigLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, yaml_mapping)


# pairs contains the original JSON key/value sequence.
# Raises ValueError for an ambiguous duplicate key.
def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


# path identifies the authoring document to read without repairing its content.
# Raises OSError or ValueError on missing, unreadable or malformed data.
def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=unique_object)


# value is the required finite number; label supplies useful rejection context.
# Raises ValueError for booleans, non-numeric or non-finite values.
def finite(value, label):
    if type(value) not in (float, int) or not math.isfinite(value):
        raise ValueError(f"{label}: expected finite number, got {value!r}")
    return float(value)


# root identifies the checkout containing the versioned artwork and palette.
# Raises ValueError or OSError when the explicit asset contract is inconsistent.
def load_contract(root=ROOT):
    rig = yaml.load((root / "assets/cody/cody.yaml").read_text(encoding="utf-8"), Loader=RigLoader)
    if rig["character"]["id"] != "cody" or rig["character"]["facing"] != "front" or not isinstance(rig["character"]["name"], str) or not rig["character"]["name"].strip():
        raise ValueError("Cody requires a nonempty name, stable id and front-facing rig")
    if rig["palette_file"] != "assets/cody/palette/palette256.json":
        raise ValueError("The PoC uses the versioned shared palette file")
    palette = read_json(root / rig["palette_file"])
    for role, slots in rig["style"]["paints"].items():
        if not isinstance(role, str) or not role or len(slots) != 2 or any(type(v) is not int or not 0 <= v < 172 for v in slots) or slots[0] > slots[1]:
            raise ValueError(f"Invalid paint role: {role}")
    if set(rig["style"]["debug"]) != {"anchor", "knot", "handle", "link", "selected"}:
        raise ValueError("All five debug color roles are required")
    for value in list(rig["style"]["debug"].values()) + [rig["style"]["back"]["ink"]]:
        if type(value) is not int or not 0 <= value < 172:
            raise ValueError(f"Invalid debug/navigation palette slot: {value}")
    back_slots = rig["style"]["back"]["fill"]
    if len(back_slots) != 2 or any(type(v) is not int or not 0 <= v < 172 for v in back_slots) or back_slots[0] > back_slots[1]:
        raise ValueError("Invalid Back gradient")
    font = rig["style"]["host_font"]
    if not isinstance(font["family"], str) or not font["family"] or type(font["size"]) is not int or not 6 <= font["size"] <= 24:
        raise ValueError("Invalid host font configuration")
    for path in rig["paths"]:
        if path["paint"] not in rig["style"]["paints"]:
            raise ValueError(f"Unknown paint: {path['name']}")
        path["paint"] = rig["style"]["paints"][path["paint"]]
    if rig["schema"] != "cody.rig.v1" or rig["subdivisions"] != 12:
        raise ValueError("Unsupported rig schema or subdivision count")
    if not 1 <= len(rig["points"]) <= 192 or not 1 <= len(rig["curves"]) <= 64 or not 1 <= len(rig["paths"]) <= 16:
        raise ValueError("Rig exceeds the bounded firmware geometry capacity")
    channel_names = [item["name"] for item in rig["channels"]]
    if channel_names != ["eye_left", "eye_right", "brow_left", "brow_right", "mouth_open", "breath",
                         "mouth_width", "nose_size", "cheek_size", "cheek_opacity"]:
        raise ValueError("Unexpected facial channel order")
    for channel in rig["channels"]:
        lower, upper, default = [finite(channel[key], channel["name"]) for key in ("minimum", "maximum", "default")]
        if not lower <= default <= upper or lower >= upper:
            raise ValueError(f"Invalid channel bounds: {channel['name']}")
        expected_range = {'brow_left': (-1, 1), 'brow_right': (-1, 1), 'breath': (-1, 1),
                          'mouth_width': (.5, 1.6), 'nose_size': (.5, 1.6), 'cheek_size': (.4, 1.8)}.get(channel['name'], (0, 1))
        if (lower, upper) != expected_range:
            raise ValueError(f"{channel['name']}: semantic range must remain {expected_range}")
    names = [point["name"] for point in rig["points"]]
    if len(set(names)) != len(names) or names[0] != "center":
        raise ValueError("Rig points require unique stable names and a leading center")
    for index, point in enumerate(rig["points"]):
        if point["role"] not in ("anchor", "knot", "handle"):
            raise ValueError(f"Invalid point role: {point['name']}")
        parent = point["parent"]
        if (index == 0 and parent is not None) or (index != 0 and parent not in names[:index]):
            raise ValueError(f"Parent must precede its child: {point['name']}")
        for field in ("position", "delta"):
            if len(point[field]) != 2 or any(abs(finite(v, point["name"])) > 2 for v in point[field]):
                raise ValueError(f"Invalid normalized {field}: {point['name']}")
        if point["channel"] is not None and point["channel"] not in channel_names:
            raise ValueError(f"Unknown point channel: {point['name']}")
    curve_names = [curve["name"] for curve in rig["curves"]]
    if len(set(curve_names)) != len(curve_names):
        raise ValueError("Duplicate curve name")
    for curve in rig["curves"]:
        if len(curve["points"]) != 4 or any(name not in names for name in curve["points"]):
            raise ValueError(f"Invalid cubic: {curve['name']}")
    next_curve = 0
    path_names = set()
    for path in rig["paths"]:
        if path["name"] in path_names or type(path["first_curve"]) is not int or type(path["curve_count"]) is not int or path["first_curve"] != next_curve or not 1 <= path["curve_count"] <= 21:
            raise ValueError(f"Invalid path extent or name: {path['name']}")
        path_names.add(path["name"])
        next_curve += path["curve_count"]
        segments = rig["curves"][path["first_curve"]:next_curve]
        if len(segments) != path["curve_count"] or any(a["points"][3] != b["points"][0] for a, b in zip(segments, segments[1:])):
            raise ValueError(f"Disconnected path: {path['name']}")
        width = finite(path["stroke"], path["name"])
        if not 0 <= width <= .1 or (width == 0 and segments[-1]["points"][3] != segments[0]["points"][0]):
            raise ValueError(f"Invalid stroke or open fill: {path['name']}")
        if path["visible"] is not None and path["visible"] not in channel_names:
            raise ValueError(f"Unknown visibility channel: {path['name']}")
        if len(path["paint"]) != 2 or any(type(slot) is not int or not 0 <= slot < 172 for slot in path["paint"]):
            raise ValueError(f"Invalid palette range: {path['name']}")
        if path["paint"][0] > path["paint"][1]:
            raise ValueError(f"Reversed palette range: {path['name']}")
    if next_curve != len(rig["curves"]):
        raise ValueError("Unowned rig curves")
    owned_paths = []
    for name, entity in rig["entities"].items():
        if not name or not isinstance(entity["label"], str) or not entity["label"].strip() or entity["anchor"] not in names or not entity["paths"]:
            raise ValueError(f"Invalid entity: {name}")
        owned_paths.extend(entity["paths"])
    if len(owned_paths) != len(set(owned_paths)) or set(owned_paths) != path_names:
        raise ValueError("Each drawn path must belong to exactly one named entity")
    validate_face_definition(rig)
    validate_contour_definition(rig)
    if len(palette["entries"]) != 256 or [item["index"] for item in palette["entries"]] != list(range(256)):
        raise ValueError("Palette must contain all 256 ordered slots")
    for entry in palette["entries"]:
        rgb = entry["rgb888"].removeprefix("#")
        if len(rgb) != 6:
            raise ValueError(f"Invalid palette color: {entry['index']}")
        red, green, blue = (int(rgb[i:i+2], 16) for i in (0, 2, 4))
        if int(entry["rgb565"], 16) != ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3):
            raise ValueError(f"RGB565 mismatch: {entry['index']}")
    if set(rig["boot_channels"]) != {"sleep", "waking", "ready"}:
        raise ValueError("Three boot channel previews are required")
    for values in rig["boot_channels"].values():
        validate_channels(rig, values)
    for name, values in rig["expression_presets"].items():
        if not isinstance(name, str) or not name.strip():
            raise ValueError("Expression presets require nonempty names")
        validate_channels(rig, values)
    placement = rig["default_transform"]
    x, y, scale = [finite(placement[key], "default_transform") for key in ("x", "y", "scale")]
    if not 0 <= x < 466 or not 0 <= y < 466 or not 20 <= scale <= 200:
        raise ValueError("Invalid authored default transform")
    return rig, palette


# rig supplies the already checked point/path graph and declarative facial bindings.
# Raises ValueError for ambiguous transforms, unsafe anchors or unsupported opacity bindings.
def validate_face_definition(rig):
    if not .05 <= finite(rig["face_transition_seconds"], "face_transition_seconds") <= 5:
        raise ValueError("Face transition time constant must be between 0.05 and 5 seconds")
    expected = {("mouth", "mouth_width", ("x",)), ("nose", "nose_size", ("x", "y")),
                ("cheek.left", "cheek_size", ("x", "y")), ("cheek.right", "cheek_size", ("x", "y"))}
    bindings = rig["face_transforms"]
    if len(bindings) != len(expected) or {(item["anchor"], item["channel"], tuple(item["axes"])) for item in bindings} != expected:
        raise ValueError("Face transforms must explicitly bind mouth width, nose size and both cheek sizes")
    points = {point["name"]: point for point in rig["points"]}
    anchors = {item["anchor"]: item for item in bindings}
    for name in anchors:
        if name not in points or points[name]["role"] != "anchor" or points[name]["parent"] != "center":
            raise ValueError(f"Face transform requires a fixed direct root anchor: {name}")
    rig["point_face_transforms"] = {}
    for point in rig["points"]:
        ancestor = point["parent"]
        binding = None
        while ancestor is not None:
            if ancestor in anchors:
                if binding is not None:
                    raise ValueError(f"Overlapping facial transform bindings: {point['name']}")
                binding = anchors[ancestor]
            ancestor = points[ancestor]["parent"]
        rig["point_face_transforms"][point["name"]] = tuple(
            binding["channel"] if binding is not None and axis in binding["axes"] else None for axis in ("x", "y"))
    if rig["path_opacity"] != {"cheek.left": "cheek_opacity", "cheek.right": "cheek_opacity"}:
        raise ValueError("Only the two cheek fills have an authored opacity channel")
    owned_points = {anchor: set() for anchor in anchors}
    for path in rig["paths"]:
        opacity_channel = rig["path_opacity"].get(path["name"])
        if opacity_channel is not None and path["stroke"] != 0:
            raise ValueError(f"Opacity is supported by filled paths only: {path['name']}")
        path["opacity_channel"] = opacity_channel
        curves = rig["curves"][path["first_curve"]:path["first_curve"] + path["curve_count"]]
        if path["name"] in owned_points:
            owned_points[path["name"]].update(name for curve in curves for name in curve["points"])
    for anchor, path_points in owned_points.items():
        expected_channel = anchors[anchor]["channel"]
        if not path_points or any(expected_channel not in rig["point_face_transforms"][name] for name in path_points):
            raise ValueError(f"All facial path points must descend from their stable anchor: {anchor}")


# rig supplies authored outline metadata and the already checked parent/path graph.
# Raises ValueError when contour controls could move a facial point or exceed PoC bounds.
def validate_contour_definition(rig):
    contour = rig["contour"]
    expected = {"fullness": (-1, 1), "amplitude": (0, 1), "speed": (0, 2), "phase_spread": (0, 1)}
    if [item["name"] for item in contour["controls"]] != list(expected):
        raise ValueError("Contour controls must be fullness, amplitude, speed and phase_spread")
    for control in contour["controls"]:
        bounds = tuple(finite(control[key], control["name"]) for key in ("minimum", "maximum"))
        if bounds != expected[control["name"]] or not bounds[0] <= finite(control["default"], control["name"]) <= bounds[1]:
            raise ValueError(f"Invalid contour range/default: {control['name']}")
        if not isinstance(control["label"], str) or not control["label"].strip():
            raise ValueError(f"Missing contour label: {control['name']}")
    if not .05 <= finite(contour["transition_seconds"], "contour.transition_seconds") <= 5:
        raise ValueError("Contour transition time constant must be between 0.05 and 5 seconds")
    body = next((path for path in rig["paths"] if path["name"] == "body"), None)
    if body is None or body["stroke"] != 0 or body["curve_count"] != 11:
        raise ValueError("Contour requires the eleven-segment filled body path")
    curves = rig["curves"][body["first_curve"]:body["first_curve"] + body["curve_count"]]
    lobe_names = [curve["points"][0] for curve in curves]
    if [lobe["point"] for lobe in contour["lobes"]] != lobe_names or len(set(lobe_names)) != 11:
        raise ValueError("Contour lobes must follow the eleven unique body knots in perimeter order")
    points = {point["name"]: point for point in rig["points"]}
    body_points = {name for curve in curves for name in curve["points"]}
    for curve in curves:
        for handle, anchor in ((curve["points"][1], curve["points"][0]), (curve["points"][2], curve["points"][3])):
            if points[handle]["role"] != "handle" or points[handle]["parent"] != anchor:
                raise ValueError(f"Contour handle must follow its own lobe: {handle}")
    for name in body_points:
        if points[name]["channel"] is not None:
            raise ValueError(f"Contour point cannot use a facial channel: {name}")
    for point in rig["points"]:
        ancestor = point["parent"]
        while ancestor is not None:
            if ancestor in lobe_names and point["name"] not in body_points:
                raise ValueError(f"Facial geometry cannot descend from a contour lobe: {point['name']}")
            ancestor = points[ancestor]["parent"]
    previous_phase = -1
    for lobe in contour["lobes"]:
        point = points[lobe["point"]]
        if point["role"] != "anchor" or point["parent"] != "center":
            raise ValueError(f"Contour lobe must be a direct root anchor: {lobe['point']}")
        if len(lobe["direction"]) != 2:
            raise ValueError(f"Contour direction needs two coordinates: {lobe['point']}")
        dx, dy = (finite(value, lobe["point"]) for value in lobe["direction"])
        if abs(dx*dx + dy*dy - 1) > 1e-6:
            raise ValueError(f"Contour direction must have unit length: {lobe['point']}")
        fullness = finite(lobe["fullness"], lobe["point"])
        wave = finite(lobe["wave"], lobe["point"])
        # The tiny lower-left and foot curves permit less travel than the large crown.
        limit = .03 if lobe["point"] in ("lobe.foot_left", "lobe.foot_right", "lobe.lower_left") else .12
        if not 0 <= fullness <= .06 or not 0 <= wave <= .06 or fullness + wave > limit:
            raise ValueError(f"Contour displacement exceeds the authored PoC bound: {lobe['point']}")
        phase = finite(lobe["phase_turns"], lobe["point"])
        if not 0 <= phase < 1 or phase <= previous_phase:
            raise ValueError(f"Contour phase offsets must increase around the perimeter: {lobe['point']}")
        previous_phase = phase


# rig supplies the independent shape controls and their authored default values.
def contour_defaults(rig):
    return {control["name"]: control["default"] for control in rig["contour"]["controls"]}


# rig supplies shape defaults; each new preview starts at a stable zero phase.
def make_contour_state(rig):
    return dict(contour_defaults(rig), phase=0.0)


# rig supplies allowed control names and ranges.
# values is a flat control dictionary, with phase only for an evaluated state.
# state distinguishes a runtime state from a target control dictionary.
# Raises ValueError for missing/extra fields or non-finite/out-of-range values.
def validate_contour_values(rig, values, state=False):
    keys = {control["name"] for control in rig["contour"]["controls"]}
    if state:
        keys.add("phase")
    if not isinstance(values, dict) or set(values) != keys:
        raise ValueError(f"Contour requires exactly these fields: {sorted(keys)}")
    for control in rig["contour"]["controls"]:
        value = finite(values[control["name"]], f"contour.{control['name']}")
        if not control["minimum"] <= value <= control["maximum"]:
            raise ValueError(f"Contour {control['name']}: rejected value {value}")
    if state and not 0 <= finite(values["phase"], "contour.phase") < math.tau:
        raise ValueError(f"Contour phase must be in [0, 2*pi): {values['phase']}")


# rig supplies the smoothing time constant and authored control ranges.
# state is the previous flat state; it is never mutated.
# target is the requested four-control dictionary without phase.
# delta_ms is nonnegative active elapsed time; zero explicitly pauses all contour motion.
# Raises ValueError for rejected controls, phase, timing or arithmetic overflow.
def advance_contour(rig, state, target, delta_ms):
    validate_contour_values(rig, state, state=True)
    validate_contour_values(rig, target)
    delta_seconds = finite(delta_ms, "contour.delta_ms") / 1000
    if delta_seconds < 0:
        raise ValueError("Contour time cannot move backwards")
    if delta_seconds == 0:
        return dict(state)
    time_constant = finite(rig["contour"]["transition_seconds"], "contour.transition_seconds")
    if not .05 <= time_constant <= 5:
        raise ValueError("Contour transition time constant must be between 0.05 and 5 seconds")
    ratio = delta_seconds / time_constant
    weight = -math.expm1(-ratio)
    result = {key: state[key] if state[key] == target[key] else state[key] * (1 - weight) + target[key] * weight for key in target}
    # Integral of the smoothed frequency; the series avoids cancellation at tiny dt.
    ramp_time = delta_seconds * ratio * (.5 - ratio/6 + ratio*ratio/24 - ratio*ratio*ratio/120) if ratio < .001 else delta_seconds - time_constant * weight
    turns = state["speed"] * (delta_seconds - ramp_time) + target["speed"] * ramp_time
    result["phase"] = (state["phase"] + math.tau * finite(turns, "contour.integrated_turns")) % math.tau
    validate_contour_values(rig, result, state=True)
    return result


# rig supplies the eleven bounded per-lobe displacement definitions.
# state is a previously validated contour state, or None for the undeformed authored outline.
def contour_offsets(rig, state):
    if state is None:
        return {}
    offsets = {}
    for lobe in rig["contour"]["lobes"]:
        wave = math.sin(state["phase"] - math.tau * state["phase_spread"] * lobe["phase_turns"])
        distance = state["fullness"] * lobe["fullness"] + state["amplitude"] * lobe["wave"] * wave
        offsets[lobe["point"]] = tuple(value * distance for value in lobe["direction"])
    return offsets


# rig supplies the channel bounds.
# values are independently composed facial controls in stable authoring order.
# Raises ValueError for missing, invalid or out-of-range channels; nothing is clamped.
def validate_channels(rig, values):
    if len(values) != len(rig["channels"]):
        raise ValueError("Unexpected channel count")
    for channel, value in zip(rig["channels"], values):
        if not channel["minimum"] <= finite(value, channel["name"]) <= channel["maximum"]:
            raise ValueError(f"{channel['name']}: rejected value {value}")


# rig supplies the ten channel bounds and face smoothing time constant.
# state contains the previous unanimated facial controls; it is not mutated.
# target contains the requested facial controls, independent of contour state.
# delta_ms is active elapsed time; zero freezes all facial transitions exactly.
# Raises ValueError for invalid channels, timing or smoothing configuration.
def advance_face(rig, state, target, delta_ms):
    validate_channels(rig, state)
    validate_channels(rig, target)
    seconds = finite(delta_ms, "face.delta_ms") / 1000
    if seconds < 0:
        raise ValueError("Face time cannot move backwards")
    time_constant = finite(rig["face_transition_seconds"], "face_transition_seconds")
    if not .05 <= time_constant <= 5:
        raise ValueError("Face transition time constant must be between 0.05 and 5 seconds")
    if seconds == 0:
        return list(state)
    weight = -math.expm1(-seconds / time_constant)
    result = [previous if previous == requested else previous * (1 - weight) + requested * weight
              for previous, requested in zip(state, target)]
    validate_channels(rig, result)
    return result


# base contains independent facial controls; elapsed_ms is monotonic animation time.
# Raises ValueError if elapsed time is negative or non-finite.
def animate(base, elapsed_ms):
    if finite(elapsed_ms, "elapsed_ms") < 0:
        raise ValueError("Animation time cannot be negative")
    breath = math.sin(math.tau * (elapsed_ms % 4000) / 4000)
    blink_ms = elapsed_ms % 4800
    blink = .5 - .5 * math.cos(math.tau * blink_ms / 280) if blink_ms < 280 else 0
    values = list(base)
    values[0] *= 1 - blink
    values[1] *= 1 - blink
    values[4] += (1 - base[4]) * .08 * (.5 + .5 * breath)
    values[5] += (1 - abs(base[5])) * breath
    return values


# rig supplies the authored paths and fixed subdivision count.
# definition identifies the single path to sample.
# world maps stable point names to evaluated display coordinates.
def sample_path(rig, definition, world):
    vertices = []
    curves = rig["curves"][definition["first_curve"]:definition["first_curve"] + definition["curve_count"]]
    for curve_index, curve in enumerate(curves):
        control = [world[name] for name in curve["points"]]
        if curve_index == 0:
            vertices.append(control[0])
        for step in range(1, rig["subdivisions"] + 1):
            t = step / rig["subdivisions"]
            u = 1 - t
            vertices.append(tuple(u*u*u*control[0][axis] + 3*u*u*t*control[1][axis] + 3*u*t*t*control[2][axis] + t*t*t*control[3][axis] for axis in (0, 1)))
    return vertices


# rig is the loaded authoring source.
# values are the independent facial controls.
# transform is (x, y, scale) in logical display coordinates.
# contour is an independent shape state; None preserves the original authored geometry.
# Raises ValueError if a caller supplies non-finite or unsupported geometry.
def evaluate(rig, values, transform, contour=None):
    validate_channels(rig, values)
    if contour is not None:
        validate_contour_values(rig, contour, state=True)
    if len(transform) != 3:
        raise ValueError("Transform requires x, y and uniform scale")
    x, y, scale = [finite(v, "transform") for v in transform]
    if not 0 <= x < 466 or not 0 <= y < 466 or not 20 <= scale <= 200:
        raise ValueError(f"Transform outside PoC limits: {transform}")
    channels = {channel["name"]: value for channel, value in zip(rig["channels"], values)}
    breathing_scale = 1 + .018 * channels["breath"]
    local = {}
    world = {}
    rest_local = {}
    rest_world = {}
    offsets = contour_offsets(rig, contour)
    for point in rig["points"]:
        amount = channels[point["channel"]] if point["channel"] is not None else 0
        px, py = [position + delta * amount for position, delta in zip(point["position"], point["delta"])]
        scale_x, scale_y = rig["point_face_transforms"][point["name"]]
        px *= channels[scale_x] if scale_x is not None else 1
        py *= channels[scale_y] if scale_y is not None else 1
        rest_x, rest_y = px, py
        offset_x, offset_y = offsets.get(point["name"], (0, 0))
        px += offset_x
        py += offset_y
        if point['parent'] is not None:
            parent_x, parent_y = local[point['parent']]
            px += parent_x
            py += parent_y
            parent_x, parent_y = rest_local[point['parent']]
            rest_x += parent_x
            rest_y += parent_y
        local[point["name"]] = (px, py)
        world[point["name"]] = (x + px * scale * breathing_scale, y + py * scale * breathing_scale)
        rest_local[point["name"]] = (rest_x, rest_y)
        rest_world[point["name"]] = (x + rest_x * scale * breathing_scale, y + rest_y * scale * breathing_scale)
    paths = []
    rest_outline = []
    for definition in rig["paths"]:
        vertices = sample_path(rig, definition, world)
        if definition["name"] == "body":
            rest_outline = sample_path(rig, definition, rest_world)
        opacity = channels[definition["opacity_channel"]] if definition["opacity_channel"] is not None else 1.0
        paths.append(dict(definition=definition, vertices=vertices,
                          visible=definition["visible"] is None or channels[definition["visible"]] >= .25,
                          opacity=opacity))
    return dict(local=local, world=world, paths=paths, scale=scale * breathing_scale, rest_outline=rest_outline)
