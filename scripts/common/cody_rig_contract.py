"""Shared authoring contract for the Cody rig and its host preview; no device I/O."""

from __future__ import annotations

import json
import math
import re
from pathlib import Path
import yaml

from scripts.common.cody_rig_types import (
    ContourControl, CurvePoint, FaceChannel, FillAntialiasing, PointRole, channel_name, face_channel,
)
from scripts.common.cody_response_contract import validate_response_config

ROOT = Path(__file__).resolve().parents[2]


class RigLoader(yaml.SafeLoader):
    """Safe YAML with duplicate-key rejection for authored character configuration."""


# loader: Safe YAML parser for this authoring document.
# node: Mapping whose original key/value pairs must remain unambiguous.
# deep: Whether nested values are constructed immediately.
# Raises ValueError on duplicate keys, including ambiguous style roles.
def yaml_mapping(loader, node, deep=False):
    pairs = (
        (loader.construct_object(key, deep=deep), loader.construct_object(value, deep=deep))
        for key, value in node.value
    )
    return unique_object(pairs)


RigLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, yaml_mapping)


# pairs contains the original JSON key/value sequence.
# Raises ValueError for an ambiguous duplicate key.
def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate authoring key: {key}")
        result[key] = value
    return result


# path identifies the authoring document to read without repairing its content.
# Raises OSError or ValueError on missing, unreadable or malformed data.
def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=unique_object)


# value: Required finite numeric input.
# label: Identifies the rejected authoring field in the error.
# Raises ValueError for booleans, non-numeric or non-finite values.
def finite(value, label):
    if type(value) not in (float, int) or not math.isfinite(value):
        raise ValueError(f"{label}: expected finite number, got {value!r}")
    return float(value)


# name: Authored stable ID converted to a readable C++ enumerator.
# Raises ValueError for an empty or unsupported identifier.
def enum_identifier(name):
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_.-]*", name):
        raise ValueError(f"Invalid stable identifier: {name!r}")
    words = re.split(r"[_.-]", name)
    if any(not word for word in words):
        raise ValueError(f"Empty identifier segment: {name!r}")
    return "".join(word[:1].upper() + word[1:] for word in words)


# names: Stable IDs inside one explicit namespace (point, curve, path, entity or color).
# category: Namespace named in duplicate/collision errors.
# Raises ValueError for duplicate IDs or IDs that produce the same C++ enum name.
def validate_identifiers(names, category):
    seen_names = set()
    seen_enumerators = {"None", "Count"}
    for name in names:
        enumerator = enum_identifier(name)
        if name in seen_names or enumerator in seen_enumerators:
            raise ValueError(f"Duplicate or ambiguous {category} identifier: {name!r}")
        seen_names.add(name)
        seen_enumerators.add(enumerator)


# root: Active checkout that owns the source assets.
# relative: Explicit YAML/palette reference, resolved inside this checkout's assets.
# Raises ValueError or OSError for an external, missing or non-file source.
def asset_path(root, relative):
    assets = (Path(root) / "assets").resolve()
    source = (Path(root) / relative).resolve()
    if not source.is_relative_to(assets) or not source.is_file():
        raise ValueError(f"Skin source must be a file below {assets}: {source}")
    return source


# rig: Checked facial-channel catalog.
# authored: Named preset values; positional authoring is deliberately unsupported.
# label: Preset identity named in any rejection.
# Raises ValueError for missing, extra or invalid preset channels.
def named_preset(rig, authored, label):
    expected = {channel_name(channel) for channel in FaceChannel}
    if not isinstance(authored, dict) or set(authored) != expected:
        raise ValueError(f"{label}: requires exactly the named facial channels")
    values = [authored[channel_name(channel)] for channel in FaceChannel]
    validate_channels(rig, values)
    return values


