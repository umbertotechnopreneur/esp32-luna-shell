"""Bounded public Codex queue client; never starts or resumes a model session."""

from __future__ import annotations

from pathlib import Path

from ccp_app_server import (
    MAX_RESPONSE_BYTES,
    NativeAppServer,
    NativeInputRejected,
    NativeInputUnconfirmed,
)
from ccp_windows import runtime_directory


class PublicAudioQueue(NativeAppServer):
    methods = frozenset({"initialize", "thread/read", "thread/queue/list", "thread/queue/add"})

    # executable: Explicit installed native Codex executable, not a shell command.
    # stop_event: Optional owning worker cancellation; native requests never survive shutdown.
    # Raises original process, protocol or file errors; closes failed initialization.
    def __init__(self, executable: str, stop_event=None):
        self.queue_checked = False
        directory = runtime_directory() / "artifacts" / "voice"
        directory.mkdir(parents=True, exist_ok=True)
        log = directory / "queue-server.log"
        if log.exists() and log.stat().st_size >= 4 * 1024 * 1024:
            raise RuntimeError("Codex audio queue log is full; review it on your PC")
        super().__init__(executable, stop_event, client_info={"name": "cody_audio_queue", "version": "0.6.9"},
                         stderr=log.open("ab"), experimental=True)

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

