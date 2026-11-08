#!/usr/bin/env python3
"""Native GTK 4 / libadwaita presentation for the shared C# account reader."""
from __future__ import annotations

import datetime as dt
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time
from watchdog import start as start_watchdog, record as log_event, state_dir
GUARD = None


def report_unhandled(kind, error, traceback):
    log_event("ui.unhandled", kind.__name__ + ": " + str(error))
    sys.__excepthook__(kind, error, traceback)


sys.excepthook = report_unhandled

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gdk, GdkPixbuf, GLib, Gtk, Pango, PangoCairo, Gio
from tray import StatusTray
import cairo

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BACKEND = ROOT / "backend/OpenAIUsage.Backend.dll"
if not DEFAULT_BACKEND.is_file():
    DEFAULT_BACKEND = ROOT / "Backend/bin/Debug/net10.0/OpenAIUsage.Backend.dll"
BACKEND = Path(os.environ.get("OPENAI_USAGE_BACKEND", str(DEFAULT_BACKEND)))


def invoke(command="snapshot", value=None):
    if not BACKEND.is_file():
        raise RuntimeError("Shared backend is missing. Build Backend/OpenAIUsage.Backend.csproj or set OPENAI_USAGE_BACKEND.")
    native_backend = BACKEND.with_suffix("")
    backend_command = [str(native_backend)] if native_backend.is_file() else ["dotnet", str(BACKEND)]
    process = subprocess.Popen([*backend_command, command], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                               start_new_session=True)
    try:
        output, _ = process.communicate(json.dumps(value) if value is not None else "", timeout=45)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        raise RuntimeError("Account reader timed out. Check Codex and your connection.") from None
    try:
        result = json.loads(output)
    except ValueError:
        raise RuntimeError("Account reader failed. Verify the .NET 10 runtime is installed.") from None
    if "error" in result:
        raise RuntimeError(result["error"])
    return result


CSS = b"""
window { background-color: @window_bg_color; color: @window_fg_color; }
headerbar { background-color: @window_bg_color; box-shadow: none; border: none; }
notebook, notebook > stack, notebook > header { background: transparent; border: none; box-shadow: none; }
notebook > header > tabs > tab { background: transparent; padding: 10px 20px; }
notebook > header > tabs > tab:checked { box-shadow: inset 0 -3px @accent_bg_color; }
.title { font-size: 21px; font-weight: 650; }
.huge { font-family: monospace; font-size: 64px; font-weight: 700; }
.forecast { font-family: monospace; font-size: 32px; font-weight: 700; }
.metric { font-family: monospace; font-size: 30px; font-weight: 650; }
.summary .quota-low { font-weight: 800; }
.learning { font-size: 24px; opacity: .75; color: @window_fg_color; }
.placeholder { font-size: 18px; opacity: .7; color: @window_fg_color; }
.mono-detail { font-family: monospace; font-size: 12px; }
.summary .huge { font-size: 40px; }
.summary .metric { font-size: 25px; }
window.micro {
    border-radius: 20px;
    border: 1px solid alpha(@accent_bg_color, .32);
    box-shadow: inset 0 0 3px alpha(@accent_bg_color, .09);
}
window.micro:backdrop { border-color: transparent; box-shadow: none; }
.micro .summary .huge { font-size: 20px; }
.micro .summary .metric { font-size: 16px; }
.micro .summary .dim { font-size: 12px; opacity: .85; }
.micro .day { font-size: 11px; }
.micro button.icon-action { padding: 5px; min-width: 18px; min-height: 18px; }
.micro button.icon-action image { -gtk-icon-size: 16px; }
.heading { font-size: 15px; }
.dim { opacity: .70; font-size: 12px; }
.warning { color: @window_fg_color; font-weight: 650; }
.day { font-size: 11px; opacity: .7; }
.bar { background: @accent_bg_color; border-radius: 3px 3px 0 0; }
.missing { background: @window_fg_color; opacity: .35; }
.table-group { font-size: 11px; font-weight: 650; opacity: .6; letter-spacing: 1px; }
.table-number { font-family: monospace; font-size: 21px; font-weight: 650; }
.history-bar trough, .history-bar progress { min-height: 4px; }
.chart-number { font-family: monospace; font-size: 11px; font-weight: 650; }
progressbar trough, progressbar progress { min-height: 7px; }
button.icon-action { background: transparent; border: none; box-shadow: none; padding: 9px; min-width: 20px; color: @window_fg_color; }
button.icon-action:hover, button.icon-action:active { color: @window_fg_color; }
button.icon-action:hover { background: alpha(@window_fg_color, .07); }
button.icon-action:active { background: alpha(@accent_bg_color, .15); }
window.widget-about, .widget-about .about-body { background-color: #fff4dc; color: #232a2f; }
.widget-about .about-body { font-family: monospace; font-size: 15px; }
.widget-about .about-body label { color: #232a2f; }
.widget-about .about-body .about-heading { font-size: 23px; font-weight: bold; }
.widget-about .about-body .about-note { color: #526573; font-size: 13px; }
.widget-about .about-body .about-link, .widget-about .about-body .about-link label { color: #3a6a96; }
.widget-about .about-body .about-link { padding: 4px; font-size: 13px; }
.widget-about .about-body .about-link:hover { background-color: alpha(#3a6a96, .10); }
"""


