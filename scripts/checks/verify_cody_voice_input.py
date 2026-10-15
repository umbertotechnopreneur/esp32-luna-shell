"""Prepared host boundary cases; execute only with fresh explicit authorization."""

import hashlib
import hmac
from pathlib import Path
import sys
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.checks.verify_power_policy import function_text

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "plugins/cody-codex-plugin/mcp"))
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.exceptions import InvalidTag
import ccp_voice_input as audio


class VoiceInputBoundaries(unittest.TestCase):
    # self: Executes extracted real firmware functions with fake acquisition/publication.
    # Raises on compile, assertion or source-extraction failure; no hardware is used.
    def test_firmware_owner_gestures_silence_and_polling(self):
        output = ROOT / "artifacts/checks/voice-input-owner"
        output.mkdir(parents=True, exist_ok=True)
        source_path = ROOT / "components/LUNASHELL_platform/src/audio/LUNASHELL_voice_input.cpp"
        source = source_path.read_text(encoding="utf-8")
        signatures = [
            "esp_err_t publish(", "bool active()", "esp_err_t start_countdown_locked(",
            "esp_err_t advance_countdown_locked(", "esp_err_t LUNASHELL::requestVoiceInput(",
            "esp_err_t LUNASHELL::beginVoiceInputCountdown(",
            "void LUNASHELL::feedVoiceInput(", "static esp_err_t cancel_voice_input_locked()",
            "esp_err_t LUNASHELL::cancelVoiceInput()", "esp_err_t LUNASHELL::finishVoiceInput(",
            "esp_err_t LUNASHELL::cancelVoiceInput(int64_t",
            "esp_err_t LUNASHELL::pauseVoiceInput()",
        ]
        (output / "voice_input_owner_under_test.inc").write_text(
            "\n\n".join(function_text(source, signature, str(source_path)) for signature in signatures),
            encoding="utf-8")
        executable = output / "voice-input-owner.exe"
        run_stage(compile_command(resolve_compiler(None),
            [str(output), "scripts/tests/cody_native", "components/LUNASHELL_platform/include"],
            ["scripts/tests/cody_native/voice_input_owner_contract.cpp"], executable), output, "compile")
        run_stage([str(executable)], output, "owner")

    # self: Executes the actual physical-wake admission wrapper with fake owner boundaries.
    # Raises on compilation, source extraction or an incorrect wake/shutdown distinction.
    def test_completed_physical_wake_admits_one_voice_request(self):
        output = ROOT / "artifacts/checks/voice-wake-owner"
        output.mkdir(parents=True, exist_ok=True)
        source_path = ROOT / "components/LUNASHELL_platform/src/power/LUNASHELL_power.cpp"
        source = source_path.read_text(encoding="utf-8")
        signatures = ("bool voice_input_is_active(", "esp_err_t wake_from_user_input(")
        (output / "voice_wake_under_test.inc").write_text(
            "\n\n".join(function_text(source, signature, str(source_path)) for signature in signatures),
            encoding="utf-8")
        executable = output / "voice-wake-owner.exe"
        run_stage(compile_command(resolve_compiler(None),
            [str(output), "scripts/tests/cody_native", "components/LUNASHELL_platform/include"],
            ["scripts/tests/cody_native/voice_wake_owner_contract.cpp"], executable,
            definitions=("CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT=1",)), output, "compile")
        run_stage([str(executable)], output, "owner")

    # self: Isolated receiver fixture; no sockets, model calls or device access.
    def setUp(self):
        # Synthetic public test bytes, unrelated to any paired device credential.
        self.link = bytes(range(32))
        self.device = "001122334455"
        self.session = bytes(range(16))
        self.presence = bytes(range(16, 32))
        self.source = ("192.0.2.1", 55000)
        peer = SimpleNamespace(key=self.link, nonce=self.presence.hex(), source=(self.source[0], 42117), observed_at=time.monotonic())
        activity = Mock()
        activity.list_sessions.return_value = [{"followed": True, "session_id": "test-chat", "turn_id": "test-turn"}]
        self.responder = SimpleNamespace(_event_lock=threading.Lock(), _event_peers={self.device: peer}, _activity=activity)
        self.receiver = audio.VoiceInputServer(self.responder)
        self.receiver.cipher = AESGCM
        self.receiver.socket = Mock()
        self.receiver.ready_thread = "test-chat"
        self.receiver.ready_at = time.monotonic()
        self.receiver.native_error = ""

    # self: Public synthetic protocol fixture.
    # sequence: Exact input block identity.
    # flag: START/PCM/END/CANCEL wire kind.
    # pcm: Acquired synthetic PCM bytes for this boundary case.
    # presence: Optional old discovery nonce for replay-rejection evidence.
    def packet(self, sequence, flag, pcm=b"", presence=None):
        nonce = self.presence if presence is None else presence
        key = hmac.new(self.link, b"cody-audio-v1|" + self.session.hex().encode() + b"|" + nonce.hex().encode(), hashlib.sha256).digest()
        header = audio.HEADER.pack(b"CYA1", bytes.fromhex(self.device), self.session, nonce, sequence, flag, len(pcm))
        return header + AESGCM(key).encrypt(b"DAT1" + sequence.to_bytes(8, "big"), pcm, header)

    # self: Receiver fixture without any network execution.
    # Raises AssertionError if retransmission duplicates PCM or native delivery jobs.
    def test_retransmission_never_duplicates_words_or_delivery(self):
        self.receiver.accept(self.packet(0, 1), self.source)
        block = self.packet(1, 2, b"\x01\x02" * 32)
        self.receiver.accept(block, self.source)
        self.receiver.accept(block, self.source)
        recording = next(iter(self.receiver.records.values()))
        self.assertEqual(len(recording.pcm), 64)
        end = self.packet(2, 3)
        self.receiver.accept(end, self.source)
        self.receiver.accept(end, self.source)
        self.assertEqual(self.receiver.jobs.qsize(), 1)
        self.assertEqual(recording.status, 3)
        self.assertFalse(recording.accepted)

    # self: Receiver fixture with a currently authenticated fresh discovery event.
    # Raises AssertionError if a recorded old START can create another capture.
    def test_old_discovery_cannot_replay_audio(self):
        with self.assertRaisesRegex(ValueError, "fresh authenticated discovery"):
            self.receiver.accept(self.packet(0, 1, presence=bytes(16)), self.source)
        self.assertEqual(self.receiver.records, {})

    # self: Receiver fixture bound to one actual socket endpoint.
    # Raises AssertionError if missing blocks or endpoint changes modify PCM.
    def test_out_of_order_and_changed_endpoint_keep_buffer_intact(self):
        self.receiver.accept(self.packet(0, 1), self.source)
        self.receiver.accept(self.packet(2, 2, bytes(20)), self.source)
        with self.assertRaisesRegex(ValueError, "endpoint"):
            self.receiver.accept(self.packet(1, 2, bytes(20)), (self.source[0], 55001))
        self.assertEqual(len(next(iter(self.receiver.records.values())).pcm), 0)

    # self: Receiver fixture whose ciphertext has been altered after acquisition.
    # Raises AssertionError if unauthenticated PCM is ever retained.
    def test_corrupt_ciphertext_is_not_audio(self):
        self.receiver.accept(self.packet(0, 1), self.source)
        packet = bytearray(self.packet(1, 2, bytes(32)))
        packet[-1] ^= 1
        with self.assertRaises(InvalidTag):
            self.receiver.accept(bytes(packet), self.source)
        self.assertEqual(len(next(iter(self.receiver.records.values())).pcm), 0)

    # self: Receiver with an ended recording still waiting for native delivery.
    # Raises AssertionError if cancellation silently becomes native acceptance.
    def test_cancel_before_native_submission_retires_pending_pcm(self):
        self.receiver.accept(self.packet(0, 1), self.source)
        self.receiver.accept(self.packet(1, 2, bytes(32)), self.source)
        self.receiver.accept(self.packet(2, 3), self.source)
        self.receiver.accept(self.packet(3, 4), self.source)
        recording = next(iter(self.receiver.records.values()))
        self.assertEqual(recording.status, 5)
        self.assertFalse(recording.accepted)
        self.assertEqual(len(recording.pcm), 0)

    # self: Real receiver holding streamed PCM before the sender admitted END.
    # Raises AssertionError if explicit discard schedules delivery or retains audio.
    def test_cancel_stream_before_end_discards_without_delivery(self):
        self.receiver.accept(self.packet(0, 1), self.source)
        self.receiver.accept(self.packet(1, 2, bytes(32)), self.source)
        # Sender may retire an unacknowledged window, so CANCEL can skip its IDs.
        self.receiver.accept(self.packet(6, 4), self.source)
        self.receiver.accept(self.packet(6, 4), self.source)
        recording = next(iter(self.receiver.records.values()))
        self.assertEqual(recording.status, 5)
        self.assertFalse(recording.pcm)
        self.assertFalse(recording.accepted)
        self.assertTrue(self.receiver.jobs.empty())

    # self: Receiver fixture with a full sixty-second utterance.
    # Raises AssertionError if new audio overwrites or exceeds retained PCM.
    def test_full_buffer_fails_without_overwriting(self):
        self.receiver.accept(self.packet(0, 1), self.source)
        recording = next(iter(self.receiver.records.values()))
        recording.pcm = bytearray(audio.MAX_PCM)
        self.receiver.accept(self.packet(1, 2, bytes(32)), self.source)
        self.assertEqual(recording.status, 5)
        self.assertEqual(len(recording.pcm), audio.MAX_PCM)

    # self: Receiver whose native readiness is unavailable or stale.
    # Raises AssertionError if recording is admitted without a recent matching native probe.
    def test_start_rejects_unavailable_native_before_pcm(self):
        self.receiver.ready_at = time.monotonic() - 11
        self.receiver.accept(self.packet(0, 1), self.source)
        recording = next(iter(self.receiver.records.values()))
        self.assertEqual(recording.status, 5)
        self.receiver.accept(self.packet(1, 2, bytes(20)), self.source)
        self.assertFalse(recording.pcm)
        self.assertTrue(self.receiver.jobs.empty())

    # self: Four-packet sender model with one lost prefix and repeated window packets.
    # Raises AssertionError if cumulative ACK advances over a gap or PCM duplicates.
    def test_window_recovery_preserves_contiguous_pcm(self):
        self.receiver.accept(self.packet(0, 1), self.source)
        recording = next(iter(self.receiver.records.values()))
        blocks = [self.packet(index, 2, bytes([index, 0]) * 4) for index in range(1, 5)]
        for packet in blocks[1:]:
            self.receiver.accept(packet, self.source)
        self.assertEqual(recording.expected, 1)
        for packet in blocks:
            self.receiver.accept(packet, self.source)
        for packet in blocks:
            self.receiver.accept(packet, self.source)
        self.assertEqual(recording.pcm, b"".join(bytes([index, 0]) * 4 for index in range(1, 5)))
        ack = self.receiver.socket.sendto.call_args.args[0]
        counter = audio.HEADER.unpack(ack[:audio.HEADER.size])[4]
        plain = AESGCM(recording.key).decrypt(b"ACK1" + counter.to_bytes(8, "big"),
                                             ack[audio.HEADER.size:], ack[:audio.HEADER.size])
        self.assertEqual(audio.struct.unpack("!IB", plain), (4, 2))



if __name__ == "__main__":
    unittest.main()
