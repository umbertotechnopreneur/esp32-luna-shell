"""Host CCP regression checks; synthetic clocks/keys, no LAN, Codex or device execution."""

from __future__ import annotations

import contextlib
import errno
import hashlib
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
import ccp_windows as windows
import ccp_interactions as interactions
from cody_ccp_event_vectors import event_vectors


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
        committed = ccp.encode_datagram("COMMITTED", fields + [("proof", ccp.make_proof(new_key, "committed", *values))])
        responder._handle_datagram(find, source)
        self.assertEqual(state.retained_keys(values[0]), ccp.RetainedKeys(old_key, new_key))
        with self.assertRaisesRegex(ValueError, "recent authenticated FIND"):
            responder._handle_datagram(committed, (source[0], source[1] + 1))
        forged = ccp.encode_datagram("COMMITTED", fields + [("proof", "0" * 64)])
        with self.assertRaisesRegex(ValueError, "proof is invalid"):
            responder._handle_datagram(forged, source)
        state.stage_pairing(values[0], bytes(32), "127.0.0.1", True)
        with self.assertRaisesRegex(ValueError, "superseded candidate"):
            responder._handle_datagram(committed, source)
        self.assertEqual(state.retained_keys(values[0]).active, old_key)

    # self: A durable PC candidate receiving a valid device commit and later repeated presence.
    # Raises AssertionError if missing ACK retry requires a fresh code or duplicate acknowledgement mutates identity.
    def test_committed_retry_promotes_once_and_remains_idempotent(self):
        state, _, new_key = self.transaction_fixture()
        state.stage_pairing("abcdef012345", new_key, "127.0.0.1")
        responder = ccp.CompanionResponder(state, Mock(), ccp.ActivityStore())
        source = ("127.0.0.1", ccp.DEVICE_PORT)
        for nonce in ("a" * 32, "b" * 32):
            values = ["abcdef012345", state.pc_id, nonce]
            fields = list(zip(("device", "pc", "nonce"), values))
            responder._handle_datagram(ccp.encode_datagram("FIND", fields +
                [("proof", ccp.make_proof(new_key, "find", *values))]), source)
            responder._handle_datagram(ccp.encode_datagram("COMMITTED", fields +
                [("proof", ccp.make_proof(new_key, "committed", *values))]), source)
            self.assertEqual(state.retained_keys(values[0]), ccp.RetainedKeys(new_key, None))

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
                                "cwd": fields.pop("cwd", "E:/synthetic-project"), "turn_id": turn,
                                "emitted_ns": self.event_time, **fields})

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
        self.assertLessEqual(len(label), 24)
        self.assertTrue(label.endswith("..."))
        self.observe("UserPromptSubmit", session="chat-b", cwd="E:/a-very-long-workspace-name-\u00e9")
        self.assertEqual(self.store.snapshot()["project"], first["project"])
        self.assertNotEqual(self.store.snapshot()["chat"], first["chat"])

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

    # self: Public MCP API that must not expose manual chat selection or fake activity.
    # Raises AssertionError if the obsolete selection tools remain callable or advertised.
    def test_manual_selection_is_removed_from_public_tools(self) -> None:
        names = {tool["name"] for tool in ccp.tool_catalog()}
        self.assertEqual(names, {"ccp_discover_devices", "ccp_pair_device",
                                 "ccp_connection_status", "ccp_list_sessions", "ccp_ask_question",
                                 "ccp_publish_notification", "ccp_poll_request", "ccp_cancel_request",
                                 "ccp_reset_device", "ccp_factory_reset_device"})
        for name in ("ccp_select_session", "ccp_clear_session"):
            with self.assertRaisesRegex(ValueError, "Unknown CCP tool"):
                ccp.call_tool(Mock(), name, {})

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
        source_activity = Mock()
        source_activity.snapshot.return_value = {"chat": "0" * 64, "revision": "0" * 16,
                                                  "phase": "none", "project": ""}
        for operation in ccp.reset.ResetOperation:
            responder = ccp.CompanionResponder(state, listener, source_activity)
            responder._resets = self.store = ccp.reset.ResetRequests(lambda: self.now)
            identity = self.store.enqueue(self.device, "SIMULATED", hashlib.sha256(key).hexdigest(),
                                           ccp.reset.CONFIRMATION_CODE, operation)
            values = [self.device, pc, self.nonce]
            find = ccp.encode_datagram("FIND", list(zip(("device", "pc", "nonce"), values))
                                      + [("activity", "1"), ("events", "1"),
                                         ("proof", ccp.make_proof(key, "find-events", *values))])
            listener.sendto.reset_mock()
            responder._handle_datagram(find, self.endpoint)
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
        self.assertIn("messaggio utente corrente", skill)
        self.assertIn("resetta il device codice 043533477", skill)
        self.assertIn("formatta cody 043533477", skill)
        definitions = {item["name"]: item for item in ccp.tool_catalog()}
        self.assertFalse(definitions["ccp_reset_device"]["annotations"]["destructiveHint"])
        self.assertTrue(definitions["ccp_factory_reset_device"]["annotations"]["destructiveHint"])


