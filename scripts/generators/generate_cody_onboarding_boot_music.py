"""Compose the original Cody onboarding startup melody as bounded mono audio."""

import math
from pathlib import Path
import struct
import wave

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "cody-device/assets/cody/audio/onboarding/generated"
RATE = 16000
DURATION_SECONDS = 4.6


# No parameters: synthesizes a warm original melody and writes matching PCM/WAV resources.
# Exception: Propagates resource and filesystem failures.
def main():
    count = int(RATE * DURATION_SECONDS)
    samples = [0.0] * count
    melody = ((0.00, 523.25, 0.92), (0.38, 659.25, 0.92), (0.76, 783.99, 0.92),
              (1.14, 987.77, 1.02), (1.66, 880.00, 0.88), (2.05, 783.99, 0.92),
              (2.48, 1046.50, 1.52))
    for start, frequency, duration in melody:
        first = int(start * RATE)
        last = min(count, first + int(duration * RATE))
        for index in range(first, last):
            t = (index - first) / RATE
            attack = min(1.0, t / 0.025)
            release = min(1.0, (duration - t) / 0.28)
            envelope = attack * release * math.exp(-2.5 * t / duration)
            phase = 2 * math.pi * frequency * t
            samples[index] += envelope * (math.sin(phase) + 0.22 * math.sin(2 * phase) +
                                           0.07 * math.sin(3 * phase))
    # A soft low chord gives the melody a sustained, friendly foundation.
    for frequency in (130.81, 164.81, 196.00):
        for index in range(count):
            t = index / RATE
            envelope = min(1.0, t / 0.4) * min(1.0, (DURATION_SECONDS - t) / 1.1)
            samples[index] += 0.095 * envelope * math.sin(2 * math.pi * frequency * t)
    delay = int(0.23 * RATE)
    for index in range(delay, count):
        samples[index] += 0.16 * samples[index - delay]
    peak = max(abs(value) for value in samples)
    gain = 0.20 / peak
    pcm = b"".join(struct.pack("<h", round(value * gain * 32767)) for value in samples)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "boot-melody.pcm16le").write_bytes(pcm)
    with wave.open(str(OUTPUT / "boot-melody.wav"), "wb") as preview:
        preview.setnchannels(1)
        preview.setsampwidth(2)
        preview.setframerate(RATE)
        preview.writeframes(pcm)
    print(f"Generated Cody boot melody: {DURATION_SECONDS:.1f} s, {RATE} Hz, mono")


if __name__ == "__main__":
    main()
