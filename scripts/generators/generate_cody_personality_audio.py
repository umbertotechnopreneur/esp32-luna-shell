"""Create original, deterministic Cody symbol micro-cues from analytic synthesis."""

from pathlib import Path
import hashlib
import json
import math
import struct
import wave

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "assets/cody/personality-icons/audio"
RATE = 16000

# Explicit cue identities match CODY_personality.hpp; ordering has no semantic role.
CATALOG = (
    ("sleep", "Sleep", 1, 220, "breath", "A soft rounded exhale with a falling airy tail."),
    ("fear", "Fear", 2, 180, "shiver", "A tiny rising tremble, intentionally gentle rather than alarming."),
    ("joy", "Joy", 3, 280, "sparkle", "Three light ascending bell sparkles."),
    ("hunger", "Hunger", 4, 240, "tummy", "A rounded two-pulse cartoon tummy wobble."),
    ("thirst", "Thirst", 5, 180, "drop", "One small descending water plip."),
    ("heat", "Heat", 6, 220, "heat", "A soft warm air puff with a descending pitch."),
    ("cold", "Cold", 7, 240, "cold", "Three faint icy glass ticks."),
    ("low-battery", "LowBattery", 8, 250, "low", "Two soft descending rounded notes, not a critical alarm."),
    ("charging", "Charging", 9, 230, "charge", "A restrained upward energy glint."),
    ("no-wifi", "NoWifi", 10, 240, "offline", "Two light broken-link notes with a short silent gap."),
    ("curiosity", "Curiosity", 11, 170, "plip", "An upward questioning plip."),
    ("confusion", "Confusion", 12, 280, "wobble", "A small elastic pitch wobble."),
    ("affection", "Affection", 13, 300, "heart", "A warm soft two-note heart chime."),
    ("frustration", "Frustration", 14, 200, "huff", "A brief rounded puff and low elastic grumble."),
)

# Set a named cue to False and regenerate to silence that symbol without removing its asset.
ENABLED_CUES = {
    "Sleep": True, "Fear": True, "Joy": True, "Hunger": True, "Thirst": True,
    "Heat": True, "Cold": True, "LowBattery": True, "Charging": True, "NoWifi": True,
    "Curiosity": True, "Confusion": True, "Affection": True, "Frustration": True,
}


# time: Seconds since one note began.
# duration: Positive note length in seconds.
# frequency: Starting frequency in hertz.
# glide: Total frequency change over this note.
# brightness: Relative amplitude of one soft second harmonic.
def note(time, duration, frequency, glide=0.0, brightness=0.15):
    if time <= 0 or time >= duration:
        return 0.0
    progress = time / duration
    attack = min(time / 0.009, 1.0)
    tail = (1.0 - progress) ** 2
    phase = math.tau * (frequency * time + glide * time * time / (2.0 * duration))
    return attack * tail * (math.sin(phase) + brightness * math.sin(phase * 2.0))


# kind: Named synthesis recipe, independent of catalog ordering.
# duration_ms: Exact authored mono duration.
# seed: Stable cue identity used only for original deterministic noise.
def synthesize(kind, duration_ms, seed):
    count = RATE * duration_ms // 1000
    duration = count / RATE
    noise_state = 0x434F4459 ^ seed
    low_noise = 0.0
    result = []
    for index in range(count):
        time = index / RATE
        progress = index / (count - 1)
        noise_state = (1664525 * noise_state + 1013904223) & 0xFFFFFFFF
        noise = ((noise_state >> 8) / 8388607.5) - 1.0
        low_noise += (noise - low_noise) * 0.10
        air = low_noise * math.sin(math.pi * progress) ** 2
        if kind == "breath":
            value = air * 2.1 + note(time, duration, 340, -110, 0.0) * 0.08
        elif kind == "shiver":
            value = note(time, duration, 540, 220) * (0.70 + 0.30 * math.sin(math.tau * 26 * time))
        elif kind == "sparkle":
            value = sum(note(time - start, duration - start, frequency, 25, 0.23) * gain
                        for start, frequency, gain in ((0, 780, .55), (.055, 1040, .55), (.115, 1320, .45)))
        elif kind == "tummy":
            value = note(time, .15, 145, -38, .35) + note(time - .11, .13, 180, -65, .25) * .7
        elif kind == "drop":
            value = note(time, duration, 1150, -830, .12)
        elif kind == "heat":
            value = air * 1.5 + note(time, duration, 390, -180, .05) * .3
        elif kind == "cold":
            value = sum(note(time - start, .10, frequency, -90, .25) * .7
                        for start, frequency in ((0, 1450), (.07, 1700), (.14, 1530)))
        elif kind == "low":
            value = note(time, .105, 440, -15, .08) * .7 + note(time - .135, .115, 330, -20, .08) * .7
        elif kind == "charge":
            value = note(time, duration, 510, 610, .18) * .7 + note(time - .10, .13, 1190, 25, .05) * .25
        elif kind == "offline":
            value = note(time, .085, 680, -80, .12) * .7 + note(time - .145, .095, 480, -60, .12) * .65
        elif kind == "plip":
            value = note(time, duration, 540, 590, .09)
        elif kind == "wobble":
            phase = math.tau * (420 * time + 2.4 * math.sin(math.tau * 7 * time))
            value = math.sin(phase) * math.sin(math.pi * progress) ** 2 * .65
        elif kind == "heart":
            value = note(time, duration, 523.25, 0, .08) * .65 + note(time - .095, .205, 659.25, 0, .08) * .5
        elif kind == "huff":
            value = air * 1.5 + note(time, duration, 210, -75, .3) * .5
        else:
            raise ValueError(f"Unknown original synthesis recipe: {kind}")
        # A final 5 ms cosine gate gives every authored clip an exact silent endpoint.
        edge = min(time / .005, (duration - 1 / RATE - time) / .005, 1.0)
        gate = .5 - .5 * math.cos(math.pi * max(0.0, edge))
        result.append(value * gate)
    peak = max(abs(sample) for sample in result)
    # Fixed -18 dBFS peak keeps the first pass deliberately discreet at saved codec volume.
    gain = (32767 * 10 ** (-18 / 20)) / peak
    return [round(sample * gain) for sample in result]


