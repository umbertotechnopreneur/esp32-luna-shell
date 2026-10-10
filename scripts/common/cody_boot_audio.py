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

"""Shared read-only SFX authoring and exact private Cody greeting preparation."""

from array import array
import hashlib
import json
import math
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shutil
import subprocess
import sys
import wave

from scripts.common.asset_provenance import text_source_hash


ROOT = Path(__file__).resolve().parents[2]
SELECTION = ROOT / "assets/cody/audio/boot-hello/source-selection.json"
SAMPLE_RATE = 22050


# pairs: Original JSON mapping entries whose identities must remain unambiguous.
# Raises RuntimeError for a duplicate authoring key.
def unique_mapping(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise RuntimeError(f"Duplicate boot greeting authoring key: {key}")
        result[key] = value
    return result


# value: Required mapping for one explicit authoring object.
# expected: Exact supported field identities, without silent defaults or aliases.
# label: Authoring section named in a rejection.
# Raises RuntimeError for missing, extra or incorrectly typed fields.
def require_fields(value, expected, label):
    if not isinstance(value, dict) or set(value) != expected:
        raise RuntimeError(f"{label} requires exactly these fields: {sorted(expected)}")


# value: Explicit numeric authoring value; booleans are not tuning numbers.
# label: Field identity included in any rejection.
# minimum: Inclusive permitted lower bound.
# maximum: Inclusive permitted upper bound.
# Raises RuntimeError for nonfinite, incorrectly typed or out-of-range values.
def bounded_number(value, label, minimum, maximum):
    if type(value) not in (int, float) or not math.isfinite(value) or not minimum <= value <= maximum:
        raise RuntimeError(f"{label} must be a finite number in {minimum}..{maximum}: {value!r}")
    return value


# value: Required lowercase SHA256 identity, never an inferred or repaired digest.
# label: Field identity included in an authoring rejection.
# Raises RuntimeError for a missing or malformed digest.
def require_sha256(value, label):
    if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise RuntimeError(f"{label} requires one explicit lowercase SHA256 digest")


# selection: Parsed catalog; selection_sha256 is added only after a trusted text read.
# Raises RuntimeError for malformed schema, unsafe paths/IDs or unbounded phrase authoring.
def validate_selection(selection):
    expected = {
        "schema_version", "archive_root", "sample_rate_hz", "target_peak_dbfs",
        "chime_gap_ms", "original_chime_sha256", "licensing", "cues",
    }
    if isinstance(selection, dict) and "selection_sha256" in selection:
        expected.add("selection_sha256")
        require_sha256(selection["selection_sha256"], "selection_sha256")
    require_fields(selection, expected, "Boot greeting catalog")
    if type(selection["schema_version"]) is not int or selection["schema_version"] != 1:
        raise RuntimeError("Unsupported boot greeting authoring schema")
    if type(selection["sample_rate_hz"]) is not int or selection["sample_rate_hz"] != SAMPLE_RATE:
        raise RuntimeError("Boot greetings require the explicit 22050 Hz shared codec rate")
    archive = selection["archive_root"]
    if (not isinstance(archive, str) or not archive or "\0" in archive or
            not (PurePosixPath(archive).is_absolute() or PureWindowsPath(archive).is_absolute())):
        raise RuntimeError("Boot greeting archive_root requires an explicit absolute path")
    bounded_number(selection["target_peak_dbfs"], "target_peak_dbfs", -18, -6)
    gap = selection["chime_gap_ms"]
    if type(gap) is not int or not 50 <= gap <= 300:
        raise RuntimeError("Boot greeting chime_gap_ms must be an explicit integer in 50..300")
    require_sha256(selection["original_chime_sha256"], "original_chime_sha256")
    require_fields(selection["licensing"], {"status", "scope", "restriction"}, "Greeting licensing")
    for field, value in selection["licensing"].items():
        if not isinstance(value, str) or not value.strip():
            raise RuntimeError(f"Greeting licensing.{field} requires explicit nonempty text")
    cues = selection["cues"]
    if not isinstance(cues, list) or not cues:
        raise RuntimeError("Boot greeting catalog requires at least one explicit candidate")
    derived_names = set()
    cue_fields = {
        "id", "source", "source_sha256", "trim_start_seconds", "trim_end_seconds",
        "pitch_ratio", "post_pitch_tempo", "expected_pcm_sha256", "expected_pcm_bytes",
    }
    for cue in cues:
        require_fields(cue, cue_fields, "Boot greeting candidate")
        identity = cue["id"]
        if not isinstance(identity, str) or re.fullmatch(r"cody-[a-z]+(?:-[a-z]+)*", identity) is None:
            raise RuntimeError(f"Unsafe boot greeting identity: {identity!r}")
        # Voice and composed-preview names share one output namespace.
        names = {identity, f"{identity}-boot"}
        if derived_names.intersection(names):
            raise RuntimeError(f"Duplicate or colliding derived greeting name: {identity}")
        derived_names.update(names)
        source = cue["source"]
        if (not isinstance(source, str) or not source or "\\" in source or ":" in source or
                "\0" in source or any(part in ("", ".", "..") for part in source.split("/"))):
            raise RuntimeError(f"{identity}.source requires a safe archive-relative path")
        require_sha256(cue["source_sha256"], f"{identity}.source_sha256")
        start = bounded_number(cue["trim_start_seconds"], f"{identity}.trim_start_seconds", 0, sys.float_info.max)
        end = bounded_number(cue["trim_end_seconds"], f"{identity}.trim_end_seconds", 0, sys.float_info.max)
        pitch = bounded_number(cue["pitch_ratio"], f"{identity}.pitch_ratio", .75, 1.6)
        tempo = bounded_number(cue["post_pitch_tempo"], f"{identity}.post_pitch_tempo", .75, 1.6)
        if end <= start:
            raise RuntimeError(f"{identity} requires a nonempty, forward phrase interval")
        effective_pitch = round(SAMPLE_RATE * pitch) / SAMPLE_RATE
        estimated_duration = (end - start) / (effective_pitch * tempo)
        if not .2 <= estimated_duration <= 1.5:
            raise RuntimeError(f"{identity} authored phrase must render approximately 200-1500 ms")
        require_sha256(cue["expected_pcm_sha256"], f"{identity}.expected_pcm_sha256")
        pcm_bytes = cue["expected_pcm_bytes"]
        if (type(pcm_bytes) is not int or pcm_bytes % 2 or
                not round(.2 * SAMPLE_RATE) * 2 <= pcm_bytes <= round(1.5 * SAMPLE_RATE) * 2):
            raise RuntimeError(f"{identity}.expected_pcm_bytes must describe bounded mono PCM16")


# path: Explicit UTF-8 source-selection file; platform line endings do not change provenance.
# Raises original filesystem, Unicode and JSON failures without changing the source.
# Raises RuntimeError for invalid or ambiguous authoring.
def load_selection(path=SELECTION):
    path = Path(path)
    selection = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_mapping)
    if isinstance(selection, dict) and "selection_sha256" in selection:
        raise RuntimeError("selection_sha256 is derived provenance, not an authored field")
    validate_selection(selection)
    selection["selection_sha256"] = text_source_hash(path)
    return selection


