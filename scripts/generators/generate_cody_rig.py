"""Compile one explicitly selected skin into immutable, named firmware tables."""

from pathlib import Path
import argparse
import hashlib
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.common.cody_rig_contract import ROOT, enum_identifier, load_contract
from scripts.common.cody_rig_types import FaceChannel, channel_name


# value: Finite authoring coordinate emitted as an unambiguous C++ float.
def cpp_float(value):
    return f"{float(value):.9f}f"


# enum_type: C++ domain type, such as PointId or FaceChannel.
# name: Stable authored ID; None is an explicit absent reference.
def cpp_identity(enum_type, name):
    enumerator = "None" if name is None else enum_identifier(name)
    return f"{enum_type}::{enumerator}"


# enum_type: Generated identity namespace.
# names: Already validated stable IDs in compact storage order.
# underlying: Unsigned C++ type sized for this table.
# sentinel: Explicit absent-reference value outside the valid table range.
def emit_enum(enum_type, names, underlying="uint16_t", sentinel="UINT16_MAX"):
    lines = [f"enum class {enum_type} : {underlying} {{"]
    for name in names:
        lines.append(f"    {enum_identifier(name)},")
    lines.extend([f"    Count, None = {sentinel}", "};"])
    return lines


# label: Descriptive name of an immutable float table.
# values: Finite checked authoring values, in the engine's semantic order.
def float_table(label, values):
    encoded = ", ".join(cpp_float(value) for value in values)
    return f"inline constexpr float {label}[] = {{{encoded}}};"


# rig: Loaded skin with named ownership and compiled geometry ranges.
# palette: Validated shared color catalog.
def emit_geometry(rig, palette):
    lines = ["inline constexpr PointDefinition points[] = {"]
    for point in rig["points"]:
        identity = cpp_identity("PointId", point["name"])
        parent = cpp_identity("PointId", point["parent"])
        channel = cpp_identity("FaceChannel", point["channel"])
        role = cpp_identity("PointRole", point["role"])
        coordinates = ", ".join(cpp_float(value) for value in point["position"] + point["delta"])
        lines.append(f"    {{{identity}, {json.dumps(point['name'])}, {coordinates}, {parent}, {channel}, {role}}},")
    lines.extend(["};", "inline constexpr CurveDefinition curves[] = {"])
    for curve in rig["curves"]:
        identity = cpp_identity("CurveId", curve["name"])
        points = ", ".join(cpp_identity("PointId", name) for name in curve["points"])
        lines.append(f"    {{{identity}, {{{points}}}}},")
    lines.extend(["};", "inline constexpr PathDefinition paths[] = {"])
    for path in rig["paths"]:
        identity = cpp_identity("PathId", path["name"])
        first_curve = rig["curves"][path["first_curve"]]["name"]
        first_color, last_color = [
            cpp_identity("ColorId", palette["entries"][slot]["id"]) for slot in path["paint"]
        ]
        visible = cpp_identity("FaceChannel", path["visible"])
        opacity = cpp_identity("FaceChannel", path["opacity_channel"])
        fields = (
            identity, json.dumps(path["name"]), cpp_identity("CurveId", first_curve),
            str(path["curve_count"]), first_color, last_color, cpp_float(path["stroke"]),
            visible, opacity,
        )
        lines.append("    {" + ", ".join(fields) + "},")
    lines.extend(["};", "inline constexpr PointFaceTransform point_face_transforms[] = {"])
    for point in rig["points"]:
        horizontal, vertical = rig["point_face_transforms"][point["name"]]
        lines.append(f"    {{{cpp_identity('FaceChannel', horizontal)}, {cpp_identity('FaceChannel', vertical)}}},")
    lines.extend(["};", "inline constexpr EntityDefinition entities[] = {"])
    for name, entity in rig["entities"].items():
        fields = (
            cpp_identity("EntityId", name), json.dumps(name), json.dumps(entity["label"]),
            cpp_identity("PointId", entity["anchor"]),
        )
        lines.append("    {" + ", ".join(fields) + "},")
    lines.append("};")
    return lines


# rig: Loaded skin containing independent face and outline controls.
def emit_animation(rig):
    lines = []
    for label, field in (("channel_min", "minimum"), ("channel_max", "maximum")):
        lines.append(float_table(label, [channel[field] for channel in rig["channels"]]))
    names = ", ".join(json.dumps(channel_name(channel)) for channel in FaceChannel)
    lines.append(f"inline constexpr const char *channel_names[] = {{{names}}};")
    for name, values in rig["boot_channels"].items():
        encoded = ", ".join(cpp_float(value) for value in values)
        lines.append(f"inline constexpr Channels {name} = {{{{{encoded}}}}};")
    defaults = ", ".join(cpp_float(channel["default"]) for channel in rig["channels"])
    lines.append(f"inline constexpr Channels defaults = {{{{{defaults}}}}};")
    lines.append(f"inline constexpr float face_transition_seconds = {cpp_float(rig['face_transition_seconds'])};")
    controls = rig["contour"]["controls"]
    defaults = ", ".join(cpp_float(control["default"]) for control in controls)
    lines.append(f"inline constexpr ContourControls contour_defaults = {{{{{defaults}}}}};")
    for label, field in (("contour_min", "minimum"), ("contour_max", "maximum")):
        lines.append(float_table(label, [control[field] for control in controls]))
    names = ", ".join(json.dumps(control["name"]) for control in controls)
    lines.append(f"inline constexpr const char *contour_names[] = {{{names}}};")
    lines.append(f"inline constexpr float contour_transition_seconds = {cpp_float(rig['contour']['transition_seconds'])};")
    lines.append("inline constexpr ContourLobeDefinition contour_lobes[] = {")
    for lobe in rig["contour"]["lobes"]:
        coordinates = lobe["direction"] + [lobe["fullness"], lobe["wave"], lobe["phase_turns"]]
        values = ", ".join(cpp_float(value) for value in coordinates)
        lines.append(f"    {{{cpp_identity('PointId', lobe['point'])}, {values}}},")
    lines.append("};")
    lobes = {lobe["point"] for lobe in rig["contour"]["lobes"]}
    identities = [
        cpp_identity("ContourLobeId", point["name"] if point["name"] in lobes else None)
        for point in rig["points"]
    ]
    lines.append("inline constexpr ContourLobeId point_contour_lobe[] = {")
    lines.extend(f"    {identity}," for identity in identities)
    lines.append("};")
    return lines


