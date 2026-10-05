"""Explicit host checks for native response/demo/statistics and authored boot cues."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.common.cody_runtime_contract import demo_targets, load_settings
from scripts.common.cody_response import (
    InputOrigin, MicrophoneSample, PlaybackSample, PowerSample, ResponseInputs,
    SampleHealth, SampleStamp, advance_response, compose_response, make_response_state,
    BodyTouchSample, EyeId, EyeTapSample, hit_test_eye,
)
from scripts.common.cody_rig_contract import contour_defaults, evaluate, load_contract
from scripts.common.cody_rig_types import ContourControl, FaceChannel
from scripts.common.asset_provenance import text_source_hash
from scripts.common.cody_typewriter_audio_contract import check_typewriter_audio_source
from scripts.common.cody_boot_audio import load_selection
from scripts.generators.generate_cody_runtime_assets import synthesize


# Validate failure visibility and parked time independently from the parity fixture.
# Raises AssertionError if a stale or invalid source influences the model as current.
def check_boundaries():
    rig, _ = load_contract()
    config = rig["response"]
    state = make_response_state(config)
    current = SampleStamp(SampleHealth.CURRENT, InputOrigin.SYNTHETIC, 1_000_000, 0)
    invalid = (
        MicrophoneSample(current, 0, (1, 1), (1, 1)),
        MicrophoneSample(current, 3, (1, 1), (1, 1)),
        MicrophoneSample(current, 2, (3, 1), (2, 1)),
        MicrophoneSample(current, 2, (32769, 1), (32769, 1)),
    )
    for sample in invalid:
        try:
            advance_response(config, state, ResponseInputs(microphone=sample), 100, 1_000_000)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid microphone accepted")
        assert state.idle_seconds == 0 and state.attention == 0
    microphone = MicrophoneSample(current, 2, (6000, 3000), (6000, 3000))
    listening = advance_response(config, state, ResponseInputs(microphone=microphone), 100, 1_000_000)
    assert listening.attention > 0
    playing = advance_response(config, state, ResponseInputs(microphone=microphone,
        playback=PlaybackSample(current, True, 5000)), 100, 1_000_000)
    assert playing.attention == 0 and playing.speaking > 0
    aged = advance_response(config, listening, ResponseInputs(microphone=microphone), 100, 4_000_000)
    assert aged.microphone_status.health == SampleHealth.STALE and aged.attention < listening.attention
    parked = advance_response(config, state, ResponseInputs(), 60_000, 60_000_000, False)
    assert parked.idle_seconds == 0 and parked.yawn_weight == 0
    scene = evaluate(rig, rig["boot_channels"]["ready"], (245, 221, 135))
    radius = config["touch"]["eye_hit_radius"]
    for eye, name in ((EyeId.LEFT, "eye.left"), (EyeId.RIGHT, "eye.right")):
        assert hit_test_eye(scene, scene["world"][name], radius) == eye
    assert hit_test_eye(scene, (20, 20), radius) == EyeId.NONE
    tapped = ResponseInputs(eye_tap=EyeTapSample(current, EyeId.LEFT), last_interaction_at_us=1_000_000)
    annoyed = advance_response(config, state, tapped, 100, 1_000_000)
    duplicate = advance_response(config, annoyed, tapped, 100, 1_100_000)
    assert annoyed.annoyance == 1 and annoyed.protective_eye_closure > .8
    assert duplicate.annoyance < 1 and duplicate.eye_hold_seconds < annoyed.eye_hold_seconds


# observed: Fifty seconds emitted by the actual portable C++ response engine.
# Raises AssertionError for independent host/model divergence or lost idle/audio behavior.
def compare_response(observed):
    rig, _ = load_contract()
    config = rig["response"]
    state = make_response_state(config)
    placement = tuple(rig["default_transform"][name] for name in ("x", "y", "scale"))
    for tick, row in enumerate(observed):
        now = 1_000_000 + tick * 100_000
        current = SampleStamp(SampleHealth.CURRENT, InputOrigin.SYNTHETIC, now, 0)
        power_stamp = current if tick < 400 else SampleStamp(SampleHealth.UNAVAILABLE, InputOrigin.SYNTHETIC, now, 0x103)
        means = (6000, 3000) if 80 <= tick < 100 else (0, 0)
        playing = 110 <= tick < 130
        inputs = ResponseInputs(power=PowerSample(power_stamp, 100 if tick < 150 else 12),
            microphone=MicrophoneSample(current, 2, means, means),
            playback=PlaybackSample(current, playing, 5000 if playing else 0),
            last_interaction_at_us=46_000_000 if tick >= 450 else 35_400_000 if tick >= 344 else 0,
            eye_tap=EyeTapSample(SampleStamp(SampleHealth.CURRENT, InputOrigin.SYNTHETIC, 46_000_000, 0), EyeId.LEFT)
                if tick >= 450 else EyeTapSample(),
            body_touch=BodyTouchSample(current, tick < 325, (60, -25), 0, 31_000_000)
                if 300 <= tick < 336 else BodyTouchSample())
        state = advance_response(config, state, inputs, 100, now)
        frame = compose_response(rig, state, rig["boot_channels"]["ready"],
            contour_defaults(rig), placement, 0)
        expected = {"attention": state.attention, "speaking": state.speaking, "fatigue": state.fatigue,
            "idle": state.idle_seconds, "yawn": state.yawn_weight,
            "mouth": frame.face[FaceChannel.MOUTH_OPEN], "eye": frame.face[FaceChannel.EYE_LEFT],
            "annoyance": state.annoyance, "closure": state.protective_eye_closure,
            "root_x": frame.current_root[0], "root_y": frame.current_root[1],
            "depth": frame.depth_scale, "amplitude": frame.contour[ContourControl.AMPLITUDE],
            "fullness": frame.contour[ContourControl.FULLNESS]}
        for name, value in expected.items():
            if not math.isclose(row[name], value, rel_tol=0, abs_tol=.00015):
                raise AssertionError(f"Native/Python tick={tick} {name}: {row[name]} vs {value}")
    assert len(observed) == 500
    assert max(row["yawn"] for row in observed) > .99
    assert observed[345]["yawn"] < observed[343]["yawn"]  # Activity releases smoothly.
    assert observed[120]["speaking"] > .5
    assert observed[95]["attention"] > .5
    assert observed[410]["fatigue"] == observed[499]["fatigue"]  # No manufactured charge recovery.
    assert observed[450]["annoyance"] == 1 and observed[451]["closure"] > .96
    assert observed[460]["closure"] < .5 and observed[480]["annoyance"] < .4


# settings: Strict Alpha authoring source with explicit cue durations and demo identities.
# Raises AssertionError or filesystem errors for mismatched generated assets.
def check_assets(settings):
    rig, palette = load_contract()
    sample_rate_hz = settings["audio"]["sample_rate_hz"]
    generated = ROOT / "assets/cody/runtime/generated"
    manifest = json.loads((generated / "manifest.json").read_text())
    assert manifest["source_sha256"] == text_source_hash(ROOT / "assets/cody/runtime.yaml")
    hashes = []
    for cue, duration_key in (("audio_ready", "ready_duration_seconds"), ("eyes_open", "eyes_open_duration_seconds")):
        raw = (generated / f"cody-{cue}.pcm16le").read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        hashes.append(digest)
        assert digest == manifest["cues"][cue]["sha256"]
        cue_metadata = manifest["cues"][cue]
        chime_bytes = round(settings["audio"][duration_key] * sample_rate_hz) * 2
        assert raw[:chime_bytes] == synthesize(settings, cue)
        expected_bytes = chime_bytes
        if cue == "eyes_open":
            assert cue_metadata["chime_pcm_bytes"] == chime_bytes
            assert cue_metadata["chime_sha256"] == hashlib.sha256(raw[:chime_bytes]).hexdigest()
            selected_hello = settings["audio"]["eyes_open_hello"]
            if selected_hello == "none":
                assert cue_metadata["hello"] is None and cue_metadata["gap_pcm_bytes"] == 0
            else:
                selection = load_selection()
                assert cue_metadata["chime_sha256"] == selection["original_chime_sha256"]
                selected_cue = next(item for item in selection["cues"] if item["id"] == selected_hello)
                hello_metadata = cue_metadata["hello"]
                assert hello_metadata["id"] == selected_hello
                assert hello_metadata["selection_sha256"] == selection["selection_sha256"]
                assert hello_metadata["source_sha256"] == selected_cue["source_sha256"]
                gap_bytes = round(selection["chime_gap_ms"] * sample_rate_hz / 1000) * 2
                assert cue_metadata["gap_pcm_bytes"] == gap_bytes
                assert raw[chime_bytes:chime_bytes + gap_bytes] == bytes(gap_bytes)
                hello = raw[chime_bytes + gap_bytes:]
                assert len(hello) == hello_metadata["pcm_bytes"]
                assert len(hello) == selected_cue["expected_pcm_bytes"]
                assert hashlib.sha256(hello).hexdigest() == hello_metadata["pcm_sha256"]
                assert hashlib.sha256(hello).hexdigest() == selected_cue["expected_pcm_sha256"]
                expected_bytes += gap_bytes + len(hello)
        assert len(raw) == expected_bytes
        assert len(raw) == cue_metadata["pcm_bytes"]
        assert cue_metadata["sample_rate_hz"] == sample_rate_hz
        assert math.isclose(cue_metadata["duration_seconds"], len(raw) / 2 / sample_rate_hz)
        with wave.open(str(generated / f"cody-{cue}.wav"), "rb") as wav:
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, sample_rate_hz)
            assert wav.readframes(wav.getnframes()) == raw
    assert len(set(hashes)) == 2
    elapsed = 0
    for step in settings["demo"]["steps"]:
        phase, _, _ = demo_targets(settings, rig, elapsed)
        assert phase == step["id"]
        elapsed += round(step["duration_seconds"] * 1000)
    assert demo_targets(settings, rig, elapsed)[0] == settings["demo"]["steps"][0]["id"]
    assert len(palette["entries"]) == 256


# argv: CLI arguments forwarded through device.ps1 check -Check cody-runtime.
# Raises original compiler, model, asset or filesystem failures; report never infers hardware behavior.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    args = parser.parse_args(argv)
    output = ROOT / "artifacts/checks/cody-runtime"
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "failed", "hardware": "NOT ACCESSED", "firmware": "NOT BUILT"}
    try:
        settings = load_settings(ROOT / "assets/cody/runtime.yaml")
        check_assets(settings)
        check_boundaries()
        check_typewriter_audio_source(ROOT)
        compiler = resolve_compiler(args.compiler)
        includes = ("scripts/tests/cody_native", "components/CODY2D/include", "components/CODY_companion/include",
                    "components/LUNASHELL_platform/include",
                    "assets/cody/rig/generated", "assets/cody/runtime/generated",
                    "assets/cody/appearance/jelly-scanline/generated",
                    "assets/cody/personality-icons/generated")
        includes += ("assets/cody/personality/generated",)
        sources = ("scripts/tests/cody_native/runtime_contract.cpp", "components/CODY2D/src/CODY2D.cpp",
                       "scripts/tests/cody_native/host_settings.cpp",
                       "scripts/tests/cody_native/host_diagnostics.cpp",
                       "components/CODY_companion/src/diagnostics/CODY_debug_hud.cpp",
                       "scripts/tests/cody_native/status_overlay_contract.cpp",
                       "components/CODY_companion/src/diagnostics/CODY_status_overlay.cpp",
                       "components/CODY2D/src/CODY2D_paths.cpp", "components/CODY_companion/src/model/CODY_rig.cpp",
                       "components/CODY_companion/src/model/CODY_response.cpp", "components/CODY_companion/src/demos/CODY_demo.cpp",
                       "components/CODY_companion/src/diagnostics/CODY_benchmark.cpp", "components/CODY_companion/src/rendering/CODY_damage.cpp",
                       "components/CODY_companion/src/rendering/CODY_personality.cpp",
                       "assets/cody/personality-icons/generated/personality_icons.cpp",
                       "components/CODY_companion/src/rendering/CODY_jelly.cpp",
                       "assets/cody/appearance/jelly-scanline/generated/cody_jelly_data.cpp")
        executable = output / "runtime-contract.exe"
        command = compile_command(compiler, includes, sources, executable,
                                  definitions=("CODY_ENABLE_PEARL_JELLY=1",), cxx_standard=20)
        run_stage(command, output, "compile")
        native = run_stage([str(executable)], output, "runtime")
        compare_response(json.loads(native.stdout))
        report.update(status="passed", response_ticks=500, fixture_seconds=50,
            contracts=["native/host audio, fatigue and idle parity", "smooth interruption",
                       "exact original chime and selected greeting composition", "256-color palette", "authored demo boundaries",
                       "rolling stage statistics, percentiles and overlay separation", "shared eye hit targets and transient annoyance",
                       "dirty-region packing, stride guards, full redraw after cache invalidation and rig framebuffer parity",
                       "scanline stroke exact parity with exhaustive reference, round caps and clipped tiles",
                       "native main HUD canonical CPU/IP/RAM/SD validity, lower-arc glyph rasterization and circular clipping",
                       "native status overlay glyph and layout contract",
                       "original tiny typewriter cue, nonblocking coalesced admission, shared audio owner and stale-scene suppression"])
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Cody runtime host contracts passed: {output / 'report.json'}")


if __name__ == "__main__":
    main()