# executable: Required ffmpeg/ffprobe tool resolved from PATH.
# arguments: Literal arguments, never passed through shell interpolation.
# Raises RuntimeError for a missing tool or its original nonzero execution status.
# Raises original process admission and timeout failures.
def run_tool(executable, arguments):
    tool = shutil.which(executable)
    if tool is None:
        raise RuntimeError(f"Required media tool is unavailable: {executable}")
    result = subprocess.run([tool, *arguments], capture_output=True, timeout=60)
    if result.returncode != 0:
        error = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"{executable} failed ({result.returncode}): {error}")
    return result.stdout


# source: Read-only external recording whose resolved identity has been hash-verified.
# cue: Validated phrase boundaries and exact original pitch/post-pitch shaping factors.
# target_peak: Normalized digital peak; not a device loudness measurement.
# Raises RuntimeError for unsupported duration/streams or silent/nonfinite/unbounded audio.
# Raises original process and JSON errors without substituting a recording.
def render_voice(source, cue, target_peak):
    probe = json.loads(run_tool("ffprobe", [
        "-v", "error", "-select_streams", "a:0", "-show_entries",
        "stream=sample_rate,channels:format=duration", "-of", "json", str(source),
    ]))
    streams = probe.get("streams", [])
    if len(streams) != 1 or streams[0].get("channels") not in (1, 2):
        raise RuntimeError(f"Expected one mono/stereo source: {source}")
    start = cue["trim_start_seconds"]
    end = cue["trim_end_seconds"]
    pitch = cue["pitch_ratio"]
    post_pitch_tempo = cue["post_pitch_tempo"]
    source_duration = float(probe["format"]["duration"])
    if not math.isfinite(source_duration) or not 0 <= start < end <= source_duration:
        raise RuntimeError(f"Phrase exceeds the source: {cue['id']}")
    downmix = "pan=mono|c0=0.5*c0+0.5*c1" if streams[0]["channels"] == 2 else "pan=mono|c0=c0"
    # asetrate changes pitch AND speed. The explicit post-pitch factor preserves
    # the audition: effective speed is pitch * post_pitch_tempo, subject to the
    # unchanged integer sample-rate rounding below.
    filters = [
        f"atrim=start={start}:end={end}", "asetpts=PTS-STARTPTS", downmix,
        "highpass=f=180:p=2", "lowpass=f=4200:p=2",
        f"aresample={SAMPLE_RATE}", f"asetrate={round(SAMPLE_RATE * pitch)}",
        f"aresample={SAMPLE_RATE}", f"atempo={post_pitch_tempo}",
        "afade=t=in:d=0.012", "areverse", "afade=t=in:d=0.045", "areverse",
    ]
    encoded = run_tool("ffmpeg", [
        "-hide_banner", "-loglevel", "error", "-nostdin", "-i", str(source),
        "-map", "0:a:0", "-af", ",".join(filters), "-ar", str(SAMPLE_RATE),
        "-ac", "1", "-c:a", "pcm_f64le", "-f", "f64le", "pipe:1",
    ])
    samples = array("d")
    if len(encoded) % samples.itemsize:
        raise RuntimeError(f"Incomplete decoded sample: {cue['id']}")
    samples.frombytes(encoded)
    if sys.byteorder != "little":
        samples.byteswap()
    if not round(.2 * SAMPLE_RATE) <= len(samples) <= round(1.5 * SAMPLE_RATE):
        raise RuntimeError(f"Rendered greeting must be 200-1500 ms: {cue['id']}")
    if not all(math.isfinite(value) for value in samples):
        raise RuntimeError(f"Non-finite decoded audio: {cue['id']}")
    peak = max(abs(value) for value in samples)
    if peak <= 1e-10:
        raise RuntimeError(f"Silent greeting: {cue['id']}")
    gain = 10 ** (target_peak / 20) / peak
    pcm = array("h", (round(value * gain * 32767) for value in samples))
    pcm[0] = 0
    pcm[-1] = 0
    if sys.byteorder != "little":
        pcm.byteswap()
    return pcm.tobytes(), filters, gain, probe


