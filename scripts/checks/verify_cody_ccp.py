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

"""Host CCP regression checks; synthetic clocks/keys, no LAN, Codex or device execution."""

from __future__ import annotations

import contextlib
from dataclasses import replace
import errno
import hashlib
import io
import json
import os
from pathlib import Path
import sqlite3
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.checks.verify_power_policy import function_text
import ccp_server as ccp
import ccp_activity as activity
import ccp_runtime as runtime
import ccp_windows as windows
import ccp_interactions as interactions
import ccp_technical as technical_metadata
import ccp_security as security
import ccp_pricing as pricing
import ccp_usage as native_usage
import ccp_chat_title as native_title
import ccp_presence as app_presence
import ccp_tool_labels as tool_labels
from cody_ccp_event_vectors import event_vectors


# No parameters: isolate every existing host fixture from the installed native state index.
# Raises OSError when the artifact-owned temporary test home cannot be prepared.
def setUpModule():
    output = ROOT / "artifacts/checks/cody-ccp"
    output.mkdir(parents=True, exist_ok=True)
    temporary = tempfile.TemporaryDirectory(dir=output)
    unittest.addModuleCleanup(temporary.cleanup)
    environment = patch.dict(os.environ, {"CODEX_HOME": temporary.name})
    environment.start()
    unittest.addModuleCleanup(environment.stop)


# link_key: Synthetic retained key belonging to the READY target.
# ready: Exact broker challenge packet produced by the real responder.
# Raises ValueError for a malformed READY envelope.
def committed_for_ready(link_key, ready):
    kind, fields = ccp.parse_datagram(ready)
    names = ("device", "pc", "nonce", "challenge", "address", "port")
    if kind != "READY" or set(fields) != {*names, "proof"}:
        raise ValueError("Fixture requires the exact fresh READY challenge")
    values = [fields[name] for name in names]
    return ccp.encode_datagram("COMMITTED", list(zip(names, values))
        + [("proof", ccp.make_proof(link_key, "committed-v2", *values))])


# responder: Real broker responder whose UDP socket is a Mock.
# link_key: Synthetic retained device key.
# source: Device endpoint that requested the latest READY.
# Raises ValueError or the original responder failure if challenge admission fails.
def acknowledge_ready(responder, link_key, source):
    ready = responder._socket.sendto.call_args.args[0]
    committed = committed_for_ready(link_key, ready)
    responder._handle_datagram(committed, source)
    return committed


class CcpContractTests(unittest.TestCase):
    # self: Host fixture whose filesystem writes are confined to artifacts/.
    # Raises OSError if the bounded temporary fixture cannot be created.
    def setUp(self) -> None:
        output = ROOT / "artifacts/checks/cody-ccp"
        output.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=output)
        self.addCleanup(temporary.cleanup)
        environment = patch.dict(ccp.os.environ, {"LOCALAPPDATA": temporary.name})
        environment.start()
        self.addCleanup(environment.stop)
        self.state_path = Path(temporary.name) / "CodyCodexPlugin/state.json"
        # Fixture diagnostics are captured; MCP stdout must stay protocol-only.
        diagnostics = contextlib.redirect_stderr(io.StringIO())
        diagnostics.__enter__()
        self.addCleanup(diagnostics.__exit__, None, None, None)

    # self: Fresh synthetic pairing fixture with mocked DPAPI protection.
    # Raises AssertionError if persisted commissioning is reported as liveness.
    def test_pairing_requires_authenticated_presence(self) -> None:
        state = ccp.StateStore()
        with patch.object(ccp, "protect_secret", return_value="synthetic-protected-key"):
            state.remember_pairing("abcdef012345", bytes(32), "192.0.2.10")
        self.assertFalse(state.public_status()[0]["connected"])
        self.assertEqual(state.public_status()[0]["last_seen_at"], 0.0)
        with patch.object(ccp.time, "monotonic", return_value=100.0):
            state.mark_seen("abcdef012345", "192.0.2.20")
            self.assertTrue(state.public_status()[0]["connected"])
        with patch.object(ccp.time, "monotonic", return_value=110.01):
            self.assertFalse(state.public_status()[0]["connected"])

    # self: Synthetic clock correction fixture.
    # Raises AssertionError if a wall-clock correction changes the liveness lease.
    def test_liveness_ignores_wall_clock_changes(self) -> None:
        state = ccp.StateStore()
        state.records["abcdef012345"] = ccp.PairingRecord("abcdef012345", "synthetic", 1.0)
        with patch.object(ccp.time, "monotonic", return_value=10.0):
            state.mark_seen("abcdef012345", "192.0.2.10")
        with patch.object(ccp.time, "time", return_value=-10000.0):
            with patch.object(ccp.time, "monotonic", return_value=19.0):
                self.assertTrue(state.public_status()[0]["connected"])
            with patch.object(ccp.time, "monotonic", return_value=21.0):
                self.assertFalse(state.public_status()[0]["connected"])

    # self: Existing persisted PC identity fixture.
    # Raises AssertionError if failed reads create or overwrite a PC identity.
    def test_read_failure_preserves_identity(self) -> None:
        ccp.StateStore()
        previous = self.state_path.read_bytes()
        with patch.object(ccp.Path, "read_text", side_effect=PermissionError("synthetic denied")):
            with self.assertRaisesRegex(RuntimeError, "state read failed"):
                ccp.StateStore()
        self.assertEqual(self.state_path.read_bytes(), previous)

    # self: Malformed persisted JSON fixture.
    # Raises AssertionError if duplicate fields are accepted or repaired.
    def test_duplicate_persisted_identity_is_rejected(self) -> None:
        ccp.StateStore()
        first_identity = "a" * 32
        second_identity = "b" * 32
        malformed = (
            '{"version":1,"pc_id":"' + first_identity
            + '","pc_id":"' + second_identity + '","records":[]}'
        )
        self.state_path.write_text(malformed, encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "duplicate field"):
            ccp.StateStore()
        self.assertEqual(self.state_path.read_text(encoding="utf-8"), malformed)

    # self: Mocked occupied listener fixture; no socket is created.
    # Raises AssertionError if a rejected second instance touches persistent state.
    def test_listener_ownership_precedes_state_access(self) -> None:
        failure = OSError("synthetic occupied port")
        with patch.object(ccp.os, "name", "nt"):
            with patch.object(ccp, "open_companion_listener", side_effect=failure):
                with patch.object(ccp, "StateStore") as state_constructor:
                    with self.assertRaises(OSError) as raised:
                        ccp.CcpService()
                    self.assertIs(raised.exception, failure)
                    state_constructor.assert_not_called()

    # self: Mocked listener and failed identity load fixture.
    # Raises AssertionError if startup leaks ownership or hides the original error.
    def test_state_failure_releases_listener(self) -> None:
        listener = Mock()
        failure = RuntimeError("synthetic corrupt state")
        with patch.object(ccp.os, "name", "nt"):
            with patch.object(ccp, "open_companion_listener", return_value=listener):
                with patch.object(ccp, "StateStore", side_effect=failure):
                    with self.assertRaises(RuntimeError) as raised:
                        ccp.CcpService()
        self.assertIs(raised.exception, failure)
        listener.close.assert_called_once_with()

    # self: Mocked failed responder fixture; no thread or network runs.
    # Raises AssertionError if a dead listener can still produce a successful status result.
    def test_listener_failure_is_a_structured_tool_error(self) -> None:
        service = Mock()
        service.responder.require_running.side_effect = OSError("synthetic receive failed")
        result = ccp.handle_request(
            {
                "id": 7,
                "method": "tools/call",
                "params": {"name": "ccp_connection_status", "arguments": {}},
            },
            service,
        )
        self.assertTrue(result["result"]["isError"])
        self.assertIn("synthetic receive failed", result["result"]["structuredContent"]["error"])
        service.state.public_status.assert_not_called()

    # self: Real status tool with one canonical owner boundary and two retained device records.
    # Raises AssertionError if status resamples, changes identity/age or omits technical metadata.
    def test_connection_status_returns_one_atomic_activity_technical_bundle(self):
        service = Mock()
        service.state.pc_id = "a" * 32
        service.state.public_status.return_value = [{"device_id": "device-a"}, {"device_id": "device-b"}]
        service.notifications_error = None
        service.responder.clock.setup_status.return_value = {}
        activity_fields = {"chat": "b" * 64, "revision": "0000000000000007", "phase": "working"}
        technical_fields = {"chat": activity_fields["chat"], "revision": activity_fields["revision"],
                            "used": "1234", "capacity": "475000", "cumulative": "678901",
                            "input": "1200", "output": "34", "usageage": "9876"}
        service.activity.snapshot_bundle.return_value = (activity_fields, technical_fields)
        result = ccp.call_tool(service, "ccp_connection_status", {})["structuredContent"]
        service.activity.snapshot_bundle.assert_called_once_with()
        service.activity.snapshot.assert_not_called()
        self.assertEqual(result["count"], 2)
        for device in ("device-a", "device-b"):
            self.assertEqual(result["activity"][device], activity_fields)
            self.assertEqual(result["technical"][device], technical_fields)
            self.assertEqual(result["technical"][device]["chat"], result["activity"][device]["chat"])
            self.assertEqual(result["technical"][device]["revision"], result["activity"][device]["revision"])

    # self: Synthetic authenticated rediscovery fixture; the UDP socket is a mock.
    # Raises AssertionError if FIND alone marks presence or READY loses endpoint binding.
    def test_presence_requires_verified_find_and_fresh_committed(self) -> None:
        state = Mock()
        state.pc_id = "a" * 32
        link_key = bytes(range(32))
        state.retained_keys.return_value = ccp.RetainedKeys(link_key, None)
        responder = ccp.CompanionResponder(state, Mock(), ccp.ActivityStore())
        device_id = "abcdef012345"
        nonce = "b" * 32
        fields = [("device", device_id), ("pc", state.pc_id), ("nonce", nonce)]
        invalid = ccp.encode_datagram("FIND", fields + [("proof", "0" * 64)])
        with self.assertRaisesRegex(ValueError, "proof does not match"):
            responder._handle_datagram(invalid, ("192.0.2.10", ccp.DEVICE_PORT))
        state.mark_seen.assert_not_called()
        responder._socket.sendto.assert_not_called()

        proof = ccp.make_proof(link_key, "find", device_id, state.pc_id, nonce)
        valid = ccp.encode_datagram("FIND", fields + [("proof", proof)])
        responder._handle_datagram(valid, ("192.0.2.10", ccp.DEVICE_PORT))
        payload, destination = responder._socket.sendto.call_args.args
        kind, response = ccp.parse_datagram(payload)
        self.assertEqual(kind, "READY")
        self.assertEqual(response["nonce"], nonce)
        names = ("device", "pc", "nonce", "challenge", "address", "port")
        self.assertEqual(set(response), {*names, "proof"})
        self.assertRegex(response["challenge"], r"^[0-9a-f]{32}$")
        self.assertEqual((response["address"], response["port"]), ("192.0.2.10", str(ccp.DEVICE_PORT)))
        expected_proof = ccp.make_proof(link_key, "ready-v2", *(response[name] for name in names))
        self.assertEqual(response["proof"], expected_proof)
        self.assertEqual(destination, ("192.0.2.10", ccp.DEVICE_PORT))
        state.mark_seen.assert_not_called()
        state.confirm_device_choice.assert_not_called()
        acknowledge_ready(responder, link_key, destination)
        state.mark_seen.assert_called_once_with(device_id, "192.0.2.10")

    # self: Synthetic pairing fixture with mocked exchanges and persistence.
    # Raises AssertionError if one-sided persistence becomes a successful pairing result.
    def test_active_confirmation_is_required_for_pairing_success(self) -> None:
        service = ccp.CcpService.__new__(ccp.CcpService)
        service.state = Mock(pc_id="a" * 32)
        offer = ccp.Offer(
            address="192.0.2.10",
            request_id="a" * 16,
            device_id="abcdef012345",
            session_id="a" * 8,
            salt="a" * 32,
            tag="a" * 64,
            received_at=1.0,
        )
        service._offers = {(offer.device_id, offer.session_id): offer}
        service._offers_lock = threading.Lock()
        service._pairing_lock = threading.Lock()
        service._matching_offers = Mock(return_value=[(offer, bytes(32))])
        operations = Mock()
        service._exchange_pairing_request = operations.device_saved
        service.state.stage_pairing = operations.pc_candidate
        service.state.confirm_device_choice = operations.pc_committed
        service._exchange_confirmation = operations.active
        operations.active.side_effect = TimeoutError("synthetic missing ACTIVE")
        with self.assertRaisesRegex(TimeoutError, "missing ACTIVE"):
            service.pair("ABCDEF")
        completed_steps = [call[0] for call in operations.mock_calls]
        self.assertEqual(completed_steps, ["pc_candidate", "device_saved", "active"])

    # self: Isolated transaction fixture; DPAPI alone is replaced by a reversible synthetic encoding.
    # Returns a real StateStore with an existing active association and two distinct fixture keys.
    def transaction_fixture(self):
        for replacement in (patch.object(ccp, "protect_secret", side_effect=lambda value, description: value.hex()),
                            patch.object(ccp, "unprotect_secret", side_effect=bytes.fromhex)):
            replacement.start()
            self.addCleanup(replacement.stop)
        state = ccp.StateStore()
        old_key, new_key = bytes(range(32)), bytes(reversed(range(32)))
        state.remember_pairing("abcdef012345", old_key, "127.0.0.1")
        return state, old_key, new_key

    # self: Durable old/new associations across a process restart.
    # Raises AssertionError if staging overwrites the active file or silently admits a second candidate.
    def test_pending_pairing_preserves_old_record_and_requires_explicit_replacement(self):
        state, old_key, new_key = self.transaction_fixture()
        active_bytes = self.state_path.read_bytes()
        state.stage_pairing("abcdef012345", new_key, "127.0.0.1")
        self.assertEqual(self.state_path.read_bytes(), active_bytes)
        restarted = ccp.StateStore()
        self.assertEqual(restarted.retained_keys("abcdef012345"), ccp.RetainedKeys(old_key, new_key))
        status = restarted.public_status()[0]
        self.assertTrue(status["pairing_pending"])
        self.assertTrue(status["retained_pairing"])
        with self.assertRaisesRegex(ValueError, "unresolved pairing candidate"):
            restarted.admit_pairing("abcdef012345", False)
        restarted.stage_pairing("abcdef012345", bytes(32), "127.0.0.1", True)
        self.assertEqual(restarted.retained_keys("abcdef012345").active, old_key)

    # self: Failure after active persistence but before candidate-file cleanup.
    # Raises AssertionError if partial failure reports success or prevents idempotent restart recovery.
    def test_pending_cleanup_failure_is_explicit_and_recoverable(self):
        state, _, new_key = self.transaction_fixture()
        state.stage_pairing("abcdef012345", new_key, "127.0.0.1")
        with patch.object(state, "_save_pending_locked", side_effect=RuntimeError("synthetic cleanup denied")):
            with self.assertRaisesRegex(RuntimeError, "cleanup denied"):
                state.confirm_device_choice("abcdef012345", new_key, new_key)
        restarted = ccp.StateStore()
        self.assertEqual(restarted.retained_keys("abcdef012345"), ccp.RetainedKeys(new_key, new_key))
        restarted.confirm_device_choice("abcdef012345", new_key, new_key)
        self.assertEqual(restarted.retained_keys("abcdef012345"), ccp.RetainedKeys(new_key, None))

    # self: Old-key messages from a retained association, including authenticated recorded replays.
    # Raises AssertionError if a previous key authorizes deleting a newer unresolved candidate.
    def test_old_key_commit_keeps_pending_candidate_and_previous_link(self):
        state, old_key, new_key = self.transaction_fixture()
        state.stage_pairing("abcdef012345", new_key, "127.0.0.1")
        state.confirm_device_choice("abcdef012345", old_key, new_key)
        self.assertEqual(state.retained_keys("abcdef012345"), ccp.RetainedKeys(old_key, new_key))
        with self.assertRaisesRegex(ValueError, "unresolved pairing candidate"):
            state.admit_pairing("abcdef012345", False)

    # self: Real HMAC responder with a private, isolated state owner.
    # Raises AssertionError if FIND promotes a candidate, or forged/stale COMMITTED is accepted.
    def test_committed_requires_nonce_source_proof_and_current_candidate(self):
        state, old_key, new_key = self.transaction_fixture()
        state.stage_pairing("abcdef012345", new_key, "127.0.0.1")
        responder = ccp.CompanionResponder(state, Mock(), ccp.ActivityStore())
        source = ("127.0.0.1", ccp.DEVICE_PORT)
        values = ["abcdef012345", state.pc_id, "b" * 32]
        fields = list(zip(("device", "pc", "nonce"), values))
        find = ccp.encode_datagram("FIND", fields + [("proof", ccp.make_proof(new_key, "find", *values))])
        responder._handle_datagram(find, source)
        committed = committed_for_ready(new_key, responder._socket.sendto.call_args.args[0])
        self.assertEqual(state.retained_keys(values[0]), ccp.RetainedKeys(old_key, new_key))
        with self.assertRaisesRegex(ValueError, "fresh endpoint challenge"):
            responder._handle_datagram(committed, (source[0], source[1] + 1))
        _, committed_fields = ccp.parse_datagram(committed)
        forged = ccp.encode_datagram("COMMITTED", [(name, "0" * 64 if name == "proof" else value)
            for name, value in committed_fields.items()])
        with self.assertRaisesRegex(ValueError, "proof is invalid"):
            responder._handle_datagram(forged, source)
        state.stage_pairing(values[0], bytes(32), "127.0.0.1", True)
        with self.assertRaisesRegex(ValueError, "superseded candidate"):
            responder._handle_datagram(committed, source)
        self.assertEqual(state.retained_keys(values[0]).active, old_key)

    # self: A durable PC candidate receiving separate fresh challenge acknowledgements.
    # Raises AssertionError if recovery requires a fresh display code or later presence changes the key.
    def test_fresh_committed_retry_preserves_the_promoted_identity(self):
        state, _, new_key = self.transaction_fixture()
        state.stage_pairing("abcdef012345", new_key, "127.0.0.1")
        responder = ccp.CompanionResponder(state, Mock(), ccp.ActivityStore())
        source = ("127.0.0.1", ccp.DEVICE_PORT)
        for nonce in ("a" * 32, "b" * 32):
            values = ["abcdef012345", state.pc_id, nonce]
            fields = list(zip(("device", "pc", "nonce"), values))
            responder._handle_datagram(ccp.encode_datagram("FIND", fields +
                [("proof", ccp.make_proof(new_key, "find", *values))]), source)
            acknowledge_ready(responder, new_key, source)
            self.assertEqual(state.retained_keys(values[0]), ccp.RetainedKeys(new_key, None))

    # self: Isolated real responder using synthetic retained credentials and a mocked socket.
    # Returns its state owner, responder, key, source endpoint and authenticated capability FIND.
    def challenge_fixture(self):
        state = Mock(pc_id="a" * 32)
        key = bytes(range(32))
        state.retained_keys.return_value = ccp.RetainedKeys(key, None)
        responder = ccp.CompanionResponder(state, Mock(), ccp.ActivityStore())
        source = ("192.0.2.10", ccp.DEVICE_PORT)
        values = ["abcdef012345", state.pc_id, "b" * 32]
        find = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values))
            + [("activity", "5"), ("events", "3"), ("current", "0" * 32), ("state", "none"), ("event_select", "0" * 32),
               ("proof", ccp.make_proof(key, "find-events-activity-v6", *values, "0" * 32, "none", "0" * 32))])
        return state, responder, key, source, find

    # self: Replayed broadcast FIND arriving from a different untrusted endpoint.
    # Raises AssertionError if replay exposes current projections, renews presence or steals the event route.
    def test_replayed_find_cannot_replace_the_authenticated_peer(self):
        state, responder, key, source, find = self.challenge_fixture()
        responder._handle_datagram(find, source)
        original_commit = acknowledge_ready(responder, key, source)
        original_peer = responder._event_peers["abcdef012345"]
        responder._socket.reset_mock()
        attacker = ("192.0.2.99", ccp.DEVICE_PORT)
        responder._handle_datagram(find, attacker)
        packets = [ccp.parse_datagram(call.args[0])[0] for call in responder._socket.sendto.call_args_list]
        self.assertEqual(packets, ["READY"])
        self.assertEqual(responder._event_peers["abcdef012345"], original_peer)
        state.mark_seen.assert_called_once_with("abcdef012345", source[0])
        with self.assertRaisesRegex(ValueError, "fresh endpoint challenge"):
            responder._handle_datagram(original_commit, attacker)
        self.assertEqual(len(responder._socket.sendto.call_args_list), 1)

    # self: A consumed genuine COMMITTED envelope and a retained authenticated association.
    # Raises AssertionError if replay repeats persistence, liveness or current projections.
    def test_committed_challenge_is_single_use(self):
        state, responder, key, source, find = self.challenge_fixture()
        responder._handle_datagram(find, source)
        committed = acknowledge_ready(responder, key, source)
        responder._socket.reset_mock()
        with self.assertRaisesRegex(ValueError, "fresh endpoint challenge"):
            responder._handle_datagram(committed, source)
        state.confirm_device_choice.assert_called_once()
        state.mark_seen.assert_called_once()
        responder._socket.sendto.assert_not_called()

    # self: A fresh broker challenge followed by delayed device proof of possession.
    # Raises AssertionError if an expired challenge creates presence or exposes current projections.
    def test_committed_challenge_expires_without_presence(self):
        state, responder, key, source, find = self.challenge_fixture()
        with patch.object(ccp.time, "monotonic", return_value=100.0):
            responder._handle_datagram(find, source)
            committed = committed_for_ready(key, responder._socket.sendto.call_args.args[0])
        with patch.object(ccp.time, "monotonic", return_value=100.0 + ccp.CHALLENGE_TIMEOUT_SECONDS + .001):
            with self.assertRaisesRegex(ValueError, "fresh endpoint challenge"):
                responder._handle_datagram(committed, source)
        state.mark_seen.assert_not_called()
        state.confirm_device_choice.assert_not_called()
        self.assertEqual(responder._event_peers, {})

    # self: Persistence failure after an otherwise valid fresh challenge acknowledgement.
    # Raises AssertionError if a failed commit publishes data or its consumed envelope can be retried.
    def test_failed_committed_persistence_consumes_the_challenge(self):
        state, responder, key, source, find = self.challenge_fixture()
        responder._handle_datagram(find, source)
        committed = committed_for_ready(key, responder._socket.sendto.call_args.args[0])
        state.confirm_device_choice.side_effect = RuntimeError("synthetic persistence denied")
        responder._socket.reset_mock()
        with self.assertRaisesRegex(RuntimeError, "persistence denied"):
            responder._handle_datagram(committed, source)
        with self.assertRaisesRegex(ValueError, "fresh endpoint challenge"):
            responder._handle_datagram(committed, source)
        state.mark_seen.assert_not_called()
        responder._socket.sendto.assert_not_called()

    # self: Partial local storage where the candidate file survived but its PC identity did not.
    # Raises AssertionError if startup creates an unrelated PC identity over ambiguous state.
    def test_orphan_candidate_store_does_not_create_a_new_identity(self):
        ccp.StateStore()
        self.state_path.unlink()
        with self.assertRaisesRegex(RuntimeError, "active identity is missing"):
            ccp.StateStore()
        self.assertFalse(self.state_path.exists())

    # self: Source-only plugin catalog fixture.
    # Raises AssertionError if packaging versions, local paths or MCP entrypoints diverge.
    def test_local_plugin_entrypoints_agree(self) -> None:
        plugin_root = ROOT / "plugins/cody-codex-plugin"
        marketplace = json.loads((ROOT / ".agents/plugins/marketplace.json").read_text(encoding="utf-8"))
        entry = marketplace["plugins"][0]
        self.assertEqual((ROOT / entry["source"]["path"]).resolve(), plugin_root.resolve())
        manifest_path = plugin_root / ".codex-plugin/plugin.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["version"], ccp.SERVER_VERSION)
        for wiring_path in ("mcp.json", ".mcp.json"):
            wiring = json.loads((plugin_root / wiring_path).read_text(encoding="utf-8"))["mcpServers"]["ccp"]
            self.assertEqual(wiring["cwd"], ".")
            self.assertEqual(wiring["args"], ["-B", "./mcp/ccp_server.py", "--stdio"])
            # Native Codex resolves cwd relative to the installed plugin, but passes
            # arguments literally. Shell-style placeholders are not valid paths.
            launch_cwd = (plugin_root / wiring["cwd"]).resolve()
            self.assertTrue(launch_cwd.is_dir())
            entrypoint = (launch_cwd / wiring["args"][1]).resolve()
            self.assertEqual(entrypoint, (plugin_root / "mcp/ccp_server.py").resolve())
            self.assertTrue(entrypoint.is_file())


