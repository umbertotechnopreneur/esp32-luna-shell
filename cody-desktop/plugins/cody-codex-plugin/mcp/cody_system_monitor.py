"""Host metrics and user-defined alert rules for the Cody companion.

This module performs read-only acquisition. The caller owns persistence,
user consent, notification delivery and the lifetime of the worker.
"""
from __future__ import annotations

import ctypes
import dataclasses
import enum
import hashlib
import json
import logging
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
from collections.abc import Callable, Iterable
from datetime import datetime, timezone

LOG = logging.getLogger(__name__)


class Severity(str, enum.Enum):
    INFORMATION = "information"
    WARNING = "warning"
    CRITICAL = "critical"


class Condition(str, enum.Enum):
    BELOW = "below"
    ABOVE = "above"
    CHANGED_TO_BELOW = "changed_to_below"
    CHANGED_TO_ABOVE = "changed_to_above"
    INCREASED = "increased"


@dataclasses.dataclass(frozen=True)
class AlertRule:
    id: str
    title: str
    metric_id: str
    condition: Condition
    threshold: float
    severity: Severity
    duration_seconds: float = 0
    repeat_seconds: float | None = None
    reset_threshold: float | None = None
    enabled: bool = True


@dataclasses.dataclass(frozen=True)
class MetricSample:
    id: str
    label: str
    value: float
    unit: str
    observed_at: datetime
    monotonic: float


@dataclasses.dataclass(frozen=True)
class AcquisitionIssue:
    source: str
    message: str


@dataclasses.dataclass(frozen=True)
class AcquisitionResult:
    samples: tuple[MetricSample, ...]
    issues: tuple[AcquisitionIssue, ...]


@dataclasses.dataclass(frozen=True)
class AlertNotification:
    rule_id: str
    title: str
    severity: Severity
    sample: MetricSample


@dataclasses.dataclass
class _RuleState:
    since: float | None = None
    last_alert: float | None = None
    previous: bool | None = None
    active: bool = False
    previous_value: float | None = None


