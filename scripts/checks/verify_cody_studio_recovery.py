"""Host-only Studio result recovery and atomic review checks; no native runtime or Tk window."""

from contextlib import ExitStack
from hashlib import sha256
from pathlib import Path
import queue
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
from uuid import uuid4

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))

import cody_studio_comparison as reviews
import cody_studio_results as results
from cody_studio_runner import CaptureRecord
from cody_studio_window import StudioWindow

OUTPUT = ROOT / "artifacts/simulator/studio-recovery-checks"


class CaptureFixture(unittest.TestCase):
    """Small isolated host files; native-evidence validation is a separate contract."""

    # self: Independent check with a small PNG and mocked native-evidence validation.
    # Exceptions: Filesystem/image failures propagate; all generated files stay in artifacts.
    def setUp(self):
        self.directory = OUTPUT / uuid4().hex
        self.directory.mkdir(parents=True, exist_ok=False)
        path = self.directory / "frame.png"
        Image.new("RGB", (4, 4), (26, 47, 68)).save(path)
        metadata = {"capture_id": uuid4().hex, "id": "ui-v1:display", "inventory_id": "P04",
                    "frame_identity": {}, "rendering_mode": "runtime-full", "at_ms": 0,
                    "png_sha256": sha256(path.read_bytes()).hexdigest(), "pixel_sha256": "a" * 64}
        self.record = CaptureRecord(path, self.directory / "capture.json", metadata)
        self.entry = {"id": "ui-v1:display", "inventory_id": "P04", "title": "Display"}
        self.review = {"status": "unreviewed", "note": "", "count": 0}


