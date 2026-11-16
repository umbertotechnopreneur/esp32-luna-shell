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

"""One Windows/Linux CCP owner shared by native MCP and the explicit companion bridge."""

from __future__ import annotations

import hmac
import http.client
import dataclasses
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import sys
import threading
import time
import tempfile
from typing import Any

import ccp_server as ccp
from ccp_pricing import PriceCache
from ccp_subscription_catalog import SubscriptionCatalog
from ccp_codex_hooks import decode_hook, hook_metadata, unique_hook_object
from ccp_repository import read_branch
from ccp_security import RejectionReporter, configure_runtime_logging
from ccp_presence import AppState
from ccp_source import current_source
from ccp_state_storage import RegistryStorage
import cody_system_monitor as host_monitor
from ccp_windows import (CONTROL_PORT, configure_exclusive_socket, control_key_path,
                         control_port_available, launch_runtime, runtime_directory)


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
    path = control_key_path()
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

    # self: Existing transport releasing only its own lease, preserving other open clients.
    # normal: True only when the MCP input stream reaches EOF without a transport/processing failure.
    # Raises OSError or RuntimeError if the owned lease cannot be released.
    def close(self, *, normal: bool = False) -> None:
        self._stop.set()
        # An in-flight recovery has a five-second startup and two-second lease bound.
        self._thread.join(timeout=8.0)
        if self._thread.is_alive():
            raise RuntimeError("CCP lease worker did not stop")
        exchange("release", {"id": self._lease, "normal": normal}, self._secret)


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
    if operation in {"bridge/status", "bridge/start", "bridge/stop"} and payload:
        raise ValueError("This companion control operation requires an empty object")
    if operation == "bridge/start":
        with server.lease_lock:
            server.persistent = True
        return bridge_status(server)
    if operation == "bridge/stop":
        with server.lease_lock:
            if server.service.responder.app_presence.snapshot().state == AppState.OPEN:
                raise ValueError("Close the active native MCP clients before stopping their shared CCP owner")
            server.stopping = True
        return {"status": "stopping"}
    if operation == "bridge/status":
        return bridge_status(server)
    if operation.startswith("monitor/"):
        return monitor_control(server, operation, payload)
    if operation == "animation/request":
        if set(payload) != {"scene", "op"}:
            raise ValueError("Animation requires exactly scene and op")
        with server.lease_lock:
            server.last_transport_activity = time.monotonic()
        return server.service.responder.animations.request(payload["scene"], payload["op"])
    if operation.startswith("device/"):
        return device_control(server, operation, payload)
    if operation in {"lease", "release"}:
        identity = payload.get("id")
        allowed_fields = ({"id"},) if operation == "lease" else ({"id"}, {"id", "normal"})
        if (set(payload) not in allowed_fields or not isinstance(identity, str)
                or ccp.HEX_PATTERNS["pc"].fullmatch(identity) is None
                or ("normal" in payload and type(payload["normal"]) is not bool)):
            raise ValueError("Invalid CCP transport lease identity")
        if operation == "release" and "normal" not in payload:
            # An already-loaded development client can release its old envelope
            # after a source update, but it supplied no evidence of normal EOF.
            print("CCP transport release lacks normal EOF evidence; treating it as abnormal",
                  file=sys.stderr, flush=True)
        new_transport = False
        with server.lease_lock:
            now = time.monotonic()
            if operation == "lease":
                new_transport = server.service.responder.app_presence.lease(identity, now)
            else:
                server.service.responder.app_presence.release(identity, payload.get("normal", False), now)
            # Shutdown grace is separate from source evidence. It allows the next
            # authenticated FIND to retry a lost final Closed packet.
            server.last_transport_activity = now
        server.service.responder.publish_app_presence()
        if new_transport:
            # A new MCP transport is a plugin start, including when yesterday's broker stayed alive.
            # Lease renewals and hooks never fetch; the cache durably enforces one daily attempt.
            try:
                server.pricing.start_daily_refresh()
            except (OSError, ValueError, RuntimeError) as error:
                print(f"CCP pricing plugin-start refresh failed: {error}", file=sys.stderr, flush=True)
        return {}
    if operation == "hook":
        result = server.service.observe_hook(payload)
        with server.lease_lock:
            server.last_hook = time.monotonic()
        return result
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