class CcpAdmissionTests(unittest.TestCase):
    # self: Pure memory fixture with no socket, protected state or actual clock dependency.
    # Raises AssertionError if one source's burst consumes another source's independent allowance.
    def test_source_burst_is_bounded_and_other_sources_still_admit(self):
        with patch.object(security.time, "monotonic", return_value=100.0):
            budget = security.DatagramAdmission()
            self.assertTrue(all(budget.admit("192.0.2.10") for _ in range(int(security.SOURCE_PACKET_BURST))))
            self.assertFalse(budget.admit("192.0.2.10"))
            self.assertTrue(budget.admit("192.0.2.20"))

    # self: More distinct untrusted sources than the fixed admission table can retain.
    # Raises AssertionError if source churn grows memory or bypasses the aggregate burst.
    def test_global_packet_budget_and_source_table_stay_bounded(self):
        with patch.object(security.time, "monotonic", return_value=100.0):
            budget = security.DatagramAdmission()
            for index in range(int(security.GLOBAL_PACKET_BURST)):
                self.assertTrue(budget.admit(f"192.0.2.{index}"))
            self.assertEqual(len(budget._sources), security.MAX_SOURCE_BUCKETS)
            self.assertFalse(budget.admit("198.51.100.1"))
            self.assertEqual(len(budget._sources), security.MAX_SOURCE_BUCKETS)

    # self: Exhausted source receiving only its configured monotonic replenishment.
    # Raises AssertionError if refill grants an unbounded fresh burst.
    def test_source_refill_matches_its_sustained_rate(self):
        with patch.object(security.time, "monotonic", return_value=100.0):
            budget = security.DatagramAdmission()
            for _ in range(int(security.SOURCE_PACKET_BURST)):
                self.assertTrue(budget.admit("192.0.2.10"))
        with patch.object(security.time, "monotonic", return_value=101.0):
            self.assertTrue(all(budget.admit("192.0.2.10") for _ in range(int(security.SOURCE_PACKET_RATE))))
            self.assertFalse(budget.admit("192.0.2.10"))

    # self: Repeated diagnostic rejection reasons confined to an in-memory stream.
    # Raises AssertionError if repeated rejections create per-packet output or lose the suppressed count.
    def test_rejection_reporter_aggregates_a_burst(self):
        output = io.StringIO()
        reporter = security.RejectionReporter()
        with contextlib.redirect_stderr(output), patch.object(security.time, "monotonic", return_value=100.0):
            for _ in range(4):
                reporter.reject("fixture packet", "synthetic malformed input")
        self.assertEqual(len(output.getvalue().splitlines()), 1)
        with contextlib.redirect_stderr(output), patch.object(security.time, "monotonic", return_value=105.0):
            reporter.reject("fixture packet", "synthetic malformed input")
        self.assertEqual(len(output.getvalue().splitlines()), 2)
        self.assertIn("additional_rejections=3", output.getvalue())

    # self: Oversized cached offers that must be rejected before any expensive verifier derivation.
    # Raises AssertionError if a rejected pairing still starts PBKDF2 work.
    def test_matching_offer_cap_precedes_key_derivation(self):
        service = ccp.CcpService.__new__(ccp.CcpService)
        with patch.object(ccp, "derive_verifier") as derive:
            with self.assertRaisesRegex(ValueError, "offer budget"):
                service._matching_offers("ABCDEF", [Mock()] * (ccp.MAX_DISCOVERY_OFFERS + 1))
        derive.assert_not_called()

    # self: Synthetic verifier work exceeding the complete pairing-verification deadline.
    # Raises AssertionError if deadline exhaustion returns a partial match or continues work.
    def test_matching_deadline_discards_partial_matches(self):
        service = ccp.CcpService.__new__(ccp.CcpService)
        values = ("a" * 16, "abcdef012345", "a" * 8, "a" * 32)
        offer = Mock(request_id=values[0], device_id=values[1], session_id=values[2], salt=values[3],
                     tag=ccp.make_proof(bytes(32), "offer", *values))
        times = [100.0, 100.0, 100.0, 100.0 + ccp.PAIRING_VERIFICATION_SECONDS]
        with patch.object(ccp.time, "monotonic", side_effect=times):
            with patch.object(ccp, "derive_verifier", return_value=bytes(32)) as derive:
                with self.assertRaisesRegex(ValueError, "verification budget"):
                    service._matching_offers("ABCDEF", [offer, offer])
        derive.assert_called_once_with("ABCDEF", offer.salt)


class ToolLabelTests(unittest.TestCase):
    # self: Exact currently exposed native tool identities, without tool arguments or private source text.
    # Raises AssertionError if a supported GitHub/native action retains its machine spelling or wrong provider.
    def test_known_native_and_github_tools_have_authored_display_labels(self) -> None:
        cases = {
            "mcp__codex_apps__github_get_pr_info": "Tool: GitHub: read pull request",
            "mcp__codex_apps__github_fetch_pr": "Tool: GitHub: read pull request",
            "mcp__codex_apps__github_get_pr_diff": "Tool: GitHub: read pull request changes",
            "mcp__codex_apps__github_fetch_file": "Tool: GitHub: read file",
            "mcp__codex_apps__github_create_pull_request": "Tool: GitHub: create pull request",
            "mcp__codex_apps__github_list_pr_changed_filenames": "Tool: GitHub: list changed files",
            "functions.exec_command": "Tool: Run command",
            "functions.apply_patch": "Tool: Edit files",
            "functions.exec": "Tool: Run tool script",
        }
        for identity, expected in cases.items():
            with self.subTest(identity=identity):
                self.assertEqual(tool_labels.tool_display_label(identity), expected)
                self.assertTrue(all(32 <= ord(character) <= 126 for character in expected))

    # self: Unknown complete identities and deliberate suffix collisions across unrelated providers.
    # Raises AssertionError if fallback loses words, invents a known action or attributes another provider's tool to GitHub.
    def test_unknown_names_preserve_the_complete_readable_identity_and_provider(self) -> None:
        cases = {
            "unknown__tool_name_with_all_parts": "Tool: unknown tool name with all parts",
            "mcp__custom_server__read_widget_details": "Tool: custom server: read widget details",
            "mcp__custom_server__github_fetch_pr": "Tool: custom server: github fetch pr",
            "mcp__github__fetch_pr": "Tool: github: fetch pr",
            "mcp__codex_apps__other_provider_fetch_pr": "Tool: codex apps: other provider fetch pr",
            "mcp__codex_apps__github_fetch_future_widget": "Tool: GitHub: fetch future widget",
            "vendor.apply_patch": "Tool: vendor.apply patch",
            "functions.mcp__codex_apps__github_fetch_pr": "Tool: functions.mcp codex apps github fetch pr",
        }
        for identity, expected in cases.items():
            with self.subTest(identity=identity):
                self.assertEqual(tool_labels.tool_display_label(identity), expected)
        self.assertNotEqual(tool_labels.tool_display_label("mcp__codex_apps__github_fetch_pr"),
                            tool_labels.tool_display_label("mcp__custom_server__github_fetch_pr"))

    # self: Exact wire boundary and unrepresentable complete names; no shortened identity may look complete.
    # Raises AssertionError for truncation, a changed 192-byte limit or an unlabeled empty projection.
    def test_display_boundary_preserves_full_text_or_reports_unavailability(self) -> None:
        self.assertEqual(tool_labels.MAX_TOOL_LABEL_LENGTH, 192)
        exact = "X" * 186
        self.assertEqual(tool_labels.tool_display_label(exact), "Tool: " + exact)
        diagnostics = io.StringIO()
        with contextlib.redirect_stderr(diagnostics):
            self.assertEqual(tool_labels.tool_display_label("X" * 187), "Tool: name exceeds display limit")
            self.assertEqual(tool_labels.tool_display_label("X" * 256), "Tool: name exceeds display limit")
            self.assertEqual(tool_labels.tool_display_label("___"), "Tool: name unavailable")
        self.assertIn("exceeds 192", diagnostics.getvalue())
        self.assertNotIn("X" * 187, diagnostics.getvalue())

    # self: Invalid source values are rejected before they can become numeric, localized or truncated UI labels.
    # Raises AssertionError if the source ASCII/type/length contract is weakened or private input is echoed.
    def test_invalid_tool_sources_are_rejected_without_echoing_their_contents(self) -> None:
        values = (None, True, 12, [], "", "X" * 257, "private\nname", "private\x00name", "private\u00e9name")
        for value in values:
            with self.subTest(kind=type(value).__name__), self.assertRaises(ValueError) as rejected:
                tool_labels.tool_display_label(value)
            self.assertNotIn("private", str(rejected.exception))


