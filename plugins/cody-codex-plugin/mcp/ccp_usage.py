# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added native usage metadata ownership on 2026-10-14.
# Human guidance: Umberto Giacobbi; requested real Codex token/context observations.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Bounded reader of native Codex usage metadata for one hook-owned chat."""

from datetime import datetime
from contextlib import closing
import json
import os
from pathlib import Path
import re
import sqlite3
import time

READ_BUDGET = 128 * 1024
CHUNK_BYTES = 4096
METADATA_LIMIT = 128 * 1024
RECORD_LIMIT = 512
IGNORED_RECORDS = (b"response_item", b"compacted", b"world_state",
                   b"inter_agent_communication_metadata", b"token_usage_record")
_ENVELOPE = re.compile(rb'^\s*\{\s*"timestamp"\s*:\s*"[^"\r\n]{1,64}"\s*,\s*(?:"ordinal"\s*:\s*[0-9]{1,20}\s*,\s*)?"type"\s*:\s*"([a-z_]+)"\s*,\s*"payload"\s*:')
_EVENT = re.compile(rb'^\s*\{\s*"type"\s*:\s*"([a-z_]+)"')


class UsageSourceError(ValueError):
    """Fixed diagnostic reason with no native path, prompt or record content."""


# error: Rejected native source operation; exception strings may contain private input.
# Returns a fixed reason or operating-system code without serializing the offending record.
def safe_usage_error(error):
    if isinstance(error, UsageSourceError):
        return str(error)
    if isinstance(error, OSError):
        return f"Native usage file operation failed: errno={error.errno} winerror={getattr(error, 'winerror', None)}"
    if isinstance(error, sqlite3.Error):
        return f"Native usage index failed: {getattr(error, 'sqlite_errorname', type(error).__name__)}"
    return f"Native usage metadata rejected: {type(error).__name__}"


# line: Bounded native JSONL prefix or complete metadata record, never echoed.
# oversized: True when the line exceeded the storage bound while its prefix was retained.
# Raises UsageSourceError for unsupported envelopes or oversized/malformed metadata.
def metadata_record(line, oversized=False):
    if not line.strip():
        return None
    envelope = _ENVELOPE.match(line[:1024])
    if envelope is None:
        raise UsageSourceError("Native usage record envelope is unsupported")
    kind = envelope[1]
    if kind in IGNORED_RECORDS:
        return None
    if kind == b"event_msg":
        event = _EVENT.match(line[envelope.end():envelope.end() + 512])
        if event is None:
            raise UsageSourceError("Native usage event envelope is unsupported")
        if event[1] not in (b"token_count", b"task_started"):
            return None
    elif kind not in (b"session_meta", b"turn_context"):
        raise UsageSourceError("Native usage record type is unsupported")
    if oversized:
        raise UsageSourceError("Native usage metadata exceeds its bound")
    try:
        decoded = json.loads(line)
    except (ValueError, UnicodeError):
        raise UsageSourceError("Native usage metadata JSON is malformed") from None
    payload = decoded.get("payload")
    if not isinstance(payload, dict):
        raise UsageSourceError("Native usage metadata payload is invalid")
    fields = {b"session_meta": ("id", "session_id", "cwd"),
              b"turn_context": ("turn_id", "cwd", "model", "effort"),
              b"event_msg": ("type", "turn_id", "info")}[kind]
    return {"type": decoded["type"], "timestamp": decoded.get("timestamp"),
            "payload": {name: payload[name] for name in fields if name in payload}}


# value: Native checkout path; normalize Windows extended prefixes without changing identity.
# Raises UsageSourceError for missing, oversized or invalid native checkout metadata.
def checkout_identity(value: str) -> str:
    if not isinstance(value, str) or not 0 < len(value) <= 4096 or "\x00" in value:
        raise UsageSourceError("Native usage checkout metadata is invalid")
    if value.startswith("\\\\?\\UNC\\"):
        value = "\\\\" + value[8:]
    elif value.startswith("\\\\?\\"):
        value = value[4:]
    return os.path.normcase(os.path.abspath(value))


# value: Native filesystem path, including Windows extended drive or UNC spellings.
# Raises OSError or UsageSourceError if the source does not exist or its identity is invalid.
def resolved_native_path(value) -> Path:
    # Path.resolve can preserve an incoming \\?\ prefix. Confinement must compare
    # the same canonical spelling on both sides after resolving symlinks.
    resolved = Path(value).resolve(strict=True)
    return Path(checkout_identity(str(resolved)))


# value: Native unsigned token counter, never a textual estimate.
# Raises ValueError for unsupported or negative counters.
def counter(value):
    if type(value) is not int or not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
        raise UsageSourceError("Native usage counter is invalid")
    return value