def text(value, style=None):
    label = Gtk.Label(label=value, xalign=0, wrap=True)
    if style:
        label.add_css_class(style)
    return label


def box(spacing=8, horizontal=False):
    return Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL if horizontal else Gtk.Orientation.VERTICAL, spacing=spacing)


def about_mark(filename, size):
    image = GdkPixbuf.Pixbuf.new_from_file_at_scale(str(ROOT / "Branding" / filename), size, size, True)
    return Gtk.Image.new_from_paintable(Gdk.Texture.new_for_pixbuf(image))


def about_link(label, url, child):
    link = Gtk.LinkButton(uri=url, halign=Gtk.Align.CENTER, tooltip_text=label)
    link.set_child(child)
    link.add_css_class("about-link")
    link.update_property([Gtk.AccessibleProperty.LABEL], [label])
    return link


def metric(label, number):
    content = box(4)
    content.append(text(label, "dim"))
    content.append(number)
    return content


class FlipTile(Gtk.Box):
    def __init__(self, name, caption):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=3)
        self.value = "--"
        self.previous = "--"
        self.started = None
        self.ticking = False
        self.face = Gtk.DrawingArea(content_width=36, content_height=32, hexpand=True)
        self.face.set_draw_func(self.draw)
        self.face.update_property([Gtk.AccessibleProperty.LABEL], [name + ": unavailable"])
        self.name = name
        self.append(self.face)
        label = Gtk.Label(label=caption, tooltip_text=name)
        label.add_css_class("day")
        self.append(label)

    def update(self, value, animate):
        if value == self.value:
            return
        self.previous, self.value = self.value, value
        self.face.update_property([Gtk.AccessibleProperty.LABEL], [self.name + ": " + value])
        self.started = time.monotonic() if animate else None
        self.face.queue_draw()
        if animate and not self.ticking:
            self.ticking = True
            self.face.add_tick_callback(self.tick)

    def tick(self, widget, frame):
        widget.queue_draw()
        if self.started is None or time.monotonic() - self.started >= .20:
            self.started = None
            self.ticking = False
            return False
        return True

    def draw(self, area, cr, width, height):
        progress = min(1, (time.monotonic()-self.started)/.20) if self.started is not None else 1
        self.draw_face(cr, width, height, self.value)
        if progress < 1 and self.previous != "--":
            # Old bottom stays visible while the top flap falls; new bottom unfolds afterwards.
            self.draw_half(cr, width, height, self.previous, False, 1)
            if progress < .5:
                self.draw_half(cr, width, height, self.previous, True, math.cos(progress*math.pi))
            else:
                self.draw_half(cr, width, height, self.value, False, math.sin((progress-.5)*math.pi))

    def draw_half(self, cr, width, height, value, top, scale):
        cr.save()
        cr.rectangle(0, 0 if top else height/2, width, height/2)
        cr.clip()
        cr.translate(0, height/2)
        cr.scale(1, max(.001, scale))
        cr.translate(0, -height/2)
        self.draw_face(cr, width, height, value)
        cr.restore()

    def draw_face(self, cr, width, height, value):
        cr.save()
        radius = 7
        for cx, cy, a, b in ((width-radius, radius, -math.pi/2, 0),
                              (width-radius, height-radius, 0, math.pi/2),
                              (radius, height-radius, math.pi/2, math.pi),
                              (radius, radius, math.pi, 3*math.pi/2)):
            cr.arc(cx, cy, radius, a, b)
        cr.close_path()
        dark = Adw.StyleManager.get_default().get_dark()
        top, bottom = (.15, .05) if dark else (.99, .89)
        edge = .02 if dark else .74
        gradient = cairo.LinearGradient(0, 0, 0, height)
        gradient.add_color_stop_rgb(0, top, top, top)
        gradient.add_color_stop_rgb(1, bottom, bottom, bottom)
        cr.set_source(gradient)
        cr.fill_preserve()
        cr.set_source_rgb(edge, edge, edge)
        cr.set_line_width(2)
        cr.stroke()
        layout = PangoCairo.create_layout(cr)
        layout.set_font_description(Pango.FontDescription(f"Monospace Bold {max(12, min(48, int(width * .34)))}"))
        layout.set_text(value, -1)
        tw, th = layout.get_pixel_size()
        cr.move_to((width-tw)/2, (height-th)/2)
        ink = 1 if dark else 0
        cr.set_source_rgb(ink, ink, ink)
        PangoCairo.show_layout(cr, layout)
        cr.set_source_rgb(edge, edge, edge)
        cr.set_line_width(2)
        cr.move_to(2, height/2)
        cr.line_to(width-2, height/2)
        cr.stroke()
        hinge = .42 if dark else .65
        cr.set_source_rgb(hinge, hinge, hinge)
        for x in (1, width-5):
            cr.rectangle(x, height/2-6, 4, 12)
            cr.fill()
        cr.restore()


