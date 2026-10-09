"""SIMULATED CCP1 regressions, including ephemeral loopback UDP; no Tk/device/live hooks."""

from __future__ import annotations

import contextlib
from dataclasses import asdict
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
from cody_scene import ScenePainter
from scripts.common.cody_rig_contract import load_contract
from PIL import Image

protocol = simulated.protocol


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
    # Returns accepted responder messages for replay/tampering assertions.
    def exchange_presence(self, pending_restart=False):
        identity, key = self.pair_candidate()
        if pending_restart:
            self.now += simulated.PAIRING_SECONDS + 1
            self.link = simulated.SimulatorLink(simulated.SimulatorStore(self.path), lambda: self.now, lambda: 1000 + self.now)
        else:
            self.confirm(identity, key)
        state = Mock(pc_id=self.pc)
        state.retained_keys.return_value = protocol.RetainedKeys(None, key) if pending_restart else protocol.RetainedKeys(key, None)
        activity = Mock()
        activity.snapshot.return_value = {"chat": "d" * 64, "revision": "1" * 16,
            "phase": "working", "project": "SIMULATED fixture".encode("ascii").hex()}
        listener = Mock()
        responder = protocol.CompanionResponder(state, listener, activity)
        self.link.tick(self.send)
        find, destination = self.send.call_args.args
        self.assertEqual(destination, (simulated.LOOPBACK, protocol.PLUGIN_PORT))
        responder._handle_datagram(find, (simulated.LOOPBACK, protocol.DEVICE_PORT))
        replies = [call.args[0] for call in listener.sendto.call_args_list]
        for payload in replies:
            self.link.handle_datagram(payload, (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
            if protocol.parse_datagram(payload)[0] == "READY":
                committed = self.send.call_args.args[0]
                self.assertEqual(protocol.parse_datagram(committed)[0], "COMMITTED")
                responder._handle_datagram(committed, (simulated.LOOPBACK, protocol.DEVICE_PORT))
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
            self.link.handle_datagram(replies[-1], (simulated.LOOPBACK, protocol.PLUGIN_PORT), self.send)
        self.now += simulated.PRESENCE_SECONDS + .1
        self.assertFalse(self.link.snapshot().activity_valid)

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

    # self: Real service and simulator model communicating through isolated ephemeral loopback sockets.
    # Raises AssertionError for handshake/activity/persistence failure; original OS failures remain visible.
    def test_real_udp_pairing_and_activity_use_isolated_loopback_ports(self):
        self.link = simulated.SimulatorLink(self.store)
        self.link.begin_pairing()
        code = self.link.display_code()
        stopped = threading.Event()
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
                    fixture_activity = Mock()
                    fixture_activity.snapshot.return_value = {"chat": "d" * 64, "revision": "1" * 16,
                        "phase": "editing", "project": "SIMULATED UDP".encode("ascii").hex()}
                    responder = protocol.CompanionResponder(state, host_socket, fixture_activity)

                    # No parameters: owns only the fixture's ephemeral socket and synthetic simulator state.
                    # Captures original transport/storage failures for assertion in the test thread.
                    def serve_simulator():
                        while not stopped.is_set():
                            try:
                                packet, source = device_socket.recvfrom(protocol.MAX_DATAGRAM_BYTES + 1)
                            except socket.timeout:
                                packet = None
                            except OSError as error:
                                failures.append(error)
                                return
                            try:
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

    # self: Maximum wire label compared with the native caption slot.
    # Raises AssertionError if the preview overflows a caption or hides its simulated identity.
    def test_preview_bounds_long_project_and_missing_activity(self):
        snapshot = simulated.SimulatorSnapshot(state=simulated.LinkState.CONNECTED,
            activity_valid=True, phase="working", project="P" * 24, chat="d" * 64)
        identity, title, lines = presentation(snapshot, "")
        self.assertEqual(identity, "working")
        self.assertEqual("".join(lines), "P" * 24)
        self.assertTrue(all(len(line) <= 21 for line in lines))
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
