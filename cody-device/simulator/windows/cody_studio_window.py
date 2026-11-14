"""Flat Tk Studio workspace; native renderers own all device pixels."""

from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import json
import queue
import shutil
import subprocess
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageTk

from cody_native_runtime import ROOT
from cody_semantic_palette import studio_hex
from cody_studio_catalog import entry_for, load_catalog, resolve_source
from cody_studio_runner import StudioRunner, load_capture
from cody_studio_comparison import compare_captures, comparison_image, load_review, preserve_baseline, save_review
from cody_studio_scenario import default_scenario, load_scenario, save_scenario, validate_scenario
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
        self.records, self.sections, self.photos = [], {}, []
        self.busy = self.closed = False
        self.focus_reveal_id = None
        self.results = queue.Queue()
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix='cody-studio')
        self.status = tk.StringVar(value='Preparing native Studio case…')
        self.preferences = self.read_preferences()
        root.title('Cody Studio — SIMULATED')
        root.configure(bg=BG)
        root.geometry('1440x960')
        root.minsize(720, 540)
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.build()
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
                     'left': self.left_visible.get(), 'right': self.right_visible.get()}
            temporary = path.with_suffix('.json.tmp')
            temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
            temporary.replace(path)
        except (OSError, ValueError) as error:
            self.status.set(f'UI preferences not saved: {error}')

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
        ttk.Combobox(self.left, textvariable=self.filter, values=('All', 'Supported', 'Unsupported'),
                     state='readonly', width=25, style='Studio.TCombobox').pack(fill='x')
        self.tree = ttk.Treeview(self.left, show='tree', style='Studio.Treeview', height=15)
        self.tree.pack(fill='both', expand=True, pady=10)
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
        modes = tk.Frame(self.center, bg=BG)
        modes.pack(fill='x')
        self.mode = tk.StringVar(value='current')
        self.mode_controls = []
        for text, mode in (('Current', 'current'), ('Before / After', 'side-by-side'),
                           ('Overlay', 'overlay'), ('Difference', 'difference')):
            control = tk.Radiobutton(modes, text=text, value=mode, variable=self.mode, command=self.draw,
                           bg=BG, fg=FG, selectcolor=FIELD, indicatoron=False, relief='flat', bd=0,
                           activeforeground=ORANGE, activebackground=FIELD, highlightcolor=ORANGE,
                           highlightbackground=BG, highlightthickness=2, padx=7, pady=4)
            control.pack(side='left')
            self.mode_controls.append((mode, control))
        self.canvas = tk.Canvas(self.center, bg=BG, highlightthickness=0, height=180)
        self.canvas.pack(fill='both', expand=True, pady=8)
        self.canvas.bind('<Configure>', lambda event: self.draw())
        bottom_host = tk.Frame(self.center, bg=BG)
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
            self.bottom_canvas.configure(scrollregion=self.bottom_canvas.bbox('all'),
                                         height=min(280, max(36, event.height)))

        bottom.bind('<Configure>', resize_bottom)
        self.bottom_canvas.bind('<Configure>', lambda event: self.bottom_canvas.itemconfigure(bottom_item, width=event.width))
        self.bottom_canvas.bind('<Configure>', lambda event: self.arrange_bottom(), add='+')
        animation = self.section(bottom, 'Animation', False)
        self.refresh_section('Animation', 'Animation · static sample only')
        caption(animation, 'Pilot cases are fixed native samples. Playback, stepping and worker replay are unavailable.',
                wraplength=580).pack(anchor='w')
        routes = self.section(bottom, 'Routes', True)
        self.route_summary = tk.StringVar()
        caption(routes, textvariable=self.route_summary, wraplength=590).pack(anchor='w')
        caption(routes, 'Source analysis · route replay unavailable · no observed replay', 10).pack(anchor='w')
        atlas = self.section(bottom, 'Atlas', True)
        self.thumbnails = tk.Frame(atlas, bg=BG)
        self.thumbnails.pack(fill='x')
        action(atlas, 'Open saved capture…', self.open_capture).pack(anchor='w')
        comparison = self.section(right, 'Comparison', True)
        action(comparison, 'Choose baseline…', self.choose_baseline).pack(fill='x')
        action(comparison, 'Preserve current as baseline', self.keep_baseline).pack(fill='x')
        self.compare_status = tk.StringVar(value='Not comparable · choose a baseline')
        caption(comparison, textvariable=self.compare_status, wraplength=275).pack(fill='x', pady=5)
        caption(comparison, 'Overlay opacity', 10).pack(anchor='w')
        self.opacity = tk.DoubleVar(value=.5)
        tk.Scale(comparison, from_=0, to=1, resolution=.05, orient='horizontal', variable=self.opacity,
                 command=lambda value: self.draw(), bg=BG, fg=FG, troughcolor=FIELD,
                 activebackground=ORANGE, highlightthickness=0).pack(fill='x')
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

    # No parameters: allow explicit side-column collapse on small windows.
    def columns(self):
        for widget, variable in ((self.left, self.left_visible), (self.right_host, self.right_visible)):
            widget.grid() if variable.get() else widget.grid_remove()
        self.persist()

    # args: Ignored Tk trace metadata.
    # Maintained source identities remain visible when their contracts are unsupported.
    def catalog(self, *args):
        self.tree.delete(*self.tree.get_children())
        groups = {}
        for item in load_catalog():
            supported = item['availability'] in ('runtime-full', 'presenter-isolated')
            mode = self.filter.get()
            text = f"{item.get('inventory_id') or item['id']} {item['title']}"
            if self.search.get().casefold() not in text.casefold():
                continue
            if (mode == 'Supported' and not supported) or (mode == 'Unsupported' and supported):
                continue
            group = item.get('group', 'Supported native') if supported else 'Source catalog · unsupported'
            if group not in groups:
                groups[group] = self.tree.insert('', 'end', text=group, open=supported)
            self.tree.insert(groups[group], 'end', iid=item['id'], text=text)

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
        self.detail.set(f"{item['id']}\n{item['availability']}\n{item.get('verification', item.get('reason', ''))}")
        routes = item.get('routes', [])
        incoming = sum(route['destination'] == item['id'] for route in routes)
        outgoing = sum(route['origin'] == item['id'] for route in routes)
        self.refresh_section('Routes', f'Routes · {incoming} in / {outgoing} out')
        self.route_summary.set('\n'.join(f"{r['origin']} → {r['destination']}\n{r['event']} · {r['condition']}\n"
                                        f"{r['component']}::{r['function']}" for r in routes)
                               or 'No declared routes in this Studio contract.')

    # No parameters: load form values without changing native state.
    def show_scenario(self):
        entry = entry_for(self.scenario['context']['target'])
        self.scenario_title.set(f"{entry.get('inventory_id') or entry['id']} · {entry['title']} inputs")
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
        self.show_case_fields(entry)

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
            if key not in ('brightness_percent', 'auto_rotate', 'orientation_degrees'):
                add_fields(('common', 'settings', key), value)

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
        for path, (variable, field_type) in self.case_fields.items():
            target = value
            for key in path[:-1]:
                target = target[key]
            text = variable.get()
            try:
                target[path[-1]] = field_type(text)
            except (ValueError, TypeError):
                raise ValueError(f"{' · '.join(map(str, path))} requires a valid {field_type.__name__}") from None
        validate_scenario(value)
        return value

    # No parameters: serialize native preparation and acquisition off the Tk thread.
    def apply(self):
        if self.busy:
            return
        try:
            scenario = self.form()
        except (ValueError, TypeError) as error:
            self.error(error)
            return
        self.busy = True
        self.apply_button.configure(state='disabled')
        entry = entry_for(scenario['context']['target'])
        self.status.set(f"Preparing {entry.get('inventory_id') or entry['id']} on the native owner…")

        # No parameters: return results through a queue; this worker never calls Tk.
        def work():
            try:
                self.runner.prepare(scenario)
                self.results.put((scenario, self.runner.capture(), None))
            except Exception as error:
                self.results.put((None, None, error))

        self.executor.submit(work)

    # No parameters: adopt worker results only on the Tk thread.
    def poll(self):
        if self.closed:
            return
        try:
            scenario, record, error = self.results.get_nowait()
        except queue.Empty:
            pass
        else:
            self.busy = False
            self.apply_button.configure(state='normal')
            if error:
                self.error(error)
            else:
                self.set_current(record)
                self.status.set(f'Acquired · native completed frame · {record.metadata_path.name} · physical acceptance pending')
        self.poll_id = self.root.after(40, self.poll)

    # record: A verified immutable capture record; never automatically becomes baseline.
    def set_current(self, record):
        review = load_review(record)
        self.current = record
        entry = entry_for(record.metadata['id'])
        self.frame_title.set(f"{entry.get('inventory_id') or entry['id']} — {entry['title']}")
        sample = record.metadata['frame_identity'].get('source_animation_sample_ms', 0)
        timing = f" · source animation {sample} ms" if sample else ''
        self.frame_subtitle.set(f"Native C/C++ · {record.metadata['rendering_mode']} · t={record.metadata['at_ms']}{timing} · 466 × 466")
        if self.tree.exists(record.metadata['id']):
            self.tree.selection_set(record.metadata['id'])
        self.describe_entry(entry)
        if all(item.metadata_path != record.metadata_path for item in self.records):
            self.records.append(record)
        self.note.delete('1.0', 'end')
        self.note.insert('1.0', review['note'])
        self.review.set(review['status'])
        self.refresh_section('Review notes', f"Review notes · {review['count']} · {review['status']}")
        for child in self.thumbnails.winfo_children():
            child.destroy()
        self.photos = []
        for index, item in enumerate(self.records):
            with Image.open(item.image_path) as image:
                photo = ImageTk.PhotoImage(image.resize((72, 72), Image.Resampling.NEAREST))
            self.photos.append(photo)
            control = action(self.thumbnails, f"{item.metadata.get('inventory_id') or item.metadata['id']} · acquired",
                             lambda chosen=item: self.select_capture(chosen))
            control.configure(image=photo, compound='top', font=('TkDefaultFont', 9))
            control.grid(row=index // 4, column=index % 4, sticky='nw', padx=(0, 7))
        screens = len({item.metadata['id'] for item in self.records})
        self.refresh_section('Atlas', f'Atlas · {screens} screens / {len(self.records)} captures')
        self.draw()

    # No parameters: present proportional native pixels or a compatible comparison.
    def draw(self):
        for mode, control in self.mode_controls:
            control.configure(fg=ORANGE if self.mode.get() == mode else FG)
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
            self.refresh_section('Changes', 'Changes · ' + (str(result.changed_pixels) + ' pixels' if result and result.compatible else 'not compared'))
            width, height = max(1, self.canvas.winfo_width()), max(1, self.canvas.winfo_height())
            usable_height = max(1, height - 30)
            factor = min(width / frame.width, usable_height / frame.height)
            size = max(1, round(frame.width * factor)), max(1, round(frame.height * factor))
            self.frame_photo = ImageTk.PhotoImage(frame.resize(size, Image.Resampling.NEAREST))
            self.canvas.delete('all')
            if self.mode.get() == 'side-by-side' and result and result.compatible:
                self.canvas.create_text(width * .25, 10, text='BEFORE · saved baseline', fill=ORANGE, anchor='n')
                self.canvas.create_text(width * .75, 10, text='AFTER · selected capture', fill=FG, anchor='n')
            self.canvas.create_image(width // 2, 30 + usable_height // 2, image=self.frame_photo)
        except (OSError, ValueError, RuntimeError) as error:
            self.error(error)

    # No parameters: explicit preservation never overwrites a previously saved baseline.
    def keep_baseline(self):
        if self.current:
            try:
                self.baseline = preserve_baseline(self.current)
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
                self.draw()
            except (OSError, ValueError) as error:
                self.error(error)

    # No parameters: inspect an existing capture without preparing native state.
    def open_capture(self):
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
        if self.current:
            try:
                note = self.note.get('1.0', 'end').strip()
                save_review(self.current, note, self.review.get())
                review = load_review(self.current)
                self.refresh_section('Review notes', f"Review notes · {review['count']} · {review['status']}")
                self.status.set('Review saved for the selected capture.')
            except (OSError, ValueError) as error:
                self.error(error)

    # No parameters: direct entry is separate from route replay, which remains unavailable.
    def open_destination(self):
        if self.busy:
            self.status.set('Wait for the active native preparation before changing destination.')
            return
        selected = self.tree.selection()
        if not selected or ':' not in selected[0]:
            return
        entry = entry_for(selected[0])
        if entry['availability'] not in ('runtime-full', 'presenter-isolated'):
            self.status.set('The selected catalog entry has no supported direct preparation contract yet.')
            return
        try:
            self.scenario = default_scenario(entry['id'])
            self.show_scenario()
            self.apply()
        except (OSError, ValueError, TypeError) as error:
            self.error(error)

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
        self.persist()
        self.closed = True
        self.root.after_cancel(self.poll_id)
        self.root.after_cancel(self.initial_apply_id)
        if self.focus_reveal_id is not None:
            self.root.after_cancel(self.focus_reveal_id)
            self.focus_reveal_id = None
        self.root.unbind('<FocusIn>', self.focus_binding)
        self.executor.shutdown(wait=False, cancel_futures=True)
        self.root.destroy()
