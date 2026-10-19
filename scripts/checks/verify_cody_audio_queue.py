"""Offline public-queue safety boundaries; no Codex process or provider is contacted."""

from __future__ import annotations

import io
import json
from pathlib import Path
import queue
import sys
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))
from ccp_audio_queue import NativeInputRejected, NativeInputUnconfirmed, PublicAudioQueue, MAX_RESPONSE_BYTES
import ccp_voice_input as audio


class AudioQueueBoundaries(unittest.TestCase):
    # self: Isolated transport without any subprocess, native user history or credentials.
    def setUp(self):
        self.client = PublicAudioQueue.__new__(PublicAudioQueue)
        self.client.process = Mock(stdin=io.BytesIO())
        self.client.replies = queue.Queue(maxsize=32)
        self.client.identity = 0
        self.client.reader_error = None
        self.client.queue_checked = False
        self.client.stop_event = threading.Event()

    # self: Forbidden methods must fail before writing anything to the native transport.
    def test_no_model_sessions_or_native_approvals(self):
        for method in ("thread/start", "thread/resume", "turn/start", "turn/steer", "thread/realtime/start"):
            with self.subTest(method=method), self.assertRaises(ValueError):
                self.client.call(method, {})
        self.assertEqual(self.client.process.stdin.getvalue(), b"")

    # self: An inbound native request can share our outbound ID without being its response.
    def test_inbound_request_is_never_answered(self):
        self.client.replies.put({"id": 1, "method": "item/commandExecution/requestApproval", "params": {}})
        with self.assertRaises(NativeInputUnconfirmed):
            self.client.call("thread/queue/add", {})
        self.assertEqual(len(self.client.process.stdin.getvalue().splitlines()), 1)

    # self: Explicit rejection and a lost acknowledgement have distinct outcomes and no replay.
    def test_rejection_and_lost_acknowledgement_are_not_retried(self):
        for response, expected in (({"id": 1, "error": {"code": -32602, "message": "synthetic rejection"}}, NativeInputRejected),
                                   (EOFError("synthetic transport loss"), EOFError)):
            with self.subTest(expected=expected):
                self.client.identity = 0
                self.client.process.stdin = io.BytesIO()
                self.client.replies.put(response)
                with self.assertRaises(expected):
                    self.client.call("thread/queue/add", {})
                self.assertEqual(len(self.client.process.stdin.getvalue().splitlines()), 1)

    # self: Reading a persisted chat does not load or resume it in the queue-only process.
    def test_probe_accepts_unloaded_history_and_checks_queue(self):
        self.client.replies.put({"id": 1, "result": {"thread": {"id": "chat", "status": {"type": "notLoaded"}}}})
        self.client.replies.put({"id": 2, "result": {"data": []}})
        self.client.probe("chat")
        messages = [json.loads(line) for line in self.client.process.stdin.getvalue().splitlines()]
        self.assertEqual([message["method"] for message in messages], ["thread/read", "thread/queue/list"])
        self.assertTrue(all(message["params"]["threadId"] == "chat" for message in messages))

    # self: A chat switch or explicit discard at the admission boundary prevents queue mutation.
    def test_cancel_before_admission_never_sends_input(self):
        before_submit = Mock(side_effect=RuntimeError("chat changed"))
        with patch.object(self.client, "probe"), patch.object(self.client, "call") as native:
            with self.assertRaisesRegex(RuntimeError, "chat changed"):
                self.client.submit("frozen-chat", "recording", Path("synthetic.wav"), before_submit)
        native.assert_not_called()

    # self: WAV delivery freezes chat/recording identity and never asks the model for transcription.
    def test_matching_receipt_confirms_only_queue_admission(self):
        before_submit = Mock()
        with patch.object(self.client, "probe"), patch.object(self.client, "call", return_value={
                "queuedSubmission": {"id": "queue-id", "clientUserMessageId": "recording"}}) as native:
            result = self.client.submit("frozen-chat", "recording", Path("synthetic.wav"), before_submit)
        before_submit.assert_called_once_with()
        self.assertEqual(result, "queue-id")
        method, params = native.call_args.args
        self.assertEqual(method, "thread/queue/add")
        self.assertEqual(params["threadId"], "frozen-chat")
        self.assertEqual(params["clientUserMessageId"], "recording")
        self.assertEqual(params["input"][-1], {"type": "localAudio", "path": "synthetic.wav"})

    # self: An unrelated or incomplete receipt must never become apparent delivery success.
    def test_mismatched_receipt_remains_unconfirmed(self):
        with patch.object(self.client, "probe"), patch.object(self.client, "call", return_value={
                "queuedSubmission": {"id": "queue-id", "clientUserMessageId": "another-recording"}}) as native:
            with self.assertRaises(NativeInputUnconfirmed):
                self.client.submit("chat", "recording", Path("synthetic.wav"), Mock())
        self.assertEqual(native.call_count, 1)

    # self: A malformed or excessive response fails explicitly without printing its media bytes.
    def test_reader_bounds_and_invalid_json(self):
        for payload in (b"x" * (MAX_RESPONSE_BYTES + 1), b"{invalid}\n", b"[]\n"):
            with self.subTest(length=len(payload)):
                self.client.replies = queue.Queue(maxsize=32)
                self.client.process.stdout = io.BytesIO(payload)
                self.client.read_messages()
                self.assertIsInstance(self.client.replies.get_nowait(), ValueError)

    # self: A saturated response mailbox cannot hide a failed reader behind an earlier success.
    def test_mailbox_overflow_fails_closed(self):
        self.client.replies = queue.Queue(maxsize=1)
        self.client.replies.put({"id": 1, "result": {}})
        self.client.process.stdout = io.BytesIO(b'{"notification":true}\n')
        self.client.read_messages()
        with self.assertRaisesRegex(RuntimeError, "mailbox exhausted"):
            self.client.call("thread/read", {})
        self.assertEqual(self.client.process.stdin.getvalue(), b"")

    # self: Completed recordings exercise the actual delivery worker with a synthetic queue peer.
    # Raises AssertionError if transport failure becomes acceptance or admission repeats.
    def test_receiver_distinguishes_queued_rejected_and_uncertain(self):
        fixtures = ROOT / "artifacts/checks/cody-audio-queue"
        fixtures.mkdir(parents=True, exist_ok=True)
        for failure, expected in ((None, 4), (NativeInputRejected("rejected"), 5), (EOFError("lost ack"), 6)):
            with self.subTest(expected=expected), tempfile.TemporaryDirectory(dir=fixtures) as directory:
                responder = Mock()
                responder._activity.list_sessions.return_value = [{"followed": True, "session_id": "chat"}]
                receiver = audio.VoiceInputServer(responder)
                receiver.refresh_readiness = Mock()
                peer = Mock()
                receiver.queue_client = peer
                recording = audio.Recording("001122334455", bytes(16), bytes(16), bytes(32),
                                            ("192.0.2.1", 50000), "chat", "capture-turn",
                                            pcm=bytearray(b"\x01\x00" * 16), finished=True)
                receiver.records[(recording.device, recording.session)] = recording
                receiver.jobs.put(recording)

                # thread_id: Frozen capture context; a changed hook turn does not change chats.
                # message_id: Stable recording identity, never a regenerated replay ID.
                # path: Completed WAV inspected without invoking a speech recognizer.
                # before_submit: Actual receiver cancellation and chat check.
                # Raises the selected synthetic transport/rejection failure after admission starts.
                def admit(thread_id, message_id, path, before_submit):
                    self.assertEqual((thread_id, message_id), ("chat", bytes(16).hex()))
                    self.assertTrue(path.is_file())
                    before_submit()
                    receiver.stop.set()
                    if failure is not None:
                        raise failure
                    return "queue-id"

                peer.submit.side_effect = admit
                with patch.object(audio, "runtime_directory", return_value=Path(directory)):
                    receiver.deliver()
                self.assertEqual(recording.status, expected)
                self.assertEqual(recording.accepted, expected == 4)
                self.assertEqual(recording.native_queue_id, "queue-id" if expected == 4 else "")
                self.assertFalse(recording.pcm)
                self.assertTrue(Path(recording.artifact).is_file())
                self.assertEqual(peer.submit.call_count, 1)
                peer.close.assert_called_once()


if __name__ == "__main__":
    unittest.main()
