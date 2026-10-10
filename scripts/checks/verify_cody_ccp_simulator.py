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

"""SIMULATED CCP1 regressions, including ephemeral loopback UDP; no Tk/device/live hooks."""

from __future__ import annotations

import contextlib
from dataclasses import asdict, replace
import hashlib
import hmac
import io
import os
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "simulator/windows"))
sys.path.insert(0, str(ROOT))
import cody_ccp_simulator as simulated
from cody_ccp_panel import presentation
from cody_ccp_event_view import EventView, paint_action, wrapped
from cody_scene import ScenePainter
from cody_native import NativeRig
from scripts.common.cody_rig_contract import load_contract
from PIL import Image

protocol = simulated.protocol
interactions = simulated.interactions


# key: Synthetic retained device key for this isolated fixture.
# ready: Exact endpoint-bound challenge from the real responder.
# Raises ValueError if the held packet is not a complete READY.
def committed_for_ready(key, ready):
    kind, fields = protocol.parse_datagram(ready)
    names = ("device", "pc", "nonce", "challenge", "address", "port")
    if kind != "READY" or set(fields) != {*names, "proof"}:
        raise ValueError("Fixture requires an exact fresh READY challenge")
    values = [fields[name] for name in names]
    return protocol.encode_datagram("COMMITTED", list(zip(names, values))
        + [("proof", protocol.make_proof(key, "committed-v2", *values))])


# phase: Explicit synthetic work phase.
# project: Explicitly SIMULATED display label.
# Returns coherent activity/technical fixture projections without inventing measured metrics.
def synthetic_activity(phase, project):
    owner = Mock()
    activity, technical = protocol.ActivityStore().snapshot_bundle()
    activity.update(chat="d" * 64, revision="1" * 16, phase=phase, project=project.encode("ascii").hex())
    technical.update(chat=activity["chat"], revision=activity["revision"], activityage="0")
    owner.snapshot_bundle.return_value = activity, technical
    return owner