# source: Resolved external recording opened only in binary read mode.
# Raises OSError for the original read failure without changing the archive.
def source_hash(source):
    digest = hashlib.sha256()
    with source.open("rb") as recording:
        while chunk := recording.read(65536):
            digest.update(chunk)
    return digest.hexdigest()


# selection: Strict catalog returned by load_selection, including stable text provenance.
# cue_id: One exact authored candidate identity, never an index or guessed fallback.
# archive_root: Optional external archive directory; None uses the authored absolute root.
# Raises RuntimeError for unsafe paths or mismatched source/rendered identities.
# Raises original filesystem/process failures; archive and firmware assets remain read-only.
def render_selected_greeting(selection, cue_id, archive_root=None):
    validate_selection(selection)
    if "selection_sha256" not in selection:
        raise RuntimeError("Greeting rendering requires catalog provenance from load_selection")
    cue = next((candidate for candidate in selection["cues"] if candidate["id"] == cue_id), None)
    if cue is None:
        raise RuntimeError(f"Unknown selected boot greeting: {cue_id!r}")
    archive = Path(selection["archive_root"] if archive_root is None else archive_root).resolve(strict=True)
    if not archive.is_dir():
        raise RuntimeError(f"Boot greeting archive is not a directory: {archive}")
    source = (archive / cue["source"]).resolve(strict=True)
    if not source.is_relative_to(archive) or not source.is_file():
        raise RuntimeError(f"Greeting source escapes the read-only archive: {source}")
    verified_source_hash = source_hash(source)
    if verified_source_hash != cue["source_sha256"]:
        raise RuntimeError(f"Selected greeting source SHA256 mismatch: {cue['id']}")
    pcm, filters, gain, probe = render_voice(source, cue, selection["target_peak_dbfs"])
    if source_hash(source) != verified_source_hash:
        raise RuntimeError(f"Selected greeting source changed during rendering: {cue['id']}")
    pcm_hash = hashlib.sha256(pcm).hexdigest()
    if len(pcm) != cue["expected_pcm_bytes"] or pcm_hash != cue["expected_pcm_sha256"]:
        raise RuntimeError(f"Selected greeting differs from its exact authored audition: {cue['id']}")
    metadata = {
        **cue, "source": str(source), "source_sha256": verified_source_hash,
        "selection_sha256": selection["selection_sha256"],
        "sample_rate_hz": SAMPLE_RATE, "channels": 1, "encoding": "PCM16LE",
        "source_format": probe, "ffmpeg_filters": filters, "linear_gain": gain,
        "target_peak_dbfs": selection["target_peak_dbfs"],
        "pcm_bytes": len(pcm), "duration_seconds": len(pcm) / 2 / SAMPLE_RATE,
        "pcm_sha256": pcm_hash,
    }
    return pcm, metadata


# destination: Caller-owned local WAV path, never inferred from the external source.
# pcm: Mono little-endian signed PCM16 at the shared codec rate.
# Raises RuntimeError for incomplete PCM16 frames.
# Raises OSError or wave.Error for the original output failure.
def write_wav(destination, pcm):
    if len(pcm) % 2:
        raise RuntimeError("Mono PCM16 WAV requires complete two-byte samples")
    with wave.open(str(destination), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm)