# rig: Style document whose named colors are resolved at the encoding boundary.
# palette: Shared RGB565 catalog; every entry has a stable unique color ID.
# Raises ValueError for unknown/reserved colors, reversed ramps or invalid pixel metrics.
def resolve_style(rig, palette):
    entries = palette["entries"]
    validate_identifiers([entry["id"] for entry in entries], "color")
    color_slots = {entry["id"]: entry["index"] for entry in entries}

    # name: Explicit source color ID; reserved slots cannot become visible skin colors.
    # Raises ValueError for a missing or reserved color reference.
    def color_slot(name):
        if name not in color_slots or entries[color_slots[name]]["reserved"]:
            raise ValueError(f"Unknown or reserved palette color: {name!r}")
        return color_slots[name]

    # names: Ordered named endpoints of a solid paint or inclusive gradient ramp.
    # Raises ValueError for malformed, reversed or reserved gradient ranges.
    def ramp(names):
        if not isinstance(names, list) or len(names) != 2:
            raise ValueError(f"Paint requires two named colors: {names!r}")
        first, last = [color_slot(name) for name in names]
        if first > last or any(entry["reserved"] for entry in entries[first:last + 1]):
            raise ValueError(f"Invalid gradient range: {names!r}")
        return [first, last]

    style = rig["style"]
    validate_identifiers(list(style["paints"]), "paint")
    style["paints"] = {name: ramp(colors) for name, colors in style["paints"].items()}
    style["debug"] = {role: color_slot(name) for role, name in style["debug"].items()}
    style["background"] = color_slot(style["background"])
    style["back"]["ink"] = color_slot(style["back"]["ink"])
    style["back"]["fill"] = ramp(style["back"]["fill"])
    expected_metrics = {
        "link_width", "handle_width", "outline_width", "anchor_diameter",
        "handle_diameter", "selected_diameter", "label_marker_diameter",
    }
    if set(style["debug_metrics"]) != expected_metrics:
        raise ValueError("Every named debug metric must be present")
    metrics = dict(style["debug_metrics"])
    metrics["back.stroke"] = style["back"]["stroke"]
    metrics["overlay.stroke"] = style["overlay_toggle"]["stroke"]
    metrics["overlay.marker_diameter"] = style["overlay_toggle"]["marker_diameter"]
    for name, value in metrics.items():
        if not 0 < finite(value, name) <= 21:
            raise ValueError(f"Unsupported pixel metric {name}: {value}")


# root: Checkout owning every source asset.
# skin_path: Explicit character YAML to load; this is a build/preview selection.
# Raises ValueError, yaml.YAMLError or OSError for invalid or unreadable skin authoring.
def load_contract(root=ROOT, skin_path="assets/cody/cody.yaml"):
    source = asset_path(root, skin_path)
    rig = yaml.load(source.read_text(encoding="utf-8"), Loader=RigLoader)
    if not isinstance(rig, dict) or rig.get("schema") != "cody.rig.v2":
        raise ValueError(f"Unsupported skin document: {source}")
    if rig["subdivisions"] != 12:
        raise ValueError("Unsupported cubic subdivision count")
    rig["source_path"] = source.relative_to(Path(root).resolve()).as_posix()
    enum_identifier(rig["character"]["id"])
    if rig["character"]["facing"] != "front" or not isinstance(rig["character"]["name"], str) or not rig["character"]["name"].strip():
        raise ValueError("Skin requires a nonempty name, stable id and front-facing rig")
    palette = read_json(asset_path(root, rig["palette_file"]))
    validate_palette(palette)
    resolve_style(rig, palette)
    if set(rig["style"]["debug"]) != {"anchor", "knot", "handle", "link", "selected"}:
        raise ValueError("All five debug color roles are required")
    font = rig["style"]["host_font"]
    if not isinstance(font["family"], str) or not font["family"] or type(font["size"]) is not int or not 6 <= font["size"] <= 24:
        raise ValueError("Invalid host font configuration")
    for path in rig["paths"]:
        if path["paint"] not in rig["style"]["paints"]:
            raise ValueError(f"Unknown paint: {path['name']}")
        path["paint"] = rig["style"]["paints"][path["paint"]]
    if not 1 <= len(rig["points"]) <= 203 or not 1 <= len(rig["curves"]) <= 64 or not 1 <= len(rig["paths"]) <= 16:
        raise ValueError("Rig exceeds the bounded firmware geometry capacity")
    validate_channel_catalog(rig)
    compile_geometry(rig)
    validate_face_definition(rig)
    compile_face_bindings(rig)
    validate_contour_definition(rig)
    validate_response_config(rig.get("response"))
    if set(rig["boot_channels"]) != {"sleep", "waking", "ready"}:
        raise ValueError("Three boot channel previews are required")
    rig["boot_channels"] = {
        name: named_preset(rig, values, f"boot.{name}")
        for name, values in rig["boot_channels"].items()
    }
    validate_identifiers(list(rig["expression_presets"]), "expression")
    if set(rig["expression_presets"]) & set(rig["boot_channels"]):
        raise ValueError("Expression IDs must not collide with boot preset IDs")
    for name, values in rig["expression_presets"].items():
        if not isinstance(name, str) or not name.strip():
            raise ValueError("Expression presets require nonempty names")
        rig["expression_presets"][name] = named_preset(rig, values, f"expression.{name}")
    placement = rig["default_transform"]
    x, y, scale = [finite(placement[key], "default_transform") for key in ("x", "y", "scale")]
    if not 0 <= x < 466 or not 0 <= y < 466 or not 20 <= scale <= 200:
        raise ValueError("Invalid authored default transform")
    motion = rig["response"]["motion"]
    excursion = scale * motion["translation_limit"]
    if x - excursion < 0 or x + excursion >= 466 or y - excursion < 0 or y + excursion >= 466:
        raise ValueError("Authored root must retain space for the complete motion excursion")
    if scale * (1 - motion["depth_limit"]) < 20 or scale * (1 + motion["depth_limit"]) > 200:
        raise ValueError("Authored scale must retain space for the complete depth excursion")
    return rig, palette