# No parameters: serves until shutdown or idle grace without an explicit companion lifetime.
# Raises OSError or RuntimeError for ownership, persistence or responder failures.
def run_broker() -> int:
    with ControlServer(("127.0.0.1", CONTROL_PORT), ControlHandler) as server:
        # Only the exclusive runtime owner may rotate the shared broker diagnostic files.
        configure_runtime_logging(runtime_directory())
        server.secret = control_secret(create=True)
        pricing = PriceCache(runtime_directory() / "artifacts" / "pricing" / "codex-standard.json")
        server.pricing = pricing
        subscriptions = SubscriptionCatalog()
        private_dir = runtime_directory()
        control_key = bytes.fromhex(server.secret)
        registry_storage = RegistryStorage(private_dir / "codex-registry.v1", control_key)
        question_storage = RegistryStorage(private_dir / "codex-questions.v1", control_key, purpose="questions")
        notification_storage = RegistryStorage(private_dir / "codex-notifications.v1", control_key, purpose="notifications")
        server.service = ccp.CcpService(pricing, subscriptions=subscriptions, registry_storage=registry_storage,
                                        question_storage=question_storage, notification_storage=notification_storage)
        server.lease_lock = threading.Lock()
        server.tool_lock = threading.Lock()
        server.last_hook = time.monotonic()
        server.last_transport_activity = server.last_hook
        server.persistent = False
        server.stopping = False
        server.monitor_lock = threading.RLock()
        server.monitor_service = None
        server.monitor_stopping = False
        server.monitor_device = ""
        server.monitor_rules = ()
        server.monitor_samples = []
        server.monitor_issues = []
        server.monitor_last_delivery_error = None
        server.monitor_config_error = None
        try:
            monitor_restore(server)
        except (OSError, ValueError, TypeError, KeyError) as error:
            server.monitor_config_error = str(error)
            print(f"Host monitor settings unavailable: {error}", file=sys.stderr, flush=True)
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
                # Reuse the existing one-second control loop; hook events never
                # become app-presence evidence, and no new worker is introduced.
                server.service.responder.publish_app_presence()
                with server.lease_lock:
                    latest = max(server.last_hook, server.last_transport_activity)
                    if server.stopping or (not server.persistent and time.monotonic() - latest > IDLE_GRACE_SECONDS):
                        break
        finally:
            try:
                try:
                    if server.monitor_service is not None:
                        server.monitor_service.stop()
                finally:
                    server.service.close()
            finally:
                try:
                    pricing.close()
                finally:
                    subscriptions.close()
    return 0


# server: Authenticated exclusive owner; widget presence never becomes a native MCP lease.
def bridge_status(server: ControlServer) -> dict[str, Any]:
    activity = server.service.activity
    presence = server.service.responder.app_presence
    pc_id = server.service.state.pc_id
    devices = [{**record, "simulated": record["source"] == "SIMULATED",
                "provider_source": current_source(activity, presence, pc_id,
                    record["connected"] and record["retained_pairing"] and not record["pairing_pending"])}
               for record in server.service.state.public_status()]
    return {"bridge": {"running": True, "version": ccp.SERVER_VERSION, "persistent": server.persistent},
            "source": current_source(activity, presence, pc_id), "devices": devices}