class ChatActivityTests(unittest.TestCase):
    # self: Pure host fixture with no Codex, sockets, protected state or physical device.
    def setUp(self) -> None:
        self.store = activity.ActivityStore()
        self.event_time = 0
        self.event_base_ns = None

    # self: In-memory activity fixture.
    # name: Genuine-hook schema event represented synthetically for this host test.
    # session: Opaque chat identity.
    # turn: Opaque turn identity.
    # fields: Additional allowlisted tool, model or compaction metadata.
    # Raises ValueError when the tested owner rejects the event.
    def observe(self, name, session="chat-a", turn="turn-a", **fields) -> None:
        self.event_time += 1
        if self.event_base_ns is None:
            self.event_base_ns = int(activity.time.monotonic() * 1_000_000_000)
        emitted_ns = self.event_base_ns + self.event_time
        with patch.object(activity.time, "monotonic_ns", return_value=emitted_ns + 1):
            self.store.observe({"hook_event_name": name, "session_id": session,
                                "cwd": fields.pop("cwd", "E:/synthetic-project"), "turn_id": turn,
                                "emitted_ns": emitted_ns, **fields})

    # self: Two synthetic chats with independently observed compaction lifecycle events.
    # Raises AssertionError if a background compaction changes the followed chat or its projection.
    def test_background_compaction_cannot_take_over_followed_chat(self) -> None:
        self.observe("UserPromptSubmit", session="chat-background", model="synthetic-model-b")
        self.observe("UserPromptSubmit", model="synthetic-model-a")
        followed = self.store.snapshot_bundle()[1]
        self.observe("PreCompact", session="chat-background", trigger="auto", model="synthetic-model-b")
        result = self.store.snapshot_bundle()[1]
        self.assertEqual(result["chat"], followed["chat"])
        self.assertEqual(result["model"], followed["model"])
        self.assertEqual(result["compaction"], "none")

    # self: Genuine-schema synthetic compaction surrounding an unrelated local command.
    # Raises AssertionError if ordinary tool activity replaces compaction or its source timestamp.
    def test_tool_activity_does_not_erase_active_compaction(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("PreCompact", trigger="auto")
        started = self.store._sessions["chat-a"].compact_observed_at
        self.observe("PreToolUse", tool_use_id="tool-a", tool_name="Bash")
        self.observe("PostToolUse", tool_use_id="tool-a")
        self.assertEqual(self.store.snapshot_bundle()[1]["compaction"], "active")
        self.assertEqual(self.store._sessions["chat-a"].compact_observed_at, started)

    # self: Duplicate, stale-turn and missing-start lifecycle boundaries.
    # Raises AssertionError if any unsupported boundary invents a successful context refresh.
    def test_compaction_duplicates_tardy_events_and_orphaned_completion(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("PreCompact", trigger="auto")
        with self.assertRaisesRegex(ValueError, "Duplicate compaction start"):
            self.observe("PreCompact", trigger="auto")
        self.observe("PostCompact", trigger="auto")
        self.assertEqual(self.store.snapshot_bundle()[1]["compaction"], "refreshed")
        with self.assertRaisesRegex(ValueError, "Duplicate compaction completion"):
            self.observe("PostCompact", trigger="auto")
        self.observe("UserPromptSubmit", turn="turn-new")
        with self.assertRaisesRegex(ValueError, "turn does not match"):
            self.observe("PostCompact", trigger="auto")
        self.observe("PostCompact", turn="turn-new", trigger="manual")
        self.assertEqual(self.store.snapshot_bundle()[1]["compaction"], "orphaned")
        sequence = self.store.snapshot_bundle()[1]["compactseq"]
        with self.assertRaisesRegex(ValueError, "Duplicate compaction completion"):
            self.observe("PostCompact", turn="turn-new", trigger="manual")
        self.assertEqual(self.store.snapshot_bundle()[1]["compactseq"], sequence)

    # self: Compaction without a final signal, followed by an explicit interruption.
    # Raises AssertionError if elapsed observation loss becomes completion or fresh counts.
    def test_compaction_expiry_and_interrupt_never_claim_refresh(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("PreCompact", trigger="auto")
        with patch.object(activity.time, "monotonic", return_value=self.event_base_ns / 1_000_000_000 + 121):
            expired = self.store.snapshot_bundle()[1]
        self.assertEqual(expired["compaction"], "stale")
        self.assertEqual(expired["used"], "-")
        self.observe("Interrupt")
        self.assertEqual(self.store.snapshot_bundle()[1]["compaction"], "interrupted")

    # self: A genuine-schema synthetic start followed only by fresh broker snapshot replies.
    # Raises AssertionError if transport replies reset elapsed or renew an unconfirmed source lease.
    def test_compaction_heartbeat_does_not_renew_source_observation(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("PreCompact", trigger="auto")
        session = self.store._sessions["chat-a"]
        started_at = session.compact_observed_at
        sequence = session.compact_sequence
        for seconds, expected in ((30, "active"), (119, "active"), (121, "stale"), (180, "stale")):
            with self.subTest(seconds=seconds), patch.object(activity.time, "monotonic", return_value=started_at + seconds):
                activity_projection, technical = self.store.snapshot_bundle()
                self.assertEqual(technical["compaction"], expected)
                self.assertEqual(int(technical["compactage"]), seconds * 1000)
                self.assertEqual(technical["revision"], activity_projection["revision"])
                self.assertEqual(session.compact_observed_at, started_at)
                self.assertEqual(session.compact_sequence, sequence)
                self.assertEqual(technical["used"], "-")

    # self: A synthetic observed start whose turn stops without its matching PostCompact hook.
    # Raises AssertionError if Stop invents a refresh or new context counts from a missing boundary.
    def test_turn_stop_with_missing_compaction_end_is_not_refreshed(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("PreCompact", trigger="auto")
        started_sequence = self.store._sessions["chat-a"].compact_sequence
        self.observe("Stop")
        activity_projection, technical = self.store.snapshot_bundle()
        self.assertEqual(activity_projection["phase"], "ready")
        self.assertEqual(technical["compaction"], "orphaned")
        self.assertEqual(int(technical["compactseq"], 16), started_sequence)
        self.assertTrue(all(technical[field] == "-" for field in technical_metadata.UNAVAILABLE_FIELDS))

    # self: Model-changing turns and same-turn user steering without a token source.
    # Raises AssertionError if a model change borrows an old numerator or steering restarts the turn clock.
    def test_model_and_turn_projection_preserve_unavailable_metrics(self) -> None:
        self.observe("UserPromptSubmit", model="synthetic-model-a")
        started = self.store._sessions["chat-a"].turn_started_at
        self.observe("PreToolUse", tool_use_id="tool-a", tool_name="Bash")
        self.observe("UserPromptSubmit", model="synthetic-model-b")
        self.assertEqual(self.store._sessions["chat-a"].turn_started_at, started)
        self.observe("PostToolUse", tool_use_id="tool-a", model="synthetic-model-b")
        activity_projection, technical = self.store.snapshot_bundle()
        self.assertEqual(activity_projection["revision"], technical["revision"])
        self.assertEqual(bytes.fromhex(technical["model"]), b"synthetic-model-b")
        self.assertTrue(all(technical[key] == "-" for key in technical_metadata.UNAVAILABLE_FIELDS))
        self.observe("UserPromptSubmit", turn="turn-new")
        self.assertEqual(self.store.snapshot_bundle()[1]["model"], "-")

    # self: A delayed previous-turn hook with private identities intentionally excluded from diagnostics.
    # Raises AssertionError if rejection weakens admission or loses its actionable hashed classification.
    def test_turn_rejection_logs_event_and_hashes_without_raw_identities(self) -> None:
        self.observe("UserPromptSubmit", turn="private-current-turn")
        diagnostics = io.StringIO()
        with contextlib.redirect_stderr(diagnostics), self.assertRaisesRegex(ValueError, "turn does not match"):
            self.observe("PreCompact", turn="private-old-turn", trigger="auto")
        message = diagnostics.getvalue()
        self.assertIn("rejected PreCompact", message)
        self.assertIn("expected_turn=", message)
        self.assertIn("observed_turn=", message)
        self.assertNotIn("private-current-turn", message)
        self.assertNotIn("private-old-turn", message)
        self.assertEqual(self.store.snapshot_bundle()[1]["compaction"], "none")

    # self: Concurrent chats whose genuine prompt events establish user intent.
    # Raises AssertionError if startup selects a chat or background events steal it.
    def test_latest_prompt_follows_automatically_without_background_switches(self) -> None:
        self.observe("SessionStart")
        self.observe("SessionStart", session="chat-b")
        self.assertEqual(self.store.snapshot()["phase"], "none")
        self.observe("UserPromptSubmit")
        first_chat = self.store.snapshot()["chat"]
        self.observe("UserPromptSubmit", session="chat-b", cwd="E:/project-b")
        second_chat = self.store.snapshot()["chat"]
        self.assertNotEqual(first_chat, second_chat)
        self.assertEqual(bytes.fromhex(self.store.snapshot()["project"]), b"project-b")
        self.observe("Stop", session="chat-a")
        self.observe("SessionStart", session="chat-a")
        self.assertEqual(self.store.snapshot()["chat"], second_chat)
        self.assertEqual(self.store.snapshot()["phase"], "working")
        self.assertEqual([item["session_id"] for item in self.store.list_sessions()
                          if item["followed"]], ["chat-b"])

    # self: Two prompt hooks whose process delivery order differs from emission order.
    # Raises AssertionError if a delayed older prompt reverses the followed chat.
    def test_delayed_prompt_cannot_reverse_newer_chat(self) -> None:
        self.event_time = 9
        self.observe("UserPromptSubmit", session="chat-b")
        followed_chat = self.store.snapshot()["chat"]
        self.event_time = 4
        self.observe("UserPromptSubmit", session="chat-a")
        self.assertEqual(self.store.snapshot()["chat"], followed_chat)
        self.event_time = 9
        with self.assertRaisesRegex(ValueError, "ambiguous prompt timestamp"):
            self.observe("UserPromptSubmit", session="chat-c")
        self.assertEqual(len(self.store.list_sessions()), 2)

    # self: Two consecutive turns in the same selected chat.
    # Raises AssertionError if a late previous-turn Stop completes a newer turn.
    def test_old_turn_completion_cannot_finish_current_turn(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("UserPromptSubmit", turn="turn-b")
        with self.assertRaisesRegex(ValueError, "turn does not match"):
            self.observe("Stop")
        self.assertEqual(self.store.snapshot()["phase"], "working")

    # self: Selected active turn with a synthetic monotonic lease.
    # Raises AssertionError if a PC heartbeat or wall time keeps activity current.
    def test_active_observation_expires_without_inventing_success(self) -> None:
        with patch.object(activity.time, "monotonic", return_value=100.0):
            self.observe("UserPromptSubmit")
        with patch.object(activity.time, "monotonic", return_value=221.0):
            self.assertEqual(self.store.snapshot()["phase"], "unavailable")
        self.observe("Stop")
        self.assertEqual(self.store.snapshot()["phase"], "ready")

    # self: Concurrent command/edit operations on one turn.
    # Raises AssertionError if finishing one operation erases another live operation.
    def test_concurrent_tools_retain_identity(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("PreToolUse", tool_use_id="tool-command", tool_name="Bash")
        self.observe("PreToolUse", tool_use_id="tool-edit", tool_name="apply_patch")
        self.observe("PostToolUse", tool_use_id="tool-edit")
        self.assertEqual(self.store.snapshot()["phase"], "running")
        self.observe("Interrupt")
        self.observe("PostToolUse", tool_use_id="tool-command")
        self.assertEqual(self.store.snapshot()["phase"], "interrupted")
        with self.assertRaisesRegex(ValueError, "Interrupted turn"):
            self.observe("Stop")

    # self: Nested and concurrently active native tools, including two distinct IDs with the same friendly label.
    # Raises AssertionError if display aliases change phase, merge identities or fail to restore the remaining tool.
    def test_friendly_tool_descriptions_follow_nested_and_concurrent_original_ids(self) -> None:
        self.observe("UserPromptSubmit")
        first_activity, first_technical = self.store.snapshot_bundle()
        steps = (
            ("PreToolUse", "shell-id", "functions.exec_command", "running", "Tool: Run command", {"shell-id"}),
            ("PreToolUse", "pr-id-a", "mcp__codex_apps__github_fetch_pr", "working", "Tool: GitHub: read pull request",
             {"shell-id", "pr-id-a"}),
            ("PreToolUse", "pr-id-b", "mcp__codex_apps__github_get_pr_info", "working", "Tool: GitHub: read pull request",
             {"shell-id", "pr-id-a", "pr-id-b"}),
            ("PostToolUse", "pr-id-a", None, "working", "Tool: GitHub: read pull request", {"shell-id", "pr-id-b"}),
            ("PreToolUse", "edit-id", "functions.apply_patch", "editing", "Tool: Edit files",
             {"shell-id", "pr-id-b", "edit-id"}),
            ("PostToolUse", "edit-id", None, "working", "Tool: GitHub: read pull request", {"shell-id", "pr-id-b"}),
            ("PostToolUse", "pr-id-b", None, "running", "Tool: Run command", {"shell-id"}),
            ("PostToolUse", "shell-id", None, "working", "", set()),
        )
        for event, identity, name, phase, description, active_ids in steps:
            with self.subTest(event=event, identity=identity):
                fields = {"tool_use_id": identity}
                if name is not None:
                    fields["tool_name"] = name
                self.observe(event, **fields)
                current, technical = self.store.snapshot_bundle()
                self.assertEqual(current["phase"], phase)
                self.assertEqual(bytes.fromhex(current["description"]).decode("ascii"), description)
                self.assertEqual(current["chat"], first_activity["chat"])
                self.assertEqual(technical["turn"], first_technical["turn"])
                session = self.store._sessions["chat-a"]
                self.assertEqual(set(session.tools), active_ids)
                self.assertEqual(set(session.tool_labels), active_ids)
                self.assertNotIn("mcp__", description)
                self.assertNotIn("functions.", description)

    # self: Two real-schema synthetic chat lifecycles with independently mapped tool descriptions.
    # Raises AssertionError if a background tool changes the followed chat or current friendly label.
    def test_background_tool_label_cannot_replace_the_followed_chat_activity(self) -> None:
        self.observe("UserPromptSubmit", session="chat-background")
        self.observe("UserPromptSubmit")
        self.observe("PreToolUse", tool_use_id="visible-id", tool_name="mcp__codex_apps__github_get_pr_diff")
        current = self.store.snapshot()
        self.observe("PreToolUse", session="chat-background", tool_use_id="background-id",
                     tool_name="mcp__custom_server__github_fetch_pr")
        background = self.store._sessions["chat-background"]
        self.assertEqual(background.tool_labels["background-id"], "Tool: custom server: github fetch pr")
        observed = self.store.snapshot()
        self.assertEqual((observed["chat"], observed["phase"], observed["description"]),
                         (current["chat"], current["phase"], current["description"]))
        self.assertEqual(bytes.fromhex(observed["description"]), b"Tool: GitHub: read pull request changes")

    # self: Invalid native source data while one genuine tool remains active.
    # Raises AssertionError if rejected labels create a tool entry, consume its identity or mutate the existing activity.
    def test_invalid_tool_label_source_preserves_current_owner_state(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("PreToolUse", tool_use_id="current-id", tool_name="functions.exec_command")
        session = self.store._sessions["chat-a"]
        before = (dict(session.tools), dict(session.tool_labels), session.phase, session.last_event_ns)
        for name in (None, "", "X" * 257, "private\nname", "private\u00e9name"):
            with self.subTest(kind=type(name).__name__), self.assertRaises(ValueError):
                self.observe("PreToolUse", tool_use_id="rejected-id", tool_name=name)
            self.assertEqual((session.tools, session.tool_labels, session.phase, session.last_event_ns), before)
        self.observe("PreToolUse", tool_use_id="rejected-id", tool_name="mcp__custom_server__read_widget")
        self.assertEqual(session.tool_labels["rejected-id"], "Tool: custom server: read widget")

    # self: Followed chat with a rejected new prompt from a different chat.
    # Raises AssertionError if rejected metadata changes the followed context.
    def test_invalid_prompt_preserves_previous_context(self) -> None:
        self.observe("UserPromptSubmit")
        previous = self.store.snapshot()
        with self.assertRaises(ValueError):
            self.observe("UserPromptSubmit", session="chat-b", turn="")
        self.assertEqual(self.store.snapshot(), previous)

    # self: Unicode and long workspace names that are labels, never chat identities.
    # Raises AssertionError if display projection loses identity or violates the wire bound.
    def test_automatic_project_label_is_bounded_ascii(self) -> None:
        self.observe("UserPromptSubmit", cwd="E:/a-very-long-workspace-name-\u00e9")
        first = self.store.snapshot()
        label = bytes.fromhex(first["project"]).decode("ascii")
        self.assertEqual(label, "UNAVAILABLE")
        self.observe("UserPromptSubmit", session="chat-b", cwd="E:/a-very-long-workspace-name-\u00e9")
        self.assertEqual(self.store.snapshot()["project"], first["project"])
        self.assertNotEqual(self.store.snapshot()["chat"], first["chat"])
        self.observe("UserPromptSubmit", cwd="E:/" + "P" * 64)
        self.assertEqual(bytes.fromhex(self.store.snapshot()["project"]), b"P" * 64)

    # self: Synthetic authenticated version-four peer with the full reported model identity.
    # Raises AssertionError if technical metadata loses activity correlation or admits invented token counts.
    def test_technical_reply_matches_activity_and_rejects_unobserved_metrics(self) -> None:
        self.observe("UserPromptSubmit", model="M" * 128)
        self.observe("PreCompact", trigger="auto")
        state = Mock(pc_id="a" * 32)
        link_key = bytes(range(32))
        state.retained_keys.return_value = ccp.RetainedKeys(link_key, None)
        listener = Mock()
        responder = ccp.CompanionResponder(state, listener, self.store)
        values = ["abcdef012345", state.pc_id, "b" * 32]
        proof = ccp.make_proof(link_key, "find-activity-v4", *values)
        find = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values)) +
                                   [("activity", "4"), ("proof", proof)])
        responder._handle_datagram(find, ("192.0.2.10", ccp.DEVICE_PORT))
        self.assertEqual([ccp.parse_datagram(call.args[0])[0] for call in listener.sendto.call_args_list], ["READY"])
        acknowledge_ready(responder, link_key, ("192.0.2.10", ccp.DEVICE_PORT))
        packets = [ccp.parse_datagram(call.args[0]) for call in listener.sendto.call_args_list]
        self.assertEqual([kind for kind, _ in packets], ["READY", "ACTIVITY", "TECH"])
        activity_fields, technical_fields = packets[1][1], packets[2][1]
        self.assertEqual(technical_fields["chat"], activity_fields["chat"])
        self.assertEqual(technical_fields["revision"], activity_fields["revision"])
        self.assertEqual(technical_fields["nonce"], values[2])
        authenticated = [technical_fields[name] for name in technical_metadata.FIELDS]
        self.assertEqual(technical_fields["proof"], ccp.make_proof(link_key, "technical-v2", *authenticated))
        self.assertLessEqual(len(listener.sendto.call_args_list[2].args[0]), 1024)
        projected = technical_metadata.decode_projection(technical_fields)
        self.assertEqual(projected.model, "M" * 128)
        self.assertEqual(projected.compaction, technical_metadata.CompactionState.ACTIVE)
        self.assertIsNone(projected.context_used)
        self.assertIsNone(projected.context_capacity)
        invalid = dict(technical_fields)
        invalid["used"] = "12000"
        with self.assertRaisesRegex(ValueError, "original source age"):
            technical_metadata.decode_projection(invalid)
        observed = dict(technical_fields, used="0", capacity="128000", cumulative="18446744073709551615",
                        input="0", output="0", usageage="119999", effort="high".encode("ascii").hex())
        decoded = technical_metadata.decode_projection(observed)
        self.assertEqual((decoded.context_used, decoded.context_capacity, decoded.cumulative_tokens,
                          decoded.input_tokens, decoded.output_tokens, decoded.usage_age_ms, decoded.effort),
                         (0, 128000, 0xffffffffffffffff, 0, 0, 119999, "high"))
        partial = dict(observed, used="-", capacity="-")
        self.assertIsNone(technical_metadata.decode_projection(partial).context_used)
        self.assertEqual(technical_metadata.decode_projection(partial).cumulative_tokens, 0xffffffffffffffff)
        over_limit = dict(observed, used="128001")
        self.assertEqual(technical_metadata.decode_projection(over_limit).context_used, 128001)
        for name, value in (("used", "18446744073709551616"), ("used", "01"), ("used", "-1"),
                            ("used", "+1"), ("input", " 0"), ("capacity", "0"), ("usageage", "-"),
                            ("turn", "0" * 64), ("effort", "00"), ("effort", "gg")):
            with self.subTest(field=name, value=value), self.assertRaises(ValueError):
                technical_metadata.decode_projection(dict(observed, **{name: value}))
        invalid = dict(technical_fields)
        invalid["modelage"] = "-"
        with self.assertRaisesRegex(ValueError, "incoherent"):
            technical_metadata.decode_projection(invalid)

    # self: Authenticated synthetic responder and maximum display label.
    # Raises AssertionError if ACTIVITY lacks nonce/HMAC binding or exceeds the wire limit.
    def test_activity_reply_authenticates_every_field(self) -> None:
        self.observe("UserPromptSubmit", cwd="E:/" + "P" * 24)
        state = Mock(pc_id="a" * 32)
        link_key = bytes(range(32))
        state.retained_keys.return_value = ccp.RetainedKeys(link_key, None)
        listener = Mock()
        responder = ccp.CompanionResponder(state, listener, self.store)
        values = ["abcdef012345", state.pc_id, "b" * 32]
        proof = ccp.make_proof(link_key, "find-activity", *values)
        find = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values)) +
                                   [("activity", "1"), ("proof", proof)])
        responder._handle_datagram(find, ("192.0.2.10", ccp.DEVICE_PORT))
        acknowledge_ready(responder, link_key, ("192.0.2.10", ccp.DEVICE_PORT))
        raw, _ = listener.sendto.call_args.args
        kind, fields = ccp.parse_datagram(raw)
        self.assertEqual(kind, "ACTIVITY")
        self.assertLessEqual(len(raw), 512)
        authenticated = [fields[key] for key in ("device", "pc", "nonce", "chat", "revision", "phase", "project")]
        expected = ccp.make_proof(link_key, "activity", *authenticated)
        self.assertEqual(fields["proof"], expected)
        self.assertEqual(fields["nonce"], values[2])
        self.assertEqual(bytes.fromhex(fields["project"]), b"P" * 24)
        listener.reset_mock()
        # Stripping the capability cannot reuse its proof as a presence-only FIND.
        stripped = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values)) + [("proof", proof)])
        with self.assertRaises(ValueError):
            responder._handle_datagram(stripped, ("192.0.2.10", ccp.DEVICE_PORT))
        listener.sendto.assert_not_called()

    # self: Ended followed chat and a new broker without stored activity metadata.
    # Raises AssertionError if an old chat is guessed or a new prompt needs manual binding.
    def test_ended_chat_and_restart_wait_for_a_genuine_prompt(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("SessionEnd")
        self.assertEqual(self.store.snapshot()["phase"], "ended")
        self.observe("SessionStart", session="chat-b")
        self.assertEqual(self.store.snapshot()["phase"], "ended")
        restarted = activity.ActivityStore()
        self.assertEqual(restarted.list_sessions(), [])
        self.assertEqual(restarted.snapshot()["phase"], "none")
        self.store = restarted
        self.observe("UserPromptSubmit", session="chat-b")
        self.assertEqual(self.store.snapshot()["phase"], "working")

    # self: Public MCP API, including authenticated settings and progress, without manual chat selection.
    # Raises AssertionError if the obsolete selection tools remain callable or advertised.
    def test_manual_selection_is_removed_from_public_tools(self) -> None:
        names = {tool["name"] for tool in ccp.tool_catalog()}
        self.assertEqual(names, {"ccp_discover_devices", "ccp_pair_device",
                                 "ccp_connection_status", "ccp_list_sessions", "ccp_ask_question",
                                 "ccp_publish_notification", "ccp_poll_request", "ccp_cancel_request",
                                 "ccp_reset_device", "ccp_factory_reset_device", "ccp_debug_screen",
                                 "ccp_read_clock", "ccp_configure_clock", "ccp_report_progress",
                                 "ccp_read_name", "ccp_set_name", "ccp_audio_status"})
        for name in ("ccp_select_session", "ccp_clear_session"):
            with self.assertRaisesRegex(ValueError, "Unknown CCP tool"):
                ccp.call_tool(Mock(), name, {})

    # self: Bounded same-user control requests represented without opening sockets.
    # Raises AssertionError if releasing one transport drops another's lease.
    def test_shared_runtime_releases_only_its_own_lease(self) -> None:
        server = Mock()
        server.lease_lock = threading.Lock()
        server.service.responder.app_presence = app_presence.AppPresence()
        runtime.dispatch(server, "lease", {"id": "a" * 32})
        runtime.dispatch(server, "lease", {"id": "a" * 32})
        runtime.dispatch(server, "lease", {"id": "b" * 32})
        runtime.dispatch(server, "release", {"id": "a" * 32, "normal": True})
        self.assertEqual(server.service.responder.app_presence.snapshot().state, app_presence.AppState.OPEN)
        runtime.dispatch(server, "release", {"id": "b" * 32, "normal": True})
        self.assertEqual(server.service.responder.app_presence.snapshot().state, app_presence.AppState.CLOSED)
        self.assertEqual(server.pricing.start_daily_refresh.call_count, 2)

    # self: Mocked Winsock reservation with no real socket or state access.
    # Raises AssertionError if availability depends on a failed connection attempt.
    def test_local_startup_uses_exclusive_port_ownership(self) -> None:
        with patch.object(windows.socket, "socket") as factory:
            reservation = factory.return_value.__enter__.return_value
            with patch.object(windows.socket, "create_connection") as connect:
                self.assertTrue(runtime.control_port_available())
            connect.assert_not_called()
            reservation.bind.assert_called_once_with(("127.0.0.1", runtime.CONTROL_PORT))
            factory.return_value.__exit__.assert_called_once()

    # self: An already occupied control address represented by the original socket error.
    # Raises AssertionError if another owner is treated as an available address.
    def test_occupied_control_port_preserves_existing_owner(self) -> None:
        with patch.object(windows.socket, "socket") as factory:
            reservation = factory.return_value.__enter__.return_value
            reservation.bind.side_effect = OSError(errno.EADDRINUSE, "synthetic occupied address")
            self.assertFalse(runtime.control_port_available())

    # self: A denied reservation whose cause cannot establish port availability.
    # Raises AssertionError if an ambiguous access failure admits a new broker.
    def test_control_port_access_failure_is_not_absence(self) -> None:
        failure = OSError(errno.EACCES, "synthetic denied reservation")
        with patch.object(windows.socket, "socket") as factory:
            factory.return_value.__enter__.return_value.bind.side_effect = failure
            with self.assertRaises(OSError) as raised:
                runtime.control_port_available()
            self.assertIs(raised.exception, failure)

    # self: Hook ingestion fixture containing private text that must never reach the broker.
    # Raises AssertionError if ingestion forwards contents rather than the native metadata locator.
    def test_hook_ingestion_discards_private_contents(self) -> None:
        original = {"hook_event_name": "UserPromptSubmit", "session_id": "chat-a",
                    "turn_id": "turn-a", "cwd": "E:/synthetic-project", "prompt": "private prompt",
                    "transcript_path": "E:/synthetic-home/sessions/fixture.jsonl", "tool_input": {"secret": "private"},
                    "last_assistant_message": "private response", "model": "reported-model", "trigger": "auto"}
        input_stream = Mock(buffer=io.BytesIO(json.dumps(original).encode("utf-8")))
        with patch.object(runtime.sys, "stdin", input_stream), contextlib.redirect_stdout(io.StringIO()):
            with patch.object(runtime, "ensure_runtime", return_value="synthetic"), patch.object(runtime, "exchange") as exchange, \
                    patch.object(runtime, "read_branch", return_value="synthetic-branch"):
                runtime.run_hook()
        submitted = exchange.call_args.args[1]
        self.assertEqual(set(submitted), {"hook_event_name", "session_id", "turn_id", "cwd", "emitted_ns", "branch", "model", "trigger", "transcript_path"})
        self.assertEqual(submitted["transcript_path"], original["transcript_path"])
        self.assertEqual(submitted["model"], "reported-model")
        self.assertEqual(submitted["trigger"], "auto")


class HookRoutingTests(unittest.TestCase):
    # self: Real activity/ticket owners with synthetic peers, clocks and no live service constructor.
    def setUp(self) -> None:
        self.device = "abcdef012345"
        self.key = bytes(range(32))
        self.service = ccp.CcpService.__new__(ccp.CcpService)
        self.service.activity = activity.ActivityStore()
        self.service.interactions = interactions.InteractionStore(lambda: 100.0)
        self.service.notifications_error = None
        self.service._hook_lock = threading.Lock()
        self.service.state = Mock()
        self.service.state.public_status.return_value = [{
            "device_id": self.device, "retained_pairing": True, "source": "LAN", "pairing_pending": False}]
        self.service.state.retained_keys.return_value = ccp.RetainedKeys(self.key, None)
        self.service.responder = Mock()
        self.service.responder.supports_events.return_value = True
        self.event_ns = 100_000_000_000
        self.usage = patch.object(self.service.activity._usage, "poll",
                                  side_effect=AssertionError("Hook routing must not read a native journal"))
        self.usage.start()
        self.addCleanup(self.usage.stop)
        title_read = patch.object(self.service.activity._titles, "read_once",
                                  return_value=native_title.ChatTitleObservation())
        self.title_read = title_read.start()
        self.addCleanup(title_read.stop)
        enqueue = patch.object(self.service.interactions, "enqueue", wraps=self.service.interactions.enqueue)
        self.enqueue = enqueue.start()
        self.addCleanup(enqueue.stop)

    # self: Isolated service fixture with monotonically ordered native hook metadata.
    # name: Documented lifecycle hook identity.
    # session: Opaque native session identity, never a workspace-derived identity.
    # turn: Exact native turn identity.
    # Raises ValueError if production correlation rejects the fixture event.
    def observe(self, name: str, session: str = "chat-a", turn: str = "turn-a") -> dict:
        self.event_ns += 1
        event = {"hook_event_name": name, "session_id": session, "turn_id": turn,
                 "cwd": "E:/same-long-project-name", "emitted_ns": self.event_ns}
        with patch.object(activity.time, "monotonic_ns", return_value=self.event_ns + 1):
            return self.service.observe_hook(event)

    # self: Strict documented hook output, including unavailable and maliciously truthy broker values.
    # Raises AssertionError if any hook except an eligible prompt adds context or leaks private contents.
    def test_prompt_additional_context_requires_explicit_physical_availability(self) -> None:
        for hook, available in (("UserPromptSubmit", True), ("UserPromptSubmit", False),
                                ("UserPromptSubmit", "true"), ("UserPromptSubmit", 1),
                                ("PermissionRequest", True), ("Stop", True)):
            with self.subTest(hook=hook, available=available):
                original = {"hook_event_name": hook, "session_id": "private-chat-id", "turn_id": "turn-a",
                            "cwd": "E:/fixture", "prompt": "PRIVATE PROMPT",
                            "last_assistant_message": "PRIVATE RESPONSE", "tool_input": {"key": "PRIVATE KEY"}}
                output = io.StringIO()
                stream = Mock(buffer=io.BytesIO(json.dumps(original).encode("utf-8")))
                with patch.object(runtime.sys, "stdin", stream), contextlib.redirect_stdout(output), \
                        patch.object(runtime, "ensure_runtime", return_value="fixture"), \
                        patch.object(runtime, "control_secret", return_value="fixture"), \
                        patch.object(runtime, "read_branch", return_value=""), \
                        patch.object(runtime, "exchange", return_value={"physical_question_routing": available}) as exchange:
                    runtime.run_hook()
                actual = json.loads(output.getvalue())
                if hook == "UserPromptSubmit" and available is True:
                    self.assertEqual(actual, {"hookSpecificOutput": {
                        "hookEventName": "UserPromptSubmit", "additionalContext": runtime.QUESTION_ROUTING_CONTEXT}})
                    context = actual["hookSpecificOutput"]["additionalContext"]
                    for term in ("ccp_ask_question", "ccp_poll_request", "answer_id", "delivered", "PC-only"):
                        self.assertIn(term, context)
                else:
                    self.assertEqual(actual, {})
                for private in ("PRIVATE PROMPT", "PRIVATE RESPONSE", "PRIVATE KEY", "private-chat-id"):
                    self.assertNotIn(private, output.getvalue())
                self.assertNotIn("prompt", exchange.call_args.args[1])
                self.assertNotIn("tool_input", exchange.call_args.args[1])
                self.assertNotIn("last_assistant_message", exchange.call_args.args[1])

    # self: Retained state alone, simulated observations and stale extension support cannot enable routing.
    # Raises AssertionError if a physical-only hook opts a simulator or unavailable peer in implicitly.
    def test_question_routing_requires_recent_retained_physical_peer(self) -> None:
        cases = (("LAN", True, False, True, True), ("LAN", False, False, True, False),
                 ("LAN", True, True, True, False), ("LAN", True, False, False, False),
                 ("SIMULATED", True, False, True, False), ("UNOBSERVED", True, False, True, False))
        for index, (source, retained, pending, supported, expected) in enumerate(cases):
            with self.subTest(source=source, retained=retained, pending=pending, supported=supported):
                self.service.state.public_status.return_value[0].update(
                    source=source, retained_pairing=retained, pairing_pending=pending)
                self.service.responder.supports_events.return_value = supported
                self.assertEqual(self.observe("UserPromptSubmit", turn=f"turn-{index}"),
                                 {"physical_question_routing": expected})
        self.enqueue.assert_not_called()

    # self: Background completion alongside a real recipient-owned question and unrelated chat prompts.
    # Raises AssertionError if a notice steals focus, erases a question or repeats on a duplicate Stop.
    def test_background_reply_notice_preserves_question_focus_and_other_chat_lifetime(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("UserPromptSubmit", "chat-b", "turn-b")
        question = interactions.EventPayload(interactions.EventKind.QUESTION, "Actual caller", "Choose cache",
            "Which cache should I use?", (
                interactions.Choice("memory", "Memory", "Keep it in memory.", interactions.Tone.POSITIVE),
                interactions.Choice("disk", "Disk", "Keep it on disk.", interactions.Tone.INFO)))
        association = hashlib.sha256(self.key).hexdigest()
        created = self.service.interactions.enqueue(self.device, question, "LAN", association_id=association)
        self.enqueue.reset_mock()
        self.observe("Stop")
        self.enqueue.assert_called_once()
        call = self.enqueue.call_args
        notice_payload = call.args[1]
        self.assertEqual((notice_payload.kind, notice_payload.title, notice_payload.body),
                         (interactions.EventKind.REPLY_READY, "Reply ready", "Open the other chat in Codex"))
        self.assertNotEqual(notice_payload.title, notice_payload.body)
        self.title_read.assert_called_once_with("chat-a", "E:/same-long-project-name")
        self.assertEqual(call.kwargs["native_session"], "chat-a")
        self.assertNotIn(hashlib.sha256(b"chat-a").hexdigest()[:8], notice_payload.context)
        self.assertEqual(notice_payload.context, "same-long-project-name")
        self.assertLessEqual(len(notice_payload.context), 24)
        self.assertNotIn("success", notice_payload.body.lower())
        active = self.service.interactions.snapshot(self.device, association)
        self.assertEqual(active[0], created["request_id"])
        self.assertEqual([item["session_id"] for item in self.service.activity.list_sessions() if item["followed"]],
                         ["chat-b"])
        notices = [ticket for ticket in self.service.interactions._tickets.values()
                   if ticket.payload.kind == interactions.EventKind.REPLY_READY]
        self.assertEqual(len(notices), 1)
        notice = notices[0]
        self.observe("UserPromptSubmit", "chat-c", "turn-c")
        self.observe("Stop")
        self.enqueue.assert_called_once()
        self.title_read.assert_called_once()
        self.assertEqual(self.service.interactions.poll(notice.request_id, notice.capability)["state"], "pending")
        self.observe("UserPromptSubmit", "chat-a", "turn-a-next")
        self.assertEqual(self.service.interactions.poll(notice.request_id, notice.capability)["state"], "cancelled")
        self.assertEqual(self.service.interactions.poll(created["request_id"], created["capability"])["state"], "pending")

    # self: Current-chat completions and stale native turns retain the existing identity checks.
    # Raises AssertionError if completion notices are inferred or accepted for the wrong native turn.
    def test_only_real_background_stop_can_publish_reply_ready(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("Stop")
        self.enqueue.assert_not_called()
        self.title_read.assert_not_called()
        self.observe("UserPromptSubmit", turn="turn-next")
        self.observe("UserPromptSubmit", "chat-b", "turn-b")
        with self.assertRaisesRegex(ValueError, "current chat turn"):
            self.observe("Stop")
        self.enqueue.assert_not_called()
        self.observe("Stop", turn="turn-next")
        self.enqueue.assert_called_once()
        self.title_read.assert_called_once()

    # self: Genuine background lifecycle with explicitly injected canonical title observations.
    # Raises AssertionError if absent, expired or oversized titles are guessed, clipped or duplicated.
    def test_reply_ready_uses_complete_current_title_or_distinct_fallback(self) -> None:
        fallback = "Open the other chat in Codex"
        current = native_title.TitleState.CURRENT
        cases = (
            (native_title.ChatTitleObservation("SIMULATED real chat", current, 100.0), "SIMULATED real chat"),
            (native_title.ChatTitleObservation("T" * 63, current, 100.0), "T" * 63),
            (native_title.ChatTitleObservation("T" * 64, current, 100.0), fallback),
            (native_title.ChatTitleObservation("Expired title", current, 55.0), fallback),
            (native_title.ChatTitleObservation("Future title", current, 101.0), fallback),
            (native_title.ChatTitleObservation(), fallback),
            (native_title.ChatTitleObservation(state=native_title.TitleState.UNSUPPORTED), fallback),
            (native_title.ChatTitleObservation(state=native_title.TitleState.ERROR), fallback),
        )
        with patch.object(activity.time, "monotonic", return_value=100.0):
            for index, (title, expected) in enumerate(cases):
                with self.subTest(state=title.state, title_length=len(title.title)):
                    self.observe("UserPromptSubmit", turn=f"turn-{index}")
                    self.observe("UserPromptSubmit", "chat-b", f"other-{index}")
                    self.enqueue.reset_mock()
                    self.title_read.reset_mock()
                    self.title_read.return_value = title
                    self.observe("Stop", turn=f"turn-{index}")
                    self.title_read.assert_called_once_with("chat-a", "E:/same-long-project-name")
                    self.enqueue.assert_called_once()
                    payload = self.enqueue.call_args.args[1]
                    self.assertEqual((payload.kind, payload.title, payload.body),
                                     (interactions.EventKind.REPLY_READY, "Reply ready", expected))
                    self.assertEqual(payload.choices, ())
                    self.assertFalse(payload.progress_valid)
                    self.assertEqual([item["session_id"] for item in self.service.activity.list_sessions()
                                      if item["followed"]], ["chat-b"])

    # self: Accepted completion whose native title read races a later genuine user prompt.
    # Raises AssertionError if an obsolete completion changes focus or publishes the previous turn's title.
    def test_background_title_refresh_rechecks_lifecycle_after_source_io(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("UserPromptSubmit", "chat-b", "turn-b")

        # session: Exact completion identity requested by the production title owner.
        # cwd: Original source checkout, retained unchanged during the race.
        # Returns stale completion metadata after accepting a newer native turn.
        def new_turn_during_read(session, cwd):
            self.event_ns += 1
            with patch.object(activity.time, "monotonic_ns", return_value=self.event_ns + 1):
                self.service.activity.observe({"hook_event_name": "UserPromptSubmit", "session_id": session,
                    "turn_id": "turn-next", "cwd": cwd, "emitted_ns": self.event_ns})
            return native_title.ChatTitleObservation("Obsolete completion title", native_title.TitleState.CURRENT,
                                                    activity.time.monotonic())

        self.title_read.side_effect = new_turn_during_read
        with patch.object(activity.time, "monotonic", return_value=100.0):
            self.observe("Stop")
            self.title_read.assert_called_once()
            self.enqueue.assert_not_called()
            followed = next(item for item in self.service.activity.list_sessions() if item["followed"])
        self.assertEqual((followed["session_id"], followed["turn_id"], followed["phase"]),
                         ("chat-a", "turn-next", "working"))

    # self: A genuine background completion with no eligible physical Cody recipient.
    # Raises AssertionError if a completion probes native metadata without a delivery target.
    def test_background_title_refresh_requires_an_eligible_physical_target(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("UserPromptSubmit", "chat-b", "turn-b")
        self.service.responder.supports_events.return_value = False
        self.observe("Stop")
        self.title_read.assert_not_called()
        self.enqueue.assert_not_called()

    # self: Native approval and optional notice failure remain separate from native authorization.
    # Raises AssertionError if a failure invents a decision, loses its cause or reads the journal.
    def test_native_approval_stays_on_pc_and_delivery_error_is_observable(self) -> None:
        self.observe("UserPromptSubmit")
        self.observe("PermissionRequest")
        payload = self.enqueue.call_args.args[1]
        self.assertEqual((payload.kind, payload.title, payload.body),
                         (interactions.EventKind.APPROVAL_ON_PC, "Approval needed", "Continue on your PC"))
        self.assertEqual(payload.choices, ())
        self.observe("UserPromptSubmit", "chat-b", "turn-b")
        self.enqueue.side_effect = ValueError("synthetic queue full")
        result = self.observe("Stop")
        self.assertEqual(result, {"notification_error": "synthetic queue full"})
        self.assertEqual(self.service.notifications_error, "synthetic queue full")
        self.assertEqual(next(item for item in self.service.activity.list_sessions()
                              if item["session_id"] == "chat-a")["phase"], "ready")
        self.assertNotIn("decision", result)
        self.observe("Stop")
        self.assertEqual(self.service.notifications_error, "synthetic queue full")


class AppPresenceTests(unittest.TestCase):
    # self: Pure lease owner with explicit clocks; no thread, network, OS process or native state.
    # Raises AssertionError if independent clients or final normal closure lose their evidence.
    def test_last_normal_transport_close_is_distinct_from_a_chat_end(self):
        owner = app_presence.AppPresence()
        self.assertEqual(owner.snapshot(1).reason, "unobserved")
        self.assertTrue(owner.lease("a", 1))
        self.assertFalse(owner.lease("a", 2))
        self.assertTrue(owner.lease("b", 3))
        owner.release("b", True, 4)
        self.assertEqual(owner.snapshot(5), app_presence.PresenceObservation(app_presence.AppState.OPEN, "none", 3000))
        with self.assertRaisesRegex(ValueError, "no live owned lease"):
            owner.release("unknown", True, 5)
        self.assertEqual(owner.snapshot(5).state, app_presence.AppState.OPEN)
        server = Mock()
        server.lease_lock = threading.Lock()
        server.service.responder.app_presence = owner
        runtime.dispatch(server, "hook", {"hook_event_name": "SessionEnd", "session_id": "synthetic-chat"})
        self.assertEqual(owner.snapshot(5).state, app_presence.AppState.OPEN)
        owner.release("a", True, 6)
        self.assertEqual(owner.snapshot(7), app_presence.PresenceObservation(app_presence.AppState.CLOSED, "none", 1000))
        owner.lease("c", 8)
        self.assertEqual(owner.snapshot(8).state, app_presence.AppState.OPEN)

    # self: Expired and failed transport evidence, including another still-live client.
    # Raises AssertionError if loss, late cleanup or a subsequent release manufactures Closed.
    def test_expiry_and_abnormal_release_remain_unavailable(self):
        owner = app_presence.AppPresence()
        owner.lease("a", 1)
        self.assertEqual(owner.snapshot(60.999).state, app_presence.AppState.OPEN)
        self.assertEqual(owner.snapshot(61), app_presence.PresenceObservation(app_presence.AppState.UNAVAILABLE, "expired"))
        with self.assertRaisesRegex(ValueError, "no live owned lease"):
            owner.release("a", True, 62)
        owner.lease("b", 63)
        owner.release("b", True, 64)
        self.assertEqual(owner.snapshot(64).state, app_presence.AppState.UNAVAILABLE)
        owner = app_presence.AppPresence()
        owner.lease("a", 1)
        owner.lease("b", 2)
        owner.release("a", False, 3)
        self.assertEqual(owner.snapshot(4).state, app_presence.AppState.OPEN)
        owner.release("b", True, 5)
        self.assertEqual(owner.snapshot(5), app_presence.PresenceObservation(app_presence.AppState.UNAVAILABLE, "transport"))

    # self: The exact lease budget and renewal boundary, without waiting on a wall clock.
    # Raises AssertionError if renewal becomes a second client or the owner grows without a bound.
    def test_lease_budget_and_renewal_are_bounded(self):
        owner = app_presence.AppPresence()
        for index in range(app_presence.MAX_TRANSPORTS):
            owner.lease(str(index), 1)
        with self.assertRaisesRegex(ValueError, "limit"):
            owner.lease("overflow", 2)
        owner.lease("0", 60)
        self.assertEqual(owner.snapshot(61), app_presence.PresenceObservation(app_presence.AppState.OPEN, "none", 1000))
        owner.release("0", True, 62)
        self.assertEqual(owner.snapshot(62).state, app_presence.AppState.UNAVAILABLE)
        with self.assertRaisesRegex(ValueError, "clock"):
            owner.snapshot(float("nan"))

    # self: Real stdio lifecycle with synthetic streams and a mocked local broker transport.
    # Raises AssertionError if an exceptional exit is released as normal EOF or hides its original cause.
    def test_stdio_distinguishes_eof_from_processing_failure(self):
        client = Mock()
        with patch.object(runtime, "BrokerClient", return_value=client), patch.object(ccp.sys, "stdin", io.StringIO("")):
            self.assertEqual(ccp.run_stdio(), 0)
        client.close.assert_called_once_with(normal=True)
        client.reset_mock()
        original = RuntimeError("synthetic broker transport failure")
        client.request.side_effect = original
        client.close.side_effect = OSError("synthetic release failure")
        with patch.object(runtime, "BrokerClient", return_value=client), \
                patch.object(ccp.sys, "stdin", io.StringIO('{"method":"initialize"}\n')), \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(RuntimeError) as raised:
                ccp.run_stdio()
        self.assertIs(raised.exception, original)
        client.close.assert_called_once_with(normal=False)

    # self: Current and already-loaded development client releases, without HTTP or UDP.
    # Raises AssertionError if an old release gains normal-close evidence or malformed fields retire a client.
    def test_old_release_is_abnormal_and_new_normal_requires_boolean(self):
        server = Mock()
        server.lease_lock = threading.Lock()
        server.service.responder.app_presence = app_presence.AppPresence()
        for fields in ({"id": "a" * 32, "normal": "true"},
                       {"id": "a" * 32, "normal": 1}, {"id": "a" * 32, "normal": True, "extra": 1}):
            with self.assertRaises(ValueError):
                runtime.dispatch(server, "release", fields)
        self.assertEqual(server.service.responder.app_presence.snapshot().state, app_presence.AppState.UNAVAILABLE)
        runtime.dispatch(server, "lease", {"id": "a" * 32})
        diagnostics = io.StringIO()
        with contextlib.redirect_stderr(diagnostics):
            runtime.dispatch(server, "release", {"id": "a" * 32})
        self.assertIn("lacks normal EOF evidence", diagnostics.getvalue())
        self.assertEqual(server.service.responder.app_presence.snapshot().state, app_presence.AppState.UNAVAILABLE)
        with self.assertRaisesRegex(ValueError, "no live owned lease"):
            server.service.responder.app_presence.release("a" * 32, True)


class AppPresenceWireTests(unittest.TestCase):
    # self: Actual encoder/responder with mocked socket and synthetic retained pairing.
    def setUp(self):
        self.device = "abcdef012345"
        self.key = bytes(range(32))
        self.state = Mock(pc_id="a" * 32)
        self.state.retained_keys.return_value = ccp.RetainedKeys(self.key, None)
        self.listener = Mock()
        self.responder = ccp.CompanionResponder(self.state, self.listener, activity.ActivityStore())
        self.source = ("127.0.0.1", ccp.DEVICE_PORT)
        self.clock = patch.object(ccp.time, "monotonic", return_value=100.0)
        self.clock.start()
        self.addCleanup(self.clock.stop)
        self.peer = ccp.EventPeer("b" * 32, self.key, self.source, 100.0)
        self.responder._event_peers[self.device] = self.peer

    # self: Synthetic capability owner retaining the existing nonce and endpoint.
    # proof: Optional forged signature used only by negative fixtures.
    # Returns one production-encoded APPCAP packet.
    def capability(self, proof=None):
        values = [self.device, self.state.pc_id, self.peer.nonce, "1"]
        return ccp.encode_datagram("APPCAP", list(zip(("device", "pc", "nonce", "app"), values))
            + [("proof", proof or ccp.make_proof(self.key, "app-cap-v1", *values))])

    # self: Current paired peer with no app capability until the explicit signed advertisement.
    # Raises AssertionError if presence broadcasts to incapable, stale, forged or wrong-source clients.
    def test_capability_binds_active_key_nonce_and_endpoint(self):
        self.responder.app_presence.lease("a", 100)
        self.responder.publish_app_presence()
        self.listener.sendto.assert_not_called()
        with self.assertRaisesRegex(ValueError, "authentication failed"):
            self.responder._handle_datagram(self.capability("0" * 64), self.source)
        with self.assertRaisesRegex(ValueError, "authentication failed"):
            self.responder._handle_datagram(self.capability(), (self.source[0], self.source[1] + 1))
        self.responder._event_peers[self.device] = replace(self.peer, nonce="c" * 32)
        with self.assertRaisesRegex(ValueError, "authentication failed"):
            self.responder._handle_datagram(self.capability(), self.source)
        self.responder._event_peers[self.device] = replace(self.peer, observed_at=89.0)
        with self.assertRaisesRegex(ValueError, "authentication failed"):
            self.responder._handle_datagram(self.capability(), self.source)
        self.responder._event_peers[self.device] = self.peer
        self.state.retained_keys.return_value = ccp.RetainedKeys(bytes(reversed(self.key)), None)
        with self.assertRaisesRegex(ValueError, "authentication failed"):
            self.responder._handle_datagram(self.capability(), self.source)
        self.listener.sendto.assert_not_called()

    # self: Actual bounded wire packets covering Open, Closed and Unavailable without a real network.
    # Raises AssertionError if sequence/proof/source-age admission or optional failure evidence is lost.
    def test_signed_transitions_preserve_age_and_retry_real_send_failure(self):
        self.responder.app_presence.lease("a", 99)
        self.responder._handle_datagram(self.capability(), self.source)
        payload, destination = self.listener.sendto.call_args.args
        kind, fields = ccp.parse_datagram(payload)
        self.assertEqual(kind, "APPSTATE")
        self.assertEqual(destination, self.source)
        self.assertLessEqual(len(payload), 512)
        self.assertEqual((fields["state"], fields["reason"], fields["age"]), ("open", "none", "1000"))
        names = ("device", "pc", "nonce", "sequence", "state", "reason", "age")
        self.assertEqual(fields["proof"], ccp.make_proof(self.key, "app-state-v1", *(fields[name] for name in names)))
        first_sequence = int(fields["sequence"], 16)
        self.responder.app_presence.release("a", True, 100)
        original = OSError("synthetic UDP send failure")
        self.listener.sendto.side_effect = original
        with contextlib.redirect_stderr(io.StringIO()):
            self.responder.publish_app_presence()
        self.assertIs(self.responder.app_presence_error, original)
        self.listener.sendto.side_effect = None
        self.responder.publish_app_presence()
        _, closed = ccp.parse_datagram(self.listener.sendto.call_args.args[0])
        self.assertEqual((closed["state"], closed["reason"], closed["age"]), ("closed", "none", "0"))
        self.assertGreater(int(closed["sequence"], 16), first_sequence)
        self.assertIsNone(self.responder.app_presence_error)
        self.responder.app_presence.lease("b", 100)
        self.responder.app_presence.release("b", False, 100)
        self.responder.publish_app_presence()
        _, missing = ccp.parse_datagram(self.listener.sendto.call_args.args[0])
        self.assertEqual((missing["state"], missing["reason"], missing["age"]), ("unavailable", "transport", "-"))
        self.assertEqual(self.responder.app_presence_status()["source"], "mcp_transport")


class BrokerRecoveryTests(unittest.TestCase):
    # self: Detached lease fixture; no constructor, thread, socket or protected state is used.
    def setUp(self) -> None:
        self.client = runtime.BrokerClient.__new__(runtime.BrokerClient)
        self.client._secret = "synthetic-retained-credential"
        self.client._lease = "a" * 32
        self.client._stop = threading.Event()
        self.client._state_lock = threading.Lock()
        self.client._error = None
        diagnostics = contextlib.redirect_stderr(io.StringIO())
        diagnostics.__enter__()
        self.addCleanup(diagnostics.__exit__, None, None, None)

    # self: Failed lease followed by the same authenticated broker identity.
    # Raises AssertionError if recovery rotates identity or clears an error before renewal succeeds.
    def test_lease_recovers_without_replaying_tools_or_rotating_identity(self) -> None:
        failure = ConnectionRefusedError("synthetic stopped broker")
        with patch.object(runtime, "exchange", side_effect=failure):
            with self.assertRaises(ConnectionRefusedError):
                self.client._renew_once()
        self.assertIs(self.client._error, failure)
        with patch.object(runtime, "control_secret", return_value=self.client._secret) as secret:
            with patch.object(runtime, "ensure_runtime", return_value=self.client._secret) as ensure:
                with patch.object(runtime, "exchange", return_value={}) as exchange:
                    self.client._renew_once()
        secret.assert_called_once_with()
        ensure.assert_called_once_with()
        exchange.assert_called_once_with("lease", {"id": "a" * 32}, "synthetic-retained-credential")
        self.assertIsNone(self.client._error)

    # self: A replaced local credential that cannot identify the retained broker.
    # Raises AssertionError if recovery launches or adopts a different control identity.
    def test_changed_credential_stays_an_explicit_error(self) -> None:
        self.client._error = ConnectionRefusedError("synthetic stopped broker")
        with patch.object(runtime, "control_secret", return_value="different"):
            with patch.object(runtime, "ensure_runtime") as ensure, patch.object(runtime, "exchange") as exchange:
                with self.assertRaisesRegex(RuntimeError, "identity changed"):
                    self.client._renew_once()
        ensure.assert_not_called()
        exchange.assert_not_called()
        self.assertEqual(self.client._secret, "synthetic-retained-credential")
        self.assertEqual(self.client._lease, "a" * 32)
        self.assertIsNotNone(self.client._error)

    # self: A failed mutating MCP exchange whose remote outcome is unknown.
    # Raises AssertionError if the client automatically retries that operation.
    def test_tool_request_is_never_replayed(self) -> None:
        failure = ConnectionResetError("synthetic ambiguous tool response")
        with patch.object(runtime, "exchange", side_effect=failure) as exchange:
            with self.assertRaises(ConnectionResetError) as raised:
                self.client.request("mcp", {"method": "tools/call"})
        self.assertIs(raised.exception, failure)
        exchange.assert_called_once()

    # self: Repeated transport failure with synthetic waits and no real delay.
    # Raises AssertionError if cadence is unbounded or a terminal error is hidden.
    def test_transient_retries_are_bounded_and_stoppable(self) -> None:
        self.client._stop = Mock()
        self.client._stop.wait.side_effect = [False] * 6 + [True]
        self.client._renew_once = Mock(side_effect=ConnectionRefusedError("synthetic offline"))
        self.client._renew()
        self.assertEqual([call.args[0] for call in self.client._stop.wait.call_args_list],
                         [10.0, 1.0, 3.0, 10.0, 30.0, 30.0, 30.0])
        self.client._stop.reset_mock()
        self.client._stop.wait.side_effect = [False]
        self.client._renew_once.side_effect = RuntimeError("synthetic authentication rejected")
        self.client._renew()
        self.client._stop.wait.assert_called_once_with(10.0)

    # self: Public discovery contract with mocked transport service.
    # Raises AssertionError if loopback discovery becomes implicit or loses its source label.
    def test_simulator_discovery_requires_explicit_boolean(self) -> None:
        service = Mock()
        service.discover.return_value = []
        ccp.call_tool(service, "ccp_discover_devices", {})
        service.discover.assert_called_once_with(1200, False)
        service.discover.reset_mock()
        ccp.call_tool(service, "ccp_discover_devices", {"include_simulator": True})
        service.discover.assert_called_once_with(1200, True)
        with self.assertRaises(ValueError):
            ccp.call_tool(service, "ccp_discover_devices", {"include_simulator": "true"})
        self.assertEqual(ccp.observation_source("127.0.0.1"), "SIMULATED")
        self.assertEqual(ccp.observation_source("192.0.2.10"), "LAN")
        self.assertEqual(ccp.observation_source(""), "UNOBSERVED")


class ResetTests(unittest.TestCase):
    # self: Isolated reset admission owner; no physical device, hook or protected identity is used.
    def setUp(self) -> None:
        self.now = 50.0
        self.store = ccp.reset.ResetRequests(lambda: self.now)
        self.device, self.digest, self.nonce = "abcdef012345", "a" * 64, "b" * 32
        self.endpoint = ("127.0.0.1", 49101)

    # self: Strict distinction between explicit confirmation, restart and destructive factory reset.
    # Raises AssertionError if missing/wrong/numeric confirmation queues any command.
    def test_confirmation_is_explicit_and_operations_cannot_be_substituted(self) -> None:
        for operation in ccp.reset.ResetOperation:
            for invalid in (None, "", 43533477, "43533477", "043533478", "è" * 9):
                with self.subTest(operation=operation, code_type=type(invalid).__name__):
                    with self.assertRaises(ValueError):
                        self.store.enqueue(self.device, "SIMULATED", self.digest, invalid, operation)
        self.assertFalse(self.store._requests)
        created = self.store.enqueue(self.device, "SIMULATED", self.digest, ccp.reset.CONFIRMATION_CODE,
                                     ccp.reset.ResetOperation.FACTORY_RESET)
        self.store.claim(self.device, self.digest, self.nonce, self.endpoint)
        with self.assertRaises(ValueError):
            self.store.acknowledge(created, self.device, self.digest, self.nonce, self.endpoint,
                                   ccp.reset.ResetOperation.RESTART)
        for _ in range(2):
            self.store.acknowledge(created, self.device, self.digest, self.nonce, self.endpoint,
                                   ccp.reset.ResetOperation.FACTORY_RESET)
        result = self.store.wait(created)
        self.assertTrue(result["accepted"])
        self.assertFalse(result["boot_verified"] or result["settings_erasure_verified"] or result["preserve_pairing"])
        self.assertIsNone(self.store.claim(self.device, self.digest, self.nonce, self.endpoint))

    # self: One-shot admission across no FIND, lost ACK, socket failure and changed pairing.
    # Raises AssertionError if ambiguous or retired requests become automatic retransmissions.
    def test_single_transmission_timeout_and_pairing_change_are_explicit(self) -> None:
        for mode in ("no_find", "lost_ack", "send_error", "new_pairing"):
            self.store = ccp.reset.ResetRequests(lambda: self.now)
            created = self.store.enqueue(self.device, "SIMULATED", self.digest, ccp.reset.CONFIRMATION_CODE,
                                         ccp.reset.ResetOperation.RESTART)
            if mode != "no_find":
                claimed = self.store.claim(self.device, "c" * 64 if mode == "new_pairing" else self.digest,
                                           self.nonce, self.endpoint)
                self.assertEqual(claimed is None, mode == "new_pairing")
            if mode == "send_error":
                self.store.fail_send(created, OSError("SIMULATED UDP send failure"))
            self.now += ccp.reset.REQUEST_SECONDS + .01
            result = self.store.wait(created)
            self.assertEqual(result["state"], "not_sent" if mode in {"no_find", "new_pairing"} else "ambiguous")
            self.assertFalse(result["accepted"] or result["retry_automatically"])
            self.assertIsNone(self.store.claim(self.device, self.digest, "d" * 32, self.endpoint))

    # self: Real RESET/FACTORYRESET wire proofs with mocked transport and synthetic peer identities.
    # Raises AssertionError if ACK authentication, endpoint/operation binding or 512-byte bounds weaken.
    def test_authenticated_reset_ack_and_fresh_find_route(self) -> None:
        key, pc = b"K" * 32, "e" * 32
        state, listener = Mock(), Mock()
        state.pc_id = pc
        state.retained_keys.return_value = ccp.RetainedKeys(key, None)
        source_activity = ccp.ActivityStore()
        for operation in ccp.reset.ResetOperation:
            responder = ccp.CompanionResponder(state, listener, source_activity)
            responder._resets = self.store = ccp.reset.ResetRequests(lambda: self.now)
            identity = self.store.enqueue(self.device, "SIMULATED", hashlib.sha256(key).hexdigest(),
                                           ccp.reset.CONFIRMATION_CODE, operation)
            values = [self.device, pc, self.nonce]
            find = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values))
                                      + [("activity", "1"), ("events", "3"), ("current", "0" * 32), ("state", "none"), ("event_select", "0" * 32),
                                         ("proof", ccp.make_proof(key, "find-events-v3", *values, "0" * 32, "none", "0" * 32))])
            listener.sendto.reset_mock()
            responder._handle_datagram(find, self.endpoint)
            acknowledge_ready(responder, key, self.endpoint)
            command, acknowledgement, _, ack_domain = ccp.reset.COMMANDS[operation]
            sent = [call.args[0] for call in listener.sendto.call_args_list]
            self.assertEqual(ccp.parse_datagram(sent[-1])[0], command)
            self.assertLessEqual(len(sent[-1]), 512)
            ack_values = [self.device, pc, self.nonce, identity, "accepted"]
            ack = ccp.encode_datagram(acknowledgement,
                list(zip(("device", "pc", "nonce", "id", "status"), ack_values))
                + [("proof", ccp.make_proof(key, ack_domain, *ack_values))])
            with self.assertRaisesRegex(ValueError, "current authenticated peer"):
                responder._handle_datagram(ack, (self.endpoint[0], self.endpoint[1] + 1))
            with self.assertRaisesRegex(ValueError, "authentication"):
                responder._handle_datagram(ack[:-1] + (b"0" if ack[-1:] != b"0" else b"1"), self.endpoint)
            state.retained_keys.return_value = ccp.RetainedKeys(b"N" * 32, None)
            with self.assertRaisesRegex(ValueError, "retired association"):
                responder._handle_datagram(ack, self.endpoint)
            state.retained_keys.return_value = ccp.RetainedKeys(key, None)
            other_operation = (ccp.reset.ResetOperation.FACTORY_RESET if operation == ccp.reset.ResetOperation.RESTART
                               else ccp.reset.ResetOperation.RESTART)
            other_command = ccp.reset.COMMANDS[other_operation]
            wrong_operation_ack = ccp.encode_datagram(other_command[1],
                list(zip(("device", "pc", "nonce", "id", "status"), ack_values))
                + [("proof", ccp.make_proof(key, other_command[3], *ack_values))])
            with self.assertRaisesRegex(ValueError, "live transmitted reset request"):
                responder._handle_datagram(wrong_operation_ack, self.endpoint)
            responder._handle_datagram(ack, self.endpoint)
            responder._handle_datagram(ack, self.endpoint)
            self.assertEqual(self.store.wait(identity)["state"], "accepted")
            before = len(listener.sendto.call_args_list)
            responder._handle_datagram(find, self.endpoint)
            acknowledge_ready(responder, key, self.endpoint)
            self.assertFalse(any(ccp.parse_datagram(call.args[0])[0] == command
                                 for call in listener.sendto.call_args_list[before:]))

    # self: Public tool boundary and the skill's explicit-current-message execution constraint.
    # Raises AssertionError if the known code becomes a default or factory intent is conflated with restart.
    def test_tools_reject_implicit_reset_and_skill_requires_current_user_intent(self) -> None:
        service = ccp.CcpService.__new__(ccp.CcpService)
        service.state, service.responder = Mock(), Mock()
        for name in ("ccp_reset_device", "ccp_factory_reset_device"):
            with self.assertRaises(ValueError):
                ccp.call_tool(service, name, {"device_id": self.device})
            service.state.public_status.assert_not_called()
        skill = (ROOT / "plugins/cody-codex-plugin/skills/cody-codex/SKILL.md").read_text(encoding="utf-8")
        self.assertIn("current user", skill)
        self.assertIn("ccp_reset_device", skill)
        self.assertIn("ccp_factory_reset_device", skill)
        definitions = {item["name"]: item for item in ccp.tool_catalog()}
        self.assertFalse(definitions["ccp_reset_device"]["annotations"]["destructiveHint"])
        self.assertTrue(definitions["ccp_factory_reset_device"]["annotations"]["destructiveHint"])


class InteractionTests(unittest.TestCase):
    # self: Three independently owned questions with one explicit authenticated navigation request.
    # Raises AssertionError if navigation changes capabilities, selects answers or extends parked deadlines.
    def test_question_queue_keeps_private_ownership_and_explicit_navigation(self) -> None:
        association = "a" * 64
        tickets = [self.store.enqueue(self.device, replace(self.payload, title=f"Question {index}"),
                   "LAN", association_id=association) for index in range(3)]
        first = self.store.snapshot(self.device, association)
        projection = interactions.decode_payload(first[3])
        self.assertEqual(projection.pending_question_count, 3)
        self.assertEqual([entry.request_id for entry in projection.pending_questions],
                         [ticket["request_id"] for ticket in tickets[:2]])
        self.now += 5
        second = self.store.snapshot(self.device, association, first[0], "pending", tickets[1]["request_id"])
        self.assertEqual(second[:3], (tickets[1]["request_id"], "pending", "175"))
        self.assertEqual([entry.request_id for entry in interactions.decode_payload(second[3]).pending_questions],
                         [ticket["request_id"] for ticket in tickets[1:]])
        confirmed = self.store.snapshot(self.device, association, second[0], "pending")
        self.assertEqual(confirmed[0], second[0])
        for ticket in tickets:
            self.assertEqual(self.store.poll(ticket["request_id"], ticket["capability"])["state"], "pending")
        with self.assertRaises(ValueError):
            self.store.poll(tickets[0]["request_id"], tickets[1]["capability"])
        with self.assertRaises(ValueError):
            self.store.snapshot(self.device, association, second[0], "sending", tickets[2]["request_id"])

    # self: Lost EVENT handoff followed by cancellation of the offered successor.
    # Raises AssertionError if the unpublished ownership transfer strands the original question behind an impossible ACK.
    def test_lost_handoff_can_withdraw_target_without_stranding_current_question(self) -> None:
        association = "a" * 64
        first = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        second = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        self.store.snapshot(self.device, association)
        offered = self.store.snapshot(self.device, association, first["request_id"], "pending", second["request_id"])
        self.assertEqual(offered[0], second["request_id"])
        self.store.cancel(second["request_id"], second["capability"])
        retry = self.store.snapshot(self.device, association, first["request_id"], "pending", second["request_id"])
        self.assertEqual(retry[0], first["request_id"])
        self.assertEqual(interactions.decode_payload(retry[3]).pending_question_count, 1)
        self.store.cancel(first["request_id"], first["capability"])
        self.assertEqual(self.store.snapshot(self.device, association, first["request_id"], "pending")[:2],
                         (first["request_id"], "cancelled"))

    # self: Two native completion notices and an independent caller-owned question.
    # Raises AssertionError if explicit notice dismissal cannot release the FIFO or retires a question.
    def test_informational_dismissal_advances_notices_and_cannot_answer_questions(self) -> None:
        association = "a" * 64
        notice = interactions.EventPayload(interactions.EventKind.REPLY_READY, "", "Reply ready", "Review the reply.")
        first = self.store.enqueue(self.device, notice, "LAN", association_id=association, native_session="chat-one")
        second = self.store.enqueue(self.device, notice, "LAN", association_id=association, native_session="chat-two")
        self.assertEqual(self.store.snapshot(self.device, association)[0], first["request_id"])
        with self.assertRaises(ValueError):
            self.store.acknowledge_notice(self.device, first["request_id"], "b" * 64)
        self.store.acknowledge_notice(self.device, first["request_id"], association)
        self.store.acknowledge_notice(self.device, first["request_id"], association)
        self.assertEqual(self.store.snapshot(self.device, association)[0], second["request_id"])
        question = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        self.store.snapshot(self.device, association)
        with self.assertRaises(ValueError):
            self.store.acknowledge_notice(self.device, question["request_id"], association)
        self.assertEqual(self.store.poll(question["request_id"], question["capability"])["state"], "pending")

    # self: Restarted volatile broker with a successor already queued before the device reconnects.
    # Raises AssertionError if a lost question blocks until timeout, exposes the successor early or invents an answer.
    def test_restart_retires_unknown_open_question_before_publishing_successor(self) -> None:
        association = "a" * 64
        first = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        self.store.snapshot(self.device, association)
        restarted = interactions.InteractionStore(lambda: self.now)
        second = restarted.enqueue(self.device, self.payload, "LAN", association_id=association)
        for state in ("pending", "sending", "received"):
            self.assertEqual(restarted.snapshot(self.device, association, first["request_id"], state),
                             (first["request_id"], "unavailable", "0", "-"))
        self.assertFalse(restarted._tickets[second["request_id"]].published)
        self.assertEqual(restarted.answer(self.device, first["request_id"], "memory"), "unknown")
        self.assertEqual(restarted.snapshot(self.device, association, first["request_id"], "rejected")[:2],
                         (second["request_id"], "pending"))
        self.assertEqual(restarted.poll(second["request_id"], second["capability"])["state"], "pending")

    # self: Signed device observation admitted through the production responder after loss of host tickets.
    # Raises AssertionError if identity/state tampering bypasses retirement or authentic retirement suppresses the successor.
    def test_find_question_observation_is_bound_to_its_proof(self) -> None:
        key = bytes(range(32))
        state, listener = Mock(pc_id="a" * 32), Mock()
        state.retained_keys.return_value = ccp.RetainedKeys(key, None)
        association = hashlib.sha256(key).hexdigest()
        queued = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        responder = ccp.CompanionResponder(state, listener, activity.ActivityStore(), self.store)
        source = ("192.0.2.10", ccp.DEVICE_PORT)
        values = [self.device, state.pc_id, "b" * 32, "c" * 32, "sending"]
        fields = list(zip(("device", "pc", "nonce", "current", "state"), values))
        fields += [("activity", "1"), ("events", "3"), ("event_select", "0" * 32)]
        proof = ccp.make_proof(key, "find-events-v3", *values, "0" * 32)
        for name, changed in (("current", "d" * 32), ("state", "rejected")):
            altered = [(field, changed if field == name else value) for field, value in fields]
            with self.assertRaises(ValueError):
                responder._handle_datagram(ccp.encode_datagram("FIND", altered + [("proof", proof)]), source)
        self.assertFalse(self.store._tickets[queued["request_id"]].published)
        responder._handle_datagram(ccp.encode_datagram("FIND", fields + [("proof", proof)]), source)
        acknowledge_ready(responder, key, source)
        kind, event = ccp.parse_datagram(listener.sendto.call_args.args[0])
        self.assertEqual((kind, event["id"], event["state"], event["data"]),
                         ("EVENT", values[3], "unavailable", "-"))
        values[-1] = "rejected"
        fields = list(zip(("device", "pc", "nonce", "current", "state"), values))
        responder._handle_datagram(ccp.encode_datagram("FIND", fields + [("activity", "1"), ("events", "3"), ("event_select", "0" * 32),
            ("proof", ccp.make_proof(key, "find-events-v3", *values, "0" * 32))]), source)
        acknowledge_ready(responder, key, source)
        self.assertEqual(ccp.parse_datagram(listener.sendto.call_args.args[0])[1]["id"], queued["request_id"])

    # self: Two sequential questions with a lost terminal packet/ACK and one active association.
    # Raises AssertionError if publication bypasses device closure or extends the queued deadline.
    def test_next_question_waits_for_authenticated_device_closure(self) -> None:
        association = "a" * 64
        first = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        self.store.snapshot(self.device, association)
        self.store.answer(self.device, first["request_id"], "memory")
        answer = self.store.poll(first["request_id"], first["capability"])
        self.store.poll(first["request_id"], first["capability"], acknowledge_answer_id=answer["answer_id"])
        second = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        self.now += interactions.TERMINAL_DISPLAY_SECONDS + 1
        self.assertEqual(self.store.snapshot(self.device, association)[:2], (first["request_id"], "delivered"))
        for device, state, digest in ((self.device, "pending", association),
                                      (self.device, "delivered", "b" * 64),
                                      ("012345abcdef", "delivered", association)):
            with self.assertRaises(ValueError):
                self.store.acknowledge_terminal(device, first["request_id"], state, digest)
        self.assertEqual(self.store.snapshot(self.device, association)[0], first["request_id"])
        self.store.acknowledge_terminal(self.device, first["request_id"], "delivered", association)
        self.store.acknowledge_terminal(self.device, first["request_id"], "delivered", association)
        self.assertEqual(self.store.snapshot(self.device, association)[:3],
                         (second["request_id"], "pending", str(180 - int(self.now - 100))))

    # self: Local expiry after recipient delivery and an unpublished cancellation.
    # Raises AssertionError if local closure rewrites the answer or an unseen cancellation blocks a question.
    def test_local_expiry_ack_preserves_recipient_result_and_unpublished_ticket_does_not_block(self) -> None:
        association = "a" * 64
        first = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        self.store.snapshot(self.device, association)
        self.store.answer(self.device, first["request_id"], "redis")
        answer = self.store.poll(first["request_id"], first["capability"])
        self.store.poll(first["request_id"], first["capability"], acknowledge_answer_id=answer["answer_id"])
        self.store.acknowledge_terminal(self.device, first["request_id"], "expired", association)
        self.assertEqual(self.store.poll(first["request_id"], first["capability"])["state"], "delivered")
        cancelled = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        self.store.cancel(cancelled["request_id"], cancelled["capability"])
        with self.assertRaises(ValueError):
            self.store.acknowledge_terminal(self.device, cancelled["request_id"], "cancelled", association)
        next_ticket = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        self.assertEqual(self.store.snapshot(self.device, association)[0], next_ticket["request_id"])

    # self: Real signed EVENTACK packets against the production responder and isolated socket mock.
    # Raises AssertionError if a wrong source, stale nonce, forged proof or retired key releases the next ticket.
    def test_terminal_ack_requires_current_peer_nonce_proof_and_association(self) -> None:
        key = bytes(range(32))
        association = hashlib.sha256(key).hexdigest()
        first = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        state = Mock(pc_id="a" * 32)
        state.retained_keys.return_value = ccp.RetainedKeys(key, None)
        responder = ccp.CompanionResponder(state, Mock(), activity.ActivityStore(), self.store)
        source = ("192.0.2.10", ccp.DEVICE_PORT)
        values = [self.device, state.pc_id, "b" * 32]
        find = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values)) +
            [("activity", "1"), ("events", "3"), ("current", "0" * 32), ("state", "none"), ("event_select", "0" * 32), ("proof", ccp.make_proof(key, "find-events-v3", *values, "0" * 32, "none", "0" * 32))])
        responder._handle_datagram(find, source)
        acknowledge_ready(responder, key, source)
        self.store.cancel(first["request_id"], first["capability"])
        second = self.store.enqueue(self.device, self.payload, "LAN", association_id=association)
        ack_values = values + [first["request_id"], "cancelled"]
        fields = list(zip(("device", "pc", "nonce", "id", "state"), ack_values))
        ack = ccp.encode_datagram("EVENTACK", fields + [("proof", ccp.make_proof(key, "event-ack", *ack_values))])
        with self.assertRaises(ValueError):
            responder._handle_datagram(ack, (source[0], source[1] + 1))
        with self.assertRaises(ValueError):
            responder._handle_datagram(ccp.encode_datagram("EVENTACK", fields + [("proof", "0" * 64)]), source)
        stale_values = [*values[:2], "c" * 32, *ack_values[3:]]
        with self.assertRaises(ValueError):
            responder._handle_datagram(ccp.encode_datagram("EVENTACK",
                list(zip(("device", "pc", "nonce", "id", "state"), stale_values)) +
                [("proof", ccp.make_proof(key, "event-ack", *stale_values))]), source)
        state.retained_keys.return_value = ccp.RetainedKeys(bytes(reversed(key)), None)
        with self.assertRaises(ValueError):
            responder._handle_datagram(ack, source)
        self.assertEqual(self.store.snapshot(self.device, association)[0], first["request_id"])
        state.retained_keys.return_value = ccp.RetainedKeys(key, None)
        responder._handle_datagram(ack, source)
        self.assertEqual(self.store.snapshot(self.device, association)[0], second["request_id"])

    # self: Pure interaction owner with synthetic clocks; no hook, protected state or socket is used.
    def setUp(self) -> None:
        self.now = 100.0
        self.store = interactions.InteractionStore(lambda: self.now)
        self.device = "abcdef012345"
        self.payload = interactions.EventPayload(interactions.EventKind.QUESTION, "SIMULATED fixture",
            "Choose cache", "Which cache should I use?", (
                interactions.Choice("memory", "Memory", "Keep the cache in memory.", interactions.Tone.POSITIVE),
                interactions.Choice("redis", "Redis", "Use the configured Redis cache.", interactions.Tone.INFO)))

    # self: Shared production-encoded vectors also consumed by the portable firmware decoder.
    # Raises AssertionError if host admission differs from the shared wire contract.
    def test_shared_payload_vectors_and_original_pairing_bound(self) -> None:
        self.assertEqual({kind.name: kind.value for kind in interactions.EventKind}, {
            "NOTICE": 1, "QUESTION": 2, "APPROVAL_ON_PC": 3, "PR_PENDING": 4,
            "CHECKS_RUNNING": 5, "CHECKS_FAILED": 6, "CHECKS_PASSED": 7, "REPLY_READY": 8})
        for name, encoded, valid in event_vectors():
            with self.subTest(vector=name):
                if valid:
                    payload = interactions.decode_payload(encoded)
                    self.assertEqual(interactions.encode_payload(payload), encoded)
                    event = ccp.encode_datagram("EVENT", [("device", self.device), ("pc", "a" * 32),
                        ("nonce", "b" * 32), ("id", "c" * 32), ("state", "pending"), ("ttl", "180"),
                        ("data", encoded), ("proof", "d" * 64)])
                    self.assertLessEqual(len(event), 4096)
                    self.assertEqual(ccp.parse_datagram(event)[0], "EVENT")
                else:
                    with self.assertRaises(ValueError):
                        interactions.decode_payload(encoded)
        with self.assertRaises(ValueError):
            ccp.encode_datagram("PAIR", [("data", "0" * 510)])
        with self.assertRaises(ValueError):
            ccp.parse_datagram(b"CCP1|PAIR|data=" + b"0" * 510)

    # self: Public notification arguments, which cannot claim an unobserved native lifecycle edge.
    # Raises AssertionError if an ordinary tool caller can manufacture the typed background completion event.
    def test_reply_ready_kind_is_reserved_for_genuine_native_lifecycle(self) -> None:
        with self.assertRaisesRegex(ValueError, "Notification kind must be"):
            interactions.payload_from_arguments({"kind": "reply_ready", "title": "Reply ready",
                "body": "An unobserved completion claim."}, question=False)

    # self: One real semantic choice represented with synthetic identities and a fixed deadline.
    # Raises AssertionError if a UDP receipt is mistaken for recipient-confirmed delivery.
    def test_reply_is_idempotent_and_delivery_requires_recipient_ack(self) -> None:
        created = self.store.enqueue(self.device, self.payload, "SIMULATED")
        identity, capability = created["request_id"], created["capability"]
        self.assertEqual(self.store.answer(self.device, identity, "redis"), "received")
        self.assertEqual(self.store.answer(self.device, identity, "redis"), "received")
        self.assertEqual(self.store.answer(self.device, identity, "memory"), "conflict")
        reply = self.store.poll(identity, capability)
        self.assertEqual(reply["state"], "received")
        self.assertFalse(reply["recipient_acknowledged"])
        self.assertEqual(self.store.snapshot(self.device)[1], "received")
        with self.assertRaises(ValueError):
            self.store.poll(identity, "0" * 64)
        with self.assertRaises(ValueError):
            self.store.poll(identity, capability, acknowledge_answer_id="0" * 32)
        acknowledged = self.store.poll(identity, capability, acknowledge_answer_id=reply["answer_id"])
        self.assertEqual(acknowledged["state"], "delivered")
        self.assertTrue(acknowledged["recipient_acknowledged"])
        self.assertEqual(self.store.answer(self.device, identity, "redis"), "delivered")
        self.assertEqual(self.store.snapshot(self.device)[1], "delivered")
        self.assertEqual(self.store.poll(identity, capability, acknowledge_answer_id=reply["answer_id"])["state"], "delivered")
        with self.assertRaises(ValueError):
            self.store.cancel(identity, capability)

    # self: Received-but-unacknowledged and explicitly cancelled questions.
    # Raises AssertionError if expiry, wrong-device input or cancellation manufactures a decision.
    def test_expiry_cancel_and_wrong_device_never_select_an_option(self) -> None:
        first = self.store.enqueue(self.device, self.payload, "LAN", 15)
        self.assertEqual(self.store.answer("012345abcdef", first["request_id"], "memory"), "unknown")
        self.assertEqual(self.store.answer(self.device, first["request_id"], "redis"), "received")
        reply = self.store.poll(first["request_id"], first["capability"])
        self.now = 115.0
        expired = self.store.poll(first["request_id"], first["capability"])
        self.assertEqual(expired["state"], "expired")
        self.assertNotIn("answer", expired)
        with self.assertRaises(ValueError):
            self.store.poll(first["request_id"], first["capability"], acknowledge_answer_id=reply["answer_id"])
        second = self.store.enqueue(self.device, self.payload, "LAN")
        self.store.cancel(second["request_id"], second["capability"])
        self.assertEqual(self.store.answer(self.device, second["request_id"], "redis"), "cancelled")
        self.assertNotIn("answer", self.store.poll(second["request_id"], second["capability"]))

    # self: Question ownership alongside a bounded notification queue and background activity.
    # Raises AssertionError if another request displaces the pending question or restores stale ownership.
    def test_notices_do_not_replace_pending_questions_and_restart_loses_no_choice(self) -> None:
        created = self.store.enqueue(self.device, self.payload, "LAN")
        notice = interactions.EventPayload(interactions.EventKind.NOTICE, "", "Notice", "A real notification.")
        self.store.enqueue(self.device, notice, "LAN")
        self.assertEqual(self.store.snapshot(self.device)[0], created["request_id"])
        queued = self.store.enqueue(self.device, self.payload, "LAN")
        self.assertEqual(self.store.snapshot(self.device)[0], created["request_id"])
        self.assertEqual(self.store.poll(queued["request_id"], queued["capability"])["state"], "pending")
        self.now += 60
        self.assertEqual(self.store.snapshot(self.device)[1:3], ("pending", "120"))
        restarted = interactions.InteractionStore(lambda: self.now)
        self.assertEqual(restarted.answer(self.device, created["request_id"], "redis"), "unknown")
        self.assertEqual(restarted.snapshot(self.device)[1], "none")

    # self: A newer informational snapshot whose shorter lifetime must not reveal its superseded predecessor.
    # Raises AssertionError if old failure/progress content can return after a newer notification expires.
    def test_new_notification_retires_previous_snapshot_without_pr_regression(self) -> None:
        old = interactions.EventPayload(interactions.EventKind.CHECKS_FAILED, "", "Checks failed", "Old current result.", failures=("compile",))
        first = self.store.enqueue(self.device, old, "LAN", 180)
        current = interactions.EventPayload(interactions.EventKind.CHECKS_PASSED, "", "Checks passed", "New current result.",
                                            progress_valid=True, passed=1, total=1)
        second = self.store.enqueue(self.device, current, "LAN", 15)
        self.assertEqual(self.store.snapshot(self.device)[0], second["request_id"])
        self.assertEqual(self.store.poll(first["request_id"], first["capability"])["state"], "cancelled")
        self.now = 115.0
        self.assertEqual(self.store.snapshot(self.device)[1], "none")

    # self: One active association replaced before a reply reaches the host.
    # Raises AssertionError if a new key can answer an old ticket, even before its first snapshot publication.
    def test_pairing_replacement_retires_old_ticket_before_answer_or_snapshot(self) -> None:
        first = self.store.enqueue(self.device, self.payload, "LAN", association_id="a" * 64)
        self.assertEqual(self.store.answer(self.device, first["request_id"], "memory", "b" * 64), "unknown")
        self.assertEqual(self.store.poll(first["request_id"], first["capability"])["state"], "cancelled")
        second = self.store.enqueue(self.device, self.payload, "LAN", association_id="a" * 64)
        self.assertEqual(self.store.snapshot(self.device, "b" * 64)[1], "none")
        self.assertEqual(self.store.poll(second["request_id"], second["capability"])["state"], "cancelled")

    # self: Last fraction of an otherwise live lifetime.
    # Raises AssertionError if Pending/Received is published with an invalid zero-second TTL.
    def test_last_fraction_of_lifetime_is_ceil_bounded_then_expires(self) -> None:
        created = self.store.enqueue(self.device, self.payload, "LAN", 15)
        self.now = 114.75
        self.assertEqual(self.store.snapshot(self.device)[1:3], ("pending", "1"))
        self.store.answer(self.device, created["request_id"], "memory")
        self.assertEqual(self.store.snapshot(self.device)[1:3], ("received", "1"))
        self.now = 115.0
        self.assertEqual(self.store.snapshot(self.device)[1:3], ("expired", "0"))

    # self: Explicit read-only PR metadata tied to one exact head.
    # Raises AssertionError if partial/stale lists produce progress or imply successful checks.
    def test_pr_metadata_requires_current_complete_head_and_whole_failure_names(self) -> None:
        metadata = {"head": "a" * 40, "checks_head": "a" * 40, "complete": False,
                    "current": True, "checks": [{"name": "compile", "state": "passed"}]}
        self.assertEqual(interactions.check_metadata(interactions.EventKind.CHECKS_RUNNING, metadata), ((), False, 0, 0))
        with self.assertRaises(ValueError):
            interactions.check_metadata(interactions.EventKind.CHECKS_PASSED, metadata)
        metadata["complete"] = True
        self.assertEqual(interactions.check_metadata(interactions.EventKind.CHECKS_PASSED, metadata), ((), True, 1, 1))
        metadata["checks_head"] = "b" * 40
        with self.assertRaises(ValueError):
            interactions.check_metadata(interactions.EventKind.CHECKS_PASSED, metadata)
        metadata["checks_head"] = metadata["head"]
        metadata["checks"] = [{"name": "F" * 49, "state": "failed"}]
        with self.assertRaisesRegex(ValueError, "48"):
            interactions.check_metadata(interactions.EventKind.CHECKS_FAILED, metadata)
        metadata["checks"] = [{"name": f"failure-{index}", "state": "failed"} for index in range(7)]
        with self.assertRaisesRegex(ValueError, "six"):
            interactions.check_metadata(interactions.EventKind.CHECKS_FAILED, metadata)

    # self: Full authenticated extension exchange through the production responder without a real socket.
    # Raises AssertionError if capability stripping, proof alteration or source mismatch is admitted.
    def test_event_extension_authenticates_request_reply_and_recipient_delivery(self) -> None:
        created = self.store.enqueue(self.device, self.payload, "LAN")
        state = Mock(pc_id="a" * 32)
        key = bytes(range(32))
        state.retained_keys.return_value = ccp.RetainedKeys(key, None)
        listener = Mock()
        responder = ccp.CompanionResponder(state, listener, activity.ActivityStore(), self.store)
        values = [self.device, state.pc_id, "b" * 32]
        find = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values)) +
            [("activity", "1"), ("events", "3"), ("current", "0" * 32), ("state", "none"), ("event_select", "0" * 32), ("proof", ccp.make_proof(key, "find-events-v3", *values, "0" * 32, "none", "0" * 32))])
        source = ("192.0.2.10", ccp.DEVICE_PORT)
        responder._handle_datagram(find, source)
        acknowledge_ready(responder, key, source)
        kind, event = ccp.parse_datagram(listener.sendto.call_args.args[0])
        self.assertEqual(kind, "EVENT")
        self.assertEqual(event["id"], created["request_id"])
        projected = interactions.decode_payload(event["data"])
        self.assertEqual(replace(projected, pending_question_count=0, pending_questions=()), self.payload)
        self.assertEqual(projected.pending_question_count, 1)
        reply_values = values + [created["request_id"], "memory"]
        reply = ccp.encode_datagram("REPLY", list(zip(("device", "pc", "nonce", "id", "choice"), reply_values)) +
                                   [("proof", ccp.make_proof(key, "reply", *reply_values))])
        with self.assertRaises(ValueError):
            responder._handle_datagram(reply, (source[0], source[1] + 1))
        responder._handle_datagram(reply, source)
        self.assertEqual(ccp.parse_datagram(listener.sendto.call_args.args[0])[1]["status"], "received")
        result = self.store.poll(created["request_id"], created["capability"])
        self.store.poll(created["request_id"], created["capability"], acknowledge_answer_id=result["answer_id"])
        responder._handle_datagram(reply, source)
        self.assertEqual(ccp.parse_datagram(listener.sendto.call_args.args[0])[1]["status"], "delivered")
        stripped = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values)) +
            [("activity", "1"), ("proof", ccp.make_proof(key, "find-events-v3", *values, "0" * 32, "none", "0" * 32))])
        with self.assertRaises(ValueError):
            responder._handle_datagram(stripped, source)

    # self: Long-poll dispatch using the real request store while the unrelated pairing lock is occupied.
    # Raises AssertionError if recipient polling acquires the global tool lock or leaks response ownership.
    def test_interaction_poll_does_not_hold_pairing_tool_lock(self) -> None:
        created = self.store.enqueue(self.device, self.payload, "LAN")
        server = Mock()
        server.service.interactions = self.store
        server.tool_lock = Mock()
        server.tool_lock.__enter__ = Mock(side_effect=AssertionError("global lock must remain available"))
        result = runtime.dispatch(server, "mcp", {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {
            "name": "ccp_poll_request", "arguments": {"request_id": created["request_id"], "capability": created["capability"]}}})
        self.assertEqual(result["result"]["structuredContent"]["state"], "pending")
        server.tool_lock.__enter__.assert_not_called()

    # self: Native hook fixture, with all private command arguments deliberately excluded.
    # Raises AssertionError if a native permission is answered or an approval tool is exposed.
    def test_native_permission_hook_never_returns_an_approval(self) -> None:
        original = {"hook_event_name": "PermissionRequest", "session_id": "chat-fixture",
                    "turn_id": "turn-fixture", "cwd": "E:/SIMULATED", "tool_name": "Bash",
                    "tool_input": {"command": "SYNTHETIC PRIVATE COMMAND"}}
        stream = io.StringIO()
        with patch.object(runtime.sys, "stdin", Mock(buffer=io.BytesIO(json.dumps(original).encode()))):
            with patch.object(runtime, "control_secret", return_value="fixture"), patch.object(runtime, "exchange") as exchange:
                with contextlib.redirect_stdout(stream):
                    runtime.run_hook()
        self.assertEqual(json.loads(stream.getvalue()), {})
        self.assertNotIn("tool_input", exchange.call_args.args[1])
        self.assertFalse(any("approve" in tool["name"] or "permission" in tool["name"] for tool in ccp.tool_catalog()))


