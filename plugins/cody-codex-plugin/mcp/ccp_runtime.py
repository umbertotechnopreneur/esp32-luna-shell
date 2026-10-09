"""One Windows CCP owner shared by MCP transports and trusted, metadata-only hooks."""

from __future__ import annotations

import hmac
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import threading
import time
from typing import Any

import ccp_server as ccp
from ccp_activity import EVENTS


CONTROL_PORT = 42119
MAX_REQUEST_BYTES = 65536
IDLE_GRACE_SECONDS = 60.0


# No parameters: uses the established per-user CCP storage owner.
# Raises RuntimeError when Windows local application storage is unavailable.
def runtime_directory() -> Path:
    location = os.environ.get("LOCALAPPDATA")
    if not location:
        raise RuntimeError("CCP requires LOCALAPPDATA for protected runtime state")
    return Path(location) / "CodyCodexPlugin"


# create: Only the process owning the exclusive control listener may create a missing key.
# Raises OSError or RuntimeError for every read, protection or atomic-write failure.
def control_secret(create: bool = False) -> str:
    path = runtime_directory() / "control-key.dpapi"
    try:
        protected = path.read_text(encoding="ascii")
    except FileNotFoundError:
        if not create:
            raise
        print("CCP control key absent: creating first-use per-user credential", file=sys.stderr, flush=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        protected = ccp.protect_secret(secrets.token_bytes(32), "CCP local control")
        temporary = path.with_suffix(".tmp")
        temporary.write_text(protected, encoding="ascii")
        os.replace(temporary, path)
    return ccp.unprotect_secret(protected).hex()


# operation: Broker operation name, never a URL or arbitrary command.
# payload: Bounded request body, stripped of hook contents before this boundary.
# secret: Per-user DPAPI-unwrapped local control credential.
# timeout: Maximum seconds allowed for the local HTTP exchange.
# Raises OSError, RuntimeError or ValueError for transport, authentication or operation errors.
def exchange(operation: str, payload: dict[str, Any], secret: str, timeout: float = 2.0) -> Any:
    body = json.dumps({"version": ccp.SERVER_VERSION, "operation": operation, "payload": payload},
                      separators=(",", ":")).encode("utf-8")
    if len(body) > MAX_REQUEST_BYTES:
        raise ValueError("CCP local request exceeds the bounded control envelope")
    connection = http.client.HTTPConnection("127.0.0.1", CONTROL_PORT, timeout=timeout)
    try:
        connection.request("POST", "/ccp", body, {
            "Authorization": "Bearer " + secret, "Content-Type": "application/json",
        })
        response = connection.getresponse()
        raw = response.read(MAX_REQUEST_BYTES + 1)
        if len(raw) > MAX_REQUEST_BYTES:
            raise RuntimeError("CCP local response exceeds its bound")
        result = json.loads(raw)
        if response.status != 200:
            raise RuntimeError(result.get("error", "CCP runtime rejected the request"))
        return result["result"]
    except http.client.HTTPException as error:
        raise RuntimeError(f"CCP local HTTP exchange failed: {error}") from error
    finally:
        connection.close()


# No parameters: starts a hidden owner only if the loopback control port is unoccupied.
# Raises OSError or RuntimeError for identity, launch, conflicting-version or startup failure.
def ensure_runtime() -> str:
    try:
        with socket.create_connection(("127.0.0.1", CONTROL_PORT), timeout=0.25):
            running = True
    except ConnectionRefusedError:
        running = False
    if not running:
        log_directory = runtime_directory() / "artifacts" / "logs"
        log_directory.mkdir(parents=True, exist_ok=True)
        with (log_directory / "runtime.log").open("ab", buffering=0) as output:
            subprocess.Popen(
                [sys.executable, "-B", str(Path(__file__).with_name("ccp_server.py")), "--broker"],
                stdin=subprocess.DEVNULL, stdout=output, stderr=output,
                creationflags=subprocess.CREATE_NO_WINDOW | subprocess.DETACHED_PROCESS,
                close_fds=True,
            )
    deadline = time.monotonic() + 5.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            secret = control_secret()
            exchange("health", {}, secret)
            return secret
        except (ConnectionRefusedError, FileNotFoundError, TimeoutError) as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(f"CCP startup did not complete; inspect artifacts/logs/runtime.log: {last_error}")


class BrokerClient:
    # No parameters: one MCP transport renews its lease independently of tool calls.
    # Raises OSError or RuntimeError when the shared owner cannot start or authenticate.
    def __init__(self) -> None:
        self._secret = ensure_runtime()
        self._lease = secrets.token_hex(16)
        self._stop = threading.Event()
        self._error: Exception | None = None
        exchange("lease", {"id": self._lease}, self._secret)
        self._thread = threading.Thread(target=self._renew, name="CCP-MCP-lease", daemon=True)
        self._thread.start()

    # No parameters: renews owner lifetime without fabricating chat activity.
    def _renew(self) -> None:
        while not self._stop.wait(10.0):
            try:
                exchange("lease", {"id": self._lease}, self._secret)
            except (OSError, RuntimeError, ValueError) as error:
                self._error = error
                print(f"CCP lease failed: {error}", file=sys.stderr, flush=True)
                return

    # operation: Requested local operation; MCP clients only use the mcp endpoint.
    # payload: Original MCP request, which the broker validates at its tool boundary.
    # Raises the original lease failure or current transport/operation error.
    def request(self, operation: str, payload: dict[str, Any]) -> Any:
        if self._error is not None:
            raise self._error
        return exchange(operation, payload, self._secret, timeout=20.0)

    # No parameters: releases only this transport's lease, preserving other open chats.
    # Raises OSError or RuntimeError if the owned lease cannot be released.
    def close(self) -> None:
        self._stop.set()
        self._thread.join(timeout=3.0)
        if self._thread.is_alive():
            raise RuntimeError("CCP lease worker did not stop")
        exchange("release", {"id": self._lease}, self._secret)


class ControlServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    # No parameters: acquires sole PC ownership before loading or creating any state.
    # Raises OSError when another process owns the loopback port.
    def server_bind(self) -> None:
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        super().server_bind()


class ControlHandler(BaseHTTPRequestHandler):
    # No parameters: applies a bounded read deadline to each local connection.
    def setup(self) -> None:
        super().setup()
        self.connection.settimeout(3.0)

    # format: HTTP access-log format, intentionally unused to avoid credential/body logging.
    # args: HTTP access-log values, intentionally unused.
    def log_message(self, format: str, *args: Any) -> None:
        return

    # No parameters: authenticates before decoding any request body or mutating service state.
    def do_POST(self) -> None:
        try:
            if self.path != "/ccp" or not hmac.compare_digest(
                self.headers.get("Authorization", ""), "Bearer " + self.server.secret
            ):
                raise ValueError("CCP local control authentication failed")
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_REQUEST_BYTES or self.headers.get("Transfer-Encoding"):
                raise ValueError("CCP local request length is invalid")
            request = json.loads(self.rfile.read(length), object_pairs_hook=ccp.unique_state_object)
            if not isinstance(request, dict) or set(request) != {"version", "operation", "payload"}:
                raise ValueError("CCP local envelope is malformed")
            if request["version"] != ccp.SERVER_VERSION:
                raise ValueError("CCP runtime version differs; close old CCP clients before updating")
            result = dispatch(self.server, request["operation"], request["payload"])
            status, response = 200, {"result": result}
        except (OSError, RuntimeError, ValueError, KeyError, TypeError) as error:
            print(f"CCP control operation failed: {error}", file=sys.stderr, flush=True)
            status, response = 400, {"error": str(error)}
        encoded = json.dumps(response, separators=(",", ":")).encode("utf-8")
        try:
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)
        except OSError as error:
            print(f"CCP control response failed: {error}", file=sys.stderr, flush=True)


