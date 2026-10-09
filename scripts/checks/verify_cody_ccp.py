"""Host CCP regression checks; synthetic clocks/keys, no LAN, Codex or device execution."""

from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))
import ccp_server as ccp
import ccp_activity as activity
import ccp_runtime as runtime


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

    # self: Synthetic authenticated rediscovery fixture; the UDP socket is a mock.
    # Raises AssertionError if an invalid proof marks presence or READY loses nonce binding.
    def test_presence_requires_verified_find_and_ready(self) -> None:
        state = Mock()
        state.pc_id = "a" * 32
        link_key = bytes(range(32))
        state.link_key.return_value = link_key
        responder = ccp.CompanionResponder(state, Mock(), ccp.ActivityStore())
        device_id = "abcdef012345"
        nonce = "b" * 32
        fields = [("device", device_id), ("pc", state.pc_id), ("nonce", nonce)]
        invalid = ccp.encode_datagram("FIND", fields + [("proof", "0" * 64)])
        with self.assertRaisesRegex(ValueError, "proof does not match"):
            responder._handle_find(invalid, ("192.0.2.10", ccp.DEVICE_PORT))
        state.mark_seen.assert_not_called()
        responder._socket.sendto.assert_not_called()

        proof = ccp.make_proof(link_key, "find", device_id, state.pc_id, nonce)
        valid = ccp.encode_datagram("FIND", fields + [("proof", proof)])
        responder._handle_find(valid, ("192.0.2.10", ccp.DEVICE_PORT))
        payload, destination = responder._socket.sendto.call_args.args
        kind, response = ccp.parse_datagram(payload)
        self.assertEqual(kind, "READY")
        self.assertEqual(response["nonce"], nonce)
        expected_proof = ccp.make_proof(link_key, "ready", device_id, state.pc_id, nonce)
        self.assertEqual(response["proof"], expected_proof)
        self.assertEqual(destination, ("192.0.2.10", ccp.DEVICE_PORT))
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
        service._matching_offers = Mock(return_value=[(offer, bytes(32))])
        operations = Mock()
        service._exchange_pairing_request = operations.device_saved
        service.state.remember_pairing = operations.pc_saved
        service._exchange_confirmation = operations.active
        operations.active.side_effect = TimeoutError("synthetic missing ACTIVE")
        with self.assertRaisesRegex(TimeoutError, "missing ACTIVE"):
            service.pair("ABCDEF")
        completed_steps = [call[0] for call in operations.mock_calls]
        self.assertEqual(completed_steps, ["device_saved", "pc_saved", "active"])

    # self: Source-only plugin catalog fixture.
    # Raises AssertionError if packaging versions, local paths or MCP entrypoints diverge.
    def test_local_plugin_entrypoints_agree(self) -> None:
        plugin_root = ROOT / "plugins/cody-codex-plugin"
        marketplace = json.loads((ROOT / ".agents/plugins/marketplace.json").read_text(encoding="utf-8"))
        entry = marketplace["plugins"][0]
        self.assertEqual((ROOT / entry["source"]["path"]).resolve(), plugin_root.resolve())
        for manifest_path in ("plugin.json", ".codex-plugin/plugin.json"):
            manifest = json.loads((plugin_root / manifest_path).read_text(encoding="utf-8"))
            self.assertEqual(manifest["version"], ccp.SERVER_VERSION)
        for wiring_path in ("mcp.json", ".mcp.json"):
            wiring = json.loads((plugin_root / wiring_path).read_text(encoding="utf-8"))["mcpServers"]["ccp"]
            self.assertEqual(wiring["cwd"], "${PLUGIN_ROOT}")
            self.assertEqual(wiring["args"], ["-B", "${PLUGIN_ROOT}/mcp/ccp_server.py", "--stdio"])