# rig: Declared semantic channels; YAML order never chooses their runtime meaning.
# Raises ValueError for duplicate names, unsupported channels or unsafe bounds.
def validate_channel_catalog(rig):
    declared_channels = {channel["name"]: channel for channel in rig["channels"]}
    expected_channels = {channel_name(channel) for channel in FaceChannel}
    validate_identifiers([channel["name"] for channel in rig["channels"]], "channel")
    if set(declared_channels) != expected_channels:
        raise ValueError("Skin must declare the supported named facial channels")
    # Canonical storage order is derived from semantic enums, never YAML positions.
    rig["channels"] = [declared_channels[channel_name(channel)] for channel in FaceChannel]
    for channel in rig["channels"]:
        lower, upper, default = [finite(channel[key], channel["name"]) for key in ("minimum", "maximum", "default")]
        if not lower <= default <= upper or lower >= upper:
            raise ValueError(f"Invalid channel bounds: {channel['name']}")
        signed_channels = {
            'brow_left', 'brow_right', 'breath', 'gaze_x', 'gaze_y',
            'eye_curve_left', 'eye_curve_right', 'eye_upper_left', 'eye_upper_right',
            'eye_lower_left', 'eye_lower_right', 'brow_height_left', 'brow_height_right',
            'brow_curve_left', 'brow_curve_right', 'mouth_corner_left', 'mouth_corner_right',
            'mouth_curve', 'mouth_x', 'mouth_y', 'nose_x', 'nose_y',
            'cheek_raise_left', 'cheek_raise_right',
        }
        scale_ranges = {'mouth_width': (.5, 1.6), 'nose_size': (.5, 1.6),
                        'cheek_size': (.4, 1.8), 'eye_width_left': (.6, 1.4),
                        'eye_width_right': (.6, 1.4)}
        expected_range = (-1, 1) if channel['name'] in signed_channels else scale_ranges.get(channel['name'], (0, 1))
        if (lower, upper) != expected_range:
            raise ValueError(f"{channel['name']}: semantic range must remain {expected_range}")