class DebugScreenContractTests(unittest.TestCase):
    # self: Pure authenticated Debug fixture with no socket/files/device/UI side effects.
    def setUp(self):
        self.device = "abcdef012345"
        self.key = bytes(range(32))
        self.state = Mock(pc_id="a" * 32)
        self.state.retained_keys.return_value = ccp.RetainedKeys(self.key, None)
        self.listener = Mock()
        self.responder = ccp.CompanionResponder(self.state, self.listener, activity.ActivityStore())
        self.source = ("127.0.0.1", ccp.DEVICE_PORT)
        self.peer = ccp.EventPeer("b" * 32, self.key, self.source, ccp.time.monotonic())
        self.responder._event_peers[self.device] = self.peer

    # self: Fixture with a genuine active event lease but no automatic Debug capability.
    # proof: Optional exact proof substitution for a forged capability case.
    # Returns a real bounded signed capability envelope.
    def capability(self, proof=None):
        values = [self.device, self.state.pc_id, self.peer.nonce, "1"]
        return ccp.encode_datagram("DEBUGCAP", list(zip(("device", "pc", "nonce", "debug"), values))
            + [("proof", proof or ccp.make_proof(self.key, "debug-cap", *values))])

    # self: Synthetic UDP transport which authenticates the UI worker's actual signed result.
    # packet: Real DEBUG packet emitted by the production responder.
    # destination: Exact authenticated endpoint selected by the responder.
    def reject_view(self, packet, destination):
        kind, fields = ccp.parse_datagram(packet)
        self.assertEqual(kind, "DEBUG")
        self.assertEqual(destination, self.source)
        names = ("device", "pc", "nonce", "id", "mode", "scroll", "status", "error", "count")
        values = [fields[name] for name in names[:6]] + ["rejected", "-1", "34"]
        ack = ccp.encode_datagram("DEBUGACK", list(zip(names, values))
            + [("proof", ccp.make_proof(self.key, "debug-ack", *values))])
        self.responder._handle_datagram(ack, self.source)

    # self: Debug capability/authentication boundary without a live connection.
    # Raises AssertionError if forged, missing or wrong-source capabilities authorize a command.
    def test_debug_capability_requires_active_key_and_exact_endpoint(self):
        with self.assertRaisesRegex(ValueError, "No recent authenticated diagnostic-screen"):
            self.responder.debug_screen(self.device, "character", 0, True)
        with self.assertRaisesRegex(ValueError, "authentication failed"):
            self.responder._handle_datagram(self.capability("0" * 64), self.source)
        with self.assertRaisesRegex(ValueError, "authentication failed"):
            self.responder._handle_datagram(self.capability(), (self.source[0], self.source[1] + 1))
        self.listener.sendto.assert_not_called()

    # self: Production Debug round trip with a native worker rejecting ESP_FAIL.
    # Raises AssertionError if a signed failure is coerced to success/u32, or same-nonce replay reauthorizes a command.
    def test_debug_ack_preserves_signed_error_and_never_claims_pixels(self):
        self.responder._handle_datagram(self.capability(), self.source)
        with self.assertRaisesRegex(ValueError, "explicit include_simulator"):
            self.responder.debug_screen(self.device, "character", 0, False)
        self.listener.sendto.side_effect = self.reject_view
        result = self.responder.debug_screen(self.device, "character", 0, True)
        self.assertEqual(result["status"], "rejected")
        self.assertEqual(result["error"], -1)
        self.assertEqual(result["channel_count"], 34)
        self.assertFalse(result["pixels_verified"])
        self.assertEqual(result["source"], "SIMULATED")
        self.responder._handle_datagram(self.capability(), self.source)
        with self.assertRaisesRegex(ValueError, "No recent authenticated diagnostic-screen"):
            self.responder.debug_screen(self.device, "normal", 0, True)
        self.listener.sendto.assert_called_once()

    # self: Malformed MCP inputs which must remain structured invalid-command errors.
    # Raises AssertionError if removed modes, untyped inputs or reserved offsets authorize a command.
    def test_debug_rejects_untyped_commands_before_network_use(self):
        for mode in ([], {}, None, "face_channels"):
            with self.assertRaises(ValueError):
                self.responder.debug_screen(self.device, mode, 0, True)
        for mode, scroll in (("normal", 1), ("character", True), ("character", 256)):
            with self.assertRaises(ValueError):
                self.responder.debug_screen(self.device, mode, scroll, True)
        self.listener.sendto.assert_not_called()


