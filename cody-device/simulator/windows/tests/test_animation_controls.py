"""Studio animation controls using the real controller and a synthetic clock."""

import unittest
from unittest.mock import Mock, patch

from cody_device_ui import ANIMATION_SCENES, DeviceStudioWindow


class AnimationControlsTests(unittest.TestCase):
    # self: Controller fixture with no Tk window or native renderer.
    def setUp(self):
        self.studio = DeviceStudioWindow.__new__(DeviceStudioWindow)
        self.studio.animation_choice = Mock()
        self.studio.animation_choice.current.return_value = 1
        self.studio.open = Mock()
        self.studio.prepare_setup_mascot = Mock()
        self.muse = Mock()
        self.studio.avatar_preview = Mock(return_value=self.muse)
        self.studio.muse_seed = 42
        self.studio.animation_scene = None
        self.studio.animation_playing = False
        self.studio.animation_elapsed_ms = 0.0
        self.studio.animation_last_frame_at = None
        self.studio.animation_frame = None

    # self: Selection starts paused and resets the shared renderer.
    def test_select_and_play_pause_resume_restart(self):
        studio = self.studio
        studio.animation_select()
        self.assertEqual(studio.animation_scene, ANIMATION_SCENES[1])
        self.assertFalse(studio.animation_playing)
        self.muse.reset.assert_called_with(0)
        studio.animation_play()
        studio.advance_animation_clock(10.0)
        with patch("cody_device_ui.time.monotonic", return_value=10.75):
            studio.animation_pause()
        self.assertFalse(studio.animation_playing)
        self.assertEqual(studio.animation_elapsed_ms, 750.0)
        studio.animation_play()
        studio.advance_animation_clock(20.0)
        studio.advance_animation_clock(20.25)
        self.assertEqual(studio.animation_elapsed_ms, 1000.0)
        studio.animation_restart()
        self.assertTrue(studio.animation_playing)
        self.assertEqual(studio.animation_elapsed_ms, 0.0)

    # self: Returning to the showcase restores its own seed and clears manual state.
    def test_showcase_clears_manual_pose(self):
        self.studio.animation_select()
        self.studio.animation_play()
        self.studio.animation_normal()
        self.assertIsNone(self.studio.animation_scene)
        self.assertFalse(self.studio.animation_playing)
        self.assertEqual(self.studio.animation_elapsed_ms, 0.0)
        self.muse.reset.assert_called_with(42)

    # self: A pending physical command disables duplicate sends until completion.
    def test_physical_request_tracks_pending_until_callback(self):
        studio = self.studio
        studio.animation_device_pending = False
        studio.animation_device_buttons = [Mock(), Mock()]
        studio.animation_device_status = Mock()
        studio.animation_transport = Mock()
        studio.animation_device_request("select")
        studio.animation_device_request("play")
        studio.animation_transport.request.assert_called_once_with(
            "listening_question", "select", studio.animation_device_complete)
        self.assertTrue(studio.animation_device_pending)
        studio.animation_device_complete(True, "applied")
        self.assertFalse(studio.animation_device_pending)
        for button in studio.animation_device_buttons:
            button.configure.assert_called_with(state="normal")


if __name__ == "__main__":
    unittest.main()
