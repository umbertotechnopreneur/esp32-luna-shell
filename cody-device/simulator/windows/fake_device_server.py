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

"""Loopback-only synthetic HTTP device backed by the scenario engine.

This module intentionally contains no hardware, serial, GUI, or firmware
integration.  The public API mirrors the firmware transport while the separate
``/__simulator`` surface controls deterministic simulator state.
"""

from __future__ import annotations

import argparse
import copy
import hmac
import ipaddress
import json
import socket
import sys
import threading
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Callable, Mapping
from urllib.parse import urlsplit

from device_model import (
    ScenarioEngine,
    ScenarioValidationError,
    SemanticActionError,
    UnknownScenarioError,
    scenario_names,
)


DEFAULT_BIND_ADDRESS = "127.0.0.1"
DEFAULT_PORT = 18080
SYNTHETIC_API_KEY = "LUNASHELL-SIMULATOR-ONLY-NOT-A-SECRET"
SYNTHETIC_HEADER = "X-LUNASHELL-Synthetic"
AUTH_FAILURE_LIMIT = 20
AUTH_FAILURE_WINDOW_SECONDS = 60.0
MAX_CONTROL_BODY_BYTES = 64 * 1024

API_PATHS = frozenset(
    {
        "/api/v1/health",
        "/api/v1/capabilities",
        "/api/v1/info",
        "/api/v1/diagnostics",
        "/api/v1/rtc",
        "/api/v1/gps",
        "/api/v1/status",
    }
)
PUBLIC_API_PATHS = frozenset({"/api/v1/health"})
CONTROL_GET_PATHS = frozenset({"/__simulator/list", "/__simulator/state"})
CONTROL_POST_PATHS = frozenset(
    {
        "/__simulator/load",
        "/__simulator/advance",
        "/__simulator/action",
    }
)


class BindAddressError(ValueError):
    """Raised when a caller tries to expose the synthetic server remotely."""


class RequestBodyError(ValueError):
    """Raised when a simulator control request has an invalid JSON body."""


class AuthFailureWindow:
    """Firmware-compatible twenty-failures-per-sixty-seconds limiter."""

    def __init__(
        self,
        *,
        clock: Callable[[], float] = time.monotonic,
        limit: int = AUTH_FAILURE_LIMIT,
        window_seconds: float = AUTH_FAILURE_WINDOW_SECONDS,
    ) -> None:
        if limit <= 0:
            raise ValueError("authentication failure limit must be positive")
        if window_seconds <= 0:
            raise ValueError("authentication failure window must be positive")
        self._clock = clock
        self._limit = limit
        self._window_seconds = window_seconds
        self._started_at: float | None = None
        self._failures = 0
        self._lock = threading.Lock()

    def _reset_if_expired(self, now: float) -> None:
        if (
            self._started_at is None
            or now - self._started_at >= self._window_seconds
        ):
            self._started_at = now
            self._failures = 0

    def is_limited(self) -> bool:
        with self._lock:
            self._reset_if_expired(self._clock())
            return self._failures >= self._limit

    def record_failure(self) -> int:
        with self._lock:
            self._reset_if_expired(self._clock())
            self._failures += 1
            return self._failures

    def failure_count(self) -> int:
        with self._lock:
            self._reset_if_expired(self._clock())
            return self._failures


@dataclass
class HttpMetrics:
    """Transport-owned counters exposed by the status payload."""

    request_count: int = 0
    authorized_count: int = 0
    unauthorized_count: int = 0
    response_failure_count: int = 0
    start_count: int = 1
    stop_count: int = 0
    start_failure_count: int = 0
    stop_failure_count: int = 0
    event_lifecycle_failure_count: int = 0
    last_lifecycle_error: int = 0