# Isolated public-document fixture. Values are reference data, not a real account or chat bill.
PRICING_HTML = """<table><tr><th>Credits per 1M tokens</th><th>Input Tokens</th>
<th>Cached input tokens</th><th>Output Tokens</th></tr>
<tr><td>GPT-6 Luna</td><td>2.5 credits</td><td>0.25 credits</td><td>12.5 credits</td></tr>
<tr><td>GPT-6 Sol</td><td>50 credits</td><td>5 credits</td><td>250 credits</td></tr>
<tr><td>GPT-6 Astra</td><td>250 credits</td><td>25 credits</td><td>1,250 credits</td></tr>
<tr><td colspan="4">These are Standard credit rates.</td></tr></table>"""


class PricingContractTests(unittest.TestCase):
    # self: Portable pricing fixture; all files are under artifacts and all downloads are injected.
    # Raises original filesystem errors before running any contract assertion.
    def setUp(self) -> None:
        directory = ROOT / "artifacts/checks/cody-ccp"
        directory.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / "pricing/codex-standard.json"
        self.now = time.mktime((2026, 10, 11, 12, 0, 0, 0, 0, -1))
        self.fetch = Mock(return_value=pricing.parse_catalog(PRICING_HTML))

    # self: Synthetic local calendar and bounded cache-file owner.
    # fetch: Optional injected failure/source for this particular broker start.
    # Returns a started background owner and waits only for its bounded fixture worker.
    # Raises original cache errors; no HTTP, socket, DPAPI, Codex or device is used.
    def start_cache(self, fetch=None):
        owner = pricing.PriceCache(self.path, self.fetch if fetch is None else fetch, lambda: self.now)
        self.addCleanup(owner.close)
        owner.start_daily_refresh()
        owner.close()
        return owner

    # self: Current public-table fixture plus each malformed/ambiguous source variant.
    # Raises AssertionError if a different price basis or guessed alias is silently accepted.
    def test_only_qualified_unambiguous_standard_table_is_accepted(self):
        rates = pricing.parse_catalog(PRICING_HTML)
        self.assertEqual(set(rates), {"gpt-6-luna", "gpt-6-sol", "gpt-6-astra"})
        self.assertEqual(str(rates["gpt-6-astra"][2]), "1250")
        for document in (PRICING_HTML.replace("Standard credit rates", "Batch credit rates"),
                         PRICING_HTML + PRICING_HTML,
                         PRICING_HTML.replace("GPT-6 Luna", "GPT-6 Sol"),
                         PRICING_HTML.replace("50 credits", "$50"),
                         PRICING_HTML.replace("1,250 credits", "12,50 credits"),
                         PRICING_HTML.replace("0.25 credits", "3 credits"),
                         PRICING_HTML.replace("</table>", "")):
            with self.subTest(document=document[-80:]):
                with self.assertRaises(ValueError):
                    pricing.parse_catalog(document)

    # self: Documented input/output thresholds, including equality and a single exceeded dimension.
    # Raises AssertionError if a high input/output reference is mislabeled by the other dimension.
    def test_policy_uses_both_uncached_input_and_output_rates(self):
        for input_rate, output_rate, expected in (
            ("10", "50", pricing.CostBand.ECONOMY), ("10.1", "50", pricing.CostBand.BALANCED),
            ("10", "50.1", pricing.CostBand.BALANCED), ("100", "500", pricing.CostBand.BALANCED),
            ("100.1", "50", pricing.CostBand.EXPENSIVE), ("50", "500.1", pricing.CostBand.EXPENSIVE)):
            self.assertEqual(pricing.classify(pricing.rate(input_rate), pricing.rate(output_rate)), expected)

    # self: Cold start, additional MCP startup and new broker in the same local day.
    # Raises AssertionError if first-start caching repeats a download or loses original source age.
    def test_daily_cache_survives_broker_restarts_and_plugin_starts(self):
        owner = self.start_cache()
        self.assertEqual(owner.projection("gpt-6-luna").band, pricing.CostBand.ECONOMY)
        self.assertEqual(owner.projection("gpt-6-sol").band, pricing.CostBand.BALANCED)
        self.assertEqual(owner.projection("gpt-6-astra").band, pricing.CostBand.EXPENSIVE)
        self.now += 60
        owner.start_daily_refresh()
        restarted = self.start_cache()
        self.fetch.assert_called_once()
        self.assertEqual(restarted.projection("gpt-6-sol").age_ms, 60000)
        state = json.loads(self.path.read_text(encoding="utf-8"))
        self.assertEqual(state["source"], pricing.SOURCE)
        self.assertEqual(state["policy"], pricing.POLICY)
        self.assertEqual(state["attempted_day"], "2026-10-11")

    # self: Broker kept alive across midnight; no refresh until a new plugin startup.
    # Raises AssertionError if yesterday's rates are presented as current or lookup performs a download.
    def test_midnight_expires_and_next_plugin_start_refreshes(self):
        owner = self.start_cache()
        self.now = time.mktime((2026, 10, 12, 0, 0, 0, 0, 0, -1))
        self.assertEqual(owner.projection("gpt-6-sol"), pricing.PriceProjection())
        self.fetch.assert_called_once()
        owner.start_daily_refresh()
        owner.close()
        self.assertEqual(self.fetch.call_count, 2)
        self.assertEqual(owner.projection("gpt-6-sol").band, pricing.CostBand.BALANCED)
        self.start_cache()
        self.assertEqual(self.fetch.call_count, 2)

    # self: Last valid catalog followed by a failed first refresh on the next local day.
    # Raises AssertionError if offline failure loses the catalog, retries that day or publishes an old band.
    def test_failed_daily_refresh_preserves_catalog_and_does_not_retry(self):
        self.start_cache()
        previous = json.loads(self.path.read_text(encoding="utf-8"))["catalog"]
        self.now = time.mktime((2026, 10, 12, 12, 0, 0, 0, 0, -1))
        failed = Mock(side_effect=OSError("Synthetic offline source"))
        owner = self.start_cache(failed)
        self.assertEqual(owner.projection("gpt-6-sol"), pricing.PriceProjection())
        self.assertIn("Synthetic offline source", owner.error)
        state = json.loads(self.path.read_text(encoding="utf-8"))
        self.assertEqual(state["catalog"], previous)
        self.assertEqual(state["attempted_day"], "2026-10-12")
        self.assertEqual(self.start_cache(failed).projection("gpt-6-sol"), pricing.PriceProjection())
        failed.assert_called_once()

    # self: Exact model IDs, missing models, unsupported names and backwards PC wall time.
    # Raises AssertionError if a substring alias or future retrieval timestamp becomes a current badge.
    def test_unrecognized_model_and_future_catalog_are_unknown(self):
        owner = self.start_cache()
        for model in ("", "gpt-6-sol-fast", "gpt-6-sol-2026-10-11", "GPT-6 Sol", "new-mini-model"):
            self.assertEqual(owner.projection(model), pricing.PriceProjection())
        self.now -= 1
        self.assertEqual(owner.projection("gpt-6-sol"), pricing.PriceProjection())

    # self: Malformed persisted data with a source download that must never be called.
    # Raises AssertionError if cache corruption is treated as absence or silently overwritten.
    def test_corrupt_cache_is_preserved_and_reported(self):
        self.path.parent.mkdir(parents=True)
        raw = b'{"schema":1,"schema":1}'
        self.path.write_bytes(raw)
        owner = pricing.PriceCache(self.path, self.fetch, lambda: self.now)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            owner.start_daily_refresh()
        self.fetch.assert_not_called()
        self.assertEqual(self.path.read_bytes(), raw)
        self.assertIn("duplicate", owner.error)
        self.assertEqual(owner.projection("gpt-6-sol"), pricing.PriceProjection())

    # self: Atomic attempt publication failure before any network work is admitted.
    # Raises AssertionError if a failed durable claim still fetches or supplies a current band.
    def test_failed_attempt_persistence_prevents_download(self):
        owner = pricing.PriceCache(self.path, self.fetch, lambda: self.now)
        with patch.object(Path, "replace", side_effect=OSError("Synthetic read-only cache")):
            with self.assertRaisesRegex(OSError, "read-only"):
                owner.start_daily_refresh()
        self.fetch.assert_not_called()
        self.assertFalse(self.path.exists())
        self.assertEqual(owner.projection("gpt-6-sol"), pricing.PriceProjection())
        self.assertIn("read-only", owner.error)

    # self: Real activity store, exact observed model and authenticated responder with a synthetic socket.
    # Raises AssertionError if prices leak, a stale model retains a band or any badge field lacks HMAC binding.
    def test_band_is_bound_to_the_followed_current_model_and_signed_fields(self):
        owner = self.start_cache()
        # Distinct synthetic prompts must not depend on the host clock advancing
        # between two fast observations. Keep admission and projection clocks coherent.
        monotonic_clock = patch.object(activity.time, "monotonic", return_value=100.0)
        emission_clock = patch.object(activity.time, "monotonic_ns", return_value=100_000_000_000)
        self.addCleanup(monotonic_clock.stop)
        self.addCleanup(emission_clock.stop)
        monotonic_clock.start()
        emission_clock.start()
        activity_store = activity.ActivityStore(owner)
        event = {"hook_event_name": "UserPromptSubmit", "session_id": "priced-chat", "cwd": "E:/fixture",
                 "turn_id": "priced-turn", "model": "gpt-6-sol", "emitted_ns": 99_000_000_000}
        activity_store.observe(event)
        state = Mock(pc_id="a" * 32)
        key = bytes(range(32))
        state.retained_keys.return_value = ccp.RetainedKeys(key, None)
        listener = Mock()
        responder = ccp.CompanionResponder(state, listener, activity_store)
        values = ["abcdef012345", state.pc_id, "b" * 32]
        find = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values)) +
            [("activity", "4"), ("proof", ccp.make_proof(key, "find-activity-v4", *values))])
        source = ("192.0.2.10", ccp.DEVICE_PORT)
        responder._handle_datagram(find, source)
        acknowledge_ready(responder, key, source)
        raw = next(call.args[0] for call in listener.sendto.call_args_list if call.args[0].startswith(b"CCP1|TECH|"))
        _, fields = ccp.parse_datagram(raw)
        decoded = technical_metadata.decode_projection(fields)
        self.assertEqual(decoded.cost_band, pricing.CostBand.BALANCED)
        self.assertEqual(decoded.cost_age_ms, 0)
        self.assertGreater(decoded.cost_valid_for_ms, 0)
        self.assertLessEqual(len(raw), ccp.MAX_TECHNICAL_DATAGRAM_BYTES)
        for name, changed in (("cost", "economy"), ("costage", "1000"), ("costttl", "1")):
            tampered = dict(fields)
            tampered[name] = changed
            self.assertNotEqual(fields["proof"], ccp.make_proof(key, "technical-v2",
                *(tampered[field] for field in technical_metadata.FIELDS)))
        for name, changed in (("cost", "cheap"), ("cost", "unknown"), ("costage", "-"),
                              ("costttl", "0"), ("costttl", "93600001"), ("modelage", "120000")):
            invalid = dict(fields)
            invalid[name] = changed
            with self.assertRaises(ValueError):
                technical_metadata.decode_projection(invalid)
        with patch.object(activity.time, "monotonic", return_value=time.monotonic() + 121):
            _, stale = activity_store.snapshot_bundle()
        self.assertEqual((stale["cost"], stale["costage"], stale["costttl"]), ("unknown", "-", "-"))
        activity_store.observe({**event, "model": "gpt-6-luna", "emitted_ns": 99_000_000_001})
        _, switched = activity_store.snapshot_bundle()
        self.assertEqual(switched["cost"], "economy")
        self.assertTrue(all(switched[name] == "-" for name in technical_metadata.UNAVAILABLE_FIELDS))


