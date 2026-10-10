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

"""One Windows CCP owner shared by MCP transports and trusted, metadata-only hooks."""

from __future__ import annotations

import hmac
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import sys
import threading
import time
from typing import Any

import ccp_server as ccp
from ccp_pricing import PriceCache
from ccp_activity import EVENTS
from ccp_repository import read_branch
from ccp_security import RejectionReporter, configure_runtime_logging
from ccp_windows import (CONTROL_PORT, configure_exclusive_socket, control_port_available,
                         launch_runtime, runtime_directory)


MAX_REQUEST_BYTES = 65536
IDLE_GRACE_SECONDS = 60.0
LEASE_RENEW_SECONDS = 10.0
LEASE_RETRY_SECONDS = (1.0, 3.0, 10.0, 30.0)
MAX_CONTROL_CONNECTIONS = 32


class BrokerTransportError(RuntimeError):
    """The HTTP transport failed before a valid broker response was available."""


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
        if not isinstance(result, dict):
            raise RuntimeError("CCP local response must be an object")
        if response.status != 200:
            raise RuntimeError(result.get("error", "CCP runtime rejected the request"))
        if set(result) != {"result"}:
            raise RuntimeError("CCP local response envelope is invalid")
        return result["result"]
    except http.client.HTTPException as error:
        raise BrokerTransportError(f"CCP local HTTP exchange failed: {error}") from error
    finally:
        connection.close()


# No parameters: starts a hidden owner only if the loopback control port is unoccupied.
# Raises OSError or RuntimeError for identity, launch, conflicting-version or startup failure.
def ensure_runtime() -> str:
    if os.name != "nt":
        raise RuntimeError("CCP requires Windows for its protected local runtime")
    if control_port_available():
        # The child re-acquires the exclusive bind before reading any state. If
        # another launcher wins this race, both clients authenticate that owner.
        launch_runtime(Path(__file__).with_name("ccp_server.py"))
    deadline = time.monotonic() + 5.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            secret = control_secret()
            remaining = max(0.01, deadline - time.monotonic())
            exchange("health", {}, secret, timeout=min(2.0, remaining))
            return secret
        except (ConnectionRefusedError, FileNotFoundError, TimeoutError) as error:
            last_error = error
            time.sleep(0.05)
    raise BrokerTransportError(
        f"CCP startup did not complete; inspect artifacts/logs/runtime.log: {last_error}") from last_error


class BrokerClient:
    # self: One MCP transport renewing its lease independently of tool calls.
    # Raises OSError or RuntimeError when the shared owner cannot start or authenticate.
    def __init__(self) -> None:
        self._secret = ensure_runtime()
        self._lease = secrets.token_hex(16)
        self._stop = threading.Event()
        self._state_lock = threading.Lock()
        self._error: Exception | None = None
        exchange("lease", {"id": self._lease}, self._secret)
        self._thread = threading.Thread(target=self._renew, name="CCP-MCP-lease", daemon=True)
        self._thread.start()

    # self: Existing MCP lease whose control credential and identity must remain unchanged.
    # Raises the original transport error, or an authentication/version/storage error.
    def _renew_once(self) -> None:
        with self._state_lock:
            recovering = self._error is not None
        try:
            if recovering:
                # A broker restart can be recovered; a replaced/missing credential
                # cannot. Read the existing credential before admitting a launch.
                retained_secret = control_secret()
                if not hmac.compare_digest(retained_secret, self._secret):
                    raise RuntimeError("CCP control identity changed; explicit plugin restart required")
                recovered_secret = ensure_runtime()
                if not hmac.compare_digest(recovered_secret, self._secret):
                    raise RuntimeError("CCP recovered owner has a different control identity")
            if self._stop.is_set():
                return
            exchange("lease", {"id": self._lease}, self._secret)
        except (OSError, RuntimeError, ValueError) as error:
            with self._state_lock:
                first_failure = self._error is None
                self._error = error
            if first_failure:
                print(f"CCP lease failed: {error}", file=sys.stderr, flush=True)
            raise
        with self._state_lock:
            self._error = None
        if recovering:
            print("CCP authenticated lease recovered; retained pairing unchanged", file=sys.stderr, flush=True)

    # self: One renewable MCP lease, independent of genuine Codex activity.
    # Transport retries have bounded waits; identity/authentication failures remain terminal.
    def _renew(self) -> None:
        failures = 0
        delay = LEASE_RENEW_SECONDS
        while not self._stop.wait(delay):
            try:
                self._renew_once()
                failures = 0
                delay = LEASE_RENEW_SECONDS
            except (OSError, BrokerTransportError):
                # Cap each attempt and its cadence. No request or hook is replayed.
                delay = LEASE_RETRY_SECONDS[min(failures, len(LEASE_RETRY_SECONDS) - 1)]
                failures += 1
            except (RuntimeError, ValueError) as error:
                print(f"CCP lease recovery requires explicit action: {error}", file=sys.stderr, flush=True)
                return

    # self: Existing transport retaining its own lease identity and current renewal error.
    # operation: Requested local operation; MCP clients only use the mcp endpoint.
    # payload: Original MCP request, which the broker validates at its tool boundary.
    # Raises the original lease failure or current transport/operation error.
    def request(self, operation: str, payload: dict[str, Any]) -> Any:
        with self._state_lock:
            error = self._error
        if error is not None:
            raise error
        return exchange(operation, payload, self._secret, timeout=20.0)

    # self: Existing transport releasing only its own lease, preserving other open chats.
    # Raises OSError or RuntimeError if the owned lease cannot be released.
    def close(self) -> None:
        self._stop.set()
        # An in-flight recovery has a five-second startup and two-second lease bound.
        self._thread.join(timeout=8.0)
        if self._thread.is_alive():
            raise RuntimeError("CCP lease worker did not stop")
        exchange("release", {"id": self._lease}, self._secret)


class ControlServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    # self: Exclusive control listener with bounded worker and rejection accounting.
    # server_address: Fixed loopback endpoint chosen by the broker.
    # handler_class: Local authenticated control handler, never supplied by a request.
    # Raises OSError when exclusive socket ownership cannot be acquired.
    def __init__(self, server_address: tuple[str, int], handler_class: type[BaseHTTPRequestHandler]) -> None:
        self._connection_slots = threading.BoundedSemaphore(MAX_CONTROL_CONNECTIONS)
        self._connection_rejections = RejectionReporter()
        super().__init__(server_address, handler_class)

    # self: Single-threaded control listener reserving worker capacity.
    # request: Accepted loopback socket; bounded before any worker is created.
    # client_address: Socket-observed local client endpoint.
    # Raises the original worker-start failure after releasing its reservation.
    def process_request(self, request: Any, client_address: tuple[str, int]) -> None:
        if not self._connection_slots.acquire(blocking=False):
            try:
                self._connection_rejections.reject("control ingress", "active connection budget exhausted")
            finally:
                self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self._connection_slots.release()
            raise

    # self: Control worker releasing exactly the reservation acquired by its listener.
    # request: Accepted socket owned by this connection's reserved worker.
    # client_address: Socket-observed local client endpoint.
    def process_request_thread(self, request: Any, client_address: tuple[str, int]) -> None:
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._connection_slots.release()

    # self: Control listener acquiring sole PC ownership before loading or creating any state.
    # Raises OSError when another process owns the loopback port.
    def server_bind(self) -> None:
        configure_exclusive_socket(self.socket)
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
        new_transport = False
        with server.lease_lock:
            if operation == "lease":
                now = time.monotonic()
                server.leases = {key: observed for key, observed in server.leases.items()
                                 if now - observed <= IDLE_GRACE_SECONDS}
                if identity not in server.leases and len(server.leases) >= 64:
                    raise ValueError("CCP active transport limit reached")
                new_transport = identity not in server.leases
                server.leases[identity] = time.monotonic()
            else:
                server.leases.pop(identity, None)
        if new_transport:
            # A new MCP transport is a plugin start, including when yesterday's broker stayed alive.
            # Lease renewals and hooks never fetch; the cache durably enforces one daily attempt.
            try:
                server.pricing.start_daily_refresh()
            except (OSError, ValueError, RuntimeError) as error:
                print(f"CCP pricing plugin-start refresh failed: {error}", file=sys.stderr, flush=True)
        return {}
    if operation == "hook":
        server.service.activity.observe(payload)
        notification_error = None
        try:
            server.service.notify_native_permission(payload)
            if payload.get("hook_event_name") == "PermissionRequest":
                server.service.notifications_error = None
        except (OSError, RuntimeError, ValueError) as error:
            # Delivery of the On your PC notice is optional. Preserve and
            # publish its real failure while leaving native permission ownership
            # with Codex; this hook must never grant or deny an operation.
            notification_error = str(error)
            server.service.notifications_error = notification_error
            print(f"CCP native notice failed: {error}", file=sys.stderr, flush=True)
        with server.lease_lock:
            server.last_hook = time.monotonic()
        return {"notification_error": notification_error} if notification_error else {}
    if operation == "mcp":
        # A bounded interaction poll waits on its own condition. Holding the
        # pairing/cache lock here would block its recipient acknowledgement.
        params = payload.get("params")
        if (payload.get("method") == "tools/call" and isinstance(params, dict)
                and params.get("name") in {"ccp_ask_question", "ccp_publish_notification",
                                           "ccp_poll_request", "ccp_cancel_request", "ccp_reset_device", "ccp_factory_reset_device",
                                           "ccp_debug_screen", "ccp_read_clock", "ccp_configure_clock", "ccp_report_progress",
                                           "ccp_read_name", "ccp_set_name"}):
            return ccp.handle_request(payload, server.service)
        # Pair/discovery cache operations are serialized; UDP liveness and hooks
        # remain independent while a pairing exchange waits for its device.
        with server.tool_lock:
            return ccp.handle_request(payload, server.service)
    raise ValueError("Unknown CCP local operation")