# rig: Named point/curve/path/entity graph, compiled without changing its geometry.
# Raises ValueError for missing IDs, ambiguous ownership or invalid geometry.
def compile_geometry(rig):
    channel_names = {channel_name(channel) for channel in FaceChannel}
    names = [point["name"] for point in rig["points"]]
    validate_identifiers(names, "point")
    # The first declared point owns the rig; give that relationship a name.
    rig["root_point"] = names[0]
    for index, point in enumerate(rig["points"]):
        if point["role"] not in {role.value for role in PointRole}:
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
    validate_identifiers(curve_names, "curve")
    for curve in rig["curves"]:
        if len(curve["points"]) != 4 or any(name not in names for name in curve["points"]):
            raise ValueError(f"Invalid cubic: {curve['name']}")
    validate_identifiers([path["name"] for path in rig["paths"]], "path")
    curves_by_name = {curve["name"]: curve for curve in rig["curves"]}
    ordered_curves = []
    owned_curves = set()
    # Compile named path ownership to compact contiguous curve ranges.
    for path in rig["paths"]:
        curve_ids = path["curves"]
        if not isinstance(curve_ids, list) or not 1 <= len(curve_ids) <= 21:
            raise ValueError(f"Invalid named curve list: {path['name']}")
        for curve_id in curve_ids:
            if curve_id not in curves_by_name or curve_id in owned_curves:
                raise ValueError(f"Missing or multiply owned curve: {curve_id}")
            owned_curves.add(curve_id)
            ordered_curves.append(curves_by_name[curve_id])
        path["first_curve"] = len(ordered_curves) - len(curve_ids)
        path["curve_count"] = len(curve_ids)
    if owned_curves != set(curves_by_name):
        raise ValueError("Every named curve must have exactly one path owner")
    rig["curves"] = ordered_curves
    path_names = {path["name"] for path in rig["paths"]}
    earlier_paths = {}
    for path in rig["paths"]:
        # Ownership was compiled above. Here we inspect the actual connected geometry.
        first_curve = path["first_curve"]
        last_curve = first_curve + path["curve_count"]
        segments = rig["curves"][first_curve:last_curve]
        for previous, following in zip(segments, segments[1:]):
            if previous["points"][CurvePoint.END] != following["points"][CurvePoint.START]:
                raise ValueError(f"Disconnected path: {path['name']}")
        width = finite(path["stroke"], path["name"])
        if not 0 <= width <= .1 or width * 200 * 1.018 > 21:
            raise ValueError(f"Unsupported stroke width: {path['name']}")
        try:
            path["antialiasing"] = FillAntialiasing(path.get("antialiasing", "disabled"))
        except (TypeError, ValueError) as error:
            raise ValueError(f"Invalid fill antialiasing: {path['name']}") from error
        if width != 0 and path["antialiasing"] != FillAntialiasing.DISABLED:
            raise ValueError(f"Coverage antialiasing requires a filled path: {path['name']}")
        first_point = segments[0]["points"][CurvePoint.START]
        last_point = segments[-1]["points"][CurvePoint.END]
        if width == 0 and first_point != last_point:
            raise ValueError(f"Filled path must be closed: {path['name']}")
        if path["visible"] is not None and path["visible"] not in channel_names:
            raise ValueError(f"Unknown visibility channel: {path['name']}")
        path["visible_minimum"] = finite(path.get("visible_minimum", .25), path["name"])
        if not 0 <= path["visible_minimum"] <= 1:
            raise ValueError(f"Invalid visibility threshold: {path['name']}")
        path["clip"] = path.get("clip")
        if path["clip"] is not None:
            if not isinstance(path["clip"], str):
                raise ValueError(f"Clip requires a named path identity: {path['name']}")
            containing = earlier_paths.get(path["clip"])
            if (containing is None or width != 0 or containing["stroke"] != 0 or
                    containing["clip"] is not None):
                raise ValueError(f"Clip requires an earlier independent filled path: {path['name']}")
            combined_vertices = (path["curve_count"] + containing["curve_count"]) * rig["subdivisions"] + 2
            if combined_vertices > 256:
                raise ValueError(f"Clipped fill exceeds shared intersection capacity: {path['name']}")
        earlier_paths[path["name"]] = path
    validate_identifiers(list(rig["entities"]), "entity")
    owned_paths = []
    for name, entity in rig["entities"].items():
        if not name or not isinstance(entity["label"], str) or not entity["label"].strip() or entity["anchor"] not in names or not entity["paths"]:
            raise ValueError(f"Invalid entity: {name}")
        owned_paths.extend(entity["paths"])
    if len(owned_paths) != len(set(owned_paths)) or set(owned_paths) != path_names:
        raise ValueError("Each drawn path must belong to exactly one named entity")