class StudioRecoveryTests(CaptureFixture):
    """Exercise the real poll/adoption boundary with file failures and later valid results."""

    # self: Case defining candidate evidence without starting a native runtime.
    # failures: Optional original exception by evidence reader name, raised once.
    # Exceptions: None during setup; the staged controller receives injected failures.
    def evidence_readers(self, failures=None):
        stack = ExitStack()
        values = {"load_capture": self.record, "entry_for": self.entry,
                  "routes_for": [], "load_review": self.review}
        for name, value in values.items():
            failure = (failures or {}).get(name)
            stack.enter_context(patch.object(results, name, return_value=value,
                                            side_effect=[failure, value] if failure else None))
        stack.enter_context(patch("cody_studio_window.ImageTk.PhotoImage", return_value=object()))
        return stack

    # self: Host-only case whose widget methods are inert.
    # Exceptions: None; actual poll, result preparation and selection methods remain in use.
    def window(self):
        window = StudioWindow.__new__(StudioWindow)
        window.closed, window.busy = False, True
        window.root = Mock()
        window.results = queue.Queue()
        window.capture_results = results.CaptureResultController()
        window.capture_results.current = SimpleNamespace(record=object())
        window.baseline = None
        window.mode = Mock(get=Mock(return_value="current"))
        window.opacity = Mock(get=Mock(return_value=0.5))
        window.route_records = {}
        window.records = []
        window.live_image = None
        window.mode_controls = []
        window.canvas = Mock(winfo_width=Mock(return_value=720), winfo_height=Mock(return_value=540))
        window.tree = Mock(exists=Mock(return_value=False))
        for name in ("poll_atlas", "poll_cue_audition", "pause_timeline", "error", "apply_button",
                     "status", "frame_title", "frame_subtitle", "describe_entry", "add_capture_record",
                     "refresh_evidence_filter", "note", "review", "refresh_section", "draw"):
            setattr(window, name, Mock())
        return window

    # self: Recoverable poll tested at capture, catalog, routes and review I/O boundaries.
    # Exceptions: AssertionError if a rejection changes the selection or prevents later adoption.
    def test_failed_adoption_preserves_capture_and_next_scheduled_result_recovers(self):
        for reader in ("load_capture", "entry_for", "routes_for", "load_review"):
            with self.subTest(reader=reader):
                failure = OSError(f"{reader}: unavailable")
                window = self.window()
                retained = window.current
                retained_presentation = window.capture_results.current
                for _ in range(2):
                    window.results.put((None, self.record, None, {"kind": "capture"}))
                with self.evidence_readers({reader: failure}):
                    window.poll()
                    self.assertIs(window.current, retained)
                    self.assertIs(window.capture_results.current, retained_presentation)
                    window.draw.assert_not_called()
                    window.error.assert_called_once_with(failure)
                    window.apply_button.configure.assert_called_with(state="normal")
                    self.assertFalse(window.busy)
                    self.assertEqual(window.root.after.call_args.args[0], 40)
                    window.root.after.call_args.args[1]()
                self.assertIs(window.current, self.record)
                self.assertIs(window.capture_results.current.record, self.record)
                window.draw.assert_called_once()
                self.assertEqual(window.root.after.call_count, 2)
                self.assertEqual(window.error.call_count, 1)
                self.assertTrue(window.results.empty())

    # self: Poll that receives an ordinary native-worker exception before a valid result.
    # Exceptions: AssertionError if the original cause is lost or the queue stops advancing.
    def test_worker_failure_does_not_block_next_completed_capture(self):
        window = self.window()
        failure = RuntimeError("native owner rejected the request")
        window.results.put((None, None, failure, {"kind": "capture"}))
        window.results.put((None, self.record, None, {"kind": "capture"}))
        with self.evidence_readers():
            window.poll()
            window.root.after.call_args.args[1]()
        window.error.assert_called_once_with(failure)
        self.assertIs(window.current, self.record)
        self.assertEqual(window.root.after.call_count, 2)

    # self: A route completion whose review fails before destination inputs can be adopted.
    # Exceptions: AssertionError if rejected evidence changes the scenario or observed route list.
    def test_rejected_route_does_not_adopt_destination_or_observation(self):
        window = self.window()
        window.scenario = {"id": "retained-scenario"}
        window.show_scenario = Mock()
        retained = window.scenario
        failure = OSError("route review unavailable")
        window.results.put((None, self.record, None, {"kind": "route", "route_id": "display-back"}))
        with self.evidence_readers({"load_review": failure}):
            window.poll()
        self.assertIs(window.scenario, retained)
        self.assertEqual(window.route_records, {})
        window.show_scenario.assert_not_called()
        window.error.assert_called_once_with(failure)
        window.root.after.assert_called_once()

    # self: Window closed by the user while the original failure is displayed.
    # Exceptions: AssertionError if recovery schedules a callback on a closed window.
    def test_error_dialog_close_does_not_reschedule_the_window(self):
        window = self.window()
        failure = OSError("capture disappeared")
        window.error.side_effect = lambda error: setattr(window, "closed", True)
        window.results.put((None, self.record, None, {"kind": "capture"}))
        with self.evidence_readers({"load_capture": failure}):
            window.poll()
        window.error.assert_called_once_with(failure)
        window.root.after.assert_not_called()

    # self: Capture whose PNG changes after its first validation.
    # Exceptions: AssertionError if replacement bytes become an adopted result.
    def test_changed_png_rejects_adoption_before_the_view_changes(self):
        window = self.window()
        retained = window.current
        retained_presentation = window.capture_results.current
        Image.new("RGB", (4, 4), (90, 80, 70)).save(self.record.image_path)
        window.results.put((None, self.record, None, {"kind": "capture"}))
        with self.evidence_readers():
            window.poll()
        self.assertIs(window.current, retained)
        self.assertIs(window.capture_results.current, retained_presentation)
        window.draw.assert_not_called()
        self.assertIn("changed while preparing", str(window.error.call_args.args[0]))
        window.root.after.assert_called_once()

    # self: The real draw method receiving staged evidence, without a real Tk interpreter.
    # Exceptions: AssertionError if image allocation failure is swallowed or changes the canvas.
    def test_staged_draw_propagates_photo_failure_instead_of_reporting_success(self):
        window = self.window()
        window.draw = StudioWindow.draw.__get__(window)
        retained = window.capture_results.current
        failure = RuntimeError("Tk frame allocation failed")
        with self.evidence_readers():
            presentation = window.prepare_presentation(self.record)
            with patch("cody_studio_window.ImageTk.PhotoImage", side_effect=failure):
                with self.assertRaises(RuntimeError) as rejected:
                    window.draw(presentation)
        self.assertIs(rejected.exception, failure)
        self.assertIs(window.capture_results.current, retained)
        window.canvas.delete.assert_not_called()
        window.error.assert_not_called()

    # self: An adoption whose frame or thumbnail cannot allocate its Tk image.
    # Exceptions: AssertionError if the failed candidate changes details, notes or capture identity.
    def test_photo_failure_precedes_all_capture_view_mutations(self):
        for thumbnail, kind in ((False, "capture"), (True, "capture"), (False, "route"), (True, "route")):
            with self.subTest(thumbnail=thumbnail, kind=kind):
                window = self.window()
                window.draw = StudioWindow.draw.__get__(window)
                window.scenario = {"id": "retained-scenario"}
                window.show_scenario = Mock()
                scenario = window.scenario
                retained = window.capture_results.current
                failure = RuntimeError("Tk image allocation failed")
                window.results.put((None, self.record, None, {"kind": kind, "route_id": "display-back"}))
                with self.evidence_readers(), patch("cody_studio_window.ImageTk.PhotoImage",
                        side_effect=[object(), failure] if thumbnail else failure):
                    window.poll()
                self.assertIs(window.capture_results.current, retained)
                self.assertIs(window.scenario, scenario)
                for control in (window.frame_title, window.frame_subtitle, window.note, window.review):
                    self.assertEqual(control.mock_calls, [])
                window.describe_entry.assert_not_called()
                window.show_scenario.assert_not_called()
                window.add_capture_record.assert_not_called()
                window.canvas.delete.assert_not_called()
                window.error.assert_called_once_with(failure)
                window.root.after.assert_called_once()

    # self: Candidate whose catalog is temporarily unavailable to a normal catalog refresh.
    # Exceptions: AssertionError if the existing tree is cleared before catalog validation.
    def test_catalog_read_failure_preserves_existing_tree(self):
        window = self.window()
        with patch("cody_studio_window.load_catalog", side_effect=OSError("catalog unavailable")):
            with self.assertRaisesRegex(OSError, "catalog unavailable"):
                window.catalog()
        window.tree.delete.assert_not_called()