class RuleEngine:
    # No parameters: initialize an empty set of rules and runtime states.
    # Exception: Does not intentionally raise.
    def __init__(self) -> None:
        self._rules: tuple[AlertRule, ...] = ()
        self._states: dict[str, _RuleState] = {}

    # rules: Complete replacement rule set with stable, unique identifiers.
    # Exception: Raises ValueError for invalid thresholds, duplicates or invalid timing.
    def set_rules(self, rules: Iterable[AlertRule]) -> None:
        import math
        replacement = tuple(rules)
        ids: set[str] = set()
        for rule in replacement:
            if (not isinstance(rule, AlertRule) or not all(isinstance(value, str) and 0 < len(value) <= 128 for value in
                    (rule.id, rule.title, rule.metric_id)) or rule.id in ids or type(rule.enabled) is not bool):
                raise ValueError("Invalid or duplicate alert rule")
            if not isinstance(rule.condition, Condition) or not isinstance(rule.severity, Severity):
                raise ValueError("Invalid alert condition or severity")
            if (type(rule.threshold) not in (int, float) or type(rule.duration_seconds) not in (int, float) or
                    not math.isfinite(rule.threshold) or not math.isfinite(rule.duration_seconds) or rule.duration_seconds < 0):
                raise ValueError("Invalid alert threshold or duration")
            if rule.repeat_seconds is not None and (type(rule.repeat_seconds) not in (int, float) or not math.isfinite(rule.repeat_seconds) or rule.repeat_seconds <= 0):
                raise ValueError("Invalid alert repeat interval")
            if rule.condition is Condition.INCREASED and (rule.duration_seconds != 0 or rule.repeat_seconds is not None or rule.reset_threshold is not None):
                raise ValueError("Event-count rules do not support duration, repeat or reset thresholds")
            above = rule.condition in (Condition.ABOVE, Condition.CHANGED_TO_ABOVE)
            if rule.reset_threshold is not None and (type(rule.reset_threshold) not in (int, float) or not math.isfinite(rule.reset_threshold) or
                    (rule.reset_threshold > rule.threshold if above else rule.reset_threshold < rule.threshold)):
                raise ValueError("Reset threshold must be on the recovery side of the trigger")
            ids.add(rule.id)
        previous = {rule.id: rule for rule in self._rules}
        self._states = {rule.id: self._states[rule.id] for rule in replacement
                        if rule.id in self._states and previous.get(rule.id) == rule}
        self._rules = replacement

    # samples: One acquisition cycle; absent values must never be treated as current.
    # Exception: Raises ValueError for duplicate metric IDs or invalid sample values.
    def evaluate(self, samples: Iterable[MetricSample]) -> tuple[AlertNotification, ...]:
        import math
        observed: dict[str, MetricSample] = {}
        for sample in samples:
            if sample.id in observed or not math.isfinite(sample.value) or not math.isfinite(sample.monotonic):
                raise ValueError("Duplicate or invalid metric sample")
            observed[sample.id] = sample
        alerts: list[AlertNotification] = []
        for rule in self._rules:
            if not rule.enabled:
                continue
            state = self._states.setdefault(rule.id, _RuleState())
            sample = observed.get(rule.metric_id)
            if sample is None:
                state.since = None
                state.previous = None
                state.previous_value = None
                continue
            if rule.condition is Condition.INCREASED:
                if state.previous_value is not None and sample.value > state.previous_value:
                    alerts.append(AlertNotification(rule.id, rule.title, rule.severity, sample))
                state.previous_value = sample.value
                continue
            above = rule.condition in (Condition.ABOVE, Condition.CHANGED_TO_ABOVE)
            matched = sample.value > rule.threshold if above else sample.value < rule.threshold
            change_only = rule.condition in (Condition.CHANGED_TO_ABOVE, Condition.CHANGED_TO_BELOW)
            crossed = state.previous is False and matched
            state.previous = matched
            if state.active:
                reset = rule.reset_threshold if rule.reset_threshold is not None else rule.threshold
                recovered = sample.value <= reset if above else sample.value >= reset
                if recovered:
                    state.active = False
                    state.since = state.last_alert = None
                elif (not change_only and matched and rule.repeat_seconds is not None and
                      state.last_alert is not None and sample.monotonic - state.last_alert >= rule.repeat_seconds):
                    state.last_alert = sample.monotonic
                    alerts.append(AlertNotification(rule.id, rule.title, rule.severity, sample))
                continue
            if not matched:
                state.since = None
                continue
            if change_only and not crossed and state.since is None:
                continue
            if state.since is None or sample.monotonic < state.since:
                state.since = sample.monotonic
            if sample.monotonic - state.since >= rule.duration_seconds:
                state.active = True
                state.last_alert = sample.monotonic
                alerts.append(AlertNotification(rule.id, rule.title, rule.severity, sample))
        return tuple(alerts)