# server: Single device/pairing owner retaining original ACK and ambiguity semantics.
# operation: Fixed public local companion operation; arbitrary MCP passthrough is forbidden.
# payload: Explicit target and settings forwarded to the existing validated owner.
# Raises ValueError, RuntimeError or OSError for rejected or failed operations.
def device_control(server: ControlServer, operation: str, payload: dict[str, Any]) -> Any:
    if operation == "device/notify-system":
        return notify_system(server, payload)
    tools = {"device/discover": "ccp_discover_devices", "device/pair": "ccp_pair_device",
             "device/read-name": "ccp_read_name", "device/set-name": "ccp_set_name",
             "device/read-clock": "ccp_read_clock", "device/configure-clock": "ccp_configure_clock",
             "device/normal": "ccp_debug_screen"}
    name = tools.get(operation)
    if name is None:
        raise ValueError("Unknown companion device operation")
    arguments = dict(payload)
    if operation == "device/normal":
        if set(arguments) != {"device_id"}:
            raise ValueError("Normal display requires only the explicit device identity")
        arguments["mode"] = "normal"
    if arguments.get("include_simulator") is True:
        raise ValueError("Companion control panel supports physical devices only")
    if operation in {"device/discover", "device/pair"}:
        with server.tool_lock:
            result = ccp.call_tool(server.service, name, arguments)
    else:
        result = ccp.call_tool(server.service, name, arguments)
    if result.get("isError"):
        return {"operation": operation, "result": result.get("structuredContent"), "isError": True}
    return result.get("structuredContent", result)


# server: Authenticated local control owner with the only access to the event queue.
# payload: Exact physical device ID, allowlisted event category and optional explicit severity.
# Returns the admission identity and state only; a queued event is not display acknowledgement.
# Raises ValueError when the category or physical destination is unsupported or unavailable.
def notify_system(server: ControlServer, payload: dict[str, Any]) -> dict[str, Any]:
    if set(payload) not in ({"device_id", "event"}, {"device_id", "event", "severity"}) or not isinstance(payload["event"], str):
        raise ValueError("System notification requires device_id and an event category")
    # The OS notification source has no severity setting; its documented default is informational.
    severity = payload.get("severity", "information")
    if not isinstance(severity, str) or severity not in ccp.interactions.PC_ALERT_SEVERITIES:
        raise ValueError("System notification severity must be information, warning or critical")
    # EVENT is authenticated but its generic payload is readable on the network.
    # Fixed strings prevent caller-provided app titles, device names or message bodies leaking.
    categories = {
        "notification": ("PC notification", "A PC notification arrived"),
        "memory": ("Memory alert", "A memory rule was triggered"),
        "disk": ("Disk alert", "A disk rule was triggered"),
        "cpu": ("CPU alert", "A CPU rule was triggered"),
        "battery": ("Battery alert", "A battery rule was triggered"),
        "temperature": ("Temperature alert", "A temperature rule was triggered"),
        "usb_connected": ("USB connected", "A USB device was connected"),
        "usb_disconnected": ("USB disconnected", "A USB device was disconnected"),
        "network_disconnected": ("Network disconnected", "The PC network disconnected"),
        "network_connected": ("Network connected", "The PC network connected"),
    }
    category = categories.get(payload["event"])
    if category is None:
        raise ValueError("Unknown system notification category")
    target = next((item for item in server.service.state.public_status()
                   if item["device_id"] == payload["device_id"]), None)
    if target is None or target.get("source") != "LAN":
        raise ValueError("System notifications require the selected physical Cody on LAN")
    title, body = category
    admitted = server.service.enqueue_interaction({"device_id": payload["device_id"],
        "kind": "notice", "context": "PC", "title": title, "body": body,
        "origin_title": "Computer", "severity": severity}, False)
    return {"request_id": admitted["request_id"], "state": admitted["state"], "source": admitted["source"],
            "pc_alert_capability": server.service.responder.pc_alert_capability(payload["device_id"])}