# palette: Shared indexed color source to check before any skin references are resolved.
# Raises ValueError for incomplete slots or inconsistent RGB888/RGB565 words.
def validate_palette(palette):
    if len(palette["entries"]) != 256 or [item["index"] for item in palette["entries"]] != list(range(256)):
        raise ValueError("Palette must contain all 256 ordered slots")
    for entry in palette["entries"]:
        rgb = entry["rgb888"].removeprefix("#")
        if len(rgb) != 6:
            raise ValueError(f"Invalid palette color: {entry['index']}")
        red, green, blue = (int(rgb[i:i+2], 16) for i in (0, 2, 4))
        if int(entry["rgb565"], 16) != ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3):
            raise ValueError(f"RGB565 mismatch: {entry['index']}")

# rig supplies the already checked point/path graph and declarative facial bindings.
# Raises ValueError for ambiguous transforms, unsafe anchors or unsupported opacity bindings.
def validate_face_definition(rig):
    if not .05 <= finite(rig["face_transition_seconds"], "face_transition_seconds") <= 5:
        raise ValueError("Face transition time constant must be between 0.05 and 5 seconds")
    bindings = rig["face_transforms"]
    anchor_ids = [binding["anchor"] for binding in bindings]
    if len(set(anchor_ids)) != len(anchor_ids):
        raise ValueError("Each facial anchor may have only one transform binding")
    for binding in bindings:
        face_channel(binding["channel"])
        axes = binding["axes"]
        if not axes or len(set(axes)) != len(axes) or set(axes) - {"x", "y"}:
            raise ValueError(f"Invalid facial transform axes: {binding['anchor']}")
    points = {point["name"]: point for point in rig["points"]}
    anchors = {item["anchor"]: item for item in bindings}
    for name in anchors:
        if name not in points or points[name]["role"] != PointRole.ANCHOR or points[name]["parent"] != rig["root_point"]:
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
    path_names = {path["name"] for path in rig["paths"]}
    for path_name, channel in rig["path_opacity"].items():
        if path_name not in path_names:
            raise ValueError(f"Unknown opacity path: {path_name}")
        definition = rig["channels"][face_channel(channel)]
        if definition["minimum"] != 0 or definition["maximum"] != 1:
            raise ValueError(f"Opacity requires a zero-to-one channel: {channel}")
    for path in rig["paths"]:
        opacity_channel = rig["path_opacity"].get(path["name"])
        if opacity_channel is not None and path["stroke"] != 0:
            raise ValueError(f"Opacity is supported by filled paths only: {path['name']}")
        path["opacity_channel"] = opacity_channel


# rig: Current authored graph; every displacement belongs to one named facial point.
# Raises ValueError for duplicate, non-facial, unbounded or ambiguous channel bindings.
def compile_face_bindings(rig):
    points = {point["name"]: point for point in rig["points"]}
    facial_anchors = {"eye.left", "eye.right", "brow.left", "brow.right",
                      "mouth", "nose", "cheek.left", "cheek.right"}
    bindings = rig["face_bindings"]
    if not isinstance(bindings, list) or not 1 <= len(bindings) <= 256:
        raise ValueError("Face requires 1-256 explicit point bindings")
    by_point = {name: [] for name in points}
    identities = set()
    for binding in bindings:
        if not isinstance(binding, dict) or set(binding) != {
                "point", "channel", "delta", "weight_channel", "invert_weight"}:
            raise ValueError(f"Invalid face binding fields: {binding!r}")
        name = binding["point"]
        if not isinstance(name, str) or name not in points:
            raise ValueError(f"Unknown bound facial point: {name!r}")
        ancestor = name
        while ancestor not in facial_anchors and points[ancestor]["parent"] is not None:
            ancestor = points[ancestor]["parent"]
        if ancestor not in facial_anchors:
            raise ValueError(f"Face binding cannot move body geometry: {name}")
        channel = face_channel(binding["channel"])
        identity = (name, channel)
        if identity in identities or points[name]["channel"] == binding["channel"]:
            raise ValueError(f"Duplicate face binding: {name}/{binding['channel']}")
        identities.add(identity)
        delta = binding["delta"]
        if not isinstance(delta, list) or len(delta) != 2:
            raise ValueError(f"Face displacement needs two coordinates: {name}")
        displacement = [finite(value, f"face binding {name}") for value in delta]
        if not any(displacement) or any(abs(value) > .2 for value in displacement):
            raise ValueError(f"Face displacement exceeds its bound: {name}/{delta!r}")
        if type(binding["invert_weight"]) is not bool:
            raise ValueError(f"Face weight inversion must be boolean: {name}")
        weight_name = binding["weight_channel"]
        if weight_name is None:
            if binding["invert_weight"]:
                raise ValueError(f"Inverted face weight needs a channel: {name}")
        else:
            weight = rig["channels"][face_channel(weight_name)]
            if (weight["minimum"], weight["maximum"]) != (0, 1):
                raise ValueError(f"Face weight requires a zero-to-one channel: {name}/{weight_name}")
        by_point[name].append(dict(binding, delta=displacement))
        if len(by_point[name]) > 8:
            raise ValueError(f"Face point exceeds eight independent bindings: {name}")
    # Named point/channel order produces deterministic flash ranges after reordering YAML.
    rig["point_face_bindings"] = {
        name: sorted(items, key=lambda item: face_channel(item["channel"]))
        for name, items in by_point.items()
    }