class SystemMetricsCollector:
    # No parameters: initialize CPU deltas; the first acquisition has no CPU percentage.
    # Exception: Does not intentionally raise.
    def __init__(self) -> None:
        self._previous_cpu: tuple[int, int] | None = None
        self._previous_usb: set[str] | None = None
        self._usb_connections = 0
        self._usb_disconnections = 0

    # No parameters: acquire host metrics; unavailable sources are reported and omitted.
    # Exception: Collection errors are included in the returned issues.
    def collect(self) -> AcquisitionResult:
        samples: list[MetricSample] = []
        issues: list[AcquisitionIssue] = []
        now = datetime.now(timezone.utc)
        tick = time.monotonic()

        # identifier: Stable metric identifier.
        # label: Human-readable metric name.
        # value: Current numerical observation.
        # unit: Display unit.
        # Exception: Does not intentionally raise.
        def add(identifier: str, label: str, value: float, unit: str) -> None:
            samples.append(MetricSample(identifier, label, value, unit, now, tick))

        for source, collector in (("memory", self._memory), ("cpu", self._cpu),
                                  ("disks", self._disks), ("battery", self._battery),
                                  ("network", self._network), ("usb", self._usb),
                                  ("temperature", self._temperature)):
            try:
                for item in collector():
                    add(*item)
            except (OSError, ValueError, KeyError, IndexError, OverflowError, RuntimeError) as exc:
                issues.append(AcquisitionIssue(source, str(exc)))
                if source == "cpu":
                    self._previous_cpu = None
        return AcquisitionResult(tuple(samples), tuple(issues))

    # No parameters: read total and currently available physical memory.
    # Exception: Raises OSError or ValueError when the platform cannot supply memory data.
    def _memory(self) -> list[tuple[str, str, float, str]]:
        if os.name == "nt":
            class MemoryStatus(ctypes.Structure):
                _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong)] + [
                    (name, ctypes.c_ulonglong) for name in ("total", "available", "total_page", "available_page",
                                                             "total_virtual", "available_virtual", "extended")]
            info = MemoryStatus()
            info.length = ctypes.sizeof(info)
            if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(info)):
                raise ctypes.WinError()
            total, available = info.total, info.available
        elif os.path.exists("/proc/meminfo"):
            values = {}
            with open("/proc/meminfo", encoding="ascii") as source:
                for line in source:
                    key, _, value = line.partition(":")
                    if key in ("MemTotal", "MemAvailable"):
                        values[key] = int(value.split()[0]) * 1024
            total, available = values["MemTotal"], values["MemAvailable"]
        else:
            raise OSError("Physical memory acquisition is unavailable")
        if not total or available > total:
            raise ValueError("Invalid physical memory measurement")
        return [("memory.available.bytes", "Available memory", available, " B"),
                ("memory.available.percent", "Available memory", available * 100.0 / total, "%")]

    # No parameters: derive CPU load from consecutive aggregate OS counters.
    # Exception: Raises OSError or ValueError when CPU counters are unavailable.
    def _cpu(self) -> list[tuple[str, str, float, str]]:
        if os.name == "nt":
            class FileTime(ctypes.Structure):
                _fields_ = [("low", ctypes.c_uint32), ("high", ctypes.c_uint32)]
                # No parameters: combine the two parts of a Windows counter.
                # Exception: Does not intentionally raise.
                def value(self) -> int:
                    return (self.high << 32) | self.low
            idle, kernel, user = FileTime(), FileTime(), FileTime()
            if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
                raise ctypes.WinError()
            idle_value, total = idle.value(), kernel.value() + user.value()
        elif os.path.exists("/proc/stat"):
            with open("/proc/stat", encoding="ascii") as source:
                parts = source.readline().split()
            if len(parts) < 5 or parts[0] != "cpu":
                raise ValueError("Missing aggregate CPU counters")
            ticks = [int(value) for value in parts[1:9]]
            idle_value = ticks[3] + (ticks[4] if len(ticks) > 4 else 0)
            total = sum(ticks)
        else:
            raise OSError("CPU acquisition is unavailable")
        previous, self._previous_cpu = self._previous_cpu, (idle_value, total)
        if previous is None or total <= previous[1] or idle_value < previous[0]:
            return []
        elapsed, idle_elapsed = total - previous[1], idle_value - previous[0]
        if idle_elapsed > elapsed:
            raise ValueError("Invalid aggregate CPU counters")
        return [("cpu.used.percent", "CPU usage", (elapsed - idle_elapsed) * 100.0 / elapsed, "%")]

    # No parameters: read accessible local volume free-space counters.
    # Exception: Raises OSError when volume enumeration fails.
    def _disks(self) -> list[tuple[str, str, float, str]]:
        if os.name == "nt":
            buffer = ctypes.create_unicode_buffer(32768)
            length = ctypes.windll.kernel32.GetLogicalDriveStringsW(len(buffer), buffer)
            if not length or length >= len(buffer):
                raise ctypes.WinError()
            # Avoid probing network mounts, which may block the monitor indefinitely.
            drive_type = ctypes.windll.kernel32.GetDriveTypeW
            drive_type.argtypes = [ctypes.c_wchar_p]
            drive_type.restype = ctypes.c_uint32
            volumes = [volume for volume in buffer[:length].split("\x00") if volume and
                       drive_type(volume) in (2, 3)]
        elif os.path.exists("/proc/mounts"):
            with open("/proc/mounts", encoding="utf-8") as source:
                volumes = [line.split()[1].replace("\\040", " ").replace("\\011", "\t").replace("\\134", "\\")
                           for line in source if line.startswith("/dev/")]
        else:
            raise OSError("Volume enumeration is unavailable")
        result = []
        import shutil
        for volume in sorted(set(volumes)):
            if not volume:
                continue
            try:
                usage = shutil.disk_usage(volume)
            except OSError as exc:
                LOG.warning("Cannot read volume %s: %s", volume, exc)
                continue
            if usage.total <= 0:
                continue
            identifier = urllib.parse.quote(volume, safe="")
            result.extend([(f"disk.{identifier}.available.bytes", f"Free space on {volume}", usage.free, " B"),
                           (f"disk.{identifier}.available.percent", f"Free space on {volume}", usage.free * 100 / usage.total, "%")])
        return result

    # No parameters: read battery charge where the operating system exposes it.
    # Exception: Raises OSError or ValueError if an accessible battery cannot be read.
    def _battery(self) -> list[tuple[str, str, float, str]]:
        if os.name == "nt":
            class PowerStatus(ctypes.Structure):
                _fields_ = [("ac", ctypes.c_ubyte), ("flags", ctypes.c_ubyte), ("percent", ctypes.c_ubyte),
                            ("system", ctypes.c_ubyte), ("remaining", ctypes.c_uint32), ("full", ctypes.c_uint32)]
            status = PowerStatus()
            if not ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status)):
                raise ctypes.WinError()
            result = []
            if status.percent <= 100:
                result.append(("battery.charge.percent", "Battery charge", status.percent, "%"))
            return result
        root = pathlib.Path("/sys/class/power_supply")
        if not root.is_dir():
            return []
        result = []
        for device in root.iterdir():
            if (device / "type").read_text(encoding="ascii").strip() != "Battery":
                continue
            value = int((device / "capacity").read_text(encoding="ascii").strip())
            if not 0 <= value <= 100:
                raise ValueError("Invalid battery percentage")
            identifier = urllib.parse.quote(device.name, safe="")
            result.append((f"battery.{identifier}.charge.percent", f"Battery {device.name} charge", value, "%"))
        return result

    # No parameters: report whether a non-loopback interface is up.
    # Exception: Raises OSError when the interface status cannot be read.
    def _network(self) -> list[tuple[str, str, float, str]]:
        if os.name == "nt":
            class InterfaceRow(ctypes.Structure):
                _fields_ = [("name", ctypes.c_wchar * 256), ("index", ctypes.c_uint32),
                            ("type", ctypes.c_uint32), ("mtu", ctypes.c_uint32), ("speed", ctypes.c_uint32),
                            ("physical_length", ctypes.c_uint32), ("physical", ctypes.c_ubyte * 8)] + [
                                (name, ctypes.c_uint32) for name in ("admin", "oper", "change", "in_octets", "in_unicast",
                                "in_nonunicast", "in_discards", "in_errors", "in_unknown", "out_octets", "out_unicast",
                                "out_nonunicast", "out_discards", "out_errors", "out_queue", "description_length")] + [
                                ("description", ctypes.c_ubyte * 256)]
            size = ctypes.c_uint32(0)
            api = ctypes.windll.iphlpapi.GetIfTable
            result = api(None, ctypes.byref(size), False)
            if result != 122 or size.value < 4 or size.value > 16 * 1024 * 1024:
                raise OSError(f"Cannot enumerate Windows network interfaces: {result}")
            buffer = ctypes.create_string_buffer(size.value)
            result = api(buffer, ctypes.byref(size), False)
            if result != 0:
                raise OSError(f"Cannot read Windows network interfaces: {result}")
            count = ctypes.c_uint32.from_buffer(buffer).value
            if 4 + count * ctypes.sizeof(InterfaceRow) > size.value:
                raise ValueError("Invalid Windows interface table")
            rows = (InterfaceRow * count).from_buffer(buffer, 4)
            # MIB_IF_OPER_STATUS_CONNECTED/OPERATIONAL, excluding software loopback.
            up = any(row.type != 24 and row.oper in (4, 5) for row in rows)
            return [("network.interface.connected", "Network interface connected", int(up), "")]
        root = pathlib.Path("/sys/class/net")
        if not root.is_dir():
            return []
        states = [(device / "operstate").read_text(encoding="ascii").strip()
                  for device in root.iterdir() if device.name != "lo"]
        return [("network.interface.connected", "Network interface connected", int("up" in states), "")]

    # No parameters: count changes among present USB devices after the initial baseline.
    # Exception: Raises OSError if OS device enumeration fails; no disconnect is inferred from failure.
    def _usb(self) -> list[tuple[str, str, float, str]]:
        if os.name == "nt":
            from ctypes import wintypes
            class Guid(ctypes.Structure):
                _fields_ = [("a", wintypes.DWORD), ("b", wintypes.WORD), ("c", wintypes.WORD), ("d", ctypes.c_ubyte * 8)]
            class DeviceInfo(ctypes.Structure):
                _fields_ = [("size", wintypes.DWORD), ("class_id", Guid), ("instance", wintypes.DWORD),
                            ("reserved", ctypes.c_size_t)]
            api = ctypes.WinDLL("setupapi", use_last_error=True)
            api.SetupDiGetClassDevsW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD]
            api.SetupDiGetClassDevsW.restype = ctypes.c_void_p
            api.SetupDiEnumDeviceInfo.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(DeviceInfo)]
            api.SetupDiGetDeviceInstanceIdW.argtypes = [ctypes.c_void_p, ctypes.POINTER(DeviceInfo), wintypes.LPWSTR,
                                                       wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
            api.SetupDiDestroyDeviceInfoList.argtypes = [ctypes.c_void_p]
            handle = api.SetupDiGetClassDevsW(None, "USB", None, 0x2 | 0x4)
            if handle == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
            present: set[str] = set()
            try:
                index = 0
                while True:
                    entry = DeviceInfo()
                    entry.size = ctypes.sizeof(entry)
                    if not api.SetupDiEnumDeviceInfo(handle, index, ctypes.byref(entry)):
                        error = ctypes.get_last_error()
                        if error == 259:
                            break
                        raise ctypes.WinError(error)
                    name = ctypes.create_unicode_buffer(1024)
                    needed = wintypes.DWORD()
                    if not api.SetupDiGetDeviceInstanceIdW(handle, ctypes.byref(entry), name, len(name), ctypes.byref(needed)):
                        raise ctypes.WinError(ctypes.get_last_error())
                    present.add(name.value)
                    index += 1
            finally:
                api.SetupDiDestroyDeviceInfoList(handle)
        else:
            root = pathlib.Path("/sys/bus/usb/devices")
            if not root.is_dir():
                return []
            # USB interfaces have a colon; filter them to avoid counting one device many times.
            present = {device.name for device in root.iterdir()
                       if ":" not in device.name and (device / "idVendor").is_file() and (device / "idProduct").is_file()}
        if self._previous_usb is not None:
            self._usb_connections += len(present - self._previous_usb)
            self._usb_disconnections += len(self._previous_usb - present)
        self._previous_usb = present
        return [("usb.connections.count", "USB device connections", self._usb_connections, ""),
                ("usb.disconnections.count", "USB device disconnections", self._usb_disconnections, "")]

    # No parameters: read labeled Celsius sensors from selected Linux hwmon drivers.
    # Exception: Raises OSError or ValueError when a selected sensor cannot be read.
    def _temperature(self) -> list[tuple[str, str, float, str]]:
        root = pathlib.Path("/sys/class/hwmon")
        if os.name == "nt" or not root.is_dir():
            return []
        result = []
        # Do not guess the unit for hwmon drivers whose temperature inputs may be voltages.
        supported = {"coretemp", "k10temp", "amdgpu", "nvme"}
        for device in root.iterdir():
            driver = (device / "name").read_text(encoding="ascii").strip()
            if driver not in supported:
                continue
            for input_file in device.glob("temp*_input"):
                prefix = input_file.name.removesuffix("_input")
                label_file = device / f"{prefix}_label"
                label = label_file.read_text(encoding="utf-8").strip() if label_file.exists() else prefix
                milli_celsius = int(input_file.read_text(encoding="ascii").strip())
                stable_path = os.path.realpath(device / "device") if (device / "device").exists() else os.path.realpath(device)
                identity = hashlib.sha256(f"{stable_path}/{driver}/{prefix}".encode()).hexdigest()[:20]
                result.append((f"temperature.{identity}.celsius", f"{driver}: {label}", milli_celsius / 1000.0, " C"))
        return result


class MonitorService:
    # on_alert: Consumer for each alert; this module never sends it to a device itself.
    # on_issues: Consumer for current acquisition errors.
    # interval_seconds: Delay between acquisition cycles; must be positive.
    # on_samples: Optional consumer for the current cycle's available metrics.
    # Exception: Raises ValueError for invalid interval.
    def __init__(self, on_alert: Callable[[AlertNotification], None],
                 on_issues: Callable[[tuple[AcquisitionIssue, ...]], None], interval_seconds: float = 5,
                 on_samples: Callable[[tuple[MetricSample, ...]], None] | None = None) -> None:
        import math
        if not math.isfinite(interval_seconds) or interval_seconds <= 0:
            raise ValueError("Monitor interval must be positive")
        self._on_alert, self._on_issues, self._interval = on_alert, on_issues, interval_seconds
        self._on_samples = on_samples
        self._collector, self._engine = SystemMetricsCollector(), RuleEngine()
        self._lock, self._stop = threading.RLock(), threading.Event()
        self._thread: threading.Thread | None = None

    # rules: Complete replacement set of user-defined alert rules.
    # Exception: Raises ValueError for invalid rules.
    def set_rules(self, rules: Iterable[AlertRule]) -> None:
        with self._lock:
            self._engine.set_rules(rules)

    # No parameters: start a single worker; repeated starts do not create more workers.
    # Exception: Raises RuntimeError if the thread cannot be started.
    def start(self) -> None:
        with self._lock:
            if self._thread is not None and self._thread.is_alive():
                return
            self._stop.clear()
            self._thread = threading.Thread(target=self._run, name="cody-host-monitor", daemon=True)
            self._thread.start()

    @property
    # No parameters: report whether the worker is still active.
    # Exception: Does not intentionally raise.
    def running(self) -> bool:
        with self._lock:
            return self._thread is not None and self._thread.is_alive()

    # timeout: Maximum seconds to wait for a running acquisition to stop.
    # Exception: Raises TimeoutError if the worker has not stopped by the deadline.
    def stop(self, timeout: float = 10) -> None:
        self._stop.set()
        with self._lock:
            thread = self._thread
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout)
            if thread.is_alive():
                raise TimeoutError("Host monitor did not stop in time")

    # No parameters: acquire and publish until stopped; callback failures are logged.
    # Exception: Does not intentionally raise to the hosting thread.
    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                result = self._collector.collect()
                if self._stop.is_set():
                    break
                with self._lock:
                    alerts = self._engine.evaluate(result.samples)
                if self._on_samples is not None:
                    self._on_samples(result.samples)
                self._on_issues(result.issues)
                for alert in alerts:
                    if self._stop.is_set():
                        break
                    self._on_alert(alert)
            except Exception:
                LOG.exception("Host monitor acquisition or callback failed")
            self._stop.wait(self._interval)


