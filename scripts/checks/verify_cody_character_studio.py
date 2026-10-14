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
import json
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "simulator/windows"))

from cody_native_behavior import CharacterActivity, CharacterWorkKind, NativeBehavior
from cody_native import NativePanelObservation, NativePersonalityPainter
from cody_happiness import PersonalityEvent
from cody_personality import PersonalityCue, PersonalityPresentationStatus
from cody_petting import PettingZone
from cody_simulator import CodyRenderer, CodyWindow
from cody_preview_layout import PreviewLayout, enlarge_framebuffer, physical_point
from scripts.common.cody_rig_contract import contour_defaults

OUTPUT = ROOT / "artifacts/checks/cody-character-studio"
ACTIVE = CharacterActivity("a" * 64, 7, current=True, working=True, compacting=True, turn="c" * 64)


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

    # self: Isolated native transport; no fixture is published to a physical or live endpoint owner.
    # Returns an explicitly synthetic current Home observation with independent measured fields.
    def home_source(self):
        source = NativePanelObservation()
        source.project, source.branch = b"SIMULATED Home", b"codex/cody"
        source.chat_id = source.technical_chat_id = b"a" * 64
        source.model, source.effort = b"gpt-6-astra", b"high"
        source.phase = 2
        source.connected = source.link_valid = source.activity_valid = source.technical_valid = source.model_valid = True
        source.effort_valid = source.used_valid = source.capacity_valid = source.turn_elapsed_valid = True
        source.used, source.capacity, source.turn_elapsed_us = 155000, 474000, 12000000
        source.cost_band, source.cost_valid_for_us = 3, 3600000000
        source.device_ip, source.device_ip_valid = b"192.0.2.25", True
        source.simulated_motion_valid, source.simulated_motion = True, 4
        return source

    # self: Production Studio rendering owner with persistent native Home presentation state.
    # source: Original isolated observation, never formatted or clipped by Python.
    # panel: Native typed lower-panel identity.
    # elapsed: Pausable active display clock independent of observation receipt age.
    # title: Complete authored native Home title.
    # Returns native pixels; raises original transport/layout/raster errors without substitutes.
    def render_home(self, source, panel=0, elapsed=1000000, title="Working"):
        scene = self.native.evaluate(self.base.face, self.native.home_transform(), self.base.contour | {"phase": 0.})
        self.native.prepare_home_identity(scene, source, title, len(title), elapsed)
        self.native.prepare_home_dashboard(scene, panel, source, elapsed)
        return self.renderer.draw_scene(scene, debug=False, navigation_visible=False).convert("RGB")

    # self: Actual Studio C++ boundary with a visibly synthetic model and independent catalog lease.
    # Raises AssertionError if a catalog expiry changes the stable title/model or keeps a stale cost badge.
    def test_home_expensive_chip_and_catalog_expiry_use_native_rendering(self):
        source = self.home_source()
        source.cost_valid_for_us = 1000000
        OUTPUT.mkdir(parents=True, exist_ok=True)
        captures = []
        for name, receipt_age in (("home-expensive", 0), ("home-price-expired", 1000000)):
            source.receipt_age_us = receipt_age
            image = self.render_home(source, panel=2)
            self.renderer.capture_sheet(image, f"SIMULATED / {name}").save(OUTPUT / f"{name}.png")
            captures.append(image)
        self.assertEqual(captures[0].crop((0, 0, 466, 268)).tobytes(), captures[1].crop((0, 0, 466, 268)).tobytes())
        self.assertNotEqual(captures[0].crop((89, 288, 377, 326)).tobytes(), captures[1].crop((89, 288, 377, 326)).tobytes())
        self.assertTrue(any(red > 160 and red > green * 2 for red, green, blue in
                            captures[0].crop((89, 288, 377, 326)).getdata()))
        self.assertFalse(any(red > 160 and red > green * 2 for red, green, blue in
                             captures[1].crop((89, 288, 377, 326)).getdata()))

    # self: Real Studio post-presentation callback with a silent recording sink for optional PC audio.
    # Raises AssertionError if carousel, pause or source loss replays a claim, or a new expensive model loses its cue.
    def test_home_expensive_audio_once_per_model_after_visible_price(self):
        from cody_ccp_panel import CcpPanel
        panel = CcpPanel.__new__(CcpPanel)
        panel._passive_visible = True
        playing = Mock()
        audio = Mock()
        panel.window = SimpleNamespace(renderer=self.renderer, playing=playing,
            audio=audio, audio_enabled=SimpleNamespace(get=lambda: True))
        cases = (("model-a", 3, 0, True, True, 0), ("model-a", 3, 2, False, True, 0),
                 ("model-a", 3, 2, True, True, 1), ("model-a", 3, 3, True, True, 1),
                 ("model-a", 3, 0, True, True, 1), ("model-a", 3, 2, True, True, 1),
                 ("model-a", 3, 2, True, False, 1), ("model-a", 3, 2, True, True, 1),
                 ("model-b", 1, 2, True, True, 1), ("model-a", 3, 2, True, True, 2),
                 ("model-c", 3, 0, True, True, 2), ("model-d", 3, 2, True, True, 3))
        for model, cost, lower_panel, running, valid, expected in cases:
            with self.subTest(model=model, lower_panel=lower_panel, running=running, valid=valid):
                source = self.home_source()
                source.model, source.cost_band, source.model_valid = model.encode("ascii"), cost, valid
                scene = self.native.evaluate(self.base.face, self.native.home_transform(), self.base.contour | {"phase": 0.})
                self.native.prepare_home_identity(scene, source, "Working", 7, 1000000)
                self.native.prepare_home_dashboard(scene, lower_panel, source, 1000000)
                self.renderer.draw_scene(scene, debug=False, navigation_visible=False)
                playing.get.return_value = running
                panel.present_home_audio(scene)
                self.assertEqual(audio.model_cost_notice.call_count, expected)
        self.assertEqual(audio.model_cost_notice.call_args.args[0],
                         ROOT / "assets/cody/audio/model-cost/generated/model-cost.wav")

    # self: Existing fake audio queue without a worker, operating only on the immutable authored PCM.
    # Raises AssertionError for altered plins/gaps, deferred muted playback or admission outside the firmware PCM contract.
    def test_home_model_cost_audio_uses_five_original_samples_in_one_queue_item(self):
        import io
        import queue
        import wave
        from cody_fake_audio import FakeAudioDriver
        driver = FakeAudioDriver.__new__(FakeAudioDriver)
        driver.enabled, driver.last_error, driver._generation = True, None, 7
        driver._queue = queue.Queue(maxsize=1)
        asset = ROOT / "assets/cody/audio/model-cost/generated/model-cost.wav"
        driver.model_cost_notice(asset)
        self.assertIsNone(driver.last_error)
        self.assertEqual(driver._queue.qsize(), 1)
        payload, memory, requested_at, generation = driver._queue.get_nowait()
        self.assertTrue(memory)
        self.assertEqual(generation, 7)
        with wave.open(str(asset), "rb") as original:
            rate = original.getframerate()
            pcm = original.readframes(original.getnframes())
        with wave.open(io.BytesIO(payload), "rb") as actual:
            self.assertEqual((actual.getnchannels(), actual.getsampwidth(), actual.getframerate()), (1, 2, rate))
            self.assertEqual(actual.readframes(actual.getnframes()), bytes(round(rate * .1) * 2).join([pcm] * 5))
        driver.enabled = False
        driver.model_cost_notice(asset)
        self.assertTrue(driver._queue.empty(), "muted presentation left a deferred sound to replay")
        driver.enabled = True
        for invalid_rate, invalid_count in ((48000, 1593), (16000, 5601)):
            with self.subTest(invalid_rate=invalid_rate, invalid_count=invalid_count):
                invalid_wav = io.BytesIO()
                with wave.open(invalid_wav, "wb") as invalid:
                    invalid.setnchannels(1)
                    invalid.setsampwidth(2)
                    invalid.setframerate(invalid_rate)
                    invalid.writeframes(bytes(invalid_count * 2))
                original_reader = wave.open(io.BytesIO(invalid_wav.getvalue()), "rb")
                with patch("cody_fake_audio.wave.open", return_value=original_reader):
                    with self.assertLogs("CodyFakeAudio", level="ERROR"):
                        driver.model_cost_notice(asset)
                self.assertIsInstance(driver.last_error, ValueError)
                self.assertTrue(driver._queue.empty(), "invalid PCM entered the playback worker")

    # self: Production Home bridge with explicitly synthetic token, model, link and IMU observations.
    # Raises AssertionError for an incorrect three-panel cycle, replaced header or lost saved pixels.
    def test_home_three_panels_keep_native_header_and_command_ticker(self):
        from PIL import Image, ImageChops
        source = self.home_source()
        baseline = self.render_home(source)
        self.assertIsNotNone(baseline.crop((73, 40, 393, 185)).getbbox(), "native Home lost its character")
        self.assertIsNotNone(baseline.crop((73, 190, 393, 264)).getbbox(), "native Home lost its state/model")
        self.assertIsNotNone(baseline.crop((73, 343, 393, 368)).getbbox(), "native Home lost its timer/IMU")
        self.assertIsNotNone(baseline.crop((113, 408, 353, 433)).getbbox(), "native Home lost its commands")
        OUTPUT.mkdir(parents=True, exist_ok=True)
        pages = []
        panel = 0
        for name, expected in (("tokens", 0), ("model", 2), ("session", 3)):
            self.assertEqual(panel, expected)
            image = self.render_home(source, panel)
            self.assertEqual(image.crop((0, 0, 466, 268)).tobytes(), baseline.crop((0, 0, 466, 268)).tobytes())
            self.assertEqual(image.crop((0, 393, 466, 466)).tobytes(), baseline.crop((0, 393, 466, 466)).tobytes())
            if panel:
                bounds = ImageChops.difference(image, baseline).getbbox()
                self.assertGreaterEqual(bounds[1], 268)
                self.assertLessEqual(bounds[3], 393)
            pages.append(image.crop((40, 268, 426, 393)).tobytes())
            destination = OUTPUT / f"home-dashboard-{name}.png"
            self.renderer.capture_sheet(image, f"SIMULATED / Home dashboard / {name}").save(destination)
            with Image.open(destination) as stored:
                self.assertEqual(stored.convert("RGB").crop((0, 0, 466, 466)).tobytes(), image.tobytes())
            panel = self.native.next_companion_panel(panel)
        self.assertEqual(panel, 0)
        self.assertEqual(len(set(pages)), 3)
        with self.assertRaises(RuntimeError):
            self.render_home(source, 1)

    # self: Production command marquee and pause owner, without a Python text renderer.
    # Raises AssertionError if the real pixels fail to move right-to-left or jump across hidden time.
    def test_home_commands_scroll_and_pause_in_native_pixels(self):
        source = self.home_source()
        first = self.render_home(source, elapsed=1000000)
        shifted = self.render_home(source, elapsed=1050000)
        self.assertEqual(first.crop((114, 408, 353, 433)).tobytes(), shifted.crop((113, 408, 352, 433)).tobytes())
        self.assertNotEqual(first.crop((113, 408, 353, 433)).tobytes(), shifted.crop((113, 408, 353, 433)).tobytes())
        self.native.pause_activity()
        resumed = self.render_home(source, elapsed=20000000)
        self.assertEqual(shifted.crop((113, 408, 353, 433)).tobytes(), resumed.crop((113, 408, 353, 433)).tobytes())

    # self: Persistent production Studio bridge with exact active-time samples, without a wall-clock wait.
    # Raises AssertionError for an early/skipped panel, hidden-time jump, missing manual reset or compaction escape.
    def test_home_rotation_uses_shared_dwell_pause_and_manual_reset(self):
        from cody_ccp_panel import CcpPanel
        source = self.home_source()
        self.native.reset_home_rotation()
        panel = self.native.advance_home_rotation(0, 1000000, True, False)
        first = self.render_home(source, panel, elapsed=1000000)
        self.assertEqual(self.native.advance_home_rotation(panel, 8999999, True, False), 0)
        panels = [first.crop((40, 268, 426, 393)).tobytes()]
        for active_us, expected in ((9000000, 2), (17000000, 3), (25000000, 0)):
            panel = self.native.advance_home_rotation(panel, active_us, True, False)
            self.assertEqual(panel, expected)
            image = self.render_home(source, panel, elapsed=active_us)
            self.assertEqual(image.crop((0, 0, 466, 268)).tobytes(), first.crop((0, 0, 466, 268)).tobytes())
            if panel:
                panels.append(image.crop((40, 268, 426, 393)).tobytes())
        self.assertEqual(len(set(panels)), 3)
        self.assertEqual(self.native.advance_home_rotation(panel, 25000000, True, False), 0)
        self.assertEqual(self.native.advance_home_rotation(panel, 29000000, True, False), 0)
        # The actual Studio hidden-view hook pauses the shared owner with four seconds remaining.
        hidden = CcpPanel.__new__(CcpPanel)
        hidden.window = SimpleNamespace(renderer=self.renderer)
        hidden._companion_panel = panel
        hidden._activity_observation_clock_us = 29000000
        hidden.pause_home_presentation()
        self.assertEqual(self.native.advance_home_rotation(panel, 229000000, True, False), 0)
        self.assertEqual(self.native.advance_home_rotation(panel, 232999999, True, False), 0)
        panel = self.native.advance_home_rotation(panel, 233000000, True, False)
        self.assertEqual(panel, 2)
        # An accepted manual tap selects immediately and gives the selected panel a complete dwell.
        panel = self.native.next_companion_panel(panel)
        self.native.reset_home_rotation()
        self.assertEqual(self.native.advance_home_rotation(panel, 233000000, True, False), 3)
        self.assertEqual(self.native.advance_home_rotation(panel, 240999999, True, False), 3)
        panel = self.native.advance_home_rotation(panel, 241000000, True, False)
        self.assertEqual(panel, 0)
        panel = self.native.next_companion_panel(panel)
        panel = self.native.advance_home_rotation(panel, 242000000, True, True)
        self.assertEqual(panel, 0)
        self.assertEqual(self.native.advance_home_rotation(panel, 400000000, True, True), 0)
        self.assertEqual(self.native.advance_home_rotation(panel, 401000000, True, False), 0)
        self.assertEqual(self.native.advance_home_rotation(panel, 408999999, True, False), 0)
        self.assertEqual(self.native.advance_home_rotation(panel, 409000000, True, False), 2)
        # Shake returns Home and resets even when Home was already selected.
        self.native.reset_home_rotation()
        self.assertEqual(self.native.advance_home_rotation(0, 410000000, True, False), 0)
        self.assertEqual(self.native.advance_home_rotation(0, 417999999, True, False), 0)
        self.assertEqual(self.native.advance_home_rotation(0, 418000000, True, False), 2)

    # self: Production native heading layout measured with the generated Home font.
    # Raises the real native layout error if any admitted Home route title cannot fit its circular chord.
    def test_home_all_authored_headings_fit_complete_native_layout(self):
        source = self.home_source()
        routes = ("looking-for-pc", "connected", "project-active", "working", "response-ready",
                  "session-paused", "session-ended", "connection-lost", "companion-idle", "activity-unavailable")
        captions = [self.renderer.scene_painter.definition(route)["title"]["text"] for route in routes]
        captions += ["Waiting for you", "Compacting", "Connecting", "Disconnected", "Editing code",
                     "Running command", "Interrupted", "Ready to team up", "Ready for more", "No connection"]
        for caption in captions:
            with self.subTest(caption=caption):
                image = self.render_home(source, title=caption)
                self.assertIsNotNone(image.crop((33, 190, 433, 236)).getbbox())

    # self: Actual native Home pixels with host-only integer presentation and acquisition transforms.
    # Raises AssertionError if resizing adds colors, changes native pixels or maps one NxN block to different touch pixels.
    def test_home_integer_pixels_preserve_native_frame_and_touch(self):
        image = self.render_home(self.home_source())
        colors = set(image.getdata())
        OUTPUT.mkdir(parents=True, exist_ok=True)
        image.save(OUTPUT / "home-native-466.png")
        for zoom in (1, 2, 3, 4):
            side = 466 * zoom
            layout = PreviewLayout.fit(side + 57, side + 39, False)
            self.assertEqual(layout.scale, zoom)
            enlarged = enlarge_framebuffer(image, zoom)
            self.assertEqual(enlarged.size, (side, side))
            self.assertEqual(set(enlarged.getdata()), colors, "host zoom invented interpolated RGB values")
            for x, y in ((0, 0), (465, 465), (40, 233), (233, 233), (73, 73), (392, 392), (203, 276)):
                block = enlarged.crop((x * zoom, y * zoom, (x + 1) * zoom, (y + 1) * zoom))
                self.assertEqual(set(block.getdata()), {image.getpixel((x, y))})
                for rotation in (0, 90, 180, 270):
                    px, py = physical_point(x, y, rotation)
                    for dx in range(zoom):
                        for dy in range(zoom):
                            self.assertEqual(layout.to_device_pixel(layout.left + px * zoom + dx,
                                layout.top + py * zoom + dy, rotation), (x, y))
            self.assertIsNone(layout.to_device_pixel(layout.left - 1, layout.top, 0))
            self.assertIsNone(layout.to_device_pixel(layout.left + side, layout.top, 0))
            if zoom in (2, 3):
                enlarged.save(OUTPUT / f"home-native-{zoom}x.png")
        self.assertEqual(PreviewLayout.fit(200, 200, False).scale, 1)
        with self.assertRaises(ValueError):
            enlarge_framebuffer(image, 1.5)

    # self: Native Home frame presented by the actual CodyWindow Tk Canvas on the current desktop.
    # Raises original GUI/capture errors or AssertionError if the captured desktop pixels differ from integer source replication.
    def test_home_actual_simulator_window_integer_screenshot(self):
        import tkinter as tk
        from PIL import ImageGrab
        from cody_ui_dpi import configure_tk_scaling, prepare_display
        from scripts.common.cody_rig_types import Screen
        prepare_display()
        root = tk.Tk()
        root.withdraw()
        configure_tk_scaling(root)
        window = None
        try:
            window = CodyWindow(root, self.renderer, SimpleNamespace(
                screen="rig", rotation=0, safe_guide=False, ccp=False))
            window.audio_enabled.set(False)
            if window.after_id is not None:
                root.after_cancel(window.after_id)
                window.after_id = None
            window.playing.set(False)
            root.deiconify()
            root.lift()
            root.attributes("-topmost", True)
            root.update()
            image = self.render_home(self.home_source())
            layout = PreviewLayout.fit(window.canvas.winfo_width(), window.canvas.winfo_height(), False)
            target = 466 * 2 + 24
            two_times_fits = (window.available_ui_width >= root.winfo_width() + max(0, target - layout.width) and
                             window.available_ui_height >= root.winfo_height() + max(0, target - layout.height))
            if two_times_fits:
                self.assertGreaterEqual(layout.scale, 2, "the monitor can fit 2x but the default window did not admit it")
            window.present_frame(image, layout, Screen.GREETING, 0)
            root.update()
            # Let the real window compositor retire its paint before reading desktop pixels.
            deadline = time.monotonic() + .1
            while time.monotonic() < deadline:
                root.update()
                time.sleep(.01)
            # Capture only this foreground simulator's real client area and real Canvas.
            # No desktop-wide capture or synthetic window chrome enters these artifacts.
            x, y = window.canvas.winfo_rootx(), window.canvas.winfo_rooty()
            canvas_capture = ImageGrab.grab(bbox=(x, y, x + layout.width, y + layout.height), all_screens=True).convert("RGB")
            wx, wy = root.winfo_rootx(), root.winfo_rooty()
            window_capture = ImageGrab.grab(bbox=(wx, wy, wx + root.winfo_width(), wy + root.winfo_height()),
                                            all_screens=True).convert("RGB")
            OUTPUT.mkdir(parents=True, exist_ok=True)
            canvas_capture.save(OUTPUT / "home-simulator-canvas-screenshot.png")
            window_capture.save(OUTPUT / "home-simulator-window-screenshot.png")
            metadata = {"source": "SIMULATED", "capture": "actual Windows Tk client and Canvas",
                "framebuffer": [466, 466], "integer_zoom": layout.scale,
                "canvas": [layout.width, layout.height], "offset": [layout.left, layout.top],
                "window": [root.winfo_width(), root.winfo_height()]}
            (OUTPUT / "home-simulator-screenshot.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
            self.assertGreaterEqual(layout.scale, 1)
            self.assertGreaterEqual(layout.left, 0)
            self.assertGreaterEqual(layout.top, 0)
            displayed = canvas_capture.crop((layout.left, layout.top,
                layout.left + layout.image_size, layout.top + layout.image_size))
            self.assertEqual(displayed.tobytes(), enlarge_framebuffer(image, layout.scale).tobytes(),
                "actual simulator window changed or smoothed the native framebuffer pixels")
        finally:
            if window is not None:
                window.close()
            else:
                root.destroy()

    # self: Production actor, native orange heading and animated context arc through Studio's real bridge.
    # Raises AssertionError when current compaction fails to direct the actor or animate visible Home pixels.
    def test_home_compaction_capture_real_native_layers(self):
        source = self.home_source()
        source.compaction = 1
        source.compaction_valid = source.compaction_age_valid = True
        source.compaction_age_us = 12000000
        placement = self.native.home_transform()
        home_base = self.native.compose_response(self.renderer.rig["boot_channels"]["ready"],
            contour_defaults(self.renderer.rig), placement, 0, False)
        directed, cue = self.behavior.direct(home_base, placement, 1000000, enabled=True, activity=ACTIVE)
        self.assertEqual(cue, PersonalityCue.CONFUSION)
        self.assertNotEqual(directed.face, home_base.face)
        scene = self.native.evaluate(directed.face, directed.transform, directed.contour | {"phase": 0.})
        self.native.prepare_home_identity(scene, source, "Compacting", 10, 12000000)
        self.native.prepare_home_dashboard(scene, 0, source, 12000000)
        image = self.renderer.draw_scene(scene, debug=False, navigation_visible=False).convert("RGB")
        self.assertTrue(any(red > 160 and green > 60 and blue < 50 for red, green, blue in
                            image.crop((73, 190, 393, 236)).getdata()))
        self.assertIsNone(image.crop((113, 408, 353, 433)).getbbox(), "urgent compaction did not pause commands")
        later = self.render_home(source, elapsed=12100000, title="Compacting")
        self.assertNotEqual(image.crop((40, 292, 426, 393)).tobytes(), later.crop((40, 292, 426, 393)).tobytes())
        OUTPUT.mkdir(parents=True, exist_ok=True)
        self.renderer.capture_sheet(image, "SIMULATED / Home compaction / native actor, heading and arc").save(
            OUTPUT / "home-compaction-native-layers.png")

    # self: Actual native personality controller, compact Home geometry and automatically reserved firmware status bounds.
    # Raises AssertionError for hidden Wi-Fi/Confusion artwork, an inherited unsafe anchor or changed character placement.
    def test_home_personality_symbols_clear_real_status_and_survive_panel_changes(self):
        from PIL import ImageChops
        placement = self.native.home_transform()
        self.assertEqual(placement, (233., 122., 73.))
        exclusions = [(33, 185, 433, 264), (40, 268, 426, 393), (113, 408, 353, 433)]
        painter = NativePersonalityPainter(self.native)
        OUTPUT.mkdir(parents=True, exist_ok=True)
        for cue in (PersonalityCue.WIFI_CONNECTED, PersonalityCue.CONFUSION):
            with self.subTest(cue=cue.name):
                painter.disable(0)
                source = self.home_source()
                title = "Connected"
                if cue == PersonalityCue.CONFUSION:
                    source.compaction = 1
                    source.compaction_valid = source.compaction_age_valid = True
                    title = "Compacting"
                standard = self.native.evaluate(self.base.face, placement, self.base.contour | {"phase": 0.})
                for elapsed_ms in (0, 300, 700, 1100):
                    original = painter.prepare(standard, cue, placement[2], "working", 0, elapsed_ms, ())
                self.assertEqual(original.status, PersonalityPresentationStatus.VISIBLE)
                self.assertGreater(original.opacity, 0)
                for panel in (0, 2, 3):
                    scene = self.native.evaluate(self.base.face, placement, self.base.contour | {"phase": 0.})
                    self.native.prepare_home_identity(scene, source, title, len(title), 1100000)
                    self.native.prepare_home_dashboard(scene, panel, source, 1100000)
                    baseline = self.renderer.draw_scene(scene, debug=False, navigation_visible=False).convert("RGB")
                    # Home context and its status exclusion are supplied by the production bridge,
                    # including when switching layouts with the animation clock already paused.
                    frame = painter.prepare(scene, cue, placement[2], "working", 0, 1100, exclusions, paused=True)
                    self.assertEqual(frame.status, PersonalityPresentationStatus.VISIBLE)
                    self.assertEqual(frame.cue, cue)
                    self.assertEqual((frame.left, frame.width, frame.height),
                                     (original.left, original.width, original.height))
                    self.assertEqual(frame.top, original.top + 12)
                    self.assertGreaterEqual(frame.top, 64, "Home symbol still intersects the actual status strip")
                    image = self.renderer.draw_scene(scene, debug=False, navigation_visible=False).convert("RGB")
                    self.assertEqual(image.crop((122, 32, 365, 64)).tobytes(),
                                     baseline.crop((122, 32, 365, 64)).tobytes())
                    difference = ImageChops.difference(image, baseline).getbbox()
                    self.assertIsNotNone(difference, "native personality was admitted but no artwork reached the framebuffer")
                    self.assertGreaterEqual(difference[0], frame.left)
                    self.assertGreaterEqual(difference[1], frame.top)
                    self.assertLessEqual(difference[2], frame.left + frame.width)
                    self.assertLessEqual(difference[3], frame.top + frame.height)
                    if panel == 0:
                        self.renderer.capture_sheet(image, f"SIMULATED / Home native {cue.name}").save(
                            OUTPUT / f"home-personality-{cue.name.lower()}.png")

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
        facts = dict(current=True, present=True, percentage_valid=True, percentage=20.5, charging=False)
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.NONE)
        facts["percentage"] = 20.
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.LOW_BATTERY)
        facts["percentage"] = 24.5
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.LOW_BATTERY)
        facts["percentage"] = 25.
        self.assertEqual(self.behavior.power_cue(**facts), PersonalityCue.NONE)
        self.assertFalse(self.behavior.power_low_active)
        facts["percentage"] = 20.
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
