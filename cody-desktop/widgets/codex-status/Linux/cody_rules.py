"""Native editor for the companion monitor's explicit control operations."""
from __future__ import annotations

import math
import threading
import uuid
from gi.repository import GLib, Gtk

CONDITIONS = ["below", "above", "changed_to_below", "changed_to_above", "increased"]


class CodyRulesWindow(Gtk.Window):
    # parent: Existing native window that owns this settings dialog.
    # invoke: Callable accepting one broker operation and its JSON object.
    # Raises native GTK errors when the window cannot be constructed.
    def __init__(self, parent, invoke):
        super().__init__(title="PC alerts", transient_for=parent, modal=True,
                         default_width=560, default_height=760)
        self.invoke = invoke
        self.rules = []
        self.device_ids = []
        self.metric_ids = []
        self.closed = False
        self.busy = False
        self.body = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        for setter in (self.body.set_margin_start, self.body.set_margin_end,
                       self.body.set_margin_top, self.body.set_margin_bottom):
            setter(24)
        self.body.append(Gtk.Label(label="Select a physical Cody, save, start the monitor and refresh to discover metrics. Refresh discards unsaved drafts. Severity delivery requires compatible Cody firmware; delivery errors appear below.", wrap=True, xalign=0))
        self.devices = self.dropdown("Physical Cody", [])
        self.list = Gtk.ListBox(selection_mode=Gtk.SelectionMode.SINGLE)
        self.list.connect("row-selected", self.selected)
        scroller = Gtk.ScrolledWindow(min_content_height=120, max_content_height=160,
                                      propagate_natural_height=True, child=self.list)
        self.body.append(scroller)
        self.title = self.entry("Alert name")
        self.metrics = self.dropdown("Metric", [])
        self.condition = self.dropdown("Condition", CONDITIONS)
        self.threshold = self.entry("Threshold")
        self.severity = self.dropdown("Severity", ["information", "warning", "critical"])
        self.duration = self.entry("Duration in seconds")
        self.duration.set_text("0")
        self.repeat = self.entry("Repeat interval in seconds (optional)")
        self.reset = self.entry("Reset threshold (optional)")
        self.condition.connect("notify::selected", self.condition_changed)
        self.enabled = Gtk.CheckButton(label="Enabled", active=True)
        self.body.append(self.enabled)
        actions = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        for text, callback in (("New", self.clear_editor), ("Apply rule", self.apply_rule), ("Duplicate", self.duplicate_rule),
                               ("Delete", self.delete_rule)):
            actions.append(self.button(text, callback))
        self.body.append(actions)
        controls = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        for text, callback in (("Refresh", self.refresh), ("Save", self.save),
                               ("Start", lambda: self.operation("monitor/start")),
                               ("Stop", lambda: self.operation("monitor/stop"))):
            controls.append(self.button(text, callback))
        self.body.append(controls)
        self.result = Gtk.Label(label="", wrap=True, xalign=0)
        self.body.append(self.result)
        self.set_child(Gtk.ScrolledWindow(child=self.body, hscrollbar_policy=Gtk.PolicyType.NEVER))
        self.connect("close-request", self.on_close)
        self.refresh()

    # caption: Accessible field label.
    # Raises native GTK errors if the control cannot be created.
    def entry(self, caption):
        self.body.append(Gtk.Label(label=caption, xalign=0))
        field = Gtk.Entry()
        self.body.append(field)
        return field

    # caption: Accessible field label.
    # values: Exact available option identifiers.
    # Raises native GTK errors if the control cannot be created.
    def dropdown(self, caption, values):
        self.body.append(Gtk.Label(label=caption, xalign=0))
        field = Gtk.DropDown(model=Gtk.StringList.new(values))
        if not values:
            field.set_selected(Gtk.INVALID_LIST_POSITION)
        self.body.append(field)
        return field

    # caption: Explicit user action.
    # action: Function invoked on the GTK thread.
    # Raises native GTK errors if the control cannot be created.
    def button(self, caption, action):
        button = Gtk.Button(label=caption)
        button.connect("clicked", lambda *_: self.dispatch(action))
        return button

    # action: One user action; errors remain visible and are never retried.
    # Exception: Displays form and operation errors instead of suppressing them.
    def dispatch(self, action):
        if self.busy or self.closed:
            return
        try:
            action()
        except (ValueError, RuntimeError, KeyError, TypeError) as error:
            self.result.set_text(str(error))

    # work: Background operation that must not access GTK controls.
    # receive: Main-thread handler for the successful result.
    # Exception: Reports the source error without retrying a potentially admitted write.
    def run(self, work, receive):
        if self.busy or self.closed:
            return
        self.busy = True
        def worker():
            try:
                value, error = work(), None
            except (OSError, RuntimeError, ValueError, KeyError, TypeError) as failure:
                value, error = None, str(failure)
            def complete():
                self.busy = False
                if not self.closed:
                    try:
                        self.result.set_text(error) if error is not None else receive(value)
                    except (ValueError, RuntimeError, KeyError, TypeError) as failure:
                        self.result.set_text(str(failure))
                return False
            GLib.idle_add(complete)
        threading.Thread(target=worker, daemon=True).start()

    # No parameters: observes the real monitor and paired-device projections.
    # Exception: The background operation reports broker or parsing errors.
    def refresh(self):
        self.run(lambda: (self.invoke("monitor/status", {}), self.invoke("bridge/status", {})), self.received)

    # observed: Exact monitor and bridge response objects from the broker.
    # Raises ValueError or KeyError for invalid projections.
    def received(self, observed):
        monitor, bridge = observed
        self.rules = [dict(rule) for rule in monitor["rules"]]
        self.device_ids = [item["device_id"] for item in bridge["devices"] if not item["simulated"]]
        self.devices.set_model(Gtk.StringList.new(self.device_ids))
        saved = monitor.get("device_id")
        self.devices.set_selected(self.device_ids.index(saved) if saved in self.device_ids else Gtk.INVALID_LIST_POSITION)
        metric_labels = {}
        for sample in monitor["samples"]:
            metric_labels[sample["id"]] = sample["label"] + (" (" + sample["unit"] + ")" if sample["unit"] else "")
        for rule in self.rules:
            metric_labels.setdefault(rule["metric_id"], rule["metric_id"] + " (unavailable)")
        self.metric_ids = list(metric_labels)
        self.metrics.set_model(Gtk.StringList.new(list(metric_labels.values())))
        self.metrics.set_selected(Gtk.INVALID_LIST_POSITION)
        self.redraw_rules()
        status = "Monitor running." if monitor["running"] else "Monitor stopped."
        if saved and "pc_alert_capability" in monitor:
            status += (" Cody severity support confirmed." if monitor["pc_alert_capability"] == "confirmed"
                       else " Cody severity support not confirmed; Cody may be offline or need an update.")
        if monitor.get("last_delivery_error"):
            status += " Delivery: " + monitor["last_delivery_error"]
        if monitor.get("config_error"):
            status += " Settings: " + monitor["config_error"]
        for issue in monitor.get("issues", []):
            status += "\n" + issue["source"] + ": " + issue["message"]
        self.result.set_text(status)

    # No parameters: rebuilds the visible list from the draft rules.
    # Exception: Native GTK errors propagate to the caller.
    def redraw_rules(self):
        child = self.list.get_first_child()
        while child is not None:
            following = child.get_next_sibling()
            self.list.remove(child)
            child = following
        for rule in self.rules:
            self.list.append(Gtk.Label(label=f"{rule['title']} · {rule['metric_id']} · {rule['severity']}", xalign=0))

    # listbox: Rule list that emitted the selection.
    # row: Selected row or None while rebuilding the list.
    # Exception: Raises when an already-loaded rule has invalid fields.
    def selected(self, listbox, row):
        if row is None or row.get_index() >= len(self.rules):
            return
        rule = self.rules[row.get_index()]
        self.title.set_text(rule["title"])
        self.metrics.set_selected(self.metric_ids.index(rule["metric_id"]))
        self.condition.set_selected(CONDITIONS.index(rule["condition"]))
        self.severity.set_selected(["information", "warning", "critical"].index(rule["severity"]))
        self.threshold.set_text(str(rule["threshold"]))
        self.duration.set_text(str(rule["duration_seconds"]))
        self.repeat.set_text("" if rule["repeat_seconds"] is None else str(rule["repeat_seconds"]))
        self.reset.set_text("" if rule["reset_threshold"] is None else str(rule["reset_threshold"]))
        self.enabled.set_active(rule["enabled"])

    # arguments: GTK selection notification; event-count rules do not use numeric fields.
    # Exception: Does not intentionally raise.
    def condition_changed(self, *arguments):
        numeric = self.condition.get_selected() != CONDITIONS.index("increased")
        for field in (self.threshold, self.duration, self.repeat, self.reset):
            field.set_sensitive(numeric)

    # No parameters: prepares a new draft while retaining existing rules.
    # Exception: Does not intentionally raise.
    def clear_editor(self):
        self.list.unselect_all()
        for field in (self.title, self.threshold, self.repeat, self.reset):
            field.set_text("")
        self.duration.set_text("0")
        self.metrics.set_selected(Gtk.INVALID_LIST_POSITION)
        self.condition.set_selected(0)
        self.severity.set_selected(0)
        self.enabled.set_active(True)

    # value: Raw field contents.
    # caption: Human-readable field name for validation errors.
    # Raises ValueError for missing, non-finite or malformed input.
    @staticmethod
    def number(value, caption):
        try:
            number = float(value)
        except ValueError as error:
            raise ValueError(caption + " must be a number.") from error
        if not math.isfinite(number):
            raise ValueError(caption + " must be finite.")
        return number

    # No parameters: adds or updates only the local draft.
    # Raises ValueError for incomplete form values.
    def apply_rule(self):
        metric = self.metrics.get_selected()
        if not self.title.get_text().strip() or metric >= len(self.metric_ids):
            raise ValueError("Choose a name and metric.")
        selected = self.list.get_selected_row()
        index = selected.get_index() if selected is not None else None
        condition = CONDITIONS[self.condition.get_selected()]
        is_event = condition == "increased"
        rule = {"id": self.rules[index]["id"] if index is not None else uuid.uuid4().hex,
                "title": self.title.get_text().strip(), "metric_id": self.metric_ids[metric],
                "condition": condition,
                "threshold": 0 if is_event else self.number(self.threshold.get_text(), "Threshold"),
                "severity": ["information", "warning", "critical"][self.severity.get_selected()],
                "duration_seconds": 0 if is_event else self.number(self.duration.get_text(), "Duration"),
                "repeat_seconds": self.number(self.repeat.get_text(), "Repeat") if not is_event and self.repeat.get_text().strip() else None,
                "reset_threshold": self.number(self.reset.get_text(), "Reset threshold") if not is_event and self.reset.get_text().strip() else None,
                "enabled": self.enabled.get_active()}
        if index is None:
            self.rules.append(rule)
            index = len(self.rules) - 1
        else:
            self.rules[index] = rule
        self.redraw_rules()
        self.list.select_row(self.list.get_row_at_index(index))
        self.result.set_text("Draft updated. Select Save to apply.")

    # No parameters: removes only the selected rule from the local draft.
    # Raises ValueError if no row is selected.
    def delete_rule(self):
        selected = self.list.get_selected_row()
        if selected is None:
            raise ValueError("Select a rule first.")
        del self.rules[selected.get_index()]
        self.redraw_rules()
        self.clear_editor()
        self.result.set_text("Draft updated. Select Save to apply.")

    # No parameters: duplicates the selected rule with a distinct stable identity.
    # Raises ValueError when no rule is selected.
    def duplicate_rule(self):
        selected = self.list.get_selected_row()
        if selected is None:
            raise ValueError("Select a rule first.")
        duplicate = dict(self.rules[selected.get_index()])
        duplicate["id"] = uuid.uuid4().hex
        duplicate["title"] += " copy"
        self.rules.append(duplicate)
        self.redraw_rules()
        self.list.select_row(self.list.get_row_at_index(len(self.rules) - 1))
        self.result.set_text("Draft updated. Select Save to apply.")

    # No parameters: publishes one complete rule set for an explicit physical Cody.
    # Raises ValueError if no destination has been selected.
    def save(self):
        index = self.devices.get_selected()
        if index >= len(self.device_ids):
            raise ValueError("Select a physical Cody first.")
        payload = {"device_id": self.device_ids[index], "rules": [dict(rule) for rule in self.rules]}
        self.run(lambda: self.invoke("monitor/configure", payload), lambda _: self.result.set_text("Alert configuration saved."))

    # operation: Explicit monitor start or stop selected by the user.
    # Exception: The background operation reports the broker's original error.
    def operation(self, operation):
        self.run(lambda: self.invoke(operation, {}), lambda _: self.result.set_text(
            "Monitor started." if operation == "monitor/start" else "Monitor stopped."))

    # arguments: GTK close-request metadata; no remote operation is implied.
    # Returns False so GTK can close the window.
    def on_close(self, *arguments):
        self.closed = True
        return False
