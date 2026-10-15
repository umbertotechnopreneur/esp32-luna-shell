"""Isolated queue installation/storage boundaries; no native host or device is contacted."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))
import ccp_audio_config as configuration
import ccp_voice_input as audio

spec = importlib.util.spec_from_file_location("configure_codex_audio", ROOT / "scripts/commands/configure_codex_audio.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class AudioSetupBoundaries(unittest.TestCase):
    # self: Isolated artifacts and synthetic executable path; never executed.
    def setUp(self):
        fixtures = ROOT / "artifacts/checks/cody-audio-setup"
        fixtures.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=fixtures)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.executable = self.directory / "codex.exe"
        self.executable.write_bytes(b"synthetic path fixture; not executable")
        self.storage = patch.object(configuration, "runtime_directory", return_value=self.directory)
        self.storage.start()
        self.addCleanup(self.storage.stop)
        self.environment = patch.dict(configuration.os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)

    # self: Missing optional setup remains unconfigured without writes or provider access.
    def test_absent_configuration_does_not_create_settings(self):
        self.assertEqual(configuration.load_config().source, "unconfigured")
        self.assertFalse((self.directory / "audio-config.json").exists())

    # self: Executable selection rejects relative paths, command strings and missing files.
    def test_executable_must_be_explicit_installed_file(self):
        self.assertEqual(configuration.validate_executable(str(self.executable)), str(self.executable))
        for value in ("", "codex.exe", str(self.executable) + " --flag", str(self.directory), "https://example.com/codex"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                configuration.validate_executable(value)
        with self.assertRaises(FileNotFoundError):
            configuration.validate_executable(str(self.directory / "absent/codex.exe"))

    # self: Invalid or old endpoint settings never silently become a different delivery route.
    def test_invalid_storage_is_not_absence_or_migration(self):
        path = self.directory / "audio-config.json"
        valid = {"version": 2, "route": "public_audio_queue", "executable": str(self.executable)}
        payloads = [b"{", b"\xff", b"x" * 4097,
                    json.dumps({**valid, "version": True}).encode(),
                    json.dumps({**valid, "extra": 1}).encode(),
                    b'{"version":2,"version":2}',
                    b'{"version":1,"endpoint":"ws://127.0.0.1:4500","token_file":""}']
        for payload in payloads:
            with self.subTest(payload=payload[:25]):
                path.write_bytes(payload)
                with self.assertRaises(ValueError):
                    configuration.load_config()
                with self.assertRaises(ValueError):
                    configuration.save_config(configuration.NativeAudioConfig(str(self.executable)))
                self.assertEqual(path.read_bytes(), payload)

    # self: Queue settings persist only an executable selection, without credentials or endpoints.
    def test_saved_configuration_roundtrips(self):
        selected = configuration.NativeAudioConfig(str(self.executable), "installation")
        path = configuration.save_config(selected)
        self.assertEqual(configuration.load_config(), selected)
        self.assertEqual(set(json.loads(path.read_text())), {"version", "route", "executable"})
        self.assertFalse(list((self.directory / "artifacts/audio-setup").iterdir()))

    # self: Failed replacement retains the previous valid configuration byte for byte.
    def test_persistence_failure_preserves_previous_configuration(self):
        selected = configuration.NativeAudioConfig(str(self.executable))
        path = configuration.save_config(selected)
        before = path.read_bytes()
        with patch.object(configuration.os, "replace", side_effect=PermissionError("synthetic persistence denied")):
            with self.assertRaisesRegex(PermissionError, "persistence denied"):
                configuration.save_config(selected)
        self.assertEqual(path.read_bytes(), before)
        self.assertFalse(list((self.directory / "artifacts/audio-setup").iterdir()))

    # self: Environment settings must be explicit; obsolete endpoint settings cannot activate a fallback.
    def test_environment_override_is_explicit(self):
        selected = configuration.load_config(environment={"CODY_CODEX_AUDIO_EXECUTABLE": str(self.executable)})
        self.assertEqual((selected.executable, selected.source), (str(self.executable), "environment"))
        for environment in ({"CODY_CODEX_AUDIO_EXECUTABLE": ""}, {"CODY_CODEX_AUDIO_ENDPOINT": ""},
                            {"CODY_CODEX_AUDIO_TOKEN_FILE": ""}):
            with self.subTest(environment=environment), self.assertRaises(ValueError):
                configuration.load_config(environment=environment)

    # self: Failed native initialization never persists a seemingly enabled queue route.
    def test_failed_probe_never_saves_configuration(self):
        with patch.object(installer, "verify_native_host", side_effect=RuntimeError("synthetic queue unavailable")):
            with self.assertRaisesRegex(RuntimeError, "queue unavailable"):
                installer.configure(str(self.executable))
        self.assertFalse((self.directory / "audio-config.json").exists())

    # self: A concurrent chat switch aborts setup while closing the queue-only client.
    def test_probe_rejects_changed_followed_chat(self):
        with patch.object(installer, "followed_chat", side_effect=["chat", "other-chat"]), \
                patch.object(installer, "PublicAudioQueue") as native:
            with self.assertRaisesRegex(RuntimeError, "changed"):
                installer.verify_native_host(str(self.executable))
        native.return_value.probe.assert_called_once_with("chat")
        native.return_value.submit.assert_not_called()
        native.return_value.close.assert_called_once()

    # self: Successful setup probes read-only access and persists without submitting audio.
    def test_successful_setup_only_reads_then_persists(self):
        with patch.object(installer, "followed_chat", return_value="chat"), \
                patch.object(installer, "PublicAudioQueue") as native:
            path = installer.configure(str(self.executable))
        self.assertTrue(path.is_file())
        native.return_value.probe.assert_called_once_with("chat")
        native.return_value.submit.assert_not_called()
        native.return_value.close.assert_called_once()

    # self: Corrupt installed configuration remains an optional failure without starting Codex.
    def test_receiver_retains_configuration_failure(self):
        (self.directory / "audio-config.json").write_text("{", encoding="utf-8")
        responder = Mock()
        responder._activity.list_sessions.return_value = [{"followed": True, "session_id": "chat"}]
        receiver = audio.VoiceInputServer(responder)
        with patch.object(audio, "PublicAudioQueue") as native:
            receiver.refresh_readiness()
        native.assert_not_called()
        status = receiver.status()
        self.assertEqual(status["native_configuration_source"], "invalid")
        self.assertFalse(status["native_ready"])
        self.assertFalse(status["transcription_available"])
        self.assertIn("configuration failed", status["native_error"])


if __name__ == "__main__":
    unittest.main()
