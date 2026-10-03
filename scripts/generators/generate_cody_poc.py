"""Generate strict PoC settings and two original, compact synthesized boot cues."""
from pathlib import Path
import hashlib
import json
import math
import struct
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_rig_contract import load_contract
from scripts.common.cody_poc_contract import load_settings
from scripts.common.asset_provenance import text_source_hash


# settings: Validated explicit cue format and durations.
# cue: One stable identity: audio_ready or eyes_open.
# Returns original PCM, not a recording or reconstruction of another startup sound.
def synthesize(settings, cue):
    audio = settings["audio"]
    duration_key = "ready_duration_seconds" if cue == "audio_ready" else "eyes_open_duration_seconds"
    duration = audio[duration_key]
    rate = audio["sample_rate_hz"]
    values = []
    for index in range(round(duration * rate)):
        time = index / rate
        progress = time / duration
        if cue == "audio_ready":
            envelope = math.sin(math.pi * progress) ** 2 * math.exp(-2.5 * progress)
            value = envelope * (math.sin(math.tau * 740 * time) + .3 * math.sin(math.tau * 1110 * time))
        else:
            # A warm original D major/add-nine voicing, with detuned bowed partials.
            attack = min(1, time / .10) ** 2
            release = min(1, (duration - time) / .22) ** 2
            envelope = attack * release * (.12 + .88 * min(1, progress / .78) ** 1.5)
            value = 0
            for voice, frequency in enumerate((293.6648, 369.9944, 440.0, 587.3295, 659.2551)):
                vibrato = .0025 * math.sin(math.tau * (4.4 + voice * .13) * time)
                for detune in (-.0018, .0018):
                    phase = math.tau * frequency * (1 + detune) * time + vibrato + voice * .41
                    for harmonic in range(1, 9):
                        value += envelope * math.sin(harmonic * phase) / harmonic ** 1.8
        values.append(value)
    peak = max(abs(value) for value in values)
    scale = .38 * 32767 / peak
    return b"".join(struct.pack("<h", round(value * scale)) for value in values)


# No parameters: generate core assets in flash-owned source folders; reports stay in artifacts.
# Raises original authoring, contract and filesystem errors.
def main():
    source = ROOT / "assets/cody/poc.yaml"
    settings = load_settings(source)
    rig, _ = load_contract()
    generated = ROOT / "assets/cody/poc/generated"
    generated.mkdir(parents=True, exist_ok=True)
    names = {"breathe": "Breathe", "contour_slow": "ContourSlow", "contour_fast": "ContourFast", "sleepy": "Sleepy", "awake": "Awake"}
    lines = ['#pragma once', '#include "CODY_demo.hpp"', 'namespace CODY::PocData {']
    lines.append(f"inline constexpr bool autostart = {str(settings['demo']['autostart']).lower()};")
    for name in ("execution_timings", "mirror_to_sd_when_available", "report_interval_ms", "frame_budget_us"):
        value = settings["diagnostics"][name]
        cpp = str(value).lower() if isinstance(value, bool) else str(value)
        lines.append(f"inline constexpr auto {name} = {cpp};")
    for name in ("ready_volume_percent", "eyes_open_volume_percent", "sample_rate_hz"):
        lines.append(f"inline constexpr int {name} = {settings['audio'][name]};")
    cycle_ms = sum(round(step["duration_seconds"] * 1000) for step in settings["demo"]["steps"])
    lines.append(f"inline constexpr uint32_t cycle_ms = {cycle_ms}U;")
    lines.append('inline constexpr DemoStep steps[] = {')
    for step in settings["demo"]["steps"]:
        presets = rig["boot_channels"] | rig["expression_presets"]
        if step["face"] not in presets:
            raise ValueError(f"Unknown named face target: {step['face']}")
        channels = ', '.join(f"{float(value):.9g}f" if float(value) % 1 else f"{int(value)}.0f" for value in presets[step["face"]])
        lines.append(f"    {{DemoPhase::{names[step['id']]}, {round(step['duration_seconds'] * 1000)}U, {{{{{channels}}}}}, {float(step['amplitude'])}f, {float(step['speed'])}f}},")
    lines.extend(['};', '} // namespace CODY::PocData', ''])
    (generated / "poc_settings.hpp").write_text('\n'.join(lines), encoding="utf-8")
    manifest = {"schema": settings["schema"], "source_sha256": text_source_hash(source), "cues": {}}
    for cue in ("audio_ready", "eyes_open"):
        pcm = synthesize(settings, cue)
        raw = generated / f"cody-{cue}.pcm16le"
        raw.write_bytes(pcm)
        wav_path = generated / f"cody-{cue}.wav"
        with wave.open(str(wav_path), 'wb') as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(settings['audio']['sample_rate_hz'])
            output.writeframes(pcm)
        manifest['cues'][cue] = {
            "pcm_bytes": len(pcm),
            "sample_rate_hz": settings['audio']['sample_rate_hz'],
            "duration_seconds": len(pcm) / 2 / settings['audio']['sample_rate_hz'],
            "sha256": hashlib.sha256(pcm).hexdigest(),
            "origin": "original procedural synthesis",
        }
    (generated / "manifest.json").write_text(json.dumps(manifest, indent=2) + '\n', encoding="utf-8")
    print(f"Generated PoC settings and original cues: {generated}")


if __name__ == '__main__':
    main()
