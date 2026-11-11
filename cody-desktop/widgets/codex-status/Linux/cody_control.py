"""Native companion controls using the shared public CCP control client."""
from __future__ import annotations

import threading
import time
import re
from gi.repository import GLib, Gtk


# value: Plain UI text; all service payloads remain outside markup.
# Returns a wrapping native label.
def label(value):
    return Gtk.Label(label=value, wrap=True, xalign=0)


class CodyControlWindow(Gtk.Window):
    # parent: Existing account widget, retained as this utility window's owner.
    # invoke: Shared bounded backend request function; never a raw device transport.
    # settings_changed: Reloads widget settings after an explicit launcher save.
    def __init__(self, parent, invoke, settings_changed):
        super().__init__(title="Cody companion", transient_for=parent, modal=True,
                         default_width=570, default_height=760)
        self.invoke = invoke
        self.settings_changed = settings_changed
        self.closed = False
        self.busy = False
        self.clock_loaded = False
        self.clock_offset = None
        self.settings = {}
        self.physical_devices = []
        self.observed_status = None
        self.status_received_at = 0.0
        self.commands = []
        self.source = label("Source unavailable")
        self.source.add_css_class("title")
        self.status = label("Reading companion configuration…")
        self.result = label("")
        self.content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        for setter in (self.content.set_margin_start, self.content.set_margin_end,
                       self.content.set_margin_top, self.content.set_margin_bottom):
            setter(24)
        self.content.append(self.source)
        self.content.append(self.status)
        self.content.append(label("Cody follows connected Codex sessions and works locally when Codex is unavailable."))
        self.content.append(self.row(self.command("Refresh", self.refresh),
                                     self.command("Start companion", lambda: self.control("bridge/start", {})),
                                     self.command("Stop companion", lambda: self.control("bridge/stop", {}))))
        self.content.append(label("Keep this widget open or hidden to tray while Codex is closed. Exiting the widget can stop its companion service."))
        setup = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.interpreter = self.entry(setup, "Python executable")
        self.launcher = self.entry(setup, "Companion service launcher (ccp_server.py)")
        setup.append(self.command("Save launcher", self.save_launcher))
        self.setup = Gtk.Expander(label="Companion service setup", child=setup)
        self.content.append(self.setup)

        self.content.append(label("Physical Cody"))
        self.device_list = Gtk.StringList.new([])
        self.devices = Gtk.DropDown(model=self.device_list, selected=Gtk.INVALID_LIST_POSITION)
        self.devices.connect("notify::selected", self.selection_changed)
        self.content.append(self.devices)
        self.content.append(self.row(self.command("Find Cody", lambda: self.control("device/discover", {"include_simulator": False})),
                                     self.command("Read preferences", self.read_preferences),
                                     self.command("Normal screen", lambda: self.device_control("device/normal", {}))))
        self.pairing = self.entry(self.content, "Six-character code shown on Cody", 6)
        self.content.append(self.command("Pair this code", self.pair))
        self.preferred_name = self.entry(self.content, "Preferred name", 24)
        self.content.append(self.command("Save name", lambda: self.device_control("device/set-name", {"preferred_name": self.preferred_name.get_text()})))
        self.utc_offset = self.entry(self.content, "UTC offset in minutes (for example +330)", 4)
        self.quiet = Gtk.CheckButton(label="Quiet hours")
        self.content.append(self.quiet)
        self.quiet_start = self.entry(self.content, "Start (HH:MM)", 5)
        self.quiet_end = self.entry(self.content, "End (HH:MM)", 5)
        self.content.append(self.command("Save clock preferences", self.save_clock))

        self.content.append(self.result)
        self.set_child(Gtk.ScrolledWindow(child=self.content, hscrollbar_policy=Gtk.PolicyType.NEVER))
        self.connect("close-request", self.on_close)
        self.poll = GLib.timeout_add_seconds(5, self.refresh)
        self.run(lambda: self.invoke("settings"), self.loaded)

    # container: Native vertical form receiving a label and entry.
    # caption: Visible English field name.
    # maximum: Explicit input limit; zero retains GTK's default.
    def entry(self, container, caption, maximum=0):
        container.append(label(caption))
        field = Gtk.Entry(max_length=maximum)
        container.append(field)
        return field

    # buttons: Parallel explicit actions displayed in one native row.
    def row(self, *buttons):
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        for button in buttons:
            row.append(button)
        return row

    # caption: The explicit user action; clicks alone admit mutations.
    # action: UI-thread function that snapshots fields before starting background IO.
    def command(self, caption, action):
        button = Gtk.Button(label=caption)
        button.connect("clicked", lambda *_: self.dispatch(action))
        self.commands.append(button)
        return button

    # action: One user action; form errors remain visible without a worker or retry.
    def dispatch(self, action):
        if self.busy or self.closed:
            return
        try:
            action()
        except (ValueError, RuntimeError, KeyError, TypeError) as error:
            self.result.set_text(str(error))

    # work: Background-only backend operation; it must not access GTK widgets.
    # receive: UI-thread consumer of a successfully observed result.
    # status_read: Clears connection claims on a failed current-status read.
    def run(self, work, receive, status_read=False):
        if self.busy or self.closed:
            return
        self.busy = True
        for button in self.commands:
            button.set_sensitive(False)

        # No parameters: execute one bounded request without touching native UI state.
        def worker():
            try:
                value, error = work(), None
            except (OSError, RuntimeError, ValueError, KeyError, TypeError) as failure:
                value, error = None, str(failure)

            # No parameters: consume the result on GTK's main context.
            def complete():
                self.busy = False
                if self.closed:
                    return False
                for button in self.commands:
                    button.set_sensitive(True)
                if error is not None:
                    if status_read:
                        self.source.set_text("Source unavailable")
                        self.status.set_text("Companion status could not be read.")
                        self.observed_status = None
                        self.physical_devices = []
                        self.device_list.splice(0, self.device_list.get_n_items(), [])
                        self.clock_loaded = False
                    self.result.set_text(error)
                else:
                    try:
                        receive(value)
                    except (ValueError, RuntimeError, KeyError, TypeError) as failure:
                        self.result.set_text(str(failure))
                return False

            GLib.idle_add(complete)
        threading.Thread(target=worker, daemon=True).start()

    # values: Current persisted widget settings; no service is implicitly started.
    def loaded(self, values):
        self.settings = values
        bridge = values.get("codyBridge", {})
        self.interpreter.set_text(bridge.get("pythonExecutable", "python3"))
        self.launcher.set_text(bridge.get("controlScriptPath", ""))
        self.setup.set_expanded(not bool(self.launcher.get_text()))
        self.refresh()

    # No parameters: observe bridge state independently of the account polling loop.
    def refresh(self):
        if self.closed:
            return False
        self.render_source()
        self.run(lambda: self.invoke("cody-status"), self.received_status, status_read=True)
        return True

    # observed: Typed shared-backend status with original source freshness.
    def received_status(self, observed):
        selected = self.selected_device(required=False)
        self.observed_status = observed
        self.status_received_at = time.monotonic()
        physical = [item for item in observed["devices"] if not item["simulated"]]
        identities = [item["deviceId"] for item in physical]
        old_identities = [item["deviceId"] for item in self.physical_devices]
        self.physical_devices = physical
        names = [item["deviceId"] + (" · Connected" if item["connected"] else " · Unavailable") for item in physical]
        old_names = [self.device_list.get_string(index) for index in range(self.device_list.get_n_items())]
        if names != old_names:
            self.device_list.splice(0, self.device_list.get_n_items(), names)
            self.devices.set_selected(identities.index(selected) if selected in identities else Gtk.INVALID_LIST_POSITION)
        if identities != old_identities:
            self.clock_loaded = False
        self.render_source()

    # No parameters: bind source evidence to the explicitly selected device, never its list position.
    def render_source(self):
        observed = self.observed_status
        if observed is None:
            return
        identity = self.selected_device(required=False)
        device = next((item for item in self.physical_devices if item["deviceId"] == identity), None)
        source = device["providerSource"] if device is not None else observed["source"]
        source_provider = source["provider"].lower()
        provider = "Local companion" if source_provider == "local" else source["provider"]
        age = source.get("ageMilliseconds")
        if age is not None:
            age += int((time.monotonic() - self.status_received_at) * 1000)
        missing_age = source_provider == "codex" and age is None
        state = "STALE" if age is not None and age >= 120000 else "UNAVAILABLE" if missing_age else source["state"].lower()
        self.source.set_text(provider + " · " + state if observed["running"] else "Companion service stopped")
        if observed["running"]:
            detail = "Source for Cody " + identity + ". " if device is not None else "Service source. "
            detail += source["detail"] + " "
            detail += "Observed " + str(age // 1000) + "s ago." if age is not None else "No source timestamp available."
        else:
            detail = "Start the companion service to connect Cody."
        self.status.set_text(detail)

    # required: True for a command requiring an explicit physical-device selection.
    # Raises ValueError rather than selecting a different device automatically.
    def selected_device(self, required=True):
        position = self.devices.get_selected()
        if position < len(self.physical_devices):
            return self.physical_devices[position]["deviceId"]
        if required:
            raise ValueError("Select the intended physical Cody first.")
        return None

    # arguments: GTK selection callback metadata; no device operation is issued.
    def selection_changed(self, *arguments):
        self.clock_loaded = False
        if hasattr(self, "preferred_name"):
            self.preferred_name.set_text("")
            self.utc_offset.set_text("")
            self.clock_offset = None
        self.render_source()

    # No parameters: save launcher fields only, preserving current account preferences.
    def save_launcher(self):
        values = dict(self.settings)
        values["codyBridge"] = {"pythonExecutable": self.interpreter.get_text().strip(),
                                "controlScriptPath": self.launcher.get_text().strip()}

        # result: Successful persistence result; no connection is inferred.
        def saved(result):
            self.settings = values
            self.settings_changed()
            self.result.set_text("Companion launcher saved.")
            self.refresh()
        self.run(lambda: self.invoke("save-settings", values), saved)

    # operation: Documented public command, never a shell command.
    # arguments: UI-thread snapshot; tokens are transported on stdin only.
    def control(self, operation, arguments):
        self.run(lambda: self.invoke("cody-control", {"operation": operation, "arguments": arguments}), self.control_complete)

    # response: Unmodified public service outcome; never includes submitted secrets.
    def control_complete(self, response):
        self.result.set_text(self.describe_result(response))
        self.refresh()

    # operation: Authenticated device command.
    # arguments: Explicit edited settings; absent fields retain device-owned values.
    def device_control(self, operation, arguments):
        payload = dict(arguments, device_id=self.selected_device())
        self.control(operation, payload)

    # No parameters: pairing consumes only the code entered for this click.
    def pair(self):
        code = self.pairing.get_text().strip()
        if len(code) != 6:
            raise ValueError("Enter the six-character code currently shown on Cody.")
        self.pairing.set_text("")
        self.control("device/pair", {"code": code})

    # No parameters: freeze selected identity across two bounded preference reads.
    def read_preferences(self):
        identity = self.selected_device()
        self.clock_loaded = False

        # No parameters: read both current native preferences, rejecting unavailable ACKs.
        def work():
            values = []
            for operation in ("device/read-name", "device/read-clock"):
                response = self.invoke("cody-control", {"operation": operation, "arguments": {"device_id": identity}})
                if response.get("status") != "applied" or response.get("error") != 0:
                    raise RuntimeError(self.describe_result(response))
                values.append(response)
            return values

        # values: Authenticated readbacks for the frozen selected device.
        def received(values):
            if self.selected_device(required=False) != identity:
                return
            name, clock = values
            self.preferred_name.set_text(name["preferred_name"])
            self.clock_offset = clock["utc_offset_minutes"]
            self.utc_offset.set_text(f"{self.clock_offset:+d}")
            self.quiet.set_active(clock["quiet_hours_enabled"])
            self.quiet_start.set_text(clock["quiet_hours_start"])
            self.quiet_end.set_text(clock["quiet_hours_end"])
            self.clock_loaded = True
            self.result.set_text("Preferences read from Cody.")
        self.run(work, received)

    # No parameters: apply the explicit form only after readback of this device's preferences.
    def save_clock(self):
        if not self.clock_loaded or self.clock_offset is None:
            raise ValueError("Read this Cody's preferences before editing its clock.")
        text = self.utc_offset.get_text().strip()
        if re.fullmatch(r"[+-]?[0-9]{1,3}", text) is None:
            raise ValueError("Enter a UTC offset in whole minutes from -840 to 840.")
        offset = int(text)
        if not -840 <= offset <= 840:
            raise ValueError("Enter a UTC offset in whole minutes from -840 to 840.")
        settings = {"quiet_hours_enabled": self.quiet.get_active(),
                    "quiet_hours_start": self.quiet_start.get_text().strip(),
                    "quiet_hours_end": self.quiet_end.get_text().strip()}
        if offset != self.clock_offset:
            settings["utc_offset_minutes"] = offset
        self.device_control("device/configure-clock", settings)

    # response: Public outcome metadata; never renders a request body or access token.
    @staticmethod
    def describe_result(response):
        if "status" in response:
            state, error = response["status"], response.get("error", "")
            if state == "applied" and error == 0:
                return "Cody acknowledged the change. Physical behavior has not been verified."
            detail = "Cody result: " + state + (" · " + str(error) if error != "" else "")
            if state in ("ambiguous", "unconfirmed"):
                detail += ". Read status before repeating the action."
            return detail
        if "count" in response:
            return "Pairing offers found: " + str(response["count"]) + ". Use the code on the intended physical Cody."
        if "device_id" in response:
            return "Pairing confirmed for Cody " + response["device_id"] + "."
        return "Companion service request completed. Refresh shows the current observed state."

    # arguments: GTK close callback metadata.
    # No exceptions: Cancels future polling; any in-flight bounded operation remains unconfirmed until readback.
    def on_close(self, *arguments):
        self.closed = True
        GLib.source_remove(self.poll)
        return False
