"""Separate authenticated PCM receiver and native Codex audio input bridge."""

from __future__ import annotations
from dataclasses import dataclass, field
import hashlib
import hmac
import ipaddress
import json
import os
from pathlib import Path
import queue
import socket
import struct
import sys
import threading
import time
from urllib.parse import urlsplit
import wave

from ccp_windows import configure_exclusive_socket, runtime_directory

HEADER = struct.Struct("!4s6s16s16sIBH")
PORT = 42119
MAX_PACKET = 1025
MAX_PCM = 16000 * 2 * 60
MAX_RECORDINGS = 64
MAX_ACTIVE = 4


class NativeInputRejected(RuntimeError):
    """The native host explicitly rejected the operation."""


class NativeInputUnconfirmed(RuntimeError):
    """An input was attempted, but its native acknowledgement is uncertain."""


@dataclass
class Recording:
    device: str
    session: bytes
    presence: bytes
    key: bytes
    source: tuple[str, int]
    thread_id: str
    turn_id: str
    expected: int = 0
    counter: int = 0
    touched: float = field(default_factory=time.monotonic)
    pcm: bytearray = field(default_factory=bytearray)
    status: int = 1
    error: str = ""
    finished: bool = False
    accepted: bool = False
    submitting: bool = False


# endpoint: Explicit local desktop/app-server endpoint; never a LAN listener.
# Raises ValueError for a missing endpoint, credentials in a URL, or non-loopback target.
def local_endpoint(endpoint: str) -> str:
    value = urlsplit(endpoint)
    if value.scheme != "ws" or value.username or value.password or not value.hostname or not value.port:
        raise ValueError("Configure an explicit loopback Codex audio WebSocket endpoint")
    if not ipaddress.ip_address(value.hostname).is_loopback or value.query or value.fragment:
        raise ValueError("Codex audio endpoint must be a literal loopback address without URL credentials")
    return endpoint


# connection: Existing local native Codex connection, never a new independent chat.
# identity: Monotonic request identity for this connection.
# method: Fixed protocol operation name.
# params: Exact native input fields.
# deadline: Optional shared monotonic deadline for the complete native submission.
# Raises RuntimeError on native errors or native authorization/input requests.
def rpc(connection, identity: int, method: str, params: dict, deadline=None):
    deadline = time.monotonic() + 15 if deadline is None else deadline
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("Codex native input deadline expired")
    connection.settimeout(remaining)
    connection.send(json.dumps({"id": identity, "method": method, "params": params}))
    for _ in range(512):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Codex native input deadline expired")
        connection.settimeout(remaining)
        message = json.loads(connection.recv())
        if message.get("id") == identity:
            if "error" in message:
                raise NativeInputRejected(f"Codex {method} rejected audio: {message['error'].get('message', 'native error')}")
            return message["result"]
        if "id" in message and "method" in message:
            # The device/plugin must not answer native approvals or user questions.
            raise NativeInputUnconfirmed("Native Codex action requires the desktop client on your PC")
    raise RuntimeError("Codex response exceeded the bounded notification budget")


# recording: Authenticated utterance bound to the exact genuine followed chat at START.
# path: Completed PCM WAV under the receiver's private runtime artifacts.
# endpoint: Explicit already-running native desktop/app-server endpoint.
# before_submit: Owner callback validating cancellation and chat identity at the send boundary.
# Raises the original transport/native error; ambiguous send is never automatically repeated.
def submit_native(recording: Recording, path: Path, endpoint: str, before_submit):
    import websocket
    headers = []
    token_file = os.environ.get("CODY_CODEX_AUDIO_TOKEN_FILE", "")
    if token_file:
        token = Path(token_file).read_text(encoding="ascii").strip()
        if not token or "\n" in token or "\r" in token:
            raise ValueError("Codex WebSocket token file is invalid")
        headers.append("Authorization: Bearer " + token)
    deadline = time.monotonic() + 15
    connection = websocket.create_connection(local_endpoint(endpoint), timeout=15, suppress_origin=True, header=headers)
    try:
        rpc(connection, 1, "initialize", {"clientInfo": {"name": "cody_voice_input", "version": "0.6.5"},
            "capabilities": {"experimentalApi": True}}, deadline)
        connection.send(json.dumps({"method": "initialized", "params": {}}))
        current = rpc(connection, 2, "thread/read", {"threadId": recording.thread_id, "includeTurns": False}, deadline)
        thread = current["thread"]
        if thread["id"] != recording.thread_id:
            raise RuntimeError("Native Codex returned a different chat identity")
        status = thread.get("status", {}).get("type")
        input_items = [{"type": "localAudio", "path": str(path)}]
        if status == "active":
            before_submit()
            return rpc(connection, 3, "turn/steer", {"threadId": recording.thread_id,
                "expectedTurnId": recording.turn_id, "input": input_items}, deadline)
        rpc(connection, 3, "thread/resume", {"threadId": recording.thread_id, "excludeTurns": True}, deadline)
        before_submit()
        return rpc(connection, 4, "turn/start", {"threadId": recording.thread_id, "input": input_items}, deadline)
    finally:
        connection.close()


