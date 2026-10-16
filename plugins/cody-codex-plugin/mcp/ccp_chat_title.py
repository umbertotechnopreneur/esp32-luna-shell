# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added native thread-title metadata ownership on 2026-10-14.
# Human guidance: Umberto Giacobbi; requested the real followed chat title on Home.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Read bounded native thread names from the local Codex index, never a transcript."""

from contextlib import closing
from dataclasses import dataclass
from enum import Enum
import os
from pathlib import Path
import sqlite3
import sys
import time

from ccp_usage import checkout_identity, resolved_native_path


TITLE_REFRESH_SECONDS = 5.0
TITLE_LEASE_SECONDS = 45.0
TITLE_MAX_BYTES = 255


class TitleState(str, Enum):
    CURRENT = "current"
    UNAVAILABLE = "unavailable"
    UNSUPPORTED = "unsupported"
    ERROR = "error"


@dataclass(frozen=True)
class ChatTitleObservation:
    title: str = ""
    state: TitleState = TitleState.UNAVAILABLE
    observed_at: float | None = None


class TitleSourceError(ValueError):
    """A fixed diagnostic that never includes the rejected title, ID or native path."""


# error: Original source error, whose raw text may include private metadata.
# Returns a fixed rejection reason or original OS/SQLite error code without native data.
def safe_title_error(error: Exception) -> str:
    if isinstance(error, TitleSourceError):
        return str(error)
    if isinstance(error, OSError):
        return f"file operation failed: errno={error.errno} winerror={getattr(error, 'winerror', None)}"
    if isinstance(error, sqlite3.Error):
        return f"index operation failed: {getattr(error, 'sqlite_errorname', type(error).__name__)}"
    return f"source metadata rejected: {type(error).__name__}"


class NativeChatTitleReader:
    # self: Single-reader cache owned by ActivityStore's existing source IO lock.
    # clock: Optional monotonic observation clock; fixtures may supply a deterministic clock.
    # Raises OSError or UsageSourceError if the configured native home identity is invalid.
    def __init__(self, clock=None):
        self.home = Path(checkout_identity(str(Path(os.environ.get(
            "CODEX_HOME", str(Path.home() / ".codex"))).resolve())))
        self._clock = clock if clock is not None else time.monotonic
        self._identity: tuple[str, str] | None = None
        self._retry_at = 0.0
        self._observation = ChatTitleObservation()
        self.error = ""

    # self: Metadata-only source; one exact primary-key query cannot scan or import conversations.
    # session: Genuine hook-owned session identifier, bound as a SQL parameter.
    # cwd: Canonical expected checkout identity from that same hook.
    # Raises original filesystem/SQLite errors or TitleSourceError for rejected source metadata.
    def _read(self, session: str, cwd: str) -> ChatTitleObservation:
        database = resolved_native_path(self.home / "state_5.sqlite")
        if not database.is_relative_to(self.home):
            raise TitleSourceError("state index escapes its owner")
        # Current native semantics select name for paginated threads and title
        # for legacy threads. Neither preview nor first_user_message is read.
        # Bound values inside SQL before they can cross into Python or logging.
        selected = "CASE history_mode WHEN 'paginated' THEN name WHEN 'legacy' THEN title END"
        query = f"""SELECT
            CASE WHEN typeof(cwd) = 'text' AND length(CAST(cwd AS BLOB)) <= 16384 THEN cwd END,
            CASE WHEN history_mode IN ('legacy', 'paginated') THEN history_mode END,
            typeof({selected}), length(CAST({selected} AS BLOB)),
            CASE WHEN typeof({selected}) = 'text' AND length(CAST({selected} AS BLOB)) <= ?
                 THEN {selected} END
            FROM threads WHERE id = ? LIMIT 2"""
        with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True, timeout=0.05)) as connection:
            connection.execute("PRAGMA query_only=ON")
            rows = connection.execute(query, (TITLE_MAX_BYTES, session)).fetchall()
        if not rows:
            return ChatTitleObservation()
        if len(rows) != 1:
            raise TitleSourceError("state index has an ambiguous thread identity")
        indexed_cwd, history_mode, kind, length, title = rows[0]
        if indexed_cwd is None or checkout_identity(indexed_cwd) != cwd:
            raise TitleSourceError("state index checkout mismatch")
        if history_mode is None:
            raise TitleSourceError("thread history mode is unsupported")
        if kind == "null" or kind == "text" and length == 0:
            return ChatTitleObservation()
        if (kind != "text" or type(length) is not int or not 1 <= length <= TITLE_MAX_BYTES
                or not isinstance(title, str) or not title.strip()
                or any(not 32 <= ord(character) <= 126 for character in title)):
            raise TitleSourceError("thread title is outside the 1-255 printable ASCII contract")
        return ChatTitleObservation(title, TitleState.CURRENT, self._clock())

    # self: Existing metadata owner; a completion read must not evict the followed-chat cache.
    # session: Exact accepted hook notice identity, never inferred from a workspace or title.
    # cwd: Checkout identity from that same canonical hook-owned session.
    # Returns explicit unavailable/unsupported/error state; logs the original safe source cause.
    def read_once(self, session: str, cwd: str) -> ChatTitleObservation:
        try:
            return self._read(session, checkout_identity(cwd))
        except (OSError, ValueError, sqlite3.Error) as error:
            state = TitleState.UNSUPPORTED if isinstance(error, TitleSourceError) else TitleState.ERROR
            print(f"CCP notice title unavailable: {safe_title_error(error)}", file=sys.stderr, flush=True)
            return ChatTitleObservation(state=state)

    # self: Bounded cache; a successful index read is the only event that refreshes title age.
    # session: Exact followed hook session identifier, never inferred from a workspace or title.
    # cwd: Hook checkout identity used to reject an unrelated native row.
    # Returns explicit unavailable/unsupported/error state; failures clear previously valid text immediately.
    def poll(self, session: str, cwd: str) -> ChatTitleObservation:
        now = self._clock()
        try:
            identity = (session, checkout_identity(cwd))
            if identity != self._identity:
                self._identity = identity
                self._retry_at = 0.0
                self._observation = ChatTitleObservation()
            if now < self._retry_at:
                return self._observation
            self._retry_at = now + TITLE_REFRESH_SECONDS
            self._observation = self._read(*identity)
            self.error = ""
        except (OSError, ValueError, sqlite3.Error) as error:
            state = TitleState.UNSUPPORTED if isinstance(error, TitleSourceError) else TitleState.ERROR
            self._observation = ChatTitleObservation(state=state)
            cause = safe_title_error(error)
            if cause != self.error:
                print(f"CCP chat title unavailable: {cause}", file=sys.stderr, flush=True)
            self.error = cause
            self._retry_at = now + TITLE_REFRESH_SECONDS
        return self._observation
