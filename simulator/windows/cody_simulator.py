"""Local Cody rig preview in the existing Windows simulator toolchain. No network/device I/O."""

from __future__ import annotations

import argparse
from copy import deepcopy
import math
import sys
import time
import tkinter as tk
from pathlib import Path
from tkinter import font as tkfont, ttk

from PIL import Image, ImageDraw, ImageFont, ImageTk

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_rig_contract import (  # noqa: E402
    advance_contour, advance_face, animate, contour_defaults, evaluate, load_contract, make_contour_state,
)
from scripts.common.cody_rig_types import (  # noqa: E402
    CurvePoint, LabelScope, PointRole, Screen,
)
from scripts.common.cody_response import (  # noqa: E402
    SampleHealth, advance_response, compose_response, effective_stamp, make_response_state, rebase_response_motion,
    EyeId, hit_test_eye,
)
from scripts.common.cody_response_contract import Axis, validate_response_config  # noqa: E402
from scripts.common.cody_poc_contract import load_settings, demo_targets  # noqa: E402
from cody_stimuli import SyntheticStimulusSource  # noqa: E402
from cody_preview_layout import (  # noqa: E402
    SIZE, PreviewLayout, logical_touch, physical_point,
)
from runtime_paths import resolve_runtime_output  # noqa: E402

SCREEN_NAMES = tuple(screen.value for screen in Screen)
BOOT_NAMES = {"boot-sleep": "cody-sleep-blue.png", "boot-waking": "cody-waking-blue.png", "boot-ready": "cody-ready-blue.png"}
LABEL_SCOPES = tuple(scope.value for scope in LabelScope)


