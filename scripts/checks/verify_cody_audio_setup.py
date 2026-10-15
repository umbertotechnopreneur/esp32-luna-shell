"""Isolated installation/storage boundaries; no native host or device is contacted."""

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
    # self: Isolated fixture under artifacts; never uses installed pairing or configuration.
    def setUp(self):
        fixtures = ROOT / "artifacts/checks/cody-audio-setup"
        fixtures.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=fixtures)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.storage = patch.object(configuration, "runtime_directory", return_value=self.directory)
        self.storage.start()
        self.addCleanup(self.storage.stop)
        self.environment = patch.dict(configuration.os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)

    # self: Missing optional setup must remain visibly unconfigured without writes.
    def test_absent_configuration_does_not_create_settings(self):
        self.assertEqual(configuration.load_config().source, "unconfigured")
        self.assertFalse((self.directory / "audio-config.json").exists())

    # self: URLs containing credentials, hostnames or remote addresses must not leak through errors.
    def test_endpoint_admission_is_loopback_only_and_errors_do_not_echo_input(self):
        for value in ("ws://127.0.0.1:4500", "ws://[::1]:4500/native"):
            self.assertEqual(configuration.validate_endpoint(value), value)
        for value in ("", "ws://localhost:4500", "ws://192.0.2.1:4500", "ws://127.0.0.1",
                      "wss://127.0.0.1:4500", "ws://user:private@127.0.0.1:4500", "ws://127.0.0.1:4500/?token=private",
                      "ws://127.0.0.1:4500/\n", "ws://127.0.0.1:99999"):
            with self.subTest(value=value), self.assertRaises(ValueError) as rejected:
                configuration.validate_endpoint(value)
            self.assertNotIn("private", str(rejected.exception))

    # self: Duplicate/invalid persisted fields must never become first-boot defaults.
    def test_invalid_storage_is_not_absence_or_repair(self):
        path = self.directory / "audio-config.json"
        valid = {"version": 1, "endpoint": "ws://127.0.0.1:4500", "token_file": ""}
        payloads = [b"{", b"\xff", b"x" * 4097,
                    json.dumps({**valid, "version": True}).encode(),
                    json.dumps({**valid, "extra": 1}).encode(),
                    b'{"version":1,"version":1,"endpoint":"ws://127.0.0.1:4500","token_file":""}']
        for payload in payloads:
            with self.subTest(payload=payload[:25]):
                path.write_bytes(payload)
                with self.assertRaises(ValueError):
                    configuration.load_config()
                with self.assertRaises(ValueError):
                    configuration.save_config(configuration.NativeAudioConfig(valid["endpoint"]))
                self.assertEqual(path.read_bytes(), payload)

    # self: Persistence retains only a capability-file reference, never capability contents.
    def test_saved_configuration_roundtrips_without_copying_credentials(self):
        token = self.directory / "capability.txt"
        token.write_text("public-synthetic-fixture", encoding="ascii")
        selected = configuration.NativeAudioConfig("ws://127.0.0.1:4500", str(token), "installation")
        path = configuration.save_config(selected)
        self.assertEqual(configuration.load_config(), selected)
        self.assertNotIn("public-synthetic-fixture", path.read_text())
        self.assertFalse(list((self.directory / "artifacts/audio-setup").iterdir()))

    # self: A failed replacement must preserve the preceding valid configuration byte for byte.
    def test_persistence_failure_preserves_previous_configuration(self):
        path = configuration.save_config(configuration.NativeAudioConfig("ws://127.0.0.1:4500"))
        before = path.read_bytes()
        with patch.object(configuration.os, "replace", side_effect=PermissionError("synthetic persistence denied")):
            with self.assertRaisesRegex(PermissionError, "persistence denied"):
                configuration.save_config(configuration.NativeAudioConfig("ws://127.0.0.1:4501"))
        self.assertEqual(path.read_bytes(), before)
        self.assertFalse(list((self.directory / "artifacts/audio-setup").iterdir()))

    # self: Explicit environment settings remain intentional overrides, with no implicit token-only setup.
    def test_environment_override_is_explicit_and_validated(self):
        configuration.save_config(configuration.NativeAudioConfig("ws://127.0.0.1:4500"))
        selected = configuration.load_config(environment={"CODY_CODEX_AUDIO_ENDPOINT": "ws://127.0.0.1:4501"})
        self.assertEqual((selected.endpoint, selected.source), ("ws://127.0.0.1:4501", "environment"))
        with self.assertRaises(ValueError):
            configuration.load_config(environment={"CODY_CODEX_AUDIO_ENDPOINT": ""})
        with self.assertRaisesRegex(ValueError, "requires an explicit endpoint"):
            configuration.load_config(environment={"CODY_CODEX_AUDIO_TOKEN_FILE": str(self.directory / "token")})

    # self: Failed host initialization must not persist a seemingly enabled endpoint.
    def test_failed_probe_never_saves_configuration(self):
        with patch.object(installer, "verify_native_host", side_effect=RuntimeError("synthetic host unavailable")):
            with self.assertRaisesRegex(RuntimeError, "host unavailable"):
                installer.configure("ws://127.0.0.1:4500", "")
        self.assertFalse((self.directory / "audio-config.json").exists())

    # self: A host reading unloaded history or a concurrently changed chat cannot pass setup.
    def test_probe_requires_live_owned_chat_and_stable_followed_identity(self):
        for status, followed in (("notLoaded", ["chat"]), ("active", ["chat", "other-chat"])):
            connection = Mock()
            with patch.object(installer, "followed_chat", side_effect=followed), \
                    patch.object(audio, "connect_native", return_value=connection), \
                    patch.object(audio, "rpc", return_value={"thread": {"id": "chat", "status": {"type": status}}}) as native:
                with self.assertRaises(RuntimeError):
                    installer.verify_native_host("ws://127.0.0.1:4500", "")
            self.assertEqual(native.call_args.args[2], "thread/read")
            connection.close.assert_called_once()

    # self: Successful setup reads the followed chat, closes the probe and saves without any input submission.
    def test_successful_setup_only_reads_then_persists(self):
        connection = Mock()
        with patch.object(installer, "followed_chat", return_value="chat"), \
                patch.object(audio, "connect_native", return_value=connection), \
                patch.object(audio, "rpc", return_value={"thread": {"id": "chat", "status": {"type": "active"}}}) as native:
            path = installer.configure("ws://127.0.0.1:4500", "")
        self.assertTrue(path.is_file())
        self.assertEqual(native.call_count, 1)
        self.assertEqual(native.call_args.args[2:], ("thread/read", {"threadId": "chat", "includeTurns": False}, native.call_args.args[-1]))
        connection.close.assert_called_once()

    # self: Corrupt installed configuration must remain an explicit optional audio failure.
    def test_receiver_retains_configuration_failure_without_native_probe(self):
        (self.directory / "audio-config.json").write_text("{", encoding="utf-8")
        responder = Mock()
        responder._activity.list_sessions.return_value = [{"followed": True, "session_id": "chat"}]
        receiver = audio.VoiceInputServer(responder)
        with patch.object(audio, "probe_native") as native:
            receiver.refresh_readiness()
        native.assert_not_called()
        status = receiver.status()
        self.assertEqual(status["native_configuration_source"], "invalid")
        self.assertFalse(status["native_ready"])
        self.assertIn("configuration failed", status["native_error"])


if __name__ == "__main__":
    unittest.main()
