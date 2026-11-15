"""Flat Tk Studio workspace; native renderers own all device pixels."""

from concurrent.futures import ThreadPoolExecutor
from bisect import bisect_right
from copy import deepcopy
import json
from pathlib import Path
import queue
import shutil
import subprocess
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageTk

from cody_native_runtime import ROOT
from cody_semantic_palette import studio_hex
from cody_studio_catalog import inventory_label, entry_for, load_catalog, resolve_source, routes_for
from cody_studio_atlas import AtlasJob, BuildWatcher, latest_atlas_path, load_atlas, verify_contract_inputs
from cody_studio_runner import StudioRunner, load_capture, verify_current_build
from cody_studio_comparison import compare_captures, comparison_image, load_review, preserve_baseline, save_review
from cody_studio_scenario import default_scenario, load_scenario, save_scenario, validate_scenario
from cody_studio_timeline import (source_timeline_scenario, timeline_capture, timeline_plan,
                                  worker_timeline_scenario)
from runtime_paths import resolve_runtime_output

BG, FG, MUTED = (studio_hex('host_ui', key) for key in ('background', 'text', 'muted'))
FIELD, ORANGE = studio_hex('host_ui', 'field'), studio_hex('setup_warning')
STATE_PATH = ROOT / 'artifacts/simulator/studio/ui-state.json'


# parent: Host widget container.
# text: Host-only caption, never device content.
# size: Font size in points.
# bold: Heading emphasis.
# options: Standard Tk label options.
# Raises tk.TclError for invalid widget configuration.
def caption(parent, text='', size=11, bold=False, **options):
    return tk.Label(parent, text=text, bg=BG, fg=FG, anchor='w', justify='left',
                    font=('TkDefaultFont', size, 'bold' if bold else 'normal'), **options)


# parent: Host action container.
# text: Explicit action caption.
# command: User-activated callback.
# primary: Whether orange identifies this primary action.
# Raises tk.TclError for invalid widget configuration.
def action(parent, text, command, primary=False):
    return tk.Button(parent, text=text, command=command, bg=BG, fg=ORANGE if primary else FG,
                     activebackground=FIELD, activeforeground=ORANGE, relief='flat', bd=0,
                     highlightbackground=BG, highlightcolor=ORANGE, highlightthickness=2,
                     takefocus=True, anchor='w', font=('TkDefaultFont', 11, 'bold' if primary else 'normal'))


