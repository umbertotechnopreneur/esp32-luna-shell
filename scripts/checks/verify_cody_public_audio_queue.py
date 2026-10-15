"""Explicit live compatibility probe of Codex's public experimental queue API.

Starts one short-lived, queue-only stdio app-server. The default probe is read
only. Explicit pilot arguments may enqueue one WAV, observe or withdraw that
exact item. Never resumes a chat, runs inference or accesses private storage.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import time
import uuid
import wave


# stream: Owned app-server stdout; no raw native payload is logged.
# replies: Bounded mailbox consumed by the deadline owner.
# Exceptions: Read/parse failures are delivered to that owner.
def read_replies(stream, replies):
    try:
        while True:
            line = stream.readline(1024 * 1024 + 1)
            if not line:
                raise EOFError("Native queue probe connection closed")
            if len(line) > 1024 * 1024:
                raise ValueError("Native queue probe reply exceeds 1 MiB")
            replies.put_nowait(json.loads(line))
    except (OSError, ValueError, EOFError, queue.Full) as error:
        try:
            replies.put_nowait(error)
        except queue.Full:
            # The owner fails when its notification budget is exhausted.
            return


# process: The sole child process owned by this probe.
# replies: Bounded native response mailbox.
# identity: Request ID belonging only to this connection.
# method: Fixed public initialization or queue API method.
# params: Explicit bounded native request fields.
# deadline: Whole probe monotonic deadline, including initialization.
# Raises RuntimeError for native rejection or an unexpected server request.
# Raises original timeout, transport, decoding or protocol errors.
def request(process, replies, identity, method, params, deadline):
    if method not in {"initialize", "thread/queue/list", "thread/queue/add", "thread/queue/delete"}:
        raise ValueError("Only public queue operations are allowed in this pilot")
    process.stdin.write(json.dumps({"id": identity, "method": method, "params": params}) + "\n")
    process.stdin.flush()
    for _ in range(128):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Native queue compatibility probe timed out")
        try:
            message = replies.get(timeout=remaining)
        except queue.Empty as error:
            raise TimeoutError("Native queue compatibility probe timed out") from error
        if isinstance(message, Exception):
            raise message
        if "id" in message and "method" in message:
            raise RuntimeError("Native queue probe unexpectedly requested user interaction")
        if message.get("id") == identity:
            if "error" in message:
                error = message["error"]
                # Native errors are surfaced; unrelated payloads are never printed.
                raise RuntimeError(f"{method}: {error.get('code')}: {error.get('message')}")
            return message["result"]
    raise RuntimeError("Native queue probe exceeded its notification budget")


# argv: Explicit executable, genuine hook-observed chat and optional single-use pilot action.
# Raises original configuration/process/protocol errors; failure is not readiness.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--live-probe", action="store_true", required=True)
    parser.add_argument("--codex-executable", type=Path, required=True)
    parser.add_argument("--thread-id", type=uuid.UUID, required=True)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--enqueue-wav", type=Path)
    action.add_argument("--observe-pilot", type=uuid.UUID)
    action.add_argument("--withdraw-pilot", type=uuid.UUID)
    parser.add_argument("--pilot-id", type=uuid.UUID)
    arguments = parser.parse_args(argv)
    if bool(arguments.enqueue_wav) != bool(arguments.pilot_id):
        parser.error("--enqueue-wav requires one explicit --pilot-id; other actions do not")
    executable = arguments.codex_executable.resolve(strict=True)
    if not executable.is_file():
        raise ValueError("Select the installed native Codex executable")
    root = Path(__file__).resolve().parents[2]
    evidence = root / "artifacts/checks/cody-public-audio-queue"
    evidence.mkdir(parents=True, exist_ok=True)
    receipt = None
    receipt_path = None
    pilot = arguments.pilot_id or arguments.observe_pilot or arguments.withdraw_pilot
    if pilot:
        receipt_path = evidence / f"pilot-{pilot}.json"
    if arguments.enqueue_wav:
        wav_path = arguments.enqueue_wav.resolve(strict=True)
        if not wav_path.is_relative_to(root / "artifacts"):
            raise ValueError("Pilot WAV must be an explicit file under repository artifacts")
        with wave.open(str(wav_path), "rb") as audio:
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) != (1, 2, 16000):
                raise ValueError("Pilot WAV must contain mono 16-bit 16 kHz PCM")
            if not 0 < audio.getnframes() <= 16000 * 60:
                raise ValueError("Pilot WAV must contain at most 60 seconds of nonempty audio")
        receipt = {"thread_id": str(arguments.thread_id), "pilot_id": str(pilot),
                   "state": "attempt_reserved", "wav": str(wav_path)}
        # Reserve before transmitting. An uncertain attempt must never be replayed.
        with receipt_path.open("x", encoding="utf-8") as output:
            json.dump(receipt, output, indent=2)
            output.flush()
            os.fsync(output.fileno())
    elif pilot:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        if (receipt.get("thread_id") != str(arguments.thread_id) or receipt.get("pilot_id") != str(pilot)
                or not isinstance(receipt.get("queued_id"), str) or not receipt["queued_id"]):
            raise ValueError("Pilot receipt does not identify a confirmed queue admission for this chat")
    deadline = time.monotonic() + 15
    replies = queue.Queue(maxsize=128)
    with (evidence / f"native-stderr-{uuid.uuid4()}.log").open("x", encoding="utf-8") as errors:
        process = subprocess.Popen([str(executable), "app-server", "--listen", "stdio://"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors,
                                   text=True, encoding="utf-8", creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        reader = threading.Thread(target=read_replies, args=(process.stdout, replies), daemon=True)
        reader.start()
        try:
            request(process, replies, 1, "initialize", {
                "clientInfo": {"name": "cody_public_queue_probe", "version": "0.6.8"},
                "capabilities": {"experimentalApi": True}}, deadline)
            process.stdin.write(json.dumps({"method": "initialized", "params": {}}) + "\n")
            process.stdin.flush()
            result = request(process, replies, 2, "thread/queue/list", {
                "threadId": str(arguments.thread_id), "limit": 1}, deadline)
            if arguments.enqueue_wav:
                admitted = request(process, replies, 3, "thread/queue/add", {
                    "threadId": str(arguments.thread_id), "clientUserMessageId": str(pilot),
                    "input": [{"type": "localAudio", "path": str(wav_path)}]}, deadline)
                queued_id = admitted["queuedSubmission"]["id"]
                if not isinstance(queued_id, str) or not queued_id:
                    raise ValueError("Native queue admission returned no item identity")
                receipt.update({"state": "queued", "queued_id": queued_id})
                receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
                print(json.dumps({"public_queue_readable": True, "audio_queued": True,
                                  "desktop_delivery_verified": False, "pilot_id": str(pilot)}))
            elif arguments.withdraw_pilot:
                withdrawn = request(process, replies, 3, "thread/queue/delete", {
                    "threadId": str(arguments.thread_id), "queuedSubmissionId": receipt["queued_id"]}, deadline)
                print(json.dumps({"withdrawal": withdrawn, "desktop_delivery_verified": False}))
            elif arguments.observe_pilot:
                # Follow opaque public pagination without logging other queue contents.
                found = any(item["id"] == receipt["queued_id"] for item in result["data"])
                cursor = result.get("nextCursor")
                identity = 3
                while not found and cursor:
                    result = request(process, replies, identity, "thread/queue/list", {
                        "threadId": str(arguments.thread_id), "cursor": cursor, "limit": 10}, deadline)
                    found = any(item["id"] == receipt["queued_id"] for item in result["data"])
                    cursor = result.get("nextCursor")
                    identity += 1
                    if identity > 130:
                        raise RuntimeError("Pilot observation exceeded the bounded page budget")
                print(json.dumps({"pilot_still_queued": found, "desktop_delivery_verified": False}))
            else:
                print(json.dumps({"public_queue_readable": True, "returned_items": len(result["data"]),
                                  "audio_submitted": False, "desktop_delivery_verified": False}))
        finally:
            try:
                process.stdin.close()
            finally:
                # Even a broken input pipe must not skip owned-process retirement.
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                reader.join(timeout=2)
                process.stdout.close()
                if reader.is_alive():
                    raise RuntimeError("Native queue probe reader did not stop")


if __name__ == "__main__":
    main()
