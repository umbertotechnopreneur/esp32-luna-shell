# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Strict shared authoring contract for reusable cloud/title/content scenes."""

import hashlib
import math
from pathlib import Path
import re

from scripts.common.cody_rig_contract import load_contract, read_json, validate_channels
from scripts.common.cody_rig_types import FaceChannel
from scripts.common.asset_provenance import text_source_hash

SCENE_IDENTITIES = {
    "greeting": (1, "Greeting"), "pair-invitation": (2, "PairInvitation"),
    "install-app": (3, "InstallApp"), "scan-to-pair": (4, "ScanToPair"),
    "pairing": (5, "Pairing"), "pairing-failed": (6, "PairingFailed"),
    "online": (7, "Online"), "cody-look": (8, "CodyLook"),
    "debug-text": (9, "DebugText"),
    "make-it-yours": (10, "MakeItYours"), "settings": (11, "Settings"),
    "connect-codex": (12, "ConnectCodex"), "setup-complete": (13, "SetupComplete"),
    "looking-for-pc": (14, "LookingForPc"), "connected": (15, "Connected"),
    "codex-choice": (16, "CodexChoice"), "setup-menu": (17, "SetupMenu"),
    "network-wait": (18, "NetworkWait"),
    "project-active": (19, "ProjectActive"), "working": (20, "Working"),
    "response-ready": (21, "ResponseReady"), "session-paused": (22, "SessionPaused"),
    "session-ended": (23, "SessionEnded"), "connection-lost": (24, "ConnectionLost"),
    "companion-idle": (25, "CompanionIdle"), "activity-unavailable": (26, "ActivityUnavailable"),
    "pr-pending": (27, "PrPending"), "checks-running": (28, "ChecksRunning"),
    "checks-failed": (29, "ChecksFailed"), "check-failures": (30, "CheckFailures"),
    "decision-notice": (31, "DecisionNotice"), "decision-choices": (32, "DecisionChoices"),
    "decision-confirm": (33, "DecisionConfirm"), "decision-waiting": (34, "DecisionWaiting"),
    "reply-sent": (35, "ReplySent"), "codex-notice": (36, "CodexNotice"),
    "approval-on-pc": (37, "ApprovalOnPc"), "event-unavailable": (38, "EventUnavailable"),
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


# directory: Existing generated scene assets; no font discovery or generation occurs here.
# Raises OSError, UnicodeError or ValueError for missing, corrupt or unsupported asset formats.
def load_generated_scene_fonts(directory: Path):
    manifest = read_json(directory / "manifest.json")
    if not isinstance(manifest, dict) or manifest.get("schema") not in (
            "cody.scene-font.v1", "cody.scene-font.v2"):
        raise ValueError("Unsupported generated scene-font schema")
    explicit_format = manifest["schema"] == "cody.scene-font.v2"
    fields = {"schema", "font_name", "source_family", "license", "font_source_sha256",
              "scenes_sha256", "first_character", "character_count", "width", "height", "advance",
              "row_bytes", "coverage_bytes", "coverage_sha256", "note", "body_font"}
    format_fields = {"coverage_bits", "coverage_file"} if explicit_format else set()
    require_keys(manifest, fields | format_fields, "generated scene font")
    if (type(manifest["first_character"]) is not int or manifest["first_character"] != 32 or
            type(manifest["character_count"]) is not int or manifest["character_count"] != 95 or
            manifest["source_family"] != "JetBrains Mono" or manifest["license"] != "SIL-OFL-1.1"):
        raise ValueError("Generated fonts require the licensed printable-ASCII JetBrains Mono source")
    for key in ("font_source_sha256", "scenes_sha256"):
        if not isinstance(manifest[key], str) or re.fullmatch(r"[0-9a-f]{64}", manifest[key]) is None:
            raise ValueError(f"Invalid generated font provenance: {key}")
    body = manifest["body_font"]
    require_keys(body, {"pixel_size", "width", "height", "advance", "row_bytes",
                        "coverage_bytes", "coverage_sha256"} | format_fields, "generated caption font")
    if type(body["pixel_size"]) is not int or body["pixel_size"] != 18:
        raise ValueError("Generated caption font must retain its explicit 18px size")
    loaded = []
    for name, font in (("scene_font", manifest), ("scene_body_font", body)):
        # V1 names the committed two-bit format. V2 always declares a format;
        # neither byte lengths nor file availability are used to guess one.
        bits = font["coverage_bits"] if explicit_format else 2
        if type(bits) is not int or bits not in (2, 4):
            raise ValueError(f"{name}: unsupported coverage format {bits!r}")
        numeric = ("width", "height", "advance", "row_bytes", "coverage_bytes")
        if (any(type(font[key]) is not int for key in numeric) or
                not 1 <= font["width"] <= 32 or not 1 <= font["height"] <= 64 or
                not 1 <= font["advance"] <= 32 or font["row_bytes"] != (font["width"] * bits + 7) // 8 or
                font["coverage_bytes"] != 95 * font["height"] * font["row_bytes"]):
            raise ValueError(f"{name}: invalid printable-ASCII packed extent")
        if not isinstance(font["coverage_sha256"], str) or re.fullmatch(
                r"[0-9a-f]{64}", font["coverage_sha256"]) is None:
            raise ValueError(f"{name}: invalid coverage digest")
        cpp = (directory / f"{name}.cpp").read_text(encoding="utf-8")
        symbol = "title_font" if name == "scene_font" else "body_font"
        initializer = re.findall(
            rf"const\s+CODY2D::MonoFont\s+{symbol}\s*\{{\s*coverage\s*,\s*sizeof\(coverage\)\s*,([^}}]+)\}}\s*;", cpp)
        if len(initializer) != 1:
            raise ValueError(f"{name}: missing or ambiguous flash font declaration")
        metrics = [field.strip() for field in initializer[0].split(",")]
        expected = ["32", "95", str(font["width"]), str(font["height"]),
                    str(font["advance"]), str(font["row_bytes"])]
        if explicit_format:
            expected.append(str(bits))
        if metrics != expected:
            raise ValueError(f"{name}: flash font metrics or coverage format differ from the manifest")
        compiled_coverage = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})\b", cpp))
        if explicit_format:
            filename = f"{name}.{bits}bpp.bin"
            if font["coverage_file"] != filename:
                raise ValueError(f"{name}: coverage file must explicitly match its format")
            coverage = (directory / filename).read_bytes()
        elif name == "scene_font":
            coverage = (directory / "scene_font.2bpp.bin").read_bytes()
        else:
            # The current V1 caption asset is committed only as a flash array.
            coverage = compiled_coverage
        if (len(coverage) != font["coverage_bytes"] or compiled_coverage != coverage or
                hashlib.sha256(coverage).hexdigest() != font["coverage_sha256"]):
            raise ValueError(f"{name}: coverage is corrupt, incomplete or differs from the flash array")
        loaded.append(coverage)
    return manifest, loaded[0], loaded[1]


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
        require_keys(scene, {"id", "identity", "intro", "entrance", "background_rgb565", "cloud", "face", "title",
                            "navigation_visible", "readout_visible", "standby_override_seconds"}, "scene")
        name = scene["id"]
        identity = scene["identity"]
        if (not isinstance(name, str) or type(identity) is not int or
                name not in SCENE_IDENTITIES or identity != SCENE_IDENTITIES[name][0]):
            raise ValueError(f"Unknown or inconsistent scene identity: {name!r}/{identity!r}")
        if name in names or identity in identities:
            raise ValueError(f"Duplicate scene identity: {name!r}/{identity!r}")
        names.add(name)
        identities.add(identity)
        standby_seconds = scene["standby_override_seconds"]
        if type(standby_seconds) is not int or not 0 <= standby_seconds <= 0xFFFFFFFF:
            raise ValueError(f"{name}.standby_override_seconds: expected integer seconds in 0..4294967295")
        if type(scene["background_rgb565"]) is not int or not 0 <= scene["background_rgb565"] <= 65535:
            raise ValueError(f"{name}.background_rgb565: invalid native color")
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