class ReviewPublicationTests(CaptureFixture):
    """Incomplete review writes remain unpublished and never poison earlier notes."""

    # self: Existing review plus a write interrupted before complete JSON is available.
    # Exceptions: AssertionError if readers see partial data or recovery loses the prior event.
    def test_interrupted_review_write_keeps_previous_note_readable_and_allows_next_save(self):
        with patch.object(reviews, "ARTIFACT_ROOT", self.directory), \
                patch.object(reviews, "load_capture", return_value=self.record):
            first = reviews.save_review(self.record, "Keep this note", "annotated")
            retained = first.read_bytes()

            # value: Event being serialized; it must remain invisible until fully published.
            # output: Temporary file stream belonging only to this candidate event.
            # options: Standard json.dump settings supplied by the persistence owner.
            # Exceptions: The original injected I/O failure simulates an interrupted save.
            def interrupt(value, output, **options):
                output.write('{"schema":')
                output.flush()
                self.assertEqual(reviews.load_review(self.record)["note"], "Keep this note")
                raise OSError("review write interrupted")

            with patch.object(reviews.json, "dump", side_effect=interrupt):
                with self.assertRaisesRegex(OSError, "review write interrupted"):
                    reviews.save_review(self.record, "Incomplete note", "approved")
            self.assertEqual(first.read_bytes(), retained)
            self.assertEqual(reviews.load_review(self.record)["count"], 1)
            reviews.save_review(self.record, "Complete replacement review", "approved")
            current = reviews.load_review(self.record)
            self.assertEqual((current["note"], current["status"], current["count"]),
                             ("Complete replacement review", "approved", 2))

    # self: Review persistence with flush and publication failures after JSON serialization.
    # Exceptions: AssertionError if a failed durable write or rename publishes an event.
    def test_sync_and_publication_failures_do_not_publish_a_review(self):
        with patch.object(reviews, "ARTIFACT_ROOT", self.directory), \
                patch.object(reviews, "load_capture", return_value=self.record):
            for operation in ("sync", "publish"):
                with self.subTest(operation=operation):
                    target = patch.object(reviews.os, "fsync", side_effect=OSError("sync failed")) if operation == "sync" else \
                        patch.object(Path, "replace", side_effect=OSError("publish failed"))
                    with target, self.assertRaisesRegex(OSError, f"{operation} failed"):
                        reviews.save_review(self.record, "Not published", "annotated")
                    self.assertEqual(reviews.load_review(self.record), self.review)
            reviews.save_review(self.record, "Published", "annotated")
            self.assertEqual(reviews.load_review(self.record)["note"], "Published")


# No parameters: run only the focused host persistence and result-recovery suite.
# Exceptions: unittest reports assertions and returns a failing process status.
def main():
    suite = unittest.TestSuite()
    loader = unittest.TestLoader()
    suite.addTests(loader.loadTestsFromTestCase(StudioRecoveryTests))
    suite.addTests(loader.loadTestsFromTestCase(ReviewPublicationTests))
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
