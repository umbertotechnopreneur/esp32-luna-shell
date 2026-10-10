# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added character verification on 2026-10-11.
# Human guidance: Umberto Giacobbi; requested firmware and simulator tests.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""SIMULATED character regressions through Studio's production C++ binding and Tk frame loop."""
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
import sys
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "simulator/windows"))

from cody_native_behavior import CharacterActivity, CharacterWorkKind, NativeBehavior
from cody_native import NativePanelObservation
from cody_happiness import PersonalityEvent
from cody_personality import PersonalityCue
from cody_petting import PettingZone
from cody_simulator import CodyRenderer, CodyWindow
from scripts.common.cody_rig_contract import contour_defaults

OUTPUT = ROOT / "artifacts/checks/cody-character-studio"
ACTIVE = CharacterActivity("a" * 64, 7, current=True, working=True, compacting=True)


class CharacterStudioTests(unittest.TestCase):
    # self: Isolated native response/direction owners, without a live CCP connection.
    # Raises original authoring, provenance or DLL initialization errors.
    def setUp(self):
        self.renderer = CodyRenderer()
        self.native = self.renderer.require_native()
        self.behavior = NativeBehavior(self.native)
        self.addCleanup(self.behavior.release)
        self.native.reset_response(42)
        self.channels = {channel["name"]: index for index, channel in enumerate(self.renderer.rig["channels"])}
        self.placement = (233., 233., 145.)
        self.base = self.native.compose_response(self.renderer.rig["boot_channels"]["ready"],
            contour_defaults(self.renderer.rig), self.placement, 0, False)

    # self: Native renderer and annotation owner.
    # frame: Actual native response after the Python/C++ direction boundary.
    # name: Fixed test scenario used only for a bounded artifact filename/caption.
    # Raises original native raster or file-write errors.
    def capture(self, frame, name):
        scene = self.native.evaluate(frame.face, frame.transform, frame.contour | {"phase": 0.})
        image = self.renderer.draw_scene(scene, debug=False, navigation_visible=False).convert("RGB")
        OUTPUT.mkdir(parents=True, exist_ok=True)
        self.renderer.capture_sheet(image, f"SIMULATED / {name}").save(OUTPUT / f"{name}.png")
        return scene

    # self: Actual Studio C++ boundary with a visibly synthetic current model and independent catalog lease.
    # Raises AssertionError if Home cannot show Expensive, retain the timer or expire the chip without losing the model.
    def test_home_expensive_chip_and_catalog_expiry_use_native_rendering(self):
        source = NativePanelObservation()
        source.project = b"SIMULATED price badge"
        source.chat_id = source.technical_chat_id = b"a" * 64
        source.model = b"gpt-6-astra"
        source.phase = 2
        source.connected = source.link_valid = source.activity_valid = source.technical_valid = source.model_valid = True
        source.cost_band = 3
        source.cost_age_us = 3600000000
        source.cost_valid_for_us = 1000000
        OUTPUT.mkdir(parents=True, exist_ok=True)
        pixels = []
        for name, receipt_age in (("home-expensive", 0), ("home-price-expired", 1000000)):
            source.receipt_age_us = receipt_age
            scene = self.native.evaluate(self.base.face, (233., 145., 95.), self.base.contour | {"phase": 0.})
            self.native.prepare_activity_identity(scene, "SIMULATED price badge", "codex/cody", source,
                9000000, False, 12000000, False)
            image = self.renderer.draw_scene(scene, debug=False, navigation_visible=False).convert("RGB")
            self.renderer.capture_sheet(image, f"SIMULATED / {name}").save(OUTPUT / f"{name}.png")
            pixels.append(image.getpixel((87, 343)))
        self.assertGreater(pixels[0][0], pixels[0][1] * 2)
        self.assertLess(abs(pixels[1][0] - pixels[1][1]), 16)

    # self: Real ctypes transport with a deliberately closed eye and audio mouth pose.
    # Raises AssertionError for gaze failure or damage to channels owned by the response model.
    def test_gaze_preserves_protective_and_audio_channels(self):
        values = list(self.base.face)
        values[self.channels["eye_left"]] = 0.
        values[self.channels["mouth_open"]] = .63
        base = replace(self.base, face=values)
        directed, cue = self.behavior.direct(base, self.placement, 300000,
            enabled=True, gaze=(340., 180.))
        self.assertGreater(directed.face[self.channels["gaze_x"]], 0.)
        self.assertLess(directed.face[self.channels["gaze_y"]], 0.)
        self.assertEqual(directed.face[self.channels["eye_left"]], 0.)
        self.assertAlmostEqual(directed.face[self.channels["mouth_open"]], .63)
        self.assertEqual(directed.transform, base.transform)
        self.assertEqual(directed.current_root, base.current_root)
        self.assertEqual(cue, PersonalityCue.NONE)
        self.capture(directed, "gaze")

    # self: Real native geometry, caress recognizer and artistic direction sharing one owner.
    # Raises AssertionError if an accepted stroke fails to warm the later face.
    def test_accepted_caress_warms_the_face(self):
        scene = self.native.evaluate(self.base.face, self.placement)
        eyes = [scene["world"][name] for name in ("eye.left", "eye.right")]
        x = sum(point[0] for point in eyes) / 2 - 12
        y = min(point[1] for point in eyes) - 40
        zones = [self.behavior.observe(scene, (x + step * 4, y), True, step == 0,
                                      1000000 + step * 100000) for step in range(7)]
        self.assertEqual(zones.count(PettingZone.ABOVE_EYES), 1)
        self.behavior.apply_event(PersonalityEvent.CARESS)
        self.behavior.direct(self.base, self.placement, 100000, enabled=True, affection_allowed=True)
        warm, _ = self.behavior.direct(self.base, self.placement, 1000000,
                                       enabled=True, affection_allowed=True)
        self.assertGreater(warm.face[self.channels["mouth_corner_left"]],
                           self.base.face[self.channels["mouth_corner_left"]])
        self.capture(warm, "caress-warmth")

    # self: Native compaction sequence owner observed through the actual Studio ABI.
    # Raises AssertionError for missing concentration or a missing genuine-end release.
    def test_matching_compaction_end_releases(self):
        active, cue = self.behavior.direct(self.base, self.placement, 1000000,
                                           enabled=True, activity=ACTIVE)
        self.assertEqual(cue, PersonalityCue.CONFUSION)
        self.capture(active, "compacting")
        completed = replace(ACTIVE, compacting=False, refreshed=True)
        self.behavior.direct(self.base, self.placement, 100000, enabled=True, activity=completed)
        released, cue = self.behavior.direct(self.base, self.placement, 1000000,
                                             enabled=True, activity=completed)
        self.assertEqual(cue, PersonalityCue.NONE)
        self.assertGreater(released.contour["fullness"], self.base.contour["fullness"])
        self.capture(released, "context-refreshed")

    # self: Hidden-route admission and source continuity across the Python/C++ boundary.
    # Raises AssertionError if Debug changes a pose or a hidden completion replays on exit.
    def test_hidden_debug_and_source_gap_do_not_replay_completion(self):
        self.behavior.direct(self.base, self.placement, 1000000, enabled=True, activity=ACTIVE)
        unchanged, cue = self.behavior.direct(self.base, self.placement, 0, enabled=False, activity=ACTIVE)
        self.assertEqual(unchanged, self.base)
        self.assertEqual(cue, PersonalityCue.NONE)
        completed = replace(ACTIVE, compacting=False, refreshed=True)
        self.behavior.direct(None, self.placement, 0, enabled=False, activity=completed)
        shown, _ = self.behavior.direct(self.base, self.placement, 1000000, enabled=True, activity=completed)
        self.assertLessEqual(shown.contour["fullness"], self.base.contour["fullness"])
        self.behavior.direct(self.base, self.placement, 1000000, enabled=True, activity=ACTIVE)
        self.behavior.direct(self.base, self.placement, 1000000, enabled=True)
        recovered, _ = self.behavior.direct(self.base, self.placement, 1000000, enabled=True, activity=completed)
        self.assertLessEqual(recovered.contour["fullness"], self.base.contour["fullness"])

    # self: Production Python/native transport following an exact observed Codex turn.
    # Raises AssertionError for missing celebration, repeated delivery or damaged audio/root channels.
    def test_same_turn_reply_celebrates_once(self):
        working = CharacterActivity(chat="a" * 64, turn="b" * 64, current=True, working=True,
                                    work_kind=CharacterWorkKind.EDITING)
        self.behavior.direct(self.base, self.placement, 1000000, enabled=True, activity=working)
        ready = replace(working, working=False, work_kind=CharacterWorkKind.NONE, ready=True)
        self.behavior.direct(self.base, self.placement, 0, enabled=True, activity=ready)
        smiled, cue = self.behavior.direct(self.base, self.placement, 900000, enabled=True, activity=ready)
        self.assertEqual(cue, PersonalityCue.JOY)
        self.assertGreater(smiled.face[self.channels["mouth_corner_left"]],
                           self.base.face[self.channels["mouth_corner_left"]])
        self.assertEqual(smiled.transform, self.base.transform)
        self.assertEqual(smiled.face[self.channels["mouth_open"]], self.base.face[self.channels["mouth_open"]])
        self.capture(smiled, "reply-ready")
        _, cue = self.behavior.direct(self.base, self.placement, 1800000, enabled=True, activity=ready)
        self.assertEqual(cue, PersonalityCue.NONE)

    # self: Native turn identity and paused/source-gap admission across the real ctypes boundary.
    # Raises AssertionError if incomplete metadata turns into a successful-task performance.
    def test_reply_requires_continuous_matching_turn(self):
        working = CharacterActivity(chat="a" * 64, turn="b" * 64, current=True, working=True)
        for turn in ("", "c" * 64):
            self.behavior.direct(None, self.placement, 0, enabled=False)
            self.behavior.direct(self.base, self.placement, 1000000, enabled=True, activity=working)
            ready = replace(working, working=False, ready=True, turn=turn)
            _, cue = self.behavior.direct(self.base, self.placement, 100000, enabled=True, activity=ready)
            self.assertEqual(cue, PersonalityCue.NONE)
        ready = replace(working, working=False, ready=True)
        self.behavior.direct(self.base, self.placement, 1000000, enabled=True, activity=working)
        self.behavior.direct(self.base, self.placement, 0, enabled=True, paused=True, activity=ready)
        _, cue = self.behavior.direct(self.base, self.placement, 100000, enabled=True, activity=ready)
        self.assertEqual(cue, PersonalityCue.NONE)

    # self: Native attention reaction with unchanged protective closure and one observed event edge.
    # Raises AssertionError for a missing double blink or replay while the same approval remains pending.
    def test_attention_double_blink_runs_once(self):
        working = CharacterActivity(chat="a" * 64, current=True, working=True)
        self.behavior.direct(self.base, self.placement, 1000000, enabled=True, activity=working)
        waiting = replace(working, working=False, attention=True)
        self.behavior.direct(self.base, self.placement, 0, enabled=True, activity=waiting)
        blinked, cue = self.behavior.direct(self.base, self.placement, 324000, enabled=True, activity=waiting)
        self.assertEqual(cue, PersonalityCue.CURIOSITY)
        self.assertLess(blinked.face[self.channels["eye_right"]], self.base.face[self.channels["eye_right"]] * .1)
        self.capture(blinked, "attention")
        restored, _ = self.behavior.direct(self.base, self.placement, 1800000, enabled=True, activity=waiting)
        self.assertEqual(restored.face[self.channels["eye_right"]], self.base.face[self.channels["eye_right"]])

    # self: Shared native battery selector; synthetic flags never become firmware observations.
    # Raises AssertionError for fabricated charging/presence or rounded threshold admission.
    def test_automatic_power_cues_keep_source_truth(self):
        facts = dict(current=True, present=True, percentage_valid=True, percentage=15.5, charging=False)
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.NONE)
        facts["percentage"] = 15.
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.LOW_BATTERY)
        facts["percentage"] = 17.
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.LOW_BATTERY)
        facts["charging"] = True
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.CHARGING)
        self.assertTrue(self.behavior.power_low_active)
        facts.update(percentage_valid=False, percentage=101.)
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.CHARGING)
        self.assertFalse(self.behavior.power_low_active)
        facts["current"] = False
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.NONE)

    # self: Actual withdrawn Tk window; synthetic inputs never enter firmware snapshots.
    # Raises original GUI/native errors or AssertionError for a broken scheduled frame loop.
    def test_studio_window_presents_and_keeps_debug_isolated(self):
        import tkinter as tk
        from cody_ui_dpi import configure_tk_scaling, prepare_display
        prepare_display()
        root = tk.Tk()
        root.withdraw()
        configure_tk_scaling(root)
        window = None
        try:
            window = CodyWindow(root, self.renderer, SimpleNamespace(
                screen="rig", rotation=0, safe_guide=False, ccp=False))
            window.audio_enabled.set(False)
            failures = []
            root.report_callback_exception = lambda *error: failures.append(error)
            # The UI's FPS counter resets periodically; count actual presentations instead.
            with patch.object(window, "present_frame", wraps=window.present_frame) as presented:
                deadline = time.monotonic() + 3.
                while time.monotonic() < deadline and presented.call_count < 2:
                    root.update()
                    time.sleep(.01)
                self.assertGreaterEqual(presented.call_count, 2)
            self.assertFalse(failures, failures)
            self.assertIsNotNone(window.photo)
            # Observe the real route's admission; the native call still executes.
            with patch.object(window.native_behavior, "direct", wraps=window.native_behavior.direct) as direct:
                window.debug.set(True)
                deadline = time.monotonic() + 3.
                while time.monotonic() < deadline and not direct.called:
                    root.update()
                    time.sleep(.01)
                self.assertTrue(direct.called)
                self.assertFalse(direct.call_args.kwargs["enabled"])
            self.assertFalse(failures, failures)
            self.capture(window.response_frame, "studio-debug")
        finally:
            if window is not None:
                window.close()
            else:
                root.destroy()


if __name__ == "__main__":
    unittest.main(verbosity=2)
