"""Bounded ingress accounting and diagnostics for the local CCP owner."""

from __future__ import annotations

from dataclasses import dataclass
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import sys
import time


MAX_SOURCE_BUCKETS = 64
GLOBAL_PACKET_RATE = 64.0
GLOBAL_PACKET_BURST = 128.0
SOURCE_PACKET_RATE = 16.0
SOURCE_PACKET_BURST = 32.0
REJECTION_REPORT_SECONDS = 5.0
RUNTIME_LOG_BYTES = 2 * 1024 * 1024
RUNTIME_LOG_BACKUPS = 2


@dataclass
class PacketBucket:
    tokens: float
    observed_at: float


class DatagramAdmission:
    # self: One serialized UDP owner's fixed-capacity ingress accountant.
    def __init__(self) -> None:
        now = time.monotonic()
        self._global = PacketBucket(GLOBAL_PACKET_BURST, now)
        self._sources: dict[str, PacketBucket] = {}

    # bucket: Mutable token owner, confined to the responder thread.
    # now: Current monotonic time; never obtained from a packet.
    # rate: Maximum sustained token replenishment per second.
    # burst: Maximum retained tokens after an idle period.
    @staticmethod
    def _consume(bucket: PacketBucket, now: float, rate: float, burst: float) -> bool:
        bucket.tokens = min(burst, bucket.tokens + max(0.0, now - bucket.observed_at) * rate)
        bucket.observed_at = now
        if bucket.tokens < 1.0:
            return False
        bucket.tokens -= 1.0
        return True

    # self: Serialized responder's ingress owner.
    # address: Socket-observed source IP; no packet-provided identity grants admission.
    # Returns false before parsing, DPAPI or HMAC work when either budget is exhausted.
    def admit(self, address: str) -> bool:
        now = time.monotonic()
        # Charge the global budget first so spoofed addresses cannot create unbounded work.
        if not self._consume(self._global, now, GLOBAL_PACKET_RATE, GLOBAL_PACKET_BURST):
            return False
        bucket = self._sources.get(address)
        if bucket is None:
            if len(self._sources) >= MAX_SOURCE_BUCKETS:
                oldest = min(self._sources, key=lambda source: self._sources[source].observed_at)
                del self._sources[oldest]
            bucket = PacketBucket(SOURCE_PACKET_BURST, now)
            self._sources[address] = bucket
        return self._consume(bucket, now, SOURCE_PACKET_RATE, SOURCE_PACKET_BURST)


class RejectionReporter:
    # self: Single-owner diagnostic counter; rejected packet bodies and secrets are never logged.
    def __init__(self) -> None:
        self._next_report_at = 0.0
        self._suppressed = 0

    # self: Single-threaded rejection counter for one ingress owner.
    # operation: Static non-secret operation name.
    # reason: Validated diagnostic reason, never packet contents or credentials.
    def reject(self, operation: str, reason: str) -> None:
        now = time.monotonic()
        if now < self._next_report_at:
            self._suppressed += 1
            return
        print(f"CCP {operation} rejected: {reason}; additional_rejections={self._suppressed}",
              file=sys.stderr, flush=True)
        self._suppressed = 0
        self._next_report_at = now + REJECTION_REPORT_SECONDS


class RuntimeLogHandler(RotatingFileHandler):
    # self: Exclusive runtime log owner; failed writes remain real failures.
    # record: Diagnostic whose bounded file write failed; its contents are not repeated.
    # Raises the original logging exception rather than recursively writing into this log stream.
    def handleError(self, record: logging.LogRecord) -> None:
        failure = sys.exc_info()[1]
        if failure is None:
            raise RuntimeError("CCP bounded log failed without an exception")
        raise failure


class RuntimeLogStream:
    # self: Print-compatible stream borrowing the synchronized file owner.
    # handler: Bounded rotating log owner; stdout and stderr use its own synchronization.
    def __init__(self, handler: RuntimeLogHandler) -> None:
        self._handler = handler

    # self: Print-compatible stream using the file owner's synchronization.
    # text: One output fragment from existing non-secret diagnostics.
    # Returns the consumed character count expected by print.
    # Raises the original log ownership or write failure.
    def write(self, text: str) -> int:
        message = text.rstrip("\r\n")
        if message:
            record = logging.LogRecord("CCP", logging.INFO, "", 0, message, (), None)
            self._handler.handle(record)
        return len(text)

    # self: Print-compatible stream borrowing the file owner's flush operation.
    # Raises the original flush failure.
    def flush(self) -> None:
        self._handler.flush()


# directory: Existing per-user runtime directory, never chosen from a packet.
# Raises OSError when the bounded log owner cannot create or open its files.
def configure_runtime_logging(directory: Path) -> None:
    path = directory / "artifacts" / "logs" / "broker.log"
    path.parent.mkdir(parents=True, exist_ok=True)
    handler = RuntimeLogHandler(path, maxBytes=RUNTIME_LOG_BYTES,
                                backupCount=RUNTIME_LOG_BACKUPS, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(message)s"))
    sys.stdout = RuntimeLogStream(handler)
    sys.stderr = RuntimeLogStream(handler)
