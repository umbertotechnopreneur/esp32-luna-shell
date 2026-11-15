"""Real authored-cue playback checks, selected through the public Studio check."""

from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
from uuid import uuid4

from cody_studio_audio import AudioAudition, CUE_KEYS, SESSION_ROOT
from cody_studio_scenario import default_scenario


# cue: Stable semantic authored sound key.
# sounds: Explicit user sound preference.
# volume: Confirmed codec percentage.
# quiet: Whether the scenario's local civil time lies in an enabled quiet window.
# mode: Real host output or the explicit firmware PCM probe.
# Raises native/provenance/worker failures with retained diagnostics.
def audition(cue, sounds=True, volume=60, quiet=False, mode="test-probe"):
    scenario = default_scenario("P12" if quiet else "P04")
    scenario["common"]["settings"].update(sounds_enabled=sounds, playback_volume=volume)
    if quiet:
        scenario["page"].update(quiet_hours_enabled=True, quiet_start_minute=0, quiet_end_minute=1439)
    job = AudioAudition.start(scenario, cue, audio_mode=mode)
    deadline = time.monotonic() + 30
    try:
        while not job.done and time.monotonic() < deadline:
            job.poll()
            if not job.done:
                time.sleep(.03)
        if not job.done:
            raise TimeoutError("Authored cue did not reach a terminal native outcome")
        if job.error:
            raise AssertionError(job.error)
        return job.result
    finally:
        job.cancel()