class NativeUsageReaderTests(unittest.TestCase):
    # self: Isolated native metadata fixture; no installed Codex state or conversation is read.
    def setUp(self):
        output = ROOT / "artifacts/checks/cody-ccp"
        output.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=output)
        self.addCleanup(temporary.cleanup)
        self.home = Path(temporary.name)
        self.cwd = str(self.home / "checkout")
        self.path = self.home / "sessions/fixture.jsonl"
        self.path.parent.mkdir()
        environment = patch.dict(os.environ, {"CODEX_HOME": str(self.home)})
        environment.start()
        self.addCleanup(environment.stop)
        wall = patch.object(native_usage.time, "time", return_value=2000000000.0)
        mono = patch.object(native_usage.time, "monotonic", return_value=1000.0)
        wall.start()
        mono.start()
        self.addCleanup(wall.stop)
        self.addCleanup(mono.stop)
        self.reader = native_usage.NativeUsageReader()

    # self: Native fixture owner.
    # kind: Exact native record discriminator.
    # payload: Synthetic metadata only; no real conversation text.
    # seconds: Native event epoch timestamp before the fake current clock.
    def record(self, kind, payload, seconds=1999999990):
        timestamp = native_usage.datetime.fromtimestamp(seconds, native_usage.datetime.now().astimezone().tzinfo).isoformat()
        return json.dumps({"timestamp": timestamp, "type": kind, "payload": payload}).encode() + b"\n"

    # self: Native fixture owner.
    # effort: Optional explicit native reasoning setting.
    def prefix(self, effort="high"):
        context = {"turn_id": "turn-a", "cwd": self.cwd, "model": "gpt-6-astra"}
        if effort is not None:
            context["effort"] = effort
        return (self.record("session_meta", {"id": "chat-a", "session_id": "chat-a", "cwd": self.cwd})
                + self.record("event_msg", {"type": "task_started", "turn_id": "turn-a"})
                + self.record("turn_context", context))

    # self: Native fixture owner.
    # capacity: Optional authoritative window capacity.
    # seconds: Original native timestamp.
    def usage(self, capacity=475000, seconds=1999999995):
        info = {"last_token_usage": {"input_tokens": 100, "output_tokens": 20, "total_tokens": 120},
                "total_token_usage": {"total_tokens": 900000}, "model_context_window": capacity}
        return self.record("event_msg", {"type": "token_count", "info": info}, seconds)

    # self: Native fixture owner.
    # turn: Genuine synthetic hook turn selected by this caller.
    def poll(self, turn="turn-a"):
        return self.reader.poll("chat-a", self.cwd, turn, str(self.path))

    # self: Real parser with a native last-call observation and an optional model-only context.
    def test_original_age_model_only_and_partial_capacity(self):
        self.path.write_bytes(self.prefix(None) + self.usage(None))
        first, context = self.poll()
        self.assertEqual(first[0], {"used": 120, "input": 100, "output": 20, "cumulative": 900000})
        self.assertEqual(context[:2], ("gpt-6-astra", None))
        with patch.object(native_usage.time, "monotonic", return_value=1050.0), patch.object(native_usage.time, "time", return_value=2000000050.0):
            second, _ = self.poll()
        self.assertEqual(second, first)
        self.assertEqual(first[1], 995.0)
        self.assertEqual(self.poll("another-turn"), (None, None))

    # self: Current Desktop records include a numeric ordinal between timestamp and outer type.
    def test_native_ordinal_envelope_is_admitted_without_parsing_response_bodies(self):
        ignored = b"".join(self.record(kind.decode(), {"content": "fixture-only"})
                           for kind in native_usage.IGNORED_RECORDS)
        records = self.prefix() + ignored + self.usage()
        lines = []
        for ordinal, line in enumerate(records.splitlines()):
            lines.append(line.replace(b'"type":', f'"ordinal": {ordinal}, "type":'.encode(), 1))
        self.path.write_bytes(b"\n".join(lines) + b"\n")
        sample, context = self.poll()
        self.assertEqual(sample[0]["used"], 120)
        self.assertEqual(context[:2], ("gpt-6-astra", "high"))

    # self: Exact-session reader; foreign parent metadata, cwd and native turn cannot populate it.
    def test_session_checkout_fork_and_turn_rejections(self):
        for changed in ({"id": "other"}, {"session_id": "parent"}, {"cwd": self.cwd + "-other"}):
            self.reader = native_usage.NativeUsageReader()
            self.path.write_bytes(self.record("session_meta", {"id": "chat-a", "session_id": "chat-a", "cwd": self.cwd, **changed}) + self.usage())
            with self.assertRaises(native_usage.UsageSourceError):
                self.poll()
        self.reader = native_usage.NativeUsageReader()
        self.path.write_bytes(self.prefix() + self.record("turn_context", {"turn_id": "turn-a", "cwd": self.cwd + "-other", "model": "gpt-6-astra"}) + self.usage())
        with self.assertRaises(native_usage.UsageSourceError):
            self.poll()

    # self: Incremental parser must not publish incomplete metadata or lose correlation on big items.
    def test_partial_record_and_large_irrelevant_item(self):
        self.path.write_bytes(self.prefix() + self.usage()[:-8])
        self.assertEqual(self.poll(), (None, None))
        with self.path.open("ab") as stream:
            stream.write(self.usage()[-8:])
        self.assertEqual(self.poll()[0][0]["used"], 120)
        huge = self.record("response_item", {"type": "message", "content": "fixture-only" * 40000})
        with self.path.open("ab") as stream:
            stream.write(huge + self.usage(seconds=1999999996))
        for _ in range(6):
            result, _ = self.poll()
            self.assertLessEqual(self.reader.bytes_read_last_poll, native_usage.READ_BUDGET)
        self.assertEqual(result[0]["used"], 120)
        self.assertEqual(self.reader.native_turn, "turn-a")

    # self: Large historical transcript fixture; discovery should read the header and recent tail only.
    def test_long_history_bootstrap_has_bounded_tail_cost(self):
        header, _, _ = self.prefix().partition(b"\n")
        history = self.record("response_item", {"type": "message", "content": "old-fixture" * 300000})
        context = self.prefix().split(b"\n")[-2] + b"\n"
        self.path.write_bytes(header + b"\n" + history + context + self.usage())
        result, _ = self.poll()
        self.assertEqual(result[0]["cumulative"], 900000)
        self.assertLessEqual(self.reader.bytes_read_last_poll, native_usage.READ_BUDGET)
        self.assertLess(self.reader.bytes_read_last_poll, self.path.stat().st_size // 10)

    # self: Record-boundary search must resume backward across multiple IO budgets.
    def test_backward_bootstrap_across_large_response(self):
        huge = self.record("response_item", {"content": "x" * 260000})
        self.path.write_bytes(self.prefix() + huge + self.usage())
        self.assertEqual(self.poll(), (None, None))
        result = None
        for _ in range(3):
            result, _ = self.poll()
            self.assertLessEqual(self.reader.bytes_read_last_poll, native_usage.READ_BUDGET)
        self.assertEqual(result[0]["used"], 120)

    # self: Reader may see native metadata before the matching genuine hook reaches its owner.
    def test_late_hook_recovers_already_consumed_native_turn(self):
        self.path.write_bytes(self.prefix() + self.usage())
        self.assertEqual(self.poll()[0][0]["used"], 120)
        with self.path.open("ab") as stream:
            stream.write(self.record("event_msg", {"type": "task_started", "turn_id": "turn-b"}))
            stream.write(self.record("turn_context", {"turn_id": "turn-b", "cwd": self.cwd,
                                                     "model": "gpt-6-sol", "effort": "medium"}))
            stream.write(self.usage(seconds=1999999996))
        self.assertEqual(self.poll(), (None, None))
        sample, context = self.poll("turn-b")
        self.assertEqual(sample[0]["used"], 120)
        self.assertEqual(sample[1], 996.0)
        self.assertEqual(context[:2], ("gpt-6-sol", "medium"))

    # self: Repeated same-turn start metadata cannot erase or renew a native model/usage observation.
    def test_repeated_native_start_preserves_observation(self):
        self.path.write_bytes(self.prefix() + self.usage())
        original = self.poll()
        with self.path.open("ab") as stream:
            stream.write(self.record("event_msg", {"type": "task_started", "turn_id": "turn-a"}, seconds=1999999998))
        self.assertEqual(self.poll(), original)

    # self: Reader of a stable inode; replacing or truncating the source cannot reuse retained data.
    def test_truncation_replacement_and_oversized_metadata_fail_closed(self):
        for operation in ("truncate", "replace", "metadata"):
            self.reader = native_usage.NativeUsageReader()
            self.path.write_bytes(self.prefix() + self.usage())
            self.poll()
            if operation == "truncate":
                self.path.write_bytes(b"")
            elif operation == "replace":
                replacement = self.path.with_suffix(".replacement")
                replacement.write_bytes(self.prefix() + self.usage())
                os.replace(replacement, self.path)
            else:
                with self.path.open("ab") as stream:
                    stream.write(self.record("turn_context", {"turn_id": "turn-a", "cwd": self.cwd, "model": "x" * 150000}))
                self.assertEqual(self.poll(), (None, None))
            with self.assertRaises(native_usage.UsageSourceError):
                self.poll()
            self.assertEqual(self.poll(), (None, None))

    # self: Native counter reader rejects future clocks and malformed values with safe reasons.
    def test_errors_do_not_echo_private_metadata(self):
        for record in (self.usage(seconds=2000000010), self.record("event_msg", {"type": "token_count", "info": {"last_token_usage": {}, "total_token_usage": {}}})):
            self.reader = native_usage.NativeUsageReader()
            self.path.write_bytes(self.prefix() + record)
            with self.assertRaises(native_usage.UsageSourceError) as rejected:
                self.poll()
            self.assertNotIn(self.cwd, native_usage.safe_usage_error(rejected.exception))
        self.assertNotIn("private", native_usage.safe_usage_error(ValueError("private")))

    # self: Locator/index ownership fixture; queries read an existing exact identity, never repair state.
    def test_readonly_index_and_path_ownership(self):
        self.path.write_bytes(self.prefix() + self.usage())
        with contextlib.closing(sqlite3.connect(self.home / "state_5.sqlite")) as database:
            database.execute("CREATE TABLE threads(id TEXT PRIMARY KEY, rollout_path TEXT, cwd TEXT)")
            database.execute("INSERT INTO threads VALUES(?,?,?)", ("chat-a", str(self.path), self.cwd))
            database.commit()
        self.assertEqual(self.reader.poll("chat-a", self.cwd, "turn-a")[0][0]["used"], 120)
        outsider = self.home / "outside.jsonl"
        outsider.write_bytes(self.prefix() + self.usage())
        with self.assertRaises(native_usage.UsageSourceError):
            self.reader.poll("chat-a", self.cwd, "turn-a", str(outsider))
        missing = native_usage.NativeUsageReader()
        with self.assertRaises(native_usage.UsageSourceError):
            missing.poll("missing-chat", self.cwd, "turn-a")

    # self: Actual filesystem confinement fixture; platforms without symlink rights report a skip.
    def test_symlink_escape_is_rejected(self):
        outsider = self.home / "outside.jsonl"
        outsider.write_bytes(self.prefix() + self.usage())
        try:
            self.path.symlink_to(outsider)
        except OSError as error:
            self.skipTest(f"Symlink creation unavailable: errno={error.errno}")
        with self.assertRaises(native_usage.UsageSourceError):
            self.poll()

    # self: Windows fixture reproducing native SQLite extended-path spelling for the same real file.
    @unittest.skipUnless(os.name == "nt", "Windows extended filesystem paths")
    def test_extended_windows_path_locator_and_index_keep_owner_identity(self):
        self.path.write_bytes(self.prefix() + self.usage())
        extended = "\\\\?\\" + str(self.path.resolve())
        self.assertEqual(self.reader.poll("chat-a", self.cwd, "turn-a", extended)[0][0]["used"], 120)
        self.assertEqual(native_usage.resolved_native_path(extended), native_usage.resolved_native_path(self.path))
        with contextlib.closing(sqlite3.connect(self.home / "state_5.sqlite")) as database:
            database.execute("CREATE TABLE threads(id TEXT PRIMARY KEY, rollout_path TEXT, cwd TEXT)")
            database.execute("INSERT INTO threads VALUES(?,?,?)", ("chat-a", extended, "\\\\?\\" + self.cwd))
            database.commit()
        reader = native_usage.NativeUsageReader()
        self.assertEqual(reader.poll("chat-a", self.cwd, "turn-a")[0][0]["used"], 120)

    # self: Real envelope filter; ignored item bodies must never enter the JSON decoder.
    def test_irrelevant_response_is_not_json_decoded(self):
        for kind in native_usage.IGNORED_RECORDS:
            irrelevant = self.record(kind.decode(), {"content": "fixture-only"})
            with patch.object(native_usage.json, "loads", side_effect=AssertionError("Response content parsed")):
                self.assertIsNone(native_usage.metadata_record(irrelevant))
                self.assertIsNone(native_usage.metadata_record(irrelevant, oversized=True))

    # self: Accepted real lifecycle handler with synthetic private IDs; logs contain only fixed metadata.
    def test_lifecycle_trace_hashes_identity_and_reports_accepted_compaction(self):
        owner = activity.ActivityStore()
        base = {"session_id": "private-chat-id", "cwd": self.cwd, "turn_id": "private-turn-id",
                "prompt": "private prompt must never appear", "trigger": "auto"}
        output = io.StringIO()
        with contextlib.redirect_stderr(output), patch.object(activity.time, "monotonic_ns", return_value=1000000000000):
            for index, name in enumerate(("UserPromptSubmit", "PreCompact", "PostCompact", "Stop")):
                owner.observe({**base, "hook_event_name": name, "emitted_ns": 999000000000 + index})
            for index, name in enumerate(("UserPromptSubmit", "Interrupt", "SessionEnd"), start=4):
                owner.observe({**base, "turn_id": "another-private-turn", "hook_event_name": name,
                               "emitted_ns": 999000000000 + index})
        diagnostic = output.getvalue()
        self.assertEqual(len(diagnostic.splitlines()), 7)
        self.assertNotIn("private", diagnostic)
        self.assertNotIn(self.cwd, diagnostic)
        self.assertIn("event=PreCompact", diagnostic)
        self.assertIn("compaction=active compactseq=0000000000000001", diagnostic)
        self.assertIn("event=PostCompact", diagnostic)
        self.assertIn("compaction=refreshed compactseq=0000000000000001", diagnostic)
        self.assertIn("chat=" + hashlib.sha256(base["session_id"].encode()).hexdigest()[:12], diagnostic)

    # self: Real hook/technical owner; completion cannot relabel pre-compaction usage as refreshed.
    def test_post_compaction_waits_for_new_native_usage_without_renewing_model(self):
        owner = activity.ActivityStore()
        base = {"session_id": "chat-a", "cwd": self.cwd, "turn_id": "turn-a"}
        values = {"used": 120, "capacity": 475000, "cumulative": 900000, "input": 100, "output": 20}
        context = ("gpt-6-astra", "high", 990.0)
        with patch.object(activity.time, "monotonic_ns", return_value=1000000000000):
            owner.observe({**base, "hook_event_name": "UserPromptSubmit", "emitted_ns": 999000000000})
            owner.observe({**base, "hook_event_name": "PreCompact", "trigger": "auto", "emitted_ns": 999100000000})
            with patch.object(owner._usage, "poll", return_value=((values, 995.0), context)):
                _, active = owner.snapshot_bundle()
                self.assertEqual(active["used"], "120")
                owner.observe({**base, "hook_event_name": "PostCompact", "trigger": "auto", "emitted_ns": 999200000000})
                _, pending = owner.snapshot_bundle()
            self.assertEqual(pending["compaction"], "refreshed")
            self.assertTrue(all(pending[name] == "-" for name in
                                ("used", "capacity", "cumulative", "input", "output", "usageage")))
            with patch.object(owner._usage, "poll", return_value=((values, 999.5), context)):
                _, refreshed = owner.snapshot_bundle()
        self.assertEqual(refreshed["used"], "120")
        self.assertEqual(refreshed["usageage"], "500")
        self.assertEqual(active["modelage"], "10000")
        self.assertEqual(pending["modelage"], active["modelage"])
        self.assertEqual(refreshed["modelage"], active["modelage"])

    # self: Real ActivityStore projection with deterministic native reader data and catalog boundary.
    def test_native_model_prices_the_same_identity_and_source_age(self):
        catalog = Mock()
        catalog.projection.return_value = pricing.PriceProjection()
        owner = activity.ActivityStore(catalog)
        with patch.object(activity.time, "monotonic_ns", return_value=1000000000000):
            owner.observe({"hook_event_name": "UserPromptSubmit", "session_id": "chat-a", "cwd": self.cwd,
                           "turn_id": "turn-a", "model": "gpt-6-sol", "emitted_ns": 999000000000})
        with patch.object(owner._usage, "poll", return_value=(None, ("gpt-6-astra", None, 995.0))):
            _, fields = owner.snapshot_bundle()
        self.assertEqual(bytes.fromhex(fields["model"]).decode(), "gpt-6-astra")
        self.assertEqual(fields["effort"], "-")
        self.assertEqual(fields["modelage"], "5000")
        catalog.projection.assert_called_once_with("gpt-6-astra")
        catalog.reset_mock()
        with patch.object(owner._usage, "poll", return_value=(None, ("gpt-6-astra", "high", 800.0))):
            _, hook_fields = owner.snapshot_bundle()
            with patch.object(activity.time, "monotonic", return_value=1010.0):
                _, later_fields = owner.snapshot_bundle()
        self.assertEqual(bytes.fromhex(hook_fields["model"]).decode(), "gpt-6-sol")
        self.assertEqual(hook_fields["modelage"], "1000")
        self.assertEqual(later_fields["modelage"], "11000")
        self.assertEqual(hook_fields["effort"], "-")
        self.assertEqual(later_fields["effort"], "-")
        self.assertEqual(catalog.projection.call_count, 2)
        catalog.projection.assert_called_with("gpt-6-sol")
        catalog.reset_mock()
        with patch.object(owner._usage, "poll", return_value=(None, ("gpt-6-astra", "high", 800.0))), \
                patch.object(activity.time, "monotonic", return_value=1200.0):
            _, stale_fields = owner.snapshot_bundle()
        self.assertEqual(bytes.fromhex(stale_fields["model"]).decode(), "gpt-6-astra")
        self.assertEqual(stale_fields["modelage"], "400000")
        self.assertEqual(stale_fields["effort"], "high".encode().hex())
        catalog.projection.assert_not_called()


class NativeChatTitleTests(unittest.TestCase):
    # self: Synthetic SQLite metadata owner under artifacts; no real title or transcript is read.
    def setUp(self) -> None:
        output = ROOT / "artifacts/checks/cody-ccp"
        output.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=output)
        self.addCleanup(temporary.cleanup)
        self.home = Path(temporary.name)
        self.database = self.home / "state_5.sqlite"
        self.cwd = str(self.home / "checkout")
        environment = patch.dict(os.environ, {"CODEX_HOME": str(self.home)})
        environment.start()
        self.addCleanup(environment.stop)
        self.now = 100.0
        self.reader = native_title.NativeChatTitleReader(lambda: self.now)
        with contextlib.closing(sqlite3.connect(self.database)) as connection:
            connection.execute("CREATE TABLE threads(id TEXT PRIMARY KEY, cwd TEXT, history_mode TEXT, title TEXT, name TEXT)")
            connection.commit()

    # self: Ephemeral native index fixture.
    # session: Synthetic native identity.
    # title: Stored legacy title, deliberately distinct from an explicit name.
    # name: Optional native user-facing name for paginated threads.
    # mode: Real native history discriminator or an intentionally unsupported fixture.
    # cwd: Optional contradictory checkout identity.
    # Raises sqlite3.Error if fixture construction fails.
    def put(self, session="chat-a", title="Original title", name=None, mode="legacy", cwd=None) -> None:
        with contextlib.closing(sqlite3.connect(self.database)) as connection:
            connection.execute("INSERT OR REPLACE INTO threads VALUES(?,?,?,?,?)",
                               (session, self.cwd if cwd is None else cwd, mode, title, name))
            connection.commit()

    # self: Real native history/name semantics and a rename observed through the bounded cache.
    # Raises AssertionError if names are guessed, cadence renews age, or the source database changes.
    def test_exact_native_title_semantics_cadence_and_read_only_source(self) -> None:
        self.put(title="Actual legacy title", name="Do not substitute this name")
        before = self.database.read_bytes()
        first = self.reader.poll("chat-a", self.cwd)
        self.assertEqual((first.title, first.state, first.observed_at),
                         ("Actual legacy title", native_title.TitleState.CURRENT, 100.0))
        self.assertEqual(self.database.read_bytes(), before)
        self.put(title="PRIVATE oversized derived title " * 10000, name="The actual renamed chat", mode="paginated")
        self.now += 4.99
        with patch.object(native_title.sqlite3, "connect", side_effect=AssertionError("Cache must not reread SQLite")):
            self.assertIs(self.reader.poll("chat-a", self.cwd), first)
        self.now = 105.0
        renamed = self.reader.poll("chat-a", self.cwd)
        self.assertEqual((renamed.title, renamed.observed_at), ("The actual renamed chat", 105.0))
        self.put(title="Never use a derived title for an unnamed paginated chat", mode="paginated")
        self.now += 5.0
        self.assertEqual(self.reader.poll("chat-a", self.cwd), native_title.ChatTitleObservation())

    # self: Two real metadata rows in an isolated SQLite fixture and one followed-title cache.
    # Raises AssertionError if a completion read changes source bytes, evicts the cache or retains failed text.
    def test_one_shot_completion_title_preserves_followed_cache_and_read_only_source(self) -> None:
        self.put(title="SIMULATED background chat")
        self.put("chat-b", title="SIMULATED followed chat")
        followed = self.reader.poll("chat-b", self.cwd)
        self.now += 1.0
        before = self.database.read_bytes()
        completed = self.reader.read_once("chat-a", self.cwd)
        self.assertEqual((completed.title, completed.state, completed.observed_at),
                         ("SIMULATED background chat", native_title.TitleState.CURRENT, self.now))
        self.assertEqual(self.database.read_bytes(), before)
        with patch.object(self.reader, "_read", side_effect=AssertionError("Followed cache must remain warm")):
            self.assertIs(self.reader.poll("chat-b", self.cwd), followed)
        diagnostics = io.StringIO()
        failure = sqlite3.OperationalError("PRIVATE completion index context")
        with contextlib.redirect_stderr(diagnostics), patch.object(native_title.sqlite3, "connect", side_effect=failure):
            failed = self.reader.read_once("chat-a", self.cwd)
        self.assertEqual(failed, native_title.ChatTitleObservation(state=native_title.TitleState.ERROR))
        self.assertIn("OperationalError", diagnostics.getvalue())
        self.assertNotIn("PRIVATE", diagnostics.getvalue())
        self.assertIs(self.reader.poll("chat-b", self.cwd), followed)

    # self: Production activity/title owners with a long-running background chat and an exact later rename.
    # Raises AssertionError if stale title age prevents refresh, Stop reads usage or a duplicate repeats source IO.
    def test_background_stop_refreshes_exact_native_title_without_follow_switch_or_usage_read(self) -> None:
        self.put(title="SIMULATED original chat")
        self.put("chat-b", title="SIMULATED followed chat")
        owner = activity.ActivityStore()
        owner._titles = self.reader
        with patch.object(activity.time, "monotonic", side_effect=lambda: self.now), \
                patch.object(activity.time, "monotonic_ns", side_effect=lambda: int(self.now * 1_000_000_000) + 1), \
                patch.object(owner._usage, "poll", return_value=(None, None)) as usage:
            owner.observe({"hook_event_name": "UserPromptSubmit", "session_id": "chat-a", "turn_id": "turn-a",
                           "cwd": self.cwd, "emitted_ns": int(self.now * 1_000_000_000)})
            owner.snapshot_bundle()
            self.now += 1.0
            owner.observe({"hook_event_name": "UserPromptSubmit", "session_id": "chat-b", "turn_id": "turn-b",
                           "cwd": self.cwd, "emitted_ns": int(self.now * 1_000_000_000)})
            owner.snapshot_bundle()
            usage.reset_mock()
            self.now += 46.0
            self.put(title="SIMULATED renamed completed chat")
            before = self.database.read_bytes()
            with patch.object(owner._titles, "read_once", wraps=owner._titles.read_once) as read_once:
                observation = owner.observe({"hook_event_name": "Stop", "session_id": "chat-a", "turn_id": "turn-a",
                    "cwd": self.cwd, "emitted_ns": int(self.now * 1_000_000_000)})
                self.assertEqual(observation.title, "")
                refreshed = owner.refresh_reply_title(observation)
                self.assertEqual((refreshed.followed, refreshed.reply_ready, refreshed.title),
                                 (False, True, "SIMULATED renamed completed chat"))
                self.now += 1.0
                repeated = owner.observe({"hook_event_name": "Stop", "session_id": "chat-a", "turn_id": "turn-a",
                    "cwd": self.cwd, "emitted_ns": int(self.now * 1_000_000_000)})
                self.assertFalse(owner.refresh_reply_title(repeated).reply_ready)
                read_once.assert_called_once_with("chat-a", self.cwd)
            usage.assert_not_called()
            self.assertEqual(self.database.read_bytes(), before)
            self.assertEqual([item["session_id"] for item in owner.list_sessions() if item["followed"]], ["chat-b"])

    # self: Several chats in one checkout, absent identities and a contradictory native checkout.
    # Raises AssertionError if any other chat supplies a title or stale cache survives a follow switch.
    def test_title_requires_exact_followed_identity_and_checkout(self) -> None:
        self.put(title="First real title")
        self.put("chat-b", title="Second real title")
        self.assertEqual(self.reader.poll("chat-a", self.cwd).title, "First real title")
        self.assertEqual(self.reader.poll("chat-b", self.cwd).title, "Second real title")
        self.assertEqual(self.reader.poll("unknown", self.cwd), native_title.ChatTitleObservation())
        self.put("chat-wrong", title="PRIVATE mismatched title", cwd=str(self.home / "other-checkout"))
        diagnostics = io.StringIO()
        with contextlib.redirect_stderr(diagnostics):
            rejected = self.reader.poll("chat-wrong", self.cwd)
        self.assertEqual(rejected.state, native_title.TitleState.UNSUPPORTED)
        self.assertEqual(rejected.title, "")
        self.assertIn("checkout mismatch", self.reader.error)
        self.assertNotIn("PRIVATE", diagnostics.getvalue())
        self.assertNotIn(self.cwd, diagnostics.getvalue())

    # self: Invalid text, oversized native metadata and unsupported history modes.
    # Raises AssertionError if invalid data is truncated, transliterated, logged or replaced by another field.
    def test_title_content_is_bounded_and_never_repaired(self) -> None:
        self.put(title="T" * 255)
        self.assertEqual(self.reader.poll("chat-a", self.cwd).title, "T" * 255)
        cases = (("T" * 256, None, "legacy"), ("PRIVATE\nmultiline", None, "legacy"),
                 ("PRIVATE caf\u00e9", None, "legacy"), ("   ", None, "legacy"),
                 ("Fallback", "N" * 256, "paginated"),
                 ("Fallback", "PRIVATE\x00name", "paginated"), ("Fallback", "Name", "unknown"))
        for title, name, mode in cases:
            with self.subTest(mode=mode, title_length=len(title), name_length=len(name) if name else 0):
                self.put(title=title, name=name, mode=mode)
                self.now += 5.0
                diagnostics = io.StringIO()
                with contextlib.redirect_stderr(diagnostics):
                    rejected = self.reader.poll("chat-a", self.cwd)
                self.assertEqual((rejected.title, rejected.state, rejected.observed_at),
                                 ("", native_title.TitleState.UNSUPPORTED, None))
                self.assertNotIn("PRIVATE", diagnostics.getvalue())
        self.put(title="Recovered title")
        self.now += 5.0
        self.assertEqual(self.reader.poll("chat-a", self.cwd).title, "Recovered title")
        self.assertEqual(self.reader.error, "")

    # self: A successful observation followed by a real source read error and bounded retry.
    # Raises AssertionError if failure leaves current text or retries on every device heartbeat.
    def test_source_failure_retires_title_and_preserves_error_without_hot_retry(self) -> None:
        self.put(title="Valid before failure")
        self.reader.poll("chat-a", self.cwd)
        self.now += 5.0
        failure = sqlite3.OperationalError("PRIVATE database context")
        diagnostics = io.StringIO()
        with contextlib.redirect_stderr(diagnostics), patch.object(native_title.sqlite3, "connect", side_effect=failure) as connect:
            failed = self.reader.poll("chat-a", self.cwd)
            self.now += 1.0
            self.assertIs(self.reader.poll("chat-a", self.cwd), failed)
        connect.assert_called_once()
        self.assertEqual(failed, native_title.ChatTitleObservation(state=native_title.TitleState.ERROR))
        self.assertIn("OperationalError", self.reader.error)
        self.assertNotIn("PRIVATE", diagnostics.getvalue())

    # self: Production ActivityStore with real SQLite titles and an isolated unavailable usage source.
    # Raises AssertionError if renames lose revision, cached age renews, or a concurrent follow leaks a title.
    def test_activity_projection_binds_title_revision_age_and_follow_identity(self) -> None:
        self.put(title="First title")
        self.put("chat-b", title="Second title")
        owner = activity.ActivityStore()
        owner._titles = self.reader
        with patch.object(activity.time, "monotonic", side_effect=lambda: self.now), \
                patch.object(activity.time, "monotonic_ns", side_effect=lambda: int(self.now * 1_000_000_000) + 1), \
                patch.object(owner._usage, "poll", return_value=(None, None)):
            owner.observe({"hook_event_name": "UserPromptSubmit", "session_id": "chat-a", "turn_id": "turn-a",
                           "cwd": self.cwd, "emitted_ns": int(self.now * 1_000_000_000)})
            first, technical = owner.snapshot_bundle()
            self.assertEqual((bytes.fromhex(first["title"]).decode("ascii"), first["titlestate"], first["titleage"]),
                             ("First title", "current", "0"))
            self.assertEqual(first["revision"], technical["revision"])
            self.now += 1.0
            cached, _ = owner.snapshot_bundle()
            self.assertEqual((cached["revision"], cached["titleage"]), (first["revision"], "1000"))
            self.put(title="Renamed title")
            self.now += 4.0
            renamed, technical = owner.snapshot_bundle()
            self.assertEqual(bytes.fromhex(renamed["title"]).decode("ascii"), "Renamed title")
            self.assertNotEqual(renamed["revision"], first["revision"])
            self.assertEqual(renamed["revision"], technical["revision"])

            # A hook can legitimately change ownership while source IO is in
            # progress. The second lock must discard the previous chat's title.
            # args: Original title reader arguments; the fixture simulates an intervening genuine hook.
            # Raises ValueError if native hook correlation rejects the synthetic switch.
            def switch_followed_chat(*args):
                owner.observe({"hook_event_name": "UserPromptSubmit", "session_id": "chat-b", "turn_id": "turn-b",
                               "cwd": self.cwd, "emitted_ns": int(self.now * 1_000_000_000)})
                return native_title.ChatTitleObservation("Wrong chat title", native_title.TitleState.CURRENT, self.now)

            self.now += 1.0
            with patch.object(owner._titles, "poll", side_effect=switch_followed_chat):
                raced, _ = owner.snapshot_bundle()
            self.assertEqual((raced["title"], raced["titlestate"], raced["titleage"]), ("-", "unavailable", "-"))
            current, _ = owner.snapshot_bundle()
            self.assertEqual(bytes.fromhex(current["title"]).decode("ascii"), "Second title")
            self.now += 1.0
            notice = owner.observe({"hook_event_name": "Stop", "session_id": "chat-a", "turn_id": "turn-a",
                                    "cwd": self.cwd, "emitted_ns": int(self.now * 1_000_000_000)})
            self.assertEqual((notice.followed, notice.title), (False, "Renamed title"))
            self.now += 45.0
            expired = owner.observe({"hook_event_name": "Stop", "session_id": "chat-a", "turn_id": "turn-a",
                                     "cwd": self.cwd, "emitted_ns": int(self.now * 1_000_000_000)})
            self.assertEqual(expired.title, "")


class NativeTechnicalUsageTests(unittest.TestCase):
    # self: Existing public CCP suite; the host compiler runs only under its explicit authorization.
    # Raises original extraction, compiler, filesystem or host assertion failures.
    def test_actual_firmware_usage_parsers(self):
        output = ROOT / "artifacts/checks/cody-ccp-technical"
        output.mkdir(parents=True, exist_ok=True)
        path = ROOT / "components/LUNASHELL_platform/src/network/LUNASHELL_codex_link.cpp"
        source = path.read_text(encoding="utf-8")
        extracted = "\n\n".join(function_text(source, signature, str(path)) for signature in
                                ("bool parseTechnicalCount(", "bool parseTechnicalDuration("))
        (output / "technical_usage_under_test.inc").write_text(extracted, encoding="utf-8")
        executable = output / "technical-usage.exe"
        command = compile_command(resolve_compiler(None), [str(output)],
                                  ["scripts/tests/cody_native/technical_usage_contract.cpp"], executable)
        run_stage(command, output, "compile")
        run_stage([str(executable)], output, "contract")


if __name__ == "__main__":
    unittest.main(verbosity=2)
