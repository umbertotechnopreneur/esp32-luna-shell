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

"""Strict source selection and immutable authored Cody action cue loading."""

from pathlib import Path, PurePosixPath
import hashlib
import json
import math
import re

from scripts.common.asset_provenance import text_source_hash

ROOT = Path(__file__).resolve().parents[2]
SELECTION = ROOT / "assets/cody/audio/codex-actions/source-selection.json"
GENERATED = SELECTION.parent / "generated"
BOOT_IDENTITY = "boot-rise"


# pairs: Original JSON object entries, before duplicate identities disappear.
# Raises ValueError for duplicate authoring keys.
def unique_mapping(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Cody action audio: duplicate key {key!r}")
        result[key] = value
    return result


# value: One explicitly authored numeric value.
# lower: Inclusive lower bound.
# upper: Inclusive upper bound.
# label: Field identity in an authoring rejection.
# Raises ValueError for wrong types, nonfinite numbers or out-of-range values.
def number(value, lower, upper, label):
    if type(value) not in (int, float) or not math.isfinite(value) or not lower <= value <= upper:
        raise ValueError(f"Cody action audio {label}: expected {lower}..{upper}, got {value!r}")
    return value


# value: Authored object requiring one exact, unambiguous field set.
# names: Required field names.
# label: Object identity in a rejection.
# Raises ValueError for wrong types or missing/extra fields.
def fields(value, names, label):
    if not isinstance(value, dict) or set(value) != set(names):
        raise ValueError(f"Cody action audio {label}: requires exactly {sorted(names)}")


# No parameters: loads the read-only source selection without decoding any sound.
# Raises ValueError or OSError for invalid/ambiguous authoring or a failed read.
def load_selection():
    selection = json.loads(SELECTION.read_text(encoding="utf-8"), object_pairs_hook=unique_mapping)
    fields(selection, ("schema_version", "archive_root", "licensing", "sources", "cues"), "selection")
    if type(selection["schema_version"]) is not int or selection["schema_version"] != 1:
        raise ValueError("Cody action audio: unsupported schema")
    if not isinstance(selection["archive_root"], str) or not Path(selection["archive_root"]).is_absolute():
        raise ValueError("Cody action audio: archive_root must be an absolute path")
    fields(selection["licensing"], ("status", "scope", "restriction"), "licensing")
    if any(not isinstance(value, str) or not value.strip() for value in selection["licensing"].values()):
        raise ValueError("Cody action audio: licensing values must be explicit text")
    sources = selection["sources"]
    if not isinstance(sources, dict) or not sources:
        raise ValueError("Cody action audio: explicit source catalog required")
    for name, source in sources.items():
        fields(source, ("path", "sha256"), f"source {name}")
        path = source["path"]
        if (not isinstance(path, str) or not path or PurePosixPath(path).is_absolute() or
                "\\" in path or ":" in path or "\0" in path or
                any(part in ("", ".", "..") for part in path.split("/"))):
            raise ValueError(f"Cody action audio: unsafe source path for {name}")
        if not isinstance(source["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", source["sha256"]):
            raise ValueError(f"Cody action audio: explicit source hash required for {name}")
    identities = set()
    names = set()
    slugs = set()
    if not isinstance(selection["cues"], list) or not selection["cues"]:
        raise ValueError("Cody action audio: nonempty cue catalog required")
    for cue in selection["cues"]:
        fields(cue, ("name", "id", "slug", "sample_rate_hz", "duration_ms", "target_peak_dbfs", "intent", "layers"), "cue")
        if type(cue["id"]) is not int or not 0x43430000 <= cue["id"] <= 0x4343FFFF:
            raise ValueError(f"Cody action audio: ID must belong to its reserved namespace: {cue['id']!r}")
        if not isinstance(cue["name"], str) or not re.fullmatch(r"[A-Z][A-Za-z0-9]+", cue["name"]):
            raise ValueError("Cody action audio: unsafe C++ cue name")
        if not isinstance(cue["slug"], str) or not re.fullmatch(r"[a-z]+(?:-[a-z]+)*", cue["slug"]):
            raise ValueError("Cody action audio: unsafe cue output name")
        if cue["id"] in identities or cue["name"] in names or cue["slug"] in slugs:
            raise ValueError(f"Cody action audio: duplicate cue identity: {cue['name']}")
        identities.add(cue["id"])
        names.add(cue["name"])
        slugs.add(cue["slug"])
        boot = cue["slug"] == BOOT_IDENTITY
        if type(cue["sample_rate_hz"]) is not int or cue["sample_rate_hz"] != (22050 if boot else 16000):
            raise ValueError(f"Cody action audio: unsupported PCM clock for {cue['name']}")
        if type(cue["duration_ms"]) is not int:
            raise ValueError(f"Cody action audio: integer duration required for {cue['name']}")
        # Only the stable question identity admits a single sustained bell; other actions stay short.
        question = (cue["id"], cue["name"], cue["slug"]) == (0x4343000B, "Question", "question")
        maximum_duration_ms = 3000 if boot else 2000 if question else 350
        number(cue["duration_ms"], 1000 if boot else 50, maximum_duration_ms, "cue.duration_ms")
        # Keep authored headroom while allowing short notices to stand above quiet activity textures.
        number(cue["target_peak_dbfs"], -30, -9 if boot else -12, "cue.target_peak_dbfs")
        if not isinstance(cue["intent"], str) or not cue["intent"].strip():
            raise ValueError(f"Cody action audio: explicit intent required for {cue['name']}")
        if not isinstance(cue["layers"], list) or not 1 <= len(cue["layers"]) <= 4:
            raise ValueError(f"Cody action audio: expected one to four layers for {cue['name']}")
        for layer in cue["layers"]:
            fields(layer, ("source", "window_start_ms", "window_end_ms", "offset_ms", "duration_ms", "pitch_ratio", "gain", "highpass_hz", "lowpass_hz", "fade_in_ms", "fade_out_ms"), "layer")
            if layer["source"] not in sources:
                raise ValueError(f"Cody action audio: unknown source {layer['source']!r}")
            number(layer["window_start_ms"], 0, 30000, "layer.window_start_ms")
            number(layer["window_end_ms"], layer["window_start_ms"] + 1, layer["window_start_ms"] + 3000, "layer.window_end_ms")
            number(layer["offset_ms"], 0, cue["duration_ms"] - 1, "layer.offset_ms")
            number(layer["duration_ms"], 30, cue["duration_ms"] - layer["offset_ms"], "layer.duration_ms")
            number(layer["pitch_ratio"], .5, 2, "layer.pitch_ratio")
            number(layer["gain"], .01, 1, "layer.gain")
            number(layer["highpass_hz"], 60, 400, "layer.highpass_hz")
            number(layer["lowpass_hz"], 800, 5000, "layer.lowpass_hz")
            number(layer["fade_in_ms"], 2, layer["duration_ms"] / 2, "layer.fade_in_ms")
            number(layer["fade_out_ms"], 10, layer["duration_ms"] - layer["fade_in_ms"], "layer.fade_out_ms")
    if BOOT_IDENTITY not in slugs:
        raise ValueError("Cody action audio: explicit boot-rise cue required")
    selection["selection_sha256"] = text_source_hash(SELECTION)
    return selection


# identity: Explicit supported authored boot cue slug.
# sample_rate_hz: Codec clock required by the runtime consumer.
# Raises ValueError or OSError for missing, stale or changed generated PCM; no external-source fallback.
def load_boot_cue(identity, sample_rate_hz):
    if identity != BOOT_IDENTITY or sample_rate_hz != 22050:
        raise ValueError(f"Cody action audio: unsupported boot selection {identity!r} at {sample_rate_hz} Hz")
    selection = load_selection()
    manifest = json.loads((GENERATED / "manifest.json").read_text(encoding="utf-8"), object_pairs_hook=unique_mapping)
    if manifest["selection_sha256"] != selection["selection_sha256"]:
        raise ValueError("Cody action audio: stale generated catalog; regenerate cody-codex-sfx")
    matches = [cue for cue in manifest["cues"] if cue["slug"] == identity]
    if len(matches) != 1:
        raise ValueError("Cody action audio: missing or ambiguous generated boot identity")
    entry = matches[0]
    pcm = (GENERATED / f"{identity}.pcm16le").read_bytes()
    if (len(pcm) != entry["pcm_bytes"] or entry["sample_rate_hz"] != sample_rate_hz or
            hashlib.sha256(pcm).hexdigest() != entry["pcm_sha256"]):
        raise ValueError("Cody action audio: generated boot PCM identity mismatch")
    return pcm, {
        "origin": "selected licensed samples transformed into Cody boot-rise",
        "selection_sha256": selection["selection_sha256"],
        "cue": identity,
        "layers": entry["layers"],
        "licensing": selection["licensing"],
    }