class ChatActivityTests(unittest.TestCase):
    # self: Pure host fixture with no Codex, sockets, protected state or physical device.
    def setUp(self) -> None:
        self.store = activity.ActivityStore()
        self.event_time = 0

    # self: In-memory activity fixture.
    # name: Genuine-hook schema event represented synthetically for this host test.
    # session: Opaque chat identity.
    # turn: Opaque turn identity.
    # fields: Additional allowlisted tool metadata.
    # Raises ValueError when the tested owner rejects the event.
    def observe(self, name, session="chat-a", turn="turn-a", **fields) -> None:
        self.event_time += 1
        with patch.object(activity.time, "monotonic_ns", return_value=1_000_000):
            self.store.observe({"hook_event_name": name, "session_id": session,
                                "cwd": "E:/synthetic-project", "turn_id": turn,
                                "emitted_ns": self.event_time, **fields})

    # self: Multiple chats and one explicit device selection.
    # Raises AssertionError if another chat changes the selected device state.
    def test_selection_is_explicit_and_isolated(self) -> None:
        self.observe("SessionStart")
        self.observe("SessionStart", session="chat-b")
        self.assertEqual(self.store.snapshot("abcdef012345")["phase"], "none")
        self.store.bind("abcdef012345", "chat-a", "Project A")
        self.observe("UserPromptSubmit", session="chat-b")
        self.assertEqual(self.store.snapshot("abcdef012345")["phase"], "active")
        self.observe("UserPromptSubmit")
        self.assertEqual(self.store.snapshot("abcdef012345")["phase"], "working")

    # self: Two consecutive turns in the same selected chat.
    # Raises AssertionError if a late previous-turn Stop completes a newer turn.
    def test_old_turn_completion_cannot_finish_current_turn(self) -> None:
        self.observe("UserPromptSubmit")
        self.store.bind("abcdef012345", "chat-a", "Project")
        self.observe("UserPromptSubmit", turn="turn-b")
        with self.assertRaisesRegex(ValueError, "turn does not match"):
            self.observe("Stop")
        self.assertEqual(self.store.snapshot("abcdef012345")["phase"], "working")

    # self: Selected active turn with a synthetic monotonic lease.
    # Raises AssertionError if a PC heartbeat or wall time keeps activity current.
    def test_active_observation_expires_without_inventing_success(self) -> None:
        with patch.object(activity.time, "monotonic", return_value=100.0):
            self.observe("UserPromptSubmit")
            self.store.bind("abcdef012345", "chat-a", "Project")
        with patch.object(activity.time, "monotonic", return_value=221.0):
            self.assertEqual(self.store.snapshot("abcdef012345")["phase"], "unavailable")
        self.observe("Stop")
        self.assertEqual(self.store.snapshot("abcdef012345")["phase"], "ready")

    # self: Concurrent command/edit operations on one turn.
    # Raises AssertionError if finishing one operation erases another live operation.
    def test_concurrent_tools_retain_identity(self) -> None:
        self.observe("UserPromptSubmit")
        self.store.bind("abcdef012345", "chat-a", "Project")
        self.observe("PreToolUse", tool_use_id="tool-command", tool_name="Bash")
        self.observe("PreToolUse", tool_use_id="tool-edit", tool_name="apply_patch")
        self.observe("PostToolUse", tool_use_id="tool-edit")
        self.assertEqual(self.store.snapshot("abcdef012345")["phase"], "running")
        self.observe("Interrupt")
        self.observe("PostToolUse", tool_use_id="tool-command")
        self.assertEqual(self.store.snapshot("abcdef012345")["phase"], "paused")
        with self.assertRaisesRegex(ValueError, "Interrupted turn"):
            self.observe("Stop")

    # self: Existing project binding with malformed replacement metadata.
    # Raises AssertionError if rejected values partially replace the selected chat.
    def test_invalid_selection_preserves_previous_binding(self) -> None:
        self.observe("SessionStart")
        self.store.bind("abcdef012345", "chat-a", "Project")
        previous = self.store.snapshot("abcdef012345")
        for label in ("", "a" * 25, "a\x00b", "accent\u00e9"):
            with self.assertRaises(ValueError):
                self.store.bind("abcdef012345", "chat-a", label)
        with self.assertRaises(ValueError):
            self.store.bind("abcdef012345", "unknown-chat", "Project")
        self.assertEqual(self.store.snapshot("abcdef012345"), previous)

    # self: Authenticated synthetic responder and maximum display label.
    # Raises AssertionError if ACTIVITY lacks nonce/HMAC binding or exceeds the wire limit.
    def test_activity_reply_authenticates_every_field(self) -> None:
        self.observe("UserPromptSubmit")
        self.store.bind("abcdef012345", "chat-a", "P" * 24)
        state = Mock(pc_id="a" * 32)
        state.link_key.return_value = bytes(range(32))
        listener = Mock()
        responder = ccp.CompanionResponder(state, listener, self.store)
        values = ["abcdef012345", state.pc_id, "b" * 32]
        proof = ccp.make_proof(state.link_key.return_value, "find-activity", *values)
        find = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values)) +
                                   [("activity", "1"), ("proof", proof)])
        responder._handle_find(find, ("192.0.2.10", ccp.DEVICE_PORT))
        raw, _ = listener.sendto.call_args.args
        kind, fields = ccp.parse_datagram(raw)
        self.assertEqual(kind, "ACTIVITY")
        self.assertLessEqual(len(raw), 512)
        authenticated = [fields[key] for key in ("device", "pc", "nonce", "chat", "revision", "phase", "project")]
        expected = ccp.make_proof(state.link_key.return_value, "activity", *authenticated)
        self.assertEqual(fields["proof"], expected)
        self.assertEqual(fields["nonce"], values[2])
        self.assertEqual(bytes.fromhex(fields["project"]), b"P" * 24)
        listener.reset_mock()
        # Stripping the capability cannot reuse its proof as a presence-only FIND.
        stripped = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values)) + [("proof", proof)])
        with self.assertRaises(ValueError):
            responder._handle_find(stripped, ("192.0.2.10", ccp.DEVICE_PORT))
        listener.sendto.assert_not_called()

    # self: Synthetic broker restart without any stored activity state.
    # Raises AssertionError if a new owner auto-selects a previously observed chat.
    def test_restart_and_explicit_clear_require_new_selection(self) -> None:
        self.observe("SessionStart")
        self.store.bind("abcdef012345", "chat-a", "Project")
        self.store.unbind("abcdef012345")
        self.assertEqual(self.store.snapshot("abcdef012345")["phase"], "none")
        restarted = activity.ActivityStore()
        self.assertEqual(restarted.list_sessions(), [])
        self.assertEqual(restarted.snapshot("abcdef012345")["phase"], "none")

    # self: Bounded same-user control requests represented without opening sockets.
    # Raises AssertionError if releasing one transport drops another's lease.
    def test_shared_runtime_releases_only_its_own_lease(self) -> None:
        server = Mock()
        server.lease_lock = threading.Lock()
        server.leases = {}
        runtime.dispatch(server, "lease", {"id": "a" * 32})
        runtime.dispatch(server, "lease", {"id": "b" * 32})
        runtime.dispatch(server, "release", {"id": "a" * 32})
        self.assertEqual(set(server.leases), {"b" * 32})

    # self: Hook ingestion fixture containing private text that must never reach the broker.
    # Raises AssertionError if ingestion forwards a prompt, transcript, response or tool input.
    def test_hook_ingestion_discards_private_contents(self) -> None:
        original = {"hook_event_name": "UserPromptSubmit", "session_id": "chat-a",
                    "turn_id": "turn-a", "cwd": "E:/synthetic-project", "prompt": "private prompt",
                    "transcript_path": "private transcript", "tool_input": {"secret": "private"},
                    "last_assistant_message": "private response"}
        input_stream = Mock(buffer=io.BytesIO(json.dumps(original).encode("utf-8")))
        with patch.object(runtime.sys, "stdin", input_stream), contextlib.redirect_stdout(io.StringIO()):
            with patch.object(runtime, "ensure_runtime", return_value="synthetic"), patch.object(runtime, "exchange") as exchange:
                runtime.run_hook()
        submitted = exchange.call_args.args[1]
        self.assertEqual(set(submitted), {"hook_event_name", "session_id", "turn_id", "cwd", "emitted_ns"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
