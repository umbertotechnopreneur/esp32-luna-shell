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

"""Visible SIMULATED CCP control panel and authored-scene preview for Cody Studio."""

from __future__ import annotations

import time
import sys
import re
import tkinter as tk
from dataclasses import replace
from datetime import datetime
from pathlib import Path
from tkinter import messagebox, ttk

from cody_ccp_simulator import LinkState, SimulatorEndpoint, SimulatorSnapshot
from cody_ccp_event_view import EventView
from cody_native import NativePersonalityPainter, NativePanelInputs, NativePanelMetric, NativePanelObservation, NativeImuStillnessState, NativeFirmwareBuildFieldState
from cody_native_behavior import CharacterActivity, CharacterWorkKind
from cody_native_debug import DiagnosticPage, debug_memory_button_contains, render_debug_controls
from cody_personality import PersonalityCue, PersonalityPresentation
from runtime_paths import resolve_runtime_output


# point: Logical desktop projection before the acquired integer coordinate boundary.
# Returns the acquired pixel or unavailable outside the round display; no clipping is invented.
# Raises ValueError for unsupported numeric pointer coordinates.
def acquired_touch_pixel(point):
    if point is None:
        return None
    x, y = point
    if (x - 233) ** 2 + (y - 233) ** 2 > 233 ** 2:
        return None
    return round(x), round(y)


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
        return "looking-for-pc", "Connecting", ("Your link is saved", "Retrying connection")
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
    titles = {"working": "Working", "editing": "Editing code", "running": "Running command",
              "paused": "Waiting for you", "ready": "Reply ready"}
    # Native identity rendering measures full bounded project/branch names in the fixed row.
    return identities[snapshot.phase], titles.get(snapshot.phase, ""), ()


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
        self._activity_phase_key = None
        self._activity_description_key = None
        self._activity_overlay_elapsed_us = 0
        self._activity_description_elapsed_us = 0
        self._activity_clock = time.monotonic()
        self._activity_observation_clock_us = 0
        self._activity_identity_key = None
        self._activity_identity_elapsed_us = 0
        self._operation_elapsed_us = -1
        self._admitted_phase = "none"
        self._companion_panel = 0
        self._gesture_point = None
        self._gesture_rotation = 0
        self._gesture_sampled_us = 0
        self._passive_visible = False
        self._shake_jobs = []
        self._debug_mode = "normal"
        self._diagnostic_page = DiagnosticPage.OVERVIEW
        self._diagnostic_controls_visible = False
        self._debug_normal_follow = True
        self._native_personality = None
        self._direction_cue = PersonalityCue.NONE
        self._connection_sampled = time.monotonic()
        self._connection_face = None
        self._model_sampled = time.perf_counter()
        self._displayed_values = None
        self._last_base_values = None
        self._debug_base_values = None
        self._route_face_state = None
        self.activity_preview = tk.BooleanVar(value=False)
        self.preview_phase = tk.StringVar(value="working")
        self.preview_scenario = tk.StringVar(value="Normal")
        self.preview_companion_mode = tk.StringVar(value="Desk")
        self.preview_project = tk.StringVar(value="ESP32 Luna Shell")
        self.preview_branch = tk.StringVar(value="codex/audio-settings")
        self.preview_description = tk.StringVar(value="Updating Cody audio cues and quiet-hours settings, then preparing the firmware for the device with clearer activity feedback and a distraction-free desktop experience.")
        self.preview_message = tk.StringVar(value="Preview is off. Synthetic metadata never enters the CCP endpoint.")
        self._preview_snapshot = None
        self._preview_compaction_sequence = 0
        self._preview_saved_follow = True
        self._preview_saved_playing = True
        self._preview_capture_due = None
        self._preview_capture_delay_ms = None
        section = window.add_section(parent, "SIMULATED / CCP1",
            "Real CCP packets on this PC. This identity belongs only to Cody Studio.")
        ttk.Checkbutton(section, text="Show CCP on the display", variable=self.follow,
            command=self.change_companion_mode).pack(anchor="w")
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
        preview = window.add_section(parent, "SIMULATED / Activity preview",
            "Local synthetic metadata through the shared C++ renderer. No pairing or packets are required; the authenticated endpoint remains separate.")
        ttk.Checkbutton(preview, text="Show SIMULATED activity preview", variable=self.activity_preview,
                        command=self.change_preview).pack(anchor="w")
        ttk.Label(preview, text="SIMULATED technical scenario").pack(anchor="w", pady=(12, 2))
        scenarios = ttk.Combobox(preview, textvariable=self.preview_scenario, state="readonly",
            values=("Normal", "Attention", "Reply ready", "Nearly full", "Long compaction",
                    "Refreshed / pending", "Counters updated", "Missing / stale", "Model changed", "Link lost",
                    "Cheap model", "Reasonable model", "Expensive model", "Custom"))
        scenarios.pack(fill="x")
        scenarios.bind("<<ComboboxSelected>>", self.select_preview_scenario)
        ttk.Label(preview, text="SIMULATED companion mode").pack(anchor="w", pady=(8, 2))
        companion_mode = ttk.Combobox(preview, textvariable=self.preview_companion_mode, state="readonly", values=("Desk", "Away"))
        companion_mode.pack(fill="x")
        companion_mode.bind("<<ComboboxSelected>>", self.change_companion_mode)
        ttk.Label(preview, style="Muted.TLabel", wraplength=310,
            text="On passive Home and technical pages, Desk retains the native rhythm. Away suppresses idle accessories and slows breathing and contour motion while preserving activity and attention alerts; device settings are unchanged.").pack(fill="x")
        ttk.Button(preview, text="SIMULATED shake: return Home", command=self.simulate_shake).pack(fill="x", pady=(8, 4))
        ttk.Label(preview, text="Phase").pack(anchor="w", pady=(12, 2))
        phase = ttk.Combobox(preview, textvariable=self.preview_phase, state="readonly",
            values=("active", "working", "editing", "running", "ready", "paused", "interrupted", "ended", "unavailable", "none"))
        phase.pack(fill="x")
        phase.bind("<<ComboboxSelected>>", self.apply_preview)
        for label, variable in (("Project", self.preview_project), ("Branch", self.preview_branch),
                                ("Description (printable ASCII, up to 192 characters)", self.preview_description)):
            ttk.Label(preview, text=label, wraplength=310).pack(anchor="w", pady=(8, 2))
            ttk.Entry(preview, textvariable=variable).pack(fill="x")
        ttk.Button(preview, text="Apply / replay preview", command=self.apply_preview).pack(fill="x", pady=(12, 4))
        self.preview_capture_button = ttk.Button(preview, text="Save annotated preview PNG", command=self.save_preview, state="disabled")
        self.preview_capture_button.pack(fill="x")
        ttk.Label(preview, textvariable=self.preview_message, style="Muted.TLabel", wraplength=310).pack(fill="x", pady=(8, 0))
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
            if self._debug_mode == "normal" and request["mode"] != "normal":
                self._debug_normal_follow = self.follow.get()
                self._debug_base_values = list(self._last_base_values if self.follow.get() and self._last_base_values is not None
                                               else self.window.face_state)
            self._debug_mode = request["mode"]
            self._diagnostic_page = DiagnosticPage.OVERVIEW
            self._diagnostic_controls_visible = False
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
    # passive_panel: Whether this call paints fixed passive artwork rather than an attention route.
    # character_enabled: Admit ordinary companion/PC-approval acting; exclude Debug/setup/other questions.
    # activity: Current isolated CCP or explicit preview facts, without a fabricated lifecycle end.
    # gaze: Visible information or an original fresh logical pointer target.
    # source: Presentation owner identity; switching sources discards completion continuity.
    # Returns native evaluated geometry and its exact composed face values.
    # Raises RuntimeError for original response/contour/geometry failures.
    def compose_scene(self, base, placement, rotation, *, passive_panel=False, character_enabled=False,
                      activity=CharacterActivity(), gaze=None, source="ccp-hidden"):
        now = time.perf_counter()
        active_ms = max(0, (now - self._model_sampled) * 1000) if self.window.playing.get() else 0
        self._model_sampled = now
        if self._route_key and self._route_key[0] == "looking-for-pc" and self._connection_face is not None:
            base = self._connection_face
        if self._route_face_state is None:
            self._route_face_state = list(base)
        self._route_face_state = self.window.renderer.advance_face(self._route_face_state, base, active_ms)
        preview_idle = (self.activity_preview.get() and self._preview_snapshot is not None
            and self._preview_snapshot.state == LinkState.CONNECTED and self._preview_snapshot.activity_valid
            and self._preview_snapshot.phase in {"none", "active"}
            and self.preview_scenario.get() != "Long compaction")
        self.change_companion_mode()
        # Idle Home retains the native admission decision; the shared Away helper
        # suppresses accessories and adjusts artistic motion without changing source samples.
        inputs = replace(self.window.observe_stimuli(round(now * 1000000), rotation), idle_scene_allowed=preview_idle)
        self.window.response_config["motion"]["sensitivity"] = self.window.motion_sensitivity.get()
        self.window.response_state = self.window.renderer.advance_model(self.window.response_config,
            self.window.response_state, inputs, active_ms, round(now * 1000000), self.window.playing.get())
        targets = {name: value.get() for name, value in self.window.contour_values.items()}
        frame = self.window.renderer.compose_model(self.window.response_state, self._route_face_state, targets,
                                                   placement, rotation, self.window.playing.get())
        frame, self._direction_cue = self.window.direct_character(frame, placement, inputs, active_ms,
            (source, rotation), enabled=character_enabled, activity=activity, gaze=gaze)
        self.window.response_frame = frame
        if self.window.contour_playing.get():
            self.window.contour_state = self.window.renderer.advance_shape(self.window.contour_state,
                                                                           frame.contour, active_ms)
        self._last_base_values = list(base)
        self._displayed_values = list(frame.face)
        # Details keep their artwork inside the same fixed native slot as firmware.
        transform = placement if passive_panel else frame.transform
        scene = self.window.renderer.evaluate_scene(frame.face, transform, self.window.contour_state)
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
        delta_us = max(0, round((now - self._connection_sampled) * 1000000)) if self.window.playing.get() else 0
        self._connection_sampled = now
        connection = native.connection_personality(True, not snapshot.network_ready, True,
            snapshot.state == LinkState.CONNECTED,
            snapshot.state in (LinkState.DISCOVERING, LinkState.PAIRING, LinkState.INCOMPLETE), delta_us,
            wifi_connected=bool(snapshot.network_ready), search_view_visible=identity == "looking-for-pc")
        self._connection_face = native.connection_face()
        cue = native.scene_personality(identity, connection)
        if cue == PersonalityCue.NONE and identity != "looking-for-pc":
            cue = self._direction_cue
        cue = self.window.apply_automatic_power_cue(cue)
        if self._native_personality is None:
            self._native_personality = NativePersonalityPainter(native)
        if self.window.native_behavior is not None:
            self._native_personality.observe_power_support(self.window.native_behavior.power_low_active,
                self.window.automatic_power_cue == PersonalityCue.CHARGING)
        # The existing optional audio path reads this exact visible frame.
        self.window.renderer.personality_painter = self._native_personality
        self._native_personality.prepare(scene, cue, scale, identity, rotation, elapsed_ms,
                                         exclusions, presentation=PersonalityPresentation.MAIN,
                                         paused=not self.window.playing.get())

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

    # self: Explicit host-only preview switch; authenticated endpoint state is never modified.
    # Exceptions: Invalid synthetic values stay visible and prevent preview admission.
    def change_preview(self):
        if self.activity_preview.get():
            self._preview_saved_follow = self.follow.get()
            self._preview_saved_playing = self.window.playing.get()
            self.apply_preview()
            if self._preview_snapshot is None:
                self.activity_preview.set(False)
        else:
            self._preview_snapshot = None
            self._preview_capture_due = None
            self._preview_capture_delay_ms = None
            self.follow.set(self._preview_saved_follow)
            self.window.playing.set(self._preview_saved_playing)
            self.window.renderer.require_native().set_away(False)
            self.preview_capture_button.configure(state="disabled")
            self.preview_message.set("Preview is off. Showing the actual SIMULATED CCP endpoint.")
            self._route_key = None
            self._frame_key = None

    # self: Explicit SIMULATED preview presentation owner, separate from persisted device settings.
    # event: Optional Tk control event; no event text is treated as metadata.
    # Raises RuntimeError for the original native mode boundary error.
    def change_companion_mode(self, event=None):
        away = bool(self.activity_preview.get() and self.follow.get()
            and self._preview_snapshot is not None and self.preview_companion_mode.get() == "Away")
        # Every explicit activity preview is passive companion content, including
        # work, attention, compaction and disconnected technical pages. The
        # independent idle admission still prevents accessories on busy Home.
        self.window.renderer.require_native().set_away(away)

    # self: Rendering-only synthetic input owner, separate from pairing, packets and snapshots.
    # event: Optional Tk selection event; no remote or firmware command is admitted.
    # Exceptions: Rejected text or phase is shown without substituting a default.
    def apply_preview(self, event=None):
        if not self.activity_preview.get():
            return
        if event is not None:
            self.preview_scenario.set("Custom")
        values = (("Project", self.preview_project.get(), 64), ("Branch", self.preview_branch.get(), 64),
                  ("Description", self.preview_description.get(), 192))
        for label, value, maximum in values:
            if len(value) > maximum or any(not 32 <= ord(character) <= 126 for character in value):
                self.preview_message.set(f"{label} rejected: use at most {maximum} printable ASCII characters.")
                return
        phase = self.preview_phase.get()
        if phase not in {"none", "active", "working", "editing", "running", "ready", "paused", "interrupted", "ended", "unavailable"}:
            self.preview_message.set("Preview phase rejected: select an actual activity phase.")
            return
        # This value belongs only to the rendering path. Never publish or persist
        # it in SimulatorEndpoint, its authenticated packets or firmware snapshots.
        self._preview_snapshot = SimulatorSnapshot(source="SIMULATED PREVIEW", state=LinkState.CONNECTED,
            network_ready=True, retained_pairing=True, phase=phase, project=self.preview_project.get(), branch=self.preview_branch.get(),
            description=self.preview_description.get(), chat="f" * 64, activity_valid=True)
        if self.preview_scenario.get() == "Link lost":
            self._preview_snapshot = replace(self._preview_snapshot, state=LinkState.DISCOVERING, activity_valid=False)
        if self.preview_scenario.get() == "Long compaction":
            self._preview_compaction_sequence += 1
        self._route_key = None
        self._frame_key = None
        self._activity_phase_key = None
        self._activity_description_key = None
        self._activity_identity_key = None
        self._activity_overlay_elapsed_us = 0
        self._activity_description_elapsed_us = 0
        self._activity_identity_elapsed_us = 0
        self.window.renderer.require_native().observe_activity("none", "", False,
            self._activity_observation_clock_us)
        self._activity_clock = time.monotonic()
        self.window.renderer.require_native().reset_home_rotation()
        self.follow.set(True)
        self.window.playing.set(True)
        self.preview_capture_button.configure(state="normal")
        self.preview_message.set(f"SIMULATED preview: {phase}. Replay restarts native fade, marquee and operation clocks.")

    # self: Explicit synthetic scenario owner, never an endpoint or firmware data writer.
    # event: Optional Tk selection event; its contents are not used as telemetry.
    # Exceptions: Original preview metadata rejection remains visible through apply_preview.
    def select_preview_scenario(self, event=None):
        phases = {"Attention": "paused", "Reply ready": "ready", "Long compaction": "working",
                  "Refreshed / pending": "working", "Counters updated": "working", "Missing / stale": "unavailable"}
        self.preview_phase.set(phases.get(self.preview_scenario.get(), "working"))
        self.apply_preview()

    # self: Rendering-only DTO owner for visibly synthetic technical observations.
    # snapshot: Isolated preview metadata; never the live authenticated endpoint.
    # elapsed_us: Real active presentation elapsed used only to animate synthetic durations.
    # Returns native typed facts; all invented counters are confined to this explicit preview.
    def preview_panel_inputs(self, snapshot, elapsed_us):
        inputs = NativePanelInputs()
        for name, value in (("project", snapshot.project), ("branch", snapshot.branch),
                            ("description", snapshot.description), ("chat_id", snapshot.chat),
                            ("model", "SIMULATED-model-v2" if self.preview_scenario.get() == "Model changed" else "SIMULATED-model-v1")):
            setattr(inputs, name, value.encode("ascii"))
        phases = ("none", "active", "working", "editing", "running", "ready", "paused", "interrupted", "ended", "unavailable")
        inputs.phase = phases.index(snapshot.phase)
        inputs.simulated = True
        inputs.connected = snapshot.state == LinkState.CONNECTED
        inputs.link = 1 if inputs.connected else 2
        inputs.activity = 1 if snapshot.activity_valid else 2
        inputs.model_state = 1
        inputs.activity_age_us = NativePanelMetric(elapsed_us, 1)
        inputs.turn_elapsed_us = NativePanelMetric(elapsed_us, 1)
        scenario = self.preview_scenario.get()
        bands = {"Cheap model": (1, b"SIMULATED-gpt-6-luna"),
                 "Reasonable model": (2, b"SIMULATED-gpt-6-sol"),
                 "Expensive model": (3, b"SIMULATED-gpt-6-astra")}
        if scenario in bands:
            inputs.cost_band, inputs.model = bands[scenario]
            inputs.cost_state = 1
        if scenario == "Long compaction":
            inputs.compaction = 1
            inputs.compaction_elapsed_us = NativePanelMetric(90000000 + elapsed_us, 1)
        elif scenario in {"Refreshed / pending", "Counters updated"}:
            inputs.compaction = 2
            inputs.counters_pending = scenario == "Refreshed / pending"
        if scenario in {"Nearly full", "Counters updated"}:
            inputs.used = NativePanelMetric(118000 if scenario == "Nearly full" else 78000, 1)
            inputs.capacity = NativePanelMetric(128000, 1)
            inputs.threshold = NativePanelMetric(115000, 1)
            inputs.cumulative = NativePanelMetric(180000, 1)
            inputs.usage_age_us = NativePanelMetric(elapsed_us, 1)
            inputs.context_coherent = True
        elif scenario == "Missing / stale":
            inputs.model_state = 2
            inputs.activity = 2
            inputs.activity_age_us = NativePanelMetric(130000000 + elapsed_us, 2)
        elif scenario == "Link lost":
            inputs.model_state = 2
            inputs.activity_age_us = NativePanelMetric(elapsed_us, 2)
        return inputs

    # self: Explicit rendering-only preview owner; no live endpoint/cache/snapshot is changed.
    # snapshot: Isolated SIMULATED preview identity selected in Studio.
    # elapsed_us: Active preview clock; synthetic durations stay confined to native host scratch.
    # Returns raw synthetic metadata so Home and lower panels share native formatting and aging.
    def preview_home_observation(self, snapshot, elapsed_us):
        observation = self.panel_observation(snapshot)
        inputs = self.preview_panel_inputs(snapshot, elapsed_us)
        observation.technical_chat_id = snapshot.chat.encode("ascii")
        observation.technical_valid = True
        observation.model = inputs.model
        observation.effort = b"high"
        observation.effort_valid = True
        observation.device_ip = b"192.0.2.25"
        observation.device_ip_valid = bool(snapshot.network_ready)
        observation.simulated_build_local_date_time = b"14/10/26 16:00"
        observation.simulated_build_time_zone = b"SIMULATED Asia Zone"
        observation.simulated_build_computer = b"SIMULATED-PC"
        observation.simulated_build_clock_state = NativeFirmwareBuildFieldState.AVAILABLE
        observation.simulated_build_computer_state = NativeFirmwareBuildFieldState.AVAILABLE
        observation.simulated_motion_valid = True
        observation.simulated_motion = 4  # Explicit SIMULATED standing classifier observation.
        observation.simulated_stillness = NativeImuStillnessState.STILL
        observation.simulated_stillness_valid = True
        observation.simulated_stillness_error = 0
        observation.model_valid = True
        observation.model_age_us = 130000000 if inputs.model_state == 2 else 0
        observation.cost_band = inputs.cost_band
        observation.cost_valid_for_us = 3600000000 if inputs.cost_band else 0
        observation.compaction = inputs.compaction
        observation.compaction_valid = True
        observation.compaction_age_valid = inputs.compaction_elapsed_us.state == 1
        observation.compaction_age_us = inputs.compaction_elapsed_us.value
        observation.activity_age_valid = inputs.activity_age_us.state in (1, 2)
        observation.activity_age_us = inputs.activity_age_us.value
        observation.turn_elapsed_valid = inputs.turn_elapsed_us.state == 1
        observation.turn_elapsed_us = inputs.turn_elapsed_us.value
        observation.usage_age_us = inputs.usage_age_us.value
        for name in ("used", "capacity", "threshold", "cumulative", "input", "output"):
            metric = getattr(inputs, name)
            setattr(observation, name + "_valid", metric.state == 1 and not inputs.counters_pending)
            setattr(observation, name, metric.value)
        return observation

    # self: Live Studio projection owner using the firmware's canonical age and panel mapper.
    # snapshot: Authenticated endpoint observation with separate transport/source ages.
    # Returns raw facts only; source aging and presentation validity belong to shared C++.
    def panel_observation(self, snapshot):
        observation = NativePanelObservation()
        observation.device_id = snapshot.device_id.encode("ascii")
        # This is the simulator device's bound address, never the broker/peer IP.
        # Isolated source fixtures without an endpoint truthfully leave it unavailable.
        endpoint = getattr(self, "endpoint", None)
        device_ip = endpoint.bound_address() if endpoint is not None else None
        observation.device_ip = device_ip.encode("ascii") if device_ip else b""
        observation.device_ip_valid = bool(device_ip and snapshot.network_ready)
        technical = snapshot.technical
        coherent = self.has_coherent_technical(snapshot)
        known = self.has_known_technical(snapshot)
        for name, value in (("project", snapshot.project), ("branch", snapshot.branch),
                            ("description", snapshot.description), ("chat_id", snapshot.chat),
                            ("technical_chat_id", technical.chat if technical else ""),
                            ("model", technical.model if technical else ""), ("effort", technical.effort if technical else "")):
            setattr(observation, name, value.encode("ascii"))
        phases = ("none", "active", "working", "editing", "running", "ready", "paused", "interrupted", "ended", "unavailable")
        compactions = ("none", "active", "refreshed", "orphaned", "interrupted", "stale")
        observation.phase = phases.index(snapshot.phase)
        observation.compaction = compactions.index(technical.compaction.value) if technical else 0
        bands = ("unknown", "economy", "balanced", "expensive")
        observation.cost_band = bands.index(technical.cost_band.value) if coherent and technical else 0
        if known and not coherent:
            if technical.compaction.value == "active":
                observation.compaction = compactions.index("stale")
            elif technical.compaction.value == "refreshed":
                observation.compaction = 0
        observation.connected = snapshot.state == LinkState.CONNECTED and snapshot.network_ready
        observation.reconnecting = snapshot.state == LinkState.DISCOVERING and snapshot.retained_pairing
        observation.link_valid = bool(snapshot.retained_pairing)
        observation.link_stale = not observation.connected
        observation.activity_valid = snapshot.activity_valid
        observation.activity_stale = not snapshot.activity_valid
        observation.chat_title = snapshot.title.encode("ascii")
        observation.chat_title_state = ("unavailable", "current", "unsupported", "error").index(snapshot.title_state)
        observation.chat_title_age_us = (snapshot.title_age_ms or 0) * 1000
        observation.chat_title_error = snapshot.title_error
        # This bridge flag admits authenticated historical identity, not current
        # freshness. Separate metric flags require the endpoint's coherent pair;
        # retained compaction becomes explicit STALE instead of a fresh operation.
        observation.technical_valid = known
        observation.receipt_age_us = max(0, snapshot.technical_age_ms or 0) * 1000
        if technical is not None:
            observation.cost_age_us = (technical.cost_age_ms or 0) * 1000
            observation.cost_valid_for_us = (technical.cost_valid_for_ms or 0) * 1000
            for field, source in (("model", technical.model_age_ms), ("compaction", technical.compaction_age_ms),
                                  ("activity", technical.activity_age_ms), ("turn_elapsed", technical.turn_elapsed_ms)):
                valid_name = "model_valid" if field == "model" else field + "_age_valid" if field in {"compaction", "activity"} else "turn_elapsed_valid"
                setattr(observation, valid_name, coherent and source is not None)
                setattr(observation, field + "_age_us" if field != "turn_elapsed" else "turn_elapsed_us", (source or 0) * 1000)
            observation.compaction_valid = known and observation.compaction != 0
            observation.effort_valid = coherent and bool(technical.effort)
            for field, value in (("used", technical.context_used), ("capacity", technical.context_capacity),
                                 ("cumulative", technical.cumulative_tokens), ("input", technical.input_tokens),
                                 ("output", technical.output_tokens), ("threshold", technical.compact_threshold)):
                setattr(observation, field + "_valid", coherent and value is not None)
                setattr(observation, field, value if value is not None else 0)
            observation.usage_age_us = (technical.usage_age_ms or 0) * 1000
        return observation

    # self: Explicit CLI preview owner; the visible simulator remains open after its one native capture.
    # delay_ms: Real host animation time requested through the public simulator AtMs option.
    # Raises ValueError for a negative or non-integer capture delay.
    def start_cli_preview(self, delay_ms):
        if type(delay_ms) is not int or delay_ms < 0:
            raise ValueError("SIMULATED preview capture delay must be a non-negative integer")
        self.activity_preview.set(True)
        self.change_preview()
        if self._preview_snapshot is not None:
            self._preview_capture_delay_ms = delay_ms

    # self: Saves only the most recently rendered explicit synthetic frame using the existing capture sheet.
    # Exceptions: Native, filesystem and Tk errors remain visible; no substitute image is generated.
    def save_preview(self):
        if (not self.activity_preview.get() or self._frame is None or self._frame_key is None
                or self._frame_key[0] != "ccp-preview"):
            self.preview_message.set("No SIMULATED preview frame is available yet.")
            return
        try:
            phase = self._preview_snapshot.phase
            name = f"ccp-activity-preview-{datetime.now():%Y%m%d-%H%M%S-%f}-{phase}.png"
            destination = resolve_runtime_output(Path(name))
            destination.parent.mkdir(parents=True, exist_ok=True)
            rotation = self.window.rotation.get()
            if rotation not in (0, 90, 180, 270):
                raise ValueError(f"Unsupported capture rotation: {rotation}")
            image = self._frame[0].copy().rotate(-rotation)
            seconds = self._activity_overlay_elapsed_us / 1000000
            caption = f"CCP preview / {phase} / {seconds:.1f}s / {rotation} deg"
            self.window.renderer.capture_sheet(image, caption).save(destination)
            self.preview_message.set(f"Saved actual shared-renderer frame:\n{destination}")
            print(destination, flush=True)
        except (OSError, RuntimeError, ValueError, tk.TclError) as error:
            self.preview_message.set(f"SIMULATED preview capture failed: {error}")
            print(f"SIMULATED preview capture failed: {error}", file=sys.stderr, flush=True)

    # self: Existing authored scenes with authenticated metadata or an explicitly isolated synthetic preview.
    # rotation: Cardinal display rotation; the parent rotates the complete image.
    # Returns an image, its evaluated cloud and a semantic render key.
    # Raises ValueError for invalid authored geometry, bounded text or protocol state.
    def render(self, rotation):
        self.refresh()
        preview = self.activity_preview.get() and self._preview_snapshot is not None
        # Reset before every early Debug/event path as well as ordinary live
        # rendering; explicit preview rhythm must not leak into authenticated views.
        self.change_companion_mode()
        snapshot, code = (self._preview_snapshot, "") if preview else (self._snapshot, self._display_code)
        if not preview and self._debug_mode != "normal":
            self.pause_home_presentation()
            return self.render_debug(rotation)
        if not preview and self.events.visible(snapshot, self.endpoint):
            # Interaction dwell pauses both observed work and local identity/purpose animation clocks.
            self.pause_home_presentation()
            result = self.events.render(snapshot, self.window, rotation, self.prepare_personality, self.compose_scene,
                activity=self.character_activity(snapshot, False))
            return result
        self.update_activity_observation(snapshot)
        identity, title, lines = presentation(replace(snapshot, phase=self._admitted_phase), code)
        compacting = self.is_compacting(snapshot, preview)
        if compacting and not (snapshot.activity_valid and snapshot.state == LinkState.CONNECTED
                and snapshot.network_ready and snapshot.phase == "paused"):
            identity, title, lines = "working", "Compacting", ()
        painter = self.window.renderer.scene_painter
        definition = painter.definition(identity)
        title = title or definition["title"]["text"]
        dashboard = identity in {"connected", "project-active", "working", "response-ready",
            "session-paused", "session-ended", "companion-idle", "activity-unavailable", "connection-lost", "looking-for-pc"}
        # Switching a lower page never restarts the unchanged upper title or marquee.
        route_key = (identity, title, lines, code, 0 if dashboard else self._companion_panel)
        if route_key != self._route_key:
            self._route_key, self._route_started = route_key, time.monotonic()
        elapsed = (time.monotonic() - self._route_started) * 1000
        count = max(0, int(elapsed / 45))
        total = len(title) + (0 if dashboard else sum(len(line) for line in lines))
        self._passive_visible = count >= total
        rotation_visible = bool(dashboard and self.window.playing.get() and self._gesture_point is None and
                                (compacting or self._passive_visible))
        self._companion_panel = self.window.renderer.require_native().advance_home_rotation(
            self._companion_panel, self._activity_observation_clock_us, rotation_visible, compacting)
        key = ("ccp-preview" if preview else "ccp", route_key, self._companion_panel, min(count, total), rotation, round(elapsed / 33))
        cloud = definition["cloud"]
        placement = (self.window.renderer.require_native().home_transform()
            if dashboard else painter.cloud_transform(identity, elapsed))
        activity = self.character_activity(snapshot, preview)
        character_enabled = bool(dashboard or
            (snapshot.state == LinkState.CONNECTED and snapshot.network_ready))
        scene, values = self.compose_scene(painter.face(identity), placement, rotation,
            passive_panel=dashboard, character_enabled=character_enabled,
            activity=activity, gaze=self.character_gaze(activity, rotation),
            source="ccp-preview" if preview else "ccp")
        if dashboard:
            native = self.window.renderer.require_native()
            self.prepare_activity(snapshot, scene, compacting, dashboard=True)
            observation = (self.preview_home_observation(snapshot, self._activity_observation_clock_us)
                if preview else self.panel_observation(snapshot))
            native.prepare_home_identity(scene, observation, title, min(len(title), count), self._activity_observation_clock_us)
            native.prepare_home_dashboard(scene, self._companion_panel, observation, self._activity_observation_clock_us)
        else:
            self.prepare_activity(snapshot, scene, compacting)
            self.window.renderer.require_native().pause_activity()
        exclusions = [(73, 210, 393, 250), (73, 300, 393, 328), (153, 330, 313, 358),
                      (73, 360, 393, 388), (73, 420, 393, 445), (324, 118, 374, 179)]
        if dashboard:
            exclusions = [(33, 185, 433, 264), (40, 268, 426, 393), (113, 408, 353, 433)]
        # Lower-panel selection never changes the actor's admission on an unrelated route.
        self.prepare_personality(snapshot, identity, scene, placement[2] if dashboard else cloud["scale"],
                                 rotation, elapsed, exclusions)
        image = self.window.renderer.draw_scene(scene, debug=False, navigation_visible=False,
                                                background_rgb565=definition["background_rgb565"]).convert("RGB")
        # A changing packet counter never restarts the text clock. Code is a complete static control value.
        if not dashboard:
            painter.paint_text(image, title, 210, definition["title"]["color_rgb565"], visible=min(len(title), count))
        preceding = len(title)
        for row, line in enumerate(lines if not dashboard else ()):
            painter.paint_text(image, line, 266 + row * 28, body=True, visible=min(len(line), max(0, count - preceding)))
            preceding += len(line)
        if code and count >= preceding and not dashboard:
            painter.paint_text(image, code, 334, color=2047)
        # Keep synthetic provenance visible without painting over the native command marquee.
        painter.paint_text(image, "SIMULATED", 22 if dashboard else 420, color=64704, body=True)
        self._frame_key, self._frame = key, (image.copy(), scene)
        if preview and self._preview_capture_delay_ms is not None:
            self._preview_capture_due = time.monotonic() + self._preview_capture_delay_ms / 1000
            self._preview_capture_delay_ms = None
        if preview and self._preview_capture_due is not None and time.monotonic() >= self._preview_capture_due:
            # The saved frame is the one just rendered by the ordinary native
            # path; a failed capture stays visible and is never retried silently.
            self._preview_capture_due = None
            self.save_preview()
        return image, scene, key

    # self: Home presenter hidden by Debug, an interaction or another Studio view.
    # Raises original native pause/clock errors; hidden dwell never advances the page.
    def pause_home_presentation(self):
        native = self.window.renderer.require_native()
        self._companion_panel = native.advance_home_rotation(self._companion_panel,
            self._activity_observation_clock_us, False, False)
        native.pause_activity()
        self._activity_clock = time.monotonic()

    # self: Home presentation owner; only its completed canvas frame may consume a model-cost notice.
    # scene: Actual native scene just presented by the containing Studio window.
    # Exceptions: Native policy failures remain visible; optional PC playback failures stay with the audio owner.
    def present_home_audio(self, scene):
        visible = bool(self._passive_visible and self.window.playing.get())
        if self.window.renderer.require_native().home_presented(scene, visible):
            self.window.audio.set_enabled(self.window.audio_enabled.get())
            asset = Path(__file__).resolve().parents[2] / "assets/cody/audio/model-cost/generated/model-cost.wav"
            self.window.audio.model_cost_notice(asset)

    # self: Studio presentation adapter; protocol and freshness admission remain with the endpoint.
    # snapshot: One complete isolated CCP observation or explicitly selected preview snapshot.
    # preview: Whether the visible SIMULATED PREVIEW owns rendering-only facts.
    # Returns a bounded semantic projection for the shared native character director.
    def character_activity(self, snapshot, preview):
        current = bool(snapshot.activity_valid and snapshot.network_ready and
            snapshot.state == LinkState.CONNECTED and snapshot.phase != "unavailable")
        sequence, refreshed, turn = 0, False, ""
        if preview:
            sequence = self._preview_compaction_sequence
            refreshed = self.preview_scenario.get() in {"Refreshed / pending", "Counters updated"}
            turn = "e" * 64 if current else ""
        elif self.has_coherent_technical(snapshot):
            sequence = snapshot.technical.compaction_sequence
            refreshed = snapshot.technical.compaction.value == "refreshed"
            turn = snapshot.technical.turn if snapshot.technical.turn != "0" * 64 else ""
        work_kinds = {"working": CharacterWorkKind.THINKING, "editing": CharacterWorkKind.EDITING,
                      "running": CharacterWorkKind.RUNNING}
        return CharacterActivity(chat=snapshot.chat if current else "", sequence=sequence,
            current=current, working=current and snapshot.phase in {"working", "editing", "running"},
            attention=current and snapshot.phase == "paused",
            compacting=current and self.is_compacting(snapshot, preview), refreshed=current and refreshed,
            turn=turn if current else "",
            work_kind=work_kinds.get(snapshot.phase, CharacterWorkKind.NONE) if current else CharacterWorkKind.NONE,
            ready=current and snapshot.phase == "ready", interrupted=current and snapshot.phase == "interrupted")

    # self: Current passive pointer/text presentation; no hardware or person tracking is inferred.
    # activity: Current native-director semantic projection for this same rendered frame.
    # rotation: Shared cardinal logical orientation of rendering and pointer acquisition.
    # Returns only fresh original contact or already visible information; attention faces forward.
    def character_gaze(self, activity, rotation):
        point = self._gesture_point
        age_us = round(time.perf_counter() * 1000000) - self._gesture_sampled_us
        timeout_us = self.window.response_config["touch"]["event_timeout_seconds"] * 1000000
        if (point is not None and self._gesture_sampled_us > 0 and 0 <= age_us <= timeout_us and
                self._gesture_rotation == rotation and 0 <= point[0] < 466 and 0 <= point[1] < 466):
            return point
        if self._passive_visible and not activity.attention and (activity.working or activity.compacting):
            return (233, 288)
        return None

    # self: Persistent native observation policy; pausing animations freezes only its presentation clock.
    # snapshot: Complete isolated CCP metadata or an explicitly SIMULATED preview.
    # Raises ValueError or RuntimeError for genuine metadata or native policy errors.
    def update_activity_observation(self, snapshot):
        now = time.monotonic()
        self._activity_delta_us = round(max(0, now - self._activity_clock) * 1000000) if self.window.playing.get() else 0
        self._activity_clock = now
        self._activity_observation_clock_us += self._activity_delta_us
        current = snapshot.activity_valid and snapshot.state == LinkState.CONNECTED and snapshot.network_ready
        self._admitted_phase, self._operation_elapsed_us = self.window.renderer.require_native().observe_activity(
            snapshot.phase, snapshot.chat, current, self._activity_observation_clock_us)

    # self: Retains only local animation clocks; metadata continues to come from the isolated CCP owner.
    # snapshot: Authenticated SIMULATED phase and explicit purpose, never a physical-device observation.
    # scene: Actual native frame receiving the shared C++ activity and description rastering.
    # compacting: Authoritative current lifecycle or explicitly labelled synthetic preview.
    # dashboard: Shared native Home helper owns upper/lower text; retain only the activity overlay here.
    # Raises ValueError or RuntimeError for invalid metadata and original native preparation errors.
    def prepare_activity(self, snapshot, scene, compacting=False, dashboard=False):
        elapsed = self._activity_delta_us
        current = snapshot.activity_valid and snapshot.state == LinkState.CONNECTED and snapshot.network_ready
        working = current and snapshot.phase in {"working", "editing", "running"}
        overlay = 4 if compacting else 1 if working else 2 if current and snapshot.phase == "paused" else 3 if current and snapshot.phase == "ready" else 0
        captions = {"active": "Ready when you are", "working": "Processing your request",
                    "editing": "Editing your code", "running": "Running a command", "ready": "Open Codex to review",
                    "paused": "Approve on your PC", "interrupted": "Paused in Codex"}
        description = (snapshot.description if working and snapshot.description else
                       captions.get(self._admitted_phase, "")) if current else ""
        if current and snapshot.phase == "paused":
            description = "Approve on your PC"
        elif compacting:
            description = "Refreshing working context"
        elif (self.has_coherent_technical(snapshot) and snapshot.technical is not None
                and snapshot.technical.compaction.value == "refreshed"
                and snapshot.technical.compaction_age_ms is not None
                and snapshot.technical.compaction_age_ms + (snapshot.technical_age_ms or 0) < 3000):
            description = "Context refreshed; counters pending"
        elif (self.has_known_technical(snapshot) and not self.has_coherent_technical(snapshot)
                and snapshot.technical.compaction.value == "active"):
            description = "Compaction: STALE"
        elif (self.has_known_technical(snapshot) and snapshot.technical is not None
                and snapshot.technical.compaction.value == "orphaned"):
            description = "Compaction not fully observed"
        elif (self.has_known_technical(snapshot) and snapshot.technical.compaction.value == "interrupted"):
            description = "Compaction interrupted"
        elif (self.has_known_technical(snapshot) and snapshot.technical.compaction.value == "stale"):
            description = "Compaction: STALE"
        elif (self.activity_preview.get() and self.preview_scenario.get() in {"Refreshed / pending", "Counters updated"}):
            description = "Context refreshed; counts pending" if self.preview_scenario.get() == "Refreshed / pending" else "Context refreshed"
        phase_key = (snapshot.chat, overlay, current, snapshot.phase)
        description_key = (snapshot.chat, description, current)
        attention = compacting or current and snapshot.phase == "paused"
        model_identity = snapshot.technical.model if self.has_coherent_technical(snapshot) and snapshot.technical else ""
        identity_key = (snapshot.chat, snapshot.project, snapshot.branch, model_identity, current, attention)
        if phase_key != self._activity_phase_key:
            self._activity_phase_key = phase_key
            self._activity_overlay_elapsed_us = 0
        else:
            self._activity_overlay_elapsed_us = min(0xffffffff, self._activity_overlay_elapsed_us + elapsed)
        if description_key != self._activity_description_key:
            self._activity_description_key = description_key
            self._activity_description_elapsed_us = 0
        else:
            self._activity_description_elapsed_us += elapsed
        if identity_key != self._activity_identity_key:
            self._activity_identity_key = identity_key
            self._activity_identity_elapsed_us = 0
        else:
            self._activity_identity_elapsed_us += elapsed
        native = self.window.renderer.require_native()
        operation_elapsed = self._operation_elapsed_us if working else -1
        if compacting:
            if self.activity_preview.get():
                operation_elapsed = 90000000 + self._activity_identity_elapsed_us
            else:
                operation_elapsed = (snapshot.technical.compaction_age_ms + (snapshot.technical_age_ms or 0)) * 1000
        if not dashboard:
            native.prepare_activity_identity(scene, snapshot.project if current else "", snapshot.branch if current else "",
                self.preview_home_observation(snapshot, self._activity_identity_elapsed_us) if self.activity_preview.get()
                else self.panel_observation(snapshot), self._activity_identity_elapsed_us, attention, operation_elapsed, compacting)
        native.prepare_activity(scene, overlay,
            self._activity_overlay_elapsed_us, "" if dashboard else description, self._activity_description_elapsed_us)

    # self: Companion presentation owner; no threshold or tool-duration inference is admitted.
    # snapshot: Genuine transport metadata or an isolated explicit preview snapshot.
    # preview: Whether visibly synthetic scenario data was explicitly selected.
    # Returns true only for a current genuine compact start or the named synthetic scenario.
    def is_compacting(self, snapshot, preview):
        if preview:
            return self.preview_scenario.get() == "Long compaction"
        technical = snapshot.technical
        return bool(self.has_coherent_technical(snapshot) and technical is not None
            and technical.compaction.value == "active" and technical.compaction_age_ms is not None
            and technical.compaction_age_ms + (snapshot.technical_age_ms or 0) <= 120000)

    # self: Presentation consumer; revision admission remains owned by SimulatorEndpoint.
    # snapshot: Canonical isolated observation with a coherence-qualified technical_valid flag.
    # Returns current technical metadata only when the accepted ACTIVITY identity still matches.
    def has_coherent_technical(self, snapshot):
        # technical_valid is set only after exact ACTIVITY chat/revision equality
        # and the independent TECH transport lease; a heartbeat cannot admit a mismatch.
        return bool(snapshot.technical_valid and snapshot.technical is not None
            and snapshot.activity_valid and snapshot.state == LinkState.CONNECTED and snapshot.network_ready
            and snapshot.technical.chat == snapshot.chat)

    # self: Presentation consumer retaining only already authenticated same-chat history.
    # snapshot: Isolated canonical endpoint observation; no transport freshness is inferred.
    # Returns whether lifecycle history can be labelled without borrowing another chat's data.
    def has_known_technical(self, snapshot):
        return bool(snapshot.technical is not None and snapshot.chat != "0" * 64
            and snapshot.technical.chat == snapshot.chat)

    # self: Transient Debug presentation retaining the normal authored route and session preferences.
    # rotation: Same logical orientation applied to native graphics and acquired pointer coordinates.
    # Raises RuntimeError for actual shared C++ geometry or Back-control failures.
    def render_debug(self, rotation):
        native = self.window.renderer.require_native()
        elapsed = max(0, (time.monotonic() - self._route_started) * 1000)
        scene, _ = self.compose_scene(self._debug_base_values, (233, 205, 130), rotation)
        if self._native_personality is not None:
            self._native_personality.disable(elapsed)
        self.window.renderer.personality_painter = None
        image = self.window.renderer.draw_scene(scene, debug=False, navigation_visible=False, background_rgb565=0)
        image = render_debug_controls(native, image, self._diagnostic_page)
        # Host facts stay visibly SIMULATED and never enter firmware snapshots.
        painter = self.window.renderer.scene_painter
        if self._diagnostic_page == DiagnosticPage.OVERVIEW:
            address = self.endpoint.bound_address()
            painter.paint_text(image, f"IP {address or 'UNAVAILABLE'}", 390, body=True)
            painter.paint_text(image, "ESP32 DATA N/A", 416, body=True)
        self.window.renderer.scene_painter.paint_text(image, "SIMULATED", 22, color=64704, body=True)
        self._diagnostic_controls_visible = True
        return image, scene, ("ccp-debug", self._debug_mode, self._diagnostic_page, rotation, round(elapsed / 33))

    # self: Circular interaction view using the parent's already-rotated logical touch coordinates.
    # point: Logical press, or None when the pointer is outside the display.
    # Errors are retained by the endpoint and shown without making a synthetic reply.
    def press(self, point):
        self.refresh()
        point = acquired_touch_pixel(point)
        preview = self.activity_preview.get() and self._preview_snapshot is not None
        if not preview and self._debug_mode != "normal":
            if not self._diagnostic_controls_visible:
                return
            if point is not None and (point[0] - 40) ** 2 + (point[1] - 233) ** 2 <= 900:
                if self._diagnostic_page == DiagnosticPage.MEMORY:
                    self._diagnostic_page = DiagnosticPage.OVERVIEW
                else:
                    self._debug_mode = "normal"
                    self.follow.set(self._debug_normal_follow)
                self._frame_key = None
                self._diagnostic_controls_visible = False
            elif self._diagnostic_page == DiagnosticPage.OVERVIEW and debug_memory_button_contains(
                    self.window.renderer.require_native(), point):
                self._diagnostic_page = DiagnosticPage.MEMORY
                self._frame_key = None
                self._diagnostic_controls_visible = False
            return
        if not preview and self.events.visible(self._snapshot, self.endpoint):
            try:
                self.events.press(point, self._snapshot, self.endpoint)
            except (OSError, RuntimeError, ValueError) as error:
                messagebox.showerror("SIMULATED CCP", str(error), parent=self.window.root)
            return
        snapshot = self._preview_snapshot if preview else self._snapshot
        eligible = self._passive_visible and point is not None and (snapshot.state == LinkState.CONNECTED or snapshot.retained_pairing)
        self._gesture_point = point
        self._gesture_rotation = self.window.display_rotation
        self._gesture_sampled_us = round(time.perf_counter() * 1000000) if point is not None else 0
        if point is None:
            self.window.renderer.require_native().cancel_companion_tap()
            return
        self.window.renderer.require_native().observe_companion_tap(point[0], point[1], True, True,
            self._gesture_sampled_us, self._gesture_rotation, eligible, False)

    # self: Retained native contact recognizer; movement never selects a panel directly.
    # point: Successfully projected logical pointer sample or unavailable when outside the display.
    # Raises RuntimeError for the original shared gesture error.
    def drag(self, point):
        point = acquired_touch_pixel(point)
        self.events.drag(point)
        if self._gesture_point is None:
            return
        native = self.window.renderer.require_native()
        if point is None:
            native.cancel_companion_tap()
            self._gesture_sampled_us = 0
            return
        self._gesture_point = point
        self._gesture_sampled_us = round(time.perf_counter() * 1000000)
        native.observe_companion_tap(point[0], point[1], True, False,
            self._gesture_sampled_us, self.window.display_rotation, True, False)

    # self: Existing Debug/event/passive navigation owner; release also retires character gaze contact.
    # point: Logical released point, or None outside the display; existing tap/swipe rules retain ownership.
    # Raises RuntimeError for original native navigation errors.
    def release(self, point):
        self.refresh()
        self._gesture_sampled_us = 0
        point = acquired_touch_pixel(point)
        preview = self.activity_preview.get() and self._preview_snapshot is not None
        if not preview and self._debug_mode != "normal":
            return
        if not preview and self.events.visible(self._snapshot, self.endpoint):
            try:
                self.events.release(point, self._snapshot, self.endpoint)
            except (OSError, RuntimeError, ValueError) as error:
                messagebox.showerror("SIMULATED CCP", str(error), parent=self.window.root)
            self.window.renderer.require_native().cancel_companion_tap()
            self._gesture_point = None
            return
        native = self.window.renderer.require_native()
        if self._gesture_point is None:
            return
        if point is None:
            native.cancel_companion_tap()
        release_point = self._gesture_point
        accepted = native.observe_companion_tap(release_point[0], release_point[1], False, False,
            round(time.perf_counter() * 1000000), self.window.display_rotation, point is not None, False)
        self._gesture_point = None
        if accepted:
            self._companion_panel = native.next_companion_panel(self._companion_panel)
            native.reset_home_rotation()

    # self: Explicit SIMULATED motion source; a real shared recognizer alone chooses Home.
    # Exceptions: Tk or native gesture errors remain visible and never become successful navigation.
    def simulate_shake(self):
        for job in self._shake_jobs:
            self.window.root.after_cancel(job)
        self._shake_jobs.clear()
        # Calm samples arm the same recognizer as the device. Three alternating
        # acquired peaks are separated by real host-clock intervals and calm samples.
        samples = [(index * 50, 0.0) for index in range(12)]
        samples += [(600, 12.0), (650, 0.0), (700, -12.0), (750, 0.0), (800, 12.0)]
        for delay, acceleration in samples:
            job = self.window.root.after(delay, self.publish_simulated_shake, acceleration)
            self._shake_jobs.append(job)

    # self: Explicit synthetic motion acquisition callback, never a physical IMU publisher.
    # acceleration: Original finite synthetic X-axis acceleration in metres per second squared.
    # Raises RuntimeError for the actual native recognition failure.
    def publish_simulated_shake(self, acceleration):
        now_us = round(time.perf_counter() * 1000000)
        preview = self.activity_preview.get() and self._preview_snapshot is not None
        blocked = not preview and (self._debug_mode != "normal" or self.events.visible(self._snapshot, self.endpoint))
        if self.window.renderer.require_native().observe_companion_shake(acceleration, 0.0, 9.81,
                now_us, now_us, self.follow.get() and not blocked):
            self._companion_panel = 0
            self.window.renderer.require_native().reset_home_rotation()
            self._route_key = None

    # self: Optional endpoint owner stopped before the containing Tk window is destroyed.
    # Raises RuntimeError or OSError if endpoint shutdown cannot complete.
    def close(self):
        for job in self._shake_jobs:
            self.window.root.after_cancel(job)
        self._shake_jobs.clear()
        self.endpoint.close()
