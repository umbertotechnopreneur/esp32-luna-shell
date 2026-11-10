"""Atomic authenticated checkpoint storage for the exclusive Cody broker."""

from __future__ import annotations

import hmac
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
from typing import Any


_MAGIC = b"CODY-REGISTRY/1\n"
_DOMAIN = b"Cody broker registry checkpoint v1\0"
_MAX_DOCUMENT_BYTES = 1024 * 1024
_MAX_FILE_BYTES = len(_MAGIC) + 65 + _MAX_DOCUMENT_BYTES


class RegistryStorageError(RuntimeError):
    """A checkpoint could not be read, authenticated, decoded or committed."""


# fields: Uncollapsed JSON object members from one authenticated checkpoint.
# Raises ValueError if a field is ambiguous. Source text is never in diagnostics.
def _unique(fields: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in fields:
        if key in result:
            raise ValueError("Duplicate registry checkpoint field")
        result[key] = value
    return result


# value: An unsupported non-finite JSON number literal.
# Raises ValueError instead of converting it to a host-specific float.
def _non_finite(value: str) -> None:
    raise ValueError("Registry checkpoint contains a non-finite number")


class RegistryStorage:
    """A single-owner private file authenticated with the protected control key."""

    # path: Exact checkpoint path inside the private broker runtime directory.
    # control_key: Existing 32-byte local control credential, already unprotected by its owner.
    # Raises ValueError for an invalid credential.
    def __init__(self, path: Path, control_key: bytes) -> None:
        if len(control_key) != 32:
            raise ValueError("Registry storage requires the protected local control credential")
        self._path = path
        self._key = hmac.digest(control_key, _DOMAIN, "sha256")

    # No parameters: reads the exact owned file into the bounded input buffer.
    # Raises OSError on open/read failure or RegistryStorageError on unsafe state or size overflow.
    def _read(self) -> bytes:
        metadata = self._path.lstat()
        if not stat.S_ISREG(metadata.st_mode):
            raise RegistryStorageError("Registry checkpoint is not a regular owned file")
        if os.name == "posix" and (metadata.st_uid != os.getuid() or metadata.st_mode & 0o077):
            raise RegistryStorageError("Registry checkpoint must be private to the broker user")
        with self._path.open("rb") as source:
            data = source.read(_MAX_FILE_BYTES + 1)
        if len(data) > _MAX_FILE_BYTES:
            raise RegistryStorageError("Registry checkpoint exceeds its file bound")
        return data

    # No parameters: restores one complete authenticated generation, or reports actual first use.
    # Raises RegistryStorageError for bad integrity, malformed encoding or unavailable storage.
    def load(self) -> dict[str, Any] | None:
        try:
            raw = self._read()
        except FileNotFoundError:
            # A missing parent is not a genuinely absent checkpoint.
            if self._path.parent.is_dir():
                return None
            raise RegistryStorageError("Registry checkpoint directory is unavailable") from None
        except OSError as error:
            raise RegistryStorageError(f"Registry checkpoint read failed: {error.strerror}") from error

        proof_start = len(_MAGIC)
        proof_end = proof_start + 64
        if (not raw.startswith(_MAGIC) or raw[proof_end:proof_end + 1] != b"\n"
                or len(raw) <= proof_end + 1):
            raise RegistryStorageError("Registry checkpoint header is invalid")
        body = raw[proof_end + 1:]
        expected = hmac.digest(self._key, _DOMAIN + body, "sha256").hex().encode("ascii")
        if not hmac.compare_digest(raw[proof_start:proof_end], expected):
            raise RegistryStorageError("Registry checkpoint authentication failed")
        try:
            document = json.loads(body.decode("ascii"), object_pairs_hook=_unique,
                                  parse_constant=_non_finite)
        except (ValueError, RecursionError) as error:
            raise RegistryStorageError("Registry checkpoint JSON is invalid") from error
        if not isinstance(document, dict):
            raise RegistryStorageError("Registry checkpoint must be an object")
        return document

    # document: Complete validated registry generation, bound to the retained PC identity.
    # Raises RegistryStorageError for size, write, flush, replacement or readback failure.
    def save(self, document: dict[str, Any]) -> None:
        body = json.dumps(document, ensure_ascii=True, allow_nan=False, separators=(",", ":")).encode("ascii")
        if len(body) > _MAX_DOCUMENT_BYTES:
            raise RegistryStorageError("Registry checkpoint exceeds its document bound")
        proof = hmac.digest(self._key, _DOMAIN + body, "sha256").hex().encode("ascii")
        raw = _MAGIC + proof + b"\n" + body
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="wb", prefix=self._path.name + ".", suffix=".tmp", dir=self._path.parent, delete=False,
            ) as output:
                temporary = Path(output.name)
                output.write(raw)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, self._path)
            temporary = None
            if os.name == "posix":
                directory = os.open(self._path.parent, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(directory)
                finally:
                    os.close(directory)
            if not hmac.compare_digest(self._read(), raw):
                raise RegistryStorageError("Registry checkpoint readback did not match the committed generation")
        except (OSError, RegistryStorageError) as error:
            if temporary is not None:
                try:
                    temporary.unlink(missing_ok=True)
                except OSError as cleanup_error:
                    print(f"Registry checkpoint staging cleanup failed: {cleanup_error.strerror}",
                          file=sys.stderr, flush=True)
            if isinstance(error, RegistryStorageError):
                raise
            raise RegistryStorageError(f"Registry checkpoint commit failed: {error.strerror}") from error
