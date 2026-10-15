"""Bounded public Codex queue client; never starts or resumes a model session."""

from __future__ import annotations

import json
from pathlib import Path
import queue
import subprocess
import threading
import time

from ccp_windows import runtime_directory

MAX_RESPONSE_BYTES = 4 * 1024 * 1024
ALLOWED_METHODS = {"initialize", "thread/read", "thread/queue/list", "thread/queue/add"}


class NativeInputRejected(RuntimeError):
    """Codex explicitly rejected an operation, without admitting this input."""


class NativeInputUnconfirmed(RuntimeError):
    """The connection cannot confirm whether Codex admitted an input."""


class PublicAudioQueue:
    # executable: Explicit installed native Codex executable, not a shell command.
    # stop_event: Optional owning worker cancellation; native requests never survive shutdown.
    # Raises original process, protocol or file errors; closes failed initialization.
    def __init__(self, executable: str, stop_event=None):
        self.process = None
        self.reader = None
        self.stderr = None
        self.reader_error = None
        self.replies = queue.Queue(maxsize=32)
        self.identity = 0
        self.queue_checked = False
        self.stop_event = threading.Event() if stop_event is None else stop_event
        directory = runtime_directory() / "artifacts" / "voice"
        directory.mkdir(parents=True, exist_ok=True)
        log = directory / "queue-server.log"
        if log.exists() and log.stat().st_size >= 4 * 1024 * 1024:
            raise RuntimeError("Codex audio queue log is full; review it on your PC")
        try:
            self.stderr = log.open("ab")
            self.process = subprocess.Popen(
                [executable, "app-server", "--listen", "stdio://"],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.stderr,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            self.reader = threading.Thread(target=self.read_messages, name="CCP-audio-queue", daemon=True)
            self.reader.start()
            self.call("initialize", {"clientInfo": {"name": "cody_audio_queue", "version": "0.6.8"},
                                     "capabilities": {"experimentalApi": True}})
            self.process.stdin.write(b'{"method":"initialized","params":{}}\n')
            self.process.stdin.flush()
        except BaseException as error:
            try:
                self.close()
            except Exception as cleanup_error:
                error.add_note(f"Codex queue cleanup failed: {cleanup_error}")
            raise

    # self: Sole stdout owner; raw media and unrelated chat contents are never logged.
    # Exceptions: Transport/parse/budget errors are passed to the calling owner.
    def read_messages(self):
        try:
            while True:
                line = self.process.stdout.readline(MAX_RESPONSE_BYTES + 1)
                if not line:
                    raise EOFError("Codex audio queue transport closed")
                if len(line) > MAX_RESPONSE_BYTES:
                    raise ValueError("Codex audio queue response exceeds 4 MiB")
                message = json.loads(line)
                if not isinstance(message, dict):
                    raise ValueError("Codex audio queue response is not an object")
                self.replies.put_nowait(message)
        except (OSError, ValueError, EOFError, queue.Full) as error:
            try:
                self.replies.put_nowait(error)
            except queue.Full:
                # An exhausted mailbox must fail even if an earlier reply was valid.
                self.reader_error = error

    # self: One serialized delivery worker owns every call and its response identity.
    # method: Allowlisted initialization, history metadata or public queue operation.
    # params: Bounded operation fields; no turn/start, resume, steering or approvals.
    # Raises original transport/timeout errors or an explicit native rejection.
    def call(self, method: str, params: dict):
        if method not in ALLOWED_METHODS:
            raise ValueError("Only public audio queue and metadata operations are permitted")
        if self.reader_error is not None:
            raise RuntimeError("Codex audio queue response mailbox exhausted") from self.reader_error
        if self.stop_event.is_set():
            raise NativeInputRejected("Codex audio queue owner is stopping; request was not sent")
        self.identity += 1
        identity = self.identity
        payload = json.dumps({"id": identity, "method": method, "params": params}).encode("utf-8") + b"\n"
        if len(payload) > 8192:
            raise ValueError("Codex audio queue request exceeds 8 KiB")
        self.process.stdin.write(payload)
        self.process.stdin.flush()
        deadline = time.monotonic() + 15
        messages = 0
        while messages < 128:
            if self.stop_event.is_set():
                raise RuntimeError("Codex audio queue owner is stopping")
            if self.reader_error is not None:
                raise RuntimeError("Codex audio queue response mailbox exhausted") from self.reader_error
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Codex audio queue request timed out")
            try:
                message = self.replies.get(timeout=min(0.2, remaining))
            except queue.Empty:
                continue
            messages += 1
            if isinstance(message, Exception):
                raise message
            if "id" in message and "method" in message:
                raise NativeInputUnconfirmed("Native Codex interaction belongs on the PC; no answer was sent")
            if message.get("id") == identity:
                if "error" in message:
                    error = message["error"]
                    raise NativeInputRejected(f"Codex {method}: {error.get('code')}: {error.get('message')}")
                if "result" not in message:
                    raise ValueError("Codex audio queue reply has no result")
                return message["result"]
        raise RuntimeError("Codex audio queue notification budget exhausted")

    # self: Queue-only native client; a notLoaded status is expected in this process.
    # thread_id: Exact genuine followed chat identity captured by the hook owner.
    # Raises original native errors; successful reads do not prove model audio support.
    def probe(self, thread_id: str):
        result = self.call("thread/read", {"threadId": thread_id, "includeTurns": False})
        if result.get("thread", {}).get("id") != thread_id:
            raise ValueError("Codex audio queue returned a different chat identity")
        if not self.queue_checked:
            result = self.call("thread/queue/list", {"threadId": thread_id, "limit": 1})
            if not isinstance(result.get("data"), list):
                raise ValueError("Codex public queue response is invalid")
            self.queue_checked = True

    # self: Queue client owned by the audio delivery worker.
    # thread_id: Frozen capture chat; never replaced with the currently selected chat.
    # message_id: Stable recording identity, retained across duplicate UDP packets.
    # path: Final local WAV retained by the authenticated receiver.
    # before_submit: Rechecks cancellation and followed chat at the admission boundary.
    # Raises native rejection or transport uncertainty; never retries queue admission.
    def submit(self, thread_id: str, message_id: str, path: Path, before_submit) -> str:
        self.probe(thread_id)
        before_submit()
        result = self.call("thread/queue/add", {
            "threadId": thread_id, "clientUserMessageId": message_id,
            "input": [{"type": "text", "text": "Cody recording: WAV attachment, without transcription.", "text_elements": []},
                      {"type": "localAudio", "path": str(path)}],
        })
        receipt = result.get("queuedSubmission", {})
        queue_id = receipt.get("id")
        if (receipt.get("clientUserMessageId") != message_id or
                not isinstance(queue_id, str) or not 1 <= len(queue_id) <= 128):
            raise NativeInputUnconfirmed("Codex audio queue returned an unmatched admission receipt")
        return queue_id

    # self: Retires only this queue-only child, never the running Desktop host.
    # Raises original cleanup errors; callers retain the primary delivery error.
    def close(self):
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=3)
            if self.reader is not None:
                self.reader.join(timeout=1)
                if self.reader.is_alive():
                    raise RuntimeError("Codex audio queue reader did not retire")
            self.process.stdin.close()
            self.process.stdout.close()
            self.process = None
        if self.stderr is not None:
            self.stderr.close()
            self.stderr = None
