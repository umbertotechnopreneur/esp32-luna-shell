"""Regression checks for Windows host compiler environment preparation."""

import os
import unittest
from unittest.mock import patch

from scripts.common import cody_host_compiler


@unittest.skipUnless(os.name == "nt", "Visual Studio environment is Windows-specific")
class HostCompilerEnvironmentTests(unittest.TestCase):
    # self: Simulated Visual Studio discovery with a caller PATH beyond cmd.exe's limit.
    # Raises AssertionError if setup loses the caller PATH or receives the long value.
    def test_long_path_is_isolated_during_visual_studio_setup(self):
        long_path = "C:\\existing;" * 900
        environment = {
            "PATH": long_path,
            "SYSTEMROOT": "C:\\Windows",
            "ProgramFiles(x86)": "C:\\Program Files (x86)",
        }
        with patch.dict(os.environ, environment, clear=True), \
             patch.object(cody_host_compiler, "resolve_compiler", side_effect=[FileNotFoundError(), "cl.exe"]), \
             patch.object(cody_host_compiler.subprocess, "run") as run:
            run.side_effect = [
                unittest.mock.Mock(stdout="C:\\VS\\Insiders\n"),
                unittest.mock.Mock(stdout="PATH=C:\\VS\\bin;C:\\Windows\\System32\nINCLUDE=C:\\VS\\include\n"),
            ]
            self.assertEqual(cody_host_compiler.prepare_host_compiler(), "cl.exe")
            setup_environment = run.call_args_list[1].kwargs["env"]
            self.assertNotIn(long_path, setup_environment["PATH"])
            self.assertTrue(os.environ["PATH"].startswith("C:\\VS\\bin;"))
            self.assertTrue(os.environ["PATH"].endswith(long_path))
            self.assertEqual(os.environ["INCLUDE"], "C:\\VS\\include")