# rows: JSON rule objects with every defined field present.
# Raises ValueError or TypeError for malformed or duplicate rules.
def monitor_parse_rules(rows: Any) -> tuple[host_monitor.AlertRule, ...]:
    if not isinstance(rows, list):
        raise ValueError("Monitor rules must be an array")
    fields = {field.name for field in dataclasses.fields(host_monitor.AlertRule)}
    rules = []
    for row in rows:
        if not isinstance(row, dict) or set(row) != fields:
            raise ValueError("Monitor rule fields are invalid")
        rules.append(host_monitor.AlertRule(**{**row, "condition": host_monitor.Condition(row["condition"]),
                                             "severity": host_monitor.Severity(row["severity"])}))
    engine = host_monitor.RuleEngine()
    engine.set_rules(rules)
    for rule in rules:
        if not (rule.metric_id.startswith(("memory.", "disk.", "cpu.", "battery.", "temperature.")) or
                rule.metric_id in {"network.interface.connected", "usb.connections.count", "usb.disconnections.count"}):
            raise ValueError("This metric cannot currently be delivered to Cody")
    return tuple(rules)


# server: Broker owner receiving the persisted configuration.
# Raises OSError or ValueError rather than replacing invalid saved settings.
def monitor_restore(server: ControlServer) -> None:
    path = runtime_directory() / "host-monitor.json"
    try:
        raw = path.read_bytes()
    except FileNotFoundError:
        return
    if len(raw) > MAX_REQUEST_BYTES:
        raise ValueError("Host monitor settings exceed the allowed size")
    document = json.loads(raw, object_pairs_hook=ccp.unique_state_object)
    if not isinstance(document, dict) or set(document) != {"version", "device_id", "rules"} or document["version"] != 1:
        raise ValueError("Invalid host monitor settings")
    device = document["device_id"]
    if not isinstance(device, str) or ccp.HEX_PATTERNS["device"].fullmatch(device) is None:
        raise ValueError("Invalid host monitor device identity")
    rules = monitor_parse_rules(document["rules"])
    # Publish only the fully validated configuration; partial restore is not usable.
    server.monitor_device = device
    server.monitor_rules = rules


