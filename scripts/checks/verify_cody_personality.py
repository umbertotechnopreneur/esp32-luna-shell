"""Check the actual Cody personality controller, raster damage and host-preview parity."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.tests.cody_personality_parity import compare_personality_fixture
from scripts.tests.cody_petting_contract import check_host_petting
from scripts.tests.cody_happiness_contract import check_host_happiness
from scripts.tests.cody_wellbeing_contract import check_host_wellbeing


# root: Checkout containing the exact authored manifests and generated binary assets.
# Raises AssertionError for identity, format, canonical palette, digest or PCM/WAV mismatch.
# Raises the original filesystem or WAV parsing error for missing or malformed artifacts.
def check_asset_contracts(root):
    directory = root / "assets/cody/personality-icons"
    icons = json.loads((directory / "generated/manifest.json").read_text(encoding="utf-8"))
    # Normalize only text line endings; binary sprite/audio hashes remain byte exact.
    palette_data = (root / icons["palette_source"]).read_text(encoding="utf-8").encode("utf-8")
    assert hashlib.sha256(palette_data).hexdigest() == icons["palette_sha256"], "Canonical palette digest differs"
    palette = json.loads(palette_data)["entries"]
    assert [entry["index"] for entry in palette] == list(range(256)), "Canonical palette slots must be stable"
    expected_palette = struct.pack("<256H", *(int(entry["rgb565"], 16) for entry in palette))
    assert (directory / "generated/palette.rgb565.bin").read_bytes() == expected_palette, "Flash RGB565 palette differs"
    assert hashlib.sha256((root / icons["source"]).read_bytes()).hexdigest() == icons["source_sha256"], "Image source differs"
    assert len(icons["icons"]) == 14, "Exactly14 icon identities are required"
    identities = {}
    slugs = set()
    for icon in icons["icons"]:
        assert icon["identity"] not in identities and icon["id"] not in slugs, "Duplicate icon identity"
        identities[icon["identity"]] = icon["enum"]
        slugs.add(icon["id"])
        indices = (directory / f'generated/{icon["id"]}.indices.bin').read_bytes()
        coverage = (directory / f'generated/{icon["id"]}.coverage.bin').read_bytes()
        assert len(indices) == len(coverage) == 4096, "Each64x64 master needs4096 indices and coverage values"
        assert hashlib.sha256(indices).hexdigest() == icon["indices_sha256"], "Index digest differs"
        assert hashlib.sha256(coverage).hexdigest() == icon["coverage_sha256"], "Coverage digest differs"
        assert any(coverage), "Icon must contain visible artwork"
        assert all(not palette[slot]["reserved"] for slot, alpha in zip(indices, coverage) if alpha), "Reserved palette slot used"
    assert set(identities) == set(range(1, 15)), "The14 stable cue IDs are incomplete"
    assert len(set(identities.values())) == 14, "Typed icon names must be unique"
    sounds = json.loads((directory / "audio/manifest.json").read_text(encoding="utf-8"))
    assert (sounds["sample_rate_hz"], sounds["channels"], sounds["bits_per_sample"]) == (16000, 1, 16), "PCM format differs"
    assert len(sounds["clips"]) == 14, "Exactly14 sound identities are required"
    seen = set()
    total_bytes = 0
    audition = bytearray()
    for index, clip in enumerate(sounds["clips"]):
        assert clip["id"] not in seen and identities.get(clip["id"]) == clip["cue"], "Audio cue identity differs"
        seen.add(clip["id"])
        wav_path = directory / "audio" / clip["file"]
        pcm = wav_path.with_suffix(".pcm16le").read_bytes()
        assert len(pcm) == clip["pcm_bytes"] == clip["samples"] * 2, "PCM length differs"
        assert 0 < clip["samples"] <= 5600, "Sound exceeds350ms admission contract"
        assert clip["samples"] * 1000 == clip["duration_ms"] * 16000, "Sound duration differs"
        assert hashlib.sha256(pcm).hexdigest() == clip["sha256_pcm"], "PCM digest differs"
        assert hashlib.sha256(wav_path.read_bytes()).hexdigest() == clip["sha256_wav"], "WAV digest differs"
        with wave.open(str(wav_path), "rb") as source:
            assert (source.getnchannels(), source.getsampwidth(), source.getframerate()) == (1, 2, 16000), "WAV format differs"
            assert source.getnframes() == clip["samples"] and source.readframes(source.getnframes()) == pcm, "WAV payload differs"
        values = struct.unpack(f"<{clip['samples']}h", pcm)
        assert values[0] == values[-1] == 0 and 0 < max(abs(value) for value in values) <= 4200, "Sound endpoints or soft peak differ"
        assert sounds["audition"]["offsets"][index] == {
            "cue": clip["cue"], "start_ms": len(audition) * 1000 // 32000, "duration_ms": clip["duration_ms"]}, "Audition cue offset differs"
        audition.extend(pcm)
        if index + 1 < len(sounds["clips"]):
            audition.extend(bytes(sounds["audition"]["gap_ms"] * 32))
        total_bytes += len(pcm)
    assert seen == set(identities) and total_bytes == sounds["total_pcm_bytes"], "Audio catalog is incomplete"
    with wave.open(str(directory / "audio" / sounds["audition"]["file"]), "rb") as source:
        assert (source.getnchannels(), source.getsampwidth(), source.getframerate()) == (1, 2, 16000), "Audition format differs"
        assert source.readframes(source.getnframes()) == audition, "Audition sequence differs"
    assert len(audition) * 1000 == sounds["audition"]["duration_ms"] * 32000, "Audition duration differs"


# argv: Explicit options forwarded only through scripts/device.ps1 check -Check cody-personality.
# Raises compiler, assertion, asset, dependency or filesystem errors without replacing their cause.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Existing clang++, g++ or cl; no installation is attempted")
    args = parser.parse_args(argv)
    output = ROOT / "artifacts/checks/cody-personality"
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "failed", "scope": "actual native portable sources and simulator parity; no hardware or audio playback"}
    try:
        check_asset_contracts(ROOT)
        compiler = resolve_compiler(args.compiler)
        executable = output / "personality-contract.exe"
        includes = ("scripts/tests/cody_native", "components/CODY2D/include",
                    "components/LUNASHELL_platform/include", "components/CODY_companion/include",
                    "assets/cody/rig/generated", "assets/cody/scenes/generated",
                    "assets/cody/personality-icons/generated", "assets/cody/appearance/jelly-scanline/generated")
        includes += ("assets/cody/personality/generated",)
        sources = ("scripts/tests/cody_native/personality_contract.cpp",
                   "components/CODY2D/src/CODY2D.cpp", "components/CODY2D/src/CODY2D_paths.cpp",
                   "components/CODY2D/src/CODY2D_text.cpp", "components/CODY2D/src/CODY2D_controls.cpp",
                   "components/CODY_companion/src/model/CODY_rig.cpp",
                   "components/CODY_companion/src/model/CODY_response.cpp",
                   "components/CODY_companion/src/model/CODY_petting.cpp",
                   "components/CODY_companion/src/model/CODY_happiness.cpp",
                   "components/CODY_companion/src/model/CODY_wellbeing.cpp",
                   "components/CODY_companion/src/rendering/CODY_dirty_plan.cpp",
                   "components/CODY_companion/src/rendering/CODY_personality.cpp",
                   "assets/cody/personality-icons/generated/personality_icons.cpp")
        report["compiler"] = compiler
        run_stage(compile_command(compiler, includes, sources, executable,
                                 definitions=("CODY_ENABLE_PEARL_JELLY=0",)), output, "compile")
        native = run_stage([str(executable)], output, "native")
        fixture = json.loads(native.stdout)
        compare_personality_fixture(ROOT, fixture)
        report["host_petting_assertions"] = check_host_petting(ROOT)
        report["host_happiness_assertions"] = check_host_happiness(ROOT, output)
        report["host_wellbeing_assertions"] = check_host_wellbeing(ROOT)
        report["status"] = "passed"
        report["native_assertions"] = fixture["assertions"]
        report["host_controller_bytes"] = fixture["host_controller_bytes"]
        report["repeat_slots"] = fixture["repeat_slots"]
        report["repeat_bytes"] = fixture["repeat_bytes"]
        report["parity_frames"] = len(fixture["samples"])
        report["contracts"] = ["400/600 ms fade and 2500 ms hold", "latest target without a queue",
            "semantic Sleep entry and paused release", "finite 360/400 ms following and 4 px radius",
            "irregular frame intervals and complete-window gaps", "nonfatal blocked layout and scene permission",
            "typed size mapping and Micro exclusion", "invalid input preserves controller state",
            "alpha-only and departing icon damage", "compact raster parity and stride guards",
            "caress zones, gesture rejection, one trigger and cooldown",
            "independent host-preview caress scenarios (no native gesture trace parity claim)",
            "native happiness gain, elapsed-minute decay, bounded phase and domain rejection",
            "portable heart/charging selection and urgent admitted low-battery smooth transitions",
            "generated typed event identities, low-battery hysteresis and fixed per-cue repeat history",
            "dormant pure wellbeing session lifecycle, independent reminders, snooze and fatigue acknowledgment",
            "independent host happiness, monotonic clock, strict authoring and generated provenance scenarios",
            "native/Python controller, transform, alpha and admission parity",
            "14 fixed-palette icon hashes/identity and14 PCM/WAV formats/hashes/audition offsets"]
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Cody personality contracts passed. Report: {output / 'report.json'}")


if __name__ == "__main__":
    main()
