"""Windows credential and per-user storage boundary; no plaintext substitute."""
from __future__ import annotations

import base64
import errno
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
from typing import Any
import socket
import subprocess
import sys

CONTROL_PORT = 42119


# listener: Windows socket whose port must have one exclusive runtime owner.
# Raises OSError when Winsock cannot apply exclusive ownership.
def configure_exclusive_socket(listener: socket.socket) -> None:
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)


# No parameters: uses the established per-user CCP storage owner.
# Raises RuntimeError when Windows local application storage is unavailable.
def runtime_directory() -> Path:
    location = os.environ.get("LOCALAPPDATA")
    if not location:
        raise RuntimeError("CCP requires LOCALAPPDATA for protected runtime state")
    return Path(location) / "CodyCodexPlugin"


class DataBlob(ctypes.Structure):
    _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_ubyte))]


# No parameters: returns Windows cryptography libraries with exact pointer signatures.
# Raises RuntimeError when called outside Windows.
def windows_cryptography_libraries() -> tuple[Any, Any]:
    if os.name != "nt":
        raise RuntimeError("CCP local link-key persistence requires Windows DPAPI")
    crypt32 = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    crypt32.CryptProtectData.argtypes = [
        ctypes.POINTER(DataBlob),
        wintypes.LPCWSTR,
        ctypes.POINTER(DataBlob),
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(DataBlob),
    ]
    crypt32.CryptProtectData.restype = wintypes.BOOL
    crypt32.CryptUnprotectData.argtypes = [
        ctypes.POINTER(DataBlob),
        ctypes.POINTER(wintypes.LPWSTR),
        ctypes.POINTER(DataBlob),
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(DataBlob),
    ]
    crypt32.CryptUnprotectData.restype = wintypes.BOOL
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p
    return crypt32, kernel32


# kernel32: Loaded Windows kernel library with the LocalFree signature configured.
# pointer: DPAPI-owned pointer returned through a DATA_BLOB or description field.
# label: Non-secret diagnostic name for the owned allocation.
# Raises RuntimeError when Windows cannot release the allocation.
def release_windows_buffer(kernel32: Any, pointer: Any, label: str) -> None:
    if pointer and kernel32.LocalFree(ctypes.cast(pointer, ctypes.c_void_p)):
        raise RuntimeError(f"Windows DPAPI {label} release failed: {ctypes.get_last_error()}")


# value: Secret bytes protected for the current Windows user.
# description: Non-secret label shown only by Windows credential diagnostics.
# Raises RuntimeError when DPAPI cannot protect or release the value.
def protect_secret(value: bytes, description: str) -> str:
    source_buffer = ctypes.create_string_buffer(value)
    source = DataBlob(len(value), ctypes.cast(source_buffer, ctypes.POINTER(ctypes.c_ubyte)))
    protected = DataBlob()
    crypt32, kernel32 = windows_cryptography_libraries()
    if not crypt32.CryptProtectData(
        ctypes.byref(source), description, None, None, None, 0x1, ctypes.byref(protected)
    ):
        raise RuntimeError(f"Windows DPAPI protection failed: {ctypes.get_last_error()}")
    try:
        payload = ctypes.string_at(protected.pbData, protected.cbData)
        return base64.b64encode(payload).decode("ascii")
    finally:
        release_windows_buffer(kernel32, protected.pbData, "protected buffer")


# encoded: Base64 DPAPI blob previously created for the current Windows user.
# Raises RuntimeError when the blob is malformed, belongs to another user, or cannot be released.
def unprotect_secret(encoded: str) -> bytes:
    try:
        payload = base64.b64decode(encoded, validate=True)
    except (ValueError, TypeError) as error:
        raise RuntimeError("CCP protected link key is not valid base64") from error
    source_buffer = ctypes.create_string_buffer(payload)
    source = DataBlob(len(payload), ctypes.cast(source_buffer, ctypes.POINTER(ctypes.c_ubyte)))
    clear = DataBlob()
    description = wintypes.LPWSTR()
    crypt32, kernel32 = windows_cryptography_libraries()
    if not crypt32.CryptUnprotectData(
        ctypes.byref(source), ctypes.byref(description), None, None, None, 0x1, ctypes.byref(clear)
    ):
        raise RuntimeError(f"Windows DPAPI unprotect failed: {ctypes.get_last_error()}")
    try:
        value = ctypes.string_at(clear.pbData, clear.cbData)
        if len(value) != 32:
            raise RuntimeError("CCP protected link key has an invalid length")
        return value
    finally:
        release_error: RuntimeError | None = None
        try:
            release_windows_buffer(kernel32, description, "description")
        except RuntimeError as error:
            release_error = error
        try:
            release_windows_buffer(kernel32, clear.pbData, "clear buffer")
        except RuntimeError as error:
            if release_error is None:
                release_error = error
        if release_error is not None:
            raise release_error


# No parameters: asks Winsock for exclusive ownership without connecting to a closed port.
# Raises OSError for access denial or any failure other than an occupied address.
def control_port_available() -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
        configure_exclusive_socket(reservation)
        try:
            reservation.bind(("127.0.0.1", CONTROL_PORT))
        except OSError as error:
            if error.errno == errno.EADDRINUSE or getattr(error, "winerror", None) == 10048:
                return False
            raise
    return True


# server_script: Canonical installed broker entrypoint, never user command text.
# Raises OSError or RuntimeError for unsupported platform, log ownership or launch failure.
def launch_runtime(server_script: Path) -> None:
    if os.name != "nt":
        raise RuntimeError("CCP requires Windows for its hidden protected runtime")
    log_directory = runtime_directory() / "artifacts" / "logs"
    log_directory.mkdir(parents=True, exist_ok=True)
    with (log_directory / "runtime.log").open("ab", buffering=0) as output:
        subprocess.Popen(
            [sys.executable, "-B", str(server_script), "--broker"],
            stdin=subprocess.DEVNULL, stdout=output, stderr=output,
            creationflags=subprocess.CREATE_NO_WINDOW | subprocess.DETACHED_PROCESS,
            close_fds=True,
        )