# rig: Validated named geometry with compiled facial transforms and opacity bindings.
# contour: Explicit outline and surface path declarations.
# points: Point definitions keyed by their stable authored identities.
# body_points: Controls belonging to the outline, kept separate from its surface art.
# Raises ValueError for shared, animated facial, or ambiguous surface geometry.
def contour_surface_points(rig, contour, points, body_points):
    surface_names = contour.get("surface_paths", [])
    if not isinstance(surface_names, list):
        raise ValueError("Contour surface_paths must be an explicit list of path IDs")
    validate_identifiers(surface_names, "contour surface path")
    paths_by_name = {path["name"]: path for path in rig["paths"]}
    surface_points = set()
    for name in surface_names:
        if name == contour["path"] or name not in paths_by_name:
            raise ValueError(f"Invalid contour surface path: {name!r}")
        path = paths_by_name[name]
        if path["stroke"] != 0 or path["visible"] is not None or path["opacity_channel"] is not None:
            raise ValueError(f"Contour surface must be an opaque independent fill: {name}")
        first_curve = path["first_curve"]
        curves = rig["curves"][first_curve:first_curve + path["curve_count"]]
        surface_points.update(point_name for curve in curves for point_name in curve["points"])

    # Decoration can inherit a lobe's motion, but cannot borrow facial geometry
    # or move the outline by sharing its curve controls.
    other_points = set(body_points)
    for path in rig["paths"]:
        if path["name"] in surface_names:
            continue
        first_curve = path["first_curve"]
        curves = rig["curves"][first_curve:first_curve + path["curve_count"]]
        other_points.update(point_name for curve in curves for point_name in curve["points"])
    shared_points = surface_points & other_points
    if shared_points:
        raise ValueError(f"Surface geometry shares another path's controls: {sorted(shared_points)}")
    for name in surface_points:
        ancestor = name
        while ancestor is not None:
            point = points[ancestor]
            transforms = rig["point_face_transforms"][ancestor]
            if point["channel"] is not None or any(channel is not None for channel in transforms):
                raise ValueError(f"Contour surface cannot inherit a facial channel: {name}")
            ancestor = point["parent"]
    return surface_points