class AudioStatusContractTests(unittest.TestCase):
    """A replace race is temporary only until a completed child must publish its result."""

    # self: Synthetic process lifecycle and retained status fixture, without native loading.
    # Raises original artifact fixture errors rather than using paths outside artifacts.
    def setUp(self):
        self.directory = SESSION_ROOT / "checks" / uuid4().hex
        self.directory.mkdir(parents=True)
        (self.directory / "stderr.log").write_text("original native diagnostic", encoding="utf-8")
        self.process = SimpleNamespace(poll=Mock(return_value=None), wait=Mock(return_value=0),
                                       kill=Mock(), stdin=None)
        self.job = AudioAudition(self.directory, self.process, "scenario", "pet:tap",
                                 {"library_sha256": "build"})
        self.path = self.directory / "status.json"
        self.status = {"schema": "cody.studio.audio.v1", "scenario_revision": "scenario", "cue": "pet:tap",
                       "library_sha256": "build", "state": "playing", "error": 0}
        self.path.write_text(json.dumps(self.status), encoding="utf-8")

    # self: The first read loses the path during replacement, then actual file bytes become visible.
    # Raises AssertionError if the live child is cancelled or a stale terminal snapshot is accepted.
    def test_missing_in_progress_name_retries_then_reads_final_after_exit(self):
        original_read = Path.read_text
        injected = {"pending": True}

        # path: The real retained status fixture or another untouched path.
        # args: Positional Path.read_text arguments, forwarded to the actual implementation.
        # kwargs: Encoding/errors parameters, forwarded without changing the file's contents.
        # Raises FileNotFoundError exactly once to model a filesystem replacement boundary.
        def read_during_replace(path, *args, **kwargs):
            if path == self.path and injected["pending"]:
                injected["pending"] = False
                raise FileNotFoundError("injected replace window")
            return original_read(path, *args, **kwargs)

        with patch.object(Path, "read_text", new=read_during_replace):
            self.assertIsNone(self.job.poll())
            self.assertFalse(self.job.done)
            self.process.kill.assert_not_called()
            self.process.wait.assert_not_called()
            self.assertEqual(self.job.poll()["state"], "playing")
            self.status.update(state="completed", probe={"nonzero_frames": 128})
            self.path.write_text(json.dumps(self.status), encoding="utf-8")
            self.process.poll.return_value = 0
            self.assertEqual(self.job.poll()["probe"]["nonzero_frames"], 128)
            self.assertTrue(self.job.done)
            self.assertIsNone(self.job.error)

    # self: A terminated worker has no publication writer left to make absence temporary.
    # Raises AssertionError if missing final evidence is accepted as completed playback.
    def test_missing_final_status_is_a_hard_error_with_native_diagnostics(self):
        self.path.unlink()
        self.process.poll.return_value = 1
        with self.assertRaisesRegex(RuntimeError, "final status missing: original native diagnostic"):
            self.job.poll()
        self.assertTrue(self.job.done)

    # self: An in-progress file that exists with invalid content must not be treated as a replace gap.
    # Raises AssertionError if malformed status is retried while its child remains running.
    def test_corrupt_json_still_cancels_and_reaps_child(self):
        self.path.write_text("{invalid JSON", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.job.poll()
        self.process.kill.assert_called_once()
        self.process.wait.assert_called_once()
        self.assertTrue(self.job.done)


class NativeCueTests(unittest.TestCase):
    """Use real firmware samples, policy and mixer with independent native runtimes."""

    # self: Explicit semantic keys are validated before any native child starts.
    # Raises AssertionError if bad inputs can launch or mutate a session.
    def test_unknown_cues_and_modes_are_rejected_before_launch(self):
        scenario = default_scenario("P04")
        with patch("cody_studio_audio.subprocess.Popen") as spawn:
            for cue in ("unknown", "codex:none", 1, None):
                with self.subTest(cue=cue), self.assertRaises(ValueError):
                    AudioAudition.start(scenario, cue)
            with self.assertRaises(ValueError):
                AudioAudition.start(scenario, "pet:tap", audio_mode="silent")
            spawn.assert_not_called()

    # self: Every published cue must complete through its actual authored native owner.
    # Raises AssertionError for missing identities, silent PCM or unrelated sample rates.
    def test_all_26_authored_cues_reach_completed_native_pcm(self):
        self.assertEqual(len(CUE_KEYS), 26)
        self.assertEqual(len(set(CUE_KEYS)), 26)
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = dict(zip(CUE_KEYS, pool.map(audition, CUE_KEYS)))
        identities = set()
        for cue, result in results.items():
            with self.subTest(cue=cue):
                self.assertEqual(result["cue"], cue)
                self.assertEqual(result["state"], "completed")
                self.assertEqual(result["error"], 0)
                self.assertGreater(result["sequence"], 0)
                self.assertGreater(result["probe"]["nonzero_frames"], 0)
                # LUNASHELL_audio.cpp configures the speaker at 22050 Hz;
                # authored 16000-Hz clips pass through its real output mixer.
                self.assertEqual(result["probe"]["last_rate"], 22050)
                self.assertNotIn(result["cue_id"], identities)
                identities.add(result["cue_id"])

    # self: Both native cue families must honor Sounds before submitting any PCM.
    # Raises AssertionError if unrelated startup sounds leak into the isolated audition.
    def test_sounds_off_suppresses_both_families_without_pcm(self):
        for cue in ("codex:editing", "pet:tap"):
            result = audition(cue, sounds=False)
            self.assertEqual(result["state"], "suppressed")
            self.assertEqual(result["probe"]["frames"], 0)
            self.assertEqual(result["probe"]["nonzero_frames"], 0)

    # self: A real civil-clock quiet window must gate authored notices and pet cues.
    # Raises AssertionError if audition bypasses the firmware's quiet policy.
    def test_quiet_hours_suppress_both_families(self):
        for cue in ("codex:checks-passed", "pet:jump"):
            result = audition(cue, quiet=True)
            self.assertEqual(result["state"], "suppressed")
            self.assertEqual(result["probe"]["nonzero_frames"], 0)

    # self: Compare one authored waveform with exact shared-codec output scaling.
    # Raises AssertionError if volume/mute policy is ignored or another cue contaminates PCM.
    def test_codec_volume_and_zero_level_are_observed_in_effective_pcm(self):
        full = audition("pet:tap", volume=100)
        half = audition("pet:tap", volume=50)
        muted = audition("pet:tap", volume=0)
        self.assertGreater(full["probe"]["peak"], half["probe"]["peak"])
        self.assertLessEqual(abs(full["probe"]["peak"] - 2 * half["probe"]["peak"]), 2)
        self.assertEqual(muted["probe"]["nonzero_frames"], 0)
        self.assertIn(muted["state"], ("completed", "suppressed"))

    # self: An immediate stop must reap the dedicated child and preserve caller inputs.
    # Raises AssertionError if cancellation mutates the scenario or leaves an audio owner running.
    def test_cancellation_reaps_child_and_preserves_input(self):
        scenario = default_scenario("P04")
        scenario["common"]["settings"].update(sounds_enabled=True, playback_volume=60)
        before = deepcopy(scenario)
        job = AudioAudition.start(scenario, "pet:first-boot-melody", audio_mode="test-probe")
        job.cancel()
        self.assertTrue(job.done)
        self.assertIsNotNone(job.process.poll())
        self.assertEqual(scenario, before)


# No parameters: select speaker output separately from the ordinary PCM-probe suite.
# Returns one actual host-output check; completion is not human listening evidence.
def host_suite():
    class HostCueTests(unittest.TestCase):
        # self: One audible authored cue on the actual selected host output device.
        # Raises the original backend/native error without claiming a person heard it.
        def test_authored_cue_completes_on_host_output(self):
            result = audition("pet:tap", mode="host-output", volume=50)
            self.assertEqual(result["state"], "completed")
            self.assertEqual(result["audio"]["mode"], "host-output")
            self.assertEqual(result["audio"]["backend_result"], 0)
    return unittest.TestSuite([HostCueTests("test_authored_cue_completes_on_host_output")])


# base_class: Existing real Tk/native fixture, executed in its own dedicated process.
# Returns only this audio workflow; inherited general UI workflows are not duplicated.
def ui_suite(base_class):
    class CueUiTests(base_class):
        # self: Real product controls and independent firmware PCM probe workers.
        # Raises AssertionError for owner contamination, missing controls or an unreaped child.
        def test_authored_cue_controls_preserve_interactive_state(self):
            from cody_native_display import completed_studio_frame
            from cody_studio_runner import CAPTURE_ROOT
            window = self.window
            self.button("Preserve current as baseline").invoke()
            current, baseline = window.current, window.baseline
            frames = set(CAPTURE_ROOT.glob("*/capture.json"))
            frame, receipt = completed_studio_frame()
            snapshot = self.device.read_snapshot()
            nvs_path = self.directory / "session.nvs"
            nvs = hashlib.sha256(nvs_path.read_bytes()).hexdigest()
            baseline_bytes = (baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes())
            window.sounds_enabled.set(True)
            window.playback_volume.set("50")
            form = window.form()
            if not window.sections["Animation"][2]:
                window.sections["Animation"][0].invoke()
            self.reveal_widget(window.cue_control, window.bottom_canvas)
            self.assertEqual(len(window.cue_control.cget("values")), 26)
            native_start = AudioAudition.start

            # scenario: Actual unsaved form copied by the real Audition cue button.
            # cue: Actual selected authored key; the output is replaced only by the explicit PCM test sink.
            def start_probe(scenario, cue):
                return native_start(scenario, cue, audio_mode="test-probe")

            with patch.object(AudioAudition, "start", side_effect=start_probe):
                window.cue_choice.set("pet:tap")
                window.cue_play_button.invoke()
                job = window.cue_job
                self.assertIsNotNone(job)
                self.wait_for(lambda: window.cue_job is None, "native cue completion", timeout=30)
                self.assertEqual(job.result["state"], "completed", job.error)
                self.assertGreater(job.result["probe"]["nonzero_frames"], 0)
                window.cue_choice.set("pet:first-boot-melody")
                window.cue_play_button.invoke()
                stopped = window.cue_job
                window.cue_stop_button.invoke()
                self.assertIsNotNone(stopped.process.poll())
                self.assertIsNone(window.cue_job)
            after_frame, after_receipt = completed_studio_frame()
            self.assertEqual(after_frame.tobytes(), frame.tobytes())
            self.assertEqual(after_receipt, receipt)
            self.assertEqual(self.device.read_snapshot(), snapshot)
            self.assertEqual(hashlib.sha256(nvs_path.read_bytes()).hexdigest(), nvs)
            self.assertEqual(window.form(), form)
            self.assertIs(window.current, current)
            self.assertIs(window.baseline, baseline)
            self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), baseline_bytes)
            self.assertEqual(set(CAPTURE_ROOT.glob("*/capture.json")), frames)
            self.assertEqual(self.errors, [])
            self.screenshot("studio-authored-cue.png")

    return unittest.TestSuite([CueUiTests("test_authored_cue_controls_preserve_interactive_state")])