# server: Exclusive local owner of service, active client leases and shutdown grace.
# operation: Fixed local operation requested by an authenticated same-user client.
# payload: Strictly bounded JSON object.
# Raises ValueError, OSError or RuntimeError for unsupported or failed operations.
def dispatch(server: ControlServer, operation: str, payload: dict[str, Any]) -> Any:
    if not isinstance(payload, dict):
        raise ValueError("CCP operation payload must be an object")
    server.service.responder.require_running()
    if operation == "health":
        return {"version": ccp.SERVER_VERSION}
    if operation in {"lease", "release"}:
        identity = payload.get("id")
        if set(payload) != {"id"} or not isinstance(identity, str) or ccp.HEX_PATTERNS["pc"].fullmatch(identity) is None:
            raise ValueError("Invalid CCP transport lease identity")
        with server.lease_lock:
            if operation == "lease":
                now = time.monotonic()
                server.leases = {key: observed for key, observed in server.leases.items()
                                 if now - observed <= IDLE_GRACE_SECONDS}
                if identity not in server.leases and len(server.leases) >= 64:
                    raise ValueError("CCP active transport limit reached")
                server.leases[identity] = time.monotonic()
            else:
                server.leases.pop(identity, None)
        return {}
    if operation == "hook":
        server.service.activity.observe(payload)
        with server.lease_lock:
            server.last_hook = time.monotonic()
        return {}
    if operation == "mcp":
        # Pair/discovery cache operations are serialized; UDP liveness and hooks
        # remain independent while a pairing exchange waits for its device.
        with server.tool_lock:
            return ccp.handle_request(payload, server.service)
    raise ValueError("Unknown CCP local operation")


# No parameters: serves until no MCP lease or hook remains within the shutdown grace.
# Raises OSError or RuntimeError for ownership, persistence or responder failures.
def run_broker() -> int:
    with ControlServer(("127.0.0.1", CONTROL_PORT), ControlHandler) as server:
        server.secret = control_secret(create=True)
        server.service = ccp.CcpService()
        server.leases = {}
        server.lease_lock = threading.Lock()
        server.tool_lock = threading.Lock()
        server.last_hook = time.monotonic()
        server.timeout = 1.0
        try:
            while True:
                server.service.responder.require_running()
                server.handle_request()
                with server.lease_lock:
                    latest = max([server.last_hook, *server.leases.values()])
                    if time.monotonic() - latest > IDLE_GRACE_SECONDS:
                        break
        finally:
            server.service.close()
    return 0


# No parameters: discards prompts, responses, transcript paths and tool arguments at ingestion.
# Raises ValueError, OSError or RuntimeError for malformed input or failed event delivery.
def run_hook() -> int:
    emitted_ns = time.monotonic_ns()
    raw = sys.stdin.buffer.read(16 * 1024 * 1024 + 1)
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError("CCP hook input exceeds its bound")
    original = json.loads(raw)
    if not isinstance(original, dict) or original.get("hook_event_name") not in EVENTS:
        raise ValueError("CCP hook event is not supported")
    allowed = ("hook_event_name", "session_id", "cwd", "turn_id", "tool_name", "tool_use_id")
    event = {key: original[key] for key in allowed if key in original}
    event["emitted_ns"] = emitted_ns
    # Only lifecycle entry points can start the owner. Interrupt must finish
    # within Codex's three-second limit, even when the runtime is unavailable.
    secret = (ensure_runtime() if event["hook_event_name"] in {"SessionStart", "UserPromptSubmit"}
              else control_secret())
    exchange("hook", event, secret, timeout=1.0)
    sys.stdout.write("{}\n")
    return 0