# path: User-selected rules file; a missing file represents no saved rules.
# Exception: Raises ValueError or OSError for invalid data or inaccessible files.
def load_rules(path: pathlib.Path) -> tuple[AlertRule, ...]:
    try:
        raw = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ()
    if len(raw.encode("utf-8")) > 1024 * 1024:
        raise ValueError("Alert rules file is too large")
    document = json.loads(raw)
    if not isinstance(document, dict) or set(document) != {"version", "rules"} or document["version"] != 1 or not isinstance(document["rules"], list):
        raise ValueError("Invalid alert rules document")
    rules = []
    fields = {field.name for field in dataclasses.fields(AlertRule)}
    for row in document["rules"]:
        if not isinstance(row, dict) or set(row) != fields:
            raise ValueError("Invalid alert rule fields")
        try:
            rule = AlertRule(**{**row, "condition": Condition(row["condition"]), "severity": Severity(row["severity"])})
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError("Invalid alert rule") from exc
        rules.append(rule)
    engine = RuleEngine()
    engine.set_rules(rules)
    return tuple(rules)


# path: User-selected destination outside the application installation directory.
# rules: Complete replacement rule set; validated before touching existing state.
# Exception: Raises ValueError or OSError when validation or atomic replacement fails.
def save_rules(path: pathlib.Path, rules: Iterable[AlertRule]) -> None:
    rules = tuple(rules)
    engine = RuleEngine()
    engine.set_rules(rules)
    body = json.dumps({"version": 1, "rules": [dataclasses.asdict(rule) for rule in rules]}, ensure_ascii=True, indent=2)
    if len(body.encode("utf-8")) > 1024 * 1024:
        raise ValueError("Alert rules file is too large")
    # The caller creates and owns the configuration directory.
    temporary: str | None = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, prefix=".cody-rules-", delete=False) as output:
            temporary = output.name
            output.write(body)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            try:
                os.unlink(temporary)
            except OSError:
                LOG.exception("Could not remove incomplete alert rules file")


