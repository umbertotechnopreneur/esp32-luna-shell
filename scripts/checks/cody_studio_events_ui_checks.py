"""Real Tk/native UI acceptance for direct Codex-event inspection."""

import unittest

from cody_studio_catalog import entry_for
from cody_studio_scenario import default_scenario, scenario_revision


# base_class: Actual NativeUiTests fixture running in a dedicated native process.
# Returns only this event workflow, without repeating inherited broad UI cases.
def ui_suite(base_class):
    class EventUiTests(base_class):
        """Use the product's real Treeview, buttons, native frames and baseline."""

        # self: A real Tk/native session with an initially completed P04 capture.
        # Raises AssertionError for the wrong frame, lost baseline or distorted native pixels.
        def test_open_event_destinations_preserves_display_baseline(self):
            window = self.window
            self.root.geometry("1440x900")
            self.root.update()
            self.assertEqual(window.current.metadata["id"], entry_for("P04")["id"])
            if not window.sections["Comparison"][2]:
                window.sections["Comparison"][0].invoke()
                self.root.update()
            self.button("Preserve current as baseline").invoke()
            baseline = window.baseline
            self.assertIsNotNone(baseline)
            retained = (baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes())
            window.filter.set("All")
            window.search.set("")
            self.root.update()

            cases = (("atlas-v1:21", "cody.event.pr-pending", "studio-event-pr-pending.png"),
                     ("atlas-v1:24", "cody.event.check-failures", "studio-event-check-failures.png"),
                     ("ui-v1:checks-passed", "cody.event.checks-passed", "studio-event-checks-passed.png"),
                     ("ui-v1:review-reply", "cody.event.review-reply", "studio-event-review-reply.png"),
                     ("ui-v1:reply-ready-detail", "cody.event.reply-ready", "studio-event-reply-ready.png"),
                     ("ui-v1:event-unavailable", "cody.event.unavailable", "studio-event-unavailable.png"))
            for identity, target, screenshot in cases:
                with self.subTest(identity=identity):
                    self.assertTrue(window.tree.exists(identity), f"Missing catalog entry {identity}")
                    previous, failures = window.current, len(self.errors)
                    window.tree.selection_set(identity)
                    window.tree.see(identity)
                    self.root.update()
                    self.button("Open destination").invoke()
                    self.wait_for(lambda: len(self.errors) > failures or
                                  (not window.busy and window.current is not previous),
                                  f"direct native {identity} entry", timeout=45)
                    self.assertEqual(len(self.errors), failures, f"Native UI entry failed: {identity}")
                    record = window.current
                    scenario = default_scenario(identity)
                    receipt = record.metadata["frame_identity"]
                    self.assertEqual(record.metadata["id"], identity)
                    self.assertEqual(record.metadata["rendering_mode"], "presenter-isolated")
                    self.assertEqual(receipt["target"], target)
                    self.assertEqual(receipt["rendering_mode"], "presenter-isolated")
                    self.assertEqual(receipt["scenario_revision"], scenario_revision(scenario))
                    self.assertEqual(receipt["at_ms"], 0)
                    self.assertEqual(window.form(), scenario)
                    self.assertIs(window.baseline, baseline)
                    self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)
                    self.root.update()
                    self.assertIn("Not comparable", window.compare_status.get())
                    self.assert_frame_geometry(1, minimum_height=100)
                    self.screenshot(screenshot)

    return unittest.TestSuite([EventUiTests("test_open_event_destinations_preserves_display_baseline")])
