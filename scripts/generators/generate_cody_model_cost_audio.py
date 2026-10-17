# VBWR B
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
# Copyright (c) 2026 Umberto Giacobbi
# VBWR E

"""Prepare the pinned CC0 model-price plink and its five-note listening preview."""
from array import array
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets/cody/audio/model-cost"


# path: Exact WAV destination under the asset or artifact owner.
# pcm: Validated mono little-endian PCM16 payload.
# rate: Authored sample clock in Hz.
# Exception: Preserves output and WAV serialization failures.
def write_wave(path, pcm, rate):
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        output.writeframes(pcm)


# selection: Pinned authoring record for one short source and its digital level.
# Exception: Rejects changed sources, missing ffmpeg, decode errors and silent/oversized audio.
def render(selection):
    source = ASSETS / selection["source_file"]
    if hashlib.sha256(source.read_bytes()).hexdigest() != selection["source_sha256"]:
        raise ValueError("Model cost audio: source hash differs from the selected CC0 recording")
    executable = shutil.which("ffmpeg")
    if executable is None:
        raise FileNotFoundError("Model cost audio requires an existing ffmpeg; nothing is installed")
    rate = selection["sample_rate_hz"]
    duration = selection["maximum_duration_ms"] / 1000
    filters = f"atrim=start=0:duration={duration:.3f},highpass=f=150,lowpass=f=5000"
    result = subprocess.run([executable, "-hide_banner", "-loglevel", "error", "-nostdin",
        "-i", str(source), "-map", "0:a:0", "-af", filters, "-ac", "1", "-ar", str(rate),
        "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"],
        capture_output=True, timeout=30, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"Model cost audio decode failed ({result.returncode}): "
                           f"{result.stderr.decode('utf-8', errors='replace').strip()}")
    samples = array("h")
    samples.frombytes(result.stdout)
    if sys.byteorder != "little":
        samples.byteswap()
    if not rate // 50 <= len(samples) <= round(rate * duration):
        raise ValueError(f"Model cost audio decoded length is invalid: {len(samples)}")
    # Short ramps avoid clicks while preserving the source's original pitched attack.
    attack, release = round(rate * .002), round(rate * .014)
    shaped = [float(value) for value in samples]
    for index in range(attack):
        shaped[index] *= math.sin(math.pi * index / (2 * (attack - 1))) ** 2
    for index in range(release):
        shaped[len(shaped) - release + index] *= math.cos(math.pi * index / (2 * (release - 1))) ** 2
    peak = max(abs(value) for value in shaped)
    if peak <= 1:
        raise ValueError("Model cost audio selected excerpt is silent")
    gain = 32767 * 10 ** (selection["target_peak_dbfs"] / 20) / peak
    rendered = array("h", (round(value * gain) for value in shaped))
    rendered[0] = rendered[-1] = 0
    pcm = array("h", rendered)
    if sys.byteorder != "little":
        pcm.byteswap()
    return rendered, pcm.tobytes(), filters


# No parameters: public device.ps1 generator owns fixed source and artifact locations.
# Exception: Preserves source, authoring, codec and write failures without substitute sounds.
def main():
    selection = json.loads((ASSETS / "source-selection.json").read_text(encoding="utf-8"))
    if (selection["schema_version"] != 1 or selection["sample_rate_hz"] != 16000 or
            selection["repeat_count"] != 5 or selection["repeat_gap_ms"] != 100 or
            selection["maximum_duration_ms"] != 220 or selection["target_peak_dbfs"] != -18.0):
        raise ValueError("Model cost audio selection differs from the authored playback contract")
    samples, pcm, filters = render(selection)
    rate = selection["sample_rate_hz"]
    generated = ASSETS / "generated"
    generated.mkdir(parents=True, exist_ok=True)
    header = ["#pragma once", "#include <cstddef>", "#include <cstdint>",
        "namespace CODY::ModelCostAudioAssets {",
        f"inline constexpr size_t sample_count = {len(samples)};",
        f"inline constexpr uint32_t sample_rate_hz = {rate};",
        "extern const int16_t samples[sample_count];",
        "} // namespace CODY::ModelCostAudioAssets", ""]
    source = ['#include "model_cost_audio.hpp"', "namespace CODY::ModelCostAudioAssets {",
              "const int16_t samples[sample_count] = {"]
    for offset in range(0, len(samples), 16):
        source.append("    " + ", ".join(str(value) for value in samples[offset:offset + 16]) + ",")
    source.extend(["};", "} // namespace CODY::ModelCostAudioAssets", ""])
    (generated / "model_cost_audio.hpp").write_text("\n".join(header), encoding="utf-8")
    (generated / "model_cost_audio.cpp").write_text("\n".join(source), encoding="utf-8")
    (generated / "model-cost.pcm16le").write_bytes(pcm)
    write_wave(generated / "model-cost.wav", pcm, rate)
    preview = ROOT / "artifacts/audio/cody-model-cost"
    preview.mkdir(parents=True, exist_ok=True)
    gap = bytes(2 * round(rate * selection["repeat_gap_ms"] / 1000))
    sequence = gap.join([pcm] * selection["repeat_count"])
    write_wave(preview / "model-cost-five-plinks.wav", sequence, rate)
    manifest = {"source": selection, "ffmpeg_filters": filters,
        "sample_count": len(samples), "pcm_bytes": len(pcm), "sample_rate_hz": rate,
        "duration_ms": len(samples) * 1000 / rate,
        "pcm_sha256": hashlib.sha256(pcm).hexdigest(),
        "peak_dbfs": 20 * math.log10(max(abs(value) for value in samples) / 32768),
        "fade_in_ms": 2, "fade_out_ms": 14,
        "preview_duration_ms": len(sequence) * 500 / rate,
        "firmware_payload": "One PCM clip; repetitions and gaps belong to the existing audio owner",
        "physical_speaker_listening_verified": False}
    (generated / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Model cost audio prepared: {len(pcm)} flash bytes, {len(samples) * 1000 / rate:.1f} ms; "
          f"preview {preview / 'model-cost-five-plinks.wav'}")


if __name__ == "__main__":
    main()