class SimulatorServerState:
    """Synchronize one mutable scenario engine across HTTP worker threads."""

    def __init__(self, scenario: str, *, start_at_ms: int = 0) -> None:
        self._lock = threading.RLock()
        self._engine = ScenarioEngine.from_scenario(
            scenario,
            start_at_ms=start_at_ms,
        )
        self.auth_failures = AuthFailureWindow()
        self.metrics = HttpMetrics()
        self._auth_decision_lock = threading.Lock()

    @staticmethod
    def _scenario_name(engine: ScenarioEngine) -> str:
        scenario = engine.scenario
        if isinstance(scenario, Mapping):
            return str(scenario.get("name", "unknown"))
        return str(getattr(scenario, "name", "unknown"))

    def api_payload(self, path: str) -> dict[str, Any]:
        with self._lock:
            payload = copy.deepcopy(self._engine.api_payload(path))
            if path == "/api/v1/health":
                data = payload.get("data")
                if isinstance(data, dict):
                    data.update(
                        {
                            "server_running": True,
                            "server_cleanup_required": False,
                            "server_handle_retained": True,
                            "server_accepting_requests": True,
                            "auth_required_for_data": True,
                        }
                    )
            elif path == "/api/v1/status":
                data = payload.get("data")
                if isinstance(data, dict):
                    data.update(
                        {
                            "server_running": True,
                            "server_cleanup_required": False,
                            "server_handle_retained": True,
                            "server_accepting_requests": True,
                        }
                    )
                    data["metrics"] = self.metrics_payload()
            return payload

    def metrics_payload(self) -> dict[str, int]:
        with self._lock:
            metrics = self.metrics
            return {
                "request_count": metrics.request_count,
                "authorized_count": metrics.authorized_count,
                "unauthorized_count": metrics.unauthorized_count,
                "response_failure_count": metrics.response_failure_count,
                "start_count": metrics.start_count,
                "stop_count": metrics.stop_count,
                "start_failure_count": metrics.start_failure_count,
                "stop_failure_count": metrics.stop_failure_count,
                "event_lifecycle_failure_count": (
                    metrics.event_lifecycle_failure_count
                ),
                "last_lifecycle_error": metrics.last_lifecycle_error,
                "auth_failures_in_window": (
                    self.auth_failures.failure_count()
                ),
            }

    def record_request(self) -> None:
        with self._lock:
            self.metrics.request_count += 1

    def record_authorized(self) -> None:
        with self._lock:
            self.metrics.authorized_count += 1

    def record_unauthorized(self) -> None:
        with self._lock:
            self.metrics.unauthorized_count += 1

    def record_response_failure(self) -> None:
        with self._lock:
            self.metrics.response_failure_count += 1

    def authorize_request(self, credential_matches: bool) -> str:
        """Make the rate-limit/auth decision atomically across worker threads."""

        with self._auth_decision_lock:
            if self.auth_failures.is_limited():
                return "limited"
            if not credential_matches:
                self.auth_failures.record_failure()
                self.record_unauthorized()
                return "unauthorized"
            self.record_authorized()
            return "authorized"

    def list_payload(self) -> dict[str, Any]:
        with self._lock:
            return {
                "scenarios": list(scenario_names()),
                "active_scenario": self._scenario_name(self._engine),
            }

    def state_payload(self) -> dict[str, Any]:
        with self._lock:
            return {
                "generation": copy.deepcopy(
                    self._engine.generation.to_dict()
                )
            }

    def load(self, scenario: str, *, start_at_ms: int = 0) -> dict[str, Any]:
        known = set(scenario_names())
        if scenario not in known:
            raise UnknownScenarioError(
                f"unknown built-in scenario {scenario!r}"
            )
        replacement = ScenarioEngine.from_scenario(
            scenario,
            start_at_ms=start_at_ms,
        )
        with self._lock:
            self._engine = replacement
            return self.state_payload()

    def advance(self, milliseconds: int) -> dict[str, Any]:
        with self._lock:
            self._engine.advance_ms(milliseconds)
            return self.state_payload()

    def action(
        self,
        action: str,
        params: Mapping[str, Any],
    ) -> dict[str, Any]:
        with self._lock:
            self._engine.action(action, **dict(params))
            return self.state_payload()


def _control_envelope(data: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "schema": "LUNASHELL.simulator.control.v1",
        "synthetic": True,
        "data": dict(data),
    }


