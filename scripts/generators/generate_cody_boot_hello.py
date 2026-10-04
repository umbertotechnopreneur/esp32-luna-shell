"""Prepare private boot-greeting auditions with ffmpeg, not firmware assets."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_boot_audio import (  # noqa: E402
    SAMPLE_RATE, load_selection, render_selected_greeting, write_wav,
)

OUTPUT = ROOT / "artifacts/audio/cody-boot-hello"


# selection: Validated catalog carrying an independent original-chime identity.
# Raises RuntimeError for corrupt runtime PCM/metadata or an unrecognized original prefix.
# Raises original filesystem or JSON errors; runtime assets are never changed.
def load_original_chime(selection):
    generated = ROOT / "assets/cody/runtime/generated"
    existing_pcm = (generated / "cody-eyes_open.pcm16le").read_bytes()
    metadata = json.loads((generated / "manifest.json").read_text(encoding="utf-8"))["cues"]["eyes_open"]
    if (type(metadata["sample_rate_hz"]) is not int or metadata["sample_rate_hz"] != SAMPLE_RATE or
            type(metadata["pcm_bytes"]) is not int or len(existing_pcm) != metadata["pcm_bytes"] or
            len(existing_pcm) % 2 or hashlib.sha256(existing_pcm).hexdigest() != metadata["sha256"]):
        raise RuntimeError("Existing boot PCM does not match its runtime manifest")
    # Composed firmware explicitly declares its original prefix length. An
    # original-only cue is itself the whole prefix. Both independently match
    # the pinned original, so a greeting is never appended twice.
    chime_bytes = metadata["chime_pcm_bytes"] if "chime_pcm_bytes" in metadata else len(existing_pcm)
    if type(chime_bytes) is not int or chime_bytes <= 0 or chime_bytes % 2 or chime_bytes > len(existing_pcm):
        raise RuntimeError("Existing boot cue declares an invalid original-chime prefix")
    chime = existing_pcm[:chime_bytes]
    chime_hash = hashlib.sha256(chime).hexdigest()
    if chime_hash != selection["original_chime_sha256"]:
        raise RuntimeError("Boot preview requires the independently pinned original synthesized chime")
    if "chime_sha256" in metadata and metadata["chime_sha256"] != chime_hash:
        raise RuntimeError("Existing boot cue's declared original-chime hash is inconsistent")
    return chime


# archive_root: Optional explicitly selected external archive directory, never modified.
# Raises original catalog, filesystem and media errors; no firmware asset is replaced.
def generate(archive_root=None):
    selection = load_selection()
    chime = load_original_chime(selection)
    gap_ms = selection["chime_gap_ms"]
    gap = bytes(round(gap_ms * SAMPLE_RATE / 1000) * 2)
    manifest = {
        "schema_version": 1, "selection_sha256": selection["selection_sha256"],
        "format": {"sample_rate_hz": SAMPLE_RATE, "channels": 1, "encoding": "PCM16LE"},
        "licensing": selection["licensing"],
        "listening_review": "Private alternatives; no English intelligibility or device acoustics claim.",
        "runtime_integration": "Previews only; this generator never replaces a firmware asset.",
        "chime_sha256": hashlib.sha256(chime).hexdigest(),
        "chime_pcm_bytes": len(chime), "chime_gap_ms": gap_ms, "cues": [],
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cody-boot-hello-", dir=OUTPUT.parent) as staging:
        stage = Path(staging)
        for cue in selection["cues"]:
            pcm, provenance = render_selected_greeting(selection, cue["id"], archive_root)
            name = cue["id"]
            write_wav(stage / f"{name}.wav", pcm)
            (stage / f"{name}.pcm16le").write_bytes(pcm)
            composite = chime + gap + pcm
            write_wav(stage / f"{name}-boot.wav", composite)
            (stage / f"{name}-boot.pcm16le").write_bytes(composite)
            manifest["cues"].append({
                **provenance, "boot_pcm_bytes": len(composite),
                "boot_duration_seconds": len(composite) / 2 / SAMPLE_RATE,
                "boot_pcm_sha256": hashlib.sha256(composite).hexdigest(),
            })
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        OUTPUT.mkdir(parents=True, exist_ok=True)
        for candidate in stage.iterdir():
            os.replace(candidate, OUTPUT / candidate.name)
    print(f"Prepared {len(selection['cues'])} private greeting auditions: {OUTPUT}")
    print("SFX perpetual commercial-use licence is owner-confirmed; device playback remains unverified.")


# No parameters: expose local audio preparation through the public device dispatcher.
# Raises original authoring, filesystem and media-tool errors without substituting a cue.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive-root", type=Path)
    arguments = parser.parse_args()
    generate(arguments.archive_root)


if __name__ == "__main__":
    main()
