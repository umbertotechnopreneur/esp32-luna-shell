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
    if channel_names != ["eye_left", "eye_right", "brow_left", "brow_right", "mouth_open", "breath"]:
        raise ValueError("Unexpected facial channel order")
    for channel in rig["channels"]:
        lower, upper, default = [finite(channel[key], channel["name"]) for key in ("minimum", "maximum", "default")]
        if not lower <= default <= upper or lower >= upper:
            raise ValueError(f"Invalid channel bounds: {channel['name']}")
        expected_range = (-1, 1) if channel['name'] in ('brow_left','brow_right','breath') else (0, 1)
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
    placement = rig["default_transform"]
    x, y, scale = [finite(placement[key], "default_transform") for key in ("x", "y", "scale")]
    if not 0 <= x < 466 or not 0 <= y < 466 or not 20 <= scale <= 200:
        raise ValueError("Invalid authored default transform")
    return rig, palette


# rig supplies the channel bounds; values are independently composed facial controls.
# Raises ValueError for missing, invalid or out-of-range channels; nothing is clamped.
def validate_channels(rig, values):
    if len(values) != len(rig["channels"]):
        raise ValueError("Unexpected channel count")
    for channel, value in zip(rig["channels"], values):
        if not channel["minimum"] <= finite(value, channel["name"]) <= channel["maximum"]:
            raise ValueError(f"{channel['name']}: rejected value {value}")


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


# rig is the loaded source; values are the facial controls; transform is (x, y, scale).
# Raises ValueError if a caller supplies non-finite or unsupported geometry.
def evaluate(rig, values, transform):
    validate_channels(rig, values)
    if len(transform) != 3:
        raise ValueError("Transform requires x, y and uniform scale")
    x, y, scale = [finite(v, "transform") for v in transform]
    if not 0 <= x < 466 or not 0 <= y < 466 or not 20 <= scale <= 200:
        raise ValueError(f"Transform outside PoC limits: {transform}")
    channels = {channel["name"]: value for channel, value in zip(rig["channels"], values)}
    breathing_scale = 1 + .018 * channels["breath"]
    local = {}
    world = {}
    for point in rig["points"]:
        amount = channels[point["channel"]] if point["channel"] is not None else 0
        px, py = [position + delta * amount for position, delta in zip(point["position"], point["delta"])]
        if point['parent'] is not None:
            parent_x, parent_y = local[point['parent']]
            px += parent_x
            py += parent_y
        local[point["name"]] = (px, py)
        world[point["name"]] = (x + px * scale * breathing_scale, y + py * scale * breathing_scale)
    paths = []
    for definition in rig["paths"]:
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
        paths.append(dict(definition=definition, vertices=vertices, visible=definition["visible"] is None or channels[definition["visible"]] >= .25))
    return dict(local=local, world=world, paths=paths, scale=scale * breathing_scale)
