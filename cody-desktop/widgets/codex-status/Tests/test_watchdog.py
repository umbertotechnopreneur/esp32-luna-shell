import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("widget_guard", Path(__file__).resolve().parents[1]/"Linux/watchdog.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class TreeOwnershipChecks(unittest.TestCase):
    def test_only_widget_descendants_are_included(self):
        snapshot = {1: {"parent": 0}, 2: {"parent": 1}, 3: {"parent": 2},
                    4: {"parent": 1}, 5: {"parent": 4}, 99: {"parent": 0}}
        self.assertEqual(set(guard.descendants(1, snapshot, excluded=[4])), {1, 2, 3})

    def test_tracked_orphan_remains_owned(self):
        snapshot = {1: {"parent": 0}, 2: {"parent": 999}}
        self.assertEqual(set(guard.descendants(1, snapshot, retained=[2])), {1, 2})

    def test_reused_pid_is_never_signalled(self):
        with patch.object(guard, "process_info", return_value={"started": 202}), patch.object(guard.os, "kill") as kill:
            guard.stop_owned({123: {"started": 101}})
            kill.assert_not_called()

    def test_owned_identity_is_stopped_then_killed(self):
        with patch.object(guard, "process_info", return_value={"started": 101}), patch.object(guard.os, "kill") as kill:
            guard.stop_owned({123: {"started": 101}})
            self.assertEqual([call.args for call in kill.call_args_list], [(123, guard.STOP_SIGNAL), (123, guard.KILL_SIGNAL)])


if __name__ == "__main__":
    unittest.main()
