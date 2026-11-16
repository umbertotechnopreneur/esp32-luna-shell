"""Read-only native voice presenter and host session isolation contracts."""

import ctypes
import json
from pathlib import Path
import subprocess
import sys
import unittest
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))
from cody_studio_catalog import entry_for
from cody_studio_scenario import default_scenario, scenario_revision, validate_scenario

OUTPUT = ROOT / "artifacts/simulator/studio/voice-checks"
CASES = {"V01": "cody.voice.preparing", "V02": "cody.voice.listening", "V03": "cody.voice.sending"}


class VoiceContractTests(unittest.TestCase):
    """Static voice views require the compiled feature and no service action."""

    # self: Three distinct inventory identities and native host target strings.
    # Raises AssertionError for a fake service or an unavailable-feature fallback.
    def test_voice_scenarios_are_static_and_capability_conditioned(self):
        from cody_studio_runner import StudioRunner

        class NoVoiceDevice:
            # name: Requested compiled feature; no features exist on this profile.
            # Returns False without accessing hardware or modifying session state.
            def studio_has_capability(self, name):
                return False

        runner = StudioRunner(NoVoiceDevice())
        for identity, target in CASES.items():
            with self.subTest(identity=identity):
                entry = entry_for(identity)
                scenario = validate_scenario(default_scenario(identity))
                self.assertEqual(entry["native_target"], target)
                self.assertEqual(entry["availability"], "profile-conditional")
                self.assertEqual(entry["rendering_mode"], "presenter-isolated")
                self.assertEqual(entry["required_capabilities"], ["voice-input"])
                self.assertEqual((scenario["page"], scenario["services"]), ({}, {}))
                self.assertEqual(scenario["time"], {"mode": "controlled-static", "initial_ms": 0,
                                                   "advances_ms": [], "capture_at_ms": [0]})
                available, reason = runner.availability(entry)
                self.assertFalse(available)
                self.assertIn("voice-input", reason)


class VoiceNativeTests(unittest.TestCase):
    """C++ voice stages retain their own receipt and retire before another view."""

    # self: Parent suite that keeps isolated worker output under artifacts.
    # Raises worker, provenance or timeout errors without substituting another renderer.
    def test_voice_frames_repeat_and_restore_p04(self):
        directory = OUTPUT / uuid4().hex
        directory.mkdir(parents=True, exist_ok=False)
        result = subprocess.run([sys.executable, "-B", str(Path(__file__).resolve()), "--worker", str(directory)],
                                cwd=ROOT, text=True, capture_output=True, timeout=240)
        for stream in ("stdout", "stderr"):
            (directory / f"worker.{stream}.log").write_text(getattr(result, stream), encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stderr[-8000:] + result.stdout[-2000:])
        summary = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(summary["repeated_cases"], sorted(CASES))
        self.assertEqual(summary["p04_restorations"], 6)
        self.assertTrue(summary["microphone_isolation_and_restore"])
        self.assertIn("not independently observed", summary["operation_evidence"])


# directory: New isolated session directory beneath voice-check artifacts.
# Raises native, identity, profile or comparison errors without performing service actions.
def run_worker(directory):
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    directory = Path(directory).resolve(strict=True)
    if not directory.is_relative_to(OUTPUT.resolve()):
        raise ValueError("Voice worker directory escaped simulator artifacts")
    device = NativeDevice({"reason": "SIMULATED-STUDIO-VOICE"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True, audio_mode="silent")
    device.start()
    if not device.studio_has_capability("voice-input"):
        raise AssertionError("Native host voice presenter is unavailable")
    runner = StudioRunner(device)
    display = default_scenario("P04")
    runner.prepare(display)
    original = runner.capture().metadata["pixel_sha256"]
    seed = device.api.cody_host_studio_test_microphone_seed
    seed.argtypes, seed.restype = [ctypes.c_uint32], ctypes.c_int
    probe = device.api.cody_host_studio_test_microphone_read
    probe.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
                      ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_int32),
                      ctypes.POINTER(ctypes.c_uint8)]
    probe.restype = ctypes.c_int
    listening = default_scenario("V02")
    isolated_hashes = []
    for level in (100, 30000):
        if seed(level) != 0:
            raise AssertionError("Synthetic microphone seeding failed")
        runner.prepare(listening)
        stored, projected = ctypes.c_uint32(), ctypes.c_uint32()
        valid, error, playback = ctypes.c_uint8(), ctypes.c_int32(), ctypes.c_uint8()
        if probe(ctypes.byref(stored), ctypes.byref(projected), ctypes.byref(valid),
                 ctypes.byref(error), ctypes.byref(playback)) != 0:
            raise AssertionError("Synthetic microphone inspection failed")
        if (stored.value != level or valid.value != 0 or error.value != 0x106 or playback.value != 1):
            raise AssertionError("Voice projection did not isolate the microphone while preserving playback")
        isolated_hashes.append(runner.capture().metadata["pixel_sha256"])
    if isolated_hashes[0] != isolated_hashes[1]:
        raise AssertionError("Voice pixels depended on a prior microphone observation")
    runner.prepare(display)
    if probe(ctypes.byref(stored), ctypes.byref(projected), ctypes.byref(valid),
             ctypes.byref(error), ctypes.byref(playback)) != 0:
        raise AssertionError("Microphone restoration inspection failed")
    if (stored.value, projected.value, valid.value, error.value, playback.value) != (30000, 30000, 1, 0, 1):
        raise AssertionError("Leaving Voice did not restore the previous host microphone observation")
    captures = {}
    restores = 0
    for identity, target in CASES.items():
        scenario = default_scenario(identity)
        images = []
        for _ in range(2):
            runner.prepare(scenario)
            metadata = runner.capture().metadata
            receipt = metadata["frame_identity"]
            if (metadata["id"] != entry_for(identity)["id"] or metadata["inventory_id"] != identity or
                    metadata["rendering_mode"] != "presenter-isolated" or receipt["target"] != target or
                    receipt["rendering_mode"] != "presenter-isolated" or
                    receipt["scenario_revision"] != scenario_revision(scenario) or
                    receipt["at_ms"] != 0 or receipt["abi"] != 3):
                raise AssertionError(f"Voice frame identity disagrees with {identity}")
            images.append(metadata["pixel_sha256"])
            runner.prepare(display)
            if runner.capture().metadata["pixel_sha256"] != original:
                raise AssertionError(f"{identity} contaminated the following P04 frame")
            restores += 1
        if images[0] != images[1]:
            raise AssertionError(f"{identity} produced different pixels after P04")
        captures[identity] = images[0]
    if len(set(captures.values())) != len(CASES):
        raise AssertionError("Distinct voice phases rendered identical frames")
    print(json.dumps({"repeated_cases": sorted(CASES), "p04_restorations": restores,
                      "microphone_isolation_and_restore": True,
                      "operation_evidence": "Touch and service calls are not independently observed; host guards are source-reviewed"}), flush=True)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--worker":
        raise SystemExit("Voice workers are internal; use device.ps1 check -Check cody-studio")
    raise SystemExit(run_worker(sys.argv[2]))
