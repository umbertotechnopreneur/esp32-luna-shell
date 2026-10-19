"""Trace one explicitly authorized WAV through public Codex queue/history APIs."""

from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
from hashlib import sha256
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import time
import uuid
import wave


ALLOWED = {
    "initialize", "model/list", "thread/read", "thread/turns/list",
    "thread/queue/list", "thread/queue/add",
}
ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT / "artifacts/checks/cody-public-audio-queue"


# No parameters; returns an unambiguous UTC diagnostic timestamp.
def timestamp():
    return datetime.now(timezone.utc).isoformat()


# process: Owned short-lived native app-server process.
# replies: Bounded parsed response mailbox.
# Raises transport/parse/size failures through the mailbox owner.
def read_messages(process, replies):
    try:
        while True:
            line = process.stdout.readline(16 * 1024 * 1024 + 1)
            if not line:
                raise EOFError("Public audio trace transport closed")
            if len(line) > 16 * 1024 * 1024:
                raise ValueError("Public audio trace response exceeds 16 MiB")
            replies.put_nowait(json.loads(line))
    except (OSError, ValueError, EOFError, queue.Full) as error:
        try:
            replies.put_nowait(error)
        except queue.Full:
            return


# process: Sole app-server child; this client never loads or resumes a chat.
# replies: Bounded native response mailbox.
# identity: Monotonically increasing request identity.
# method: Explicit allowlisted public RPC.
# params: RPC parameters supplied by the trace owner.
# Raises RuntimeError for a native rejection or an unexpected server request.
# Raises TimeoutError if the read exceeds its bounded deadline.
def rpc(process, replies, identity, method, params):
    if method not in ALLOWED:
        raise ValueError(f"Disallowed trace method: {method}")
    process.stdin.write(json.dumps({"id": identity, "method": method, "params": params}) + "\n")
    process.stdin.flush()
    deadline = time.monotonic() + 15
    for _ in range(128):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"Public trace timed out: {method}")
        try:
            message = replies.get(timeout=remaining)
        except queue.Empty as error:
            raise TimeoutError(f"Public trace timed out: {method}") from error
        if isinstance(message, Exception):
            raise message
        if "id" in message and "method" in message:
            raise RuntimeError("Read-only trace unexpectedly requested interaction")
        if message.get("id") == identity:
            if "error" in message:
                error = message["error"]
                raise RuntimeError(f"{method}: {error.get('code')}: {error.get('message')}")
            return message["result"]
    raise RuntimeError("Public trace exceeded its notification budget")


# value: A public user-input object; raw audio and unrelated text stay private.
# marker: Exact synthetic-pilot correlation text.
# Returns bounded metadata and byte hashes, never PCM/base64 or chat transcripts.
def input_summary(value, marker):
    kind = value.get("type", "unknown")
    result = {"type": kind}
    if kind == "text":
        text = value.get("text", "")
        result["characters"] = len(text)
        result["pilot_marker"] = marker in text
        result["audio_omitted_placeholder"] = "audio content omitted" in text.lower()
    if kind == "localAudio":
        result["local_audio_path"] = value.get("path")
    if kind == "audio":
        url = value.get("url", "")
        result["inline_data"] = url.startswith("data:")
        if result["inline_data"]:
            header, encoded = url.split(",", 1)
            result["media_header"] = header
            if ";base64" not in header:
                raise ValueError("Audio data URL is not base64 encoded")
            payload = base64.b64decode(encoded, validate=True)
            result["bytes"] = len(payload)
            result["sha256"] = sha256(payload).hexdigest()
        else:
            result["remote_url_present"] = bool(url)
    return result


# process: Sole owned public app-server process.
# replies: Bounded native response mailbox.
# thread_id: Explicit intended existing chat identity.
# pilot_id: Distinct synthetic test identity.
# request_id: Next available connection-local RPC identity.
# Returns a sanitized snapshot and next RPC identity.
# Raises original native/schema failures rather than inventing observations.
def snapshot(process, replies, thread_id, pilot_id, request_id):
    marker = f"CODY_AUDIO_TRACE {pilot_id}"
    metadata = rpc(process, replies, request_id, "thread/read", {
        "threadId": thread_id, "includeTurns": False})["thread"]
    request_id += 1
    turns = rpc(process, replies, request_id, "thread/turns/list", {
        "threadId": thread_id, "limit": 6, "sortDirection": "desc", "itemsView": "summary"})
    request_id += 1
    queued = rpc(process, replies, request_id, "thread/queue/list", {
        "threadId": thread_id, "limit": 10})
    request_id += 1
    user_messages = []
    for turn in turns["data"]:
        for item in turn.get("items", []):
            if item.get("type") != "userMessage":
                continue
            user_messages.append({
                "turn_id": turn["id"], "turn_status": turn.get("status"),
                "turn_error_present": turn.get("error") is not None, "item_id": item["id"],
                "input": [input_summary(value, marker) for value in item["content"]],
            })
    own_queue = [
        {"id": item["id"], "input": [input_summary(value, marker) for value in item["input"]]}
        for item in queued["data"] if item.get("clientUserMessageId") == pilot_id
        or any(value.get("type") == "text" and marker in value.get("text", "")
               for value in item["input"])
    ]
    return {"timestamp": timestamp(), "thread_id": thread_id,
            "model": metadata.get("model"), "model_provider": metadata.get("modelProvider"),
            "thread_status": metadata.get("status"), "status_scope": "trace_app_server_only",
            "pilot_queue": own_queue,
            "queue_page_complete": queued.get("nextCursor") is None,
            "recent_user_messages": user_messages}, request_id


