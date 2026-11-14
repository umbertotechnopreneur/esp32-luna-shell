"""Native-device bootstrap and input transport; no Python device drawing."""

import ctypes
from copy import deepcopy
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys
import time

from cody_host_lan import host_lan_ipv4
from cody_native_display import (StudioFrameIdentity, completed_frame, completed_studio_frame,
                                 completed_studio_route_frame, completed_studio_worker_frame)
from cody_native_runtime import ROOT, load_runtime_library
from cody_native_snapshot import NativeSnapshot, checked


class BootstrapReport(ctypes.Structure):
    _fields_ = [("settings_storage", ctypes.c_int), ("initial_snapshot", ctypes.c_int),
                ("pairing", ctypes.c_int), ("panel", ctypes.c_int), ("display", ctypes.c_int),
                ("audio_backend", ctypes.c_int)]


class StudioCall(ctypes.Structure):
    _fields_ = [("id", ctypes.c_uint64), ("turn_id", ctypes.c_uint64),
                ("tool_name", ctypes.c_char * 129), ("label", ctypes.c_char * 97),
                ("category", ctypes.c_char * 16), ("state", ctypes.c_char * 16),
                ("outcome", ctypes.c_char * 16)]


class StudioCase(ctypes.Structure):
    _fields_ = [("abi", ctypes.c_uint32), ("size", ctypes.c_uint32), ("identity", StudioFrameIdentity),
                ("brightness_percent", ctypes.c_uint32), ("auto_rotate", ctypes.c_uint32),
                ("orientation_degrees", ctypes.c_uint32), ("sounds_enabled", ctypes.c_uint32),
                ("playback_volume", ctypes.c_uint32), ("quiet_enabled", ctypes.c_uint32),
                ("quiet_start_minute", ctypes.c_uint32), ("quiet_end_minute", ctypes.c_uint32),
                ("quiet_edit_start", ctypes.c_uint32), ("quiet_draft_minute", ctypes.c_uint32),
                ("weekly_reset_unix_seconds", ctypes.c_int64), ("weekly_remaining_basis_points", ctypes.c_uint32),
                ("weekly_source_age_ms", ctypes.c_uint32), ("short_reset_unix_seconds", ctypes.c_int64),
                ("short_remaining_basis_points", ctypes.c_uint32), ("short_window_minutes", ctypes.c_uint32),
                ("short_source_age_ms", ctypes.c_uint32), ("provisioning_outcome", ctypes.c_char * 24),
                ("codex_link_outcome", ctypes.c_char * 16),
                ("domain_source_id", ctypes.c_uint64), ("domain_chat_id", ctypes.c_uint64),
                ("domain_turn_id", ctypes.c_uint64), ("domain_generation", ctypes.c_uint32),
                ("domain_call_count", ctypes.c_uint32), ("domain_chat_title", ctypes.c_char * 129),
                ("domain_execution", ctypes.c_char * 16), ("domain_calls", StudioCall * 8)]


class StudioLiveCue(ctypes.Structure):
    _fields_ = [("id", ctypes.c_uint32), ("sequence", ctypes.c_uint32), ("error", ctypes.c_int32),
                ("state", ctypes.c_char * 24)]


class StudioLiveStatus(ctypes.Structure):
    _fields_ = [("abi", ctypes.c_uint32), ("animation_complete", ctypes.c_uint32),
                ("audio_complete", ctypes.c_uint32), ("cue_count", ctypes.c_uint32),
                ("worker_error", ctypes.c_int32), ("cues", StudioLiveCue * 8)]


class StudioRouteRequest(ctypes.Structure):
    _fields_ = [("abi", ctypes.c_uint32), ("size", ctypes.c_uint32), ("route_id", ctypes.c_char * 32),
                ("origin_scenario_revision", ctypes.c_char * 65), ("destination_scenario_revision", ctypes.c_char * 65)]


class StudioWorkerRequest(ctypes.Structure):
    _fields_ = [("abi", ctypes.c_uint32), ("size", ctypes.c_uint32),
                ("requested_ms", ctypes.c_uint32), ("scenario_revision", ctypes.c_char * 65)]


