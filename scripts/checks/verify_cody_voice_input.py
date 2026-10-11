"""Prepared host boundary cases; execute only with fresh explicit authorization."""

from collections import deque
import hashlib
import hmac
import json
from pathlib import Path
import sys
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "plugins/cody-codex-plugin/mcp"))
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.exceptions import InvalidTag
import ccp_voice_input as audio


class VoiceInputBoundaries(unittest.TestCase):
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

    # self: Receiver fixture with a full sixty-second utterance.
    # Raises AssertionError if new audio overwrites or exceeds retained PCM.
    def test_full_buffer_fails_without_overwriting(self):
        self.receiver.accept(self.packet(0, 1), self.source)
        recording = next(iter(self.receiver.records.values()))
        recording.pcm = bytearray(audio.MAX_PCM)
        self.receiver.accept(self.packet(1, 2, bytes(32)), self.source)
        self.assertEqual(recording.status, 5)
        self.assertEqual(len(recording.pcm), audio.MAX_PCM)

    # self: Native protocol fixture with a genuine pending approval request.
    # Raises AssertionError if the plugin returns any approval or invents audio acceptance.
    def test_native_approval_is_never_answered(self):
        connection = Mock()
        connection.recv.side_effect = deque([json.dumps({"id": 3, "method": "item/commandExecution/requestApproval", "params": {}})])
        with self.assertRaisesRegex(RuntimeError, "desktop client"):
            audio.rpc(connection, 3, "turn/start", {"threadId": "test-chat", "input": []})
        self.assertEqual(connection.send.call_count, 1)

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

    # self: Native bridge fixture; the current same-chat hook turn differs from capture start.
    # Raises AssertionError if steering uses the old turn or loses the recording correlation ID.
    def test_submission_uses_current_turn_in_frozen_chat(self):
        self.receiver.accept(self.packet(0, 1), self.source)
        recording = next(iter(self.receiver.records.values()))
        admission = Mock(return_value="new-turn")
        connection = Mock()
        with patch.object(audio, "connect_native", return_value=connection), patch.object(audio, "rpc") as native:
            native.side_effect = [{"thread": {"id": "test-chat", "status": {"type": "active"}}},
                                  {"turnId": "new-turn"}]
            audio.submit_native(recording, Path("synthetic.wav"), "ws://127.0.0.1:1", admission)
        admission.assert_called_once_with(True)
        params = native.call_args.args[3]
        self.assertEqual(params["expectedTurnId"], "new-turn")
        self.assertEqual(params["threadId"], "test-chat")
        self.assertEqual(params["clientUserMessageId"], self.session.hex())
        self.assertEqual(recording.native_turn_id, "new-turn")
        connection.close.assert_called_once()


if __name__ == "__main__":
    unittest.main()
