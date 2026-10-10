"""Visible SIMULATED CCP control panel and authored-scene preview for Cody Studio."""

from __future__ import annotations

import time
import sys
import re
import tkinter as tk
from dataclasses import replace
from tkinter import messagebox, ttk

from cody_ccp_simulator import LinkState, SimulatorEndpoint, SimulatorSnapshot
from cody_ccp_event_view import EventView
from cody_native import NativePersonalityPainter
from cody_native_debug import render_debug_channels
from cody_personality import PersonalityPresentation


# snapshot: One isolated, authenticated simulator observation, never a firmware snapshot.
# code: Ephemeral code obtained only for the visible simulator display.
# Returns the authored scene, bounded title override and at most two caption lines.
def presentation(snapshot: SimulatorSnapshot, code: str) -> tuple[str, str, tuple[str, ...]]:
    if not snapshot.network_ready:
        return "network-wait", "Wi-Fi needed", ("SIMULATED LAN offline", "Your link is saved" if snapshot.retained_pairing else "Connect LAN to pair")
    if snapshot.state == LinkState.PAIRING:
        return "connect-codex", "Connect Codex", ("Share code in Codex", "Confirm simulator")
    if snapshot.state == LinkState.INCOMPLETE:
        return "pairing-failed", "Pairing pending", ("Awaiting confirmation", "Cancel to start again")
    if snapshot.state == LinkState.UNPAIRED:
        return "codex-choice", "Connect Codex?", ("Choose Connect Codex", "in the CCP panel")
    if snapshot.state == LinkState.DISCOVERING:
        return "looking-for-pc", "Finding your PC", ("Your link is saved", "Open Codex to rejoin")
    if snapshot.state == LinkState.PAIRED:
        return "setup-complete", "You're all set.", ("Open Codex to rejoin",)
    if snapshot.state == LinkState.TIMED_OUT:
        return "pairing-failed", "Code expired", ("Choose Connect Codex", "for a new code")
    if snapshot.state == LinkState.STOPPED:
        return "pairing-failed", "Link stopped", ("See the CCP panel",)
    if not snapshot.activity_valid:
        return "activity-unavailable", "UNAVAILABLE", ("Activity unavailable", "Check Codex on PC")
    if snapshot.phase == "none":
        return "connected", "Connected", ("Ready to team up", "Start a chat in Codex")
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
        self._debug_mode = "normal"
        self._debug_scroll = 0
        self._debug_press = None
        self._debug_normal_follow = True
        self._native_personality = None
        self._connection_sampled = time.monotonic()
        self._model_sampled = time.perf_counter()
        self._displayed_values = None
        self._last_base_values = None
        self._debug_base_values = None
        self._route_face_state = None
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
            self.window.renderer.require_native()
            self.endpoint.start()
            self.endpoint.enable_debug(True)
        except (OSError, RuntimeError, ValueError) as error:
            self._startup_error = f"CCP start: {type(error).__name__}: {error}"
            print(f"SIMULATED {self._startup_error}", file=sys.stderr, flush=True)
        self.refresh()

    # self: UI owner publishing one synchronized, secret-free status and separate display code.
    # Raises tk.TclError if the panel is no longer available.
    def refresh(self):
        self.apply_debug_request()
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

    # self: Rendering-thread owner applying an authenticated Debug view transaction.
    # Exceptions: Original native errors are acknowledged and kept visible; ACK state failure propagates.
    def apply_debug_request(self):
        request = self.endpoint.take_debug()
        if request is None:
            return
        native = self.window.renderer.require_native()
        count = int(native.api.cody_host_channel_count())
        error = 0
        try:
            if request["mode"] != "normal":
                # Preflight current native geometry/text without changing the visible route.
                values = self._displayed_values
                if values is None:
                    frame = self.window.response_frame
                    if frame is None:
                        raise ValueError("No composed native frame is available for Debug")
                    values = list(frame.face)
                self.window.renderer.evaluate_scene(values, (233, 205, 130))
                if request["mode"] == "face_channels":
                    render_debug_channels(native, values, request["scroll"])
            if self._debug_mode == "normal" and request["mode"] != "normal":
                self._debug_normal_follow = self.follow.get()
                self._debug_base_values = list(self._last_base_values if self.follow.get() and self._last_base_values is not None
                                               else self.window.face_state)
            self._debug_mode, self._debug_scroll = request["mode"], request["scroll"]
            self.follow.set(self._debug_normal_follow if self._debug_mode == "normal" else True)
            self._frame_key = None
        except (ValueError, RuntimeError) as failure:
            match = re.search(r"esp_err_t=0x([0-9a-fA-F]{8})", str(failure))
            error = int(match.group(1), 16) if match else 0x102
            if error >= 2 ** 31:
                error -= 2 ** 32
            self._startup_error = f"Optional SIMULATED Debug view rejected: {failure}"
            print(self._startup_error, file=sys.stderr, flush=True)
        self.endpoint.complete_debug(request["id"], error, count)

    # self: Actual shared C++ response owner for both ordinary CCP and its Debug overlay.
    # base: Current independent authored face controls, before native animation composition.
    # placement: Authored logical resting root/scale shared by the response model.
    # rotation: Logical orientation also supplied to synthetic touch/motion observations.
    # Returns native evaluated geometry and its exact composed face values.
    # Raises RuntimeError for original response/contour/geometry failures.
    def compose_scene(self, base, placement, rotation):
        now = time.perf_counter()
        active_ms = max(0, (now - self._model_sampled) * 1000) if self.window.playing.get() else 0
        self._model_sampled = now
        if self._route_face_state is None:
            self._route_face_state = list(base)
        self._route_face_state = self.window.renderer.advance_face(self._route_face_state, base, active_ms)
        inputs = replace(self.window.observe_stimuli(round(now * 1000000), rotation), idle_scene_allowed=False)
        self.window.response_config["motion"]["sensitivity"] = self.window.motion_sensitivity.get()
        self.window.response_state = self.window.renderer.advance_model(self.window.response_config,
            self.window.response_state, inputs, active_ms, round(now * 1000000), self.window.playing.get())
        targets = {name: value.get() for name, value in self.window.contour_values.items()}
        frame = self.window.renderer.compose_model(self.window.response_state, self._route_face_state, targets,
                                                   placement, rotation, self.window.playing.get())
        self.window.response_frame = frame
        if self.window.contour_playing.get():
            self.window.contour_state = self.window.renderer.advance_shape(self.window.contour_state,
                                                                           frame.contour, active_ms)
        self._last_base_values = list(base)
        self._displayed_values = list(frame.face)
        scene = self.window.renderer.evaluate_scene(frame.face, frame.transform, self.window.contour_state)
        return scene, self._displayed_values

    # self: Shared native connection-cue model retained across genuine CCP observations.
    # snapshot: Isolated current simulator link state, visibly labelled SIMULATED.
    # identity: Exact authored current route.
    # scene: Already evaluated actual C++ geometry which receives the native sprite.
    # scale: Authored character scale before breathing.
    # rotation: Logical cardinal orientation shared by rendering/touch.
    # elapsed_ms: Active route clock.
    # exclusions: Half-open content/navigation bounds reserved before manga placement.
    # Raises RuntimeError for original native model or layout errors.
    def prepare_personality(self, snapshot, identity, scene, scale, rotation, elapsed_ms, exclusions):
        native = self.window.renderer.require_native()
        now = time.monotonic()
        delta_us = max(0, round((now - self._connection_sampled) * 1000000))
        self._connection_sampled = now
        connection = native.connection_personality(True, not snapshot.network_ready, True,
            snapshot.state == LinkState.CONNECTED,
            snapshot.state in (LinkState.DISCOVERING, LinkState.PAIRING, LinkState.INCOMPLETE), delta_us)
        cue = native.scene_personality(identity, connection)
        if self._native_personality is None:
            self._native_personality = NativePersonalityPainter(native)
        # The existing optional audio path reads this exact visible frame.
        self.window.renderer.personality_painter = self._native_personality
        self._native_personality.prepare(scene, cue, scale, identity, rotation, elapsed_ms,
                                         exclusions, presentation=PersonalityPresentation.MAIN)

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
        if self._debug_mode != "normal":
            return self.render_debug(rotation)
        if self.events.visible(snapshot, self.endpoint):
            result = self.events.render(snapshot, self.window, rotation, self.prepare_personality, self.compose_scene)
            return result
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
        key = ("ccp", route_key, min(count, total), rotation, round(elapsed / 33))
        cloud = definition["cloud"]
        placement = painter.cloud_transform(identity, elapsed)
        scene, values = self.compose_scene(painter.face(identity), placement, rotation)
        exclusions = [(73, 210, 393, 240), (73, 266, 393, 330), (73, 334, 393, 365), (73, 420, 393, 445)]
        self.prepare_personality(snapshot, identity, scene, cloud["scale"], rotation, elapsed, exclusions)
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

    # self: Transient Debug presentation retaining the normal authored route and session preferences.
    # rotation: Same logical orientation applied to native graphics and acquired pointer coordinates.
    # Raises RuntimeError for actual shared C++ geometry/list failures.
    def render_debug(self, rotation):
        native = self.window.renderer.require_native()
        elapsed = max(0, (time.monotonic() - self._route_started) * 1000)
        scene, values = self.compose_scene(self._debug_base_values, (233, 205, 130), rotation)
        if self._native_personality is not None:
            self._native_personality.disable(elapsed)
        self.window.renderer.personality_painter = None
        if self._debug_mode == "face_channels":
            image = render_debug_channels(native, values, self._debug_scroll)
        else:
            image = self.window.renderer.draw_scene(scene, debug=False, navigation_visible=False, background_rgb565=0)
            image = render_debug_channels(native, values, 0, image)
            # Host facts stay visibly SIMULATED and never enter firmware snapshots.
            painter = self.window.renderer.scene_painter
            address = self.endpoint.bound_address()
            painter.paint_text(image, f"IP {address or 'UNAVAILABLE'}", 352, body=True)
            painter.paint_text(image, "ESP32 DATA UNAVAILABLE", 384, body=True)
        self.window.renderer.scene_painter.paint_text(image, "SIMULATED", 22, color=64704, body=True)
        return image, scene, ("ccp-debug", self._debug_mode, self._debug_scroll, rotation, round(elapsed / 33))

    # self: Circular interaction view using the parent's already-rotated logical touch coordinates.
    # point: Logical press, or None when the pointer is outside the display.
    # Errors are retained by the endpoint and shown without making a synthetic reply.
    def press(self, point):
        self.refresh()
        if self._debug_mode != "normal":
            self._debug_press = point
            if point is not None and (point[0] - 40) ** 2 + (point[1] - 233) ** 2 <= 900:
                self._debug_mode = "normal"
                self.follow.set(self._debug_normal_follow)
                self._frame_key = None
            return
        if self.events.visible(self._snapshot, self.endpoint):
            try:
                self.events.press(point, self._snapshot, self.endpoint)
            except (OSError, RuntimeError, ValueError) as error:
                messagebox.showerror("SIMULATED CCP", str(error), parent=self.window.root)

    # self: Read-only PR failure-list navigation.
    # point: Logical released point; vertical swipes page complete names, never select a row.
    def release(self, point):
        self.refresh()
        if self._debug_mode != "normal":
            if (self._debug_mode == "face_channels" and point is not None and self._debug_press is not None
                    and 73 <= self._debug_press[0] < 393 and 100 <= self._debug_press[1] < 366
                    and abs(point[0] - self._debug_press[0]) <= 80):
                displacement = self._debug_press[1] - point[1]
                count = int(self.window.renderer.require_native().api.cody_host_channel_count())
                if abs(displacement) >= 24:
                    rows = max(1, round(abs(displacement) / 34))
                    self._debug_scroll = max(0, min(count - 1, self._debug_scroll + (rows if displacement > 0 else -rows)))
            self._debug_press = None
            return
        if self.events.visible(self._snapshot, self.endpoint):
            self.events.release(point, self._snapshot)

    # self: Optional endpoint owner stopped before the containing Tk window is destroyed.
    # Raises RuntimeError or OSError if endpoint shutdown cannot complete.
    def close(self):
        self.endpoint.close()
