# Project: ESP32 Luna Shell; Creator: Umberto Giacobbi
# Copyright (c) 2026 Umberto Giacobbi
"""Generate reusable Muse PCM cues from the owner's sound library."""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.generators.generate_cody_codex_sfx import render_cue, write_wav
from scripts.generators.generate_cody_gentle_sfx import probe_source, source_sha256

ASSETS = ROOT / "cody-device/assets/cody/audio/pet"


# No parameters: read the authored selection and produce only the pet's audio assets.
# Raises ValueError, OSError, or RuntimeError for invalid sources or rendering errors.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive-root", type=Path)
    args = parser.parse_args()
    selection = json.loads((ASSETS / "source-selection.json").read_text(encoding="utf-8"))
    archive = (args.archive_root or Path(selection["archive_root"])).resolve(strict=True)
    sources = {}
    for identity, authored in selection["sources"].items():
        path = (archive / authored["path"]).resolve(strict=True)
        if not path.is_relative_to(archive):
            raise ValueError(f"Pet sound source escapes archive: {identity}")
        digest = source_sha256(path)
        if digest.lower() != authored["sha256"].lower():
            raise ValueError(f"Pet sound source changed: {identity}")
        sources[identity] = {"path": path, "relative_path": authored["path"],
                             "sha256": digest, "format": probe_source(path)}
    prepared = [render_cue(cue, sources) for cue in selection["cues"]]
    output = ASSETS / "generated"
    output.mkdir(parents=True, exist_ok=True)
    for _, pcm, entry in prepared:
        (output / f"{entry['slug']}.pcm16le").write_bytes(pcm)
        write_wav(output / f"{entry['slug']}.wav", pcm, entry["sample_rate_hz"])
    manifest = {"encoding": "PCM16LE", "channels": 1, "sample_rate_hz": 16000,
                "generator": "scripts/device.ps1 generate -Generator cody-pet-sfx",
                "cues": [entry for _, _, entry in prepared]}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(prepared)} reusable Muse sound cues.")


if __name__ == "__main__":
    main()