class NativeUsageReader:
    # No parameters: all files remain local; ownership is established by genuine hooks.
    def __init__(self):
        self.home = Path(checkout_identity(str(Path(os.environ.get(
            "CODEX_HOME", str(Path.home() / ".codex"))).resolve())))
        self.identity = None
        self.path = None
        self.file_identity = None
        self.file_size = 0
        self.offset = 0
        self.pending = bytearray()
        self.discarding = False
        self.verified = False
        self.native_turn = ""
        self.sample = None
        self.context = None
        self.retry_at = 0.0
        self.failed = False
        self.clock_offset = time.monotonic() - time.time()
        self.header_end = 0
        self.bootstrap_position = None
        self.bootstrap_records = []
        self.bootstrap_carry = b""
        self.bootstrap_oversized = False
        self.bootstrapped = False
        self.bytes_read_last_poll = 0
        self.bootstrap_metadata_bytes = 0

    # session: Genuine followed hook session identity.
    # cwd: Genuine hook checkout identity.
    # locator: Optional native transcript path; never transmitted to the device.
    # Raises OSError, ValueError or sqlite3.Error for rejected source ownership.
    def locate(self, session, cwd, locator):
        if locator:
            path = Path(locator)
        else:
            database = resolved_native_path(self.home / "state_5.sqlite")
            if not database.is_relative_to(self.home):
                raise UsageSourceError("Native usage state index escapes its owner")
            with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True, timeout=0.05)) as connection:
                row = connection.execute("SELECT rollout_path, cwd FROM threads WHERE id = ?", (session,)).fetchone()
            if row is None:
                raise UsageSourceError("Native usage session is absent from the state index")
            if checkout_identity(row[1]) != checkout_identity(cwd):
                raise UsageSourceError("Native usage index checkout mismatch")
            path = Path(row[0])
        resolved = resolved_native_path(path)
        approved = resolved_native_path(self.home / "sessions")
        if (not approved.is_relative_to(self.home) or not resolved.is_relative_to(approved)
                or resolved.suffix != ".jsonl"):
            raise UsageSourceError("Native usage path is outside the session owner")
        self.path = resolved

    # event: Parsed native metadata record; prompt/item contents are never retained.
    # session: Exact hook identity required in every session metadata record.
    # cwd: Exact hook checkout required in session and turn metadata.
    # turn: Current genuine hook turn; other native turns cannot populate its projection.
    # Raises ValueError for identity, clock or counter inconsistencies.
    def consume(self, event, session, cwd, turn):
        kind = event.get("type")
        payload = event.get("payload")
        if not isinstance(payload, dict):
            return
        if kind == "session_meta":
            if (payload.get("id") != session or payload.get("session_id", session) != session
                    or checkout_identity(payload.get("cwd", "")) != checkout_identity(cwd)):
                raise UsageSourceError("Native usage session metadata mismatch")
            self.verified = True
            self.native_turn = ""
            self.sample = None
            self.context = None
            return
        if not self.verified:
            raise UsageSourceError("Native usage is missing initial session metadata")
        if kind == "turn_context" or (kind == "event_msg" and payload.get("type") == "task_started"):
            next_turn = payload.get("turn_id", "")
            if kind == "event_msg" and next_turn and next_turn == self.native_turn:
                # Repeated start metadata for the same native identity carries
                # neither new model configuration nor a usage observation.
                return
            self.native_turn = next_turn
            self.sample = None
            self.context = None
            if kind == "turn_context" and self.native_turn == turn:
                if checkout_identity(payload.get("cwd", "")) != checkout_identity(cwd):
                    raise UsageSourceError("Native usage turn checkout mismatch")
                model, effort = payload.get("model"), payload.get("effort")
                if (isinstance(model, str) and 0 < len(model) <= 128 and model.isascii()
                        and all(33 <= ord(char) <= 126 for char in model)):
                    if effort is not None and (not isinstance(effort, str) or not 0 < len(effort) <= 16
                            or not all(33 <= ord(char) <= 126 for char in effort)):
                        raise UsageSourceError("Native usage effort metadata is invalid")
                    self.context = (model, effort, self.observed_at(event))
            return
        if kind != "event_msg" or payload.get("type") != "token_count" or self.native_turn != turn:
            return
        info = payload.get("info")
        if not isinstance(info, dict):
            return
        last, total = info.get("last_token_usage"), info.get("total_token_usage")
        if not isinstance(last, dict) or not isinstance(total, dict):
            raise UsageSourceError("Native usage counters are incomplete")
        values = {"used": counter(last.get("total_tokens")),
                  "input": counter(last.get("input_tokens")),
                  "output": counter(last.get("output_tokens")),
                  "cumulative": counter(total.get("total_tokens"))}
        capacity = info.get("model_context_window")
        if capacity is not None:
            values["capacity"] = counter(capacity)
            if values["capacity"] == 0:
                raise UsageSourceError("Native context capacity is zero")
        observed = self.observed_at(event)
        if self.sample is not None and observed < self.sample[1]:
            raise UsageSourceError("Native usage timestamp moved backwards")
        self.sample = (values, observed)

    # event: Native record with an original timezone-qualified timestamp.
    # Raises ValueError for missing, naive or future timestamps; rereads never renew age.
    def observed_at(self, event):
        try:
            timestamp = datetime.fromisoformat(event["timestamp"].replace("Z", "+00:00"))
        except (ValueError, TypeError, AttributeError, KeyError):
            raise UsageSourceError("Native usage timestamp is malformed") from None
        if timestamp.tzinfo is None:
            raise UsageSourceError("Native usage timestamp has no timezone")
        age = time.time() - timestamp.timestamp()
        if age < 0:
            raise UsageSourceError("Native usage timestamp is in the future")
        return self.clock_offset + timestamp.timestamp()

    # stream: Verified open native file; reads are charged to this poll's shared budget.
    # count: Desired bytes, capped to one small IO chunk.
    def read_chunk(self, stream, count=CHUNK_BYTES):
        chunk = stream.read(min(count, CHUNK_BYTES, READ_BUDGET - self.bytes_read_last_poll))
        self.bytes_read_last_poll += len(chunk)
        return chunk

    # stream: Open verified file positioned independently of the forward reader.
    # size: Fixed file-size observation; later appends are handled by the forward reader.
    # session: Exact hook identity verified against the first metadata record.
    # cwd: Exact checkout identity.
    # turn: Genuine current hook turn identity, never inferred from record proximity alone.
    # Raises UsageSourceError for missing/ambiguous headers or excessive metadata backlog.
    def bootstrap(self, stream, size, session, cwd, turn):
        if not self.verified:
            stream.seek(self.offset)
            while self.bytes_read_last_poll < READ_BUDGET:
                chunk = self.read_chunk(stream)
                if not chunk:
                    return False
                before, separator, _ = chunk.partition(b"\n")
                self.pending.extend(before)
                self.offset += len(before) + bool(separator)
                if len(self.pending) > METADATA_LIMIT:
                    raise UsageSourceError("Native usage session header exceeds its bound")
                if separator:
                    record = metadata_record(self.pending)
                    if record is None or record.get("type") != "session_meta":
                        raise UsageSourceError("Native usage initial session header is missing")
                    self.consume(record, session, cwd, turn)
                    self.pending.clear()
                    self.header_end = self.offset
                    break
        if not self.verified:
            return False
        if self.bootstrap_position is None:
            # Start at the recent tail, never parse 25 MB of old chat contents.
            self.bootstrap_position = size
            self.offset = size
            self.discarding = True  # Ignore the possibly incomplete trailing line.
        while self.bootstrap_position > self.header_end and self.bytes_read_last_poll < READ_BUDGET:
            count = min(CHUNK_BYTES, self.bootstrap_position - self.header_end,
                        READ_BUDGET - self.bytes_read_last_poll)
            self.bootstrap_position -= count
            stream.seek(self.bootstrap_position)
            chunk = self.read_chunk(stream, count)
            fragments = chunk.split(b"\n")
            for index in range(len(fragments) - 1, -1, -1):
                fragment = fragments[index]
                if self.discarding:
                    # Keep the forward cursor at the start of an unfinished final record.
                    self.offset -= len(fragment)
                else:
                    combined = fragment + self.bootstrap_carry
                    self.bootstrap_oversized |= len(combined) > METADATA_LIMIT
                    self.bootstrap_carry = combined[:METADATA_LIMIT]
                if index == 0:
                    continue
                if self.discarding:
                    self.discarding = False
                    continue
                record = metadata_record(self.bootstrap_carry, self.bootstrap_oversized)
                self.bootstrap_carry = b""
                self.bootstrap_oversized = False
                if record is None:
                    continue
                self.bootstrap_records.append(record)
                self.bootstrap_metadata_bytes += len(json.dumps(record))
                if len(self.bootstrap_records) > RECORD_LIMIT or self.bootstrap_metadata_bytes > METADATA_LIMIT:
                    raise UsageSourceError("Native usage bootstrap metadata count exceeds its bound")
                if record.get("type") == "session_meta":
                    raise UsageSourceError("Native usage bootstrap crosses a second session identity")
                if record.get("type") == "turn_context":
                    for observation in reversed(self.bootstrap_records):
                        self.consume(observation, session, cwd, turn)
                    self.bootstrap_records.clear()
                    self.bootstrapped = True
                    return True
        if self.bootstrap_position == self.header_end:
            self.discarding = False
            if self.bootstrap_carry:
                record = metadata_record(self.bootstrap_carry, self.bootstrap_oversized)
                if record is not None:
                    self.bootstrap_records.append(record)
            for observation in reversed(self.bootstrap_records):
                self.consume(observation, session, cwd, turn)
            self.bootstrap_records.clear()
            self.bootstrap_carry = b""
            self.bootstrapped = True
            return True
        return False

    # chunk: Small forward IO fragment; only allowlisted metadata reaches the JSON parser.
    # session: Exact hook session identity.
    # cwd: Exact hook checkout identity.
    # turn: Exact current native turn identity.
    # Raises UsageSourceError for oversized metadata; oversized response items are ignored.
    def feed(self, chunk, session, cwd, turn):
        for fragment in chunk.splitlines(keepends=True):
            complete = fragment.endswith(b"\n")
            if not self.discarding:
                remaining = METADATA_LIMIT - len(self.pending)
                self.pending.extend(fragment[:remaining])
                prefix = _ENVELOPE.match(self.pending[:1024])
                irrelevant = prefix is not None and prefix[1] in IGNORED_RECORDS
                if prefix is not None and prefix[1] == b"event_msg":
                    event = _EVENT.match(self.pending[prefix.end():prefix.end() + 512])
                    irrelevant = event is not None and event[1] not in (b"token_count", b"task_started")
                if irrelevant:
                    self.pending.clear()
                    self.discarding = True
                elif len(fragment) > remaining:
                    # The retained prefix establishes that this is an irrelevant response
                    # before discarding its body. Unknown/metadata records fail closed.
                    metadata_record(self.pending, oversized=True)
                    self.pending.clear()
                    self.discarding = True
            if complete:
                if not self.discarding:
                    record = metadata_record(self.pending)
                    if record is not None:
                        self.consume(record, session, cwd, turn)
                self.pending.clear()
                self.discarding = False

    # session: Exact followed chat, admitted by genuine hooks.
    # cwd: Hook checkout identity.
    # turn: Exact current native hook turn identity.
    # locator: Optional original transcript path; otherwise resolve exact SQLite identity.
    # Raises OSError, ValueError or sqlite3.Error; caller publishes unavailable metadata.
    def poll(self, session, cwd, turn, locator=""):
        # A native turn can be appended before its hook is delivered. Rebootstrap
        # on the actual selected hook turn so previously withheld metadata is
        # recovered without inventing a new observation timestamp.
        identity = (session, checkout_identity(cwd), turn, locator)
        if identity != self.identity:
            self.__init__()
            self.identity = identity
        if self.failed:
            return None, None
        self.bytes_read_last_poll = 0
        try:
            if self.path is None:
                if time.monotonic() < self.retry_at:
                    return None, None
                self.retry_at = time.monotonic() + 5.0
                self.locate(session, cwd, locator)
            # Re-resolve on every read, so a replaced path cannot escape via a symlink.
            if resolved_native_path(self.path) != self.path:
                raise UsageSourceError("Native usage path changed identity")
            expected_file = self.path.stat()
            with self.path.open("rb") as stream:
                stat = os.fstat(stream.fileno())
                file_identity = (stat.st_dev, stat.st_ino)
                if file_identity != (expected_file.st_dev, expected_file.st_ino):
                    raise UsageSourceError("Native usage file changed during open")
                if self.file_identity is not None and (file_identity != self.file_identity or stat.st_size < self.file_size):
                    raise UsageSourceError("Native usage file was replaced or truncated")
                self.file_identity = file_identity
                self.file_size = stat.st_size
                if not self.bootstrapped and not self.bootstrap(stream, stat.st_size, session, cwd, turn):
                    return None, None
                stream.seek(self.offset)
                while self.bytes_read_last_poll < READ_BUDGET:
                    chunk = self.read_chunk(stream)
                    if not chunk:
                        break
                    self.offset += len(chunk)
                    self.feed(chunk, session, cwd, turn)
                # Never expose an earlier window while catching up to newer native events.
                if self.offset < stat.st_size or self.pending or self.discarding:
                    return None, None
            if self.native_turn != turn:
                return None, None
            return self.sample, self.context
        except (OSError, ValueError, KeyError, TypeError, sqlite3.Error):
            self.sample = None
            self.context = None
            if self.path is not None:
                self.failed = True
            raise