class SimulatorProtocolTests(unittest.TestCase):
    # self: Pure protocol fixture with deterministic clocks and isolated temporary persistence.
    # Raises OSError if its artifacts directory cannot be created.
    def setUp(self):
        directory = ROOT / "artifacts/checks/cody-ccp-simulator"
        directory.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / "device.json"
        # Only the DPAPI boundary is mocked: packets, derivation and authentication are real.
        protect = patch.object(protocol, "protect_secret", side_effect=lambda value, description: value.hex())
        unprotect = patch.object(protocol, "unprotect_secret", side_effect=bytes.fromhex)
        protect.start()
        unprotect.start()
        self.addCleanup(protect.stop)
        self.addCleanup(unprotect.stop)
        self.stderr = io.StringIO()
        diagnostics = contextlib.redirect_stderr(self.stderr)
        diagnostics.__enter__()
        self.addCleanup(diagnostics.__exit__, None, None, None)
        self.now = 100.0
        self.store = simulated.SimulatorStore(self.path)
        self.link = simulated.SimulatorLink(self.store, lambda: self.now, lambda: 1000 + self.now)
        self.send = Mock()
        self.source = (simulated.LOOPBACK, 54321)
        self.pc = "a" * 32

    # self: Fixture collecting real protocol messages without creating a socket.
    # replace: Explicit user confirmation when the fixture already has a retained association.
    # Returns the authenticated confirmation fields and derived candidate key.
    # Raises AssertionError if the real OFFER/PAIR/PAIRED handshake is incoherent.
    def pair_candidate(self, replace=False):
        self.link.begin_pairing(replace)
        code = self.link.display_code()
        request = "b" * 16
        self.link.handle_datagram(protocol.encode_datagram("DISCOVER", [("request", request)]), self.source, self.send)
        kind, fields = protocol.parse_datagram(self.send.call_args.args[0])
        self.assertEqual(kind, "OFFER")
        verifier = protocol.derive_verifier(code, fields["salt"])
        expected = protocol.make_proof(verifier, "offer", request, fields["device"], fields["session"], fields["salt"])
        self.assertEqual(fields["tag"], expected)
        identity = [("device", fields["device"]), ("session", fields["session"]), ("pc", self.pc), ("nonce", "c" * 32)]
        values = [value for _, value in identity]
        proof = protocol.make_proof(verifier, "pair", *values[:3], str(protocol.PLUGIN_PORT), values[3])
        self.link.handle_datagram(protocol.encode_datagram("PAIR", identity +
            [("port", str(protocol.PLUGIN_PORT)), ("proof", proof)]), self.source, self.send)
        kind, paired = protocol.parse_datagram(self.send.call_args.args[0])
        key = hmac.new(verifier, protocol.canonical_text("link", *values), hashlib.sha256).digest()
        self.assertEqual(kind, "PAIRED")
        self.assertEqual(paired["proof"], protocol.make_proof(key, "paired", *values))
        self.assertEqual(self.link.display_code(), "")
        self.assertNotIn(code, self.stderr.getvalue())
        return identity, key

    # self: Fixture whose PC has durably retained the candidate key.
    # identity: Current commissioning device/session/PC/nonce fields.
    # key: Real derived fixture key.
    def confirm(self, identity, key):
        values = [value for _, value in identity]
        payload = protocol.encode_datagram("CONFIRM", identity + [("proof", protocol.make_proof(key, "confirm", *values))])
        self.link.handle_datagram(payload, self.source, self.send)

    # self: Candidate promoted only after PC confirmation, then durable across restarts.
    # Raises AssertionError if candidate admission is mistaken for active pairing.
    def test_pair_confirm_and_restart_preserve_identity(self):
        identity, key = self.pair_candidate()
        self.assertIsNone(self.link.active)
        self.assertIsNotNone(self.link.pending)
        self.confirm(identity, key)
        kind, _ = protocol.parse_datagram(self.send.call_args.args[0])
        self.assertEqual(kind, "ACTIVE")
        self.assertIsNone(self.link.pending)
        self.assertEqual(protocol.unprotect_secret(self.link.active["protected_link_key"]), key)
        restarted = simulated.SimulatorLink(simulated.SimulatorStore(self.path))
        self.assertEqual(restarted.device_id, self.link.device_id)
        self.assertEqual(restarted.state, simulated.LinkState.DISCOVERING)
        self.assertEqual(restarted.display_code(), "")

    # self: Authenticated model coupled directly to the real CCP responder with no OS transport.
    # pending_restart: Exercise recovery of a durable candidate instead of confirmed pairing.
    # drop_committed: Deliberately lose the first post-READY acknowledgement before broker admission.
    # activity_owner: Optional explicit synthetic activity/technical source for extension regressions.
    # Returns accepted responder messages for replay/tampering assertions.
    def exchange_presence(self, pending_restart=False, drop_committed=False, activity_owner=None):
        identity, key = self.pair_candidate()
        if pending_restart:
            self.now += simulated.PAIRING_SECONDS + 1
            self.link = simulated.SimulatorLink(simulated.SimulatorStore(self.path), lambda: self.now, lambda: 1000 + self.now)
        else:
            self.confirm(identity, key)
        state = Mock(pc_id=self.pc)
        state.retained_keys.return_value = protocol.RetainedKeys(None, key) if pending_restart else protocol.RetainedKeys(key, None)
        activity = synthetic_activity("working", "SIMULATED fixture") if activity_owner is None else activity_owner
        listener = Mock()
        self.requests = interactions.InteractionStore(lambda: self.now)
        responder = protocol.CompanionResponder(state, listener, activity, self.requests)
        self.host, self.host_listener = responder, listener
        self.link.tick(self.send)
        find, destination = self.send.call_args.args
        self.assertEqual(destination, (simulated.LOOPBACK, protocol.PLUGIN_PORT))
        responder._handle_datagram(find, (simulated.LOOPBACK, protocol.DEVICE_PORT))
        self.assertEqual([protocol.parse_datagram(call.args[0])[0]
            for call in listener.sendto.call_args_list], ["READY"])
        state.mark_seen.assert_not_called()
        self.accept_presence_packets(drop_committed)
        replies = [call.args[0] for call in listener.sendto.call_args_list]
        if drop_committed:
            state.confirm_device_choice.assert_not_called()
            state.mark_seen.assert_not_called()
        else:
            state.confirm_device_choice.assert_called_once()
        return replies

    # self: Real nonce/HMAC protocol round trip with an explicitly synthetic activity source.
    # Raises AssertionError if rejected/replayed data can become a current snapshot.
    def test_real_responder_activity_and_replay_guard(self):
        replies = self.exchange_presence()
        snapshot = self.link.snapshot()
        self.assertEqual(snapshot.source, "SIMULATED")
        self.assertEqual(snapshot.state, simulated.LinkState.CONNECTED)
        self.assertEqual(snapshot.project, "SIMULATED fixture")
        self.assertTrue(snapshot.activity_valid)
        with self.assertRaisesRegex(ValueError, "one reply per nonce"):
            activity_reply = next(reply for reply in replies if protocol.parse_datagram(reply)[0] == "ACTIVITY")
            self.link.handle_datagram(activity_reply, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.now += simulated.PRESENCE_SECONDS + .1
        self.assertFalse(self.link.snapshot().activity_valid)

    # self: Real paired responder/simulator with one explicitly synthetic current Astra price band.
    # Raises AssertionError if the authenticated TECH extension loses the band, source lease or model identity.
    def test_authenticated_cost_band_reaches_the_simulator(self):
        owner = synthetic_activity("working", "SIMULATED pricing")
        _, technical = owner.snapshot_bundle.return_value
        technical.update(turn="e" * 64, model="gpt-6-astra".encode("ascii").hex(), modelage="0",
                         cost="expensive", costage="1000", costttl="3600000")
        replies = self.exchange_presence(activity_owner=owner)
        snapshot = self.link.snapshot()
        self.assertEqual(snapshot.technical.model, "gpt-6-astra")
        self.assertEqual(snapshot.technical.cost_band.value, "expensive")
        self.assertEqual(snapshot.technical.cost_age_ms, 1000)
        self.assertEqual(snapshot.technical.cost_valid_for_ms, 3600000)
        packet = next(reply for reply in replies if protocol.parse_datagram(reply)[0] == "TECH")
        self.assertLessEqual(len(packet), protocol.MAX_TECHNICAL_DATAGRAM_BYTES)
        with self.assertRaisesRegex(ValueError, "one reply per nonce"):
            self.link.handle_datagram(packet, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().technical.cost_band.value, "expensive")

    # self: Restart after PAIR/PC persistence but before CONFIRM.
    # Raises AssertionError if another visible code is required or unauthenticated state is promoted.
    def test_durable_candidate_recovers_through_authenticated_ready(self):
        self.exchange_presence(pending_restart=True)
        self.assertIsNone(self.link.pending)
        self.assertIsNotNone(self.link.active)
        self.assertEqual(self.link.state, simulated.LinkState.CONNECTED)
        self.assertEqual(self.link.display_code(), "")

    # self: A completed association whose first outbound COMMITTED response is lost.
    # Raises AssertionError if the next valid READY does not resend the durable acknowledgement.
    def test_ready_repeats_committed_after_ack_loss(self):
        replies = self.exchange_presence()
        self.send.reset_mock()
        self.link.handle_datagram(replies[0], (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        kind, fields = protocol.parse_datagram(self.send.call_args.args[0])
        self.assertEqual(kind, "COMMITTED")
        self.assertEqual(fields["nonce"], protocol.parse_datagram(replies[0])[1]["nonce"])
        self.assertEqual(self.link.state, simulated.LinkState.CONNECTED)

    # self: One dropped COMMITTED packet followed by a distinct fresh FIND challenge.
    # Raises AssertionError if lost delivery leaks current data or forces a new display-code pairing.
    def test_lost_committed_recovers_with_the_next_fresh_challenge(self):
        replies = self.exchange_presence(drop_committed=True)
        self.assertEqual([protocol.parse_datagram(packet)[0] for packet in replies], ["READY"])
        self.assertFalse(self.link.snapshot().activity_valid)
        first = protocol.parse_datagram(replies[0])[1]
        self.deliver_events()
        next_ready = protocol.parse_datagram(self.host_listener.sendto.call_args_list[0].args[0])[1]
        self.assertNotEqual(next_ready["challenge"], first["challenge"])
        self.assertTrue(self.link.snapshot().activity_valid)
        self.assertEqual(self.link.display_code(), "")
        self.host._state.confirm_device_choice.assert_called_once()

    # self: Broker challenge honestly signed for a different observed device endpoint.
    # Raises AssertionError if signed relayed READY activates a route or emits a COMMITTED packet.
    def test_signed_ready_for_another_endpoint_is_rejected(self):
        self.exchange_presence()
        self.now += simulated.FIND_SECONDS + .01
        self.send.reset_mock()
        self.host_listener.reset_mock()
        self.link.tick(self.send)
        find = next(call.args[0] for call in self.send.call_args_list
                    if protocol.parse_datagram(call.args[0])[0] == "FIND")
        self.host._handle_datagram(find, (simulated.LOOPBACK, protocol.DEVICE_PORT))
        _, ready = protocol.parse_datagram(self.host_listener.sendto.call_args.args[0])
        key = protocol.unprotect_secret(self.link.active["protected_link_key"])
        names = ("device", "pc", "nonce", "challenge", "address", "port")
        prior_nonce = self.link._ready_nonce
        for name, value in (("address", "127.0.0.2"), ("port", str(protocol.DEVICE_PORT + 1))):
            fields = dict(ready)
            fields[name] = value
            values = [fields[field] for field in names]
            packet = protocol.encode_datagram("READY", list(zip(names, values))
                + [("proof", protocol.make_proof(key, "ready-v2", *values))])
            self.send.reset_mock()
            with self.assertRaisesRegex(ValueError, "another socket endpoint"):
                self.link.handle_datagram(packet, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
            self.send.assert_not_called()
            self.assertEqual(self.link._ready_nonce, prior_nonce)

    # self: Genuine device proof whose broker challenge has already been consumed.
    # Raises AssertionError if duplicate acknowledgement emits new projections or renews the event peer.
    def test_broker_rejects_consumed_simulated_commit(self):
        self.exchange_presence()
        committed = next(call.args[0] for call in self.send.call_args_list
                         if protocol.parse_datagram(call.args[0])[0] == "COMMITTED")
        peer = self.host._event_peers[self.link.device_id]
        self.host_listener.reset_mock()
        with self.assertRaisesRegex(ValueError, "fresh endpoint challenge"):
            self.host._handle_datagram(committed, (simulated.LOOPBACK, protocol.DEVICE_PORT))
        self.assertEqual(self.host._event_peers[self.link.device_id], peer)
        self.host_listener.sendto.assert_not_called()

    # self: Real service and simulator model communicating through isolated ephemeral loopback sockets.
    # Raises AssertionError for handshake/activity/persistence failure; original OS failures remain visible.
    def test_real_udp_pairing_and_activity_use_isolated_loopback_ports(self):
        self.link = simulated.SimulatorLink(self.store)
        self.link.begin_pairing()
        code = self.link.display_code()
        stopped = threading.Event()
        model_lock = threading.RLock()
        failures = []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as device_socket:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as host_socket:
                device_socket.bind((simulated.LOOPBACK, 0))
                host_socket.bind((simulated.LOOPBACK, 0))
                device_socket.settimeout(0.05)
                host_socket.settimeout(0.05)
                ports = {"DEVICE_PORT": device_socket.getsockname()[1], "PLUGIN_PORT": host_socket.getsockname()[1]}
                isolated_home = str(self.path.parent / "host")
                with patch.multiple(protocol, **ports), patch.dict(os.environ, {"LOCALAPPDATA": isolated_home}):
                    state = protocol.StateStore()
                    fixture_activity = synthetic_activity("editing", "SIMULATED UDP")
                    responder = protocol.CompanionResponder(state, host_socket, fixture_activity)

                    # No parameters: owns only the fixture's ephemeral socket and synthetic simulator state.
                    # Captures original transport/storage failures for assertion in the test thread.
                    def serve_simulator():
                        while not stopped.is_set():
                            try:
                                packet, source = device_socket.recvfrom(interactions.MAX_EVENT_DATAGRAM_BYTES + 1)
                            except socket.timeout:
                                packet = None
                            except OSError as error:
                                failures.append(error)
                                return
                            try:
                                with model_lock:
                                    if packet is not None:
                                        self.link.handle_datagram(packet, source, device_socket.sendto)
                                    self.link.tick(device_socket.sendto)
                            except ValueError as error:
                                # UDP reordering is rejected explicitly, just as by SimulatorEndpoint.
                                self.link.record_error("fixture packet", error)
                            except (OSError, RuntimeError) as error:
                                failures.append(error)
                                return

                    worker = threading.Thread(target=serve_simulator, name="SIMULATED-CCP-UDP-test", daemon=True)
                    responder.start()
                    worker.start()
                    try:
                        # Discover on loopback only; this fixture never broadcasts to the real LAN.
                        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as discovery:
                            discovery.bind((simulated.LOOPBACK, 0))
                            discovery.settimeout(1.0)
                            request = "b" * 16
                            discovery.sendto(protocol.encode_datagram("DISCOVER", [("request", request)]),
                                             (simulated.LOOPBACK, ports["DEVICE_PORT"]))
                            raw_offer, source = discovery.recvfrom(protocol.MAX_DATAGRAM_BYTES + 1)
                        kind, fields = protocol.parse_datagram(raw_offer)
                        self.assertEqual(kind, "OFFER")
                        offer = protocol.offer_from_fields(fields, source[0], request)
                        service = protocol.CcpService.__new__(protocol.CcpService)
                        service.state, service.activity, service.responder = state, fixture_activity, responder
                        service.interactions = responder._requests
                        service._offers = {(offer.device_id, offer.session_id): offer}
                        service._offers_lock, service._pairing_lock = threading.Lock(), threading.Lock()
                        result = service.pair(code)
                        self.assertTrue(result["paired"])
                        self.assertEqual(result["source"], "SIMULATED")
                        deadline = time.monotonic() + 3.0
                        while time.monotonic() < deadline and not failures:
                            if self.link.snapshot().activity_valid and not state.pending_records:
                                break
                            stopped.wait(0.02)
                        self.assertFalse(failures, "The isolated UDP worker reported a transport/storage failure")
                        snapshot = self.link.snapshot()
                        self.assertEqual(snapshot.state, simulated.LinkState.CONNECTED)
                        self.assertEqual(snapshot.project, "SIMULATED UDP")
                        self.assertEqual(snapshot.phase, "editing")
                        self.assertTrue(snapshot.activity_valid)
                        self.assertFalse(state.pending_records)
                        self.assertEqual(state.public_status()[0]["source"], "SIMULATED")
                        self.assertNotIn(code, self.stderr.getvalue())
                        deadline = time.monotonic() + 5.0
                        while not responder.supports_events(self.link.device_id) and time.monotonic() < deadline:
                            stopped.wait(.02)
                        question = protocol.call_tool(service, "ccp_ask_question", {
                            "device_id": self.link.device_id, "include_simulator": True,
                            "context": "SIMULATED UDP", "title": "Choose test cache", "body": "Which fixture cache?",
                            "choices": [
                                {"id": "memory", "label": "Memory", "detail": "Use fixture memory.", "tone": "positive"},
                                {"id": "redis", "label": "Redis", "detail": "Use fixture Redis.", "tone": "info"},
                            ]})["structuredContent"]
                        deadline = time.monotonic() + 4.0
                        while self.link.snapshot().request_id != question["request_id"] and time.monotonic() < deadline:
                            stopped.wait(.02)
                        with model_lock:
                            self.assertEqual(self.link.snapshot().event_state, "pending")
                            self.link.submit_reply("redis", device_socket.sendto)
                        ownership = {name: question[name] for name in ("request_id", "capability")}
                        answer = protocol.call_tool(service, "ccp_poll_request", {**ownership, "wait_ms": 1500})["structuredContent"]
                        self.assertEqual(answer["state"], "received")
                        self.assertEqual(answer["answer"]["id"], "redis")
                        protocol.call_tool(service, "ccp_poll_request", {
                            **ownership, "acknowledge_answer_id": answer["answer_id"]})
                        deadline = time.monotonic() + 4.0
                        while self.link.snapshot().event_state != "delivered" and time.monotonic() < deadline:
                            stopped.wait(.02)
                        self.assertEqual(self.link.snapshot().event_state, "delivered")
                        self.assertNotIn(question["capability"], self.stderr.getvalue())
                        self.assertFalse(failures, "The isolated question round trip reported an OS failure")
                        reset_arguments = {"device_id": self.link.device_id, "include_simulator": True,
                                           "confirmation_code": protocol.reset.CONFIRMATION_CODE}
                        old_pc_id = state.pc_id
                        old_link = state.retained_keys(self.link.device_id).active
                        restart = protocol.call_tool(service, "ccp_reset_device", reset_arguments)["structuredContent"]
                        self.assertTrue(restart["accepted"])
                        self.assertFalse(restart["boot_verified"])
                        deadline = time.monotonic() + 2.0
                        while self.link.reset_count < 1 and time.monotonic() < deadline:
                            stopped.wait(.02)
                        self.assertEqual(self.link.reset_count, 1)
                        self.assertIsNotNone(self.link.active)
                        factory = protocol.call_tool(service, "ccp_factory_reset_device", reset_arguments)["structuredContent"]
                        self.assertTrue(factory["accepted"])
                        self.assertFalse(factory["settings_erasure_verified"])
                        deadline = time.monotonic() + 2.0
                        while self.link.reset_count < 2 and time.monotonic() < deadline:
                            stopped.wait(.02)
                        self.assertEqual(self.link.reset_count, 2)
                        self.assertFalse(self.link.network_ready)
                        self.assertIsNone(self.link.active)
                        self.assertEqual(state.pc_id, old_pc_id)
                        self.assertEqual(state.retained_keys(self.link.device_id).active, old_link)
                        self.assertFalse(failures, "The isolated reset round trips reported an OS failure")
                    finally:
                        stopped.set()
                        worker.join(timeout=1.0)
                        responder.close()
                        self.assertFalse(worker.is_alive(), "The isolated UDP worker did not stop")

    # self: Existing confirmed identity with an explicitly admitted replacement candidate.
    # Raises AssertionError if failed persistence or Cancel loses the old confirmed association.
    def test_replacement_failure_and_cancel_preserve_old_link(self):
        identity, key = self.pair_candidate()
        self.confirm(identity, key)
        previous = dict(self.link.active)
        with self.assertRaisesRegex(ValueError, "explicit replacement"):
            self.link.begin_pairing()
        self.pair_candidate(replace=True)
        self.assertEqual(self.link.active, previous)
        failure = PermissionError("synthetic persist denied")
        with patch.object(self.store, "_write", side_effect=failure):
            with self.assertRaises(PermissionError) as raised:
                self.link.cancel_pairing()
        self.assertIs(raised.exception, failure)
        self.assertEqual(self.link.active, previous)
        self.assertIsNotNone(self.link.pending)
        self.link.cancel_pairing()
        self.assertEqual(self.link.active, previous)
        self.assertIsNone(self.link.pending)

    # self: Retained association across offline toggles and a deliberate Forget action.
    # Raises AssertionError if a network transition resets identity or exposes stored secrets.
    def test_offline_and_explicit_forget_are_separate(self):
        self.exchange_presence()
        device = self.link.device_id
        previous = dict(self.link.active)
        self.link.set_network_ready(False)
        self.assertFalse(self.link.snapshot().activity_valid)
        self.assertEqual(self.link.active, previous)
        self.link.set_network_ready(True)
        self.assertEqual(self.link.state, simulated.LinkState.DISCOVERING)
        with self.assertRaisesRegex(ValueError, "explicit confirmation"):
            self.link.forget_pairing(False)
        self.link.forget_pairing(True)
        self.assertIsNone(self.link.active)
        self.assertEqual(self.link.device_id, device)
        public = asdict(self.link.snapshot())
        for name in ("code", "salt", "proof", "nonce", "protected_link_key", "key"):
            self.assertNotIn(name, public)

    # self: Paired isolated simulator with a current authenticated FIND nonce.
    # operation: Explicit restart or factory-reset wire domain.
    # code: Current-message confirmation fixture; a wrong value still receives a valid HMAC for rejection testing.
    # Returns one authenticated command without sending it to a physical device.
    def reset_packet(self, operation, code="043533477"):
        values = [self.link.device_id, self.link.active["pc_id"], self.link._nonce, "f" * 32, code]
        command, _, domain, _ = protocol.reset.COMMANDS[operation]
        key = protocol.unprotect_secret(self.link.active["protected_link_key"])
        return protocol.encode_datagram(command, list(zip(("device", "pc", "nonce", "id", "code"), values))
                                        + [("proof", protocol.make_proof(key, domain, *values))])

    # self: Explicit restart fixture using the real protocol parser and active link key.
    # Raises AssertionError if wrong/source-forged commands act, duplicates schedule twice, or restart loses settings.
    def test_authenticated_restart_is_one_shot_and_preserves_pairing(self):
        self.exchange_presence()
        operation = protocol.reset.ResetOperation.RESTART
        source = (simulated.LOOPBACK, protocol.PLUGIN_PORT)
        before = self.path.read_bytes()
        with self.assertRaises(ValueError):
            self.link.handle_datagram(self.reset_packet(operation, "043533478"), source, self.send)
        packet = self.reset_packet(operation)
        with self.assertRaises(ValueError):
            self.link.handle_datagram(packet, (source[0], source[1] + 1), self.send)
        with self.assertRaisesRegex(ValueError, "authentication"):
            self.link.handle_datagram(packet[:-1] + (b"0" if packet[-1:] != b"0" else b"1"), source, self.send)
        self.assertIsNone(self.link._reset_due)
        self.link.handle_datagram(packet, source, self.send)
        first_deadline = self.link._reset_due
        self.now += .1
        self.link.handle_datagram(packet, source, self.send)
        self.assertEqual(self.link._reset_due, first_deadline)
        self.assertEqual(protocol.parse_datagram(self.send.call_args.args[0])[0], "RESETACK")
        self.assertEqual(self.link.reset_count, 0)
        self.now = first_deadline + .01
        self.link.tick(self.send)
        self.assertEqual(self.link.reset_count, 1)
        self.assertEqual(self.path.read_bytes(), before)
        self.assertTrue(self.link.snapshot().retained_pairing)
        self.assertTrue(self.link.network_ready)
        with self.assertRaises(ValueError):
            self.link.handle_datagram(packet, source, self.send)
        self.link.tick(self.send)
        self.assertEqual(self.link.reset_count, 1)

    # self: Explicit factory reset of the simulator-owned store only.
    # Raises AssertionError if erasure touches PC identity, runs before ACK, preserves the simulated Wi-Fi key or retries.
    def test_factory_reset_returns_simulator_to_unconfigured_setup(self):
        self.exchange_presence()
        device = self.link.device_id
        pc_before = self.host._state.retained_keys.return_value
        source = (simulated.LOOPBACK, protocol.PLUGIN_PORT)
        packet = self.reset_packet(protocol.reset.ResetOperation.FACTORY_RESET)
        self.link.handle_datagram(packet, source, self.send)
        self.assertEqual(protocol.parse_datagram(self.send.call_args.args[0])[0], "FACTORYRESETACK")
        self.assertIsNotNone(self.link.active)
        self.now += .51
        self.link.tick(self.send)
        self.assertIsNone(self.link.active)
        self.assertIsNone(self.link.pending)
        self.assertFalse(self.link.network_ready)
        self.assertEqual(self.link.device_id, device)
        self.assertEqual(self.link.reset_count, 1)
        self.assertEqual(self.host._state.retained_keys.return_value, pc_before)
        reopened = simulated.SimulatorLink(simulated.SimulatorStore(self.path))
        self.assertFalse(reopened.network_ready)
        self.assertFalse(reopened.snapshot().retained_pairing)
        reopened.set_network_ready(True)
        self.assertTrue(reopened.store.document["network_configured"])
        self.assertEqual(reopened.state, simulated.LinkState.UNPAIRED)

    # self: An admitted simulator restart whose immediate ACK socket write fails.
    # Raises AssertionError if the ambiguous send failure cancels admission or causes a second restart.
    def test_reset_ack_send_failure_keeps_the_admitted_single_operation(self):
        self.exchange_presence()
        failure = OSError("SIMULATED reset ACK send failed")
        with self.assertRaises(OSError) as raised:
            self.link.handle_datagram(self.reset_packet(protocol.reset.ResetOperation.RESTART),
                                      (simulated.LOOPBACK, protocol.PLUGIN_PORT), Mock(side_effect=failure))
        self.assertIs(raised.exception, failure)
        self.assertIsNotNone(self.link._reset_due)
        self.now += .51
        self.link.tick(self.send)
        self.assertEqual(self.link.reset_count, 1)
        self.link.tick(self.send)
        self.assertEqual(self.link.reset_count, 1)
        self.assertIsNotNone(self.link.active)

    # self: Simulator factory reset whose isolated persistence owner rejects the erase.
    # Raises AssertionError if a failed write is hidden by reboot, in-memory wipe or an automatic second attempt.
    def test_factory_reset_storage_failure_does_not_reboot_or_retry(self):
        self.exchange_presence()
        before = self.path.read_bytes()
        self.link.handle_datagram(self.reset_packet(protocol.reset.ResetOperation.FACTORY_RESET),
                                  (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.now += .51
        failure = PermissionError("SIMULATED factory reset write denied")
        with patch.object(self.store, "_write", side_effect=failure) as writer:
            with self.assertRaises(PermissionError) as raised:
                self.link.tick(self.send)
            self.assertIs(raised.exception, failure)
            self.link.tick(self.send)
            writer.assert_called_once()
        self.assertEqual(self.link.reset_count, 0)
        self.assertEqual(self.path.read_bytes(), before)
        self.assertIsNotNone(self.link.active)
        self.deliver_events()
        self.assertIn("SIMULATED factory reset write denied", self.link.snapshot().last_error)

    # self: Corrupt persisted state, never treated as first use.
    # Raises AssertionError if construction repairs or replaces an invalid stored identity.
    def test_corrupt_storage_remains_an_error(self):
        self.path.write_text('{"version":1,"version":2}', encoding="utf-8")
        before = self.path.read_bytes()
        with self.assertRaises((RuntimeError, ValueError)):
            simulated.SimulatorStore(self.path)
        self.assertEqual(self.path.read_bytes(), before)

    # self: Spoofed network/proof input with a real pending commissioning context.
    # Raises AssertionError if invalid authentication changes the retained transaction.
    def test_forged_confirmation_and_non_loopback_source_are_rejected(self):
        identity, _ = self.pair_candidate()
        payload = protocol.encode_datagram("CONFIRM", identity + [("proof", "0" * 64)])
        with self.assertRaisesRegex(ValueError, "authentication failed"):
            self.link.handle_datagram(payload, self.source, self.send)
        with self.assertRaisesRegex(ValueError, "loopback peers only"):
            self.link.handle_datagram(payload, ("192.0.2.10", 54321), self.send)
        self.assertIsNone(self.link.active)
        self.assertIsNotNone(self.link.pending)

    # self: Isolated complete question fixture after real cryptographic commissioning.
    # Returns the caller-owned ticket and response capability; nothing is sent to a live Codex instance.
    def question(self):
        self.exchange_presence()
        payload = interactions.EventPayload(interactions.EventKind.QUESTION, "SIMULATED fixture",
            "Choose cache", "Which cache should I use?", (
                interactions.Choice("memory", "Memory", "Keep the cache in memory.", interactions.Tone.POSITIVE),
                interactions.Choice("redis", "Redis", "Use the configured Redis cache.", interactions.Tone.INFO),
                interactions.Choice("none", "No cache", "Continue without a cache.", interactions.Tone.NEUTRAL)))
        key = protocol.unprotect_secret(self.link.active["protected_link_key"])
        created = self.requests.enqueue(self.link.device_id, payload, "SIMULATED",
                                       association_id=hashlib.sha256(key).hexdigest())
        self.deliver_events()
        return created

    # self: Fixture completing FIND -> READY -> COMMITTED before admitting current projections.
    # Raises AssertionError or the real protocol error if the contract is incoherent.
    def deliver_events(self):
        self.now += simulated.FIND_SECONDS + .01
        self.send.reset_mock()
        self.host_listener.reset_mock()
        self.link.tick(self.send)
        find = next(call.args[0] for call in reversed(self.send.call_args_list)
                    if protocol.parse_datagram(call.args[0])[0] == "FIND")
        self.host._handle_datagram(find, (simulated.LOOPBACK, protocol.DEVICE_PORT))
        self.accept_presence_packets()

    # self: Isolated fixture delivering the real responder's challenge and subsequent packets.
    # drop_committed: Model a lost acknowledgement without admitting any dependent capabilities.
    # Raises ValueError or the original protocol error if either endpoint rejects the round trip.
    def accept_presence_packets(self, drop_committed=False):
        index = 0
        while index < len(self.host_listener.sendto.call_args_list):
            payload = self.host_listener.sendto.call_args_list[index].args[0]
            index += 1
            before = len(self.send.call_args_list)
            self.link.handle_datagram(payload, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
            # Deliver generated COMMITTED before any capability using its newly authenticated peer.
            if protocol.parse_datagram(payload)[0] == "READY" and not drop_committed:
                for call in self.send.call_args_list[before:]:
                    self.host._handle_datagram(call.args[0], (simulated.LOOPBACK, protocol.DEVICE_PORT))

    # self: Real responder fixture holding its next presence packets for explicit receive ordering.
    # Returns authenticated projections held for intentional device-admission ordering assertions.
    # Raises AssertionError or the original protocol error if no FIND was transmitted.
    def pending_presence_packets(self):
        self.now += simulated.FIND_SECONDS + .01
        self.send.reset_mock()
        self.host_listener.reset_mock()
        self.link.tick(self.send)
        find = next(call.args[0] for call in self.send.call_args_list
                    if protocol.parse_datagram(call.args[0])[0] == "FIND")
        self.host._handle_datagram(find, (simulated.LOOPBACK, protocol.DEVICE_PORT))
        ready = self.host_listener.sendto.call_args.args[0]
        key = protocol.unprotect_secret(self.link.active["protected_link_key"])
        # Model the device's proof of possession while holding its incoming READY. This
        # creates reordered receive fixtures; it never bypasses the broker's challenge.
        self.host._handle_datagram(committed_for_ready(key, ready), (simulated.LOOPBACK, protocol.DEVICE_PORT))
        return [call.args[0] for call in self.host_listener.sendto.call_args_list]

    # self: Source-only input fixture; native preparation still owns the complete action geometry.
    # view: Actual preview whose next input must match the last presented observation.
    # snapshot: Complete event observation represented by the simulated displayed frame.
    # controls_visible: Explicit reveal gate, allowing hidden-control rejection to be tested.
    # Returns the complete native action plan for semantic target lookup.
    def present_event_plan(self, view, snapshot, controls_visible=True):
        _, _, view._actions, view._back = view.plan(snapshot)
        view._controls_visible = controls_visible
        view._rendered_event = view.rendered_identity(snapshot)
        return view._actions

    # self: Input fixture selecting a semantic action from the actual native plan.
    # actions: Complete native plan previously represented by the displayed observation.
    # identity: Stable action identity, independent of its position or storage order.
    # Returns the center of its native touch bounds.
    # Raises StopIteration if the expected semantic action is absent.
    def action_center(self, actions, identity):
        action = next(action for action in actions if action.identity == identity)
        x, y, width, height = action.bounds
        return x + width // 2, y + height // 2

    # self: Real reply receipt delayed while the device transmits a new FIND.
    # Raises AssertionError if FIND rotation rejects the last transmitted REPLY or accepts a forged ACK.
    def test_reply_ack_keeps_sent_nonce_across_find(self):
        created = self.question()
        self.link.submit_reply("memory", self.send)
        self.host._handle_datagram(self.send.call_args.args[0], (simulated.LOOPBACK, protocol.DEVICE_PORT))
        receipt = self.host_listener.sendto.call_args.args[0]
        sent_nonce = protocol.parse_datagram(receipt)[1]["nonce"]
        deadline = self.link._event_expires
        packets = self.pending_presence_packets()
        self.assertNotEqual(self.link._nonce, sent_nonce)
        corrupted = receipt[:-1] + (b"0" if receipt[-1:] != b"0" else b"1")
        with self.assertRaisesRegex(ValueError, "authentication"):
            self.link.handle_datagram(corrupted, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.link.handle_datagram(receipt, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        snapshot = self.link.snapshot()
        self.assertEqual((snapshot.request_id, snapshot.event_state), (created["request_id"], "received"))
        self.assertEqual(snapshot.event_payload.selected_id, "memory")
        self.assertEqual(self.link._event_expires, deadline)
        for packet in packets:
            self.link.handle_datagram(packet, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().event_state, "received")

    # self: Pending choice retransmitted successfully under a newer authenticated READY.
    # Raises AssertionError if the earlier send nonce remains admissible after the new REPLY is transmitted.
    def test_next_successful_reply_replaces_ack_admission_nonce(self):
        self.question()
        self.link.submit_reply("memory", self.send)
        _, earlier = protocol.parse_datagram(self.send.call_args.args[0])
        key = protocol.unprotect_secret(self.link.active["protected_link_key"])
        values = [earlier[name] for name in ("device", "pc", "nonce", "id", "choice")] + ["received"]
        old_receipt = protocol.encode_datagram("REPLYACK", list(zip(
            ("device", "pc", "nonce", "id", "choice", "status"), values))
            + [("proof", protocol.make_proof(key, "reply-ack", *values))])
        for packet in self.pending_presence_packets():
            self.link.handle_datagram(packet, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.now += simulated.FIND_SECONDS + .01
        self.send.reset_mock()
        self.link.tick(self.send)
        reply = next(call.args[0] for call in self.send.call_args_list
                     if protocol.parse_datagram(call.args[0])[0] == "REPLY")
        self.assertNotEqual(protocol.parse_datagram(reply)[1]["nonce"], earlier["nonce"])
        with self.assertRaisesRegex(ValueError, "current active FIND"):
            self.link.handle_datagram(old_receipt, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().event_state, "sending")
        self.host._handle_datagram(reply, (simulated.LOOPBACK, protocol.DEVICE_PORT))
        self.link.handle_datagram(self.host_listener.sendto.call_args.args[0],
                                  (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().event_state, "received")

    # self: New authenticated EVENT reordered ahead of its READY while the previous connection stays live.
    # Raises AssertionError if the previous READY admits a new FIND's EVENT or rejection mutates the question.
    def test_current_event_waits_for_matching_ready(self):
        self.question()
        previous = self.link.snapshot()
        deadline = self.link._event_expires
        packets = self.pending_presence_packets()
        event = next(packet for packet in packets if protocol.parse_datagram(packet)[0] == "EVENT")
        self.assertEqual(self.link.state, simulated.LinkState.CONNECTED)
        with self.assertRaisesRegex(ValueError, "current active FIND"):
            self.link.handle_datagram(event, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().event_payload, previous.event_payload)
        self.assertEqual(self.link._event_expires, deadline)
        ready = next(packet for packet in packets if protocol.parse_datagram(packet)[0] == "READY")
        self.link.handle_datagram(ready, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.link.handle_datagram(event, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().event_state, "pending")
        self.assertTrue(self.link.snapshot().event_fresh)

    # self: Confirmed local choice whose first socket send fails before any REPLY is transmitted.
    # Raises AssertionError if a forged receipt for the unsent packet is admitted or the real retry loses its choice.
    def test_failed_reply_send_does_not_admit_ack_before_successful_retry(self):
        self.question()
        self.send.side_effect = OSError("fixture reply send failure")
        with self.assertRaisesRegex(OSError, "fixture reply send failure"):
            self.link.submit_reply("memory", self.send)
        self.send.side_effect = None
        key = protocol.unprotect_secret(self.link.active["protected_link_key"])
        values = [self.link.device_id, self.pc, self.link._nonce, self.link._request_id, "memory", "received"]
        forged = protocol.encode_datagram("REPLYACK", list(zip(
            ("device", "pc", "nonce", "id", "choice", "status"), values))
            + [("proof", protocol.make_proof(key, "reply-ack", *values))])
        with self.assertRaisesRegex(ValueError, "current active FIND"):
            self.link.handle_datagram(forged, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().event_state, "sending")
        self.link.tick(self.send)
        self.host._handle_datagram(self.send.call_args.args[0], (simulated.LOOPBACK, protocol.DEVICE_PORT))
        self.link.handle_datagram(self.host_listener.sendto.call_args.args[0],
                                  (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().event_state, "received")
        self.assertEqual(self.link.snapshot().event_payload.selected_id, "memory")

    # self: Old valid receipt from an association whose transient network route was retired.
    # Raises AssertionError if reconnect authenticates an old send nonce without transmitting a new REPLY.
    def test_reconnect_retires_previous_reply_ack_nonce(self):
        self.question()
        self.link.submit_reply("memory", self.send)
        self.host._handle_datagram(self.send.call_args.args[0], (simulated.LOOPBACK, protocol.DEVICE_PORT))
        receipt = self.host_listener.sendto.call_args.args[0]
        self.link.set_network_ready(False)
        self.link.set_network_ready(True)
        self.deliver_events()
        with self.assertRaisesRegex(ValueError, "current active FIND"):
            self.link.handle_datagram(receipt, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().event_state, "received")

    # self: Real HMAC reply path with local confirmation, receipt and later recipient acknowledgement.
    # Raises AssertionError if Send/socket success or UDP receipt produces Reply sent prematurely.
    def test_question_waits_for_the_original_mcp_recipient_ack(self):
        created = self.question()
        self.assertEqual(self.link.snapshot().event_state, "pending")
        self.link.submit_reply("redis", self.send)
        self.assertEqual(self.link.snapshot().event_state, "sending")
        self.host._handle_datagram(self.send.call_args.args[0], (simulated.LOOPBACK, protocol.DEVICE_PORT))
        receipt = self.host_listener.sendto.call_args.args[0]
        self.link.handle_datagram(receipt, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.assertEqual(self.link.snapshot().event_state, "received")
        reply = self.requests.poll(created["request_id"], created["capability"])
        self.assertFalse(reply["recipient_acknowledged"])
        self.requests.poll(created["request_id"], created["capability"], acknowledge_answer_id=reply["answer_id"])
        self.deliver_events()
        self.assertEqual(self.link.snapshot().event_state, "delivered")
        self.assertEqual(self.link.snapshot().event_payload.selected_id, "redis")

    # self: Two real broker/simulator transactions with the first terminal acknowledgement lost.
    # Raises AssertionError if the broker hides closure, a retry changes the answer, or the second ticket is rejected.
    def test_consecutive_questions_retry_terminal_ack_before_publishing_next(self):
        first = self.question()
        payload = self.link.snapshot().event_payload
        self.link.submit_reply("redis", self.send)
        self.host._handle_datagram(self.send.call_args.args[0], (simulated.LOOPBACK, protocol.DEVICE_PORT))
        self.link.handle_datagram(self.host_listener.sendto.call_args.args[0],
                                  (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        answer = self.requests.poll(first["request_id"], first["capability"])
        self.requests.poll(first["request_id"], first["capability"], acknowledge_answer_id=answer["answer_id"])
        key = protocol.unprotect_secret(self.link.active["protected_link_key"])
        association = hashlib.sha256(key).hexdigest()
        second = self.requests.enqueue(self.link.device_id, payload, "SIMULATED", association_id=association)
        self.deliver_events()
        self.assertEqual(self.link.snapshot().request_id, first["request_id"])
        self.assertEqual(self.link.snapshot().event_state, "delivered")
        # Drop this EVENTACK; the next FIND must still publish the first closure.
        self.now += interactions.TERMINAL_DISPLAY_SECONDS + 1
        self.deliver_events()
        self.assertEqual(self.link.snapshot().request_id, first["request_id"])
        kind, fields = protocol.parse_datagram(self.send.call_args.args[0])
        self.assertEqual((kind, fields["state"]), ("EVENTACK", "delivered"))
        self.host._handle_datagram(self.send.call_args.args[0], (simulated.LOOPBACK, protocol.DEVICE_PORT))
        self.deliver_events()
        self.assertEqual(self.link.snapshot().request_id, second["request_id"])
        self.assertEqual(self.link.snapshot().event_state, "pending")

    # self: Pending semantic question across simulated radio loss and reconnection.
    # Raises AssertionError if disconnect loses the route/selection or permits a stale reply.
    def test_offline_preserves_request_and_rejects_send_until_fresh(self):
        self.question()
        previous = self.link.snapshot().event_payload
        view = EventView()
        view.visible(self.link.snapshot())
        view.stage, view.selected_id = "confirm", "memory"
        self.link.set_network_ready(False)
        self.assertEqual(self.link.snapshot().event_payload, previous)
        self.assertFalse(self.link.snapshot().event_fresh)
        self.assertEqual(view.plan(self.link.snapshot())[0], "decision-confirm")
        self.assertFalse(view.plan(self.link.snapshot())[2][0].enabled)
        with self.assertRaises(ValueError):
            self.link.submit_reply("memory", self.send)
        self.link.set_network_ready(True)
        self.deliver_events()
        self.assertEqual(self.link.snapshot().event_payload, previous)
        self.assertTrue(self.link.snapshot().event_fresh)
        self.assertEqual(view.stage, "confirm")

    # self: Immutable event payloads subjected to replay, alteration, expiry and broker loss.
    # Raises AssertionError if stale or forged metadata restores an answerable question.
    def test_event_replay_tamper_expiry_and_broker_restart_are_not_answers(self):
        self.question()
        packet = self.host_listener.sendto.call_args.args[0]
        with self.assertRaisesRegex(ValueError, "one snapshot"):
            self.link.handle_datagram(packet, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        corrupted = packet[:-1] + (b"0" if packet[-1:] != b"0" else b"1")
        with self.assertRaisesRegex(ValueError, "authentication"):
            self.link.handle_datagram(corrupted, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.host._requests = interactions.InteractionStore(lambda: self.now)
        self.deliver_events()
        self.assertEqual(self.link.snapshot().event_state, "rejected")
        with self.assertRaises(ValueError):
            self.link.submit_reply("memory", self.send)

    # self: A restarted real responder whose new recipient queues a question before any reconnect frame.
    # Raises AssertionError if the unknown question hides its replacement until expiry or is mistaken for delivery.
    def test_broker_restart_with_queued_successor_retires_old_question_first(self):
        first = self.question()
        self.link.submit_reply("memory", self.send)
        payload = replace(self.link.snapshot().event_payload, selected_id="")
        key = protocol.unprotect_secret(self.link.active["protected_link_key"])
        self.requests = self.host._requests = interactions.InteractionStore(lambda: self.now)
        second = self.requests.enqueue(self.link.device_id, payload, "SIMULATED",
                                       association_id=hashlib.sha256(key).hexdigest())
        self.deliver_events()
        snapshot = self.link.snapshot()
        self.assertEqual((snapshot.request_id, snapshot.event_state), (first["request_id"], "rejected"))
        self.assertEqual(snapshot.event_payload.selected_id, "memory")
        self.assertFalse(self.requests._tickets[second["request_id"]].published)
        self.assertFalse(any(protocol.parse_datagram(call.args[0])[0] == "EVENTACK"
                             for call in self.send.call_args_list))
        self.deliver_events()
        self.assertEqual((self.link.snapshot().request_id, self.link.snapshot().event_state),
                         (second["request_id"], "pending"))
        self.assertEqual(self.requests.poll(second["request_id"], second["capability"])["state"], "pending")

    # self: Confirmed local selection with a valid packet admitted before the endpoint's next maintenance tick.
    # incoming: Authenticated state whose positive TTL must never extend the original device deadline.
    # Raises AssertionError if late receipt/delivery/pending revives the decision or loses local Send evidence.
    def late_event_before_tick(self, incoming):
        created = self.question()
        self.link.submit_reply("memory", self.send)
        deadline = self.link._event_expires
        self.now += simulated.FIND_SECONDS + .01
        self.send.reset_mock()
        self.host_listener.reset_mock()
        self.link.tick(self.send)
        find = next(call.args[0] for call in self.send.call_args_list
                    if protocol.parse_datagram(call.args[0])[0] == "FIND")
        self.host._handle_datagram(find, (simulated.LOOPBACK, protocol.DEVICE_PORT))
        ready = self.host_listener.sendto.call_args.args[0]
        self.link.handle_datagram(ready, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        committed = next(call.args[0] for call in reversed(self.send.call_args_list)
            if protocol.parse_datagram(call.args[0])[0] == "COMMITTED")
        self.host._handle_datagram(committed, (simulated.LOOPBACK, protocol.DEVICE_PORT))
        # Admit READY before changing the clock. Do not run tick at the deadline.
        for call in self.host_listener.sendto.call_args_list:
            if protocol.parse_datagram(call.args[0])[0] not in {"READY", "EVENT"}:
                self.link.handle_datagram(call.args[0], (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        payload = self.link.snapshot().event_payload
        if incoming == "pending":
            payload = replace(payload, selected_id="")
        key = protocol.unprotect_secret(self.link.active["protected_link_key"])
        values = [self.link.device_id, self.pc, self.link._nonce, created["request_id"], incoming,
                  "30", interactions.encode_payload(payload)]
        packet = protocol.encode_datagram("EVENT", list(zip(
            ("device", "pc", "nonce", "id", "state", "ttl", "data"), values))
            + [("proof", protocol.make_proof(key, "event", *values))])
        self.now = deadline + .01
        self.link.handle_datagram(packet, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        snapshot = self.link.snapshot()
        self.assertEqual(snapshot.event_state, "expired")
        self.assertEqual(snapshot.event_payload.selected_id, "memory")
        self.assertEqual(self.link._event_expires, deadline)
        self.assertEqual(protocol.parse_datagram(self.send.call_args.args[0])[1]["state"], "expired")

    # self: A late recipient-confirmed EVENT before maintenance has enforced local expiry.
    # Raises AssertionError if the simulator accepts Delivered where firmware requires Expired.
    def test_late_delivered_event_cannot_bypass_original_deadline(self):
        self.late_event_before_tick("delivered")

    # self: Late broker receipt before maintenance.
    # Raises AssertionError if receipt revives a closed local decision window.
    def test_late_received_event_cannot_bypass_original_deadline(self):
        self.late_event_before_tick("received")

    # self: Late pending retransmission with a positive remaining host TTL.
    # Raises AssertionError if retransmission extends the original local deadline or erases Send evidence.
    def test_late_pending_event_cannot_bypass_original_deadline(self):
        self.late_event_before_tick("pending")

    # self: Confirmed local Send whose UDP reply never reaches the original recipient.
    # Raises AssertionError if host expiry erases local submission evidence or presents success/retry.
    def test_expiry_after_send_keeps_unconfirmed_delivery_and_terminal_done(self):
        self.question()
        self.link.submit_reply("memory", self.send)
        self.now += interactions.MAX_TTL_SECONDS + .01
        self.deliver_events()
        snapshot = self.link.snapshot()
        self.assertEqual(snapshot.event_state, "expired")
        self.assertEqual(snapshot.event_payload.selected_id, "memory")
        view = EventView()
        view.visible(snapshot)
        _, lines, actions, _ = view.plan(snapshot)
        self.assertEqual(lines[0][0], snapshot.event_payload.title)
        self.assertIn("Delivery unconfirmed", [text for text, _ in lines])
        self.assertEqual([action.identity for action in actions], ["dismiss"])
        delivered = replace(snapshot, event_state="delivered")
        view._delivered_at = time.monotonic() - 10
        self.assertTrue(view.visible(delivered), "A terminal question must wait for explicit Done")
        self.assertEqual([action.label for action in view.plan(delivered)[2]], ["Done"])

    # self: Preview input using approved 2+1 cards, separate confirmation and canonical Back.
    # Raises AssertionError if choosing a card sends a response or Back discards the pending request.
    def test_preview_selection_is_reversible_until_explicit_send(self):
        self.question()
        snapshot = self.link.snapshot()
        view = EventView()
        endpoint = Mock()
        view.visible(snapshot)
        view.stage = "choices"
        actions = self.present_event_plan(view, snapshot, controls_visible=False)
        choice_point = self.action_center(actions, "choose:memory")
        view.press(choice_point, snapshot, endpoint)
        self.assertEqual((view.stage, view.selected_id), ("choices", ""))
        endpoint.reply.assert_not_called()
        self.present_event_plan(view, snapshot)
        view.press(choice_point, snapshot, endpoint)
        self.assertEqual((view.stage, view.selected_id), ("confirm", "memory"))
        endpoint.reply.assert_not_called()
        self.present_event_plan(view, snapshot)
        self.assertTrue(view._back)
        view.press((40, 233), snapshot, endpoint)
        self.assertEqual(view.stage, "choices")
        self.assertEqual(view.selected_id, "memory")
        actions = self.present_event_plan(view, snapshot)
        view.press(self.action_center(actions, "choose:memory"), snapshot, endpoint)
        self.assertEqual(view.stage, "confirm")
        actions = self.present_event_plan(view, snapshot)
        view.press(self.action_center(actions, "send"), snapshot, endpoint)
        endpoint.reply.assert_called_once_with("memory")

    # self: Native glyph/card preview and read-only six-name pagination, without starting Tk.
    # Raises AssertionError or ValueError for text loss, illegal action targets or geometry outside the circle.
    def test_event_preview_complete_names_and_approved_action_geometry(self):
        self.question()
        snapshot = self.link.snapshot()
        view = EventView()
        view.visible(snapshot)
        rig, _ = load_contract(ROOT)
        painter = ScenePainter(ROOT, rig)
        # Control raster is the actual C++ backend. Regenerate its library before
        # authorizing this suite; missing/stale native output is an explicit failure.
        painter.native = NativeRig(ROOT, rig)
        for stage in ("notice", "choices", "confirm"):
            view.stage, view.selected_id = stage, "memory"
            identity, lines, actions, _ = view.plan(snapshot)
            framebuffer = Image.new("RGB", (466, 466), (0, 0, 0))
            definition = painter.definition(identity)
            painter.paint_text(framebuffer, definition["title"]["text"], definition["title"]["top"])
            for line, top in lines:
                painter.paint_text(framebuffer, line, top, body=True, max_width=300)
            for action in actions:
                paint_action(framebuffer, painter, action)
        names = tuple(str(index) + "F" * 47 for index in range(6))
        payload = interactions.EventPayload(interactions.EventKind.CHECKS_FAILED, "", "Checks failed",
            "Review the real names.", failures=names)
        failed = simulated.SimulatorSnapshot(state=simulated.LinkState.CONNECTED, request_id="f" * 32,
            event_state="pending", event_payload=payload, event_fresh=True)
        view.visible(failed)
        view.stage = "failures"
        self.assertEqual(view.plan(failed)[2], ())
        self.present_event_plan(view, failed, controls_visible=False)
        view.press((250, 320), failed, Mock())
        view.release((250, 220), failed)
        self.assertEqual(view.page, 0)
        self.present_event_plan(view, failed)
        view.press((250, 320), failed, Mock())
        view.release((250, 220), failed)
        self.assertEqual(view.page, 1)
        for name in names:
            self.assertEqual("".join(wrapped(name)), name)

    # self: Connected preview whose project is owned by the native identity row.
    # Raises AssertionError if the obsolete body slot duplicates the project or missing data is hidden.
    def test_preview_bounds_long_project_and_missing_activity(self):
        snapshot = simulated.SimulatorSnapshot(state=simulated.LinkState.CONNECTED,
            activity_valid=True, phase="working", project="P" * 24, chat="d" * 64)
        identity, title, lines = presentation(snapshot, "")
        self.assertEqual(identity, "working")
        self.assertEqual(lines, ())
        self.assertEqual(snapshot.project, "P" * 24)
        self.assertEqual(presentation(simulated.SimulatorSnapshot(state=simulated.LinkState.CONNECTED), "")[1], "UNAVAILABLE")

    # self: Preview contract coupled to the actual generated native glyphs and authored scene identities.
    # Raises AssertionError or ValueError if a route/caption leaves its circular slot or loses coverage.
    def test_preview_routes_render_with_native_font_bounds(self):
        rig, _ = load_contract(ROOT)
        painter = ScenePainter(ROOT, rig)
        snapshots = [simulated.SimulatorSnapshot(state=state) for state in simulated.LinkState]
        snapshots += [simulated.SimulatorSnapshot(state=simulated.LinkState.CONNECTED, activity_valid=True,
            phase=phase, project="P" * 24, chat="d" * 64) for phase in simulated.PHASES]
        snapshots.append(simulated.SimulatorSnapshot(network_ready=False))
        for snapshot in snapshots:
            identity, title, lines = presentation(snapshot, "")
            title = title or painter.definition(identity)["title"]["text"]
            framebuffer = Image.new("RGB", (466, 466), (0, 0, 0))
            painter.paint_text(framebuffer, title, 210)
            for row, line in enumerate(lines):
                painter.paint_text(framebuffer, line, 266 + row * 28, body=True)
            painter.paint_text(framebuffer, "SIMULATED", 420, body=True)
            self.assertIsNotNone(framebuffer.getbbox(), f"No visible glyphs for {identity}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