# rig: Authored outline metadata and the already checked parent/path graph.
# Raises ValueError when contour controls could move a facial point or exceed authored bounds.
def validate_contour_definition(rig):
    contour = rig["contour"]
    expected = {"fullness": (-1, 1), "amplitude": (0, 1), "speed": (0, 2), "phase_spread": (0, 1)}
    validate_identifiers([item["name"] for item in contour["controls"]], "contour control")
    declared = {item["name"]: item for item in contour["controls"]}
    if set(declared) != set(expected):
        raise ValueError("Contour controls must name fullness, amplitude, speed and phase_spread")
    contour["controls"] = [declared[control.value] for control in ContourControl]
    for control in contour["controls"]:
        bounds = tuple(finite(control[key], control["name"]) for key in ("minimum", "maximum"))
        if bounds != expected[control["name"]] or not bounds[0] <= finite(control["default"], control["name"]) <= bounds[1]:
            raise ValueError(f"Invalid contour range/default: {control['name']}")
        if not isinstance(control["label"], str) or not control["label"].strip():
            raise ValueError(f"Missing contour label: {control['name']}")
    if not .05 <= finite(contour["transition_seconds"], "contour.transition_seconds") <= 5:
        raise ValueError("Contour transition time constant must be between 0.05 and 5 seconds")
    body = next((path for path in rig["paths"] if path["name"] == contour["path"]), None)
    if body is None or body["stroke"] != 0 or not 1 <= body["curve_count"] <= 21:
        raise ValueError("Contour requires an explicit bounded filled path")
    curves = rig["curves"][body["first_curve"]:body["first_curve"] + body["curve_count"]]
    lobe_names = [curve["points"][CurvePoint.START] for curve in curves]
    if [lobe["point"] for lobe in contour["lobes"]] != lobe_names or len(set(lobe_names)) != len(curves):
        raise ValueError("Contour lobes must follow the unique outline knots in perimeter order")
    points = {point["name"]: point for point in rig["points"]}
    body_points = {name for curve in curves for name in curve["points"]}
    for curve in curves:
        for handle, anchor in ((curve["points"][CurvePoint.OUTGOING_HANDLE], curve["points"][CurvePoint.START]), (curve["points"][CurvePoint.INCOMING_HANDLE], curve["points"][CurvePoint.END])):
            if points[handle]["role"] != PointRole.HANDLE or points[handle]["parent"] != anchor:
                raise ValueError(f"Contour handle must follow its own lobe: {handle}")
    for name in body_points:
        if points[name]["channel"] is not None:
            raise ValueError(f"Contour point cannot use a facial channel: {name}")
    surface_points = contour_surface_points(rig, contour, points, body_points)
    movable_points = body_points | surface_points
    for point in rig["points"]:
        ancestor = point["parent"]
        while ancestor is not None:
            if ancestor in lobe_names and point["name"] not in movable_points:
                raise ValueError(f"Facial geometry cannot descend from a contour lobe: {point['name']}")
            ancestor = points[ancestor]["parent"]
    previous_phase = -1
    for lobe in contour["lobes"]:
        point = points[lobe["point"]]
        if point["role"] != PointRole.ANCHOR or point["parent"] != rig["root_point"]:
            raise ValueError(f"Contour lobe must be a direct root anchor: {lobe['point']}")
        if len(lobe["direction"]) != 2:
            raise ValueError(f"Contour direction needs two coordinates: {lobe['point']}")
        dx, dy = (finite(value, lobe["point"]) for value in lobe["direction"])
        if abs(dx*dx + dy*dy - 1) > 1e-6:
            raise ValueError(f"Contour direction must have unit length: {lobe['point']}")
        fullness = finite(lobe["fullness"], lobe["point"])
        wave = finite(lobe["wave"], lobe["point"])
        # Each skin declares its local excursion limit; the engine keeps a hard safety ceiling.
        limit = finite(lobe["limit"], f"contour.{lobe['point']}.limit")
        if not 0 < limit <= .12:
            raise ValueError(f"Invalid contour excursion limit: {lobe['point']}")
        if not 0 <= fullness <= .06 or not 0 <= wave <= .06 or fullness + wave > limit:
            raise ValueError(f"Contour displacement exceeds the authored bound: {lobe['point']}")
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
    result = {}
    for name, requested in target.items():
        previous = state[name]
        result[name] = previous if previous == requested else previous * (1 - weight) + requested * weight
    # Integral of the smoothed frequency; the series avoids cancellation at tiny dt.
    if ratio < .001:
        ramp_time = delta_seconds * ratio * (.5 - ratio / 6 + ratio ** 2 / 24 - ratio ** 3 / 120)
    else:
        ramp_time = delta_seconds - time_constant * weight
    turns = state[ContourControl.SPEED] * (delta_seconds - ramp_time) + target[ContourControl.SPEED] * ramp_time
    result["phase"] = (state["phase"] + math.tau * finite(turns, "contour.integrated_turns")) % math.tau
    validate_contour_values(rig, result, state=True)
    return result


