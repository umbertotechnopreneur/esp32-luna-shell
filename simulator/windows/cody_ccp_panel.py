"""Visible SIMULATED CCP control panel and authored-scene preview for Cody Studio."""

from __future__ import annotations

import time
import sys
import tkinter as tk
from tkinter import messagebox, ttk

from cody_ccp_simulator import LinkState, SimulatorEndpoint, SimulatorSnapshot
from cody_ccp_event_view import EventView
from scripts.common.cody_rig_contract import evaluate


# snapshot: One isolated, authenticated simulator observation, never a firmware snapshot.
# code: Ephemeral code obtained only for the visible simulator display.
# Returns the authored scene, bounded title override and at most two caption lines.
def presentation(snapshot: SimulatorSnapshot, code: str) -> tuple[str, str, tuple[str, ...]]:
    if not snapshot.network_ready:
        return "network-wait", "No connection", ("SIMULATED LAN offline", "Saved pairing kept" if snapshot.retained_pairing else "Connect LAN to pair")
    if snapshot.state == LinkState.PAIRING:
        return "connect-codex", "Connect Codex", ("Enter code in Codex", "Confirm simulator")
    if snapshot.state == LinkState.INCOMPLETE:
        return "pairing-failed", "Pairing pending", ("Waiting for CONFIRM", "Cancel to start again")
    if snapshot.state == LinkState.UNPAIRED:
        return "codex-choice", "Connect Codex?", ("Choose Connect Codex", "in the CCP panel")
    if snapshot.state == LinkState.DISCOVERING:
        return "looking-for-pc", "Looking for PC", ("Saved pairing kept", "Waiting for CCP")
    if snapshot.state == LinkState.PAIRED:
        return "setup-complete", "You're all set.", ("Waiting for CCP",)
    if snapshot.state == LinkState.TIMED_OUT:
        return "pairing-failed", "Code expired", ("Choose Connect Codex", "for a new code")
    if snapshot.state == LinkState.STOPPED:
        return "pairing-failed", "CCP stopped", ("See the CCP panel",)
    if not snapshot.activity_valid:
        return "activity-unavailable", "UNAVAILABLE", ("Waiting for snapshot", "from CCP")
    if snapshot.phase == "none":
        return "connected", "Connected", ("Waiting for Codex", "Send a real prompt")
    identities = {"active": "project-active", "working": "working", "editing": "working",
                  "running": "working", "ready": "response-ready", "paused": "session-paused",
                  "interrupted": "session-paused", "ended": "session-ended",
                  "unavailable": "activity-unavailable"}
    # The protocol permits 24 project characters; the native caption slot holds 21.
    lines = tuple(snapshot.project[offset:offset + 21] for offset in range(0, len(snapshot.project), 21))
    return identities[snapshot.phase], "", lines