# rig: Style with resolved palette slots and named pixel metrics.
# palette: Complete native RGB565 catalog.
def emit_style(rig, palette):
    lines = ["inline constexpr uint16_t palette[256] = {"]
    for start in range(0, len(palette["entries"]), 8):
        words = [entry["rgb565"] for entry in palette["entries"][start:start + 8]]
        lines.append("    " + ", ".join(words) + ",")
    lines.append("};")
    style = rig["style"]
    colors = {f"debug_{role}": slot for role, slot in style["debug"].items()}
    colors.update(
        background_color=style["background"],
        back_top=style["back"]["fill"][0],
        back_bottom=style["back"]["fill"][1],
        back_ink=style["back"]["ink"],
    )
    for name, slot in colors.items():
        identity = cpp_identity("ColorId", palette["entries"][slot]["id"])
        lines.append(f"inline constexpr ColorId {name} = {identity};")
    metrics = {f"debug_{name}": value for name, value in style["debug_metrics"].items()}
    metrics.update(
        back_stroke=style["back"]["stroke"],
        overlay_stroke=style["overlay_toggle"]["stroke"],
        overlay_marker_diameter=style["overlay_toggle"]["marker_diameter"],
    )
    for name, value in metrics.items():
        lines.append(f"inline constexpr float {name} = {cpp_float(value)};")
    placement = ", ".join(cpp_float(rig["default_transform"][key]) for key in ("x", "y", "scale"))
    lines.append(f"inline constexpr Transform placement = {{{placement}}};")
    lines.append(f"inline constexpr const char *character_id = {json.dumps(rig['character']['id'])};")
    lines.append(f"inline constexpr const char *character_name = {json.dumps(rig['character']['name'])};")
    return lines


# rig: Selected skin with identifiers already checked by the authoring contract.
# palette: Validated native color catalog.
def generate_header(rig, palette):
    lines = [
        f"// Generated from {rig['source_path']}; edit that skin, never this table.",
        "#pragma once", '#include "CODY_rig.hpp"', "namespace CODY {",
    ]
    for enum_type, names in (
        ("PointId", [point["name"] for point in rig["points"]]),
        ("CurveId", [curve["name"] for curve in rig["curves"]]),
        ("PathId", [path["name"] for path in rig["paths"]]),
        ("EntityId", list(rig["entities"])),
        ("ColorId", [entry["id"] for entry in palette["entries"]]),
    ):
        lines.extend(emit_enum(enum_type, names))
    lines.extend(emit_enum("ContourLobeId", [lobe["point"] for lobe in rig["contour"]["lobes"]],
                           "uint8_t", "UINT8_MAX"))
    lines.extend(["}  // namespace CODY", "namespace CODY::Data {"])
    lines.extend(emit_geometry(rig, palette))
    lines.extend(emit_animation(rig))
    lines.extend(emit_style(rig, palette))
    lines.extend(["}  // namespace CODY::Data", ""])
    return "\n".join(lines)


# argv: Optional explicit CLI arguments from scripts/device.ps1.
# Raises OSError or ValueError for invalid selected assets or failed package writes.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skin", default="assets/cody/cody.yaml",
                        help="Selected character YAML below this checkout's assets directory")
    args = parser.parse_args(argv)
    rig, palette = load_contract(skin_path=args.skin)
    content = generate_header(rig, palette)
    output = ROOT / "assets/cody/rig/generated"
    manifest = {
        "schema": "cody.rig.generated.v2",
        "skin_id": rig["character"]["id"],
        "source_path": rig["source_path"],
        "points": len(rig["points"]),
        "curves": len(rig["curves"]),
        "paths": len(rig["paths"]),
        "subdivisions": rig["subdivisions"],
        "palette_slots": len(palette["entries"]),
        "contour_controls": len(rig["contour"]["controls"]),
        "contour_lobes": len(rig["contour"]["lobes"]),
        "facial_channels": len(rig["channels"]),
        "face_transforms": len(rig["face_transforms"]),
        "source_sha256": hashlib.sha256((ROOT / rig["source_path"]).read_bytes()).hexdigest(),
        "palette_sha256": hashlib.sha256((ROOT / rig["palette_file"]).read_bytes()).hexdigest(),
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "cloud_rig_data.hpp").write_text(content, encoding="utf-8")
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated skin {rig['character']['id']}: {output / 'cloud_rig_data.hpp'}")


if __name__ == "__main__":
    main()