class StudioWindow:
    """One controlled native session, with immutable captures and explicit review."""

    # root: Tk interpreter owned by the existing native window entry point.
    # device: Native runtime created with deterministic_clock=True before startup.
    # scenario: Validated versioned pilot scenario with explicit page and service inputs.
    # Raises catalog/schema/Tk errors; never selects an alternate renderer.
    def __init__(self, root, device, scenario):
        self.root, self.scenario = root, deepcopy(scenario)
        self.runner = StudioRunner(device)
        self.current = self.baseline = None
        self.baseline_error = None
        self.unavailable_baseline = None
        self.records, self.sections, self.photos = [], {}, []
        self.thumbnail_controls = []
        self.thumbnail_records = []
        self.busy = self.closed = False
        self.focus_reveal_id = None
        self.timeline_timer = None
        self.timeline_playing = False
        self.timeline_scenario = None
        self.timeline_contract = None
        self.timeline_target = None
        self.timeline_deadline = None
        self.timeline_position = 0
        self.live_process = self.live_directory = self.live_timer = self.live_image = None
        self.live_started = None
        self.live_caption_backup = self.live_title = None
        self.cue_job = None
        self.route_records = {}
        self.declared_routes = []
        self.atlas_job = self.atlas_manifest_path = None
        self.atlas_watcher = None
        self.atlas_watch_deadline = 0
        self.atlas_pending_build = False
        self.atlas_last_watch_error = None
        self.atlas_exporting = False
        self.bottom_content_height = 280
        self.atlas_results = queue.Queue()
        self.atlas_executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix='cody-atlas-export')
        self.results = queue.Queue()
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix='cody-studio')
        self.status = tk.StringVar(value='Preparing native Studio case…')
        self.preferences = self.read_preferences()
        self.restore_baseline()
        root.title('Cody Studio — SIMULATED')
        root.configure(bg=BG)
        root.geometry('1440x960')
        root.minsize(720, 540)
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.build()
        self.restore_atlas()
        self.show_scenario()
        # Descendants include this Toplevel in their normal bindtags, including
        # controls created by later scenario changes. Other windows are untouched.
        self.focus_binding = root.bind('<FocusIn>', self.queue_focus_reveal, add='+')
        self.poll_id = root.after(40, self.poll)
        self.initial_apply_id = root.after_idle(self.apply)

    # event: Focus transition in this window's normal Toplevel bindtag.
    # Coalesces ancestor/child events and reveals only fields inside our scroll areas.
    def queue_focus_reveal(self, event):
        if self.closed or self.focus_viewport(event.widget) is None:
            return
        if self.focus_reveal_id is not None:
            self.root.after_cancel(self.focus_reveal_id)
        self.focus_reveal_id = self.root.after_idle(self.reveal_focus)

    # widget: Focused widget whose parent chain is inspected without changing focus.
    # Returns only this window's inspector/bottom viewport, or None for other controls.
    def focus_viewport(self, widget):
        for viewport in (self.inspector_canvas, self.bottom_canvas):
            current = getattr(widget, 'master', None)
            while current is not None:
                if current is viewport:
                    return viewport
                current = getattr(current, 'master', None)
        return None

    # No parameters: reveal the final focused control after pending layout changes.
    # The same logic handles long scalar forms and lower Atlas thumbnail rows.
    def reveal_focus(self):
        self.focus_reveal_id = None
        if self.closed:
            return
        widget = self.root.focus_get()
        viewport = self.focus_viewport(widget)
        if viewport is None or not widget.winfo_ismapped():
            return
        bounds = viewport.bbox('all')
        if bounds is None or bounds[3] <= bounds[1]:
            return
        height = viewport.winfo_height()
        visible_top = viewport.canvasy(0)
        top = viewport.canvasy(widget.winfo_rooty() - viewport.winfo_rooty())
        bottom = top + widget.winfo_height()
        margin = 4
        if top < visible_top + margin:
            desired_top = top - margin
        elif bottom > visible_top + height - margin:
            desired_top = bottom + margin - height
        else:
            return
        desired_top = max(bounds[1], min(desired_top, max(bounds[1], bounds[3] - height)))
        viewport.yview_moveto((desired_top - bounds[1]) / (bounds[3] - bounds[1]))

    # No parameters: restore host visibility preferences, with visible parse errors.
    def read_preferences(self):
        try:
            path = resolve_runtime_output(STATE_PATH)
            value = json.loads(path.read_text(encoding='utf-8'))
            if not isinstance(value, dict) or value.get('schema') != 'cody.studio.ui.v1':
                raise ValueError('Unsupported Studio UI preferences')
            return value
        except FileNotFoundError:
            return {}
        except (OSError, ValueError) as error:
            self.status.set(f'UI preferences unavailable: {error}')
            return {}

    # No parameters: persist only UI preferences, never captures or baselines.
    def persist(self):
        try:
            path = resolve_runtime_output(STATE_PATH)
            path.parent.mkdir(parents=True, exist_ok=True)
            value = {'schema': 'cody.studio.ui.v1',
                     'expanders': {key: data[2] for key, data in self.sections.items()},
                     'left': self.left_visible.get(), 'right': self.right_visible.get(),
                     'atlas_suite': self.atlas_suite.get(),
                     'baseline': (str(self.baseline.metadata_path.relative_to(ROOT)) if self.baseline
                                  else self.unavailable_baseline)}
            temporary = path.with_suffix('.json.tmp')
            temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
            temporary.replace(path)
        except (OSError, ValueError) as error:
            self.status.set(f'UI preferences not saved: {error}')

    # No parameters: restore only an explicitly preserved baseline from artifacts.
    # Exceptions: Corrupt or missing evidence is reported and never substituted.
    def restore_baseline(self):
        stored = self.preferences.get('baseline')
        if stored is None:
            return
        try:
            if not isinstance(stored, str) or Path(stored).is_absolute():
                raise ValueError('Baseline preference requires a repository-relative path')
            record = load_capture(ROOT / stored)
            if record.metadata.get('baseline') is not True:
                raise ValueError('Baseline preference does not identify a preserved baseline')
            self.baseline = record
        except (OSError, ValueError) as error:
            self.baseline_error = str(error)
            self.unavailable_baseline = stored
            self.status.set(f'Baseline unavailable: {error}')

    # parent: Flat section owner.
    # name: Stable preference key and heading.
    # opened: Default visibility.
    # Returns the section body, without a decorative enclosing container.
    def section(self, parent, name, opened):
        heading = action(parent, name, lambda: self.toggle(name))
        heading.configure(font=('TkDefaultFont', 16, 'bold'))
        heading.pack(fill='x', pady=(12, 3))
        body = tk.Frame(parent, bg=BG)
        settings = self.preferences.get('expanders', {})
        opened = settings.get(name, opened) is True if isinstance(settings, dict) else opened
        self.sections[name] = [heading, body, opened, name]
        self.refresh_section(name)
        return body

    # name: Stable section key.
    # summary: Optional evidence-qualified replacement caption.
    def refresh_section(self, name, summary=None):
        data = self.sections[name]
        if summary is not None:
            data[3] = summary
        data[0].configure(text=('▾  ' if data[2] else '▸  ') + data[3])
        if data[2]:
            data[1].pack(fill='x', after=data[0], padx=(20, 0))
        else:
            data[1].pack_forget()

    # name: Stable section key changed by keyboard or pointer activation.
    def toggle(self, name):
        self.sections[name][2] = not self.sections[name][2]
        self.refresh_section(name)
        if name in ('Animation', 'Routes', 'Atlas'):
            self.arrange_bottom()
            self.bottom_canvas.yview_moveto(0)
        self.persist()

    # No parameters: collapsed lower sections share one compact row so small
    # windows retain an inspectable device preview and keyboard-accessible titles.
    def arrange_bottom(self):
        names = ('Animation', 'Routes', 'Atlas')
        compact = all(not self.sections[name][2] for name in names)
        width = max(1, self.bottom_canvas.winfo_width())
        for name in names:
            heading, body, opened, _ = self.sections[name]
            heading.pack_forget()
            body.pack_forget()
            heading.configure(font=('TkDefaultFont', 11 if compact else 16, 'bold'),
                              wraplength=max(100, width // 3 - 18) if compact else 0)
            if compact:
                heading.pack(side='left', fill='both', expand=True, pady=(6, 3))
            else:
                heading.pack(fill='x', pady=(12, 3))
                if opened:
                    body.pack(fill='x', padx=(20, 0))

    # No parameters: construct the approved host composition with visible focus.
    def build(self):
        style = ttk.Style(self.root)
        style.theme_use('clam')
        style.configure('Studio.Treeview', background=BG, fieldbackground=BG, foreground=FG,
                        borderwidth=0, rowheight=28, font=('TkDefaultFont', 11))
        style.map('Studio.Treeview', background=[('selected', FIELD)], foreground=[('selected', ORANGE)])
        style.layout('Studio.Treeview', [('Treeview.treearea', {'sticky': 'nswe'})])
        style.configure('Studio.TCombobox', fieldbackground=FIELD, background=FIELD,
                        foreground=FG, arrowcolor=FG, borderwidth=0, padding=3)
        style.map('Studio.TCombobox', fieldbackground=[('readonly', FIELD)],
                  foreground=[('readonly', FG)], selectbackground=[('readonly', FIELD)],
                  selectforeground=[('readonly', FG)])
        self.root.option_add('*TCombobox*Listbox.background', FIELD)
        self.root.option_add('*TCombobox*Listbox.foreground', FG)
        self.root.option_add('*TCombobox*Listbox.selectBackground', BG)
        self.root.option_add('*TCombobox*Listbox.selectForeground', ORANGE)
        header = tk.Frame(self.root, bg=BG)
        header.pack(fill='x', padx=24, pady=12)
        caption(header, 'Cody Studio', 22, True).pack(side='left')
        badge = caption(header, 'SIMULATED', 11, True)
        badge.configure(fg=ORANGE)
        badge.pack(side='left', padx=24)
        self.left_visible = tk.BooleanVar(value=self.preferences.get('left', True) is True)
        self.right_visible = tk.BooleanVar(value=self.preferences.get('right', True) is True)
        for text, variable in (('Catalog', self.left_visible), ('Inspector', self.right_visible)):
            tk.Checkbutton(header, text=text, variable=variable, command=self.columns,
                           bg=BG, fg=FG, selectcolor=FIELD, activebackground=BG,
                           activeforeground=ORANGE, highlightcolor=ORANGE).pack(side='right')
        work = tk.Frame(self.root, bg=BG)
        work.pack(fill='both', expand=True, padx=24)
        work.columnconfigure(1, weight=1)
        work.rowconfigure(0, weight=1)
        self.left, self.center = tk.Frame(work, bg=BG), tk.Frame(work, bg=BG)
        self.left.grid(row=0, column=0, sticky='nsew', padx=(0, 20))
        self.center.grid(row=0, column=1, sticky='nsew')
        # Scrolling the inspector keeps all controls reachable on smaller hosts.
        self.right_host = tk.Frame(work, bg=BG)
        self.right_host.grid(row=0, column=2, sticky='nsew', padx=(20, 0))
        scroll = tk.Scrollbar(self.right_host, orient='vertical')
        scroll.pack(side='right', fill='y')
        pane = tk.Canvas(self.right_host, bg=BG, width=300, highlightthickness=0, yscrollcommand=scroll.set)
        self.inspector_canvas = pane
        pane.pack(side='left', fill='both', expand=True)
        scroll.configure(command=pane.yview)
        right = tk.Frame(pane, bg=BG)
        pane.create_window(0, 0, window=right, anchor='nw', width=300)
        right.bind('<Configure>', lambda event: pane.configure(scrollregion=pane.bbox('all')))
        caption(self.left, 'Screen catalog', 19, True).pack(anchor='w', pady=(0, 12))
        caption(self.left, 'Search screens', 10).pack(anchor='w')
        self.search = tk.StringVar()
        self.field(self.left, self.search).pack(fill='x', pady=(4, 8))
        self.filter = tk.StringVar(value='All')
        ttk.Combobox(self.left, textvariable=self.filter,
                     values=('All', 'Supported', 'Runtime full', 'Presenter isolated', 'Profile conditional',
                             'Unsupported', 'Not applicable', 'Changed', 'Needs review'),
                     state='readonly', width=25, style='Studio.TCombobox').pack(fill='x')
        self.catalog_message = tk.StringVar(value='Changed and Needs review use captures loaded in this session.')
        caption(self.left, textvariable=self.catalog_message, size=9, wraplength=240).pack(fill='x', pady=(4, 0))
        catalog_list = tk.Frame(self.left, bg=BG)
        catalog_list.pack(fill='both', expand=True, pady=10)
        self.catalog_scroll = tk.Scrollbar(catalog_list, orient='vertical')
        self.catalog_scroll.pack(side='right', fill='y')
        self.tree = ttk.Treeview(catalog_list, show='tree', style='Studio.Treeview', height=15,
                                 yscrollcommand=self.catalog_scroll.set)
        self.tree.pack(side='left', fill='both', expand=True)
        self.catalog_scroll.configure(command=self.tree.yview)
        self.tree.bind('<<TreeviewSelect>>', self.selection)
        self.search.trace_add('write', self.catalog)
        self.filter.trace_add('write', self.catalog)
        self.catalog()
        self.detail = tk.StringVar()
        caption(self.left, textvariable=self.detail, wraplength=240).pack(fill='x')
        action(self.left, 'Open destination', self.open_destination, True).pack(fill='x', pady=8)
        action(self.left, 'Open source', self.open_source).pack(fill='x')
        self.frame_title = tk.StringVar(value='Preparing native frame')
        self.frame_subtitle = tk.StringVar(value='Native C/C++ · synthetic scenario inputs')
        caption(self.center, textvariable=self.frame_title, size=28, bold=True).pack(anchor='w')
        caption(self.center, textvariable=self.frame_subtitle, size=11).pack(anchor='w', pady=(4, 10))
        self.baseline_warning_label = caption(self.center, size=10, wraplength=480)
        self.update_baseline_warning()
        modes = tk.Frame(self.center, bg=BG)
        modes.pack(fill='x')
        self.mode = tk.StringVar(value='current')
        self.mode_controls = []
        for text, mode in (('Current', 'current'), ('Before / After', 'side-by-side'),
                           ('Overlay', 'overlay'), ('Difference', 'difference')):
            control = tk.Radiobutton(modes, text=text, value=mode, variable=self.mode, command=self.mode_changed,
                           bg=BG, fg=FG, selectcolor=FIELD, indicatoron=False, relief='flat', bd=0,
                           activeforeground=ORANGE, activebackground=FIELD, highlightcolor=ORANGE,
                           highlightbackground=BG, highlightthickness=2, padx=7, pady=4)
            control.pack(side='left')
            self.mode_controls.append((mode, control))
        self.canvas = tk.Canvas(self.center, bg=BG, highlightthickness=0, height=180)
        self.canvas.pack(fill='both', expand=True, pady=8)
        self.canvas.bind('<Configure>', lambda event: self.draw())
        bottom_host = self.bottom_host = tk.Frame(self.center, bg=BG)
        bottom_host.pack(side='bottom', fill='x', before=self.canvas)
        bottom_scroll = tk.Scrollbar(bottom_host, orient='vertical')
        bottom_scroll.pack(side='right', fill='y')
        self.bottom_canvas = tk.Canvas(bottom_host, bg=BG, highlightthickness=0, height=280,
                                       yscrollcommand=bottom_scroll.set)
        self.bottom_canvas.pack(fill='x', expand=True)
        bottom_scroll.configure(command=self.bottom_canvas.yview)
        bottom = tk.Frame(self.bottom_canvas, bg=BG)
        bottom_item = self.bottom_canvas.create_window(0, 0, window=bottom, anchor='nw')

        # event: Actual size after an expander changes; keep content reachable.
        def resize_bottom(event):
            self.bottom_content_height = event.height
            self.bottom_canvas.configure(scrollregion=self.bottom_canvas.bbox('all'))
            self.resize_bottom_viewport()

        bottom.bind('<Configure>', resize_bottom)
        self.bottom_canvas.bind('<Configure>', lambda event: self.bottom_canvas.itemconfigure(bottom_item, width=event.width))
        self.bottom_canvas.bind('<Configure>', lambda event: self.arrange_bottom(), add='+')
        self.center.bind('<Configure>', self.resize_bottom_viewport)
        self.baseline_warning_label.bind('<Configure>', self.resize_bottom_viewport)
        modes.bind('<Configure>', self.resize_bottom_viewport)
        animation = self.section(bottom, 'Animation', False)
        self.animation_status = tk.StringVar(value='No native source animation selected.')
        caption(animation, textvariable=self.animation_status, wraplength=580).pack(anchor='w')
        clock_row = tk.Frame(animation, bg=BG)
        clock_row.pack(fill='x')
        caption(clock_row, 'Clock', 10).pack(side='left')
        self.animation_clock = tk.StringVar(value='Source frames')
        self.clock_control = ttk.Combobox(clock_row, textvariable=self.animation_clock,
                                          values=('Source frames',), state='readonly', width=18,
                                          style='Studio.TCombobox')
        self.clock_control.pack(side='left', padx=6)
        self.clock_control.bind('<<ComboboxSelected>>', self.change_timeline_clock)
        animation_actions = tk.Frame(animation, bg=BG)
        animation_actions.pack(fill='x')
        self.play_button = action(animation_actions, 'Play', self.play_timeline, True)
        self.play_button.pack(side='left', padx=(0, 12))
        self.live_button = action(animation_actions, 'Boot replay', self.play_live)
        self.live_button.pack(side='left', padx=(0, 12))
        animation_steps = tk.Frame(animation, bg=BG)
        animation_steps.pack(fill='x')
        self.pause_button = action(animation_steps, 'Pause', self.pause_timeline)
        self.pause_button.pack(side='left', padx=(0, 12))
        self.step_button = action(animation_steps, 'Step', self.step_timeline)
        self.step_button.pack(side='left', padx=(0, 12))
        speed_row = tk.Frame(animation, bg=BG)
        speed_row.pack(fill='x')
        caption(speed_row, 'Speed', 10).pack(side='left')
        self.animation_speed = tk.StringVar(value='1x')
        self.speed_control = ttk.Combobox(speed_row, textvariable=self.animation_speed,
                                          values=('0.5x', '1x', '2x'), state='readonly', width=5,
                                          style='Studio.TCombobox')
        self.speed_control.pack(side='left', padx=6)
        keyframes = tk.Frame(animation, bg=BG)
        keyframes.pack(fill='x')
        caption(keyframes, 'Keyframe (ms)', 10).pack(side='left')
        self.animation_keyframe = tk.StringVar()
        self.keyframe_control = ttk.Combobox(keyframes, textvariable=self.animation_keyframe,
                                             state='readonly', width=9, style='Studio.TCombobox')
        self.keyframe_control.pack(side='left', padx=6)
        self.seek_button = action(keyframes, 'Show keyframe', self.seek_timeline)
        self.seek_button.pack(side='left')
        self.refresh_section('Animation', 'Animation · availability pending')
        from cody_studio_audio import CUE_KEYS
        caption(animation, 'Authored audio cue', 11, True).pack(anchor='w', pady=(10, 3))
        self.cue_choice = tk.StringVar(value='pet:tap')
        self.cue_control = ttk.Combobox(animation, textvariable=self.cue_choice, values=CUE_KEYS,
                                         state='readonly', width=28, style='Studio.TCombobox')
        self.cue_control.pack(anchor='w')
        cue_actions = tk.Frame(animation, bg=BG)
        cue_actions.pack(fill='x')
        self.cue_play_button = action(cue_actions, 'Audition cue', self.play_cue_audition, True)
        self.cue_play_button.pack(side='left', padx=(0, 12))
        self.cue_stop_button = action(cue_actions, 'Stop cue', self.stop_cue_audition)
        self.cue_stop_button.configure(state='disabled')
        self.cue_stop_button.pack(side='left')
        self.cue_status = tk.StringVar(value='26 authored cues · uses Scenario Sounds, volume and quiet hours. Audition is not route evidence.')
        caption(animation, textvariable=self.cue_status, size=10, wraplength=580).pack(anchor='w')
        routes = self.section(bottom, 'Routes', True)
        self.route_summary = tk.StringVar()
        self.route_choice = tk.StringVar()
        self.route_control = ttk.Combobox(routes, textvariable=self.route_choice, state='readonly',
                                           style='Studio.TCombobox')
        self.route_control.pack(fill='x')
        self.route_control.bind('<<ComboboxSelected>>', self.route_selected)
        caption(routes, textvariable=self.route_summary, wraplength=590).pack(anchor='w')
        route_actions = tk.Frame(routes, bg=BG)
        route_actions.pack(fill='x')
        self.replay_button = action(route_actions, 'Replay route', self.replay_route, True)
        self.replay_button.pack(side='left', padx=(0, 15))
        self.route_source_button = action(route_actions, 'Open route source', self.open_route_source)
        self.route_source_button.pack(side='left')
        atlas = self.section(bottom, 'Atlas', True)
        suite_row = tk.Frame(atlas, bg=BG)
        suite_row.pack(fill='x')
        caption(suite_row, 'Suite', 10).pack(side='left')
        suites = ('Pilot (10)', 'Supported screens', 'Filtered catalog')
        saved_suite = self.preferences.get('atlas_suite', suites[0])
        self.atlas_suite = tk.StringVar(value=saved_suite if saved_suite in suites else suites[0])
        self.atlas_suite_control = ttk.Combobox(suite_row, textvariable=self.atlas_suite, values=suites,
                                               state='readonly', width=21, style='Studio.TCombobox')
        self.atlas_suite_control.pack(side='left', padx=6)
        self.atlas_suite_control.bind('<<ComboboxSelected>>', lambda event: self.persist())
        atlas_actions = tk.Frame(atlas, bg=BG)
        atlas_actions.pack(fill='x')
        self.atlas_generate_button = action(atlas_actions, 'Generate atlas', self.generate_atlas, True)
        self.atlas_generate_button.pack(side='left', padx=(0, 12))
        self.atlas_cancel_button = action(atlas_actions, 'Cancel', self.cancel_atlas)
        self.atlas_cancel_button.configure(state='disabled')
        self.atlas_cancel_button.pack(side='left', padx=(0, 12))
        self.atlas_export_button = action(atlas_actions, 'Export PDF…', self.export_atlas)
        self.atlas_export_button.configure(state='disabled')
        self.atlas_export_button.pack(side='left')
        self.atlas_auto = tk.BooleanVar(value=False)
        self.atlas_watch_control = tk.Checkbutton(atlas, text='Regenerate selected suite after a validated native build',
                       variable=self.atlas_auto, command=self.change_atlas_watch,
                       bg=BG, fg=FG, selectcolor=FIELD, activebackground=BG,
                       activeforeground=ORANGE, highlightcolor=ORANGE, wraplength=500,
                       justify='left', anchor='w')
        self.atlas_watch_control.pack(anchor='w', fill='x')
        self.atlas_status = tk.StringVar(value='No published atlas loaded. Generation uses a separate native process.')
        atlas_status_label = caption(atlas, textvariable=self.atlas_status, size=10, wraplength=580)
        atlas_status_label.pack(anchor='w', fill='x', pady=4)
        self.atlas_case_status = tk.StringVar()
        atlas_case_label = caption(atlas, textvariable=self.atlas_case_status, size=9, wraplength=580)
        atlas_case_label.pack(anchor='w', fill='x')

        # event: Actual available width; keep progress and build-watcher captions readable.
        def resize_atlas(event):
            width = max(120, event.width - 8)
            atlas_status_label.configure(wraplength=width)
            atlas_case_label.configure(wraplength=width)
            self.atlas_watch_control.configure(wraplength=max(100, width - 28))

        atlas.bind('<Configure>', resize_atlas)
        atlas_log_host = tk.Frame(atlas, bg=BG)
        atlas_log_host.pack(fill='x', pady=(3, 6))
        atlas_log_scroll = tk.Scrollbar(atlas_log_host, orient='vertical')
        atlas_log_scroll.pack(side='right', fill='y')
        self.atlas_log = tk.Text(atlas_log_host, height=3, width=40, bg=BG, fg=MUTED,
                                 relief='flat', bd=0, wrap='word', state='disabled',
                                 highlightbackground=BG, highlightcolor=ORANGE,
                                 highlightthickness=1, takefocus=True,
                                 font=('TkDefaultFont', 9), yscrollcommand=atlas_log_scroll.set)
        self.atlas_log.pack(side='left', fill='x', expand=True)
        atlas_log_scroll.configure(command=self.atlas_log.yview)
        self.thumbnails = tk.Frame(atlas, bg=BG)
        self.thumbnails.pack(fill='x')
        action(atlas, 'Open saved capture…', self.open_capture).pack(anchor='w')
        comparison = self.section(right, 'Comparison', False)
        action(comparison, 'Choose baseline…', self.choose_baseline).pack(fill='x')
        action(comparison, 'Preserve current as baseline', self.keep_baseline).pack(fill='x')
        action(comparison, 'Clear baseline selection', self.clear_baseline).pack(fill='x')
        self.evidence_status = tk.StringVar(value='Acquisition: awaiting capture\nDifference: not comparable\nReview: unreviewed')
        caption(comparison, textvariable=self.evidence_status, wraplength=275).pack(fill='x', pady=5)
        self.compare_status = tk.StringVar(value='Not comparable · choose a baseline')
        caption(comparison, textvariable=self.compare_status, wraplength=275).pack(fill='x', pady=5)
        caption(comparison, 'Overlay opacity', 10).pack(anchor='w')
        self.opacity = tk.DoubleVar(value=.5)
        self.opacity_scale = tk.Scale(comparison, from_=0, to=1, resolution=.05, orient='horizontal',
                                      variable=self.opacity, command=lambda value: self.draw(),
                                      bg=BG, fg=FG, troughcolor=MUTED, activebackground=BG,
                                      highlightbackground=BG, highlightcolor=ORANGE,
                                      highlightthickness=2, takefocus=True)
        self.opacity_scale.pack(fill='x')
        scenario = self.section(right, 'Scenario', True)
        self.scenario_title = tk.StringVar()
        caption(scenario, textvariable=self.scenario_title, size=11, bold=True, wraplength=275).pack(anchor='w')
        caption(scenario, 'Variant', 10).pack(anchor='w', pady=(4, 0))
        self.variant = tk.StringVar()
        self.variant_control = ttk.Combobox(scenario, textvariable=self.variant, state='readonly',
                                           style='Studio.TCombobox')
        self.variant_control.pack(fill='x')
        self.inputs = {}
        for key, text in (('battery', 'Battery (%)'), ('network', 'Network'), ('ssid', 'Network name'),
                          ('rssi', 'Signal (dBm)'), ('epoch', 'Fixed civil clock · UTC epoch seconds'),
                          ('offset', 'UTC offset (minutes)'), ('brightness', 'Brightness (10–100%)'),
                          ('orientation', 'Orientation (degrees)')):
            caption(scenario, text, 10).pack(anchor='w', pady=(4, 0))
            variable = self.inputs[key] = tk.StringVar()
            if key in ('network', 'orientation'):
                values = ('Connected', 'Disconnected') if key == 'network' else ('0', '180')
                ttk.Combobox(scenario, textvariable=variable, values=values, state='readonly',
                             style='Studio.TCombobox').pack(fill='x')
            else:
                self.field(scenario, variable).pack(fill='x')
        self.auto_rotate = tk.BooleanVar()
        tk.Checkbutton(scenario, text='Auto rotate (simulated)', variable=self.auto_rotate,
                       bg=BG, fg=FG, selectcolor=FIELD, activebackground=BG,
                       activeforeground=ORANGE, highlightcolor=ORANGE).pack(anchor='w', pady=4)
        self.sounds_enabled = tk.BooleanVar()
        tk.Checkbutton(scenario, text='Sounds enabled', variable=self.sounds_enabled,
                       bg=BG, fg=FG, selectcolor=FIELD, activebackground=BG,
                       activeforeground=ORANGE, highlightcolor=ORANGE).pack(anchor='w', pady=4)
        caption(scenario, 'Playback volume (0–100%)', 10).pack(anchor='w')
        self.playback_volume = tk.StringVar()
        self.field(scenario, self.playback_volume).pack(fill='x')
        self.audio_status = tk.StringVar(value='Audio backend status pending')
        caption(scenario, textvariable=self.audio_status, size=10, wraplength=275).pack(anchor='w', pady=4)
        caption(scenario, 'Boot replay and cue audition use these sound settings. Turn Sounds off before Apply and capture or timeline capture.',
                10, wraplength=275).pack(anchor='w')
        self.refresh_audio_status()
        caption(scenario, 'Logical raster before panel rotation.\nAuto rotation has no simulated gravity input.',
                10, wraplength=275).pack(anchor='w')
        self.case_fields_host = tk.Frame(scenario, bg=BG)
        self.case_fields_host.pack(fill='x', pady=(8, 0))
        self.case_fields = {}
        self.case_field_widgets = {}
        self.apply_button = action(scenario, 'Apply and capture', self.apply, True)
        self.apply_button.pack(fill='x')
        action(scenario, 'Open scenario…', self.open_scenario).pack(anchor='w')
        action(scenario, 'Save scenario…', self.save_scenario).pack(anchor='w')
        changes = self.section(right, 'Changes', False)
        caption(changes, textvariable=self.compare_status, wraplength=275).pack(anchor='w')
        self.refresh_section('Changes', 'Changes · not compared')
        notes = self.section(right, 'Review notes', False)
        self.note = tk.Text(notes, bg=FIELD, fg=FG, insertbackground=FG, width=27, height=3,
                            relief='flat', highlightcolor=ORANGE, highlightthickness=1, wrap='word')
        self.note.pack(fill='x')
        self.review = tk.StringVar(value='unreviewed')
        ttk.Combobox(notes, textvariable=self.review, values=('unreviewed', 'annotated', 'approved'),
                     state='readonly', style='Studio.TCombobox').pack(fill='x', pady=4)
        action(notes, 'Save capture review', self.save_note).pack(anchor='w')
        self.refresh_section('Review notes', 'Review notes · 0')
        self.arrange_bottom()
        self.status_label = caption(self.root, textvariable=self.status, size=10, wraplength=1300)
        self.status_label.pack(side='bottom', fill='x', padx=24, pady=10, before=work)
        self.status_label.bind('<Configure>', lambda event: self.status_label.configure(wraplength=max(120, event.width)))
        self.columns()
        self.tree.selection_set(self.scenario['context']['target'])

    # parent: Host form container.
    # variable: Editable string; schema validation precedes native publication.
    def field(self, parent, variable):
        return tk.Entry(parent, textvariable=variable, bg=FIELD, fg=FG, insertbackground=FG,
                        relief='flat', highlightbackground=FIELD, highlightcolor=ORANGE, highlightthickness=1)

    # event: Optional actual center/chrome geometry change; cached canvas geometry is not used.
    # Reserves a visible 180-pixel frame plus its 30-pixel caption and vertical margins.
    # Exceptions: Tk geometry failures propagate rather than leaving an unmapped preview unnoticed.
    def resize_bottom_viewport(self, event=None):
        if self.closed or not hasattr(self, 'bottom_canvas'):
            return
        chrome_height = 0
        for widget in self.center.pack_slaves():
            if widget in (self.canvas, self.bottom_host):
                continue
            chrome_height += widget.winfo_reqheight()
            for option in ('pady', 'ipady'):
                padding = widget.pack_info().get(option, 0)
                values = padding if isinstance(padding, (tuple, list)) else str(padding).split()
                pixels = [widget.winfo_pixels(value) for value in values]
                chrome_height += sum(pixels) if len(pixels) > 1 else 2 * pixels[0]
        remaining = self.center.winfo_height() - chrome_height - 210 - 16
        height = min(280, max(36, self.bottom_content_height), max(36, remaining))
        if int(self.bottom_canvas.cget('height')) != height:
            self.bottom_canvas.configure(height=height)

    # No parameters: report backend initialization without claiming a cue was heard.
    def refresh_audio_status(self):
        try:
            status = self.runner.device.audio_status()
            if status['mode'] == 'silent':
                description = 'Capture session: silent · live backend checked on playback'
            elif not status['attempted']:
                description = 'Audio output: backend not initialized'
            elif status['backend_result'] == 0:
                description = 'Audio output: host backend initialized'
            else:
                description = f"Audio output: backend unavailable (error {status['backend_result']})"
        except (AttributeError, KeyError, OSError, ValueError, RuntimeError) as error:
            description = f'Audio output: status unavailable ({error})'
        self.audio_status.set(description)

    # No parameters: restore only a validated published atlas without changing native state.
    # Exceptions: Missing or corrupt evidence remains visible and preserves the current capture.
    def restore_atlas(self):
        try:
            path = latest_atlas_path()
            if path is not None:
                self.load_atlas_result(path)
        except (OSError, ValueError, RuntimeError, KeyError) as error:
            self.atlas_status.set(f'Published atlas unavailable: {error}')

    # message: One host-only progress or per-case outcome, retained in the scrollable log.
    # Exceptions: Tk widget failures propagate to the caller.
    def atlas_log_message(self, message):
        self.atlas_log.configure(state='normal')
        self.atlas_log.insert('end', message + '\n')
        self.atlas_log.see('end')
        self.atlas_log.configure(state='disabled')

    # No parameters: snapshot the selected suite; catalog filtering never changes native inputs.
    # Raises ValueError for an empty filtered catalog or an unknown suite selection.
    def atlas_selection(self):
        selection = self.atlas_suite.get()
        if selection == 'Pilot (10)':
            return None, 'pilot'
        if selection == 'Supported screens':
            return None, 'supported'
        if selection != 'Filtered catalog':
            raise ValueError(f'Unknown atlas suite: {selection}')
        identities = [identity for group in self.tree.get_children()
                      for identity in self.tree.get_children(group)]
        if not identities:
            raise ValueError('The filtered catalog contains no screens')
        return identities, 'all'

    # auto: True only when the user-enabled build watcher requests regeneration.
    # Exceptions: Admission errors retain the last published suite and its PDF action.
    def generate_atlas(self, auto=False):
        if self.closed or self.atlas_job is not None:
            return
        try:
            identities, suite = self.atlas_selection()
            job = AtlasJob.start(identities=identities, suite=suite, auto=auto)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            self.atlas_status.set(f'Atlas generation could not start: {error}')
            self.atlas_log_message(str(error))
            return
        self.atlas_job = job
        self.atlas_generate_button.configure(state='disabled')
        self.atlas_cancel_button.configure(state='normal')
        self.atlas_suite_control.configure(state='disabled')
        self.atlas_status.set('Generating selected suite in an isolated native process…')
        self.atlas_case_status.set('Waiting for native preparation. The interactive scenario remains available.')
        self.atlas_log_message(f"Started {'automatic' if auto else 'manual'} generation · {self.atlas_suite.get()}")

    # No parameters: cancel only this suite process and retain the last published atlas.
    # Exceptions: Original cancellation failures remain visible in the Atlas log.
    def cancel_atlas(self):
        if self.atlas_job is None:
            return
        self.atlas_pending_build = False
        try:
            self.atlas_job.cancel()
            self.atlas_cancel_button.configure(state='disabled')
            self.atlas_status.set('Cancelling atlas generation… Last published atlas retained.')
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            self.atlas_log_message(f'Cancellation failed: {error}')

    # path: Published immutable suite manifest, validated again before thumbnails are added.
    # Exceptions: Capture/manifest validation errors leave the current frame and baseline intact.
    def load_atlas_result(self, path):
        manifest = load_atlas(path)
        records = [load_capture(ROOT / case['capture']) for case in manifest['cases']
                   if case['acquisition'] == 'captured']
        for record in records:
            self.add_capture_record(record)
        self.atlas_manifest_path = Path(path).resolve()
        self.atlas_export_button.configure(state='disabled' if self.atlas_exporting else 'normal')
        counts = {}
        for case in manifest['cases']:
            state = case['acquisition']
            counts[state] = counts.get(state, 0) + 1
        summary = ' · '.join(f'{value} {state}' for state, value in sorted(counts.items()))
        try:
            freshness = 'current native build' if manifest['build'] == verify_current_build() else 'previous native build'
        except (OSError, ValueError, RuntimeError):
            freshness = 'saved build · current build unavailable'
        try:
            verify_contract_inputs(manifest['contract_inputs'])
        except (OSError, ValueError, RuntimeError):
            freshness += ' · catalog/scenarios changed or unavailable'
        self.atlas_status.set(f'Published atlas · {summary} · {freshness}')
        self.atlas_case_status.set('Choose a thumbnail to inspect it. Baseline and interactive scenario are preserved.')
        self.refresh_evidence_filter()
        self.draw()

    # No parameters: start or stop watching the native build inputs, never atlas output files.
    # Auto mode is deliberately opt-in per window and is not restored after a restart.
    def change_atlas_watch(self):
        self.atlas_pending_build = False
        self.atlas_last_watch_error = None
        if not self.atlas_auto.get():
            self.atlas_watcher = None
            self.atlas_log_message('Automatic regeneration disabled.')
            return
        try:
            self.atlas_watcher = BuildWatcher()
            self.atlas_watch_deadline = 0
            self.atlas_log_message('Watching native build provenance. Existing build seeds the watcher.')
        except (OSError, ValueError, RuntimeError) as error:
            self.atlas_auto.set(False)
            self.atlas_watcher = None
            self.atlas_log_message(f'Build watcher unavailable: {error}')

    # No parameters: collect bounded child progress and publish thumbnails on the Tk thread.
    # Exceptions: Failures keep the previous atlas, current frame and explicit baseline selected.
    def poll_atlas(self):
        job = self.atlas_job
        if job is not None:
            try:
                for event in job.poll():
                    message = event.get('message')
                    if event.get('type') == 'case-result':
                        case = event.get('case')
                        completed, total = event.get('completed'), event.get('total')
                        if (not isinstance(case, dict) or type(completed) is not int or type(total) is not int or
                                not 1 <= completed <= total or not isinstance(case.get('id'), str) or
                                not case['id'] or case.get('acquisition') not in
                                ('captured', 'failed', 'unsupported', 'not-applicable') or
                                (case.get('reason') is not None and not isinstance(case['reason'], str))):
                            raise ValueError('Invalid atlas case-result progress payload')
                        identity = inventory_label(case)
                        message = f"{identity}: {case['acquisition']}"
                        if case.get('reason'):
                            message += f" · {case['reason']}"
                        self.atlas_case_status.set(f"{completed} / {total} cases completed · {message}")
                    if message:
                        self.atlas_log_message(str(message))
                if job.done:
                    self.atlas_job = None
                    self.atlas_generate_button.configure(state='normal')
                    self.atlas_cancel_button.configure(state='disabled')
                    self.atlas_suite_control.configure(state='readonly')
                    if job.result is not None:
                        self.load_atlas_result(job.result)
                    else:
                        self.atlas_status.set(f"Atlas not published: {job.error or 'cancelled'} · last published atlas retained")
            except (OSError, ValueError, RuntimeError, KeyError, subprocess.SubprocessError) as error:
                try:
                    job.cancel()
                except (OSError, RuntimeError, subprocess.SubprocessError) as cancellation_error:
                    self.atlas_log_message(f'Cancellation failed: {cancellation_error}')
                self.atlas_job = None
                self.atlas_generate_button.configure(state='normal')
                self.atlas_cancel_button.configure(state='disabled')
                self.atlas_suite_control.configure(state='readonly')
                self.atlas_status.set(f'Atlas unavailable: {error} · last published atlas retained')
                self.atlas_log_message(str(error))
        now = time.monotonic()
        if self.atlas_watcher is not None and now >= self.atlas_watch_deadline:
            self.atlas_watch_deadline = now + 1
            try:
                if self.atlas_watcher.poll() is not None:
                    self.atlas_pending_build = True
                    self.atlas_log_message('Validated new native build detected.')
                error = self.atlas_watcher.error
                if error and error != self.atlas_last_watch_error:
                    self.atlas_log_message(f'Build not eligible for regeneration: {error}')
                self.atlas_last_watch_error = error
            except (OSError, ValueError, RuntimeError) as error:
                if str(error) != self.atlas_last_watch_error:
                    self.atlas_log_message(f'Build watcher: {error}')
                self.atlas_last_watch_error = str(error)
        if self.atlas_pending_build and self.atlas_job is None and self.atlas_auto.get():
            self.atlas_pending_build = False
            self.generate_atlas(auto=True)
        try:
            path, error = self.atlas_results.get_nowait()
        except queue.Empty:
            return
        self.atlas_exporting = False
        self.atlas_export_button.configure(state='normal' if self.atlas_manifest_path is not None else 'disabled')
        if error:
            self.atlas_status.set(f'PDF export failed: {error} · published atlas retained')
            self.atlas_log_message(str(error))
        else:
            self.atlas_status.set(f'PDF exported · {path.relative_to(ROOT)}')
            self.atlas_log_message(f'PDF exported · {path.relative_to(ROOT)}')

    # No parameters: export the published suite, using no interactive frame or unsaved scenario.
    # Exceptions: Destination and PDF failures retain the atlas and existing PDF bytes.
    def export_atlas(self):
        if self.atlas_manifest_path is None or self.atlas_exporting:
            return
        destination = filedialog.asksaveasfilename(parent=self.root, title='Export Cody Studio atlas PDF',
                         initialdir=ROOT / 'artifacts/simulator/studio/atlas',
                         initialfile=f'cody-studio-atlas-{time.time_ns()}.pdf',
                         defaultextension='.pdf', filetypes=(('PDF documents', '*.pdf'),))
        if not destination:
            return
        source = self.atlas_manifest_path
        self.atlas_exporting = True
        self.atlas_export_button.configure(state='disabled')
        self.atlas_status.set('Exporting published atlas PDF…')

        # No parameters: export on a separate host worker; never call Tk or the native owner.
        def work():
            try:
                from cody_studio_pdf import export_atlas
                self.atlas_results.put((export_atlas(source, Path(destination)), None))
            except Exception as error:
                self.atlas_results.put((None, error))

        self.atlas_executor.submit(work)

    # No parameters: allow explicit side-column collapse on small windows.
    def columns(self):
        for widget, variable in ((self.left, self.left_visible), (self.right_host, self.right_visible)):
            widget.grid() if variable.get() else widget.grid_remove()
        self.persist()

    # args: Ignored Tk trace metadata.
    # Maintained source identities remain visible when their contracts are unsupported.
    def catalog(self, *args):
        selected = self.tree.selection()
        self.tree.delete(*self.tree.get_children())
        groups = {}
        latest = {}
        for record in reversed(self.records):
            latest.setdefault(record.metadata['id'], record)
        availability_modes = {'Runtime full': 'runtime-full', 'Presenter isolated': 'presenter-isolated',
                              'Profile conditional': 'profile-conditional', 'Unsupported': 'unsupported',
                              'Not applicable': 'not-applicable'}
        failures = []
        for item in load_catalog():
            try:
                supported, reason = self.runner.availability(item)
            except (OSError, ValueError, RuntimeError) as error:
                supported, reason = False, str(error)
                failures.append(f"{inventory_label(item)}: {reason}")
            mode = self.filter.get()
            text = f"{inventory_label(item)} {item['title']}"
            if self.search.get().casefold() not in text.casefold():
                continue
            if mode == 'Supported' and not supported:
                continue
            if mode in availability_modes and item['availability'] != availability_modes[mode]:
                continue
            if mode in ('Changed', 'Needs review'):
                record = latest.get(item['id'])
                if record is None:
                    continue
                try:
                    if mode == 'Changed':
                        if not self.baseline or compare_captures(self.baseline, record).status != 'changed':
                            continue
                    elif load_review(record)['status'] == 'approved':
                        continue
                except (OSError, ValueError) as error:
                    failures.append(f"{inventory_label(item)}: {error}")
                    continue
            group = item.get('group', 'Supported native') if supported else 'Source catalog · ' + item['availability']
            if group not in groups:
                groups[group] = self.tree.insert('', 'end', text=group, open=supported)
            self.tree.insert(groups[group], 'end', iid=item['id'], text=text)
        if selected and self.tree.exists(selected[0]):
            self.tree.selection_set(selected[0])
        message = 'Changed and Needs review use captures loaded in this session.'
        if failures:
            message += '\nEvidence unavailable: ' + '; '.join(failures)
        self.catalog_message.set(message)

    # No parameters: keep an unavailable saved baseline visible until explicitly replaced or cleared.
    # Exceptions: Tk errors propagate; the saved preference is not modified here.
    def update_baseline_warning(self):
        if self.baseline_error:
            self.baseline_warning_label.configure(text=f'Baseline unavailable: {self.baseline_error}', fg=ORANGE)
            self.baseline_warning_label.pack(anchor='w', before=self.canvas if hasattr(self, 'canvas') else None)
        else:
            self.baseline_warning_label.pack_forget()

    # No parameters: refresh evidence filters after a capture, baseline or review changes.
    # Exceptions: Catalog validation errors propagate instead of inventing evidence.
    def refresh_evidence_filter(self):
        for control, record in zip(self.thumbnail_controls, self.thumbnail_records):
            try:
                difference = compare_captures(self.baseline, record).status if self.baseline else 'not compared'
            except (OSError, ValueError, RuntimeError):
                difference = 'not comparable'
            control.configure(text=f"{inventory_label(record.metadata)} · {difference}")
        if self.filter.get() in ('Changed', 'Needs review'):
            self.catalog()

    # No parameters: the requested comparison mode makes its controls visible.
    # Exceptions: Persistence failures are reported by persist().
    def mode_changed(self):
        self.stop_live()
        if self.mode.get() != 'current' and not self.sections['Comparison'][2]:
            self.sections['Comparison'][2] = True
            self.refresh_section('Comparison')
            self.persist()
        self.draw()

    # event: Optional native Tk selection event.
    def selection(self, event=None):
        selected = self.tree.selection()
        if not selected or ':' not in selected[0]:
            return
        item = entry_for(selected[0])
        self.describe_entry(item)

    # item: Source-qualified catalog entry whose declared routes are inspected.
    # Exceptions: Tk errors propagate without changing the selected native frame.
    def describe_entry(self, item):
        try:
            available, reason = self.runner.availability(item)
            capability = reason if item['availability'] == 'profile-conditional' else ''
        except (OSError, ValueError, RuntimeError) as error:
            available, capability = False, f'Native capability unavailable: {error}'
        self.detail.set(f"{item['id']}\n{item['availability']}\n{capability}\n{item.get('verification', item.get('reason', ''))}")
        routes = routes_for(item['id'])
        incoming = sum(route['destination'] == item['id'] for route in routes)
        outgoing = sum(route['origin'] == item['id'] for route in routes)
        self.refresh_section('Routes', f'Routes · {incoming} in / {outgoing} out')
        previous = self.selected_route()
        self.declared_routes = routes
        labels = [f"{r['origin']} → {r['destination']} · {r['event']}" for r in routes]
        self.route_control.configure(values=labels, state='readonly' if labels else 'disabled')
        index = next((i for i, route in enumerate(routes) if route == previous), 0)
        if routes:
            self.route_control.current(index)
        else:
            self.route_choice.set('')
        self.route_selected()
        if self.timeline_target != item['id']:
            self.pause_timeline()
            self.timeline_target = item['id']
            self.configure_timeline(item)

    # No parameters: return the selected declared transition, if any.
    # Exceptions: No model state is changed when the selection is empty.
    def selected_route(self):
        index = self.route_control.current() if hasattr(self, 'route_control') else -1
        return self.declared_routes[index] if 0 <= index < len(self.declared_routes) else None

    # event: Optional Tk combobox selection event.
    # Exceptions: Incomplete route metadata is shown as unavailable, never observed.
    def route_selected(self, event=None):
        route = self.selected_route()
        if not route:
            self.route_summary.set('No declared routes for this catalog entry.')
            self.replay_button.configure(state='disabled')
            self.route_source_button.configure(state='disabled')
            return
        supported = route.get('replay') is True and bool(route.get('route_id'))
        capability_error = ''
        if supported:
            try:
                for capability in route.get('required_capabilities', []):
                    if not self.runner.device.studio_has_capability(capability):
                        supported = False
                        capability_error = f'Native profile does not provide {capability}'
                        break
            except (OSError, ValueError, RuntimeError) as error:
                supported = False
                capability_error = f'Native capability unavailable: {error}'
        observed = self.route_records.get(route.get('route_id'))
        result = ('Observed in this session: capture ' + observed['capture_id'] +
                  ' · scenario ' + observed['scenario_revision'][:12]) if observed else 'Not yet replayed in this session'
        self.route_summary.set(f"{route['origin']} → {route['destination']}\n"
                               f"{route['event']} · {route['condition']}\n"
                               f"{route['component']}::{route['function']}\n"
                               f"Declared evidence: {route['evidence']} · "
                               f"Replay: {'available' if supported else 'unavailable'}"
                               f"{(' · ' + capability_error) if capability_error else ''}\n{result}")
        self.replay_button.configure(state='normal' if supported else 'disabled')
        self.route_source_button.configure(state='normal' if route.get('source') and route.get('function') else 'disabled')

    # No parameters: resolve this route's current source and symbol on demand.
    # Exceptions: Source/editor failures remain visible and do not change the frame.
    def open_route_source(self):
        route = self.selected_route()
        if not route:
            return
        try:
            item = {'source': route['source'], 'symbol': route['function'].rsplit('::', 1)[-1]}
            path, number = resolve_source(item)
            editor = shutil.which('code') or shutil.which('codium')
            if editor:
                subprocess.Popen([editor, '--goto', f'{path}:{number}'], cwd=ROOT)
            self.status.set(f'{path.relative_to(ROOT)}:{number}' + ('' if editor else ' · editor unavailable'))
        except (KeyError, OSError, ValueError) as error:
            self.error(error)

    # No parameters: load form values without changing native state.
    def show_scenario(self):
        entry = entry_for(self.scenario['context']['target'])
        self.scenario_title.set(f"{inventory_label(entry)} · {entry['title']} inputs")
        self.variant_control.configure(values=entry['variants'])
        self.variant.set(self.scenario['variant'])
        common = self.scenario['common']
        data, settings = common['observations'], common['settings']
        values = {'battery': data['battery']['level_percent'],
                  'network': 'Connected' if data['network']['connected'] else 'Disconnected',
                  'ssid': data['network']['ssid'], 'rssi': data['network']['rssi'],
                  'epoch': data['clock']['epoch_seconds'], 'offset': data['clock']['utc_offset_minutes'],
                  'brightness': settings['brightness_percent'], 'orientation': settings['orientation_degrees']}
        for key, value in values.items():
            self.inputs[key].set('' if value is None else str(value))
        self.auto_rotate.set(settings['auto_rotate'])
        self.sounds_enabled.set(settings.get('sounds_enabled', False))
        self.playback_volume.set(str(settings.get('playback_volume', 90)))
        self.show_case_fields(entry)
        self.pause_timeline()
        self.timeline_scenario = None
        self.timeline_target = entry['id']
        self.configure_timeline(entry)

    # item: Catalog entry currently associated with the editable scenario.
    # Exceptions: Missing animation support remains visibly unavailable.
    def configure_timeline(self, item):
        supported = (item['id'] == self.scenario['context']['target'] and
                     'sample_at_ms' in item.get('scenario_contract', {}).get('page', {}))
        plan = None
        if supported:
            try:
                plan = timeline_plan(self.runner, self.timeline_scenario or self.scenario)
            except (OSError, ValueError, RuntimeError) as error:
                self.animation_status.set(f'Animation unavailable: {error}')
        else:
            self.animation_status.set('Open a destination with a supported native source animation.')
        self.timeline_contract = plan
        clock_modes = ('Source frames', 'Display worker') if isinstance(item.get('worker_replay'), dict) else ('Source frames',)
        self.clock_control.configure(values=clock_modes, state='readonly' if plan else 'disabled')
        worker_mode = self.scenario['time']['mode'] == 'controlled-worker'
        self.animation_clock.set('Display worker' if worker_mode else 'Source frames')
        if plan:
            self.keyframe_control.configure(values=tuple(str(value) for value in plan.keyframes_ms), state='readonly')
            self.timeline_position = (self.scenario['time']['capture_at_ms'][0] if worker_mode
                                      else self.scenario['page']['sample_at_ms'])
            self.animation_keyframe.set(str(self.timeline_position))
            self.animation_status.set(f'{plan.rendering_mode} · scenario {plan.scenario_revision[:12]} · '
                                      f'{plan.limitation} · playback speed depends on capture throughput')
            self.refresh_section('Animation', f'Animation · {plan.duration_ms} ms · {len(plan.keyframes_ms)} keyframes')
        else:
            self.keyframe_control.configure(values=(), state='disabled')
            self.animation_keyframe.set('')
            self.refresh_section('Animation', 'Animation · no sequence · 26 audio cues')
        for control in (self.play_button, self.step_button, self.seek_button):
            control.configure(state='normal' if plan else 'disabled')
        self.live_button.configure(state='normal' if plan and isinstance(item.get('worker_replay'), dict) else 'disabled')
        self.pause_button.configure(state='disabled')

    # event: Optional user selection of source frames or controlled display worker.
    # Exceptions: Invalid inputs or unavailable native capability leave the scenario unchanged.
    def change_timeline_clock(self, event=None):
        if self.busy:
            self.animation_clock.set('Display worker' if self.scenario['time']['mode'] == 'controlled-worker'
                                     else 'Source frames')
            self.status.set('Wait for the active frame before changing the clock.')
            return
        self.pause_timeline()
        try:
            current = self.form()
            if self.animation_clock.get() == 'Display worker':
                value = worker_timeline_scenario(self.runner, current)
            elif self.animation_clock.get() == 'Source frames':
                value = source_timeline_scenario(current)
            else:
                raise ValueError('Unknown timeline clock selection')
            self.scenario = value
            self.show_scenario()
            self.status.set('Timeline clock selected; Apply or a keyframe prepares the native frame.')
        except (OSError, ValueError, TypeError, RuntimeError) as error:
            self.animation_clock.set('Display worker' if self.scenario['time']['mode'] == 'controlled-worker'
                                     else 'Source frames')
            self.error(error)

    # No parameters: retain a detached validated input revision for animation sampling.
    # Exceptions: Contract, input and native source errors propagate to the caller.
    def prepare_timeline(self):
        scenario = self.form()
        if scenario['common']['settings'].get('sounds_enabled', False):
            raise ValueError('Sounds is available for Boot replay. Turn Sounds off before a timeline capture.')
        # A changed sample remains in the same sequence. Any other edited input
        # starts a new base revision instead of mixing unrelated evidence.
        comparable = deepcopy(scenario)
        if 'sample_at_ms' in comparable['page']:
            comparable['page']['sample_at_ms'] = 0
        previous = deepcopy(self.timeline_scenario)
        if previous and 'sample_at_ms' in previous['page']:
            previous['page']['sample_at_ms'] = 0
        if previous != comparable:
            self.timeline_scenario = deepcopy(scenario)
        plan = timeline_plan(self.runner, self.timeline_scenario)
        self.timeline_contract = plan
        self.keyframe_control.configure(values=tuple(str(value) for value in plan.keyframes_ms), state='readonly')
        self.refresh_section('Animation', f'Animation · {plan.duration_ms} ms · {len(plan.keyframes_ms)} keyframes')
        return scenario, plan

    # No parameters: start serialized native source-frame acquisition from the current sample.
    # Exceptions: Invalid inputs fail without replacing the last capture.
    def play_timeline(self):
        if self.busy or self.timeline_playing:
            return
        try:
            self.pause_timeline()
            scenario, plan = self.prepare_timeline()
            position = self.timeline_position if plan.clock_mode == 'display-worker' else scenario['page']['sample_at_ms']
            index = bisect_right(plan.keyframes_ms, position)
            if index == len(plan.keyframes_ms):
                index = 0
            self.timeline_playing = True
            self.timeline_deadline = time.monotonic()
            self.pause_button.configure(state='normal')
            self.queue_timeline_frame(index)
        except (OSError, ValueError, TypeError, RuntimeError) as error:
            self.error(error)

    # No parameters: stop future playback; an admitted native frame retires normally.
    # Exceptions: Tk timer cancellation errors remain visible to the caller.
    def pause_timeline(self):
        was_playing = self.timeline_playing
        self.timeline_playing = False
        self.timeline_deadline = None
        self.stop_live()
        if self.timeline_timer is not None:
            self.root.after_cancel(self.timeline_timer)
            self.timeline_timer = None
        if hasattr(self, 'pause_button'):
            self.pause_button.configure(state='disabled')
        if was_playing and hasattr(self, 'animation_status'):
            self.animation_status.set('Pausing · waiting for the admitted frame' if self.busy else
                                      'Paused · current completed frame retained')

    # No parameters: audition one authored native cue using a copied form and independent owner.
    # Exceptions: Admission failures remain visible without mutating current capture or baseline.
    def play_cue_audition(self):
        if self.closed or self.cue_job is not None:
            return
        try:
            self.pause_timeline()
            from cody_studio_audio import AudioAudition
            self.cue_job = AudioAudition.start(self.form(), self.cue_choice.get())
            self.cue_play_button.configure(state='disabled')
            self.cue_stop_button.configure(state='normal')
            self.cue_status.set(f'Audition starting · {self.cue_choice.get()} · separate native audio owner')
        except (OSError, ValueError, RuntimeError) as error:
            self.cue_status.set(f'Audition could not start: {error}')

    # No parameters: stop and reap only the explicit audition process.
    # Exceptions: Original cancellation errors remain visible in the audio status.
    def stop_cue_audition(self):
        job, self.cue_job = self.cue_job, None
        if job is not None:
            try:
                job.cancel()
                self.cue_status.set('Audition stopped · interactive frame and baseline retained')
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                self.cue_status.set(f'Audition cancellation failed: {error}')
        if hasattr(self, 'cue_play_button'):
            self.cue_play_button.configure(state='normal')
            self.cue_stop_button.configure(state='disabled')

    # No parameters: project real audio admission/completion, never inferred event acceptance.
    # Exceptions: Malformed or failed child outcomes preserve all interactive state.
    def poll_cue_audition(self):
        job = self.cue_job
        if job is None:
            return
        try:
            status = job.poll()
            if status is not None:
                self.cue_status.set(f"Audition · {status['cue']} · {status['state']} · error 0x{status['error'] & 0xffffffff:08x}")
            if job.done:
                self.cue_job = None
                self.cue_play_button.configure(state='normal')
                self.cue_stop_button.configure(state='disabled')
                if job.error:
                    self.cue_status.set(job.error)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            self.stop_cue_audition()
            self.cue_status.set(f'Audition failed: {error}')

    # No parameters: preview the real live native worker and its actual sound cues.
    # Live frames never become capture, baseline or review evidence.
    def play_live(self):
        if self.busy or self.live_process is not None:
            return
        try:
            self.stop_cue_audition()
            self.pause_timeline()
            scenario = self.form()
            from cody_studio_live import start_live
            from cody_studio_runner import verify_current_build
            build = verify_current_build()
            if getattr(self.runner.device.api, '_cody_library_sha256', None) != build['library_sha256']:
                raise ValueError('A different native build is loaded in Studio; restart this session')
            entry = entry_for(scenario['context']['target'])
            self.live_title = f"{inventory_label(entry)} — {entry['title']}"
            self.live_caption_backup = (self.frame_title.get(), self.frame_subtitle.get())
            self.live_process, self.live_directory = start_live(scenario)
            self.live_build_sha256 = build['library_sha256']
            self.live_started = time.monotonic()
            self.pause_button.configure(state='normal')
            self.animation_status.set('Starting isolated native boot replay…')
            self.poll_live()
        except (OSError, ValueError, RuntimeError) as error:
            self.stop_live()
            self.error(error)

    # No parameters: refresh an atomic preview frame and report process errors.
    def poll_live(self):
        self.live_timer = None
        process = self.live_process
        if self.closed or process is None:
            return
        try:
            status_path, frame_path = self.live_directory / 'status.json', self.live_directory / 'frame.png'
            playback = None
            if status_path.exists():
                status = json.loads(status_path.read_text(encoding='utf-8'))
                if status['library_sha256'] != self.live_build_sha256:
                    raise ValueError('Live process loaded a different native build')
                audio = status['audio']
                state = 'Sounds on' if status['sounds_enabled'] else 'Sounds off'
                playback = status['playback']
                cues = ', '.join(f"#{cue['id']} / {cue['sequence']}: {cue['state']} (error {cue['error']})"
                                 for cue in playback['cues']) or 'no cue observed'
                self.animation_status.set(f"Native boot replay · {state} · volume {status['playback_volume']}% · "
                                          f"backend result {audio['backend_result']} · cue outcomes: {cues} · preview only")
            if frame_path.exists():
                with Image.open(frame_path) as frame:
                    self.live_image = frame.copy()
                self.frame_title.set(self.live_title)
                self.frame_subtitle.set('Native C/C++ · live boot replay · not a capture')
                self.draw()
            result = process.poll()
            if result is not None:
                if result:
                    if result == 124:
                        raise TimeoutError('Live playback exceeded its 30-second owner completion deadline')
                    if result == 125:
                        raise RuntimeError('Live playback stopped because its parent connection closed')
                    detail = (self.live_directory / 'stderr.log').read_text(encoding='utf-8')[-3000:]
                    raise RuntimeError(f'Live native playback failed ({result}): {detail}')
                if not playback or not playback['animation_complete'] or not playback['audio_complete']:
                    raise RuntimeError('Live playback exited without owner completion evidence')
                if process.stdin is not None:
                    process.stdin.close()
                self.live_process = None
                self.pause_button.configure(state='normal' if self.live_image is not None else 'disabled')
                self.animation_status.set(f'Boot replay ended · cue outcomes: {cues} · preview only')
                return
            if time.monotonic() - self.live_started > 30:
                raise TimeoutError('Live native playback exceeded its 30-second deadline')
            self.live_timer = self.root.after(60, self.poll_live)
        except (OSError, ValueError, KeyError, RuntimeError) as error:
            self.stop_live()
            self.error(error)

    # No parameters: end the isolated live owner; pausing does not freeze audio clocks.
    def stop_live(self):
        if self.live_timer is not None:
            self.root.after_cancel(self.live_timer)
            self.live_timer = None
        process, self.live_process = self.live_process, None
        if process is not None and process.poll() is None:
            try:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            except ProcessLookupError:
                process.wait()
        if process is not None and process.stdin is not None:
            process.stdin.close()
        if self.live_caption_backup is not None:
            title, subtitle = self.live_caption_backup
            self.frame_title.set(title)
            self.frame_subtitle.set(subtitle)
            self.live_caption_backup = self.live_title = None
        if self.live_image is not None:
            self.live_image = None
            if hasattr(self, 'canvas'):
                self.draw()

    # No parameters: acquire the next native keyframe from a fresh preparation.
    # Exceptions: Input/contract errors leave the current capture intact.
    def step_timeline(self):
        if self.busy:
            return
        self.pause_timeline()
        try:
            scenario, plan = self.prepare_timeline()
            position = self.timeline_position if plan.clock_mode == 'display-worker' else scenario['page']['sample_at_ms']
            index = bisect_right(plan.keyframes_ms, position)
            if index >= len(plan.keyframes_ms):
                self.animation_status.set('Last keyframe reached. Select an earlier keyframe to replay.')
                return
            self.queue_timeline_frame(index)
        except (OSError, ValueError, TypeError, RuntimeError) as error:
            self.error(error)

    # No parameters: seek by re-preparing the scenario, including when seeking backwards.
    # Exceptions: Unknown keyframes and input errors do not modify native state.
    def seek_timeline(self):
        if self.busy:
            return
        self.pause_timeline()
        try:
            _, plan = self.prepare_timeline()
            sample = int(self.animation_keyframe.get())
            self.queue_timeline_frame(plan.keyframes_ms.index(sample))
        except (OSError, ValueError, TypeError, RuntimeError) as error:
            self.error(error)

    # index: Valid keyframe index in the frozen scenario's native animation plan.
    # Exceptions: A busy worker is never replaced; failures arrive through the result queue.
    def queue_timeline_frame(self, index):
        self.timeline_timer = None
        if self.closed or self.busy or self.timeline_contract is None:
            return
        at_ms = self.timeline_contract.keyframes_ms[index]
        scenario = deepcopy(self.timeline_scenario)
        self.busy = True
        self.apply_button.configure(state='disabled')
        self.status.set(f'Preparing native {self.timeline_contract.clock_mode} frame at {at_ms} ms…')

        # No parameters: owner-backed capture happens outside the Tk thread.
        def work():
            try:
                record = timeline_capture(self.runner, scenario, at_ms)
                self.results.put((None, record, None, {'kind': 'timeline', 'index': index, 'sample': at_ms}))
            except Exception as error:
                self.results.put((None, None, error, {'kind': 'timeline'}))

        self.executor.submit(work)

    # entry: Catalog contract describing the currently editable scenario.
    # Adds leaf controls for page/service inputs and additional owner settings.
    # Schema validation remains authoritative; nulls are explicitly unavailable.
    def show_case_fields(self, entry):
        for child in self.case_fields_host.winfo_children():
            child.destroy()
        self.case_fields = {}
        self.case_field_widgets = {}
        descriptions = {tuple(item['path']): item for item in entry.get('controls', [])}

        # path: Exact schema location, including a bounded array row when present.
        # Returns the catalog's scalar field contract for control choices.
        def field_specification(path):
            contract = entry['scenario_contract']
            keys = path[2:] if path[:2] == ('common', 'settings') else path[1:]
            fields = contract['settings'] if path[:2] == ('common', 'settings') else contract[path[0]]
            specification = {'type': 'object', 'fields': fields}
            for key in keys:
                if specification['type'] == 'array':
                    specification = {'type': 'object', 'fields': specification['fields']}
                else:
                    specification = specification['fields'][key]
            return specification

        # path: Schema location within the detached scenario.
        # value: Supported scalar or nested mapping; containers are never flattened into native pointers.
        def add_fields(path, value):
            if isinstance(value, dict):
                for key, child in value.items():
                    add_fields((*path, key), child)
                return
            if isinstance(value, list) and field_specification(path)['type'] == 'int-array':
                description = descriptions.get(path, {})
                title = description.get('label', str(path[-1]).replace('_', ' ').title())
                caption(self.case_fields_host, f'{title} · {len(value)} samples · JSON integers',
                        10, wraplength=275).pack(anchor='w', pady=(8, 0))
                variable = tk.StringVar(value=json.dumps(value, separators=(',', ':')))
                control = self.field(self.case_fields_host, variable)
                control.pack(fill='x')
                self.case_fields[path] = (variable, list)
                self.case_field_widgets[path] = control
                return
            if isinstance(value, list):
                caption(self.case_fields_host, f"{str(path[-1]).replace('_', ' ').title()} · {len(value)} records",
                        11, True).pack(anchor='w', pady=(8, 0))
                for index, row in enumerate(value):
                    add_fields((*path, index), row)
                return
            description = descriptions.get(path, {})
            title = ' · '.join(str(part + 1) if type(part) is int else part.replace('_', ' ').title() for part in path)
            if any(type(part) is int for part in path):
                index = next(part for part in path if type(part) is int)
                title = f"Call {index + 1} · {str(path[-1]).replace('_', ' ').title()}"
            title = description.get('label', title)
            if value is None:
                caption(self.case_fields_host, f'{title}: unavailable', 10, wraplength=275).pack(anchor='w')
                return
            if type(value) is bool:
                variable = tk.BooleanVar(value=value)
                control = tk.Checkbutton(self.case_fields_host, text=title, variable=variable, bg=BG, fg=FG,
                               selectcolor=FIELD, activebackground=BG, activeforeground=ORANGE,
                               highlightcolor=ORANGE, wraplength=260, anchor='w')
                control.pack(fill='x')
            else:
                caption(self.case_fields_host, title, 10, wraplength=275).pack(anchor='w', pady=(4, 0))
                variable = tk.StringVar(value=str(value))
                values = description.get('values', field_specification(path).get('values'))
                if values:
                    control = ttk.Combobox(self.case_fields_host, textvariable=variable, values=values,
                                          state='readonly', style='Studio.TCombobox')
                else:
                    control = self.field(self.case_fields_host, variable)
                control.pack(fill='x')
            self.case_fields[path] = (variable, type(value))
            self.case_field_widgets[path] = control

        add_fields(('page',), self.scenario['page'])
        add_fields(('services',), self.scenario['services'])
        for key, value in self.scenario['common']['settings'].items():
            if key not in ('brightness_percent', 'auto_rotate', 'orientation_degrees', 'sounds_enabled', 'playback_volume'):
                add_fields(('common', 'settings', key), value)
        # Retire the recursive closure on Tk's thread, without retaining this
        # window and its variables until a worker happens to trigger cyclic GC.
        add_fields = None

    # No parameters: construct and validate a detached complete scenario.
    # Raises ValueError on malformed form values or unsupported contracts.
    def form(self):
        value = deepcopy(self.scenario)
        value['variant'] = self.variant.get()
        connected = self.inputs['network'].get() == 'Connected'
        value['common']['observations'] = {'battery': {'level_percent': int(self.inputs['battery'].get())},
                             'network': {'connected': connected,
                                         'ssid': self.inputs['ssid'].get() if connected else '',
                                         'rssi': int(self.inputs['rssi'].get()) if connected and self.inputs['rssi'].get().strip() else None},
                             'clock': {'epoch_seconds': int(self.inputs['epoch'].get()),
                                       'utc_offset_minutes': int(self.inputs['offset'].get())}}
        value['common']['settings'].update({'brightness_percent': int(self.inputs['brightness'].get()),
                         'auto_rotate': self.auto_rotate.get(),
                         'orientation_degrees': int(self.inputs['orientation'].get())})
        volume = int(self.playback_volume.get())
        if ('sounds_enabled' in value['common']['settings'] or self.sounds_enabled.get() or volume != 90):
            value['common']['settings'].update({'sounds_enabled': self.sounds_enabled.get(),
                                                'playback_volume': volume})
        for path, (variable, field_type) in self.case_fields.items():
            target = value
            for key in path[:-1]:
                target = target[key]
            text = variable.get()
            try:
                target[path[-1]] = json.loads(text) if field_type is list else field_type(text)
            except (ValueError, TypeError):
                raise ValueError(f"{' · '.join(map(str, path))} requires a valid {field_type.__name__}") from None
        validate_scenario(value)
        return value

    # No parameters: serialize native preparation and acquisition off the Tk thread.
    def apply(self):
        if self.busy:
            return
        self.pause_timeline()
        try:
            scenario = self.form()
            if scenario['common']['settings'].get('sounds_enabled', False):
                raise ValueError('Sounds is available for Boot replay. Turn Sounds off before Apply and capture.')
            requested_at_ms = None
            if scenario['time']['mode'] == 'controlled-worker':
                requested_at_ms = int(self.animation_keyframe.get())
                if requested_at_ms not in scenario['time']['capture_at_ms']:
                    raise ValueError('Select a declared worker capture boundary')
        except (ValueError, TypeError) as error:
            self.error(error)
            return
        self.busy = True
        self.apply_button.configure(state='disabled')
        entry = entry_for(scenario['context']['target'])
        self.status.set(f"Preparing {inventory_label(entry)} on the native owner…")

        # No parameters: return results through a queue; this worker never calls Tk.
        def work():
            try:
                self.runner.prepare(scenario, requested_at_ms=requested_at_ms)
                self.results.put((scenario, self.runner.capture(), None, {'kind': 'capture'}))
            except Exception as error:
                self.results.put((None, None, error, {'kind': 'capture'}))

        self.executor.submit(work)

    # No parameters: adopt worker results only on the Tk thread.
    def poll(self):
        if self.closed:
            return
        self.poll_atlas()
        self.poll_cue_audition()
        try:
            scenario, record, error, operation = self.results.get_nowait()
        except queue.Empty:
            pass
        else:
            self.busy = False
            self.apply_button.configure(state='normal')
            if error:
                self.pause_timeline()
                self.error(error)
            else:
                if operation['kind'] == 'route':
                    self.route_records[operation['route_id']] = dict(record.metadata)
                    self.scenario = deepcopy(record.metadata['scenario'])
                    self.show_scenario()
                self.set_current(record)
                if operation['kind'] == 'route':
                    self.status.set(f"Observed route {operation['route_id']} · native capture {record.metadata['capture_id']}")
                elif operation['kind'] == 'timeline':
                    sample = operation['sample']
                    field = self.case_fields.get(('page', 'sample_at_ms'))
                    worker = record.metadata.get('worker')
                    if field and worker is None:
                        field[0].set(str(sample))
                    self.timeline_position = sample
                    self.animation_keyframe.set(str(sample))
                    if worker:
                        actual = worker['receipt']['frame_at_ms']
                        source = record.metadata['frame_identity']['source_animation_sample_ms']
                        self.status.set(f'Worker requested {sample} ms · completed frame {actual} ms · source {source} ms')
                    else:
                        self.status.set(f'Acquired source-animation keyframe {sample} ms · native completed frame')
                    next_index = operation['index'] + 1
                    if self.timeline_playing and next_index < len(self.timeline_contract.keyframes_ms):
                        delay = self.timeline_contract.keyframes_ms[next_index] - sample
                        speed = {'0.5x': 0.5, '1x': 1.0, '2x': 2.0}.get(self.animation_speed.get())
                        if speed is None:
                            self.pause_timeline()
                            self.status.set('Playback stopped: unsupported speed.')
                        else:
                            self.timeline_deadline += delay / (speed * 1000)
                            remaining = max(1, round((self.timeline_deadline - time.monotonic()) * 1000))
                            self.timeline_timer = self.root.after(remaining,
                                                                  lambda: self.queue_timeline_frame(next_index))
                    elif self.timeline_playing:
                        self.pause_timeline()
                    playback = 'Playing' if self.timeline_playing else 'Paused'
                    timing = (f"display worker requested {sample} ms · frame {actual} ms · source {source} ms"
                              if worker else f"source animation {sample} ms · worker replay unavailable")
                    self.animation_status.set(f"{playback} · {record.metadata['rendering_mode']} · scenario "
                                              f"{record.metadata['timeline']['base_revision'][:12]} · {timing}")
                else:
                    if record.metadata.get('worker'):
                        self.timeline_position = record.metadata['requested_at_ms']
                        self.animation_keyframe.set(str(self.timeline_position))
                    self.status.set(f'Acquired · native completed frame · {record.metadata_path.name} · physical acceptance pending')
        self.poll_id = self.root.after(40, self.poll)

    # record: A verified immutable capture record; never automatically becomes baseline.
    def set_current(self, record):
        self.live_image = None
        review = load_review(record)
        self.current = record
        route_evidence = record.metadata.get('route')
        if isinstance(route_evidence, dict):
            route_id = route_evidence.get('receipt', {}).get('route_id')
            if route_id:
                self.route_records[route_id] = dict(record.metadata)
        entry = entry_for(record.metadata['id'])
        self.frame_title.set(f"{inventory_label(entry)} — {entry['title']}")
        sample = record.metadata['frame_identity'].get('source_animation_sample_ms', 0)
        timing = f" · source animation {sample} ms" if sample else ''
        if record.metadata.get('worker'):
            timing += f" · requested {record.metadata['requested_at_ms']} ms · display worker"
        self.frame_subtitle.set(f"Native C/C++ · {record.metadata['rendering_mode']} · t={record.metadata['at_ms']}{timing} · 466 × 466")
        if self.tree.exists(record.metadata['id']):
            self.tree.selection_set(record.metadata['id'])
        self.describe_entry(entry)
        self.add_capture_record(record)
        self.refresh_evidence_filter()
        self.note.delete('1.0', 'end')
        self.note.insert('1.0', review['note'])
        self.review.set(review['status'])
        self.refresh_section('Review notes', f"Review notes · {review['count']} · {review['status']}")
        self.draw()

    # record: Hash-checked saved capture added to the atlas without selecting it or touching native state.
    # Exceptions: Image/Tk failures propagate; a duplicate immutable capture is ignored.
    def add_capture_record(self, record):
        if all(item.metadata_path != record.metadata_path for item in self.records):
            with Image.open(record.image_path) as image:
                photo = ImageTk.PhotoImage(image.resize((72, 72), Image.Resampling.NEAREST))
            difference = 'not compared'
            if self.baseline is not None:
                try:
                    difference = compare_captures(self.baseline, record).status
                except (OSError, ValueError, RuntimeError):
                    difference = 'not comparable'
            control = action(self.thumbnails, f"{inventory_label(record.metadata)} · acquired",
                             lambda chosen=record: self.select_capture(chosen))
            control.configure(image=photo, compound='top', font=('TkDefaultFont', 9))
            control.configure(text=f"{inventory_label(record.metadata)} · {difference}")
            self.records.append(record)
            self.photos.append(photo)
            self.thumbnail_controls.append(control)
            self.thumbnail_records.append(record)
            if len(self.thumbnail_controls) > 40:
                self.thumbnail_controls.pop(0).destroy()
                self.photos.pop(0)
                self.thumbnail_records.pop(0)
            for position, thumbnail in enumerate(self.thumbnail_controls):
                thumbnail.grid(row=position // 4, column=position % 4, sticky='nw', padx=(0, 7))
        screens = len({item.metadata['id'] for item in self.records})
        visible = f' · latest {len(self.thumbnail_controls)} shown' if len(self.records) > 40 else ''
        self.refresh_section('Atlas', f'Atlas · {screens} screens / {len(self.records)} captures{visible}')

    # No parameters: present proportional native pixels or a compatible comparison.
    def draw(self):
        for mode, control in self.mode_controls:
            control.configure(fg=ORANGE if self.mode.get() == mode else FG)
        if self.live_image is not None:
            frame = self.live_image
            width, height = max(1, self.canvas.winfo_width()), max(1, self.canvas.winfo_height())
            factor = min(width / frame.width, max(1, height - 30) / frame.height)
            size = max(1, round(frame.width * factor)), max(1, round(frame.height * factor))
            self.frame_photo = ImageTk.PhotoImage(frame.resize(size, Image.Resampling.NEAREST))
            self.canvas.delete('all')
            self.canvas.create_text(width // 2, 4, text='BOOT REPLAY · NOT A CAPTURE', fill=ORANGE, anchor='n')
            self.canvas.create_image(width // 2, 30 + max(1, height - 30) // 2, image=self.frame_photo)
            return
        if self.current is None:
            return
        try:
            result = compare_captures(self.baseline, self.current) if self.baseline else None
            if result is None or not result.compatible or self.mode.get() == 'current':
                with Image.open(self.current.image_path) as image:
                    frame = image.copy()
            else:
                frame = comparison_image(self.baseline, self.current, self.mode.get(), self.opacity.get())
            if result is None:
                summary = 'Not comparable · choose a baseline'
            elif not result.compatible:
                summary = 'Not comparable · ' + '; '.join(result.reasons)
            else:
                summary = ('Unchanged' if result.changed_pixels == 0 else 'Modified') + f' · {result.changed_pixels} changed pixels'
            self.compare_status.set(summary + '\nAcquisition and human review are separate states.')
            review = load_review(self.current)
            difference = result.status if result else 'not-comparable'
            self.evidence_status.set(f"Acquisition: captured\nDifference: {difference}\nReview: {review['status']}")
            self.refresh_section('Changes', 'Changes · ' + (str(result.changed_pixels) + ' pixels' if result and result.compatible else 'not compared'))
            width, height = max(1, self.canvas.winfo_width()), max(1, self.canvas.winfo_height())
            unavailable = self.mode.get() != 'current' and (result is None or not result.compatible)
            header_height = 60 if unavailable else 30
            usable_height = max(1, height - header_height)
            factor = min(width / frame.width, usable_height / frame.height)
            size = max(1, round(frame.width * factor)), max(1, round(frame.height * factor))
            self.frame_photo = ImageTk.PhotoImage(frame.resize(size, Image.Resampling.NEAREST))
            self.canvas.delete('all')
            if self.mode.get() == 'side-by-side' and result and result.compatible:
                self.canvas.create_text(width * .25, 10, text='BEFORE · saved baseline', fill=ORANGE, anchor='n')
                self.canvas.create_text(width * .75, 10, text='AFTER · selected capture', fill=FG, anchor='n')
            elif unavailable:
                reason = ('; '.join(result.reasons) if result else
                          ('Saved baseline unavailable: ' + self.baseline_error if self.baseline_error else 'Choose a baseline'))
                self.canvas.create_text(width // 2, 4, text='Not comparable · ' + reason,
                                        fill=ORANGE, anchor='n', width=max(1, width - 16))
            self.canvas.create_image(width // 2, header_height + usable_height // 2, image=self.frame_photo)
        except (OSError, ValueError, RuntimeError) as error:
            self.error(error)

    # No parameters: explicit preservation never overwrites a previously saved baseline.
    def keep_baseline(self):
        if self.live_process is not None or self.live_image is not None:
            self.status.set('Live preview is not a capture. Stop it and select a capture before setting a baseline.')
            messagebox.showinfo('Cody Studio — SIMULATED', self.status.get(), parent=self.root)
            return
        if self.current:
            try:
                self.baseline = preserve_baseline(self.current)
                self.baseline_error = None
                self.unavailable_baseline = None
                self.update_baseline_warning()
                self.persist()
                self.refresh_evidence_filter()
                self.draw()
            except (OSError, ValueError) as error:
                self.error(error)

    # No parameters: choose metadata for a baseline and preserve its bytes independently.
    def choose_baseline(self):
        path = filedialog.askopenfilename(parent=self.root, title='Choose baseline capture JSON',
                                          initialdir=str(STATE_PATH.parent), filetypes=(('Capture metadata', '*.json'),))
        if path:
            try:
                self.baseline = preserve_baseline(load_capture(path))
                self.baseline_error = None
                self.unavailable_baseline = None
                self.update_baseline_warning()
                self.persist()
                self.refresh_evidence_filter()
                self.draw()
            except (OSError, ValueError) as error:
                self.error(error)

    # No parameters: deselect the baseline while retaining its immutable artifacts.
    # Exceptions: Persistence errors are reported by persist().
    def clear_baseline(self):
        self.baseline = None
        self.baseline_error = None
        self.unavailable_baseline = None
        self.update_baseline_warning()
        self.persist()
        self.refresh_evidence_filter()
        self.draw()

    # No parameters: inspect an existing capture without preparing native state.
    def open_capture(self):
        self.pause_timeline()
        path = filedialog.askopenfilename(parent=self.root, title='Open capture JSON',
                                          initialdir=str(STATE_PATH.parent), filetypes=(('Capture metadata', '*.json'),))
        if path:
            try:
                self.select_capture(load_capture(path))
            except (OSError, ValueError) as error:
                self.error(error)

    # record: Explicitly selected history/file capture, already hash-validated.
    # Synchronizes editable inputs, catalog and routes with that exact frame.
    # Exceptions: Preserves schema/source errors and leaves native acquisition untouched.
    def select_capture(self, record):
        if self.busy:
            self.status.set('Wait for the active preparation before selecting a saved capture.')
            return
        load_review(record)
        self.scenario = deepcopy(record.metadata['scenario'])
        self.show_scenario()
        timeline = record.metadata.get('timeline')
        if isinstance(timeline, dict):
            self.timeline_scenario = deepcopy(timeline['base_scenario'])
            self.configure_timeline(entry_for(record.metadata['id']))
        if record.metadata.get('worker'):
            self.timeline_position = record.metadata['requested_at_ms']
            self.animation_keyframe.set(str(self.timeline_position))
        self.set_current(record)
        if self.tree.exists(record.metadata['id']):
            self.tree.selection_set(record.metadata['id'])
        self.describe_entry(entry_for(record.metadata['id']))

    # No parameters: save a scenario for replay, leaving application to Apply.
    def save_scenario(self):
        try:
            value = self.form()
            path = filedialog.asksaveasfilename(parent=self.root, title='Save Studio scenario',
                                               initialdir=str(STATE_PATH.parent), defaultextension='.json')
            if path:
                save_scenario(value, path)
                self.status.set('Saved scenario; Apply prepares and captures it.')
        except (OSError, ValueError) as error:
            self.error(error)

    # No parameters: load a strict scenario for review before publishing it.
    def open_scenario(self):
        if self.busy:
            self.status.set('Wait for the active preparation before loading another scenario.')
            return
        self.pause_timeline()
        path = filedialog.askopenfilename(parent=self.root, title='Open Studio scenario',
                                          filetypes=(('Studio scenario', '*.json'),))
        if path:
            try:
                self.scenario = load_scenario(path)
                self.show_scenario()
                self.status.set('Scenario loaded; Apply prepares and captures it.')
            except (OSError, ValueError) as error:
                self.error(error)

    # No parameters: write human review for the selected capture identity only.
    def save_note(self):
        if self.live_process is not None or self.live_image is not None:
            self.status.set('Live preview is not a capture. Stop it and select a capture before saving a review.')
            messagebox.showinfo('Cody Studio — SIMULATED', self.status.get(), parent=self.root)
            return
        if self.current:
            try:
                note = self.note.get('1.0', 'end').strip()
                save_review(self.current, note, self.review.get())
                review = load_review(self.current)
                self.refresh_section('Review notes', f"Review notes · {review['count']} · {review['status']}")
                self.refresh_evidence_filter()
                self.draw()
                self.status.set('Review saved for the selected capture.')
            except (OSError, ValueError) as error:
                self.error(error)

    # No parameters: direct destination preparation is separate from route replay.
    def open_destination(self):
        if self.busy:
            self.status.set('Wait for the active native preparation before changing destination.')
            return
        selected = self.tree.selection()
        if not selected or ':' not in selected[0]:
            return
        entry = entry_for(selected[0])
        try:
            available, reason = self.runner.availability(entry)
            if not available:
                self.status.set('The selected catalog entry has no supported direct preparation contract: ' + reason)
                return
            self.scenario = default_scenario(entry['id'])
            self.show_scenario()
            self.apply()
        except (OSError, ValueError, TypeError) as error:
            self.error(error)

    # No parameters: prepare the declared origin, then execute its real native event.
    # Exceptions: Unsupported routes and preparation failures never claim observation.
    def replay_route(self):
        if self.busy:
            return
        route = self.selected_route()
        if not route or route.get('replay') is not True or not route.get('route_id'):
            self.status.set('The selected route has no supported native replay contract.')
            return
        try:
            for capability in route.get('required_capabilities', []):
                if not self.runner.device.studio_has_capability(capability):
                    raise ValueError(f'Native profile does not provide {capability}')
        except (OSError, ValueError, RuntimeError) as error:
            self.error(error)
            return
        self.pause_timeline()
        try:
            inputs = self.form()
            origin = default_scenario(route['origin'])
            origin['common'] = deepcopy(inputs['common'])
            validate_scenario(origin)
        except (OSError, ValueError, TypeError) as error:
            self.error(error)
            return
        route_id = route['route_id']
        self.busy = True
        self.apply_button.configure(state='disabled')
        self.status.set(f'Preparing route origin {route["origin"]} before {route["event"]}…')

        # No parameters: serialize owner preparation, event admission and capture.
        def work():
            try:
                self.runner.prepare(origin)
                record = self.runner.replay_route(route_id)
                self.results.put((None, record, None, {'kind': 'route', 'route_id': route_id}))
            except Exception as error:
                self.results.put((None, None, error, {'kind': 'route', 'route_id': route_id}))

        self.executor.submit(work)

    # No parameters: resolve the current symbol line before opening a source editor.
    def open_source(self):
        selected = self.tree.selection()
        if not selected or ':' not in selected[0]:
            return
        try:
            item = entry_for(selected[0])
            if 'source' not in item:
                raise ValueError('No source-symbol contract for this entry yet')
            path, number = resolve_source(item)
            editor = shutil.which('code') or shutil.which('codium')
            if editor:
                subprocess.Popen([editor, '--goto', f'{path}:{number}'], cwd=ROOT)
            self.status.set(f'{path.relative_to(ROOT)}:{number}' + ('' if editor else ' · editor unavailable'))
        except (OSError, ValueError) as error:
            self.error(error)

    # error: Original operation/provenance failure; last valid frame stays visible.
    def error(self, error):
        self.status.set(f'Failed: {error} · last valid capture retained')
        messagebox.showerror('Cody Studio — SIMULATED', str(error), parent=self.root)

    # No parameters: close only this window and retain generated artifacts.
    def close(self):
        self.stop_cue_audition()
        self.pause_timeline()
        if self.atlas_job is not None:
            try:
                self.atlas_job.cancel()
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                self.atlas_log_message(f'Cancellation during close failed: {error}')
        self.atlas_watcher = None
        self.persist()
        self.closed = True
        self.root.after_cancel(self.poll_id)
        self.root.after_cancel(self.initial_apply_id)
        if self.focus_reveal_id is not None:
            self.root.after_cancel(self.focus_reveal_id)
            self.focus_reveal_id = None
        self.root.unbind('<FocusIn>', self.focus_binding)
        self.runner.cancel_worker()
        self.executor.shutdown(wait=False, cancel_futures=True)
        self.atlas_executor.shutdown(wait=False, cancel_futures=True)
        self.root.destroy()
