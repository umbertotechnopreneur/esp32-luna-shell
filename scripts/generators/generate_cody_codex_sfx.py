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

"""Author Cody's boot chime and bounded Codex action cues from purchased local samples."""

from array import array
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.asset_provenance import text_source_hash
from scripts.common.cody_codex_audio import BOOT_IDENTITY, GENERATED, load_selection
from scripts.generators.generate_cody_gentle_sfx import probe_source, run_media_tool, source_sha256


# values: Finite decoded mono samples at the cue's target clock.
# length: Required exact excerpt length in samples.
# rate: Decoded sample clock.
# Raises ValueError if the source window is too short or contains no usable sound.
def active_excerpt(values, length, rate):
    if len(values) < length or any(not math.isfinite(value) for value in values):
        raise ValueError("Cody action audio: source window is short or nonfinite")
    energy = sum(value * value for value in values[:length])
    best_energy = energy
    best_start = 0
    previous = 0
    for start in range(max(1, rate // 200), len(values) - length + 1, max(1, rate // 200)):
        energy -= sum(value * value for value in values[previous:start])
        energy += sum(value * value for value in values[previous + length:start + length])
        if energy > best_energy:
            best_energy = energy
            best_start = start
        previous = start
    if best_energy <= 1e-12:
        raise ValueError("Cody action audio: configured source window is silent")
    return values[best_start:best_start + length], best_start


# layer: One authored bounded source passage, pitch and envelope.
# rate: Output clock, 16000 for actions or 22050 for the boot owner.
# source: Read-only external source with verified format and identity.
# Raises ValueError or RuntimeError for source or media processing failures.
def render_layer(layer, rate, source):
    if layer["window_end_ms"] / 1000 > source["format"]["duration_seconds"]:
        raise ValueError(f"Cody action audio: window exceeds {source['relative_path']}")
    downmix = "pan=mono|c0=0.5*c0+0.5*c1" if source["format"]["channels"] == 2 else "pan=mono|c0=c0"
    pitch_clock = round(rate * layer["pitch_ratio"])
    filters = [
        f"atrim=start={layer['window_start_ms'] / 1000:.6f}:end={layer['window_end_ms'] / 1000:.6f}",
        "asetpts=PTS-STARTPTS", downmix,
        f"highpass=f={layer['highpass_hz']}:p=2", f"lowpass=f={layer['lowpass_hz']}:p=2",
        f"aresample={rate}", f"asetrate={pitch_clock}", f"aresample={rate}",
    ]
    encoded = run_media_tool("ffmpeg", [
        "-hide_banner", "-loglevel", "error", "-nostdin", "-i", str(source["path"]),
        "-map", "0:a:0", "-af", ",".join(filters), "-ac", "1", "-ar", str(rate),
        "-c:a", "pcm_f64le", "-f", "f64le", "pipe:1",
    ])
    values = array("d")
    values.frombytes(encoded)
    if sys.byteorder != "little":
        values.byteswap()
    values, trim_frames = active_excerpt(values, round(layer["duration_ms"] * rate / 1000), rate)
    attack = round(layer["fade_in_ms"] * rate / 1000)
    release = round(layer["fade_out_ms"] * rate / 1000)
    for index in range(attack):
        values[index] *= math.sin(math.pi * index / (2 * (attack - 1))) ** 2
    for index in range(release):
        values[len(values) - release + index] *= math.cos(math.pi * index / (2 * (release - 1))) ** 2
    peak = max(abs(value) for value in values)
    if peak <= 1e-10:
        raise ValueError(f"Cody action audio: silent shaped layer {source['relative_path']}")
    # Normalize layers before their explicit mix gains; source recording level is not product volume.
    values = [value / peak * layer["gain"] for value in values]
    return values, {
        **layer,
        "source_path": source["relative_path"], "source_sha256": source["sha256"],
        "source_format": source["format"], "ffmpeg_filters": filters,
        "active_excerpt_offset_frames_after_pitch": trim_frames,
        "actual_pitch_ratio": pitch_clock / rate,
        "fade_curve": "sine-squared", "normalization_gain_before_mix": 1 / peak,
    }


# cue: One named action or boot cue with an explicit target duration and level.
# sources: Verified read-only source recordings indexed by stable names.
# Raises ValueError or RuntimeError for invalid source material or failed rendering.
def render_cue(cue, sources):
    rate = cue["sample_rate_hz"]
    mixed = [0.0] * round(cue["duration_ms"] * rate / 1000)
    layers = []
    for layer in cue["layers"]:
        values, metadata = render_layer(layer, rate, sources[layer["source"]])
        offset = round(layer["offset_ms"] * rate / 1000)
        if offset + len(values) > len(mixed):
            raise ValueError(f"Cody action audio: layer exceeds the cue {cue['name']}")
        for index, value in enumerate(values):
            mixed[offset + index] += value
        layers.append(metadata)
    peak = max(abs(value) for value in mixed)
    if not math.isfinite(peak) or peak <= 1e-10:
        raise ValueError(f"Cody action audio: silent mixed cue {cue['name']}")
    gain = 32767 * 10 ** (cue["target_peak_dbfs"] / 20) / peak
    samples = [round(value * gain) for value in mixed]
    samples[0] = 0
    samples[-1] = 0
    pcm = struct.pack(f"<{len(samples)}h", *samples)
    return samples, pcm, {
        "id": cue["id"], "name": cue["name"], "slug": cue["slug"], "intent": cue["intent"],
        "sample_rate_hz": rate, "sample_count": len(samples), "pcm_bytes": len(pcm),
        "duration_ms": len(samples) * 1000 / rate,
        "target_peak_dbfs": cue["target_peak_dbfs"], "linear_gain_after_mix": gain,
        "peak_dbfs": 20 * math.log10(max(abs(value) for value in samples) / 32768),
        "rms_dbfs": 20 * math.log10(math.sqrt(sum(value * value for value in samples) / len(samples)) / 32768),
        "pcm_sha256": hashlib.sha256(pcm).hexdigest(), "layers": layers,
    }


# path: Authored WAV output below the repository asset or artifacts directories.
# pcm: Mono little-endian PCM16 payload.
# rate: Payload's exact sample rate.
# Raises OSError or wave.Error for a real output failure.
def write_wav(path, pcm, rate):
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        output.writeframes(pcm)


# prepared: Fully rendered cue samples and metadata, before any firmware file is replaced.
# Raises OSError for an output failure.
def write_catalog(prepared):
    header = ['#pragma once', '#include <cstddef>', '#include <cstdint>', 'namespace CODY {',
              'enum class CodexAudioCue : uint32_t {', '    None = 0,']
    entries = [entry for entry in prepared if entry[2]["slug"] != BOOT_IDENTITY]
    for _, _, metadata in entries:
        header.append(f"    {metadata['name']} = 0x{metadata['id']:08X}U,")
    header += ['};', 'namespace CodexAudioAssets {', 'struct Clip {', '    CodexAudioCue cue;',
               '    const int16_t *samples;', '    size_t sample_count;', '    uint32_t sample_rate_hz;', '};',
               f'inline constexpr size_t clip_count = {len(entries)};', 'extern const Clip clips[clip_count];',
               '} // namespace CodexAudioAssets', '} // namespace CODY', '']
    source = ['#include "codex_audio_assets.hpp"', 'namespace CODY::CodexAudioAssets {', 'namespace {']
    views = []
    for samples, _, metadata in entries:
        name = f"k{metadata['name']}"
        source.append(f"const int16_t {name}[] = {{")
        for index in range(0, len(samples), 16):
            source.append("    " + ", ".join(str(value) for value in samples[index:index + 16]) + ",")
        source.append("};")
        views.append(f"    {{CodexAudioCue::{metadata['name']}, {name}, {len(samples)}, {metadata['sample_rate_hz']}}},")
    source += ['} // namespace', 'const Clip clips[clip_count] = {', *views, '};', '} // namespace CODY::CodexAudioAssets', '']
    (GENERATED / "codex_audio_assets.hpp").write_text("\n".join(header), encoding="utf-8")
    (GENERATED / "codex_audio_assets.cpp").write_text("\n".join(source), encoding="utf-8")


# No parameters: authors only the selected transformed cues, never modifying the source archive.
# Raises the original source, media, authoring or filesystem error; no substitute sounds.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive-root", type=Path)
    arguments = parser.parse_args()
    selection = load_selection()
    archive = (arguments.archive_root or Path(selection["archive_root"])).resolve(strict=True)
    sources = {}
    for identity, authored in selection["sources"].items():
        path = (archive / authored["path"]).resolve(strict=True)
        if not path.is_relative_to(archive):
            raise ValueError(f"Cody action audio: source escapes archive {identity}")
        digest = source_sha256(path)
        if digest != authored["sha256"]:
            raise ValueError(f"Cody action audio: source identity mismatch for {identity}")
        sources[identity] = {"path": path, "relative_path": authored["path"], "sha256": digest, "format": probe_source(path)}
    prepared = [render_cue(cue, sources) for cue in selection["cues"]]
    GENERATED.mkdir(parents=True, exist_ok=True)
    for _, pcm, entry in prepared:
        (GENERATED / f"{entry['slug']}.pcm16le").write_bytes(pcm)
        path = GENERATED / f"{entry['slug']}.wav"
        write_wav(path, pcm, entry["sample_rate_hz"])
        entry["wav_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    write_catalog(prepared)
    # Listening preview is not a firmware asset and is never played automatically.
    preview_root = ROOT / "artifacts/audio/cody-codex-sfx"
    preview_root.mkdir(parents=True, exist_ok=True)
    audition = bytearray(bytes(16000 * 2))
    offsets = []
    for _, pcm, entry in prepared:
        if entry["sample_rate_hz"] != 16000:
            pcm = run_media_tool("ffmpeg", ["-v", "error", "-nostdin", "-f", "s16le", "-ar", str(entry["sample_rate_hz"]),
                "-ac", "1", "-i", str(GENERATED / f"{entry['slug']}.pcm16le"), "-ar", "16000", "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"])
        offsets.append({"cue": entry["name"], "start_ms": len(audition) * 1000 // 32000, "duration_ms": len(pcm) * 1000 / 32000})
        audition.extend(pcm)
        audition.extend(bytes(16000 * 2))
    write_wav(preview_root / "cody-codex-sfx-audition.wav", audition, 16000)
    manifest = {
        "schema_version": 1, "selection_sha256": selection["selection_sha256"],
        "generator": "scripts/device.ps1 generate -Generator cody-codex-sfx",
        "generator_sha256": text_source_hash(Path(__file__)),
        "contract_sha256": text_source_hash(ROOT / "scripts/common/cody_codex_audio.py"),
        "licensing": selection["licensing"], "channels": 1, "encoding": "PCM16LE",
        "listening_review": "Authored first pass; perceived sound and device playback have not been evaluated.",
        "action_pcm_bytes": sum(len(pcm) for _, pcm, entry in prepared if entry["slug"] != BOOT_IDENTITY),
        "cues": [entry for _, _, entry in prepared],
        "audition": {"file": "artifacts/audio/cody-codex-sfx/cody-codex-sfx-audition.wav", "gap_ms": 1000, "offsets": offsets},
    }
    (GENERATED / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {len(prepared)} Cody cues; boot 1800 ms; action PCM {manifest['action_pcm_bytes']} bytes.")
    print(f"Preview: {preview_root / 'cody-codex-sfx-audition.wav'}")


if __name__ == "__main__":
    main()