# server: Broker owner serializing the explicit configuration change.
# device_id: User-selected physical Cody identity.
# rules: Validated complete rule set.
# Raises OSError or ValueError if the configuration cannot be retained atomically.
def monitor_store(server: ControlServer, device_id: str, rules: tuple[host_monitor.AlertRule, ...]) -> None:
    document = json.dumps({"version": 1, "device_id": device_id, "rules": [dataclasses.asdict(r) for r in rules]},
                          separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    if len(document) > MAX_REQUEST_BYTES:
        raise ValueError("Host monitor settings exceed the allowed size")
    path = runtime_directory() / "host-monitor.json"
    temporary = None
    try:
        with tempfile.NamedTemporaryFile("wb", dir=path.parent, prefix=".host-monitor-", delete=False) as output:
            temporary = output.name
            output.write(document)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            try:
                os.unlink(temporary)
            except OSError as error:
                print(f"Host monitor temporary settings cleanup failed: {error}", file=sys.stderr, flush=True)


# server: Broker owning the monitor, device and shared connection.
# operation: Explicit monitor configuration, lifecycle or status operation.
# payload: Strict operation-specific user settings.
# Raises OSError, ValueError or RuntimeError on invalid settings and failures.
def monitor_control(server: ControlServer, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    if operation == "monitor/configure":
        if set(payload) != {"device_id", "rules"}:
            raise ValueError("Monitor configuration requires device_id and rules")
        device = payload["device_id"]
        if not isinstance(device, str) or ccp.HEX_PATTERNS["device"].fullmatch(device) is None:
            raise ValueError("Choose an explicit physical Cody identity")
        target = next((item for item in server.service.state.public_status() if item["device_id"] == device), None)
        if target is None or target.get("source") != "LAN" or not target.get("retained_pairing"):
            raise ValueError("Select a paired physical Cody")
        rules = monitor_parse_rules(payload["rules"])
        with server.monitor_lock:
            if server.monitor_service is not None and server.monitor_device != device:
                raise ValueError("Stop monitoring before changing the selected Cody")
            monitor_store(server, device, rules)
            server.monitor_device, server.monitor_rules = device, rules
            server.monitor_config_error = None
            if server.monitor_service is not None:
                server.monitor_service.set_rules(rules)
    elif payload:
        raise ValueError("This monitor operation requires an empty object")
    if operation == "monitor/start":
        with server.monitor_lock:
            if server.monitor_config_error is not None:
                raise ValueError("Host monitor settings could not be restored; save a valid configuration first")
            if not server.monitor_device:
                raise ValueError("Configure a physical Cody and rules before starting")
            if server.monitor_stopping:
                raise RuntimeError("Host monitor is still stopping")
            if server.monitor_service is not None and not server.monitor_service.running:
                server.monitor_service = None
            if server.monitor_service is None:
                # Callback owns no retry; queue admission is not display confirmation.
                def on_alert(alert: host_monitor.AlertNotification) -> None:
                    try:
                        with server.monitor_lock:
                            device_id = server.monitor_device
                        notify_system(server, {"device_id": device_id, "event": host_monitor.cody_event_for_alert(alert),
                                               "severity": alert.severity.value})
                    except (OSError, RuntimeError, ValueError) as error:
                        with server.monitor_lock:
                            server.monitor_last_delivery_error = str(error)
                def on_issues(issues: tuple[host_monitor.AcquisitionIssue, ...]) -> None:
                    with server.monitor_lock:
                        server.monitor_issues = [dataclasses.asdict(issue) for issue in issues]
                def on_samples(samples: tuple[host_monitor.MetricSample, ...]) -> None:
                    with server.monitor_lock:
                        server.monitor_samples = [{"id": s.id, "label": s.label, "value": s.value, "unit": s.unit,
                                                   "observed_at": s.observed_at.isoformat()} for s in samples]
                service = host_monitor.MonitorService(on_alert, on_issues, on_samples=on_samples)
                service.set_rules(server.monitor_rules)
                service.start()
                server.monitor_service = service
            with server.lease_lock:
                server.persistent = True
    elif operation == "monitor/stop":
        with server.monitor_lock:
            if server.monitor_stopping:
                raise RuntimeError("Host monitor is already stopping")
            service = server.monitor_service
            server.monitor_stopping = service is not None
        if service is not None:
            try:
                service.stop()
            finally:
                with server.monitor_lock:
                    server.monitor_stopping = False
                    if not service.running:
                        server.monitor_service = None
                        server.monitor_samples = []
    elif operation not in {"monitor/status", "monitor/configure"}:
        raise ValueError("Unknown host monitor operation")
    with server.monitor_lock:
        return {"running": server.monitor_service is not None and server.monitor_service.running, "device_id": server.monitor_device,
                "rules": [dataclasses.asdict(r) for r in server.monitor_rules], "samples": server.monitor_samples,
                "issues": server.monitor_issues, "last_delivery_error": server.monitor_last_delivery_error,
                "config_error": server.monitor_config_error,
                "pc_alert_capability": server.service.responder.pc_alert_capability(server.monitor_device)}


# operation: Fixed documented local operation chosen by the control-panel user.
# Reads at most one bounded JSON object from stdin; secrets never appear in argv.
# Returns one JSON envelope and a nonzero exit code for every rejected/failed operation.
def run_control(operation: str) -> int:
    try:
        raw = sys.stdin.buffer.read(MAX_REQUEST_BYTES + 1)
        if len(raw) > MAX_REQUEST_BYTES:
            raise ValueError("Companion control input exceeds its bound")
        payload = json.loads(raw or b"{}", object_pairs_hook=ccp.unique_state_object)
        if not isinstance(payload, dict):
            raise ValueError("Companion control input must be an object")
        allowed = {"bridge/status", "bridge/start", "bridge/stop",
                   "device/discover", "device/pair", "device/read-name", "device/set-name",
                   "device/read-clock", "device/configure-clock", "device/normal", "device/notify-system",
                   "monitor/status", "monitor/configure", "monitor/start", "monitor/stop"}
        if operation not in allowed:
            raise ValueError("Unknown companion control operation")
        if operation == "bridge/status" and control_port_available():
            if payload:
                raise ValueError("Read-only companion status requires an empty object")
            result = {
                "bridge": {"running": False, "version": ccp.SERVER_VERSION, "persistent": False},
                "source": {"provider": "local", "identity": "-", "state": "unavailable", "age_ms": None,
                           "label": "Cody", "detail": "Bridge stopped"}, "devices": []}
        else:
            secret = ensure_runtime() if operation == "bridge/start" else control_secret()
            result = exchange(operation, payload, secret, timeout=20.0)
        sys.stdout.write(json.dumps({"result": result}, separators=(",", ":")) + "\n")
        return 0
    except (OSError, RuntimeError, ValueError, KeyError, TypeError) as failure:
        sys.stdout.write(json.dumps({"error": {"code": type(failure).__name__, "message": str(failure)}}, separators=(",", ":")) + "\n")
        return 1


QUESTION_ROUTING_CONTEXT = (
    "A paired physical Cody currently supports questions. When you need a semantic user choice, "
    "use the cody-codex skill and ccp_ask_question for the intended paired LAN device instead of "
    "opening the same clarification only on the PC. Verify the target with ccp_connection_status; "
    "do not guess among multiple devices or include a simulator without explicit user intent. "
    "Send the actual self-contained question and two or three complete English ASCII choices: "
    "preserve the original choice wording, stable IDs and meaning; labels allow 63 characters "
    "and details 255. If it does not fit or needs free text, keep it on the PC and explain why. "
    "Keep the returned capability private. Poll ccp_poll_request (at most 15 seconds per call); "
    "after received, read answer and answer_id, acknowledge that exact answer_id, and use the "
    "choice only after delivered. Pending, expiry, cancellation and errors are not answers. "
    "Never replay an ambiguously admitted question or duplicate it on both surfaces. Native "
    "permissions and approvals remain on the PC. Existing native request_user_input prompts "
    "cannot be imported or answered through this hook; do not inspect transcripts or fabricate "
    "native responses. Follow an explicit user preference for PC-only questions."
)


# No parameters: discards private contents and emits only supported static question-routing context.
# Raises ValueError, OSError or RuntimeError for malformed input or failed event delivery.
def run_hook() -> int:
    emitted_ns = time.monotonic_ns()
    raw = sys.stdin.buffer.read(16 * 1024 * 1024 + 1)
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError("CCP hook input exceeds its bound")
    event = hook_metadata(json.loads(raw, object_pairs_hook=unique_hook_object))
    event["emitted_ns"] = emitted_ns
    # Reject malformed field values before any filesystem access or IPC.
    native = decode_hook(event, emitted_ns)
    event["branch"] = read_branch(native.cwd)
    # Only lifecycle entry points can start the owner. Interrupt must finish
    # within Codex's three-second limit, even when the runtime is unavailable.
    secret = (ensure_runtime() if event["hook_event_name"] in {"SessionStart", "UserPromptSubmit"}
              else control_secret())
    result = exchange("hook", event, secret, timeout=1.0)
    output: dict[str, Any] = {}
    if (event["hook_event_name"] == "UserPromptSubmit" and isinstance(result, dict)
            and result.get("physical_question_routing") is True):
        output["hookSpecificOutput"] = {"hookEventName": "UserPromptSubmit",
                                       "additionalContext": QUESTION_ROUTING_CONTEXT}
    sys.stdout.write(json.dumps(output, separators=(",", ":")) + "\n")
    return 0