class CcpPanel:
    # self: Optional Studio owner; all network use is admitted by the public -Ccp flag.
    # window: Existing Cody Studio renderer, Tk controls and viewport owner.
    # parent: Dedicated notebook tab; desktop controls stay outside the circular display.
    # Raises tk.TclError for unavailable Tk widgets; endpoint startup errors remain visible.
    def __init__(self, window, parent):
        self.window = window
        self.endpoint = SimulatorEndpoint()
        self.events = EventView()
        self.follow = tk.BooleanVar(value=True)
        self.network = tk.BooleanVar(value=True)
        self.message = tk.StringVar(value="Starting SIMULATED CCP...")
        self.code = tk.StringVar(value="No active code")
        self._startup_error = ""
        self._route_key = None
        self._route_started = time.monotonic()
        self._snapshot = SimulatorSnapshot()
        self._display_code = ""
        self._frame_key = None
        self._frame = None
        section = window.add_section(parent, "SIMULATED / CCP1",
            "Real CCP packets on this PC. This identity belongs only to Cody Studio.")
        ttk.Checkbutton(section, text="Show CCP on the display", variable=self.follow).pack(anchor="w")
        ttk.Checkbutton(section, text="SIMULATED LAN available", variable=self.network,
                        command=self.change_network).pack(anchor="w", pady=(8, 12))
        ttk.Label(section, textvariable=self.message, style="Muted.TLabel", wraplength=310).pack(fill="x")
        ttk.Label(section, textvariable=self.code, style="Title.TLabel").pack(pady=(16, 12))
        buttons = ttk.Frame(section)
        buttons.pack(fill="x")
        buttons.columnconfigure((0, 1), weight=1)
        self.pair_button = ttk.Button(buttons, text="Connect Codex", command=self.pair)
        self.pair_button.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        self.cancel_button = ttk.Button(buttons, text="Cancel code", command=self.cancel)
        self.cancel_button.grid(row=0, column=1, sticky="ew")
        self.forget_button = ttk.Button(buttons, text="Forget pairing", command=self.forget)
        self.forget_button.grid(row=1, column=0, columnspan=2, pady=(8, 0))
        ttk.Label(section, style="Muted.TLabel", wraplength=310,
            text="In Codex, explicitly ask to connect the SIMULATED Cody Studio and give its visible code. "
                 "The latest genuine user prompt selects the chat. Clicking a tab does not send a prompt.").pack(fill="x", pady=(16, 0))
        try:
            self.endpoint.start()
        except (OSError, RuntimeError, ValueError) as error:
            self._startup_error = f"CCP start: {type(error).__name__}: {error}"
            print(f"SIMULATED {self._startup_error}", file=sys.stderr, flush=True)
        self.refresh()

    # self: UI owner publishing one synchronized, secret-free status and separate display code.
    # Raises tk.TclError if the panel is no longer available.
    def refresh(self):
        snapshot, code = self.endpoint.observe()
        self._snapshot, self._display_code = snapshot, code
        self.code.set(code or "No active code")
        self.network.set(snapshot.network_ready)
        error = self._startup_error or snapshot.last_error
        context = snapshot.project if snapshot.activity_valid else "UNAVAILABLE"
        self.message.set(f"{snapshot.source} / {snapshot.state.value}\n"
            f"Device {snapshot.device_id or 'UNAVAILABLE'}\n"
            f"Saved pairing: {'yes' if snapshot.retained_pairing else 'no'}\n"
            f"Chat: {snapshot.chat[:12] if snapshot.activity_valid and snapshot.phase != 'none' else 'UNAVAILABLE'}\n"
            f"Project: {context or 'No active chat'}\n"
            f"Packets: {snapshot.accepted_packets} accepted / {snapshot.rejected_packets} rejected"
            + (f"\nRequest: {snapshot.event_state}" if snapshot.event_payload is not None else "")
            + (f"\n{error}" if error else ""))
        available = snapshot.state != LinkState.STOPPED
        busy = bool(code) or snapshot.pending_pairing or snapshot.state in (LinkState.PAIRING, LinkState.INCOMPLETE)
        self.pair_button.configure(text="Replace pairing" if snapshot.retained_pairing else "Connect Codex",
            state="normal" if available and not busy and snapshot.network_ready else "disabled")
        self.cancel_button.configure(state="normal" if available and busy else "disabled")
        self.forget_button.configure(state="normal" if available and (snapshot.retained_pairing or busy) else "disabled")

    # self: Explicit desktop action owner; failures are shown and remain in endpoint state.
    # action: Fixed local simulator action, never a hook or manual chat-selection message.
    # confirmed: Single explicit replacement/forget confirmation or synthetic LAN state.
    def act(self, action, confirmed=False):
        try:
            self.endpoint.act(action, confirmed)
        except (OSError, RuntimeError, ValueError) as error:
            messagebox.showerror("SIMULATED CCP", str(error), parent=self.window.root)
        self.refresh()

    # self: Simulator pairing UI; retaining an existing association requires no new code.
    def pair(self):
        self.refresh()
        confirmed = False
        if self._snapshot.retained_pairing:
            confirmed = messagebox.askyesno("Already paired",
                "Replace the saved SIMULATED pairing? The old record is kept until the new one is confirmed.",
                parent=self.window.root, default="no")
            if not confirmed:
                return
        self.act("pair", confirmed)

    # self: Explicit cancellation discards only the current simulator candidate/code.
    def cancel(self):
        self.act("cancel")

    # self: Explicit forget UI; firmware and PC records are not modified by this operation.
    def forget(self):
        if messagebox.askyesno("Forget SIMULATED pairing",
                "Forget this simulator's saved pairing and candidate? A new visible code will be required.",
                parent=self.window.root, default="no"):
            self.act("forget", True)

    # self: Desktop synthetic connectivity switch; never controls the physical radio.
    def change_network(self):
        self.act("network", self.network.get())

    # self: Authenticated-state preview using existing authored scenes and exact font coverage.
    # rotation: Cardinal display rotation; the parent rotates the complete image.
    # Returns an image, its evaluated cloud and a semantic render key.
    # Raises ValueError for invalid authored geometry, bounded text or protocol state.
    def render(self, rotation):
        self.refresh()
        snapshot, code = self._snapshot, self._display_code
        if self.events.visible(snapshot):
            return self.events.render(snapshot, self.window, rotation)
        identity, title, lines = presentation(snapshot, code)
        painter = self.window.renderer.scene_painter
        definition = painter.definition(identity)
        title = title or definition["title"]["text"]
        route_key = (identity, title, lines, code)
        if route_key != self._route_key:
            self._route_key, self._route_started = route_key, time.monotonic()
        elapsed = (time.monotonic() - self._route_started) * 1000
        count = max(0, int(elapsed / 45))
        total = len(title) + sum(len(line) for line in lines)
        key = ("ccp", route_key, min(count, total), rotation)
        if self._frame_key == key:
            return self._frame[0].copy(), self._frame[1], key
        cloud = definition["cloud"]
        scene = evaluate(self.window.renderer.rig, painter.face(identity), tuple(cloud[key] for key in ("x", "y", "scale")))
        image = self.window.renderer.draw_scene(scene, debug=False, navigation_visible=False,
                                                background_rgb565=definition["background_rgb565"]).convert("RGB")
        # A changing packet counter never restarts the text clock. Code is a complete static control value.
        painter.paint_text(image, title, 210, definition["title"]["color_rgb565"], visible=min(len(title), count))
        preceding = len(title)
        for row, line in enumerate(lines):
            painter.paint_text(image, line, 266 + row * 28, body=True, visible=min(len(line), max(0, count - preceding)))
            preceding += len(line)
        if code and count >= preceding:
            painter.paint_text(image, code, 334, color=2047)
        painter.paint_text(image, "SIMULATED", 420, color=64704, body=True)
        self._frame_key, self._frame = key, (image.copy(), scene)
        return image, scene, key

    # self: Circular interaction view using the parent's already-rotated logical touch coordinates.
    # point: Logical press, or None when the pointer is outside the display.
    # Errors are retained by the endpoint and shown without making a synthetic reply.
    def press(self, point):
        self.refresh()
        if self.events.visible(self._snapshot):
            try:
                self.events.press(point, self._snapshot, self.endpoint)
            except (OSError, RuntimeError, ValueError) as error:
                messagebox.showerror("SIMULATED CCP", str(error), parent=self.window.root)

    # self: Read-only PR failure-list navigation.
    # point: Logical released point; vertical swipes page complete names, never select a row.
    def release(self, point):
        self.refresh()
        if self.events.visible(self._snapshot):
            self.events.release(point, self._snapshot)

    # self: Optional endpoint owner stopped before the containing Tk window is destroyed.
    # Raises RuntimeError or OSError if endpoint shutdown cannot complete.
    def close(self):
        self.endpoint.close()