class UsageWindow(Gtk.ApplicationWindow):
    def __init__(self, app):
        # Adw.ApplicationWindow imposes a minimum height larger than the strip.
        super().__init__(application=app, title="OpenAI Usage", default_height=62,
                         icon_name="biz.umbertogiacobbi.OpenAIUsage")
        self.state = {}
        self.settings = {}
        self.busy = False
        self.closed = False
        self.last_reset_refresh = 0.0
        self.allow_close = False
        self.close_dialog = None
        self.about_dialog = None
        self.tray = None
        self.labels = {}
        self.add_css_class("micro")
        self.set_decorated(False)
        self.set_resizable(False)
        self.connect("close-request", self.on_close)

        strip=box(12,True)
        for setter in (strip.set_margin_start,strip.set_margin_end):
            setter(10)
        for setter in (strip.set_margin_top,strip.set_margin_bottom):
            setter(7)
        strip.set_valign(Gtk.Align.CENTER)
        self.brand=Gtk.Image.new_from_file(str(ROOT/"Branding/app-icon-512.png"))
        self.brand.set_pixel_size(25)
        self.brand.set_tooltip_text("Connecting to Codex…")
        strip.append(self.brand)

        summary=Gtk.Grid(column_spacing=16, valign=Gtk.Align.CENTER)
        summary.add_css_class("summary")
        quota=box(4)
        quota.append(self.label("allowanceLabel","Weekly limit","dim"))
        quota.append(self.label("remaining","—","huge"))
        summary.attach(quota,0,0,1,1)
        summary.attach(metric("Tokens today",self.label("todayTokensLabel","—","metric")),1,0,1,1)
        summary.attach(metric("Credits left",self.label("creditsLabel","—","metric")),2,0,1,1)
        for i in range(3):
            heading=summary.get_child_at(i,0).get_first_child()
            heading.set_wrap(False)
        strip.append(summary)

        tile_row=box(5,True)
        tile_row.set_homogeneous(True)
        self.tiles = [FlipTile(name, caption) for name, caption in
                      (("Days", "DAYS"), ("Hours", "HRS"), ("Minutes", "MIN"), ("Seconds", "SEC"))]
        for tile in self.tiles:
            tile_row.append(tile)
        strip.append(tile_row)

        actions=box(0,True)
        self.problem=Gtk.Button(icon_name="dialog-warning-symbolic",tooltip_text="Connection status")
        self.problem.add_css_class("icon-action")
        self.problem.connect("clicked",self.show_settings)
        self.problem.set_visible(False)
        actions.append(self.problem)
        for icon,title,callback in [("emblem-system-symbolic","Settings",self.show_settings),
                                    ("help-about-symbolic","About OpenAI Usage",self.show_about),
                                    ("window-close-symbolic","Close",self.close)]:
            button=Gtk.Button(icon_name=icon,tooltip_text=title)
            button.add_css_class("icon-action")
            button.connect("clicked",lambda _,cb=callback:cb())
            actions.append(button)
        strip.append(actions)
        handle=Gtk.WindowHandle(child=strip)
        # Capture decorative content too, while preserving every button's click handling.
        drag=Gtk.GestureClick(button=Gdk.BUTTON_PRIMARY)
        drag.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
        drag.connect("pressed",self.begin_move)
        handle.add_controller(drag)
        self.set_child(handle)
        log_event("window.compact","Compact-only native widget; no full-mode or caption sizing controls.")
        try:
            self.tray=StatusTray({"open":self.present,"about":self.show_about,"settings":self.show_settings,
                                 "website":lambda:Gio.AppInfo.launch_default_for_uri("https://umbertogiacobbi.biz",None),
                                 "manifesto":lambda:Gio.AppInfo.launch_default_for_uri("https://umbertogiacobbi.biz/vibeware/manifesto",None),
                                 "exit":self.exit_application})
        except Exception as error:
            log_event("tray.unavailable",type(error).__name__)
        self.clock_id=GLib.timeout_add_seconds(1,self.tick)
        self.refresh_id=GLib.timeout_add_seconds(60,self.refresh)
        self.refresh()

    def label(self,key,initial="Unavailable",style=None):
        result=text(initial,style)
        result.set_wrap(False)
        self.labels[key]=result
        return result

    def refresh(self):
        if self.closed:
            return False
        if self.busy:
            return True
        self.busy=True
        self.brand.set_tooltip_text("Refreshing Codex account…")
        def worker():
            try:
                settings=invoke("settings")
                state=invoke()
            except (OSError,RuntimeError) as exc:
                settings,state={}, {"connected":False,"status":str(exc)}
            GLib.idle_add(self.received,settings,state)
        threading.Thread(target=worker,daemon=True).start()
        return True

    def received(self,settings,state):
        self.busy=False
        if self.closed:
            return False
        if self.state.get("connected")!=state.get("connected") or self.state.get("status")!=state.get("status"):
            log_event("connection.ready" if state.get("connected") else "connection.failed",state.get("status",""))
            if not state.get("connected"):
                notification=Gio.Notification.new("OpenAI Usage connection error")
                notification.set_body(state.get("status","Connection unavailable"))
                self.get_application().send_notification("connection-error",notification)
        self.settings,self.state=settings,state
        self.labels["remaining"].set_text(f"{state['remaining']:g}%" if state.get("remaining") is not None else "—")
        selected=next((w for w in state.get("windows",[]) if w["key"]==state.get("selectedWindow")),{})
        self.labels["allowanceLabel"].set_text("Weekly limit" if selected.get("minutes")==10080 else "Allowance limit")
        self.labels["todayTokensLabel"].set_text(state.get("todayTokensLabel","—"))
        self.labels["todayTokensLabel"].set_tooltip_text(state.get("todayTokensNote",""))
        self.labels["creditsLabel"].set_text(state.get("creditsLabel","—"))
        self.labels["remaining"].set_tooltip_text(state.get("resetLabel","") + "\n" + state.get("forecastDetail",""))
        low=state.get("remaining") is not None and state["remaining"]<=10
        (self.labels["remaining"].add_css_class if low else self.labels["remaining"].remove_css_class)("quota-low")
        self.tick()
        return False

    def tick(self):
        if self.closed:
            return False
        if GUARD is not None and GUARD.poll() is not None:
            log_event("guard.failure","Separate guard exited unexpectedly; closing the widget.")
            self.get_application().quit()
            return False
        end=self.state.get("resetsAt")
        seconds=max(0,int(end-time.time())) if end is not None else None
        values=[f"{seconds//86400:02}",f"{seconds//3600%24:02}",f"{seconds//60%60:02}",f"{seconds%60:02}"] if seconds is not None else ["--"]*4
        animations = Gtk.Settings.get_default().get_property("gtk-enable-animations")
        for tile,value in zip(self.tiles,values):
            tile.update(value,animations)
        status=self.state.get("status","Connecting to Codex…")
        updated=self.state.get("updatedAt")
        stale=False
        if updated:
            timestamp=dt.datetime.fromisoformat(updated.replace("Z","+00:00"))
            stale=time.time()-timestamp.timestamp()>180
            status += " · " + timestamp.astimezone().strftime("%H:%M:%S")
        if stale:
            status += " · STALE"
        self.brand.set_tooltip_text(status)
        self.problem.set_tooltip_text(status)
        self.problem.set_visible(not self.state.get("connected",False) or stale)
        if seconds==0 and time.monotonic()-self.last_reset_refresh>=60:
            self.last_reset_refresh=time.monotonic()
            self.refresh()
        return True

    def save_async(self, settings, dialog=None, error_label=None):
        def worker():
            try:
                invoke("save-settings", settings)
                error = None
            except (OSError, RuntimeError) as exc:
                error = str(exc)
            def done():
                if self.closed:
                    return False
                if error:
                    if error_label is not None:
                        error_label.set_text(error)
                    self.problem.set_visible(True)
                    self.problem.set_tooltip_text(error)
                    log_event("settings.failed",error)
                else:
                    self.settings = settings
                    if dialog:
                        dialog.close()
                    self.refresh()
                return False
            GLib.idle_add(done)
        threading.Thread(target=worker, daemon=True).start()

    def show_settings(self, *_):
        self.present()
        dialog = Gtk.Window(title="Settings", transient_for=self, modal=True, default_width=460, icon_name="biz.umbertogiacobbi.OpenAIUsage")
        content = box(12)
        for setter in (content.set_margin_start, content.set_margin_end, content.set_margin_top, content.set_margin_bottom):
            setter(24)
        executable = Gtk.Entry(text=self.settings.get("codexPath", "codex"))
        timezone = Gtk.Entry(text=self.settings.get("timeZoneId", "UTC"))
        content.append(text("Codex executable", "dim"))
        content.append(executable)
        content.append(text("Time zone", "dim"))
        content.append(timezone)
        content.append(text("Uses your existing Codex ChatGPT sign-in.", "dim"))
        error = text("", "warning")
        content.append(error)
        buttons = box(12, True)
        cancel = Gtk.Button(label="Cancel"); cancel.connect("clicked", lambda *_: dialog.close()); buttons.append(cancel)
        save = Gtk.Button(label="Save"); save.add_css_class("suggested-action"); buttons.append(save)
        def submit(*_):
            settings = dict(self.settings, codexPath=executable.get_text().strip(),
                            timeZoneId=timezone.get_text().strip())
            self.save_async(settings, dialog, error)
        save.connect("clicked", submit)
        content.append(buttons)
        dialog.set_child(content)
        dialog.present()

    def show_about(self, *_):
        self.present()
        if self.about_dialog is not None:
            self.about_dialog.present()
            return

        dialog = Gtk.Window(application=self.get_application(), title="About OpenAI Usage",
                            transient_for=self, destroy_with_parent=True, modal=True,
                            default_width=640, default_height=620, icon_name="biz.umbertogiacobbi.OpenAIUsage")
        self.about_dialog = dialog
        dialog.add_css_class("widget-about")
        content = box(24)
        content.add_css_class("about-body")
        content.set_halign(Gtk.Align.CENTER)
        for set_margin in (content.set_margin_start, content.set_margin_end, content.set_margin_top, content.set_margin_bottom):
            set_margin(24)

        def centered(value, style=None):
            label = Gtk.Label(label=value, wrap=True, justify=Gtk.Justification.CENTER,
                              max_width_chars=60, halign=Gtk.Align.CENTER)
            if style:
                label.add_css_class(style)
            return label

        website = "https://umbertogiacobbi.biz"
        content.append(about_mark("app-icon-512.png", 100))
        identity = box(5)
        identity.append(centered("OpenAI Usage", "about-heading"))
        identity.append(centered("Version 0.4.1", "about-note"))
        content.append(identity)
        content.append(centered("I built OpenAI Usage to see my Codex allowance, tokens and credits at a glance."))
        content.append(centered("I provide this software independently and as-is, without warranty.", "about-note"))
        content.append(centered("I'm Umberto Giacobbi · Proprietary"))

        details = Gtk.Expander(label="Technical details", halign=Gtk.Align.CENTER)
        diagnostic = centered("Separate watchdog: 1 GB aggregate RSS limit.\nLogs: " + str(state_dir() / "logs"), "about-note")
        diagnostic.set_selectable(True)
        details.set_child(diagnostic)
        content.append(details)
        content.append(about_link("VibeWare manifesto", website + "/vibeware/manifesto", about_mark("VibeWare.png", 64)))

        links = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, min_children_per_line=1, max_children_per_line=4,
                            column_spacing=8, row_spacing=4, halign=Gtk.Align.CENTER)
        for label, url, icon in (
            ("UmbertoGiacobbi.biz", website, "web-browser-symbolic"),
            ("Private source", "https://github.com/umbertotechnopreneur/Tools/tree/main/Widgets/OpenAI", "folder-remote-symbolic"),
            ("Terms", website + "/terms/", "text-x-generic-symbolic"),
            ("Privacy", website + "/privacy/", "changes-prevent-symbolic"),
        ):
            pair = box(6, horizontal=True)
            pair.append(Gtk.Image.new_from_icon_name(icon))
            pair.append(Gtk.Label(label=label))
            link = about_link(label, url, pair)
            if label == "Private source":
                link.set_tooltip_text("Private GitHub repository. Access is required.")
            links.append(link)
        content.append(links)

        scroll = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER, vscrollbar_policy=Gtk.PolicyType.AUTOMATIC)
        scroll.set_child(content)
        dialog.set_child(scroll)

        def forget_dialog(*_):
            self.about_dialog = None
            return False

        def dismiss(_controller, key, *_):
            if key == Gdk.KEY_Escape:
                dialog.close()
                return True
            return False

        dialog.connect("close-request", forget_dialog)
        keys = Gtk.EventControllerKey()
        keys.connect("key-pressed", dismiss)
        dialog.add_controller(keys)
        dialog.present()

    def begin_move(self, gesture, presses, x, y):
        """Ask the compositor to drag from any non-button part of the micro strip."""
        if self.closed or presses != 1:
            return
        target=gesture.get_widget().pick(x,y,Gtk.PickFlags.DEFAULT)
        while target is not None:
            if isinstance(target,Gtk.Button):
                gesture.set_state(Gtk.EventSequenceState.DENIED)
                return
            target=target.get_parent()
        event=gesture.get_current_event()
        surface=self.get_surface()
        if event is None or surface is None:
            return
        valid, surface_x, surface_y=event.get_position()
        device=event.get_device()
        if valid and device is not None:
            gesture.set_state(Gtk.EventSequenceState.CLAIMED)
            surface.begin_move(device,Gdk.BUTTON_PRIMARY,surface_x,surface_y,event.get_time())

    def exit_application(self):
        self.allow_close = True
        self.close()

    def on_close(self, *_):
        if not self.allow_close:
            if self.close_dialog is not None:
                return True
            tray_available = self.tray is not None and self.tray.available
            message = "Keep monitoring in the notification area, or exit the app." if tray_available else "Your desktop's notification area is unavailable. You can exit or keep the window open."
            dialog = Adw.MessageDialog(transient_for=self, modal=True, heading="Close OpenAI Usage?", body=message)
            if tray_available:
                dialog.add_response("tray", "Minimize to tray")
            dialog.add_response("exit", "Exit")
            dialog.add_response("cancel", "Cancel")
            dialog.set_close_response("cancel")
            dialog.set_default_response("tray" if tray_available else "cancel")
            def response(_, result):
                self.close_dialog = None
                if result == "exit":
                    self.exit_application()
                elif result == "tray" and self.tray is not None and self.tray.available:
                    self.set_visible(False)
            dialog.connect("response", response)
            self.close_dialog = dialog
            dialog.present()
            return True
        self.closed = True
        if self.tray:
            self.tray.close()
        GLib.source_remove(self.clock_id)
        GLib.source_remove(self.refresh_id)
        return False


class UsageApp(Adw.Application):
    def __init__(self):
        super().__init__(application_id="biz.umbertogiacobbi.OpenAIUsage")
        self.window = None

    def do_activate(self):
        if self.window is None:
            manager = Adw.StyleManager.get_default()
            manager.set_color_scheme(Adw.ColorScheme.DEFAULT)
            provider = Gtk.CssProvider()
            provider.load_from_data(CSS)
            Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
            Gtk.IconTheme.get_for_display(Gdk.Display.get_default()).add_search_path(str(ROOT/"Branding/icons"))
            self.window = UsageWindow(self)
            manager.connect("notify::dark", self.redraw_countdown)
        self.window.present()

    def redraw_countdown(self, *_):
        for tile in self.window.tiles:
            tile.face.queue_draw()


if __name__ == "__main__":
    GUARD = start_watchdog()
    raise SystemExit(UsageApp().run(sys.argv))
