"""Package pinned eSpeak data for the explicit SD or English-only flash variant."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/tts/espeak-ng"


# backend: Explicit sd or flash storage selection.
# output: Destination inside artifacts, never a device/card path.
# Raises ValueError, OSError or JSONDecodeError for invalid input or package integrity.
def generate(backend: str, output: Path) -> None:
    if backend not in ("sd", "flash"):
        raise ValueError(f"Unsupported TTS backend: {backend}")
    output = output.resolve()
    output.relative_to((ROOT / "artifacts").resolve())
    if output == (ROOT / "artifacts").resolve():
        raise ValueError("TTS output must be a dedicated artifact directory")
    manifest = json.loads((SOURCE / "manifest.json").read_text(encoding="utf-8"))
    if manifest["schema_version"] != 1:
        raise ValueError("Unsupported TTS package manifest")
    expected = {"phondata": "Common", "phontab": "Common", "phonindex": "Common",
                "intonations": "Common", "en_dict": "English", "lang/gmw/en": "English",
                "it_dict": "Italian", "lang/roa/it": "Italian", "cmn_dict": "Mandarin",
                "lang/sit/cmn": "Mandarin", "yue_dict": "Cantonese", "lang/sit/yue": "Cantonese"}
    if manifest["source_revision"] != "93b2651d324874e1e00077af008d8ab83ab32e73":
        raise ValueError("Unsupported TTS source revision")
    if len(manifest["files"]) != len(expected):
        raise ValueError("TTS manifest must contain the reviewed twelve-file package")
    selected = []
    seen = set()
    for entry in manifest["files"]:
        name = entry["path"]
        if name in seen or Path(name).is_absolute() or ".." in Path(name).parts:
            raise ValueError(f"Invalid/duplicate TTS path: {name}")
        seen.add(name)
        if name not in expected or entry["language"] != expected[name]:
            raise ValueError(f"Unsupported TTS path/language assignment: {name}")
        if backend == "flash" and entry["language"] not in ("Common", "English"):
            continue
        data = (SOURCE / name).read_bytes()
        if len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"TTS source integrity failure: {name}")
        selected.append((entry, data))
    if backend not in ("sd", "flash"):
        raise ValueError(f"Unsupported TTS backend: {backend}")
    output.mkdir(parents=True, exist_ok=True)
    lines = ['#include "asset_registry.h"', ""]
    if backend == "flash":
        for index, (_, data) in enumerate(selected):
            lines.append(f"alignas(4) static const uint8_t data_{index}[] = {{")
            for offset in range(0, len(data), 24):
                lines.append("    " + ",".join(str(value) for value in data[offset:offset + 24]) + ",")
            lines.append("};")
    lines.append('extern "C" {')
    lines.append("luna_tts_asset_t luna_tts_assets[] = {")
    for index, (entry, _) in enumerate(selected):
        pointer = f"data_{index}" if backend == "flash" else "nullptr"
        digest = ",".join(str(value) for value in bytes.fromhex(entry["sha256"]))
        lines.append(f'    {{"{entry["path"]}", {pointer}, {entry["size"]}, {{{digest}}}}},')
    lines.extend(["};", f"extern const size_t luna_tts_asset_count = {len(selected)};", "}", ""])
    # Each mode has an isolated destination; no all-language array can leak into flash.
    if backend == "sd":
        package = output / "sd-root/LUNASHELL/tts/espeak-ng-data"
        for entry, _ in selected:
            destination = package / entry["path"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(SOURCE / entry["path"], destination)
        (package / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    # Publish the build output only after the complete selected package exists.
    (output / "tts_assets.cpp").write_text("\n".join(lines), encoding="utf-8")


# No parameters: public invocation goes through scripts/device.ps1 generate.
# Raises package, path and I/O failures; never invokes firmware or touches a card.
def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", required=True, choices=("sd", "flash"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or ROOT / "artifacts/tts" / args.backend
    generate(args.backend, output)
    print(f"TTS {args.backend} artifacts: {output.resolve()}")


if __name__ == "__main__":
    main()