# alert: Alert raised by the rule engine, whose metric determines the generic event.
# Exception: Raises ValueError when no generic device category exists for the metric.
def cody_event_for_alert(alert: AlertNotification) -> str:
    metric = alert.sample.id
    if metric.startswith("memory."):
        event = "memory"
    elif metric.startswith("disk."):
        event = "disk"
    elif metric.startswith("cpu."):
        event = "cpu"
    elif metric.startswith("battery."):
        event = "battery"
    elif metric.startswith("temperature."):
        event = "temperature"
    elif metric == "network.interface.connected":
        event = "network_connected" if alert.sample.value > 0 else "network_disconnected"
    elif metric == "usb.connections.count":
        event = "usb_connected"
    elif metric == "usb.disconnections.count":
        event = "usb_disconnected"
    else:
        raise ValueError("No Cody event category is defined for this metric")
    return event


# control_script: Absolute path to the installed CCP service control entrypoint.
# device_id: Explicit retained identity of the physical Cody selected by the user.
# alert: Alert raised by the rule engine, whose metric determines the generic event.
# python_executable: Interpreter running the CCP control script.
# Exception: Raises ValueError, OSError or TimeoutError when admission cannot be confirmed.
def send_cody_alert(control_script: pathlib.Path, device_id: str, alert: AlertNotification,
                    python_executable: str = sys.executable) -> dict:
    if not control_script.is_absolute() or not control_script.is_file():
        raise ValueError("Choose an existing absolute CCP control script")
    if len(device_id) != 12 or any(character not in "0123456789abcdefABCDEF" for character in device_id):
        raise ValueError("Choose an explicit physical Cody identity")
    request = json.dumps({"device_id": device_id, "event": cody_event_for_alert(alert),
                          "severity": alert.severity.value}, separators=(",", ":"))
    try:
        result = subprocess.run([python_executable, "-B", str(control_script), "--control", "device/notify-system"],
                                input=request, text=True, encoding="utf-8", stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, timeout=30, check=False,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except subprocess.TimeoutExpired as exc:
        raise TimeoutError("Cody event admission is unconfirmed; do not replay automatically") from exc
    if len(result.stdout) > 65536:
        raise ValueError("Cody control response exceeded its size limit")
    envelope = json.loads(result.stdout)
    if isinstance(envelope, dict) and isinstance(envelope.get("error"), dict):
        raise OSError(str(envelope["error"].get("message", "Cody service rejected the event")))
    if result.returncode != 0 or not isinstance(envelope, dict) or "result" not in envelope:
        raise OSError("Cody service rejected the event or returned an invalid response")
    response = envelope["result"]
    if not isinstance(response, dict) or response.get("isError") is True:
        raise OSError("Cody service rejected the event")
    # Admission is not evidence that the event was displayed on the device.
    return response