class SyntheticDeviceRequestHandler(BaseHTTPRequestHandler):
    """Serve the firmware-compatible API and local simulator control plane."""

    protocol_version = "HTTP/1.1"
    server_version = "LUNASHELLSyntheticDevice/1"
    sys_version = ""

    @property
    def simulator_state(self) -> SimulatorServerState:
        return self.server.simulator_state  # type: ignore[attr-defined]

    def log_message(self, format_string: str, *args: Any) -> None:
        sys.stderr.write(
            "%s - - [%s] %s\n"
            % (self.client_address[0], self.log_date_time_string(), format_string % args)
        )

    def _send_json(
        self,
        status: int,
        payload: Mapping[str, Any],
        *,
        headers: Mapping[str, str] | None = None,
    ) -> None:
        body = json.dumps(
            payload,
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
        try:
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header(SYNTHETIC_HEADER, "true")
            self.send_header("Content-Length", str(len(body)))
            if headers is not None:
                for name, value in headers.items():
                    self.send_header(name, value)
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            self.simulator_state.record_response_failure()

    def _send_error_json(
        self,
        status: int,
        message: str,
        *,
        headers: Mapping[str, str] | None = None,
    ) -> None:
        self._send_json(status, {"error": message}, headers=headers)

    def _read_json_body(self) -> dict[str, Any]:
        raw_length = self.headers.get("Content-Length")
        if raw_length is None:
            raise RequestBodyError("Content-Length is required")
        try:
            length = int(raw_length, 10)
        except ValueError as error:
            raise RequestBodyError("invalid Content-Length") from error
        if length < 0:
            raise RequestBodyError("invalid Content-Length")
        if length > MAX_CONTROL_BODY_BYTES:
            raise RequestBodyError(
                f"request body exceeds {MAX_CONTROL_BODY_BYTES} bytes"
            )
        raw_body = self.rfile.read(length)
        try:
            decoded = raw_body.decode("utf-8")
            value = json.loads(decoded)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise RequestBodyError("request body must be valid UTF-8 JSON") from error
        if not isinstance(value, dict):
            raise RequestBodyError("request body must be a JSON object")
        return value

    def _authorized(self) -> bool:
        expected = f"Bearer {SYNTHETIC_API_KEY}"
        actual = self.headers.get("Authorization", "")
        return hmac.compare_digest(actual, expected)

    def _handle_api_get(self, path: str) -> None:
        state = self.simulator_state
        state.record_request()
        if path not in PUBLIC_API_PATHS:
            auth_decision = state.authorize_request(self._authorized())
            if auth_decision == "limited":
                self._send_error_json(
                    429,
                    "too many authentication failures",
                    headers={"Retry-After": "60"},
                )
                return
            if auth_decision == "unauthorized":
                self._send_error_json(
                    401,
                    "authentication required",
                    headers={"WWW-Authenticate": "Bearer"},
                )
                return
        try:
            payload = state.api_payload(path)
        except (ScenarioValidationError, SemanticActionError, ValueError) as error:
            self._send_error_json(500, str(error))
            return
        self._send_json(200, payload)

    def _handle_control_get(self, path: str) -> None:
        if path == "/__simulator/list":
            payload = self.simulator_state.list_payload()
        else:
            payload = self.simulator_state.state_payload()
        self._send_json(200, _control_envelope(payload))

    @staticmethod
    def _required_nonempty_string(
        body: Mapping[str, Any],
        field: str,
    ) -> str:
        value = body.get(field)
        if not isinstance(value, str) or not value.strip():
            raise RequestBodyError(f"{field} must be a non-empty string")
        return value

    @staticmethod
    def _nonnegative_integer(
        body: Mapping[str, Any],
        field: str,
        *,
        default: int | None = None,
    ) -> int:
        value = body.get(field, default)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise RequestBodyError(f"{field} must be a non-negative integer")
        return value

    def _handle_control_post(self, path: str) -> None:
        try:
            body = self._read_json_body()
            if path == "/__simulator/load":
                scenario = self._required_nonempty_string(body, "scenario")
                start_at_ms = self._nonnegative_integer(
                    body,
                    "start_at_ms",
                    default=0,
                )
                result = self.simulator_state.load(
                    scenario,
                    start_at_ms=start_at_ms,
                )
            elif path == "/__simulator/advance":
                milliseconds = self._nonnegative_integer(
                    body,
                    "milliseconds",
                )
                result = self.simulator_state.advance(milliseconds)
            else:
                action = self._required_nonempty_string(body, "action")
                params = body.get("params", {})
                if not isinstance(params, dict):
                    raise RequestBodyError("params must be a JSON object")
                result = self.simulator_state.action(action, params)
        except RequestBodyError as error:
            status = 413 if "exceeds" in str(error) else 400
            self._send_error_json(status, str(error))
            return
        except UnknownScenarioError as error:
            self._send_error_json(404, str(error))
            return
        except ScenarioValidationError as error:
            self._send_error_json(422, str(error))
            return
        except SemanticActionError as error:
            self._send_error_json(409, str(error))
            return
        except ValueError as error:
            self._send_error_json(400, str(error))
            return
        self._send_json(200, _control_envelope(result))

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        path = urlsplit(self.path).path
        if path in API_PATHS:
            self._handle_api_get(path)
        elif path in CONTROL_GET_PATHS:
            self._handle_control_get(path)
        elif path in CONTROL_POST_PATHS:
            self._send_error_json(405, "method not allowed", headers={"Allow": "POST"})
        else:
            self._send_error_json(404, "route not found")

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        path = urlsplit(self.path).path
        if path in CONTROL_POST_PATHS:
            self._handle_control_post(path)
        elif path in API_PATHS or path in CONTROL_GET_PATHS:
            self._send_error_json(405, "method not allowed", headers={"Allow": "GET"})
        else:
            self._send_error_json(404, "route not found")

    def do_HEAD(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._send_error_json(405, "method not allowed", headers={"Allow": "GET"})

    def do_PUT(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._send_error_json(405, "method not allowed")

    def do_PATCH(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._send_error_json(405, "method not allowed")

    def do_DELETE(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._send_error_json(405, "method not allowed")


class SyntheticDeviceHttpServer(ThreadingHTTPServer):
    """Threaded HTTP server carrying one simulator state object."""

    allow_reuse_address = True
    daemon_threads = True

    def __init__(
        self,
        server_address: tuple[str, int],
        simulator_state: SimulatorServerState,
    ) -> None:
        resolved_address = resolve_loopback_bind(server_address[0])
        self.simulator_state = simulator_state
        super().__init__(
            (resolved_address, server_address[1]),
            SyntheticDeviceRequestHandler,
        )


class SyntheticDeviceHttpServerV6(SyntheticDeviceHttpServer):
    """IPv6 loopback variant."""

    address_family = socket.AF_INET6


def resolve_loopback_bind(value: str) -> str:
    """Resolve a bind argument while rejecting every non-loopback address."""

    candidate = value.strip()
    if not candidate:
        raise BindAddressError("bind address must not be empty")
    if candidate.lower() == "localhost":
        return DEFAULT_BIND_ADDRESS
    try:
        address = ipaddress.ip_address(candidate)
    except ValueError as error:
        raise BindAddressError(
            "bind address must be a loopback IP literal or localhost"
        ) from error
    if not address.is_loopback:
        raise BindAddressError(
            f"refusing non-loopback bind address {candidate!r}"
        )
    return address.compressed


def create_server(
    *,
    bind_address: str = DEFAULT_BIND_ADDRESS,
    port: int = DEFAULT_PORT,
    scenario: str = "healthy",
    start_at_ms: int = 0,
) -> SyntheticDeviceHttpServer:
    """Create, but do not start, a loopback-only synthetic device server."""

    resolved_address = resolve_loopback_bind(bind_address)
    if isinstance(port, bool) or not isinstance(port, int) or not 0 <= port <= 65535:
        raise ValueError("port must be an integer from 0 through 65535")
    if isinstance(start_at_ms, bool) or not isinstance(start_at_ms, int) or start_at_ms < 0:
        raise ValueError("start_at_ms must be a non-negative integer")
    state = SimulatorServerState(scenario, start_at_ms=start_at_ms)
    address = ipaddress.ip_address(resolved_address)
    server_class: type[SyntheticDeviceHttpServer]
    server_class = (
        SyntheticDeviceHttpServerV6
        if address.version == 6
        else SyntheticDeviceHttpServer
    )
    return server_class((resolved_address, port), state)


def _nonnegative_cli_integer(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def _port(value: str) -> int:
    parsed = _nonnegative_cli_integer(value)
    if parsed == 0 or parsed > 65535:
        raise argparse.ArgumentTypeError("must be from 1 through 65535")
    return parsed


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Serve one deterministic LUNASHELL synthetic device on loopback."
        )
    )
    parser.add_argument(
        "--bind",
        default=DEFAULT_BIND_ADDRESS,
        help="loopback bind address (default: 127.0.0.1; remote binds rejected)",
    )
    parser.add_argument(
        "--port",
        type=_port,
        default=DEFAULT_PORT,
        help=f"listen port (default: {DEFAULT_PORT})",
    )
    parser.add_argument(
        "--scenario",
        default="healthy",
        help="built-in scenario name or local scenario JSON path",
    )
    parser.add_argument(
        "--start-at-ms",
        type=_nonnegative_cli_integer,
        default=0,
        help="initial virtual-clock offset in milliseconds",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    try:
        server = create_server(
            bind_address=args.bind,
            port=args.port,
            scenario=args.scenario,
            start_at_ms=args.start_at_ms,
        )
    except (BindAddressError, ScenarioValidationError, UnknownScenarioError, ValueError) as error:
        parser.error(str(error))

    bound_host, bound_port = server.server_address[:2]
    display_host = f"[{bound_host}]" if ":" in bound_host else bound_host
    print(
        f"LUNASHELL synthetic device: http://{display_host}:{bound_port}",
        flush=True,
    )
    print(f"Synthetic API key: {SYNTHETIC_API_KEY}", flush=True)
    print("Press Ctrl+C to stop.", flush=True)
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        print("Stopping synthetic device.", flush=True)
    finally:
        server.simulator_state.metrics.stop_count += 1
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