# path: Private local JSONL diagnostic output.
# value: Sanitized metadata containing no raw input audio or chat transcripts.
# Raises OSError for a failed diagnostic write.
def record(path, value):
    with path.open("a", encoding="utf-8") as output:
        output.write(json.dumps(value) + "\n")
        output.flush()
        os.fsync(output.fileno())


# argv: Explicit executable/chat/pilot and optional one-time WAV submission.
# Raises original configuration, protocol, persistence and child-process errors.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codex-executable", type=Path, required=True)
    parser.add_argument("--thread-id", type=uuid.UUID, required=True)
    parser.add_argument("--pilot-id", type=uuid.UUID, required=True)
    parser.add_argument("--submit-wav", type=Path)
    parser.add_argument("--watch-seconds", type=int, default=0)
    args = parser.parse_args(argv)
    if not 0 <= args.watch_seconds <= 600:
        parser.error("watch-seconds must be between 0 and 600")
    if args.submit_wav and args.watch_seconds:
        parser.error("Submit once, then start a separate read-only watcher")
    executable = args.codex_executable.resolve(strict=True)
    DIRECTORY.mkdir(parents=True, exist_ok=True)
    thread_id, pilot_id = str(args.thread_id), str(args.pilot_id)
    evidence = DIRECTORY / f"trace-{pilot_id}.jsonl"
    receipt_path = DIRECTORY / f"pilot-{pilot_id}.json"
    receipt = None
    if args.submit_wav:
        wav_path = args.submit_wav.resolve(strict=True)
        if not wav_path.is_relative_to(ROOT / "artifacts"):
            raise ValueError("Synthetic WAV must be under repository artifacts")
        with wave.open(str(wav_path), "rb") as audio:
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) != (1, 2, 16000):
                raise ValueError("Synthetic WAV must be mono PCM16 at 16 kHz")
            if not 0 < audio.getnframes() <= 960000:
                raise ValueError("Synthetic WAV duration must be nonzero and at most 60 seconds")
        receipt = {"thread_id": thread_id, "pilot_id": pilot_id, "state": "attempt_reserved",
                   "wav": str(wav_path), "wav_bytes": wav_path.stat().st_size,
                   "wav_sha256": sha256(wav_path.read_bytes()).hexdigest(), "timestamp": timestamp()}
        with receipt_path.open("x", encoding="utf-8") as output:
            json.dump(receipt, output, indent=2)
            output.flush()
            os.fsync(output.fileno())
    with (DIRECTORY / f"trace-stderr-{pilot_id}-{uuid.uuid4()}.log").open("x", encoding="utf-8") as errors:
        process = subprocess.Popen([str(executable), "app-server", "--listen", "stdio://"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors,
                                   text=True, encoding="utf-8",
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        replies = queue.Queue(maxsize=128)
        reader = threading.Thread(target=read_messages, args=(process, replies), daemon=True)
        reader.start()
        try:
            rpc(process, replies, 1, "initialize", {
                "clientInfo": {"name": "cody_public_audio_trace", "version": "0.6.8"},
                "capabilities": {"experimentalApi": True}})
            process.stdin.write(json.dumps({"method": "initialized", "params": {}}) + "\n")
            process.stdin.flush()
            models = rpc(process, replies, 2, "model/list", {"limit": 100, "includeHidden": True})
            record(evidence, {"event": "model_catalog", "timestamp": timestamp(),
                              "models": [{"model": item["model"], "input_modalities": item.get("inputModalities")}
                                         for item in models["data"]],
                              "complete": models.get("nextCursor") is None})
            view, identity = snapshot(process, replies, thread_id, pilot_id, 3)
            record(evidence, {"event": "baseline", **view})
            if receipt is not None:
                marker = f"CODY_AUDIO_TRACE {pilot_id}"
                message = (f"{marker}. This is the owner's authorized synthetic WAV delivery test. "
                           "Continue tracing this exact pilot through the public read-only history API. "
                           "State whether actual audio is available or only an omission placeholder. "
                           "Do not infer its words from the local fixture or read its transcript source.")
                admitted = rpc(process, replies, identity, "thread/queue/add", {
                    "threadId": thread_id, "clientUserMessageId": pilot_id,
                    "input": [{"type": "text", "text": message, "text_elements": []},
                              {"type": "localAudio", "path": str(wav_path)}]})
                identity += 1
                receipt.update({"state": "queued", "queued_id": admitted["queuedSubmission"]["id"]})
                receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
                record(evidence, {"event": "queue_admitted", **receipt})
                view, identity = snapshot(process, replies, thread_id, pilot_id, identity)
                record(evidence, {"event": "after_admission", **view})
            deadline = time.monotonic() + args.watch_seconds
            while time.monotonic() < deadline:
                time.sleep(min(5, max(0, deadline - time.monotonic())))
                view, identity = snapshot(process, replies, thread_id, pilot_id, identity)
                record(evidence, {"event": "watch", **view})
                if any(any(value.get("pilot_marker") for value in item["input"])
                       for item in view["recent_user_messages"]):
                    break
            print(json.dumps({"pilot_id": pilot_id, "audio_submitted": receipt is not None,
                              "trace": str(evidence), "latest": view}))
        finally:
            try:
                process.stdin.close()
            finally:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                reader.join(timeout=2)
                process.stdout.close()
                if reader.is_alive():
                    raise RuntimeError("Public audio trace reader did not retire")


if __name__ == "__main__":
    main()