# value is a native RGB565 word, expanded without inventing extra indexed colors.
def rgb565(value):
    return ((value >> 11 & 31) * 255 // 31, (value >> 5 & 63) * 255 // 63, (value & 31) * 255 // 31)


# image is the logical canvas; rotation is a clockwise cardinal orientation.
# Raises ValueError for a non-cardinal rotation.
def orient(image, rotation):
    modes = {90: Image.Transpose.ROTATE_270, 180: Image.Transpose.ROTATE_180, 270: Image.Transpose.ROTATE_90}
    if rotation == 0:
        return image
    if rotation not in modes:
        raise ValueError(f"Unsupported rotation: {rotation}")
    return image.transpose(modes[rotation])


class CodyRenderer:
    """Authoring tables are immutable for this preview session; evaluated scenes are transient."""

    # root: Active checkout owning the preview sources.
    # skin_path: Selected YAML character; changing the skin does not change the renderer.
    # Raises OSError or ValueError for unreadable artwork or invalid authoring data.
    def __init__(self, root=ROOT, skin_path="assets/cody/cody.yaml"):
        self.rig, self.palette = load_contract(root, skin_path)
        self.native_palette = [int(entry["rgb565"], 16) for entry in self.palette["entries"]]
        self.rgb_palette = [rgb565(value) for value in self.native_palette]
        self.flat_palette = [channel for color in self.rgb_palette for channel in color]
        self.boot = {}
        for screen, name in BOOT_NAMES.items():
            with Image.open(root / "assets/cody/boot" / name) as source:
                # Exact source artwork is previewed; firmware's indexed RLE conversion is separate.
                self.boot[screen] = source.convert("RGB").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        self.font = ImageFont.load_default()

    # color is the initial palette slot of a fresh full logical canvas.
    def indexed(self, color=None):
        background = self.rig["style"]["background"] if color is None else color
        image = Image.new("P", (SIZE, SIZE), background)
        image.putpalette(self.flat_palette)
        return image

    # image receives the polygon in indexed mode or native-color RGB presentation mode.
    # vertices are the already evaluated curve samples in logical coordinates.
    # slots identifies the ordered inclusive palette ramp.
    # opacity is the quantized source-over coverage, from zero through 255.
    # Raises ValueError for opacity outside the explicit byte contract.
    def fill(self, image, vertices, slots, opacity=255):
        if type(opacity) is not int or not 0 <= opacity <= 255:
            raise ValueError(f"Invalid polygon opacity: {opacity}")
        if opacity == 0:
            return
        first, last = slots
        if first == last and opacity == 255:
            ImageDraw.Draw(image).polygon(vertices, fill=self.paint(image, first))
            return
        mask = Image.new("L", (SIZE, SIZE))
        ImageDraw.Draw(mask).polygon(vertices, fill=255)
        top = min(y for _, y in vertices)
        bottom = max(y for _, y in vertices)
        ramp = self.indexed(first)
        draw = ImageDraw.Draw(ramp)
        for row in range(max(0, math.floor(top)), min(SIZE, math.ceil(bottom))):
            # This is edge coverage clipping; channel inputs themselves are never clamped.
            ratio = max(0, min(1, (row + .5 - top) / (bottom - top))) if bottom > top else 0
            slot = first + math.floor(ratio * (last - first) + .5)
            draw.line((0, row, SIZE - 1, row), fill=slot)
        if opacity == 255:
            image.paste(ramp if image.mode == "P" else ramp.convert("RGB"), (0, 0), mask)
            return
        self.composite_fill(image, ramp, mask, opacity)

    # image determines whether the drawing backend expects a palette index or RGB tuple.
    # slot is an authored source color from the shared 256-entry palette.
    def paint(self, image, slot):
        return slot if image.mode == "P" else self.rgb_palette[slot]

    # image is the RGB destination whose pixels are already quantized to RGB565.
    # source contains indexed source colors for the current polygon.
    # mask selects covered logical pixels without altering the authored polygon.
    # opacity is the previously validated byte coverage, strictly between zero and 255.
    # Raises ValueError when a partial-alpha destination has not been expanded to RGB.
    def composite_fill(self, image, source, mask, opacity):
        if image.mode != "RGB":
            raise ValueError("Partial opacity requires an RGB565-quantized RGB destination")
        bounds = mask.getbbox()
        if bounds is None:
            return
        pixels, sources, coverage = image.load(), source.load(), mask.load()
        mixed_colors = {}
        for y in range(bounds[1], bounds[3]):
            for x in range(bounds[0], bounds[2]):
                if not coverage[x, y]:
                    continue
                slot, background = sources[x, y], pixels[x, y]
                key = (slot, background)
                if key not in mixed_colors:
                    red, green, blue = background
                    destination = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
                    source_word = self.native_palette[slot]
                    # Match the firmware's rounded native 5/6/5 channel blend exactly.
                    word = 0
                    for shift, mask_bits in ((11, 31), (5, 63), (0, 31)):
                        foreground = (source_word >> shift) & mask_bits
                        beneath = (destination >> shift) & mask_bits
                        mixed = (foreground * opacity + beneath * (255 - opacity) + 127) // 255
                        word |= mixed << shift
                    mixed_colors[key] = rgb565(word)
                pixels[x, y] = mixed_colors[key]

    # self: Renderer owning the immutable skin and palette for this scene.
    # scene is evaluated once and shared by artwork, outlines and point overlays.
    # debug controls point, link and cubic handle visibility.
    # selected is the stable name of the highlighted point, or None.
    # guide shows the canonical safe square without changing geometry or hit testing.
    # outline shows the evaluated body contour without recomputing its curves.
    # rest_outline shows the undeformed contour with the same placement and breathing scale.
    # response_frame optionally shows the actual resting/current root and inertia vector.
    # Raises ValueError for an invalid evaluated opacity or unsupported partial-alpha target.
    def draw_scene(self, scene, debug=True, selected=None, guide=False, *, outline=False, rest_outline=False, response_frame=None):
        image = self.indexed()
        for path in scene["paths"]:
            if not path["visible"]:
                continue
            definition = path["definition"]
            opacity = math.floor(path["opacity"] * 255 + .5)
            if opacity == 0:
                continue
            if opacity < 255 and image.mode == "P":
                # The source palette stays fixed; true translucency needs derived framebuffer colors.
                image = image.convert("RGB")
            if definition["stroke"]:
                # Pillow wants whole pixels; the device keeps fractional widths.
                width = max(1, round(definition["stroke"] * scene["scale"]))
                draw = ImageDraw.Draw(image)
                color = self.paint(image, definition["paint"][0])
                draw.line(path["vertices"], fill=color, width=width, joint="curve")
                radius = width / 2
                for x, y in (path["vertices"][0], path["vertices"][-1]):
                    draw.ellipse((x-radius, y-radius, x+radius, y+radius), fill=color)
            else:
                self.fill(image, path["vertices"], definition["paint"], opacity)
        draw = ImageDraw.Draw(image)
        metrics = self.rig["style"]["debug_metrics"]
        colors = {name: self.paint(image, slot) for name, slot in self.rig["style"]["debug"].items()}
        if rest_outline:
            vertices = scene["rest_outline"]
            for index in range(0, len(vertices)-1, 2):
                draw.line(vertices[index:index+2], fill=colors["handle"], width=max(1, round(metrics["handle_width"])))
        if outline:
            for path in scene["paths"]:
                if path["definition"]["name"] == self.rig["contour"]["path"]:
                    draw.line(path["vertices"], fill=colors["selected"], width=max(1, round(metrics["outline_width"])))
        if debug:
            for point in self.rig["points"]:
                if point["role"] == PointRole.ANCHOR and point["parent"] is not None:
                    draw.line([scene["world"][point["parent"]], scene["world"][point["name"]]], fill=colors["link"], width=max(1, round(metrics["link_width"])))
            for curve in self.rig["curves"]:
                controls = [scene["world"][name] for name in curve["points"]]
                for anchor, handle in (
                    (CurvePoint.START, CurvePoint.OUTGOING_HANDLE),
                    (CurvePoint.END, CurvePoint.INCOMING_HANDLE),
                ):
                    draw.line(
                        [controls[anchor], controls[handle]],
                        fill=colors["handle"],
                        width=max(1, round(metrics["handle_width"])),
                    )
            for point in self.rig["points"]:
                x, y = scene["world"][point["name"]]
                if point["name"] == selected:
                    diameter = metrics["selected_diameter"]
                elif point["role"] == PointRole.HANDLE:
                    diameter = metrics["handle_diameter"]
                else:
                    diameter = metrics["anchor_diameter"]
                radius = (diameter - 1) / 2
                slot = colors["selected"] if point["name"] == selected else colors[point["role"]]
                draw.ellipse((x-radius, y-radius, x+radius, y+radius), fill=slot)
        if response_frame is not None:
            self.draw_response_roots(image, response_frame, colors)
        self.draw_navigation(image, debug, colors)
        if guide:
            draw.rectangle((73,73,392,392), outline=colors["selected"])
        return image

    # self: Renderer owning the palette and fixed navigation presentation.
    # image: Logical framebuffer before fixed navigation is drawn.
    # frame: Composed root positions also used by the evaluated geometry.
    # colors: Resolved debug colors; source palette allocation stays unchanged.
    def draw_response_roots(self, image, frame, colors):
        draw = ImageDraw.Draw(image)
        resting_x, resting_y = frame.resting_root
        current_x, current_y = frame.current_root
        draw.line((resting_x-6, resting_y, resting_x+6, resting_y), fill=colors["handle"], width=2)
        draw.line((resting_x, resting_y-6, resting_x, resting_y+6), fill=colors["handle"], width=2)
        draw.line((resting_x, resting_y, current_x, current_y), fill=colors["selected"], width=2)
        draw.ellipse((current_x-4, current_y-4, current_x+4, current_y+4), outline=colors["selected"], width=2)

    # image: Logical framebuffer receiving the fixed lateral navigation controls.
    # debug: Whether the overlay-toggle icon should appear selected.
    # colors: Resolved named debug colors for this image's pixel mode.
    def draw_navigation(self, image, debug, colors):
        draw = ImageDraw.Draw(image)
        # Navigation uses the same exact logical circle for rendering and input.
        back = self.rig["style"]["back"]
        back_ink = self.paint(image, back["ink"])
        first, last = back["fill"]
        for row in range(203, 263):
            dy = row + .5 - 233
            half = math.sqrt(900 - dy*dy)
            slot = first + int((dy+30) * (last-first) / 60)
            for center in (40,426):
                draw.line((math.ceil(center-half), row, math.floor(center+half), row), fill=self.paint(image, slot))
        draw.line([(44,222),(33,233),(44,244)], fill=back_ink, width=max(1, round(back["stroke"])))
        nodes = [(416,242),(426,222),(436,242)]
        draw.line(nodes + [nodes[0]], fill=back_ink,
                  width = max(1, round(self.rig["style"]["overlay_toggle"]["stroke"])))
        marker_radius = (self.rig["style"]["overlay_toggle"]["marker_diameter"] - 1) / 2
        for x, y in nodes:
            draw.ellipse((x - marker_radius, y - marker_radius, x + marker_radius, y + marker_radius),
                         fill = colors["selected"] if debug else back_ink)

    # self: Renderer whose evaluated scene is shared by all visible overlays.
    # screen selects the rig or one of the existing boot artworks.
    # values contains the independently composed facial channels.
    # transform contains logical x, y and uniform scale.
    # debug controls point, link and cubic handle visibility.
    # selected identifies the highlighted stable point name, or None.
    # guide shows the canonical safe square.
    # rotation transforms the whole device image with the shared cardinal transform.
    # contour is the current independent outline state, or None for undeformed geometry.
    # outline enables the evaluated body contour overlay.
    # rest_outline enables the original body contour overlay.
    # response_frame enables resting/current root overlays without recomputing geometry.
    # Raises ValueError for an unknown screen or invalid rig values.
    def render(self, screen, values, transform, debug=True, selected=None, guide=False, rotation=0, *, contour=None, outline=False, rest_outline=False, response_frame=None):
        if screen not in SCREEN_NAMES:
            raise ValueError(f"Unknown Cody screen: {screen}")
        scene = evaluate(self.rig, values, transform, contour=contour)
        if screen == Screen.RIG:
            image = self.draw_scene(scene, debug, selected, guide,
                                    outline=outline, rest_outline=rest_outline, response_frame=response_frame)
        else:
            image = self.boot[screen].copy()
        circular = Image.new("L", (SIZE, SIZE))
        ImageDraw.Draw(circular).ellipse((0,0,SIZE-1,SIZE-1), fill=255)
        image = image.convert("RGB")
        image.paste((0,0,0), (0,0,SIZE,SIZE), Image.eval(circular, lambda v:255-v))
        return orient(image, rotation), scene

    # image is a rendered device view; caption identifies the synthetic desktop context.
    def capture_sheet(self, image, caption):
        sheet = Image.new("RGB", (SIZE, SIZE+42), "#101820")
        sheet.paste(image, (0,0))
        ImageDraw.Draw(sheet).text((12,SIZE+12), "SYNTHETIC / " + caption, fill="#75CBF3", font=self.font)
        return sheet


class CodyWindow:
    # self: New preview controller owning transient animation and synthetic model state.
    # root owns the Tk preview window and its scheduled frame callback.
    # renderer owns immutable authored tables and the shared scene drawing code.
    # args selects the initial screen, rotation and safe-square overlay.
    # Raises tk.TclError for failed widget construction or lost Tk ownership.
    def __init__(self, root, renderer, args):
        self.root, self.renderer = root, renderer
        self.root.title(f"{renderer.rig['character']['name']} / Rig PoC / SYNTHETIC")
        self.screen = tk.StringVar(value=args.screen)
        self.rotation = tk.IntVar(value=args.rotation)
        self.debug = tk.BooleanVar(value=True)
        self.names = tk.BooleanVar(value=True)
        self.outline = tk.BooleanVar(value=True)
        self.rest_outline = tk.BooleanVar(value=True)
        self.label_scope = tk.StringVar(value=LabelScope.CONTOUR.value)
        self.label_page = tk.IntVar(value=0)
        self.guide = tk.BooleanVar(value=args.safe_guide)
        self.playing = tk.BooleanVar(value=True)
        self.contour_playing = tk.BooleanVar(value=True)
        self.contour_state = make_contour_state(renderer.rig)
        self.entity_name = next(iter(renderer.rig["entities"]))
        self.face_state = [channel["default"] for channel in renderer.rig["channels"]]
        self.face_presets = renderer.rig["boot_channels"] | renderer.rig["expression_presets"]
        self.response_config = deepcopy(renderer.rig["response"])
        self.response_state = make_response_state(self.response_config)
        self.stimulus_source = SyntheticStimulusSource()
        self.poc_settings = load_settings(ROOT / "assets/cody/poc.yaml")
        self.demo_enabled = tk.BooleanVar(value=False)
        self.response_frame = None
        self.motion_overlay = tk.BooleanVar(value=True)
        self.elapsed_ms = 0.0
        self.last_tick = time.perf_counter()
        self.selected = None
        self.scene = None
        self.after_id = None
        self.layout_mode = None
        self.pending_canvas_size = (1,1)
        self.layout = PreviewLayout.fit(1,1,False)
        self.display_rotation = args.rotation
        self.display_screen = args.screen
        self.label_text_cache = {}
        self.canvas = tk.Canvas(root, width=600, height=420,
                                highlightthickness = 0, bg="#101820")
        self.canvas.grid(row=0,column=0,sticky="nsew")
        self.canvas.bind("<Button-1>", self.touch)
        self.canvas.bind("<Configure>",self.resize_preview)
        self.image_item = self.canvas.create_image(0,0,anchor="nw")
        self.controls_host = ttk.Frame(root)
        self.controls_host.grid(row=0,column=1,sticky="nsew")
        self.controls_host.rowconfigure(0,weight=1)
        self.controls_host.columnconfigure(0,weight=1)
        self.controls_view = tk.Canvas(self.controls_host,width=360,height=160,highlightthickness=0)
        self.controls_view.grid(row=0,column=0,sticky="nsew")
        scroll = ttk.Scrollbar(self.controls_host,orient="vertical",command=self.controls_view.yview)
        scroll.grid(row=0,column=1,sticky="ns")
        self.controls_view.configure(yscrollcommand=scroll.set)
        controls = ttk.Frame(self.controls_view,padding=12)
        self.controls_item = self.controls_view.create_window(0,0,window=controls,anchor="nw")
        self.controls_view.bind("<Configure>",self.resize_controls)
        controls.bind("<Configure>",self.update_controls_scroll)
        font = renderer.rig["style"]["host_font"]
        self.label_font = tkfont.Font(root=root,family=font["family"],size=font["size"])
        self.label_row_height = self.label_font.metrics("linespace")+6
        ttk.Label(controls,text=f"{renderer.rig['character']['name']} / LOCAL RIG",font=(font["family"],12,"bold")).pack(anchor="w")
        ttk.Label(controls,text="SYNTHETIC INPUT / no sensor or PC link").pack(anchor="w")
        ttk.Combobox(controls,textvariable=self.screen,values=SCREEN_NAMES,state="readonly",width=27).pack(fill="x",pady=6)
        ttk.Combobox(controls,textvariable=self.rotation,values=(0,90,180,270),state="readonly").pack(fill="x")
        tabs = ttk.Notebook(controls)
        tabs.pack(fill="both",pady=8)
        appearance = ttk.Frame(tabs,padding=8)
        contour = ttk.Frame(tabs,padding=8)
        stimuli = ttk.Frame(tabs,padding=8)
        overlay = ttk.Frame(tabs,padding=8)
        tabs.add(appearance,text="Face")
        tabs.add(contour,text="Contour")
        tabs.add(stimuli,text="Stimuli")
        tabs.add(overlay,text="Debug")
        tabs.select(stimuli)
        self.build_face_controls(appearance)
        self.build_contour_controls(contour)
        self.build_stimulus_controls(stimuli)
        self.build_debug_controls(overlay)
        self.info = ttk.Label(controls,text="Select a point to inspect its stable name.",width=38,font=self.label_font)
        self.info.pack(anchor="w",pady=6)
        self.timing = ttk.Label(root,text="SYNTHETIC / host timings only")
        self.timing.grid(row=1,column=0,columnspan=2,sticky="w",padx=8,pady=8)
        self.root.bind("<Escape>",self.back)
        self.root.bind("<Configure>",self.resize_window)
        self.root.bind("<MouseWheel>",self.scroll_controls)
        self.root.protocol("WM_DELETE_WINDOW",self.close)
        width = min(1280,max(640,self.root.winfo_screenwidth()-80))
        height = min(760,max(480,self.root.winfo_screenheight()-100))
        self.root.minsize(640,480)
        self.root.geometry(f"{width}x{height}")
        self.arrange_panels(width)
        self.tick()

    # appearance: Face tab owning the named sliders and expression buttons.
    def build_face_controls(self, appearance):
        # Targets belong to the controls; smoothing belongs to the animation state.
        self.add_toggle(appearance,"Animate breath and blink",self.playing)
        self.add_toggle(appearance,"Run authored hardware demo cycle",self.demo_enabled)
        phase_row = ttk.Frame(appearance)
        phase_row.pack(fill="x",pady=6)
        for phase in ("sleep","waking","ready"):
            ttk.Button(phase_row,text=phase.title(),command=lambda value=phase:self.preset(value)).pack(side="left")
        expression_row = ttk.Frame(appearance)
        expression_row.pack(fill="x",pady=(0,6))
        for name in self.renderer.rig["expression_presets"]:
            ttk.Button(expression_row,text=name.replace("_"," ").title(),command=lambda value=name:self.preset(value)).pack(side="left")
        self.values = []
        for channel in self.renderer.rig["channels"]:
            value = tk.DoubleVar(value=channel["default"])
            self.values.append(value)
            self.add_slider(appearance,channel["name"],channel["minimum"],channel["maximum"],value)

    # contour: Outline tab owning the independent wave controls.
    def build_contour_controls(self, contour):
        # A new target nudges the wave; it never starts the clock over.
        ttk.Label(contour,text="Change the cloud shape independently.\nTargets blend without restarting the wave.",wraplength=295).pack(anchor="w",pady=(0,8))
        self.add_toggle(contour,"Run contour / uncheck to freeze",self.contour_playing)
        ttk.Label(contour,text="While paused, targets apply on resume.").pack(anchor="w",pady=(0,8))
        self.contour_values = {}
        self.contour_readouts = {}
        defaults = contour_defaults(self.renderer.rig)
        for control in self.renderer.rig["contour"]["controls"]:
            name = control["name"]
            value = tk.DoubleVar(value=defaults[name])
            self.contour_values[name]=value
            self.contour_readouts[name]=self.add_slider(contour,control["label"],control["minimum"],control["maximum"],value,readout=True)
        ttk.Label(contour,text="Readouts: current > target\nSpeed: cycles/s; phase spread: normalized\nFullness changes the resting shape.",wraplength=295).pack(anchor="w",pady=8)
        self.contour_info = ttk.Label(contour,text="",font=self.label_font)
        self.contour_info.pack(anchor="w",pady=4)
        ttk.Button(contour,text="Restore contour targets",command=self.reset_contour).pack(fill="x",pady=6)

    # self: Preview controller owning shared geometry/response overlay switches.
    # overlay: Debug tab owning point selection, labels and placement.
    # Raises tk.TclError when widgets cannot be constructed.
    def build_debug_controls(self, overlay):
        for text,variable in (("Points, links and curve handles",self.debug),
                              ("Stable point names + leader lines",self.names),
                              ("Current contour / mint",self.outline),
                              ("Original contour / dashed amber",self.rest_outline),
                              ("Resting root + inertia vector",self.motion_overlay),
                              ("320 x 320 safe square",self.guide)):
            self.add_toggle(overlay,text,variable)
        scope = ttk.Combobox(overlay,textvariable=self.label_scope,values=LABEL_SCOPES,state="readonly")
        scope.pack(fill="x",pady=(8,2))
        scope.bind("<<ComboboxSelected>>",self.reset_label_page)
        pages = ttk.Frame(overlay)
        pages.pack(fill="x",pady=2)
        ttk.Button(pages,text="<",width=3,command=lambda:self.change_label_page(-1)).pack(side="left")
        self.page_info = ttk.Label(pages,text="")
        self.page_info.pack(side="left",padx=8)
        ttk.Button(pages,text=">",width=3,command=lambda:self.change_label_page(1)).pack(side="right")
        self.entity_choices = {
            f"{entity['label']} [{name}]": name
            for name, entity in self.renderer.rig["entities"].items()
        }
        self.entity_picker = ttk.Combobox(
            overlay, values=list(self.entity_choices), state="readonly"
        )
        self.entity_picker.pack(fill="x",pady=(6,2))
        self.entity_picker.current(0)
        self.entity_picker.bind("<<ComboboxSelected>>",self.select_entity)
        self.point_picker = ttk.Combobox(overlay,values=[point["name"] for point in self.renderer.rig["points"]],state="readonly")
        self.point_picker.pack(fill="x",pady=(2,6))
        self.point_picker.bind("<<ComboboxSelected>>",self.select_point)
        self.transform = []
        for key,minimum,maximum in (("x",80,386),("y",100,360),("scale",60,180)):
            value = tk.DoubleVar(value=self.renderer.rig['default_transform'][key])
            self.transform.append(value)
            self.add_slider(overlay,key,minimum,maximum,value)

    # self: Preview controller whose source targets stay separate from artistic model state.
    # stimuli: Responsive, scrollable tab owning synthetic source controls and model readouts.
    # Raises tk.TclError when widgets cannot be constructed.
    def build_stimulus_controls(self, stimuli):
        ttk.Label(stimuli, text="SYNTHETIC SNAPSHOT / 50 Hz\nAxes: board frame, m/s2 offsets.\nZ includes a 9.81 m/s2 gravity baseline.", wraplength=295).pack(anchor="w")
        health_choices = tuple(health.value for health in SampleHealth)
        self.motion_health = tk.StringVar(value=SampleHealth.CURRENT.value)
        ttk.Combobox(stimuli, textvariable=self.motion_health, values=health_choices, state="readonly").pack(fill="x", pady=6)
        self.acceleration = {}
        self.acceleration_readouts = {}
        for axis in Axis:
            variable = tk.DoubleVar(value=0)
            self.acceleration[axis] = variable
            self.acceleration_readouts[axis] = self.add_slider(stimuli, f"Acceleration {axis.value.upper()}", -20, 20, variable, readout=True)
        self.motion_sensitivity = tk.DoubleVar(value=self.response_config["motion"]["sensitivity"])
        self.sensitivity_readout = self.add_slider(stimuli, "Motion sensitivity", 0, 4, self.motion_sensitivity, readout=True)
        pulse_row = ttk.Frame(stimuli)
        pulse_row.pack(fill="x", pady=6)
        self.pulse_axis = tk.StringVar(value=Axis.X.value)
        ttk.Combobox(pulse_row, textvariable=self.pulse_axis, values=tuple(axis.value for axis in Axis), state="readonly", width=4).pack(side="left")
        ttk.Button(pulse_row, text="Pulse +8 m/s2", command=self.pulse_motion).pack(side="right", fill="x", expand=True)
        self.shaking = tk.BooleanVar(value=False)
        self.add_toggle(stimuli, "Continuous shake / all axes", self.shaking)
        ttk.Separator(stimuli).pack(fill="x", pady=10)
        ttk.Label(stimuli, text="SYNTHETIC BATTERY / percent").pack(anchor="w")
        self.power_health = tk.StringVar(value=SampleHealth.CURRENT.value)
        ttk.Combobox(stimuli, textvariable=self.power_health, values=health_choices, state="readonly").pack(fill="x", pady=6)
        self.charge_percent = tk.DoubleVar(value=100)
        self.charge_readout = self.add_slider(stimuli, "Charge level", 0, 100, self.charge_percent, readout=True)
        self.charging = tk.BooleanVar(value=False)
        self.add_toggle(stimuli, "Charging / fatigue follows charge level", self.charging)
        self.battery_present = tk.BooleanVar(value=True)
        self.add_toggle(stimuli, "Battery present / otherwise UNAVAILABLE", self.battery_present)
        ttk.Separator(stimuli).pack(fill="x", pady=10)
        ttk.Label(stimuli, text="SYNTHETIC AUDIO / normalized PCM means").pack(anchor="w")
        self.microphone_health = tk.StringVar(value=SampleHealth.CURRENT.value)
        ttk.Combobox(stimuli, textvariable=self.microphone_health, values=health_choices, state="readonly").pack(fill="x")
        self.microphone_levels = (tk.DoubleVar(value=0), tk.DoubleVar(value=0))
        for channel, variable in zip(("Left", "Right"), self.microphone_levels):
            self.add_slider(stimuli, f"Microphone {channel}", 0, 1, variable)
        self.playback_health = tk.StringVar(value=SampleHealth.CURRENT.value)
        ttk.Combobox(stimuli, textvariable=self.playback_health, values=health_choices, state="readonly").pack(fill="x")
        self.playback_active = tk.BooleanVar(value=False)
        self.add_toggle(stimuli, "Synthetic playback active", self.playback_active)
        self.playback_level = tk.DoubleVar(value=.18)
        self.add_slider(stimuli, "Playback PCM mean", 0, 1, self.playback_level)
        for cue, label in (("audio_ready", "Preview audio-ready cue"), ("eyes_open", "Preview eyes-open strings")):
            ttk.Button(stimuli, text=label, command=lambda selected=cue: self.preview_cue(selected)).pack(fill="x", pady=3)
        ttk.Button(stimuli, text="Interaction / cancel idle yawn", command=self.preview_interaction).pack(fill="x", pady=3)
        self.response_info = ttk.Label(stimuli, text="", font=self.label_font, wraplength=295)
        self.response_info.pack(fill="x", pady=8)
        ttk.Button(stimuli, text="Reset response model", command=self.reset_response).pack(fill="x")

    # self: Preview controller; this button plays a local authored WAV, never the device speaker.
    # cue: Stable identity of one of the two generated original boot cues.
    # Raises OSError or RuntimeError if Windows cannot play the existing asset.
    def preview_cue(self, cue):
        import winsound
        path = ROOT / "assets/cody/poc/generated" / f"cody-{cue}.wav"
        winsound.PlaySound(str(path), winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT)

    # self: Preview controller publishing explicit activity through the synthetic snapshot owner.
    def preview_interaction(self):
        self.stimulus_source.interact(int(time.perf_counter() * 1_000_000))

    # self: Preview controller supplying the chosen board axis to its synthetic source.
    # Raises ValueError for an unsupported selected axis, or tk.TclError on a lost control.
    def pulse_motion(self):
        self.stimulus_source.pulse(self.pulse_axis.get(), int(time.perf_counter() * 1_000_000))

    # self: Preview controller receiving an explicit debug-only reset of model state.
    def reset_response(self):
        self.response_state = make_response_state(self.response_config)
        self.response_frame = None

    # self: Preview controller that reads controls and obtains one synthetic snapshot.
    # now_us: Monotonic host time for the single synthetic snapshot owner.
    # rotation: Cardinal preview orientation; synthetic acceleration remains in board axes.
    # Raises ValueError for an unknown health identity, or tk.TclError on lost controls.
    def observe_stimuli(self, now_us, rotation):
        acceleration = {axis: value.get() for axis, value in self.acceleration.items()}
        return self.stimulus_source.snapshot(now_us, acceleration, self.motion_health.get(),
            self.charge_percent.get(), self.charging.get(),
            self.power_health.get() if self.battery_present.get() else SampleHealth.UNAVAILABLE,
            self.shaking.get(), rotation,
            microphone_levels=tuple(level.get() for level in self.microphone_levels),
            microphone_health=self.microphone_health.get(), playback_level=self.playback_level.get(),
            playback_active=self.playback_active.get(), playback_health=self.playback_health.get())

    # self: Preview controller owning source and artistic-state readouts.
    # inputs: The exact synthetic snapshot consumed by this frame, never a device observation.
    # screen: Current route; boot artwork freezes the response animation.
    # Raises ValueError for inconsistent metadata, or tk.TclError on lost controls.
    def update_stimulus_readouts(self, inputs, screen):
        for axis, readout in self.acceleration_readouts.items():
            readout.configure(text=f"{self.acceleration[axis].get():+.2f}")
        self.sensitivity_readout.configure(text=f"{self.motion_sensitivity.get():.2f}")
        self.charge_readout.configure(text=f"{self.charge_percent.get():.1f}% target")
        state = self.response_state
        motion = state.motion_status
        power = state.power_status
        if screen != Screen.RIG:
            now_us = int(time.perf_counter() * 1_000_000)
            motion = effective_stamp(inputs.motion.stamp, now_us, self.response_config["motion"]["sample_timeout_seconds"], "motion.acceleration")
            power = effective_stamp(inputs.power.stamp, now_us, self.response_config["power"]["sample_timeout_seconds"], "power.charge")
        motion_text = f"Motion: {motion.health.value} / err {motion.error}"
        if motion.health == SampleHealth.CURRENT:
            sample = inputs.motion.acceleration_mps2
            motion_text += f"\na: {sample[Axis.X]:+.1f}, {sample[Axis.Y]:+.1f}, {sample[Axis.Z]:+.1f}"
        power_text = f"Charge: {power.health.value} / err {power.error}"
        if power.health == SampleHealth.CURRENT:
            power_text += f" / {inputs.power.level_percent:.1f}%"
        fatigue_text = f"{state.fatigue:.3f}" if state.fatigue_known else "UNKNOWN"
        if state.fatigue_known and power.health != SampleHealth.CURRENT:
            fatigue_text += " (held)"
        body_text = ""
        if self.response_frame is not None:
            frame = self.response_frame
            dx = frame.current_root[0] - frame.resting_root[0]
            dy = frame.current_root[1] - frame.resting_root[1]
            vx, vy = frame.spring_velocity_px_per_second
            body_text = f"\nRoot: {dx:+.1f}, {dy:+.1f} px\nSpring v: {vx:+.1f}, {vy:+.1f} px/s\nDepth: {frame.depth_scale:.4f}"
        status = "RUNNING" if screen == Screen.RIG else "PAUSED / boot artwork"
        self.response_info.configure(text=f"{status} / SYNTHETIC\n{motion_text}\n{power_text}\n"
            f"Microphone: {state.microphone_status.health.value}\nPlayback: {state.playback_status.health.value}\n"
            f"MODEL attention: {state.attention:.3f} / speaking: {state.speaking:.3f}\n"
            f"MODEL idle: {state.idle_seconds:.1f}s / yawn: {state.yawn_weight:.3f}\n"
            f"MODEL annoyance: {state.annoyance:.3f} / eye closure: {state.protective_eye_closure:.3f}\n"
            f"MODEL stress: {state.stress:.3f}\nMODEL fatigue: {fatigue_text}{body_text}")

    # parent owns the shared checkbox row.
    # text describes the independent setting in English.
    # variable is the existing BooleanVar that owns the setting.
    def add_toggle(self, parent, text, variable):
        ttk.Checkbutton(parent,text=text,variable=variable).pack(anchor="w")

    # parent owns the shared label/slider row.
    # label identifies the authored control.
    # minimum is the lower bound accepted by the underlying contract.
    # maximum is the upper bound accepted by the underlying contract.
    # variable owns the target value independently from rendered state.
    # readout requests a right-aligned label for current/target values.
    def add_slider(self, parent, label, minimum, maximum, variable, readout=False):
        header = ttk.Frame(parent)
        header.pack(fill="x",pady=(8,0))
        ttk.Label(header,text=label).pack(side="left")
        output = None
        if readout:
            output = ttk.Label(header,text="",font=self.label_font)
            output.pack(side="right")
        ttk.Scale(parent,from_=minimum,to=maximum,variable=variable,length=280).pack(fill="x")
        return output

    # width is the current outer-window width used only to choose panel arrangement.
    def arrange_panels(self, width):
        mode = "wide" if width>=1040 else "stacked"
        if mode==self.layout_mode:
            return
        self.layout_mode = mode
        self.root.columnconfigure(0,weight=1,minsize=0)
        self.root.columnconfigure(1,weight=0,minsize=0)
        self.root.rowconfigure(2,weight=0,minsize=0)
        if mode=="wide":
            self.root.rowconfigure(0,weight=1,minsize=0)
            self.root.rowconfigure(1,weight=0,minsize=0)
            self.controls_host.grid(row=0,column=1,sticky="nsew")
            self.timing.grid(row=1,column=0,columnspan=2,sticky="ew")
        else:
            self.root.rowconfigure(0,weight=3,minsize=200)
            self.root.rowconfigure(1,weight=2,minsize=120)
            self.controls_host.grid(row=1,column=0,sticky="nsew")
            self.timing.grid(row=2,column=0,columnspan=1,sticky="ew")

    # event is a root Configure notification; child notifications do not change panel layout.
    def resize_window(self, event):
        if event.widget is not self.root:
            return
        self.arrange_panels(event.width)
        self.timing.configure(wraplength=max(160,event.width-24))

    # event supplies the allocated preview dimensions, coalesced until the next normal frame.
    def resize_preview(self, event):
        self.pending_canvas_size = (max(1,event.width),max(1,event.height))

    # event supplies the visible control viewport width, independent of its scrolling height.
    def resize_controls(self, event):
        self.controls_view.itemconfigure(self.controls_item,width=max(1,event.width))

    # event reports a controls-content size change after a tab or viewport resize.
    def update_controls_scroll(self, event):
        bounds = self.controls_view.bbox(self.controls_item)
        if bounds is not None:
            self.controls_view.configure(scrollregion=bounds)

    # event supplies a wheel delta; only descendants of the controls pane are scrolled.
    def scroll_controls(self, event):
        widget = event.widget
        if isinstance(widget,(ttk.Scale,ttk.Combobox)):
            return
        while widget is not None and widget is not self.controls_host:
            widget = getattr(widget,"master",None)
        if widget is self.controls_host and event.delta:
            self.controls_view.yview_scroll(-1 if event.delta>0 else 1,"units")
            return "break"

    # cancel the scheduled callback before destroying the interactive preview.
    def close(self):
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.root.destroy()

    # name selects a validated boot or expression preset as smooth channel targets.
    # Raises KeyError when the requested preset is not defined in the YAML catalog.
    def preset(self, name):
        values = self.face_presets[name]
        self.screen.set(Screen.RIG.value)
        for target,value in zip(self.values,values):
            target.set(value)

    # restore authored targets without resetting the continuous phase.
    def reset_contour(self):
        for name,value in contour_defaults(self.renderer.rig).items():
            self.contour_values[name].set(value)

    # event is the optional scope-selection notification from Tk.
    def reset_label_page(self, event=None):
        self.label_page.set(0)

    # direction is -1 for the previous label page or +1 for the next page.
    def change_label_page(self, direction):
        count = max(1,math.ceil(len(self.label_candidates())/self.label_page_size()))
        self.label_page.set((self.label_page.get()+direction)%count)

    # reserve room for a selected point without crowding short windows.
    def label_page_size(self):
        capacity = self.layout.label_capacity(self.label_row_height)
        return max(1,capacity-(1 if self.selected is not None else 0))

    # select stable point names from the authored geometry, without recomputing it.
    # Raises ValueError for an unsupported label scope.
    def label_candidates(self):
        points = self.renderer.rig["points"]
        scope = self.label_scope.get()
        if scope == LabelScope.ANCHORS:
            return [point["name"] for point in points if point["role"] == PointRole.ANCHOR]
        if scope == LabelScope.CONTOUR:
            contour_names = {lobe["point"] for lobe in self.renderer.rig["contour"]["lobes"]}
            return [point["name"] for point in points if point["name"] in contour_names]
        if scope == LabelScope.POINT:
            return [self.selected] if self.selected is not None else []
        if scope == LabelScope.ALL:
            return [point["name"] for point in points]
        if scope != LabelScope.ENTITY:
            raise ValueError(f"Unknown point label scope: {scope}")
        entity = self.renderer.rig["entities"][self.entity_name]
        names = {entity["anchor"]}
        for path in self.renderer.rig["paths"]:
            if path["name"] in entity["paths"]:
                for curve in self.renderer.rig["curves"][path["first_curve"]:path["first_curve"]+path["curve_count"]]:
                    names.update(curve["points"])
        return [point["name"] for point in points if point["name"] in names]

    # paginate dense label scopes and always retain the inspected point.
    def overlay_names(self):
        candidates = self.label_candidates()
        capacity = self.label_page_size()
        pages = max(1,math.ceil(len(candidates)/capacity))
        page = self.label_page.get()%pages
        start = page*capacity
        names = candidates[start:start+capacity]
        if self.selected is not None and self.selected not in names:
            names.append(self.selected)
        self.page_info.configure(text=f"{page+1} / {pages} ({len(candidates)} points)")
        return names

    # name is the stable authored identifier selected through a marker, list or label.
    def choose_point(self, name):
        self.selected = name
        self.point_picker.set(name if name is not None else "")

    # event identifies a stable point from the point-selection list.
    def select_point(self, event):
        self.choose_point(self.point_picker.get())

    # name is a stable point identifier; the inspector always keeps its complete spelling.
    # width is the available text width in desktop pixels, without shrinking the font.
    def fit_label(self, name, width):
        width = max(0,int(width))
        key = (name,width)
        if key in self.label_text_cache:
            return self.label_text_cache[key]
        if self.label_font.measure(name)<=width:
            text = name
        elif self.label_font.measure("...")>width:
            text = ""
        else:
            lower,upper = 0,len(name)
            while lower<upper:
                middle = (lower+upper+1)//2
                if self.label_font.measure(name[:middle]+"...")<=width:
                    lower = middle
                else:
                    upper = middle-1
            text = name[:lower]+"..."
        self.label_text_cache[key] = text
        return text

    # upright desktop labels connect to the same rotated, evaluated points.
    # Raises ValueError for an unsupported label scope or displayed rotation.
    def draw_labels(self):
        self.canvas.delete("rig_label")
        names = self.overlay_names()
        if not self.names.get() or self.display_screen != Screen.RIG or self.scene is None or self.layout.height<self.label_row_height+24:
            return
        colors = self.renderer.rig["style"]["debug"]
        palette = self.renderer.rgb_palette
        color = lambda slot:"#%02x%02x%02x" % palette[slot]
        metrics = self.renderer.rig["style"]["debug_metrics"]
        marker_radius = metrics["label_marker_diameter"] / 2
        points = []
        for name in names:
            x,y = self.layout.to_canvas(*self.scene["world"][name],self.display_rotation)
            points.append((name,x,y))
        # Splitting by horizontal rank bounds both margins even for a dense cluster.
        points.sort(key=lambda point:point[1])
        midpoint = (len(points)+1)//2
        for side,group in (("left",points[:midpoint]),("right",points[midpoint:])):
            group.sort(key=lambda point:point[2])
            for index,(name,x,y) in enumerate(group):
                row = 12+(self.layout.height-24)*(index+.5)/max(1,len(group))
                label_x = self.layout.left-12 if side=="left" else self.layout.left+self.layout.image_size+12
                elbow = self.layout.left-6 if side=="left" else self.layout.left+self.layout.image_size+6
                width = label_x-8 if side=="left" else self.layout.width-label_x-8
                ink = color(colors["selected"] if name==self.selected else colors["anchor"])
                tags = ("rig_label",f"point:{name}")
                self.canvas.create_line(label_x,row,elbow,row,x,y,fill=color(colors["link"]), width=metrics["link_width"], tags=tags)
                self.canvas.create_oval(x - marker_radius, y - marker_radius,
                                        x + marker_radius, y + marker_radius,
                                        outline=ink, width=metrics["outline_width"], tags=tags)
                self.canvas.create_text(label_x,row,text=self.fit_label(name,width),anchor="e" if side=="left" else "w",
                                        fill = ink,font=self.label_font,tags=tags)

    # event identifies the selected entity from the YAML catalog.
    def select_entity(self, event):
        selection = self.entity_picker.get()
        if selection:
            # The visible label resolves to an ID; changing list order changes nothing.
            self.entity_name = self.entity_choices[selection]
            entity = self.renderer.rig["entities"][self.entity_name]
            self.choose_point(entity["anchor"])
            self.reset_label_page()

    # event is optional keyboard input; both keyboard and pointer use the displayed route.
    def back(self, event=None):
        if self.display_screen == Screen.RIG:
            self.screen.set(Screen.BOOT_READY.value)

    # event supplies desktop coordinates projected through the last displayed layout and rotation.
    def touch(self, event):
        self.preview_interaction()
        clicked_name = None
        for item in self.canvas.find_withtag("current"):
            for tag in self.canvas.gettags(item):
                if tag.startswith("point:"):
                    clicked_name = tag.removeprefix("point:")
        logical = self.layout.to_logical(event.x,event.y,self.display_rotation)
        if logical is None:
            if clicked_name is not None and self.display_screen == Screen.RIG:
                self.choose_point(clicked_name)
            return
        x,y = logical
        if (x-233)**2+(y-233)**2>233**2:
            return
        if self.display_screen == Screen.BOOT_READY and 73<=x<393 and 73<=y<393:
            self.screen.set(Screen.RIG.value)
            return
        if self.display_screen != Screen.RIG:
            return
        if (x-40)**2+(y-233)**2<=900:
            self.back()
            return
        if (x-426)**2+(y-233)**2<=900:
            self.debug.set(not self.debug.get())
            self.choose_point(None)
            return
        # Debug leaders never override the device's fixed navigation touch targets.
        if self.scene is not None:
            eye = hit_test_eye(self.scene, (x, y), self.response_config["touch"]["eye_hit_radius"])
            if eye != EyeId.NONE:
                self.stimulus_source.tap_eye(eye, int(time.perf_counter() * 1_000_000))
        if clicked_name is not None:
            self.choose_point(clicked_name)
            return
        if not self.debug.get() and not self.names.get():
            return
        if self.scene is None:
            return
        nearest = min(self.scene['world'],key=lambda name:(x-self.scene['world'][name][0])**2+(y-self.scene['world'][name][1])**2)
        px,py = self.scene['world'][nearest]
        self.choose_point(nearest if (x-px)**2+(y-py)**2<=144 else None)

    # visible terminal errors stop the preview rather than retaining a false healthy state.
    def tick(self):
        try:
            self.render_tick()
        except Exception as error:
            self.playing.set(False)
            self.contour_playing.set(False)
            self.timing.configure(text=f"PREVIEW STOPPED / {type(error).__name__}: {error}")
            self.info.configure(text="Rendering failed. See console for the original error.")
            self.root.report_callback_exception(type(error), error, error.__traceback__)


    # self: Preview controller holding independent face targets and their transition state.
    # screen: Route drawn during this frame; hidden routes do not advance animation.
    # active_ms: Actual elapsed time since the previous active frame.
    # Raises ValueError for rejected targets or invalid elapsed time.
    def advance_animation(self, screen, active_ms):
        face_targets = [value.get() for value in self.values]
        targets = {name: value.get() for name, value in self.contour_values.items()}
        if self.demo_enabled.get() and screen == Screen.RIG:
            _, face_targets, targets = demo_targets(self.poc_settings, self.renderer.rig, self.elapsed_ms)
        self.face_state = advance_face(
            self.renderer.rig, self.face_state, face_targets, active_ms
        )
        values = self.face_state
        if self.playing.get() and screen != Screen.RIG:
            values = animate(self.face_state, self.elapsed_ms)
        # The single response composer adds effects before the contour transition.
        return values, targets

    # image: Completed logical framebuffer; its geometry and hit targets stay unchanged.
    # layout: Desktop viewport computed for this frame.
    # screen: Route associated with the displayed scene.
    # rotation: Cardinal orientation used for both display and subsequent input.
    # Raises tk.TclError if Tk cannot publish the frame.
    def present_frame(self, image, layout, screen, rotation):
        # A bigger coat for the same skeleton: scale only the presentation.
        display_image = image.resize(
            (layout.image_size, layout.image_size),
            Image.Resampling.NEAREST,
        )
        self.photo = ImageTk.PhotoImage(display_image)
        if layout != self.layout:
            self.label_text_cache.clear()

        self.layout = layout
        self.display_rotation = rotation
        self.display_screen = screen
        self.canvas.coords(self.image_item, layout.left, layout.top)
        self.canvas.itemconfigure(self.image_item, image=self.photo)
        self.draw_labels()

    # targets: Current named outline targets shown beside the evaluated controls.
    # screen: Displayed route deciding whether the contour is running.
    # Raises tk.TclError if a readout can no longer be updated.
    def update_contour_readouts(self, targets, screen):
        for name, readout in self.contour_readouts.items():
            readout.configure(
                text=f"{self.contour_state[name]:.2f} > {targets[name]:.2f}"
            )
        status = "PAUSED"
        if self.contour_playing.get() and screen == Screen.RIG:
            status = "RUNNING"
        phase = self.contour_state["phase"]
        blend_seconds = self.renderer.rig["contour"]["transition_seconds"]
        self.contour_info.configure(
            text=f"{status} / phase {phase:.2f} rad\nBlend: {blend_seconds:.2f} s"
        )

    # Show coordinates from the frame on screen, not a newly evaluated pose.
    # Raises ValueError for unsupported rotation, or tk.TclError on a lost inspector.
    def update_point_inspector(self):
        if self.selected is None or self.scene is None:
            self.info.configure(
                text="Click a point, its name or use Debug.\n"
                     "Mint: contour / amber: original\n"
                     "Names are desktop-only annotations.\n"
                     "Coordinates follow the drawn scene."
            )
            return

        local_x, local_y = self.scene["local"][self.selected]
        world_x, world_y = self.scene["world"][self.selected]
        panel_x, panel_y = physical_point(world_x, world_y, self.display_rotation)
        self.info.configure(
            text=f"{self.selected}\n"
                 f"local  {local_x:+.4f}, {local_y:+.4f}\n"
                 f"canvas {world_x:.1f}, {world_y:.1f}\n"
                 f"panel  {panel_x:.1f}, {panel_y:.1f}"
        )

    # self: Preview controller advancing sources, model, geometry and presentation once.
    # Raises ValueError or tk.TclError; tick exposes the original error and stops.
    def render_tick(self):
        started = time.perf_counter()
        interval_seconds = started - self.last_tick
        self.last_tick = started
        screen = self.screen.get()
        rotation = self.rotation.get()
        show_names = self.names.get() and screen == Screen.RIG
        layout = PreviewLayout.fit(*self.pending_canvas_size, show_names)
        active_ms = interval_seconds * 1000 if screen == Screen.RIG else 0
        if self.playing.get():
            self.elapsed_ms += active_ms

        values, targets = self.advance_animation(screen, active_ms)
        transform = [value.get() for value in self.transform]
        inputs = self.observe_stimuli(int(started * 1_000_000), rotation)
        self.response_frame = None
        if screen == Screen.RIG:
            if self.display_screen != Screen.RIG:
                self.response_state = rebase_response_motion(self.response_state)
            self.response_config["motion"]["sensitivity"] = self.motion_sensitivity.get()
            validate_response_config(self.response_config)
            self.response_state = advance_response(self.response_config, self.response_state, inputs,
                                                   active_ms, int(started * 1_000_000), self.playing.get())
            self.response_frame = compose_response(self.renderer.rig, self.response_state, values, targets,
                                                   transform, rotation, self.playing.get())
            values = self.response_frame.face
            targets = self.response_frame.contour
            transform = self.response_frame.transform
            if self.contour_playing.get():
                self.contour_state = advance_contour(self.renderer.rig, self.contour_state, targets, active_ms)
        image, self.scene = self.renderer.render(
            screen,
            values,
            transform,
            self.debug.get(),
            self.selected,
            self.guide.get(),
            rotation,
            contour=self.contour_state,
            outline=self.outline.get(),
            rest_outline=self.rest_outline.get(),
            response_frame=self.response_frame if self.motion_overlay.get() else None,
        )
        self.present_frame(image, layout, screen, rotation)
        self.update_contour_readouts(targets, screen)
        self.update_stimulus_readouts(inputs, screen)
        self.update_point_inspector()

        duration_ms = (time.perf_counter() - started) * 1000
        self.timing.configure(
            text=f"SYNTHETIC / host frame {duration_ms:.1f} ms / "
                 f"interval {interval_seconds * 1000:.1f} ms / "
                 "target 60 Hz / ESP32: UNMEASURED"
        )
        delay_ms = max(1, math.ceil(1000 / 60 - duration_ms))
        self.after_id = self.root.after(delay_ms, self.tick)


# argv is the explicit Cody argument list from the common Windows launcher.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skin", default="assets/cody/cody.yaml")
    parser.add_argument('--screen',choices=SCREEN_NAMES,default='rig')
    parser.add_argument('--rotation',type=int,choices=(0,90,180,270),default=0)
    parser.add_argument('--capture',type=Path,
                        help = 'Output PNG; relative names use artifacts/simulator, explicit paths must stay under artifacts')
    parser.add_argument('--safe-guide',action='store_true')
    parser.add_argument('--at-ms',type=int,default=1000)
    args = parser.parse_args(argv)
    if args.at_ms<0:
        parser.error('--at-ms must be non-negative')
    if args.capture is not None:
        try:
            args.capture = resolve_runtime_output(args.capture)
        except (OSError, RuntimeError, ValueError) as error:
            parser.error(str(error))
    renderer = CodyRenderer(skin_path=args.skin)
    if args.capture:
        destination = args.capture
        values = animate(renderer.rig['boot_channels']['ready'],args.at_ms)
        contour = advance_contour(renderer.rig,make_contour_state(renderer.rig),contour_defaults(renderer.rig),args.at_ms)
        image,_ = renderer.render(args.screen,values,tuple(renderer.rig['default_transform'][key] for key in ('x','y','scale')),
                                guide = args.safe_guide,rotation=args.rotation,contour=contour)
        destination.parent.mkdir(parents=True,exist_ok=True)
        renderer.capture_sheet(image,f"{args.screen} / {args.rotation} deg / HOST PREVIEW").save(destination)
        print(destination)
        return
    root = tk.Tk()
    CodyWindow(root,renderer,args)
    root.mainloop()


if __name__=='__main__':
    main()