class NativeDevice:
    """Host lifetime owner of one actual Cody runtime and explicit synthetic input."""

    # snapshot: Complete synthetic scenario in the native snapshot schema.
    # settings_path: Explicit host-only .nvs path under artifacts/simulator.
    # initial_brightness: Explicit synthetic panel level, 0..100.
    # panel_enabled: Explicit synthetic panel gate.
    # reset_reason: Explicit synthetic ESP reset reason 0..10.
    # use_pc_lan: Read-only opt-in to project this PC's LAN as a synthetic Cody link.
    # use_pc_clock: Read-only opt-in to use this PC's time in the simulated clock.
    # deterministic_clock: Freeze native time before bootstrap for the static Studio sample.
    # Raises native/source validation, bootstrap or startup errors; no Python fallback.
    def __init__(self, snapshot, settings_path, initial_brightness, panel_enabled, reset_reason, use_pc_lan=False,
                 simulated_open_wifi=False, simulated_battery_percent=None, use_pc_clock=False,
                 simulated_wifi_scenario=None, simulated_pairing=None, simulated_codex=None,
                 deterministic_clock=False, audio_mode="host-output"):
        self.api = load_runtime_library("graphics-device")
        if type(deterministic_clock) is not bool:
            raise ValueError("deterministic_clock must be boolean")
        if deterministic_clock and (use_pc_lan or use_pc_clock or simulated_open_wifi or
                                    simulated_wifi_scenario or simulated_pairing or simulated_codex):
            raise ValueError("Static Studio sessions require explicit offline input and clock data")
        self.deterministic_clock = deterministic_clock
        audio_modes = {"host-output": 0, "silent": 1, "test-probe": 2}
        if not isinstance(audio_mode, str) or audio_mode not in audio_modes:
            raise ValueError("audio_mode must be host-output, silent or test-probe")
        configure_audio = self.api.cody_host_audio_configure
        configure_audio.argtypes, configure_audio.restype = [ctypes.c_uint32], ctypes.c_int
        checked("host audio configuration", configure_audio(audio_modes[audio_mode]))
        wifi_modes = {None: 0, "secured": 1, "empty": 2, "timeout": 3}
        pairing_modes = {None: 0, "ready": 1, "error": 2, "timeout": 3}
        codex_modes = {None: 0, "pairing": 1, "error": 2}
        if (not isinstance(simulated_wifi_scenario, (str, type(None))) or simulated_wifi_scenario not in wifi_modes or
                not isinstance(simulated_pairing, (str, type(None))) or simulated_pairing not in pairing_modes or
                not isinstance(simulated_codex, (str, type(None))) or simulated_codex not in codex_modes or
                (simulated_open_wifi and simulated_wifi_scenario is not None)):
            raise ValueError("Invalid or conflicting simulated connectivity scenario")
        if (not isinstance(snapshot, dict) or
                type(initial_brightness) is not int or not 0 <= initial_brightness <= 100 or
                type(panel_enabled) is not bool or type(reset_reason) is not int or not 0 <= reset_reason <= 10 or
                type(use_pc_lan) is not bool or type(simulated_open_wifi) is not bool or type(use_pc_clock) is not bool or
                (simulated_battery_percent is not None and
                 (type(simulated_battery_percent) is not int or not 0 <= simulated_battery_percent <= 100))):
            raise ValueError("Invalid explicit simulated panel or reset state")
        if deterministic_clock:
            initialize_clock = self.api.cody_host_studio_clock_initialize
            initialize_clock.argtypes, initialize_clock.restype = [], ctypes.c_int
            checked("Studio static clock initialization", initialize_clock())
        now_us = self.monotonic_us()
        if now_us <= 0:
            now_us = self.monotonic_us()
        if now_us <= 0:
            raise RuntimeError("Native monotonic clock has not advanced")
        synthetic = deepcopy(snapshot)
        if use_pc_lan:
            try:
                address = host_lan_ipv4()
            except (OSError, subprocess.SubprocessError, ValueError) as error:
                # Keep the radio observation unavailable when the PC state cannot
                # be read. Never expose the address or command output in logs.
                print(f"SIMULATED: PC LAN state unavailable ({type(error).__name__})", file=sys.stderr)
            else:
                radios = synthetic.setdefault("radios", {})
                if not isinstance(radios, dict) or "wifi" in radios:
                    raise ValueError("PC LAN scenario cannot overwrite an explicit simulated Wi-Fi observation")
                connected = address is not None
                radios["wifi"] = {
                    "health": "ready", "last_error": 0, "data_valid": True, "data_stale": False,
                    "last_attempt_at_us": now_us, "last_success_at_us": now_us,
                    "connection_state_valid": True, "started": True, "connected": connected,
                    "ip_acquired": connected, "ssid": "SIMULATED-PC-LAN" if connected else "",
                    "ip_address": address or "",
                }
        self.snapshot = NativeSnapshot(synthetic, now_us, driver="graphics-device")
        self.report = BootstrapReport()
        bootstrap = self.api.cody_host_device_bootstrap
        bootstrap.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p,
                              ctypes.c_int64, ctypes.c_int, ctypes.c_uint8, ctypes.c_int,
                              ctypes.POINTER(BootstrapReport)]
        bootstrap.restype = ctypes.c_int
        # Paths are converted to UTF-8; the C++ boundary validates containment.
        root = str(ROOT.resolve()).encode("utf-8")
        path = str(Path(os.fspath(settings_path)).resolve()).encode("utf-8")
        checked("device bootstrap", bootstrap(root, path, self.snapshot.handle,
                                               now_us, initial_brightness, int(panel_enabled), reset_reason,
                                               ctypes.byref(self.report)))
        if simulated_open_wifi:
            seed_wifi = self.api.cody_host_wifi_seed_open_network
            seed_wifi.argtypes, seed_wifi.restype = [], ctypes.c_int
            checked("simulated Wi-Fi scan seed", seed_wifi())
        if simulated_wifi_scenario is not None:
            seed_wifi = self.api.cody_host_wifi_seed_scenario
            seed_wifi.argtypes, seed_wifi.restype = [ctypes.c_int], ctypes.c_int
            checked("simulated Wi-Fi scenario", seed_wifi(wifi_modes[simulated_wifi_scenario]))
        if simulated_pairing is not None:
            seed_pairing = self.api.cody_host_pairing_seed_scenario
            seed_pairing.argtypes, seed_pairing.restype = [ctypes.c_int], ctypes.c_int
            checked("simulated BLE pairing scenario", seed_pairing(pairing_modes[simulated_pairing]))
        if simulated_codex is not None:
            seed_codex = self.api.cody_host_codex_simulation_configure
            seed_codex.argtypes, seed_codex.restype = [ctypes.c_int], ctypes.c_int
            checked("simulated Codex scenario", seed_codex(codex_modes[simulated_codex]))
        self.simulated_battery_percent = simulated_battery_percent
        self.simulated_clock_enabled = use_pc_clock
        self.simulated_wifi_signal = None
        self.last_simulated_refresh_us = 0
        if use_pc_clock:
            offset = datetime.now().astimezone().utcoffset()
            if offset is None:
                raise RuntimeError("SIMULATED PC timezone offset unavailable")
            sync = self.api.cody_host_pc_clock_init
            sync.argtypes, sync.restype = [ctypes.c_int64, ctypes.c_int], ctypes.c_int
            checked("simulated PC time", sync(int(time.time()), int(offset.total_seconds() // 60)))
        self.refresh_simulated_sensors()

    # Refresh explicit simulated sensors and the firmware clock on the host cadence.
    def refresh_simulated_sensors(self):
        if self.simulated_battery_percent is None and not self.simulated_clock_enabled and self.simulated_wifi_signal is None:
            return
        now_us = self.monotonic_us()
        if self.last_simulated_refresh_us and now_us - self.last_simulated_refresh_us < 1_000_000:
            return
        if self.simulated_battery_percent is not None:
            battery = self.api.cody_host_simulated_battery_refresh
            battery.argtypes, battery.restype = [ctypes.c_int], ctypes.c_int
            checked("simulated battery", battery(self.simulated_battery_percent))
        if self.simulated_clock_enabled:
            clock = self.api.cody_host_pc_clock_refresh
            clock.argtypes, clock.restype = [], ctypes.c_int
            checked("simulated clock refresh", clock())
        if self.simulated_wifi_signal is not None:
            signal = self.api.cody_host_simulated_wifi_signal_refresh
            signal.argtypes, signal.restype = [ctypes.c_char_p, ctypes.c_int], ctypes.c_int
            ssid, rssi = self.simulated_wifi_signal
            checked("simulated Wi-Fi signal refresh", signal(ssid.encode("ascii"), rssi))
        self.last_simulated_refresh_us = now_us

    # No parameters: reads the native owner's scenario-time clock, not the PC wall clock.
    def monotonic_us(self):
        function = self.api.cody_host_monotonic_us
        function.argtypes, function.restype = [], ctypes.c_int64
        return function()

    def seed_studio_voice_stopped(self):
        """Publish the unavailable host AFE and stopped input in an isolated replay."""
        if not self.deterministic_clock:
            raise ValueError("Studio voice seeding requires an isolated static Studio child")
        function = self.api.cody_host_studio_seed_voice_stopped
        function.argtypes, function.restype = [], ctypes.c_int
        checked("Studio stopped voice observation", function())

    def seed_studio_boot_replay(self):
        """Seed a dedicated replay settings store before starting the device owner."""
        if not self.deterministic_clock:
            raise ValueError("Boot replay seeding requires an isolated static Studio child")
        function = self.api.cody_host_studio_seed_boot_replay
        function.argtypes, function.restype = [], ctypes.c_int
        checked("Studio boot replay settings", function())

    def verify_studio_boot_replay(self):
        """Verify the firmware owner's loaded boot replay settings after startup."""
        function = self.api.cody_host_studio_verify_boot_replay
        function.argtypes, function.restype = [], ctypes.c_int
        checked("Studio boot replay settings readback", function())

    # No parameters: starts the real Cody runtime; the caller owns boot sequencing.
    # Raises the unchanged device owner error when initialization fails.
    def start(self):
        function = self.api.cody_host_device_start
        function.argtypes, function.restype = [], ctypes.c_int
        checked("device start", function())

    # No parameters: completes the original UI worker's boot handoff.
    # Raises the original not-ready or startup error; callers decide when to retry.
    def boot_completed(self):
        if self.deterministic_clock:
            raise ValueError("Static Studio sessions use direct preparation instead of the boot timeline")
        function = self.api.cody_host_device_ready
        function.argtypes, function.restype = [], ctypes.c_int
        checked("device boot handoff", function())

    # target: Qualified native semantic identity from the executable Studio catalog.
    # scenario_revision: Exact SHA-256 identity of the complete validated scenario.
    # at_ms: Controlled static sample zero; source animation samples stay in page.sample_at_ms.
    # settings: Explicit synthetic panel settings, applied through their native owners.
    # page: Bounded source-specific data or draft, validated by the shared scenario contract.
    # services: Explicit synthetic service responses; never a real account or network request.
    # Raises schema, ABI, provenance, owner preparation, frame or deadline errors.
    def prepare_studio_case(self, target, scenario_revision, at_ms=0, *, settings=None, page=None, services=None):
        from cody_studio_catalog import load_catalog
        from cody_studio_scenario import default_scenario, validate_scenario

        if not self.deterministic_clock:
            raise ValueError("Direct Studio preparation needs a new deterministic Studio session")
        if (not isinstance(scenario_revision, str) or len(scenario_revision) != 64 or
                any(character not in "0123456789abcdef" for character in scenario_revision)):
            raise ValueError("scenario_revision must be one lowercase SHA-256 identity")
        if type(at_ms) is not int or at_ms != 0:
            raise ValueError("Studio pilot cases support only controlled static at_ms=0")
        entries = [entry for entry in load_catalog() if entry.get("native_target") == target]
        if len(entries) != 1:
            raise ValueError(f"Unknown or ambiguous native Studio target: {target!r}")
        entry = entries[0]
        value = default_scenario(entry["id"])
        if settings is not None:
            value["common"]["settings"] = deepcopy(settings)
        if page is not None:
            value["page"] = deepcopy(page)
        if services is not None:
            value["services"] = deepcopy(services)
        value = validate_scenario(value)
        settings, page, services = value["common"]["settings"], value["page"], value["services"]
        if settings.get("sounds_enabled", False):
            raise ValueError("Static Studio cannot play sound with a frozen clock; use an isolated live session")
        request = StudioCase()
        request.abi, request.size = 1, ctypes.sizeof(StudioCase)
        request.identity.abi = 2
        request.identity.target = target.encode("ascii")
        request.identity.scenario_revision = scenario_revision.encode("ascii")
        request.identity.at_ms = at_ms
        request.brightness_percent = settings["brightness_percent"]
        request.auto_rotate = int(settings["auto_rotate"])
        request.orientation_degrees = settings["orientation_degrees"]
        request.sounds_enabled = int(settings.get("sounds_enabled", False))
        request.playback_volume = settings.get("playback_volume", 90)
        request.quiet_start_minute, request.quiet_end_minute = 1320, 420
        request.quiet_edit_start, request.quiet_draft_minute = 1, 1320
        if target in ("cody.boot.branding", "cody.boot.birth"):
            request.identity.source_animation_sample_ms = page["sample_at_ms"]
        elif target == "cody.settings.quiet-edit":
            request.quiet_enabled = int(page["quiet_hours_enabled"])
            request.quiet_start_minute = page["quiet_start_minute"]
            request.quiet_end_minute = page["quiet_end_minute"]
            request.quiet_edit_start = int(page["endpoint"] == "start")
            request.quiet_draft_minute = page["minute"]
        elif target in ("cody.dashboard.reset", "cody.dashboard.limits"):
            request.codex_link_outcome = services["codex_link"]["outcome"].encode("ascii")
            weekly = page["weekly"]
            request.weekly_reset_unix_seconds = weekly["reset_unix_seconds"]
            request.weekly_remaining_basis_points = weekly["remaining_basis_points"]
            request.weekly_source_age_ms = weekly["source_age_at_receive_ms"]
            if target == "cody.dashboard.limits":
                account = page["account"]
                request.short_reset_unix_seconds = account["short_reset_unix_seconds"]
                request.short_remaining_basis_points = account["short_remaining_basis_points"]
                request.short_window_minutes = account["short_window_minutes"]
                request.short_source_age_ms = account["source_age_at_receive_ms"]
        elif target == "cody.provisioning.error":
            request.provisioning_outcome = services["provisioning"]["outcome"].encode("ascii")
        elif target == "cody.codex.calls":
            domain = services["domain"]
            request.identity.source_id = int(page["source_id"], 16)
            request.identity.chat_id = int(page["chat_id"], 16)
            request.identity.turn_id = int(page["turn_id"], 16)
            request.domain_source_id = int(domain["source_id"], 16)
            request.domain_chat_id = int(domain["chat_id"], 16)
            request.domain_turn_id = int(domain["turn_id"], 16)
            request.domain_generation = domain["generation"]
            request.domain_chat_title = domain["chat_title"].encode("ascii")
            request.domain_execution = domain["execution"].encode("ascii")
            request.domain_call_count = len(domain["calls"])
            for index, call in enumerate(domain["calls"]):
                row = request.domain_calls[index]
                row.id, row.turn_id = int(call["id"], 16), int(call["turn_id"], 16)
                for name in ("tool_name", "label", "category", "state", "outcome"):
                    setattr(row, name, call[name].encode("ascii"))
        api = load_runtime_library("graphics-device")
        prepare = api.cody_host_studio_prepare_case
        prepare.argtypes = [ctypes.POINTER(StudioCase)]
        prepare.restype = ctypes.c_int
        checked(f"Studio {entry['inventory_id']} preparation", prepare(ctypes.byref(request)))
        image, identity = completed_studio_frame()
        if (identity["target"] != target or identity["scenario_revision"] != scenario_revision or
                identity["at_ms"] != at_ms or identity["rendering_mode"] != entry["rendering_mode"]):
            raise ValueError("Native completed frame does not match the requested Studio preparation")
        return image, identity

    def start_studio_live(self, sounds_enabled, volume):
        """Start a one-way, live-clock animation in an isolated Studio child."""
        if not self.deterministic_clock or type(sounds_enabled) is not bool or type(volume) is not int or not 0 <= volume <= 100:
            raise ValueError("Live Studio requires a prepared static child, boolean Sounds and volume 0..100")
        start = self.api.cody_host_studio_start_live
        start.argtypes, start.restype = [ctypes.c_uint32, ctypes.c_uint32], ctypes.c_int
        checked("Studio live start", start(int(sounds_enabled), volume))

    def studio_live_status(self):
        """Observe display-owner completion and actual startup cue terminal states."""
        query = self.api.cody_host_studio_live_status
        query.argtypes, query.restype = [ctypes.POINTER(StudioLiveStatus), ctypes.c_size_t], ctypes.c_int
        result = StudioLiveStatus()
        checked("Studio live status", query(ctypes.byref(result), ctypes.sizeof(result)))
        if (result.abi != 1 or result.animation_complete not in (0, 1) or result.audio_complete not in (0, 1)
                or result.cue_count > 8):
            raise ValueError("Invalid native Studio live status")
        valid = {"idle", "dispatched", "playing", "completed", "dropped-busy", "dropped-cooldown",
                 "suppressed", "expired", "failed", "unavailable"}
        cues = []
        for cue in result.cues[:result.cue_count]:
            state = cue.state.decode("ascii")
            if state not in valid:
                raise ValueError("Invalid native Studio live cue state")
            cues.append({"id": cue.id, "sequence": cue.sequence, "state": state, "error": cue.error})
        return {"animation_complete": bool(result.animation_complete), "audio_complete": bool(result.audio_complete),
                "cue_count": result.cue_count, "cues": cues, "worker_error": result.worker_error}

    def audio_status(self):
        """Return the configured host output mode and the original backend result."""
        query = self.api.cody_host_audio_status
        query.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
                          ctypes.POINTER(ctypes.c_int)]
        query.restype = ctypes.c_int
        mode, attempted, result = ctypes.c_uint32(), ctypes.c_uint32(), ctypes.c_int()
        checked("host audio status", query(ctypes.byref(mode), ctypes.byref(attempted), ctypes.byref(result)))
        if mode.value not in (0, 1, 2) or attempted.value not in (0, 1):
            raise ValueError("Native host audio status returned an invalid value")
        return {"mode": ("host-output", "silent", "test-probe")[mode.value],
                "attempted": bool(attempted.value), "backend_result": result.value}

    # name: Stable host capability name accepted by the compiled native profile.
    # Raises native errors for unavailable or unknown capabilities without guessing.
    def studio_has_capability(self, name):
        if not isinstance(name, str) or not name.isascii() or not name:
            raise ValueError("A native Studio capability requires an ASCII name")
        query = load_runtime_library("graphics-device").cody_host_studio_has_capability
        query.argtypes, query.restype = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint8)], ctypes.c_int
        available = ctypes.c_uint8(0xff)
        checked("Studio capability", query(name.encode("ascii"), ctypes.byref(available)))
        if available.value not in (0, 1):
            raise ValueError("Native Studio capability returned an invalid boolean")
        return bool(available.value)

    # target: One native animation target from the executable Studio catalog.
    # Raises native, malformed metadata or unsupported-animation errors without inventing keyframes.
    def studio_animation_keyframes(self, target):
        if target not in ("cody.boot.branding", "cody.boot.birth"):
            raise ValueError(f"Unsupported native Studio source animation: {target!r}")
        native = load_runtime_library("graphics-device").cody_host_studio_animation_frame
        native.argtypes = [ctypes.c_char_p, ctypes.c_uint32, *([ctypes.POINTER(ctypes.c_uint32)] * 4)]
        native.restype = ctypes.c_int
        values = [ctypes.c_uint32() for _ in range(4)]

        # index: Authored native frame index in the compiled animation.
        # Raises the original native error for an invalid index or missing animation.
        def frame(index):
            checked("Studio animation frame metadata", native(target.encode("ascii"), index,
                    *(ctypes.byref(value) for value in values)))
            return tuple(value.value for value in values)

        count, _, _, duration = frame(0)
        if not 1 <= count <= 10000 or duration <= 0:
            raise ValueError("Invalid native Studio animation frame count or duration")
        starts, expected = [], 0
        for index in range(count):
            observed_count, start, length, cycle = frame(index)
            if observed_count != count or cycle != duration or start != expected or length <= 0:
                raise ValueError("Inconsistent native Studio animation intervals")
            starts.append(start)
            expected += length
        if expected != duration:
            raise ValueError("Native Studio animation intervals do not cover their duration")
        return duration, tuple(starts)

    # route_id: Named, explicitly supported owner-dispatched native gesture.
    # origin_revision: Exact validated source scenario identity of the retired origin frame.
    # destination_revision: Exact validated destination scenario identity for the next retired frame.
    # Raises validation, native gesture, receipt or atomic snapshot errors; failures never claim replay.
    def replay_studio_route(self, route_id, origin_revision, destination_revision):
        if not self.deterministic_clock:
            raise ValueError("Studio route replay requires a controlled static session")
        destinations = {"settings-menu-to-display": "cody.settings.display",
                        "display-to-settings-menu": "cody.settings.menu",
                        "guide-buttons-to-touch": "cody.guide.touch",
                        "guide-touch-to-buttons": "cody.guide.buttons",
                        "guide-touch-to-sofia": "cody.guide.sofia",
                        "guide-sofia-to-touch": "cody.guide.touch",
                        "guide-sofia-to-offline": "cody.guide.offline",
                        "guide-offline-to-sofia": "cody.guide.sofia",
                        "guide-offline-to-project": "cody.guide.project",
                        "guide-project-to-offline": "cody.guide.offline",
                        "guide-project-to-thanks": "cody.guide.thanks",
                        "guide-thanks-to-project": "cody.guide.project"}
        if route_id not in destinations:
            raise ValueError(f"Unsupported native Studio route: {route_id!r}")
        for revision in (origin_revision, destination_revision):
            if (not isinstance(revision, str) or len(revision) != 64 or
                    any(character not in "0123456789abcdef" for character in revision)):
                raise ValueError("Route scenario revisions must be lowercase SHA-256 identities")
        request = StudioRouteRequest()
        request.abi, request.size = 1, ctypes.sizeof(StudioRouteRequest)
        request.route_id = route_id.encode("ascii")
        request.origin_scenario_revision = origin_revision.encode("ascii")
        request.destination_scenario_revision = destination_revision.encode("ascii")
        replay = load_runtime_library("graphics-device").cody_host_studio_replay_route
        replay.argtypes, replay.restype = [ctypes.POINTER(StudioRouteRequest)], ctypes.c_int
        checked("Studio route replay", replay(ctypes.byref(request)))
        image, identity, receipt = completed_studio_route_frame()
        if (identity["target"] != destinations[route_id] or identity["scenario_revision"] != destination_revision or
                receipt["route_id"] != route_id or receipt["origin_scenario_revision"] != origin_revision):
            raise ValueError("Native observed route does not match the requested source and destination")
        return image, identity, receipt

    # scenario_revision: Immutable identity of this isolated worker scenario.
    # elapsed_ms: Forward-only scenario instant, with no clock rewind.
    # Raises native, identity, time or frame errors without synthesizing a frame.
    def replay_studio_worker(self, scenario_revision, elapsed_ms):
        if not self.deterministic_clock:
            raise ValueError("Worker replay requires an isolated deterministic Studio session")
        if (not isinstance(scenario_revision, str) or len(scenario_revision) != 64 or
                any(character not in "0123456789abcdef" for character in scenario_revision)):
            raise ValueError("Worker scenario revision must be a lowercase SHA-256 identity")
        if type(elapsed_ms) is not int or not 0 <= elapsed_ms < 7000:
            raise ValueError("Worker replay time must be an integer between 0 and 6999 ms")
        request = StudioWorkerRequest()
        request.abi, request.size, request.requested_ms = 1, ctypes.sizeof(StudioWorkerRequest), elapsed_ms
        request.scenario_revision = scenario_revision.encode("ascii")
        replay = load_runtime_library("graphics-device").cody_host_studio_replay_worker
        replay.argtypes, replay.restype = [ctypes.POINTER(StudioWorkerRequest)], ctypes.c_int
        checked("Studio worker replay", replay(ctypes.byref(request)))
        image, identity, receipt = completed_studio_worker_frame()
        if receipt["scenario_revision"] != scenario_revision or receipt["requested_ms"] != elapsed_ms:
            raise ValueError("Native display-worker acknowledgement does not match the request")
        return image, identity, receipt

    # pressed: Boolean contact state.
    # x: Logical unrotated horizontal pixel.
    # y: Logical unrotated vertical pixel.
    # Raises the native input admission error; no synthetic event is added on failure.
    def touch(self, pressed, x, y):
        if type(pressed) is not bool or type(x) is not int or type(y) is not int or not (0 <= x < 466 and 0 <= y < 466):
            raise ValueError("Invalid simulated touch")
        function = self.api.cody_host_touch_event
        function.argtypes = [ctypes.c_uint8, ctypes.c_uint16, ctypes.c_uint16]
        function.restype = ctypes.c_int
        checked("device touch", function(int(pressed), x, y))

    # held: True for the device's long BOOT action, false for its short action.
    # Raises the native callback admission error if no callback is registered.
    def boot(self, held=False):
        if type(held) is not bool:
            raise ValueError("BOOT hold selector must be boolean")
        function = self.api.cody_host_boot_event
        function.argtypes, function.restype = [ctypes.c_uint8], ctypes.c_int
        checked("device BOOT", function(int(held)))

    # No parameters: copies only the most recent successfully retired device frame.
    # Raises the native unavailable error until an actual frame has retired.
    def frame(self):
        self.refresh_simulated_sensors()
        return completed_frame(self.api)

    # Read editable simulated values from the native owner, preserving unavailable fields.
    # Raises the native snapshot error without substituting a successful reading.
    def read_snapshot(self):
        percent = ctypes.c_int()
        read = self.api.cody_host_simulated_battery_read
        read.argtypes, read.restype = [ctypes.POINTER(ctypes.c_int)], ctypes.c_int
        checked("simulated battery read", read(ctypes.byref(percent)))
        connected = ctypes.c_int()
        ssid = ctypes.create_string_buffer(33)
        rssi = ctypes.c_int()
        wifi = self.api.cody_host_simulated_wifi_read
        wifi.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_int)]
        wifi.restype = ctypes.c_int
        checked("simulated Wi-Fi read", wifi(ctypes.byref(connected), ssid, len(ssid), ctypes.byref(rssi)))
        epoch, offset = ctypes.c_int64(), ctypes.c_int()
        clock = self.api.cody_host_simulated_clock_read
        clock.argtypes, clock.restype = [ctypes.POINTER(ctypes.c_int64), ctypes.POINTER(ctypes.c_int)], ctypes.c_int
        checked("simulated clock read", clock(ctypes.byref(epoch), ctypes.byref(offset)))
        clock_available = epoch.value > 0 and -840 <= offset.value <= 840
        return {"battery": {"level_percent": percent.value if percent.value >= 0 else None},
                "network": {"connected": bool(connected.value) if connected.value >= 0 else None,
                            "ssid": ssid.value.decode("ascii"), "rssi": rssi.value if rssi.value >= -100 else None},
                "clock": {"epoch_seconds": epoch.value if clock_available else None,
                          "utc_offset_minutes": offset.value if clock_available else None}}

    # snapshot: Editable simulated projection; null measurements remain unchanged.
    # Raises a validation or original native error; a service failure can leave earlier services applied.
    def apply_snapshot(self, snapshot):
        if not isinstance(snapshot, dict) or set(snapshot) != {"battery", "network", "clock"}:
            raise ValueError("Expected simulated battery, network and clock values")
        battery, network, clock = snapshot["battery"], snapshot["network"], snapshot["clock"]
        if not isinstance(battery, dict) or set(battery) != {"level_percent"}:
            raise ValueError("Expected battery.level_percent")
        percent = battery["level_percent"]
        if percent is not None and (type(percent) is not int or not 0 <= percent <= 100):
            raise ValueError("battery.level_percent must be null or an integer from 0 to 100")
        if not isinstance(network, dict) or set(network) != {"connected", "ssid", "rssi"}:
            raise ValueError("Expected network.connected, ssid and rssi")
        connected, ssid, rssi = network["connected"], network["ssid"], network["rssi"]
        if connected is not None and type(connected) is not bool:
            raise ValueError("network.connected must be true, false or null")
        if not isinstance(ssid, str) or len(ssid) > 32 or any(not 32 <= ord(char) <= 126 for char in ssid):
            raise ValueError("network.ssid must contain at most 32 printable ASCII characters")
        if rssi is not None and (type(rssi) is not int or not -100 <= rssi <= -1):
            raise ValueError("network.rssi must be null or -100 to -1 dBm")
        if connected and not ssid:
            raise ValueError("A connected simulated network needs an SSID")
        if not isinstance(clock, dict) or set(clock) != {"epoch_seconds", "utc_offset_minutes"}:
            raise ValueError("Expected clock.epoch_seconds and utc_offset_minutes")
        epoch, offset = clock["epoch_seconds"], clock["utc_offset_minutes"]
        if (epoch is None) != (offset is None):
            raise ValueError("Clock time and offset must both be present or both null")
        if epoch is not None and (type(epoch) is not int or not 1704067200 <= epoch <= 4102444799 or
                                  type(offset) is not int or not -840 <= offset <= 840):
            raise ValueError("Clock time must be in 2024–2099 and UTC offset between -840 and 840 minutes")
        current = self.read_snapshot()
        if epoch is not None:
            sync = self.api.cody_host_pc_clock_init
            sync.argtypes, sync.restype = [ctypes.c_int64, ctypes.c_int], ctypes.c_int
            checked("simulated clock apply", sync(epoch, offset))
            self.simulated_clock_enabled = not self.deterministic_clock
        if connected is not None and network != current["network"]:
            wifi = self.api.cody_host_simulated_wifi_apply
            wifi.argtypes, wifi.restype = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int], ctypes.c_int
            checked("simulated Wi-Fi apply", wifi(int(connected), ssid.encode("ascii"), rssi if rssi is not None else -101))
            self.simulated_wifi_signal = (ssid, rssi) if connected and rssi is not None else None
        if percent is not None:
            refresh = self.api.cody_host_simulated_battery_refresh
            refresh.argtypes, refresh.restype = [ctypes.c_int], ctypes.c_int
            checked("simulated battery apply", refresh(percent))
            self.simulated_battery_percent = percent
        self.last_simulated_refresh_us = self.monotonic_us()