# No parameters: serves until no MCP lease or hook remains within the shutdown grace.
# Raises OSError or RuntimeError for ownership, persistence or responder failures.
def run_broker() -> int:
    if os.name != "nt":
        raise RuntimeError("CCP requires Windows for its protected local runtime")
    with ControlServer(("127.0.0.1", CONTROL_PORT), ControlHandler) as server:
        # Only the exclusive runtime owner may rotate the shared broker diagnostic files.
        configure_runtime_logging(runtime_directory())
        server.secret = control_secret(create=True)
        pricing = PriceCache(runtime_directory() / "artifacts" / "pricing" / "codex-standard.json")
        server.pricing = pricing
        server.service = ccp.CcpService(pricing)
        server.leases = {}
        server.lease_lock = threading.Lock()
        server.tool_lock = threading.Lock()
        server.last_hook = time.monotonic()
        server.timeout = 1.0
        try:
            # Pricing is optional metadata. Its real error remains in PriceCache and the log;
            # a catalog failure must not prevent hooks, native approvals or pairing delivery.
            try:
                pricing.start_daily_refresh()
            except (OSError, ValueError, RuntimeError) as error:
                print(f"CCP pricing startup failed: {error}", file=sys.stderr, flush=True)
            while True:
                server.service.responder.require_running()
                server.handle_request()
                with server.lease_lock:
                    latest = max([server.last_hook, *server.leases.values()])
                    if time.monotonic() - latest > IDLE_GRACE_SECONDS:
                        break
        finally:
            try:
                server.service.close()
            finally:
                pricing.close()
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
    allowed = ("hook_event_name", "session_id", "cwd", "turn_id", "tool_name", "tool_use_id", "model", "trigger")
    event = {key: original[key] for key in allowed if key in original}
    event["emitted_ns"] = emitted_ns
    # Only checkout metadata is read; tool arguments and all chat contents remain discarded.
    if isinstance(event.get("cwd"), str) and 0 < len(event["cwd"]) <= 4096:
        event["branch"] = read_branch(event["cwd"])
    # Only lifecycle entry points can start the owner. Interrupt must finish
    # within Codex's three-second limit, even when the runtime is unavailable.
    secret = (ensure_runtime() if event["hook_event_name"] in {"SessionStart", "UserPromptSubmit"}
              else control_secret())
    exchange("hook", event, secret, timeout=1.0)
    sys.stdout.write("{}\n")
    return 0