class VoiceInputServer:
    # self: Optional audio owner, separate from ordinary CCP metadata.
    # responder: Existing paired CCP owner; supplies fresh authenticated endpoint/key metadata.
    def __init__(self, responder):
        self.responder = responder
        self.socket = None
        self.error = ""
        self.rejections = 0
        self.ingress_tokens = 128.0
        self.ingress_at = time.monotonic()
        self.records: dict[tuple[str, bytes], Recording] = {}
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.jobs = queue.Queue(maxsize=MAX_ACTIVE)
        self.endpoint = os.environ.get("CODY_CODEX_AUDIO_ENDPOINT", "")
        self.receiver = threading.Thread(target=self.receive, name="CCP-audio-receiver", daemon=True)
        self.delivery = threading.Thread(target=self.deliver, name="CCP-audio-delivery", daemon=True)

    # self: Optional audio owner; failures remain visible without replacing CCP presence.
    def start(self):
        try:
            from cryptography.hazmat.primitives.ciphers.aead import AESGCM
            self.cipher = AESGCM
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            configure_exclusive_socket(self.socket)
            self.socket.bind(("0.0.0.0", PORT))
            self.socket.settimeout(0.2)
            self.receiver.start()
            self.delivery.start()
        except (ImportError, OSError, RuntimeError) as error:
            self.error = str(error)
            self.stop.set()
            if self.receiver.ident is not None:
                self.receiver.join(timeout=1)
            if self.socket is not None:
                self.socket.close()
                self.socket = None
            print(f"CCP optional audio unavailable: {error}", file=sys.stderr, flush=True)

    # self: Stops only sockets and workers owned by this optional channel.
    # Raises RuntimeError if a worker cannot retire within its bounded socket deadline.
    def close(self):
        self.stop.set()
        if self.socket is not None:
            if self.receiver.ident is not None:
                self.receiver.join(timeout=1)
            if self.delivery.ident is not None:
                self.delivery.join(timeout=16)
            if self.receiver.is_alive() or self.delivery.is_alive():
                raise RuntimeError("CCP audio worker did not retire within its deadline")
            self.socket.close()

    # self: Existing authenticated audio owner.
    # device: Exact paired identity from the bounded binary header.
    # source: Socket-observed source; a saved address is insufficient.
    # Raises ValueError when fresh authenticated CCP liveness is absent.
    def peer_key(self, device: str, source: tuple[str, int]) -> tuple[bytes, str]:
        with self.responder._event_lock:
            peer = self.responder._event_peers.get(device)
            if peer is None or peer.source[0] != source[0] or not 0 <= time.monotonic() - peer.observed_at <= 10:
                raise ValueError("Audio source lacks current authenticated CCP presence")
            return peer.key, peer.nonce

    # self: Serialized audio acknowledgement owner.
    # recording: Exact authenticated input owner.
    # sequence: Last in-order packet identity; ACK counter has a separate nonce domain.
    # Raises OSError for the actual acknowledgement transport failure.
    def acknowledge(self, recording: Recording, sequence: int):
        recording.counter += 1
        if recording.counter > 0xffffffff:
            raise ValueError("Audio acknowledgement nonce budget exhausted")
        body = struct.pack("!IB", sequence, recording.status)
        header = HEADER.pack(b"CYA2", bytes.fromhex(recording.device), recording.session, recording.presence,
            recording.counter, 0, len(body))
        nonce = b"ACK1" + recording.counter.to_bytes(8, "big")
        sealed = self.cipher(recording.key).encrypt(nonce, body, header)
        self.socket.sendto(header + sealed, recording.source)

    # self: Serialized receive owner with a bounded ingress budget.
    # packet: Bounded encrypted datagram; plaintext is never logged.
    # source: Exact socket endpoint; each utterance stays bound to that endpoint.
    # Raises original authentication, bounds, file or queue error.
    def accept(self, packet: bytes, source: tuple[str, int]):
        now = time.monotonic()
        self.ingress_tokens = min(128.0, self.ingress_tokens + max(0.0, now - self.ingress_at) * 128)
        self.ingress_at = now
        if self.ingress_tokens < 1:
            raise ValueError("Audio ingress budget exhausted before cryptographic work")
        self.ingress_tokens -= 1
        if not HEADER.size + 16 <= len(packet) <= MAX_PACKET:
            raise ValueError("Audio packet length is invalid")
        magic, device_bytes, session, presence, sequence, flag, size = HEADER.unpack(packet[:HEADER.size])
        if magic != b"CYA1" or flag not in (1, 2, 3, 4) or size > 960 or size % 2 or len(packet) != HEADER.size + size + 16:
            raise ValueError("Audio header is invalid")
        if (flag == 1 and (sequence != 0 or size != 0)) or (flag != 1 and sequence == 0) or (flag in (3, 4) and size != 0):
            raise ValueError("Audio packet identity and payload kind disagree")
        device = device_bytes.hex()
        retained_key, current_nonce = self.peer_key(device, source)
        key = hmac.new(retained_key, b"cody-audio-v1|" + session.hex().encode("ascii") + b"|" +
            presence.hex().encode("ascii"), hashlib.sha256).digest()
        plaintext = self.cipher(key).decrypt(b"DAT1" + sequence.to_bytes(8, "big"), packet[HEADER.size:], packet[:HEADER.size])
        with self.lock:
            identity = (device, session)
            recording = self.records.get(identity)
            if recording is None:
                if sequence != 0 or flag != 1 or size != 0:
                    raise ValueError("Audio input requires an authenticated START")
                if not hmac.compare_digest(presence.hex(), current_nonce):
                    raise ValueError("Audio START is not bound to fresh authenticated discovery")
                # Retain terminal identities for replay rejection; bounded slots never evict a live recording.
                self.records = {item: record for item, record in self.records.items()
                    if time.monotonic() - record.touched < 180}
                if len(self.records) >= MAX_RECORDINGS or sum(not item.finished and item.status != 5
                    for item in self.records.values()) >= MAX_ACTIVE:
                    raise ValueError("Audio receiver is full")
                followed = [item for item in self.responder._activity.list_sessions() if item["followed"]]
                recording = Recording(device, session, presence, key, source,
                    followed[0]["session_id"] if len(followed) == 1 else "",
                    followed[0]["turn_id"] if len(followed) == 1 else "")
                self.records[identity] = recording
                if not recording.thread_id or not recording.turn_id:
                    recording.status, recording.error = 5, "No genuine followed Codex chat"
            if recording.source != source or not hmac.compare_digest(key, recording.key):
                raise ValueError("Audio input endpoint or retained key changed")
            if flag == 4 and size == 0:
                # Once native admission is in flight, cancellation cannot claim to undo it.
                if not recording.submitting and not recording.accepted:
                    recording.status, recording.error = 5, "Explicitly cancelled on Cody"
                    recording.pcm.clear()
                recording.expected = max(recording.expected, sequence + 1)
                self.acknowledge(recording, sequence)
                return
            if sequence > recording.expected:
                raise ValueError("Audio packet arrived out of order")
            if sequence < recording.expected and sequence != recording.expected - 1:
                raise ValueError("Audio replay is outside the one-packet acknowledgement window")
            recording.touched = time.monotonic()
            if sequence == recording.expected and not recording.finished and recording.status != 5:
                if flag == 1 and sequence != 0:
                    raise ValueError("Audio START replay is invalid")
                if flag == 2:
                    if size == 0 or len(recording.pcm) + size > MAX_PCM:
                        recording.status, recording.error = 5, "Audio input exceeds its 60-second bound"
                    else:
                        recording.pcm.extend(plaintext)
                        recording.status = 2
                elif flag == 3:
                    if size != 0 or not recording.pcm:
                        recording.status, recording.error = 5, "Audio END has no complete PCM"
                    else:
                        recording.finished = True
                        recording.status = 3
                        try:
                            self.jobs.put_nowait(recording)
                        except queue.Full:
                            recording.status, recording.error = 5, "Native audio delivery queue is full"
                recording.expected += 1
            self.acknowledge(recording, sequence)

    # self: Bounded receive/crypto worker independent of hooks and metadata.
    def receive(self):
        from cryptography.exceptions import InvalidTag
        next_report = 0.0
        while not self.stop.is_set():
            with self.lock:
                for recording in self.records.values():
                    if not recording.finished and recording.status != 5 and time.monotonic() - recording.touched > 10:
                        recording.status, recording.error = 5, "Audio capture stopped before END"
                        recording.pcm.clear()
            try:
                packet, source = self.socket.recvfrom(MAX_PACKET + 1)
                self.accept(packet, source)
            except socket.timeout:
                continue
            except (OSError, ValueError, InvalidTag, queue.Full) as error:
                self.rejections += 1
                self.error = str(error) or "Audio authentication failed"
                # Repeated invalid datagrams cannot make logging unbounded.
                if time.monotonic() >= next_report:
                    print(f"CCP audio receive rejected: {self.error}; rejected_packets={self.rejections}", file=sys.stderr, flush=True)
                    next_report = time.monotonic() + 5

    # self: File/native input worker, independent of authenticated UDP reception.
    def deliver(self):
        while not self.stop.is_set():
            try:
                recording = self.jobs.get(timeout=0.2)
            except queue.Empty:
                continue
            try:
                with self.lock:
                    if recording.status == 5:
                        continue
                directory = runtime_directory() / "artifacts" / "voice"
                directory.mkdir(parents=True, exist_ok=True)
                # Reject new persistence at capacity; never delete another utterance silently.
                paths = list(directory.glob("*.wav"))
                if len(paths) >= 1024 or sum(path.stat().st_size for path in paths) + len(recording.pcm) + 44 > 64 * 1024 * 1024:
                    raise RuntimeError("Voice artifact storage is full; review and clear recordings on your PC")
                path = directory / (recording.device + "-" + recording.session.hex() + ".wav")
                with self.lock:
                    pcm = bytes(recording.pcm)
                with wave.open(str(path), "wb") as output:
                    output.setnchannels(1)
                    output.setsampwidth(2)
                    output.setframerate(16000)
                    output.writeframes(pcm)
                followed = [item for item in self.responder._activity.list_sessions() if item["followed"]]
                if len(followed) != 1 or followed[0]["session_id"] != recording.thread_id:
                    raise RuntimeError("The followed Codex chat changed during audio capture")
                # No parameters: checks explicit cancellation and current chat at native input admission.
                # Raises RuntimeError without sending any user input if either owner changed.
                def before_submit():
                    with self.lock:
                        current = [item for item in self.responder._activity.list_sessions() if item["followed"]]
                        if self.stop.is_set() or recording.status == 5 or len(current) != 1 or current[0]["session_id"] != recording.thread_id:
                            raise RuntimeError("Audio cancelled or followed chat changed before native submission")
                        recording.submitting = True

                submit_native(recording, path, self.endpoint, before_submit)
                with self.lock:
                    recording.status, recording.accepted = 4, True
            except Exception as error:
                # Optional failure stays structured; unknown exceptions do not become delivery success.
                with self.lock:
                    recording.status = 6 if recording.submitting and not isinstance(error, NativeInputRejected) else 5
                    recording.error = str(error)
                print(f"CCP native audio delivery failed: {error}", file=sys.stderr, flush=True)
            finally:
                pcm = b""
                with self.lock:
                    recording.pcm.clear()
                self.jobs.task_done()

    # self: Reports transport/native state without PCM, keys or transcript contents.
    def status(self) -> dict:
        with self.lock:
            return {"available": self.socket is not None, "error": self.error or None,
                "rejected_packets": self.rejections,
                "native_endpoint_configured": bool(self.endpoint),
                "recordings": [{"device_id": item.device, "pc_received": item.finished,
                    "codex_accepted": item.accepted, "error": item.error or None} for item in self.records.values()]}


# No parameters: read-only audio diagnostics; never selects a chat or starts a capture.
def tool_definition():
    return {"name": "ccp_audio_status", "description": "Read one-way voice receiver and native Codex audio delivery status; no PCM or secrets.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
        "annotations": {"readOnlyHint": True, "destructiveHint": False, "openWorldHint": False}}
