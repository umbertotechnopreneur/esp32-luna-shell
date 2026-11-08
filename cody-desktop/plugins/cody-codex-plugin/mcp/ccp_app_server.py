"""Bounded private stdio transport for Cody's public Codex App Server clients."""

from __future__ import annotations

import json
import queue
import subprocess
import threading
import time

MAX_RESPONSE_BYTES = 4 * 1024 * 1024


class NativeInputRejected(RuntimeError):
    """Codex explicitly rejected the requested operation."""


class NativeInputUnconfirmed(RuntimeError):
    """The connection could not confirm the result of an operation."""


class NativeAppServer:
    methods = frozenset({"initialize"})
    response_limit = MAX_RESPONSE_BYTES
    child_options: tuple[str, ...] = ()

    # executable: Absolute path to the installed native Codex executable.
    # stop_event: Optional cancellation event owned by the calling worker.
    # client_info: Public App Server initialization metadata.
    # stderr: Optional diagnostic file transferred to this client, otherwise discarded.
    # experimental: Opt-in only for a client whose documented operations require it.
    # Raises process, protocol or transport failures and retires a failed initialization.
    def __init__(self, executable: str, stop_event=None, *, client_info: dict,
                 stderr=None, experimental: bool = False) -> None:
        self.process = None
        self.reader = None
        self.stderr = stderr
        self.reader_error = None
        self.replies = queue.Queue(maxsize=32)
        self.identity = 0
        self.stop_event = threading.Event() if stop_event is None else stop_event
        try:
            self.process = subprocess.Popen(
                [executable, "app-server", "--listen", "stdio://", *self.child_options],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL if stderr is None else stderr,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            self.reader = threading.Thread(target=self.read_messages, name="CCP-app-server", daemon=True)
            self.reader.start()
            parameters = {"clientInfo": client_info}
            if experimental:
                parameters["capabilities"] = {"experimentalApi": True}
            self.call("initialize", parameters)
            self.process.stdin.write(b'{"method":"initialized","params":{}}\n')
            self.process.stdin.flush()
        except BaseException as error:
            try:
                self.close()
            except Exception as cleanup_error:
                error.add_note(f"Codex App Server cleanup failed: {cleanup_error}")
            raise

    # self: Sole bounded stdout reader; raw server payloads never enter the logs.
    # Raises no transport errors here; passes the failure to the request owner.
    def read_messages(self) -> None:
        try:
            while True:
                line = self.process.stdout.readline(self.response_limit + 1)
                if not line:
                    raise EOFError("Codex App Server transport closed")
                if len(line) > self.response_limit:
                    raise ValueError("Codex App Server response exceeds its byte limit")
                message = json.loads(line)
                if not isinstance(message, dict):
                    raise ValueError("Codex App Server response is not an object")
                self.replies.put_nowait(message)
        except (OSError, ValueError, EOFError, queue.Full) as error:
            try:
                self.replies.put_nowait(error)
            except queue.Full:
                # Exhaustion invalidates even an earlier queued reply.
                self.reader_error = error

    # self: One serialized request owner.
    # method: Public operation on the specific client's fixed allowlist.
    # params: Bounded operation arguments, omitted when the method takes none.
    # Raises transport, timeout or protocol errors, or an explicit native rejection.
    def call(self, method: str, params: dict | None):
        if method not in self.methods:
            raise ValueError("Codex App Server method is outside the client allowlist")
        if self.reader_error is not None:
            raise RuntimeError("Codex App Server response mailbox exhausted") from self.reader_error
        if self.stop_event.is_set():
            raise NativeInputRejected("Codex App Server owner is stopping; request was not sent")
        self.identity += 1
        identity = self.identity
        request = {"id": identity, "method": method}
        if params is not None:
            request["params"] = params
        payload = json.dumps(request).encode("utf-8") + b"\n"
        if len(payload) > 8192:
            raise ValueError("Codex App Server request exceeds 8 KiB")
        self.process.stdin.write(payload)
        self.process.stdin.flush()
        deadline = time.monotonic() + 15
        messages = 0
        while messages < 128:
            if self.stop_event.is_set():
                raise RuntimeError("Codex App Server owner is stopping")
            if self.reader_error is not None:
                raise RuntimeError("Codex App Server response mailbox exhausted") from self.reader_error
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Codex App Server request timed out")
            try:
                message = self.replies.get(timeout=min(0.2, remaining))
            except queue.Empty:
                continue
            messages += 1
            if isinstance(message, Exception):
                raise message
            if "id" in message and "method" in message:
                raise NativeInputUnconfirmed("Native Codex interaction belongs on the PC; no answer was sent")
            if type(message.get("id")) is int and message["id"] == identity:
                if "error" in message:
                    error = message["error"]
                    code = error.get("code") if isinstance(error, dict) else None
                    detail = str(code) if type(code) is int else "unknown code"
                    raise NativeInputRejected(f"Codex {method} rejected the request ({detail})")
                if "result" not in message:
                    raise ValueError("Codex App Server reply has no result")
                return message["result"]
        raise RuntimeError("Codex App Server notification budget exhausted")

    # self: Retires only this client-owned child and its optional diagnostic stream.
    # Raises original cleanup errors; callers retain their primary operation error.
    def close(self) -> None:
        try:
            if self.process is None:
                return
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=3)
            if self.reader is not None and self.reader.ident is not None:
                self.reader.join(timeout=1)
                if self.reader.is_alive():
                    raise RuntimeError("Codex App Server reader did not retire")
            self.process.stdin.close()
            self.process.stdout.close()
            self.process = None
        finally:
            if self.stderr is not None:
                self.stderr.close()
                self.stderr = None