# rig: Supplies the skin-defined bounded per-lobe displacement definitions.
# state is a previously validated contour state, or None for the undeformed authored outline.
def contour_offsets(rig, state):
    if state is None:
        return {}
    offsets = {}
    for lobe in rig["contour"]["lobes"]:
        wave = math.sin(state["phase"] - math.tau * state[ContourControl.PHASE_SPREAD] * lobe["phase_turns"])
        distance = state[ContourControl.FULLNESS] * lobe["fullness"] + state[ContourControl.AMPLITUDE] * lobe["wave"] * wave
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


# rig supplies the named channel bounds and face smoothing time constant.
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


# values: Validated face channels, updated in place after artistic response contributions.
# breathing: Existing rhythm's sine value in [-1,1]; no separate clock or synthetic input.
def apply_mouth_breathing(values, breathing):
    inhalation = .5 + .5 * breathing
    exhalation = 1 - inhalation
    mouth = FaceChannel.MOUTH_OPEN
    reopening = .16 + .08 * max(0, 1 - values[mouth] / .14)
    values[mouth] *= 1 - .75 * exhalation
    values[mouth] += (1 - values[mouth]) * reopening * inhalation
    width = FaceChannel.MOUTH_WIDTH
    # Match the native rig's canonical width interval without clipping rejected inputs.
    values[width] -= (values[width] - .5) * .18 * exhalation
    values[width] += (1.6 - values[width]) * .18 * inhalation


# base: Independent semantic face channels, accessed through FaceChannel.
# elapsed_ms: Active monotonic animation time.
# Raises ValueError if elapsed time is negative or non-finite.
def animate(base, elapsed_ms):
    if finite(elapsed_ms, "elapsed_ms") < 0:
        raise ValueError("Animation time cannot be negative")
    breath = math.sin(math.tau * (elapsed_ms % 4000) / 4000)
    blink_ms = elapsed_ms % 4800
    blink = .5 - .5 * math.cos(math.tau * blink_ms / 280) if blink_ms < 280 else 0
    values = list(base)
    values[FaceChannel.EYE_LEFT] *= 1 - blink
    values[FaceChannel.EYE_RIGHT] *= 1 - blink
    apply_mouth_breathing(values, breath)
    values[FaceChannel.BREATH] += (1 - abs(base[FaceChannel.BREATH])) * breath
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
            vertices.append(control[CurvePoint.START])
        for step in range(1, rig["subdivisions"] + 1):
            progress = step / rig["subdivisions"]
            remaining = 1 - progress
            weights = (
                remaining ** 3,
                3 * remaining ** 2 * progress,
                3 * remaining * progress ** 2,
                progress ** 3,
            )
            # The handles steer the curve; they are not extra points on its outline.
            horizontal = sum(weight * point[0] for weight, point in zip(weights, control))
            vertical = sum(weight * point[1] for weight, point in zip(weights, control))
            vertices.append((horizontal, vertical))
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
        raise ValueError(f"Transform outside authored limits: {transform}")
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
        for binding in rig["point_face_bindings"][point["name"]]:
            weight_name = binding["weight_channel"]
            weight = channels[weight_name] if weight_name is not None else 1
            if binding["invert_weight"]:
                weight = 1 - weight
            contribution = channels[binding["channel"]] * weight
            px += binding["delta"][0] * contribution
            py += binding["delta"][1] * contribution
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
        if definition["name"] == rig["contour"]["path"]:
            rest_outline = sample_path(rig, definition, rest_world)
        opacity = channels[definition["opacity_channel"]] if definition["opacity_channel"] is not None else 1.0
        paths.append(dict(definition=definition, vertices=vertices,
                          visible=definition["visible"] is None or
                              channels[definition["visible"]] >= definition["visible_minimum"],
                          opacity=opacity))
        if definition["clip"] is not None:
            containing = next(path for path in paths if path["definition"]["name"] == definition["clip"])
            paths[-1]["visible"] = paths[-1]["visible"] and containing["visible"]
    return dict(local=local, world=world, paths=paths, scale=scale * breathing_scale, rest_outline=rest_outline)
