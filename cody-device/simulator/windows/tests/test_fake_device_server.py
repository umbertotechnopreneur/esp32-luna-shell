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

"""Focused loopback tests for the synthetic HTTP device transport."""

from __future__ import annotations

import http.client
import json
from pathlib import Path
import sys
import threading
import unittest


WINDOWS_SIMULATOR_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WINDOWS_SIMULATOR_DIR))

from fake_device_server import (  # noqa: E402
    AuthFailureWindow,
    BindAddressError,
    SYNTHETIC_API_KEY,
    create_server,
    resolve_loopback_bind,
)


class SyntheticDeviceServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.server = create_server(port=0, scenario="healthy")
        self.thread = threading.Thread(
            target=self.server.serve_forever,
            kwargs={"poll_interval": 0.01},
            daemon=True,
        )
        self.thread.start()
        self.host = str(self.server.server_address[0])
        self.port = int(self.server.server_address[1])

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2.0)
        self.assertFalse(self.thread.is_alive())

    def request(
        self,
        method: str,
        path: str,
        *,
        body: dict[str, object] | None = None,
        authorization: str | None = None,
    ) -> tuple[int, dict[str, str], dict[str, object]]:
        connection = http.client.HTTPConnection(self.host, self.port, timeout=2.0)
        headers: dict[str, str] = {}
        encoded_body: bytes | None = None
        if body is not None:
            encoded_body = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
            headers["Content-Length"] = str(len(encoded_body))
        if authorization is not None:
            headers["Authorization"] = authorization
        connection.request(method, path, body=encoded_body, headers=headers)
        response = connection.getresponse()
        response_body = json.loads(response.read().decode("utf-8"))
        response_headers = {name.lower(): value for name, value in response.getheaders()}
        status = response.status
        connection.close()
        return status, response_headers, response_body

    def test_health_is_public_and_data_routes_require_exact_bearer(self) -> None:
        status, headers, payload = self.request("GET", "/api/v1/health")
        self.assertEqual(status, 200)
        self.assertEqual(headers["x-lunashell-synthetic"], "true")
        self.assertEqual(headers["cache-control"], "no-store")
        self.assertEqual(payload["schema"], "LUNASHELL.device.health.v1")

        status, headers, payload = self.request("GET", "/api/v1/info")
        self.assertEqual(status, 401)
        self.assertEqual(headers["www-authenticate"], "Bearer")
        self.assertEqual(payload, {"error": "authentication required"})

        status, _, _ = self.request(
            "GET",
            "/api/v1/info",
            authorization=f"bearer {SYNTHETIC_API_KEY}",
        )
        self.assertEqual(status, 401)

        status, headers, payload = self.request(
            "GET",
            "/api/v1/info",
            authorization=f"Bearer {SYNTHETIC_API_KEY}",
        )
        self.assertEqual(status, 200)
        self.assertEqual(headers["x-lunashell-synthetic"], "true")
        self.assertEqual(payload["schema"], "LUNASHELL.device.info.v1")

    def test_twenty_failures_block_even_valid_auth_but_not_health(self) -> None:
        for _ in range(20):
            status, _, _ = self.request("GET", "/api/v1/status")
            self.assertEqual(status, 401)

        status, headers, payload = self.request(
            "GET",
            "/api/v1/status",
            authorization=f"Bearer {SYNTHETIC_API_KEY}",
        )
        self.assertEqual(status, 429)
        self.assertEqual(headers["retry-after"], "60")
        self.assertEqual(
            payload,
            {"error": "too many authentication failures"},
        )

        status, _, payload = self.request("GET", "/api/v1/health")
        self.assertEqual(status, 200)
        self.assertEqual(payload["schema"], "LUNASHELL.device.health.v1")

    def test_every_documented_data_route_is_authenticated_and_synthetic(self) -> None:
        expected_schemas = {
            "/api/v1/capabilities": "LUNASHELL.device.capabilities.v1",
            "/api/v1/info": "LUNASHELL.device.info.v1",
            "/api/v1/diagnostics": "LUNASHELL.device.diagnostics.v1",
            "/api/v1/rtc": "LUNASHELL.device.rtc.v1",
            "/api/v1/gps": "LUNASHELL.device.gps.v1",
            "/api/v1/status": "LUNASHELL.device.status.v1",
        }
        payloads: dict[str, dict[str, object]] = {}
        for path, expected_schema in expected_schemas.items():
            with self.subTest(path=path):
                status, headers, payload = self.request(
                    "GET",
                    path,
                    authorization=f"Bearer {SYNTHETIC_API_KEY}",
                )
                self.assertEqual(status, 200)
                self.assertEqual(headers["x-lunashell-synthetic"], "true")
                self.assertEqual(headers["cache-control"], "no-store")
                self.assertEqual(payload["schema"], expected_schema)
                self.assertTrue(payload["simulator"]["synthetic"])
                payloads[path] = payload

        public_info = payloads["/api/v1/info"]["data"]
        diagnostics = payloads["/api/v1/diagnostics"]["data"]
        for field in ("ssid", "ip_address", "mac_address"):
            self.assertNotIn(field, public_info["radios"]["wifi"])
            self.assertIn(field, diagnostics["radios"]["wifi"])
        self.assertNotIn("first_sentence", public_info["gps"])
        self.assertIn("first_sentence", diagnostics["gps"])

    # self: Authenticated loopback responses produced by the shared firmware serializer.
    # Raises AssertionError if protocol envelopes, redaction or unavailable host facts lose their contract.
    def test_payload_shapes_match_the_firmware_serializer(self) -> None:
        for path in ("/api/v1/info", "/api/v1/diagnostics", "/api/v1/rtc", "/api/v1/gps", "/api/v1/status"):
            status, _, payload = self.request("GET", path, authorization=f"Bearer {SYNTHETIC_API_KEY}")
            self.assertEqual(status, 200)
            self.assertTrue(payload["simulator"]["synthetic"])
            self.assertEqual(payload["simulator"]["runtime"], "production-cpp")
            for field in ("sequence", "last_update_attempt_at_us", "last_update_complete", "last_update_error", "age_ms", "data"):
                self.assertIn(field, payload)
        status, _, health = self.request("GET", "/api/v1/health")
        self.assertEqual(status, 200)
        self.assertFalse(health["data"]["liveness"]["available"])
        self.assertEqual(health["data"]["liveness"]["query_error"], "ESP_ERR_NOT_SUPPORTED")

    def test_overall_health_uses_only_the_firmware_required_components(self) -> None:
        status, _, baseline = self.request(
            "GET",
            "/api/v1/status",
            authorization=f"Bearer {SYNTHETIC_API_KEY}",
        )
        self.assertEqual(status, 200)
        self.assertEqual(baseline["data"]["overall"], "unknown")
        expected_overall = baseline["data"]["overall"]

        for scenario in ("tof-one-missing", "memory-pressure"):
            with self.subTest(scenario=scenario):
                status, _, _ = self.request(
                    "POST",
                    "/__simulator/load",
                    body={"scenario": scenario},
                )
                self.assertEqual(status, 200)
                status, _, payload = self.request(
                    "GET",
                    "/api/v1/status",
                    authorization=f"Bearer {SYNTHETIC_API_KEY}",
                )
                self.assertEqual(status, 200)
                self.assertEqual(payload["data"]["overall"], expected_overall)
                self.assertNotIn("distance", payload["data"]["components"])
                self.assertNotIn("memory", payload["data"]["components"])

        status, _, _ = self.request(
            "POST",
            "/__simulator/load",
            body={"scenario": "boot-partial"},
        )
        self.assertEqual(status, 200)
        status, _, info = self.request(
            "GET",
            "/api/v1/info",
            authorization=f"Bearer {SYNTHETIC_API_KEY}",
        )
        self.assertEqual(status, 200)
        self.assertFalse(info["last_update_complete"])
        self.assertEqual(info["last_update_error"], "ESP_ERR_INVALID_STATE")
        status, _, health = self.request("GET", "/api/v1/health")
        self.assertEqual(status, 200)
        self.assertFalse(health["data"]["ok"])
        self.assertEqual(health["data"]["liveness"]["query_error"], "ESP_ERR_NOT_SUPPORTED")
        self.assertFalse(health["data"]["snapshot"]["last_update_complete"])

    def test_control_plane_lists_loads_advances_and_reports_errors(self) -> None:
        status, _, payload = self.request("GET", "/__simulator/list")
        self.assertEqual(status, 200)
        self.assertTrue(payload["synthetic"])
        self.assertIn("healthy", payload["data"]["scenarios"])

        status, _, before = self.request("GET", "/__simulator/state")
        self.assertEqual(status, 200)
        before_us = before["data"]["generation"]["simulator"]["clock_us"]

        status, _, after = self.request(
            "POST",
            "/__simulator/advance",
            body={"milliseconds": 25},
        )
        self.assertEqual(status, 200)
        after_us = after["data"]["generation"]["simulator"]["clock_us"]
        self.assertEqual(after_us - before_us, 25_000)

        status, _, loaded = self.request(
            "POST",
            "/__simulator/load",
            body={"scenario": "healthy", "start_at_ms": 5},
        )
        self.assertEqual(status, 200)
        self.assertEqual(
            loaded["data"]["generation"]["simulator"]["elapsed_ms"],
            5,
        )

        status, _, payload = self.request(
            "POST",
            "/__simulator/action",
            body={"action": "definitely-not-an-action"},
        )
        self.assertEqual(status, 409)
        self.assertIn("error", payload)


class AuthFailureWindowTests(unittest.TestCase):
    def test_limit_resets_at_exact_sixty_second_boundary(self) -> None:
        now = [100.0]
        window = AuthFailureWindow(clock=lambda: now[0])
        for _ in range(20):
            window.record_failure()
        self.assertTrue(window.is_limited())
        now[0] += 59.999
        self.assertTrue(window.is_limited())
        now[0] += 0.001
        self.assertFalse(window.is_limited())
        self.assertEqual(window.failure_count(), 0)


class LoopbackBindingTests(unittest.TestCase):
    def test_only_loopback_literals_and_localhost_are_allowed(self) -> None:
        self.assertEqual(resolve_loopback_bind("localhost"), "127.0.0.1")
        self.assertEqual(resolve_loopback_bind("127.0.0.2"), "127.0.0.2")
        self.assertEqual(resolve_loopback_bind("::1"), "::1")
        for value in ("0.0.0.0", "192.0.2.10", "example.com", ""):
            with self.subTest(value=value), self.assertRaises(BindAddressError):
                resolve_loopback_bind(value)


if __name__ == "__main__":
    unittest.main()
