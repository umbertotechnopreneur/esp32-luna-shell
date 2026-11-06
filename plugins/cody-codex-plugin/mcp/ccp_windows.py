# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Windows DPAPI and Linux Secret Service boundary; no plaintext substitute."""
from __future__ import annotations

import base64
import errno
import ctypes
from ctypes import wintypes
from functools import lru_cache
import os
from pathlib import Path
import re
import secrets
import shutil
from typing import Any
import socket
import subprocess
import sys

CONTROL_PORT = 42119


# listener: Socket whose port must have one exclusive runtime owner.
# Raises OSError when the operating system cannot apply exclusive ownership.
def configure_exclusive_socket(listener: socket.socket) -> None:
    if os.name == "nt":
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    else:
        # Linux exclusive bind is the default; never enable SO_REUSEPORT or SO_REUSEADDR.
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 0)


# No parameters: uses the established per-user CCP storage owner.
# Raises RuntimeError when supported per-user storage is unavailable or unsafe.
def runtime_directory() -> Path:
    if os.name == "nt":
        location = os.environ.get("LOCALAPPDATA")
        if not location:
            raise RuntimeError("CCP requires LOCALAPPDATA for protected runtime state")
        return Path(location) / "CodyCodexPlugin"
    if not sys.platform.startswith("linux"):
        raise RuntimeError("CCP supports Windows and Linux")
    base = Path(os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local" / "state")))
    if not base.is_absolute():
        raise RuntimeError("CCP XDG_STATE_HOME must be absolute")
    directory = base / "cody-companion"
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    metadata = directory.stat()
    if directory.is_symlink() or metadata.st_uid != os.getuid() or metadata.st_mode & 0o077:
        raise RuntimeError("CCP Linux state directory must be owned by this user with mode 0700")
    return directory


# arguments: Fixed libsecret CLI operation and non-secret item attributes.
# secret: Optional secret bytes passed through stdin only, never arguments or logs.
# Raises RuntimeError for absent, locked, unavailable or failed Secret Service access.
def secret_service(arguments: list[str], secret: bytes | None = None) -> bytes:
    executable = shutil.which("secret-tool")
    if executable is None:
        raise RuntimeError("CCP requires libsecret secret-tool and an unlocked Secret Service on Linux")
    try:
        result = subprocess.run([executable, *arguments], input=secret, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=8.0, check=False)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("CCP Secret Service timed out; unlock the user keyring explicitly") from error
    if result.returncode != 0:
        raise RuntimeError(f"CCP Secret Service operation failed with status {result.returncode}")
    if len(result.stdout) > 4096:
        raise RuntimeError("CCP Secret Service returned an oversized value")
    return result.stdout


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


# value: Secret bytes protected by the current user's native credential service.
# description: Non-secret label shown only by the credential manager.
# Raises RuntimeError when the service cannot protect or release the value.
def protect_secret(value: bytes, description: str) -> str:
    if len(value) != 32:
        raise RuntimeError("CCP credentials must contain exactly 32 bytes")
    if os.name != "nt":
        if not sys.platform.startswith("linux"):
            raise RuntimeError("CCP credential protection supports Windows and Linux")
        identity = secrets.token_hex(16)
        secret_service(["store", "--label=" + description, "application", "cody-companion",
                        "credential", identity], base64.b64encode(value))
        return "secret-service:v1:" + identity
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


# encoded: Existing DPAPI blob or Secret Service item reference for this user.
# A bounded process-only cache avoids spawning a keyring lookup on every UDP heartbeat.
# Raises RuntimeError when the credential is malformed, inaccessible or cannot be released.
@lru_cache(maxsize=260)
def unprotect_secret(encoded: str) -> bytes:
    if os.name != "nt":
        if not sys.platform.startswith("linux") or re.fullmatch(r"secret-service:v1:[0-9a-f]{32}", encoded) is None:
            raise RuntimeError("CCP credential does not identify a supported Linux Secret Service item")
        payload = secret_service(["lookup", "application", "cody-companion", "credential", encoded.rsplit(":", 1)[1]])
        # Accept the CLI's optional one-line terminator; all other bytes remain strictly validated.
        if payload.endswith(b"\n"):
            payload = payload[:-1]
        try:
            value = base64.b64decode(payload, validate=True)
        except ValueError as error:
            raise RuntimeError("CCP Secret Service item is malformed") from error
        if len(value) != 32:
            raise RuntimeError("CCP Secret Service item has an invalid length")
        return value
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
    log_directory = runtime_directory() / "artifacts" / "logs"
    log_directory.mkdir(parents=True, exist_ok=True)
    with (log_directory / "runtime.log").open("ab", buffering=0) as output:
        options: dict[str, Any] = {}
        if os.name == "nt":
            options["creationflags"] = subprocess.CREATE_NO_WINDOW | subprocess.DETACHED_PROCESS
        elif not sys.platform.startswith("linux"):
            raise RuntimeError("CCP runtime supports Windows and Linux")
        # The widget's existing guardian retains process ownership on both platforms.
        subprocess.Popen(
            [sys.executable, "-B", str(server_script), "--broker"],
            stdin=subprocess.DEVNULL, stdout=output, stderr=output,
            close_fds=True, **options,
        )
