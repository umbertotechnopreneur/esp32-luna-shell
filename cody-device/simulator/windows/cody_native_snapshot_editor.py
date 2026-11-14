"""Edit explicitly simulated host inputs; device pixels remain native."""

from datetime import datetime, timedelta, timezone
import json
import tkinter as tk
from tkinter import filedialog, messagebox, ttk


class SnapshotEditor:
    """Edit, apply, save and load the simulator's supported input projection."""

    # parent: Existing native simulator window.
    # device: Native device adapter exposing read_snapshot and apply_snapshot.
    # Exceptions: Tk errors are propagated to the owning window.
    def __init__(self, parent, device):
        self.device = device
        self._baseline = None
        self._opened_file = False
        self.window = tk.Toplevel(parent)
        self.window.title("Snapshot editor — SIMULATED")
        self.window.resizable(False, False)
        frame = ttk.Frame(self.window, padding=12)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="SIMULATED — battery, network and clock").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 10))

        self.level = tk.StringVar()
        self.connected = tk.StringVar()
        self.ssid = tk.StringVar()
        self.rssi = tk.StringVar()
        self.local_time = tk.StringVar()
        self.offset = tk.StringVar()
        rows = (
            ("Battery (%)", ttk.Spinbox(frame, from_=0, to=100, textvariable=self.level, width=24)),
            ("Network", ttk.Combobox(frame, textvariable=self.connected, values=("Connected", "Disconnected", "Unavailable"), state="readonly", width=23)),
            ("Network name", ttk.Entry(frame, textvariable=self.ssid, width=26)),
            ("Signal (dBm)", ttk.Entry(frame, textvariable=self.rssi, width=26)),
            ("Local time", ttk.Entry(frame, textvariable=self.local_time, width=26)),
            ("UTC offset (minutes)", ttk.Entry(frame, textvariable=self.offset, width=26)),
        )
        for index, (label, control) in enumerate(rows, start=1):
            ttk.Label(frame, text=label).grid(row=index, column=0, sticky="w", padx=(0, 14), pady=3)
            control.grid(row=index, column=1, sticky="ew", pady=3)
        ttk.Label(frame, text="Time format: YYYY-MM-DD HH:MM:SS. Blank means unavailable.").grid(row=7, column=0, columnspan=2, sticky="w", pady=(6, 0))
        ttk.Label(frame, text="Network data is simulated and applies only to this simulator.").grid(row=8, column=0, columnspan=2, sticky="w")
        actions = ttk.Frame(frame)
        actions.grid(row=9, column=0, columnspan=2, sticky="w", pady=(12, 0))
        for label, command in (("Read current", self.read_current), ("Apply", self.apply),
                               ("Open…", self.open_file), ("Save as…", self.save_file)):
            ttk.Button(actions, text=label, command=command).pack(side="left", padx=(0, 6))
        self.status = tk.StringVar(value="Reading simulated inputs…")
        ttk.Label(frame, textvariable=self.status, wraplength=500).grid(row=10, column=0, columnspan=2, sticky="w", pady=(8, 0))
        self.window.after_idle(self.read_current)

    # text: User-entered integer, possibly empty when a field is unavailable.
    # name: Human-readable field name for errors.
    # allow_unavailable: Preserve an empty field as null when saving a projection.
    # Raises ValueError if the input is not a whole number.
    @staticmethod
    def _number(text, name, allow_unavailable):
        text = text.strip()
        if not text and allow_unavailable:
            return None
        try:
            return int(text)
        except ValueError:
            raise ValueError(f"{name} must be a whole number.") from None

    # allow_unavailable: Permit incomplete values in saved files, not on apply.
    # Returns the same projection shape accepted by the native adapter.
    # Raises ValueError or OverflowError on invalid data.
    def _value(self, *, allow_unavailable=False):
        level = self._number(self.level.get(), "Battery", allow_unavailable)
        rssi = self._number(self.rssi.get(), "Signal", True)
        offset = self._number(self.offset.get(), "UTC offset", allow_unavailable)
        if level is not None and not 0 <= level <= 100:
            raise ValueError("Battery must be from 0 to 100%.")
        if rssi is not None and not -100 <= rssi <= -1:
            raise ValueError("Signal must be from -100 to -1 dBm.")
        if offset is not None and not -840 <= offset <= 840:
            raise ValueError("UTC offset must be from -840 to 840 minutes.")
        states = {"Connected": True, "Disconnected": False, "Unavailable": None}
        if self.connected.get() not in states:
            raise ValueError("Select a network state.")
        connected = states[self.connected.get()]
        if connected is None and not allow_unavailable:
            raise ValueError("Select Connected or Disconnected before applying.")
        ssid = self.ssid.get().strip()
        if (ssid and (not ssid.isascii() or not ssid.isprintable() or len(ssid) > 32)) or (connected is True and not ssid):
            raise ValueError("A connected network needs a printable ASCII name (1–32 characters).")
        text = self.local_time.get().strip()
        if not text and allow_unavailable:
            if offset is not None:
                raise ValueError("Clear both local time and UTC offset to leave the clock unchanged.")
            epoch = None
        else:
            if offset is None:
                raise ValueError("Enter a UTC offset for the local time.")
            try:
                date = datetime.strptime(text, "%Y-%m-%d %H:%M:%S")
                epoch = int(date.replace(tzinfo=timezone(timedelta(minutes=offset))).timestamp())
            except (ValueError, OverflowError, OSError):
                raise ValueError("Enter a valid local time in YYYY-MM-DD HH:MM:SS format.") from None
            if not 1704067200 <= epoch <= 4102444799:
                raise ValueError("The simulated clock must be between 2024 and 2099 UTC.")
        return {"battery": {"level_percent": level}, "network": {"connected": connected, "ssid": ssid, "rssi": rssi},
                "clock": {"epoch_seconds": epoch, "utc_offset_minutes": offset}}

    # value: Projection read from the device or loaded from a JSON file.
    # Raises ValueError on malformed or out-of-range data.
    def _show(self, value):
        if not isinstance(value, dict) or set(value) != {"battery", "network", "clock"}:
            raise ValueError("Expected battery, network and clock objects.")
        battery, network, clock = value["battery"], value["network"], value["clock"]
        if (not isinstance(battery, dict) or set(battery) != {"level_percent"}
                or not isinstance(network, dict) or set(network) != {"connected", "ssid", "rssi"}
                or not isinstance(clock, dict) or set(clock) != {"epoch_seconds", "utc_offset_minutes"}):
            raise ValueError("Invalid simulated snapshot fields.")
        level, connected, ssid, rssi = battery["level_percent"], network["connected"], network["ssid"], network["rssi"]
        epoch, offset = clock["epoch_seconds"], clock["utc_offset_minutes"]
        if ((level is not None and (type(level) is not int or not 0 <= level <= 100))
                or (connected is not None and type(connected) is not bool)
                or not isinstance(ssid, str) or (ssid and (not ssid.isascii() or not ssid.isprintable() or len(ssid) > 32))
                or (rssi is not None and (type(rssi) is not int or not -100 <= rssi <= -1))
                or (offset is not None and (type(offset) is not int or not -840 <= offset <= 840))
                or (epoch is not None and type(epoch) is not int)):
            raise ValueError("Snapshot contains invalid battery, network or clock values.")
        if (epoch is None) != (offset is None):
            raise ValueError("The clock and UTC offset must both be provided or both be unavailable.")
        try:
            local_time = "" if epoch is None else datetime.fromtimestamp(epoch, timezone(timedelta(minutes=offset))).strftime("%Y-%m-%d %H:%M:%S")
        except (ValueError, OverflowError, OSError):
            raise ValueError("Snapshot contains an unsupported clock value.") from None
        self.level.set("" if level is None else str(level))
        self.connected.set("Unavailable" if connected is None else ("Connected" if connected else "Disconnected"))
        self.ssid.set(ssid)
        self.rssi.set("" if rssi is None else str(rssi))
        self.local_time.set(local_time)
        self.offset.set("" if offset is None else str(offset))

    # action: Failed operation shown to the user.
    # error: Original exception to display.
    def _error(self, action, error):
        self.status.set(f"{action} failed.")
        messagebox.showerror("Snapshot editor — SIMULATED", f"{action} failed:\n{error}", parent=self.window)

    # No parameters: identify unchanged form sections without rereading sensors.
    def _form_state(self):
        return {"battery": self.level.get(),
                "network": (self.connected.get(), self.ssid.get(), self.rssi.get()),
                "clock": (self.local_time.get(), self.offset.get())}

    # No parameters: read the current values, leaving the device untouched.
    def read_current(self):
        try:
            self._show(self.device.read_snapshot())
        except (AttributeError, RuntimeError, ValueError, TypeError, OverflowError) as error:
            self._error("Read current snapshot", error)
            return
        self._baseline = self._form_state()
        self._opened_file = False
        self.status.set("Current simulated inputs loaded; blank fields are unavailable.")

    # No parameters: validate and publish the values explicitly to the simulator.
    def apply(self):
        try:
            values = self._value(allow_unavailable=True)
            current = self._form_state()
            if not self._opened_file and self._baseline is not None:
                # Do not republish aging readings or rewind a clock that advanced
                # while the user edited a different section.
                if current["battery"] == self._baseline["battery"]:
                    values["battery"]["level_percent"] = None
                if current["network"] == self._baseline["network"]:
                    values["network"]["connected"] = None
                if current["clock"] == self._baseline["clock"]:
                    values["clock"] = {"epoch_seconds": None, "utc_offset_minutes": None}
            self.device.apply_snapshot(values)
        except (AttributeError, RuntimeError, ValueError, TypeError, OverflowError) as error:
            self._error("Apply snapshot (some services may already have changed)", error)
            return
        self._baseline = current
        self._opened_file = False
        self.status.set("Applied the changed simulated values.")

    # No parameters: load a projection for review without applying it.
    def open_file(self):
        path = filedialog.askopenfilename(parent=self.window, title="Open simulated snapshot",
                                          filetypes=(("JSON files", "*.json"), ("All files", "*")))
        if not path:
            return
        try:
            with open(path, encoding="utf-8") as source:
                self._show(json.load(source))
        except (OSError, UnicodeError, ValueError, TypeError, OverflowError) as error:
            self._error("Open snapshot", error)
            return
        self._opened_file = True
        self.status.set("File opened; select Apply to publish the values.")

    # No parameters: save the editable projection without applying it.
    def save_file(self):
        try:
            text = json.dumps(self._value(allow_unavailable=True), indent=2, ensure_ascii=False, allow_nan=False) + "\n"
        except (ValueError, TypeError, OverflowError) as error:
            self._error("Save snapshot", error)
            return
        path = filedialog.asksaveasfilename(parent=self.window, title="Save simulated snapshot", defaultextension=".json",
                                            filetypes=(("JSON files", "*.json"), ("All files", "*")))
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8", newline="\n") as destination:
                destination.write(text)
        except (OSError, UnicodeError) as error:
            self._error("Save snapshot", error)
            return
        self.status.set("Saved the simulated inputs; saving does not apply them.")