class InteractionTests(unittest.TestCase):
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
            [("activity", "1"), ("events", "1"), ("proof", ccp.make_proof(key, "find-events", *values))])
        responder._handle_datagram(find, source)
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
        for name, encoded, valid in event_vectors():
            with self.subTest(vector=name):
                if valid:
                    payload = interactions.decode_payload(encoded)
                    self.assertEqual(interactions.encode_payload(payload), encoded)
                    event = ccp.encode_datagram("EVENT", [("device", self.device), ("pc", "a" * 32),
                        ("nonce", "b" * 32), ("id", "c" * 32), ("state", "pending"), ("ttl", "180"),
                        ("data", encoded), ("proof", "d" * 64)])
                    self.assertLessEqual(len(event), 1280)
                    self.assertEqual(ccp.parse_datagram(event)[0], "EVENT")
                else:
                    with self.assertRaises(ValueError):
                        interactions.decode_payload(encoded)
        with self.assertRaises(ValueError):
            ccp.encode_datagram("PAIR", [("data", "0" * 510)])
        with self.assertRaises(ValueError):
            ccp.parse_datagram(b"CCP1|PAIR|data=" + b"0" * 510)

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
        with self.assertRaisesRegex(ValueError, "pending question"):
            self.store.enqueue(self.device, self.payload, "LAN")
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
            [("activity", "1"), ("events", "1"), ("proof", ccp.make_proof(key, "find-events", *values))])
        source = ("192.0.2.10", ccp.DEVICE_PORT)
        responder._handle_datagram(find, source)
        kind, event = ccp.parse_datagram(listener.sendto.call_args.args[0])
        self.assertEqual(kind, "EVENT")
        self.assertEqual(event["id"], created["request_id"])
        self.assertEqual(interactions.decode_payload(event["data"]), self.payload)
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
            [("activity", "1"), ("proof", ccp.make_proof(key, "find-events", *values))])
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
        with self.assertRaisesRegex(ValueError, "No recent authenticated Debug"):
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
        result = self.responder.debug_screen(self.device, "face_channels", 2, True)
        self.assertEqual(result["status"], "rejected")
        self.assertEqual(result["error"], -1)
        self.assertEqual(result["channel_count"], 34)
        self.assertFalse(result["pixels_verified"])
        self.assertEqual(result["source"], "SIMULATED")
        self.responder._handle_datagram(self.capability(), self.source)
        with self.assertRaisesRegex(ValueError, "No recent authenticated Debug"):
            self.responder.debug_screen(self.device, "normal", 0, True)
        self.listener.sendto.assert_called_once()

    # self: Malformed MCP inputs which must remain structured invalid-command errors.
    # Raises AssertionError if nonhashable modes or non-list offsets escape as TypeError or authorize a command.
    def test_debug_rejects_untyped_commands_before_network_use(self):
        for mode in ([], {}, None):
            with self.assertRaises(ValueError):
                self.responder.debug_screen(self.device, mode, 0, True)
        for mode, scroll in (("normal", 1), ("face_channels", True), ("face_channels", 256)):
            with self.assertRaises(ValueError):
                self.responder.debug_screen(self.device, mode, scroll, True)
        self.listener.sendto.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
