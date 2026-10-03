"""Strict shared authoring contract for reusable cloud/title/content scenes."""

import math
from pathlib import Path

from scripts.common.cody_rig_contract import load_contract, read_json, validate_channels
from scripts.common.cody_rig_types import FaceChannel
from scripts.common.asset_provenance import text_source_hash

SCENE_IDENTITIES = {
    "greeting": (1, "Greeting"), "pair-invitation": (2, "PairInvitation"),
    "install-app": (3, "InstallApp"), "scan-to-pair": (4, "ScanToPair"),
    "pairing": (5, "Pairing"), "pairing-failed": (6, "PairingFailed"),
    "online": (7, "Online"), "cody-look": (8, "CodyLook"),
}


# root: Checkout containing the current scene source.
# Raises OSError or UnicodeError for unreadable authoring; LF/CRLF is not source drift.
def scene_source_hash(root: Path):
    return text_source_hash(root / "assets/cody/scenes/scenes.json")


# value: Authored JSON object for one named part of the scene.
# expected: Exact supported field names; unknown fields are not silently ignored.
# label: Context included in an actionable error.
# Raises ValueError for an unsupported field or object type.
def require_keys(value, expected, label):
    if not isinstance(value, dict) or set(value) != set(expected):
        raise ValueError(f"{label}: expected fields {sorted(expected)}, got {value!r}")


# root: Active checkout containing the single current scene schema.
# rig: Optional already-loaded current rig; None loads the same canonical authoring.
# Raises OSError for missing files and ValueError for invalid or ambiguous authoring.
def load_scenes(root: Path, rig=None):
    document = read_json(root / "assets/cody/scenes/scenes.json")
    require_keys(document, {"schema", "boot_delay_ms", "font", "scenes"}, "scenes")
    if document["schema"] != "cody.scenes.v1":
        raise ValueError(f"Unsupported scene schema: {document['schema']!r}")
    delay = document["boot_delay_ms"]
    if type(delay) is not int or not 1 <= delay <= 10000:
        raise ValueError(f"Invalid first-boot scene delay: {delay!r}")
    font = document["font"]
    require_keys(font, {"pixel_size", "first_character", "last_character"}, "scene font")
    if (any(type(value) is not int for value in font.values()) or
            font != {"pixel_size": 30, "first_character": 32, "last_character": 126}):
        raise ValueError("Scene font must explicitly select the current 30px printable-ASCII asset")
    scenes = document["scenes"]
    if not isinstance(scenes, list) or not scenes:
        raise ValueError("At least one named scene is required")
    identities = set()
    names = set()
    if rig is None:
        rig, _ = load_contract(root)
    for scene in scenes:
        require_keys(scene, {"id", "identity", "intro", "entrance", "cloud", "face", "title",
                            "navigation_visible", "readout_visible"}, "scene")
        name = scene["id"]
        identity = scene["identity"]
        if (not isinstance(name, str) or type(identity) is not int or
                name not in SCENE_IDENTITIES or identity != SCENE_IDENTITIES[name][0]):
            raise ValueError(f"Unknown or inconsistent scene identity: {name!r}/{identity!r}")
        if name in names or identity in identities:
            raise ValueError(f"Duplicate scene identity: {name!r}/{identity!r}")
        names.add(name)
        identities.add(identity)
        intro = scene["intro"]
        require_keys(intro, {"bitmap_fade_out_ms", "cloud_fade_in_ms"}, f"{name}.intro")
        for key, value in intro.items():
            if type(value) is not int or not 0 <= value <= 10000:
                raise ValueError(f"{name}.intro.{key}: expected integer milliseconds in 0..10000")
        cloud = scene["cloud"]
        require_keys(cloud, {"x", "y", "scale"}, f"{name}.cloud")
        for key, value in cloud.items():
            if type(value) not in (int, float) or not math.isfinite(value):
                raise ValueError(f"{name}.cloud.{key}: expected a finite number")
        if not 0 <= cloud["x"] < 466 or not 0 <= cloud["y"] < 466 or not 20 <= cloud["scale"] <= 200:
            raise ValueError(f"{name}.cloud leaves the supported logical canvas")
        entrance = scene["entrance"]
        require_keys(entrance, {"from", "move_ms", "text_delay_ms"}, f"{name}.entrance")
        require_keys(entrance["from"], {"x", "y", "scale"}, f"{name}.entrance.from")
        for key, maximum in (("x", 465), ("y", 465), ("scale", 200)):
            value = entrance["from"][key]
            if (type(value) not in (int, float) or not math.isfinite(value)
                    or not (20 if key == "scale" else 0) <= value <= maximum):
                raise ValueError(f"{name}.entrance.from.{key}: invalid placement")
        for key in ("move_ms", "text_delay_ms"):
            if type(entrance[key]) is not int or not 0 <= entrance[key] <= 10000:
                raise ValueError(f"{name}.entrance.{key}: invalid duration")
        face = scene["face"]
        require_keys(face, {channel.name.lower() for channel in FaceChannel}, f"{name}.face")
        if any(type(value) not in (int, float) or not math.isfinite(value) for value in face.values()):
            raise ValueError(f"{name}.face contains a non-finite channel")
        validate_channels(rig, [face[channel.name.lower()] for channel in FaceChannel])
        title = scene["title"]
        require_keys(title, {"text", "top", "color_rgb565", "character_interval_ms", "visible"}, f"{name}.title")
        if type(title["visible"]) is not bool:
            raise ValueError(f"{name}.title.visible must be an explicit boolean")
        text = title["text"]
        if not isinstance(text, str) or not 1 <= len(text) <= 64 or any(not 32 <= ord(c) <= 126 for c in text):
            raise ValueError(f"{name}.title requires bounded printable ASCII, without fallback glyphs")
        for key, maximum in (("top", 465), ("color_rgb565", 65535), ("character_interval_ms", 1000)):
            if type(title[key]) is not int or not 0 <= title[key] <= maximum:
                raise ValueError(f"{name}.title.{key}: invalid value {title[key]!r}")
        for key in ("navigation_visible", "readout_visible"):
            if type(scene[key]) is not bool:
                raise ValueError(f"{name}.{key} must be an explicit boolean")
    return document
