"""Host-only preference preservation checks, included by the public Studio suite."""

from copy import deepcopy
import json
from pathlib import Path
import unittest
from unittest.mock import Mock, patch
from uuid import uuid4

from cody_studio_preferences import PreferenceStore

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "artifacts/simulator/studio-checks"


class PreferenceStoreTests(unittest.TestCase):
    """Unreadable preferences remain intact across automatic save attempts."""

    # self: Test case owning a retained, isolated directory beneath artifacts.
    # Raises OSError if the fixture directory cannot be created.
    def setUp(self):
        self.directory = OUTPUT / uuid4().hex
        self.directory.mkdir(parents=True, exist_ok=False)
        self.path = self.directory / "ui-state.json"
        self.preferences = {
            "schema": "cody.studio.ui.v1",
            "expanders": {"Scenario": True, "Review notes": False},
            "left": True,
            "right": False,
            "atlas_suite": "Pilot (10)",
            "baseline": "artifacts/simulator/studio/baselines/retained/capture.json",
        }

    # self: A genuinely absent file, without any prior read error.
    # Raises AssertionError if normal first-use persistence is blocked or loses values.
    def test_absent_file_can_be_created_after_successful_load(self):
        store = PreferenceStore(self.path)
        self.assertEqual(store.load(), {})
        self.assertIsNone(store.error)
        self.assertFalse(self.path.exists())
        store.save(self.preferences)
        self.assertEqual(PreferenceStore(self.path).load(), self.preferences)
        self.assertIsNone(store.error)

    # self: Existing preferences retaining an explicit preserved-baseline reference.
    # Raises AssertionError if ordinary edits discard unrelated preference values.
    def test_valid_file_round_trip_retains_baseline_and_optional_fields(self):
        self.path.write_text(json.dumps(self.preferences), encoding="utf-8")
        store = PreferenceStore(self.path)
        loaded = store.load()
        self.assertEqual(loaded, self.preferences)
        loaded["left"] = False
        loaded["expanders"]["Review notes"] = True
        store.save(loaded)
        self.assertEqual(PreferenceStore(self.path).load(), loaded)
        self.assertEqual(loaded["baseline"], self.preferences["baseline"])

    # self: The current schema with all optional host preferences absent.
    # Raises AssertionError if absence of optional fields is treated as corruption.
    def test_schema_only_file_is_valid_without_implicit_repair(self):
        raw = b'{"schema":"cody.studio.ui.v1"}'
        self.path.write_bytes(raw)
        store = PreferenceStore(self.path)
        self.assertEqual(store.load(), {"schema": "cody.studio.ui.v1"})
        self.assertEqual(self.path.read_bytes(), raw)
        self.assertIsNone(store.error)

    # self: Malformed bytes, unsupported schemas and invalid present-field types.
    # Raises AssertionError if repeated save attempts overwrite a rejected file.
    def test_invalid_files_block_repeated_saves_and_preserve_exact_bytes(self):
        invalid_objects = {
            "wrong-schema": {**self.preferences, "schema": "unknown"},
            "missing-schema": {"baseline": self.preferences["baseline"]},
            "non-object": [],
            "left-number": {**self.preferences, "left": 1},
            "right-text": {**self.preferences, "right": "false"},
            "expanders-list": {**self.preferences, "expanders": []},
            "expander-number": {**self.preferences, "expanders": {"Scenario": 1}},
            "atlas-suite": {**self.preferences, "atlas_suite": "unknown"},
            "baseline-empty": {**self.preferences, "baseline": ""},
            "baseline-number": {**self.preferences, "baseline": 1},
            "unknown-field": {**self.preferences, "unexpected": True},
        }
        invalid_bytes = {name: json.dumps(value).encode("utf-8")
                         for name, value in invalid_objects.items()}
        invalid_bytes.update({
            "truncated-json": b'{"schema":"cody.studio.ui.v1",',
            "invalid-utf8": b'\xff',
            "duplicate-schema": b'{"schema":"unknown","schema":"cody.studio.ui.v1"}',
            "duplicate-expander": b'{"schema":"cody.studio.ui.v1",'
                                  b'"expanders":{"Scenario":false,"Scenario":true}}',
        })
        for name, raw in invalid_bytes.items():
            with self.subTest(case=name):
                directory = self.directory / name
                directory.mkdir()
                path = directory / "ui-state.json"
                path.write_bytes(raw)
                store = PreferenceStore(path)
                with self.assertRaises(ValueError) as rejected:
                    store.load()
                self.assertIs(store.error, rejected.exception)
                for _ in range(2):
                    with self.assertRaises(ValueError) as repeated:
                        store.save(self.preferences)
                    self.assertIs(repeated.exception, rejected.exception)
                    self.assertEqual(path.read_bytes(), raw)
                self.assertEqual(list(directory.iterdir()), [path])

    # self: Existing valid bytes whose first read is denied by the filesystem.
    # Raises AssertionError if a permission failure becomes an absent-file default.
    def test_read_permission_failure_preserves_cause_and_baseline(self):
        original = json.dumps(self.preferences).encode("utf-8")
        self.path.write_bytes(original)
        store = PreferenceStore(self.path)
        denied = PermissionError("injected preference read denial")
        with patch.object(Path, "open", side_effect=denied):
            with self.assertRaises(PermissionError) as rejected:
                store.load()
        self.assertIs(rejected.exception, denied)
        self.assertIs(store.error, denied)
        with self.assertRaises(PermissionError) as repeated:
            store.save(self.preferences)
        self.assertIs(repeated.exception, denied)
        self.assertEqual(self.path.read_bytes(), original)

    # self: A session that already rejected its file before an external repair.
    # Raises AssertionError if repair or removal silently enables that session's writes.
    def test_read_failure_stays_latched_until_a_new_store_loads_repair(self):
        self.path.write_bytes(b"{broken")
        store = PreferenceStore(self.path)
        with self.assertRaises(ValueError) as rejected:
            store.load()
        repaired = json.dumps(self.preferences).encode("utf-8")
        self.path.write_bytes(repaired)
        with self.assertRaises(ValueError) as repeated_load:
            store.load()
        self.assertIs(repeated_load.exception, rejected.exception)
        with self.assertRaises(ValueError) as repeated_save:
            store.save(self.preferences)
        self.assertIs(repeated_save.exception, rejected.exception)
        self.assertEqual(self.path.read_bytes(), repaired)
        self.assertEqual(PreferenceStore(self.path).load(), self.preferences)
        self.path.unlink()
        with self.assertRaises(ValueError):
            store.save(self.preferences)
        self.assertFalse(self.path.exists())

    # self: Preferences that become unreadable while their window remains open.
    # Raises AssertionError if a stale successful load authorizes destructive replacement.
    def test_corruption_after_load_is_rejected_before_save(self):
        self.path.write_text(json.dumps(self.preferences), encoding="utf-8")
        store = PreferenceStore(self.path)
        store.load()
        corrupted = b'{"baseline":"retained",'
        self.path.write_bytes(corrupted)
        with self.assertRaises(ValueError) as rejected:
            store.save(self.preferences)
        self.assertIs(store.error, rejected.exception)
        self.assertEqual(self.path.read_bytes(), corrupted)
        self.assertEqual(list(self.directory.iterdir()), [self.path])

    # self: A first-use session whose absent file is created with invalid bytes later.
    # Raises AssertionError if startup absence remains permission to overwrite new corruption.
    def test_invalid_file_appearing_after_absent_load_is_preserved(self):
        store = PreferenceStore(self.path)
        self.assertEqual(store.load(), {})
        appeared = b'{"schema":"cody.studio.ui.v1","baseline":'
        self.path.write_bytes(appeared)
        with self.assertRaises(ValueError) as rejected:
            store.save(self.preferences)
        self.assertIs(store.error, rejected.exception)
        self.assertEqual(self.path.read_bytes(), appeared)
        self.assertEqual(list(self.directory.iterdir()), [self.path])

    # self: Existing valid preferences and a failed atomic publication operation.
    # Raises AssertionError if failed publication truncates or replaces the destination.
    def test_atomic_replace_failure_keeps_original_and_latches_error(self):
        original = json.dumps(self.preferences).encode("utf-8")
        self.path.write_bytes(original)
        store = PreferenceStore(self.path)
        store.load()
        changed = deepcopy(self.preferences)
        changed["left"] = False
        denied = PermissionError("injected preference replacement denial")
        with patch.object(Path, "replace", side_effect=denied):
            with self.assertRaises(PermissionError) as rejected:
                store.save(changed)
        self.assertIs(rejected.exception, denied)
        self.assertIs(store.error, denied)
        self.assertEqual(self.path.read_bytes(), original)
        temporary = list(self.directory.glob("*.tmp"))
        self.assertEqual(len(temporary), 1)
        self.assertEqual(json.loads(temporary[0].read_text(encoding="utf-8")), changed)
        with self.assertRaises(PermissionError) as repeated:
            store.save(changed)
        self.assertIs(repeated.exception, denied)
        self.assertEqual(self.path.read_bytes(), original)
        self.assertEqual(list(self.directory.glob("*.tmp")), temporary)

    # self: A caller that attempts persistence without establishing a readable state.
    # Raises AssertionError if save-before-load or a schema-less payload creates a file.
    def test_save_requires_loaded_state_and_valid_schema(self):
        store = PreferenceStore(self.path)
        with self.assertRaises(RuntimeError):
            store.save(self.preferences)
        self.assertFalse(self.path.exists())
        loaded_store = PreferenceStore(self.path)
        self.assertEqual(loaded_store.load(), {})
        with self.assertRaises(ValueError):
            loaded_store.save({})
        self.assertFalse(self.path.exists())

    # self: A preference pathname redirected to valid bytes or a nonexistent target.
    # Raises AssertionError if either symlink is read or replaced as ordinary preferences.
    def test_preference_symlinks_are_rejected_without_touching_targets(self):
        target = self.directory / "target.json"
        original = json.dumps(self.preferences).encode("utf-8")
        target.write_bytes(original)
        for name, destination in (("existing", target), ("dangling", self.directory / "absent.json")):
            with self.subTest(case=name):
                path = self.directory / f"{name}.json"
                try:
                    path.symlink_to(destination)
                except (OSError, NotImplementedError) as error:
                    self.skipTest(f"Host cannot create a symlink fixture: {error}")
                store = PreferenceStore(path)
                with self.assertRaises((OSError, ValueError, RuntimeError)) as rejected:
                    store.load()
                self.assertIs(store.error, rejected.exception)
                with self.assertRaises(type(rejected.exception)):
                    store.save(self.preferences)
                self.assertTrue(path.is_symlink())
                self.assertEqual(target.read_bytes(), original)
                self.assertFalse((self.directory / "absent.json").exists())

    # self: Real window persistence/close methods with mocked widgets and job owners.
    # Raises AssertionError if close destroys unreadable preferences or hides their warning.
    def test_window_save_and_close_preserve_invalid_file_and_warning(self):
        # Importing the window reads only its authored palette; native loading and
        # Tk construction are forbidden throughout this host-only integration case.
        with patch("ctypes.CDLL", side_effect=AssertionError("Unexpected native library load")), \
                patch("tkinter.Tk", side_effect=AssertionError("Unexpected Tk window")):
            from cody_studio_window import StudioWindow

            original = b'{"schema":"cody.studio.ui.v1","baseline":"retained",'
            self.path.write_bytes(original)
            window = StudioWindow.__new__(StudioWindow)
            window.preference_store = PreferenceStore(self.path)
            window.preferences_warning = Mock(spec=["set"])
            window.preferences_warning_label = Mock(spec=["pack"])
            window.status = Mock(spec=["set"])
            window.status_label = object()
            window.root = Mock(spec=["after_cancel", "unbind", "destroy"])
            window.sections = {"Scenario": (None, None, True)}
            window.left_visible = Mock(get=Mock(return_value=True))
            window.right_visible = Mock(get=Mock(return_value=False))
            window.atlas_suite = Mock(get=Mock(return_value="Pilot (10)"))
            window.baseline = window.unavailable_baseline = None
            window.stop_cue_audition = Mock()
            window.pause_timeline = Mock()
            window.atlas_job = window.atlas_watcher = None
            window.closed = False
            window.poll_id, window.initial_apply_id = "poll", "apply"
            window.focus_reveal_id, window.focus_binding = "focus", "binding"
            window.runner = Mock(spec=["cancel_worker"])
            window.executor = Mock(spec=["shutdown"])
            window.atlas_executor = Mock(spec=["shutdown"])

            self.assertEqual(window.read_preferences(), {})
            failure = window.preference_store.error
            self.assertIsInstance(failure, ValueError)
            warning = window.preferences_warning.set.call_args.args[0]
            self.assertIn(str(self.path), warning)
            self.assertIn(str(failure), warning)
            self.assertIn("reopen", warning)

            window.persist()
            window.persist()
            self.assertEqual(self.path.read_bytes(), original)
            self.assertIs(window.preference_store.error, failure)
            warning_calls = window.preferences_warning.set.call_count
            window.status.set("Acquired native frame")
            self.assertEqual(window.preferences_warning.set.call_count, warning_calls)
            self.assertEqual(window.preferences_warning.set.call_args.args[0], warning)
            window.preferences_warning_label.pack.assert_called()

            window.close()
            self.assertTrue(window.closed)
            window.root.destroy.assert_called_once_with()
            window.runner.cancel_worker.assert_called_once_with()
            window.executor.shutdown.assert_called_once_with(wait=False, cancel_futures=True)
            window.atlas_executor.shutdown.assert_called_once_with(wait=False, cancel_futures=True)
            self.assertEqual(window.preferences_warning.set.call_args.args[0], warning)
            self.assertEqual(self.path.read_bytes(), original)
            self.assertEqual(list(self.directory.iterdir()), [self.path])