# No parameters: writes the reproducible WAV, raw PCM and typed flash catalog.
def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    entries = []
    arrays = []
    views = []
    audition = bytearray()
    audition_offsets = []
    for slug, identity, stable_id, duration_ms, recipe, description in CATALOG:
        samples = synthesize(recipe, duration_ms, stable_id)
        pcm = struct.pack(f"<{len(samples)}h", *samples)
        (OUTPUT / f"{slug}.pcm16le").write_bytes(pcm)
        with wave.open(str(OUTPUT / f"{slug}.wav"), "wb") as output:
            output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
            output.writeframes(pcm)
        name = f"k{identity}"
        rows = [", ".join(str(value) for value in samples[index:index + 16])
                for index in range(0, len(samples), 16)]
        arrays.append(f"const int16_t {name}[] = {{\n    " + ",\n    ".join(rows) + "\n};")
        enabled = "true" if ENABLED_CUES[identity] else "false"
        views.append(f"    {{PersonalityCue::{identity}, {name}, {len(samples)}, {RATE}, {enabled}}},")
        audition_offsets.append({"cue": identity, "start_ms": len(audition) * 1000 // (RATE * 2),
                                 "duration_ms": duration_ms})
        audition.extend(pcm)
        if len(audition_offsets) < len(CATALOG):
            audition.extend(bytes(RATE * 700 // 1000 * 2))
        entries.append({"id": stable_id, "cue": identity, "file": f"{slug}.wav", "recipe": recipe,
                        "description": description, "duration_ms": duration_ms, "samples": len(samples),
                        "pcm_bytes": len(pcm), "peak_dbfs": -18, "enabled": ENABLED_CUES[identity],
                        "sha256_pcm": hashlib.sha256(pcm).hexdigest(),
                        "sha256_wav": hashlib.sha256((OUTPUT / f"{slug}.wav").read_bytes()).hexdigest()})
    header = '''#pragma once
#include <cstddef>
#include <cstdint>
#include "CODY_personality.hpp"
namespace CODY::PersonalityAudioAssets {
struct Clip {
    PersonalityCue cue;
    const int16_t *samples;
    size_t sample_count;
    uint32_t sample_rate_hz;
    bool enabled;
};
inline constexpr size_t clip_count = 14;
extern const Clip clips[clip_count];
} // namespace CODY::PersonalityAudioAssets
'''
    source = '#include "personality_audio.hpp"\nnamespace CODY::PersonalityAudioAssets {\nnamespace {\n'
    source += "\n".join(arrays) + "\n} // namespace\nconst Clip clips[clip_count] = {\n"
    source += "\n".join(views) + "\n};\n} // namespace CODY::PersonalityAudioAssets\n"
    (OUTPUT / "personality_audio.hpp").write_text(header, encoding="utf-8")
    (OUTPUT / "personality_audio.cpp").write_text(source, encoding="utf-8")
    with wave.open(str(OUTPUT / "audition-all-14.wav"), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(audition)
    manifest = {"version": 1, "provenance": "Original analytic synthesis; no external recordings, models or samples.",
                "sample_rate_hz": RATE, "channels": 1, "bits_per_sample": 16,
                "generator": "scripts/device.ps1 generate -Generator cody-personality-audio",
                "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "listening_review": "Not performed; first authored pass for later listening and refinement.",
                "total_pcm_bytes": sum(entry["pcm_bytes"] for entry in entries), "clips": entries,
                "audition": {"file": "audition-all-14.wav", "gap_ms": 700,
                             "duration_ms": len(audition) * 1000 // (RATE * 2), "offsets": audition_offsets}}
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(entries)} original PCM16/WAV symbol cues; {manifest['total_pcm_bytes']} bytes at {RATE} Hz.")


if __name__ == "__main__":
    main()
