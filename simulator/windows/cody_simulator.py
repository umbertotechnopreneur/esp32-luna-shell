"""Cody Studio rig preview, with an explicit SIMULATED CCP1 loopback endpoint via --ccp."""

from __future__ import annotations

import argparse
from copy import deepcopy
from dataclasses import replace
from enum import Enum
from secrets import randbits, randbelow
import math
import sys
import time
import webbrowser
import tkinter as tk
from pathlib import Path
from tkinter import font as tkfont, ttk

from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageTk

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_rig_contract import (  # noqa: E402
    advance_contour, advance_face, animate, contour_defaults, evaluate, load_contract, make_contour_state,
)
from scripts.common.cody_rig_types import (  # noqa: E402
    CurvePoint, FaceChannel, FillAntialiasing, LabelScope, PointRole, Screen,
)
from scripts.common.cody_response import (  # noqa: E402
    SampleHealth, advance_response, compose_response, effective_stamp, make_response_state, rebase_response_motion,
    EyeId, IdleSceneId, hit_test_eye, hit_test_body,
)
from scripts.common.cody_response_contract import Axis, validate_response_config  # noqa: E402
from scripts.common.cody_navigation import load_navigation, control_contains, NavigationVisibility  # noqa: E402
from scripts.common.cody_runtime_contract import load_settings, demo_targets  # noqa: E402
from scripts.common.cody_personality_contract import load_personality_rules  # noqa: E402
from cody_native import NativeRig, NativePersonalityPainter  # noqa: E402
from cody_native_behavior import NativeBehavior  # noqa: E402
from cody_stimuli import SyntheticStimulusSource  # noqa: E402
from cody_coverage import FULL_COVERAGE, coverage_mask  # noqa: E402
from cody_cloud_material import paint_cloud_material  # noqa: E402
from cody_png_mesh import PngMeshStudy  # noqa: E402
from cody_jelly_scanline import JellyScanlineStudy  # noqa: E402
from cody_personality import (  # noqa: E402
    PersonalityCue, PersonalityPainter, PersonalityPresentation, PersonalityPresentationStatus, select_personality_target,
)
from cody_petting import PettingRecognizer, PettingZone, load_petting_config  # noqa: E402
from cody_happiness import initial_state, advance_happiness, apply_event, PersonalityEvent, sadness  # noqa: E402
from cody_scene import ScenePainter  # noqa: E402
from cody_ccp_panel import CcpPanel  # noqa: E402
from cody_fake_audio import FakeAudioDriver  # noqa: E402
from cody_preview_layout import (  # noqa: E402
    SIZE, PreviewLayout, logical_touch, physical_point,
)
from runtime_paths import resolve_runtime_output  # noqa: E402
from cody_ui_theme import FONTS, install_theme, scale_pixels  # noqa: E402
from cody_ui_dpi import prepare_display, configure_tk_scaling, window_work_area  # noqa: E402
from cody_ui_widgets import InspectorTab  # noqa: E402
from vibeware_boot import VibeWareBoot  # noqa: E402

SCREEN_NAMES = tuple(screen.value for screen in Screen)
ANIMATED_SCREENS = (Screen.RIG, Screen.GREETING)
TIMED_SCREENS = ANIMATED_SCREENS + (Screen.BOOT_VIBEWARE,)
BOOT_NAMES = {
    "boot-sleep": "cody-sleep-pearl.png",
    "boot-waking": "cody-waking-pearl.png",
    "boot-ready": "cody-ready-pearl.png",
}
LABEL_SCOPES = tuple(scope.value for scope in LabelScope)


class StudioLink(str, Enum):
    PROJECT = "https://github.com/umbertotechnopreneur/esp32-luna-shell"
    WEBSITE = "https://umbertogiacobbi.biz"
    MANIFESTO = "https://umbertogiacobbi.biz/vibeware/manifesto"


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
    # jelly_study: Select the isolated pearlescent scanline renderer for this preview.
    # offline_study: Explicit opt-in material/mesh study; ordinary simulation always executes C++.
    # self: Renderer owning immutable authoring tables and optional cached artwork.
    # Raises OSError or ValueError for unreadable artwork or invalid authoring data.
    def __init__(self, root=ROOT, skin_path="assets/cody/cody.yaml", *, jelly_study=False, offline_study=False):
        self.navigation = load_navigation(root)
        self.navigation_opacity = 255
        self.navigation_paused = False
        self.debug_controls_enabled = True  # This desktop preview represents a Debug build.
        self.rig, self.palette = load_contract(root, skin_path)
        self.native_palette = [int(entry["rgb565"], 16) for entry in self.palette["entries"]]
        self.rgb_palette = [rgb565(value) for value in self.native_palette]
        self.flat_palette = [channel for color in self.rgb_palette for channel in color]
        self.boot = {}
        self.branding = None
        for screen, name in BOOT_NAMES.items():
            with Image.open(root / "assets/cody/boot" / name) as source:
                # Exact source artwork is previewed; firmware's indexed RLE conversion is separate.
                self.boot[screen] = source.convert("RGB").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        self.font = ImageFont.load_default()
        self.root = root
        self.png_mesh = None
        self.jelly_study_enabled = jelly_study
        self.offline_study = offline_study or jelly_study
        self.jelly_sprite = None
        self.face_follows_body = True
        self.personality_painter = None
        self.native = None
        self.scene_painter = ScenePainter(root, self.rig)

    # self: Studio renderer owning its lazy C++ transport.
    # Raises OSError or ValueError for a missing, stale or incompatible native library.
    def require_native(self):
        if self.native is None:
            self.native = NativeRig(self.root, self.rig)
            self.scene_painter.native = self.native
        return self.native

    # self: Renderer selecting the shared C++ rig for ordinary simulation.
    # values: Current independent channel controls.
    # transform: Logical x/y/scale.
    # contour: Optional named shape controls and phase.
    # study: Explicit offline material-study selection, retaining its authoring evaluator.
    # Raises RuntimeError for real native geometry errors; no implicit fallback exists.
    def evaluate_scene(self, values, transform, contour=None, *, study=False):
        if study or self.offline_study:
            return evaluate(self.rig, values, transform, contour=contour)
        return self.require_native().evaluate(values, transform, contour, self.face_follows_body)

    # self: Studio renderer selecting native facial smoothing in ordinary simulation.
    # state: Current channels.
    # target: Requested channels.
    # delta_ms: Nonnegative active elapsed time.
    # Raises RuntimeError for the original native transition error.
    def advance_face(self, state, target, delta_ms):
        if self.offline_study:
            return advance_face(self.rig, state, target, delta_ms)
        return self.require_native().advance_face(state, target, delta_ms)

    # self: Renderer selecting firmware blink/breathing in ordinary simulation.
    # values: Current independent controls.
    # elapsed_ms: Active animation clock.
    # Raises RuntimeError or ValueError for rejected native animation input.
    def animate_face(self, values, elapsed_ms):
        if self.offline_study:
            return animate(values, elapsed_ms)
        return self.require_native().animate(values, elapsed_ms)

    # self: Renderer selecting firmware contour integration in ordinary simulation.
    # state: Current named controls and phase.
    # target: Requested named controls.
    # delta_ms: Nonnegative active elapsed time.
    # Raises RuntimeError for rejected native contour transitions.
    def advance_shape(self, state, target, delta_ms):
        if self.offline_study:
            return advance_contour(self.rig, state, target, delta_ms)
        return self.require_native().advance_contour(state, target, delta_ms)

    # self: Renderer selecting a host-owned native response state.
    # config: Current immutable authored settings; ordinary native compilation owns their gains.
    # seed: Explicit nonzero artistic idle seed supplied by Studio initialization/reset.
    # Raises RuntimeError for native model errors.
    def reset_model(self, config, seed):
        if self.offline_study:
            state = make_response_state(config)
            state.idle_random_state = seed
            return state
        return self.require_native().reset_response(seed)

    # self: Response owner crossing an active route boundary.
    # state: Copied inspector state; actual ordinary dynamics remain owned by C++.
    # Raises RuntimeError for unavailable native state.
    def rebase_model(self, state):
        if self.offline_study:
            return rebase_response_motion(state)
        return self.require_native().rebase_response()

    # self: Renderer selecting actual firmware response math for ordinary simulation.
    # config: Authored settings with explicit Studio motion sensitivity override.
    # state: Copied inspector state; offline studies own their independent Python state.
    # inputs: One explicitly synthetic driver bundle with original timestamps and errors.
    # delta_ms: Nonnegative active elapsed time.
    # now_us: Original host observation clock.
    # animate_rhythm: Whether native breath/blink timing advances.
    # Raises RuntimeError for actual native input/configuration/model errors.
    def advance_model(self, config, state, inputs, delta_ms, now_us, animate_rhythm):
        if self.offline_study:
            return advance_response(config, state, inputs, delta_ms, now_us, animate_rhythm)
        return self.require_native().advance_response(inputs, config['motion']['sensitivity'], delta_ms, now_us, animate_rhythm)

    # self: Renderer selecting actual firmware response composition.
    # state: Copied inspector state; ordinary dynamics are retained inside C++.
    # values: Independent authored face channels.
    # targets: Independent authored contour controls.
    # transform: Resting logical root and scale.
    # rotation: Shared cardinal display and touch rotation.
    # animate_rhythm: Whether the composition includes breathing/blink.
    # Raises RuntimeError for native composition errors.
    def compose_model(self, state, values, targets, transform, rotation, animate_rhythm):
        if self.offline_study:
            return compose_response(self.rig, state, values, targets, transform, rotation, animate_rhythm)
        return self.require_native().compose_response(values, targets, transform, rotation, animate_rhythm)

    # self: Renderer selecting actual firmware manga arbitration for ordinary simulation.
    # requested: Typed synthetic baseline cue.
    # sleeping: Explicit semantic Sleep presentation permission.
    # affection: Whether an accepted host caress still has active lifetime.
    # eye_reaction: Current typed pointer-derived eye reaction.
    # Raises RuntimeError for native cue/selection errors.
    def select_personality(self, requested, sleeping, affection, eye_reaction=PersonalityCue.NONE):
        if self.offline_study:
            return select_personality_target(requested, sleeping, affection, eye_reaction)
        return self.require_native().select_target(requested, sleeping, affection, eye_reaction)

    # self: Native displayed-geometry owner.
    # scene: Exactly the currently displayed evaluated scene.
    # point: Logical pointer coordinate after cardinal rotation mapping.
    # radius: Authored normalized eye hit radius.
    # Raises RuntimeError for native geometry/hit failures.
    def hit_eye(self, scene, point, radius):
        if self.offline_study:
            return hit_test_eye(scene, point, radius)
        return self.require_native().hit(scene, point, radius)[0]

    # self: Native displayed-geometry owner.
    # scene: Exactly the currently displayed evaluated scene.
    # point: Logical pointer coordinate after cardinal rotation mapping.
    # Raises RuntimeError for native body-geometry/hit failures.
    def hit_body(self, scene, point):
        if self.offline_study:
            return hit_test_body(scene, point)
        return self.require_native().hit(scene, point, self.rig['response']['touch']['eye_hit_radius'])[1]

    # color is the initial palette slot of a fresh full logical canvas.
    def indexed(self, color=None):
        background = self.rig["style"]["background"] if color is None else color
        image = Image.new("P", (SIZE, SIZE), background)
        image.putpalette(self.flat_palette)
        return image

    # self: Renderer owning the immutable native palette.
    # image: Polygon destination in indexed mode or RGB565-quantized RGB mode.
    # vertices: Already evaluated curve samples in logical coordinates.
    # slots: Ordered inclusive palette ramp.
    # opacity: Quantized source-over opacity, from zero through 255.
    # antialiasing: Explicit coverage policy; disabled keeps the original Pillow fill.
    # clip_vertices: Optional evaluated containing polygon, in the same warped coordinates.
    # Raises ValueError for an invalid opacity, coverage policy or polygon.
    def fill(self, image, vertices, slots, opacity=255, antialiasing=FillAntialiasing.DISABLED,
             clip_vertices=None):
        if type(opacity) is not int or not 0 <= opacity <= 255:
            raise ValueError(f"Invalid polygon opacity: {opacity}")
        antialiasing = FillAntialiasing(antialiasing)
        if opacity == 0:
            return
        uses_coverage = antialiasing == FillAntialiasing.COVERAGE
        first, last = slots
        if first == last and opacity == 255 and not uses_coverage and clip_vertices is None:
            ImageDraw.Draw(image).polygon(vertices, fill=self.paint(image, first))
            return
        coverage_runs = [] if uses_coverage else None
        if uses_coverage:
            mask = coverage_mask(vertices, (SIZE, SIZE), clip_vertices, coverage_runs)
        else:
            mask = Image.new("L", (SIZE, SIZE))
            ImageDraw.Draw(mask).polygon(vertices, fill=255)
            if clip_vertices is not None:
                containing_mask = Image.new("L", (SIZE, SIZE))
                ImageDraw.Draw(containing_mask).polygon(clip_vertices, fill=255)
                mask = ImageChops.multiply(mask, containing_mask)
        top = min(y for _, y in vertices)
        bottom = max(y for _, y in vertices)
        ramp = self.indexed(first)
        draw = ImageDraw.Draw(ramp)
        for row in range(max(0, math.floor(top)), min(SIZE, math.ceil(bottom))):
            # This is edge coverage clipping; channel inputs themselves are never clamped.
            ratio = max(0, min(1, (row + .5 - top) / (bottom - top))) if bottom > top else 0
            slot = first + math.floor(ratio * (last - first) + .5)
            draw.line((0, row, SIZE - 1, row), fill=slot)
        if opacity == 255 and not uses_coverage:
            image.paste(ramp if image.mode == "P" else ramp.convert("RGB"), (0, 0), mask)
            return
        self.composite_fill(image, ramp, mask, opacity, coverage_runs)

    # image determines whether the drawing backend expects a palette index or RGB tuple.
    # slot is an authored source color from the shared 256-entry palette.
    def paint(self, image, slot):
        return slot if image.mode == "P" else self.rgb_palette[slot]

    # self: Renderer owning immutable RGB565 source colors.
    # image: RGB destination whose pixels are already quantized to RGB565.
    # source: Indexed source colors for the current polygon.
    # mask: Binary L mask or integer I geometric coverage in the range 0..1024.
    # opacity: Previously validated source opacity, from one through 255.
    # coverage_runs: Optional exact nonzero runs collected during geometric coverage.
    # Raises ValueError for an unsupported mask or a destination not expanded to RGB.
    def composite_fill(self, image, source, mask, opacity, coverage_runs=None):
        if image.mode != "RGB":
            raise ValueError("Coverage and partial opacity require an RGB565-quantized RGB destination")
        if mask.mode not in ("L", "I"):
            raise ValueError(f"Unsupported polygon coverage mask: {mask.mode}")
        if coverage_runs is not None:
            if mask.mode != "I":
                raise ValueError("Exact coverage runs require an integer coverage mask")
            self.composite_coverage_runs(image, source, coverage_runs, opacity)
            return
        bounds = mask.getbbox()
        if bounds is None:
            return
        coverage_limit = FULL_COVERAGE if mask.mode == "I" else 255
        pixels = image.load()
        sources = source.load()
        coverage = mask.load()
        mixed_colors = {}
        for y in range(bounds[1], bounds[3]):
            for x in range(bounds[0], bounds[2]):
                # Combine opacity with the full-precision geometric mask once.
                # Reducing the mask to a byte first would double-round edge pixels.
                effective_opacity = (opacity * coverage[x, y] + coverage_limit // 2) // coverage_limit
                if effective_opacity == 0:
                    continue
                slot = sources[x, y]
                if effective_opacity == 255:
                    pixels[x, y] = self.rgb_palette[slot]
                    continue
                background = pixels[x, y]
                key = (slot, background, effective_opacity)
                if key not in mixed_colors:
                    red, green, blue = background
                    destination = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
                    source_word = self.native_palette[slot]
                    # Match the firmware's rounded native 5/6/5 channel blend exactly.
                    word = 0
                    for shift, mask_bits in ((11, 31), (5, 63), (0, 31)):
                        foreground = (source_word >> shift) & mask_bits
                        beneath = (destination >> shift) & mask_bits
                        mixed = (foreground * effective_opacity + beneath * (255 - effective_opacity) + 127) // 255
                        word |= mixed << shift
                    mixed_colors[key] = rgb565(word)
                pixels[x, y] = mixed_colors[key]

    # self: Renderer owning the canonical RGB565 palette.
    # image: RGB destination expanded from native RGB565 words.
    # source: Indexed ramp carrying the polygon's authored colors.
    # coverage_runs: Disjoint nonzero scanline runs from the exact coverage accumulator.
    # opacity: Validated polygon opacity in the range one through 255.
    # Raises ValueError if an internal run contains invalid coverage.
    def composite_coverage_runs(self, image, source, coverage_runs, opacity):
        opaque_mask = Image.new("L", image.size, 0)
        opaque_draw = ImageDraw.Draw(opaque_mask)
        has_opaque = False
        partial_runs = []
        for row, left, right, coverage in coverage_runs:
            if not 0 < coverage <= FULL_COVERAGE:
                raise ValueError(f"Invalid scanline coverage: {coverage}")
            alpha = (opacity * coverage + FULL_COVERAGE // 2) // FULL_COVERAGE
            if alpha == 255:
                opaque_draw.line((left, row, right - 1, row), fill=255)
                has_opaque = True
            elif alpha:
                partial_runs.append((row, left, right, alpha))
        # Runs are disjoint: copying opaque interiors cannot change any boundary
        # background. Pillow copies their exact palette RGB in C, without blending.
        if has_opaque:
            image.paste(source.convert("RGB"), (0, 0), opaque_mask)
        pixels = image.load()
        sources = source.load()
        mixed_colors = {}
        for row, left, right, alpha in partial_runs:
            for column in range(left, right):
                slot = sources[column, row]
                background = pixels[column, row]
                key = (slot, background, alpha)
                if key not in mixed_colors:
                    red, green, blue = background
                    destination = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
                    source_word = self.native_palette[slot]
                    word = 0
                    for shift, mask_bits in ((11, 31), (5, 63), (0, 31)):
                        foreground = (source_word >> shift) & mask_bits
                        beneath = (destination >> shift) & mask_bits
                        mixed = (foreground * alpha + beneath * (255 - alpha) + 127) // 255
                        word |= mixed << shift
                    mixed_colors[key] = rgb565(word)
                pixels[column, row] = mixed_colors[key]

    # self: Renderer owning the immutable skin and palette for this scene.
    # scene is evaluated once and shared by artwork, outlines and point overlays.
    # debug controls point, link and cubic handle visibility.
    # selected is the stable name of the highlighted point, or None.
    # guide shows the canonical safe square without changing geometry or hit testing.
    # outline shows the evaluated body contour without recomputing its curves.
    # rest_outline shows the undeformed contour with the same placement and breathing scale.
    # response_frame optionally shows the actual resting/current root and inertia vector.
    # material_study: Opt-in host-only continuous cloud shading, not firmware parity.
    # png_mesh_study: Opt-in painted texture driven by the same semantic lobe pose.
    # mesh_overlay: Show the actual triangle edges when the PNG study is selected.
    # navigation_visible: Scene policy; hidden controls have no raster footprint.
    # background_rgb565: Explicit scene background, or the authored plain-rig palette color.
    # Raises ValueError for an invalid opacity, coverage policy or unsupported blend target.
    # Raises OSError for unreadable alternate PNG artwork.
    # Raises RuntimeError for real shared boot-branding preparation or rendering failures.
    def draw_scene(self, scene, debug=True, selected=None, guide=False, *, outline=False, rest_outline=False, response_frame=None, material_study=False, png_mesh_study=False, mesh_overlay=False, navigation_visible=True, background_rgb565=None):
        if sum((material_study, png_mesh_study, self.jelly_study_enabled)) > 1:
            raise ValueError('Choose only one cloud rendering study')
        if mesh_overlay and not png_mesh_study:
            raise ValueError('Mesh overlay requires the PNG mesh study')
        if not (material_study or png_mesh_study or self.jelly_study_enabled):
            image = self.require_native().draw(scene, debug, selected, self.navigation_paused,
                navigation_visible and self.debug_controls_enabled, self.navigation_opacity,
                self.native_palette[self.rig['style']['background']] if background_rgb565 is None else background_rgb565)
            # Desktop inspection annotations surround actual C++ geometry; device artwork
            # and its skeleton are already rasterized by draw_rig_rows.
            colors = {name: rgb565(self.native_palette[slot]) for name, slot in self.rig['style']['debug'].items()}
            draw = ImageDraw.Draw(image)
            if rest_outline:
                vertices = scene['rest_outline']
                for index in range(0, len(vertices) - 1, 2):
                    draw.line(vertices[index:index + 2], fill=colors['handle'], width=2)
            if outline:
                body = next(path for path in scene['paths'] if path['definition']['name'] == self.rig['contour']['path'])
                draw.line(body['vertices'], fill=colors['selected'], width=2)
            if response_frame is not None:
                self.draw_response_roots(image, response_frame, colors)
            if guide:
                draw.rectangle((73, 73, 392, 392), outline=colors['selected'])
            return image
        image = self.indexed()
        if background_rgb565 is not None:
            if type(background_rgb565) is not int or not 0 <= background_rgb565 <= 65535:
                raise ValueError("Invalid scene background RGB565")
            image = Image.new("RGB", (SIZE, SIZE), rgb565(background_rgb565))
        surface_paths = set(self.rig['contour'].get('surface_paths', []))
        paths_by_name = {path["definition"]["name"]: path for path in scene["paths"]}
        for path in scene["paths"]:
            if not path["visible"]:
                continue
            definition = path["definition"]
            if (material_study or png_mesh_study or self.jelly_study_enabled) and definition['name'] in surface_paths:
                continue
            if self.jelly_study_enabled and definition['name'] == self.rig['contour']['path']:
                if self.jelly_sprite is None:
                    self.jelly_sprite = JellyScanlineStudy(self.root, self.rig)
                image = image.convert('RGB')
                self.jelly_sprite.paint(image, scene)
                continue
            if png_mesh_study and definition['name'] == self.rig['contour']['path']:
                if self.png_mesh is None:
                    self.png_mesh = PngMeshStudy(self.root, self.rig)
                image = image.convert('RGB')
                self.png_mesh.paint(image, scene, overlay=mesh_overlay)
                continue
            if material_study and definition['name'] == self.rig['contour']['path']:
                image = image.convert('RGB')
                paint_cloud_material(image, path['vertices'], scene)
                continue
            antialiasing = definition["antialiasing"]
            opacity = math.floor(path["opacity"] * 255 + .5)
            if opacity == 0:
                continue
            if image.mode == "P" and (opacity < 255 or antialiasing == FillAntialiasing.COVERAGE):
                # Boundary coverage creates derived RGB565 colors even for opaque artwork.
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
                containing = paths_by_name.get(definition["clip"])
                clip_vertices = containing["vertices"] if containing is not None else None
                self.fill(image, path["vertices"], definition["paint"], opacity, antialiasing, clip_vertices)
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
        if navigation_visible:
            image = image.convert("RGB")
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
        if not self.debug_controls_enabled or self.navigation_opacity == 0:
            return
        layer = image.convert("RGB")
        draw = ImageDraw.Draw(layer)
        mask = Image.new("L", image.size, 0)
        coverage = mask.load()
        back = self.rig["style"]["back"]
        material = self.navigation["colors_rgb565"]
        back_ink = rgb565(material["ink"])
        for control in self.navigation["controls"]:
            if not control["visible_by_default"]:
                continue
            left, top, right, bottom = control["bounds"]
            for row in range(top, bottom):
                step = (row-top)*31//(bottom-top)
                shade = 0
                for shift, channel_mask in ((11, 31), (5, 63), (0, 31)):
                    start = (material["top"] >> shift) & channel_mask
                    end = (material["bottom"] >> shift) & channel_mask
                    shade |= ((start*(31-step) + end*step + 15)//31) << shift
                for column in range(left, right):
                    if control_contains(control, column+.5, row+.5):
                        draw.point((column, row), fill=rgb565(shade))
                        coverage[column, row] = self.navigation_opacity
            x, y = control["icon_center"]
            stroke = max(1, round(back["stroke"]))
            if control["id"] == "back":
                draw.line([(x+4,y-11),(x-7,y),(x+4,y+11)], fill=back_ink, width=stroke)
            elif control["id"] == "pause":
                if self.navigation_paused:
                    draw.line([(x-6,y-11),(x+10,y),(x-6,y+11),(x-6,y-11)], fill=back_ink, width=stroke)
                else:
                    for bar in (x-7, x+7):
                        draw.line([(bar,y-11),(bar,y+11)], fill=back_ink, width=stroke)
            elif control["id"] == "screenshot":
                draw.rectangle((x-11, y-7, x+11, y+8), outline=back_ink, width=stroke)
                draw.line([(x-5,y-7),(x-2,y-11),(x+4,y-11),(x+7,y-7)],
                    fill=back_ink, width=stroke)
                draw.ellipse((x-5,y-5,x+5,y+5), outline=back_ink, width=stroke)
            else:
                nodes = [(x-10,y+9),(x,y-11),(x+10,y+9)]
                draw.line(nodes + [nodes[0]], fill=back_ink,
                    width=max(1, round(self.rig["style"]["overlay_toggle"]["stroke"])))
                radius = (self.rig["style"]["overlay_toggle"]["marker_diameter"]-1)/2
                for node_x, node_y in nodes:
                    draw.ellipse((node_x-radius,node_y-radius,node_x+radius,node_y+radius),
                        fill=rgb565(material["selected"]) if debug else back_ink)
        image.paste(layer, (0, 0), mask)

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
    # material_study: Opt-in host-only material study; default rendering is unchanged.
    # png_mesh_study: Opt-in PNG/triangle body; the existing face remains independent.
    # mesh_overlay: Show triangle topology rather than just the painted result.
    # scene_elapsed_ms: Active intro/typewriter time, independent of the shared rig clock.
    # personality_cue: Single explicit host symbol preview; device cues are synthetic.
    # personality_elapsed_ms: Active cue clock, frozen while animation is paused.
    # personality_nominal_scale: Unanimated authored size; None uses the supplied transform.
    # personality_sleep: Semantic Sleep preset selection; never inferred from eyelid values.
    # personality_paused: Freeze the icon channel and its geometry during redraws.
    # Raises ValueError for an unknown screen or invalid rig values.
    # Raises OSError for unreadable alternate PNG artwork.
    def render(self, screen, values, transform, debug=True, selected=None, guide=False, rotation=0, *, contour=None, outline=False, rest_outline=False, response_frame=None, material_study=False, png_mesh_study=False, mesh_overlay=False, scene_elapsed_ms=0, personality_cue=PersonalityCue.NONE, personality_elapsed_ms=0, personality_nominal_scale=None, personality_sleep=False, personality_paused=False):
        if screen not in SCREEN_NAMES:
            raise ValueError(f"Unknown Cody screen: {screen}")
        study = material_study or png_mesh_study or self.jelly_study_enabled
        scene = (self.evaluate_scene(values, transform, contour, study=study)
                 if screen in ANIMATED_SCREENS else None)
        boot_visible, opacity, content_visible = (False, 255, True)
        if screen == Screen.GREETING:
            boot_visible, opacity, content_visible = self.scene_painter.presentation("greeting", scene_elapsed_ms)
        if screen in ANIMATED_SCREENS:
            if self.jelly_study_enabled and self.face_follows_body:
                if self.jelly_sprite is None:
                    self.jelly_sprite = JellyScanlineStudy(self.root, self.rig)
                self.jelly_sprite.deform_face(scene)
            cue_frame = None
            if self.personality_painter is not None or personality_cue != PersonalityCue.NONE or personality_sleep:
                if self.personality_painter is None:
                    self.personality_painter = (PersonalityPainter(self.root, self.rig) if study
                                                else NativePersonalityPainter(self.require_native()))
                # Reserve the native status layer even while its touch opacity is zero.
                exclusions = [(10, 203, 70, 263), (122, 32, 365, 64)]
                presentation = PersonalityPresentation.DISABLED
                if screen == Screen.RIG:
                    presentation = PersonalityPresentation.SLEEP if personality_sleep else PersonalityPresentation.MAIN
                body_top = None
                if self.jelly_study_enabled and self.jelly_sprite is not None:
                    body_top = max(0, math.floor(self.jelly_sprite.map_rows(scene)[0].destination_y))
                cue_frame = self.personality_painter.prepare(
                    scene, personality_cue,
                    transform[2] if personality_nominal_scale is None else personality_nominal_scale,
                    screen, rotation, personality_elapsed_ms, exclusions, body_top,
                    presentation=presentation, paused=personality_paused)
            image = self.draw_scene(scene, debug, selected, guide,
                                    outline=outline, rest_outline=rest_outline, response_frame=response_frame,
                                    material_study=material_study, png_mesh_study=png_mesh_study,
                                    mesh_overlay=mesh_overlay,
                                    navigation_visible=not boot_visible,
                                    background_rgb565=self.scene_painter.definition("greeting")["background_rgb565"]
                                    if screen == Screen.GREETING else None)
            if cue_frame is not None:
                self.personality_painter.paint(image, cue_frame)
            if screen == Screen.GREETING:
                image = self.boot[Screen.BOOT_READY].copy() if boot_visible else image
                image = image.convert("RGB")
                self.scene_painter.fade_to_black(image, opacity)
                if content_visible:
                    self.scene_painter.paint_title(image, "greeting", scene_elapsed_ms)
        else:
            if screen == Screen.BOOT_VIBEWARE:
                if self.branding is None:
                    self.branding = VibeWareBoot(self.root, cody_companion=True)
                image = self.branding.image(personality_elapsed_ms)
            else:
                image = self.boot[screen].copy()
            if self.personality_painter is not None:
                self.personality_painter.disable(personality_elapsed_ms)
        circular = Image.new("L", (SIZE, SIZE))
        ImageDraw.Draw(circular).ellipse((0,0,SIZE-1,SIZE-1), fill=255)
        image = image.convert("RGB")
        image.paste((0,0,0), (0,0,SIZE,SIZE), Image.eval(circular, lambda v:255-v))
        image = orient(image, rotation)
        if material_study and screen == Screen.RIG:
            image.info['cody_material_study'] = True
        if png_mesh_study and screen == Screen.RIG:
            image.info['cody_png_mesh_study'] = True
        if self.jelly_study_enabled and screen in ANIMATED_SCREENS:
            image.info['cody_jelly_study'] = True
        return image, scene

    # self: Renderer owning the presentation font.
    # image: Rendered logical view, optionally marked as a host-only cloud study.
    # caption: Context shown for the ordinary simulator capture.
    def capture_sheet(self, image, caption):
        sheet = Image.new("RGB", (SIZE, SIZE+42), "#101820")
        sheet.paste(image, (0,0))
        draw = ImageDraw.Draw(sheet)
        if image.info.get('cody_jelly_study', False):
            draw.text((12, SIZE + 7), 'PEARL JELLY / SCANLINE STUDY / HOST ONLY', fill='#75CBF3', font=self.font)
            draw.text((12, SIZE + 23), 'SYNTHETIC / ESP32 performance UNMEASURED', fill='#C3D0D5', font=self.font)
        elif image.info.get('cody_png_mesh_study', False):
            draw.text((12, SIZE + 7), 'PNG + TRIANGLE MESH / HOST ONLY', fill='#75CBF3', font=self.font)
            draw.text((12, SIZE + 23), 'SYNTHETIC / ESP32 performance UNMEASURED', fill='#C3D0D5', font=self.font)
        elif image.info.get('cody_material_study', False):
            draw.text((12, SIZE + 7), 'MATERIAL STUDY / HOST ONLY', fill='#75CBF3', font=self.font)
            draw.text((12, SIZE + 23), 'SYNTHETIC / Not the firmware renderer', fill='#C3D0D5', font=self.font)
        else:
            draw.text((12,SIZE+12), "SYNTHETIC / " + caption, fill="#75CBF3", font=self.font)
        return sheet

    # self: Renderer with a lazily decoded mesh or jelly PNG and shared semantic rig.
    # destination: GIF output restricted to artifacts; also owns a sibling pose-sheet PNG.
    # rotation: Cardinal orientation applied to artwork and controls together.
    # mesh_overlay: Include triangle edges in mesh mode; unsupported in scanline mode.
    # Raises OSError for a failed artwork read or capture write.
    # Raises ValueError for an invalid pose, inverted mapping or unsupported overlay.
    def capture_sprite_motion(self, destination, rotation, mesh_overlay):
        frames = []
        transform = tuple(self.rig['default_transform'][key] for key in ('x', 'y', 'scale'))
        frame_count = 80 if self.jelly_study_enabled else 32
        frame_duration_ms = 40 if self.jelly_study_enabled else 100
        for frame_number in range(frame_count):
            phase = math.tau * frame_number / frame_count
            values = list(self.rig['boot_channels']['ready'])
            values[FaceChannel.BREATH] = .5 * math.sin(phase)
            contour = make_contour_state(self.rig)
            # Explicit synthetic artistic sweep, not fabricated IMU telemetry.
            # All authored lobe excursion limits and the stationary root remain intact.
            contour.update(fullness=.8 * math.sin(phase), amplitude=1.0,
                           speed=.3125, phase_spread=1.0, phase=phase)
            image, _ = self.render(Screen.RIG, values, transform, debug=False, rotation=rotation,
                                   contour=contour, png_mesh_study=not self.jelly_study_enabled,
                                   mesh_overlay=mesh_overlay)
            frames.append(self.capture_sheet(image, 'Sprite / synthetic lobe sweep'))
        # A shared GIF palette avoids frame-to-frame palette changes. Still PNGs
        # retain RGB565-expanded RGB and are the reference for color inspection.
        palette = frames[0].quantize(colors=256)
        indexed = [frame.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG) for frame in frames]
        destination.parent.mkdir(parents=True, exist_ok=True)
        indexed[0].save(destination, save_all=True, append_images=indexed[1:],
                        duration=frame_duration_ms, loop=0, disposal=2, optimize=False)
        # Retain full-color phase samples alongside the compressed motion loop.
        # These make texture/edge attachment inspectable without a GIF player.
        pose_sheet = Image.new('RGB', (SIZE * 2, (SIZE + 42) * 2), '#101820')
        phases = {'phase-0': (0.0, 0, 0), 'phase-90': (.25, 1, 0),
                  'phase-180': (.50, 0, 1), 'phase-270': (.75, 1, 1)}
        for name, (fraction, column, row) in phases.items():
            frame = frames[round(fraction * frame_count)].copy()
            ImageDraw.Draw(frame).text((12, 12), name + ' / SYNTHETIC', fill='#75CBF3', font=self.font)
            pose_sheet.paste(frame, (column * SIZE, row * (SIZE + 42)))
        pose_path = destination.with_name(destination.stem + '-poses.png')
        pose_sheet.save(pose_path)
        print(pose_path)


class CodyWindow:
    # self: New preview controller owning transient animation and synthetic model state.
    # root owns the Tk preview window and its scheduled frame callback.
    # renderer owns immutable authored tables and the shared scene drawing code.
    # args selects the initial screen, rotation, study and optional SIMULATED CCP endpoint.
    # Raises tk.TclError for failed widget construction or lost Tk ownership.
    # Raises OSError or AttributeError if required Windows monitor APIs are unavailable.
    # Raises RuntimeError if the window DPI context or work-area bounds are invalid.
    # Raises OSError or RuntimeError if the fake audio DLL cannot be loaded/prepared.
    # Raises ValueError or RuntimeError when the required native graphics/behavior owner cannot initialize.
    def __init__(self, root, renderer, args):
        self.root, self.renderer = root, renderer
        self.audio = FakeAudioDriver(ROOT)
        self.audio_enabled = tk.BooleanVar(value=True)
        self.last_audio_cue = PersonalityCue.NONE
        self.last_whistle_generation = None
        self.last_audio_screen = None
        self.last_audio_glyphs = 0
        self.last_link_error = None
        self.ccp_panel = None
        self.jelly_study = renderer.jelly_study_enabled
        preview_name = 'PEARL JELLY / HOST ONLY' if self.jelly_study else 'Cody Alpha'
        self.root.title(f"{renderer.rig['character']['name']} / VibeWare {preview_name} / SYNTHETIC")
        self.ui_colors = install_theme(root)
        self.screen = tk.StringVar(value=args.screen)
        self.rotation = tk.IntVar(value=args.rotation)
        # The first view is the character; inspection overlays are explicit desktop choices.
        self.debug = tk.BooleanVar(value=False)
        self.names = tk.BooleanVar(value=False)
        self.outline = tk.BooleanVar(value=False)
        self.rest_outline = tk.BooleanVar(value=False)
        self.label_scope = tk.StringVar(value=LabelScope.CONTOUR.value)
        self.label_page = tk.IntVar(value=0)
        self.guide = tk.BooleanVar(value=args.safe_guide)
        self.playing = tk.BooleanVar(value=True)
        self.preview_hz = tk.IntVar(value=25 if self.jelly_study else 30)
        self.face_follows_body = tk.BooleanVar(value=renderer.face_follows_body)
        self.personality_cue = tk.StringVar(value=PersonalityCue.NONE.name)
        self.native_behavior = None if renderer.offline_study else NativeBehavior(renderer.require_native())
        self.petting = self.native_behavior if self.native_behavior is not None else PettingRecognizer(load_petting_config(ROOT))
        self.affection_remaining_us = 0
        self.affection_owns_channel = False
        self.eye_reaction = PersonalityCue.NONE
        self.last_eye_reaction = PersonalityCue.NONE
        self.eye_reaction_remaining_us = 0
        self.petting_context = None
        self.petting_selected_cue = PersonalityCue.NONE
        self.personality_rules = load_personality_rules(ROOT / "assets/cody/personality.yaml")
        self.happiness_state = (self.native_behavior.snapshot() if self.native_behavior is not None
                                else initial_state(self.personality_rules))
        self.happiness_sampled_us = int(time.perf_counter() * 1_000_000)
        self.contour_playing = tk.BooleanVar(value=True)
        self.contour_state = make_contour_state(renderer.rig)
        self.entity_name = next(iter(renderer.rig["entities"]))
        self.face_state = [channel["default"] for channel in renderer.rig["channels"]]
        self.face_presets = renderer.rig["boot_channels"] | renderer.rig["expression_presets"]
        self.face_preset_name = None
        self.personality_sleep = False
        self.response_config = deepcopy(renderer.rig["response"])
        self.response_state = renderer.reset_model(self.response_config, randbits(32) | 1)
        self.stimulus_source = SyntheticStimulusSource()
        self.runtime_settings = load_settings(ROOT / "assets/cody/runtime.yaml")
        self.demo_enabled = tk.BooleanVar(value=False)
        self.response_frame = None
        self.motion_overlay = tk.BooleanVar(value=False)
        self.elapsed_ms = 0.0
        self.scene_elapsed_ms = 0.0
        self.previous_animation_screen = None
        self.last_tick = time.perf_counter()
        self.readout_tab = None
        self.next_readout_at = 0.0
        self.next_status_at = 0.0
        self.status_sampled_at = self.last_tick
        self.completed_preview_frames = 0
        self.navigation_visibility = NavigationVisibility(renderer.navigation)
        self.renderer.navigation_opacity = 0  # Touch, not startup, reveals Debug tools.
        self.selected = None
        self.scene = None
        self.after_id = None
        self.layout_mode = None
        self.pending_canvas_size = (1,1)
        self.layout = PreviewLayout.fit(1,1,False)
        self.display_rotation = args.rotation
        self.display_screen = args.screen
        self.body_drag_origin = None
        self.body_drag_point = None
        self.body_drag_rotation = args.rotation
        self.body_drag_started_us = 0
        self.label_text_cache = {}
        self.label_items = {}
        self.last_presentation_key = None
        self.photo = None
        self.workspace = ttk.Frame(root, style="Studio.TFrame",
                                    padding=self.ui_pixels((20, 0, 20, 16)))
        self.workspace.grid(row=1, column=0, sticky="nsew")
        self.build_studio_header(preview_name)
        self.preview_host = ttk.Frame(self.workspace, style="Studio.TFrame")
        self.preview_host.grid(row=0, column=0, sticky="nsew")
        self.preview_host.columnconfigure(0, weight=1)
        self.preview_host.rowconfigure(1, weight=1)
        preview_header = ttk.Frame(self.preview_host, style="Studio.TFrame", padding=self.ui_pixels((0, 0, 0, 12)))
        preview_header.grid(row=0, column=0, sticky="ew")
        ttk.Label(preview_header, text="LIVE PREVIEW", style="Section.TLabel",
                  background=self.ui_colors["background"]).pack(side="left")
        ttk.Label(preview_header, text="466 x 466 / local touch", style="Subtitle.TLabel",
                  background=self.ui_colors["background"]).pack(side="right")
        self.canvas = tk.Canvas(self.preview_host, width=self.ui_pixels(600), height=self.ui_pixels(420),
                                highlightthickness=0, borderwidth=0, bg=self.ui_colors["canvas"])
        self.canvas.grid(row=1, column=0, sticky="nsew")
        self.canvas.bind("<Button-1>", self.touch)
        self.canvas.bind("<B1-Motion>", self.drag_body)
        self.canvas.bind("<ButtonRelease-1>", self.release_body)
        self.canvas.bind("<Configure>",self.resize_preview)
        self.image_item = self.canvas.create_image(0,0,anchor="nw")
        self.controls_host = ttk.Frame(self.workspace, style="Panel.TFrame")
        self.controls_host.grid(row=0, column=1, sticky="nsew", padx=self.ui_pixels((20, 0)))
        self.controls_host.rowconfigure(1, weight=1)
        self.controls_host.columnconfigure(0, weight=1)
        font = renderer.rig["style"]["host_font"]
        self.label_font = tkfont.Font(root=root,family=FONTS["mono"][0],size=font["size"])
        self.label_row_height = self.label_font.metrics("linespace")+6
        self.build_inspector_header()
        self.tabs = ttk.Notebook(self.controls_host)
        self.tabs.grid(row=1, column=0, sticky="nsew")
        self.inspector_tabs = []
        for name in ("Face", "Shape", "Inputs", "Inspect"):
            tab = InspectorTab(self.tabs, self.ui_colors["panel"], self.root._cody_ui_pixel_scale)
            self.tabs.add(tab, text=name)
            self.inspector_tabs.append(tab)
        appearance, contour, stimuli, overlay = (tab.content for tab in self.inspector_tabs)
        self.build_face_controls(appearance)
        self.build_contour_controls(contour)
        self.build_stimulus_controls(stimuli)
        self.build_debug_controls(overlay)
        self.tabs.select(self.inspector_tabs[0])
        if args.ccp:
            ccp_tab = InspectorTab(self.tabs, self.ui_colors["panel"], self.root._cody_ui_pixel_scale)
            self.tabs.add(ccp_tab, text="CCP")
            self.inspector_tabs.append(ccp_tab)
            self.ccp_panel = CcpPanel(self, ccp_tab.content)
            self.tabs.select(ccp_tab)
        self.tabs.bind("<<NotebookTabChanged>>", self.inspector_tab_changed)
        if self.jelly_study:
            # Artistic host controls only; no firmware defaults or sensor samples change.
            self.contour_values['amplitude'].set(.9)
            self.contour_values['speed'].set(.4)
            self.tabs.select(self.inspector_tabs[1])
        self.info = ttk.Label(overlay, text="Select a point to inspect its stable name.",
                              style="Muted.TLabel", wraplength=self.ui_pixels(330))
        self.info.pack(fill="x", pady=(18, 0))
        status_bar = ttk.Frame(root, style="Studio.TFrame", padding=self.ui_pixels((20, 10, 20, 12)))
        status_bar.grid(row=2, column=0, sticky="ew")
        self.timing = ttk.Label(status_bar, text="SYNTHETIC / host timings only", style="Status.TLabel")
        self.timing.pack(fill="x")
        self.root.bind("<Escape>",self.back)
        self.root.bind("<Configure>",self.resize_window)
        self.root.bind("<MouseWheel>",self.scroll_controls)
        self.root.protocol("WM_DELETE_WINDOW",self.close)
        work_left, work_top, work_right, work_bottom = window_work_area(root)
        self.available_ui_width = max(1, work_right - work_left - self.ui_pixels(40))
        self.available_ui_height = max(1, work_bottom - work_top - self.ui_pixels(60))
        width = min(self.ui_pixels(1440), self.available_ui_width)
        height = min(self.ui_pixels(900), self.available_ui_height)
        self.root.minsize(min(self.ui_pixels(700), width), min(self.ui_pixels(560), height))
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)
        left = work_left + (work_right - work_left - width) // 2
        top = work_top + (work_bottom - work_top - height) // 2
        self.root.geometry(f"{width}x{height}{left:+d}{top:+d}")
        self.arrange_panels(width)
        self.tick()

    # self: Desktop controller converting UI spacing without changing the logical scene.
    # value: One 96-DPI pixel count or a tuple of counts used for widget padding.
    # Raises tk.TclError if the startup UI scale cannot be read.
    def ui_pixels(self, value):
        if isinstance(value, tuple):
            return tuple(self.ui_pixels(count) for count in value)
        return 0 if value == 0 else scale_pixels(self.root, value)

    # self: Desktop controller requesting fresh readouts for the newly visible tab.
    # event: Notebook selection notification, independent of synthetic model time.
    def inspector_tab_changed(self, event):
        self.next_readout_at = 0.0

    # self: Desktop controller updating only the visible inspector at a bounded cadence.
    # inputs: The exact current synthetic snapshot, never a cached firmware observation.
    # targets: Current contour targets from the response composer.
    # screen: Current presentation route controlling model readout availability.
    # now: Monotonic host time at the beginning of this animation tick.
    # Raises tk.TclError or ValueError if an inspector readout cannot be updated.
    def refresh_inspector_readouts(self, inputs, targets, screen, now):
        selected = self.tabs.select()
        if selected == self.readout_tab and now < self.next_readout_at:
            return
        self.readout_tab = selected
        self.next_readout_at = now + .1
        # Only display text is throttled. Model, pointer and channel clocks still
        # advance on every tick using the current synthetic snapshot.
        if selected == str(self.inspector_tabs[1]):
            self.update_contour_readouts(targets, screen)
        elif selected == str(self.inspector_tabs[2]):
            self.update_stimulus_readouts(inputs, screen)
        elif selected == str(self.inspector_tabs[3]):
            self.update_point_inspector()

    # self: Desktop controller owning the fixed studio header and quick switches.
    # preview_name: Human-readable identity of the selected host rendering mode.
    # Raises tk.TclError if Tk cannot construct the header.
    # Raises OSError if the bundled approved logo cannot be read.
    def build_studio_header(self, preview_name):
        header = ttk.Frame(self.root, style="Studio.TFrame", padding=self.ui_pixels((20, 18, 20, 18)))
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(0, weight=1)
        identity = ttk.Frame(header, style="Studio.TFrame")
        identity.grid(row=0, column=0, columnspan=2, sticky="w")
        with Image.open(ROOT / "simulator/windows/assets/branding/vibeware-logo.png") as logo:
            # Display scaling preserves the approved source canvas and pixel-art edges.
            side = self.ui_pixels(80)
            self.brand_logo = ImageTk.PhotoImage(logo.resize((side, side), Image.Resampling.NEAREST))
        self.root.iconphoto(True, self.brand_logo)
        ttk.Label(identity, image=self.brand_logo, style="Studio.TLabel").pack(side="left", padx=self.ui_pixels((0, 14)))
        title = ttk.Frame(identity, style="Studio.TFrame")
        title.pack(side="left")
        ttk.Label(title, text="Cody Studio", style="Title.TLabel").pack(anchor="w")
        ttk.Label(title, text=preview_name + " / character playground", style="Subtitle.TLabel",
                  background=self.ui_colors["background"]).pack(anchor="w", pady=(2, 0))
        links = ttk.Frame(header, style="Studio.TFrame")
        links.grid(row=1, column=0, sticky="w", pady=self.ui_pixels((6, 0)))
        for link, label in ((StudioLink.PROJECT, "GitHub"), (StudioLink.WEBSITE, "Website"),
                            (StudioLink.MANIFESTO, "Manifesto")):
            ttk.Button(links, text=label, style="Link.TButton",
                       command=lambda target=link: self.open_studio_link(target)).pack(side="left", padx=self.ui_pixels((0, 8)))
        actions = ttk.Frame(header, style="Studio.TFrame")
        actions.grid(row=1, column=1, sticky="e", pady=self.ui_pixels((6, 0)))
        ttk.Label(actions, text="SYNTHETIC", style="Badge.TLabel").pack(side="left", padx=(0, 16))
        ttk.Checkbutton(actions, text="Animate", variable=self.playing, style="Toolbar.TCheckbutton").pack(side="left", padx=(0, 12))
        ttk.Checkbutton(actions, text="Rig", variable=self.debug, style="Toolbar.TCheckbutton").pack(side="left")

    # self: Host controller retaining browser-launch errors in its visible status.
    # link: Typed public destination opened only by an explicit user click.
    def open_studio_link(self, link):
        try:
            if not webbrowser.open_new_tab(link.value):
                raise RuntimeError(f"Browser rejected {link.name}: {link.value}")
        except (OSError, webbrowser.Error, RuntimeError) as error:
            self.last_link_error = error
            print(f"[cody-ui] Open {link.name} failed: {error!r}", file=sys.stderr, flush=True)

    # self: Desktop controller retaining the permanent scene and orientation controls.
    # Raises tk.TclError if Tk cannot construct the inspector header.
    def build_inspector_header(self):
        header = ttk.Frame(self.controls_host, style="Panel.TFrame", padding=self.ui_pixels((18, 16, 18, 16)))
        header.grid(row=0, column=0, sticky="ew")
        ttk.Label(header, text="Character controls", style="Section.TLabel").pack(anchor="w")
        ttk.Label(header, text="Local preview / physical device separate", style="Muted.TLabel").pack(anchor="w", pady=(4, 14))
        selection = ttk.Frame(header, style="Panel.TFrame")
        selection.pack(fill="x")
        selection.columnconfigure(0, weight=1)
        ttk.Label(selection, text="Scene", style="Muted.TLabel").grid(row=0, column=0, sticky="w", pady=(0, 6))
        ttk.Label(selection, text="Rotation", style="Muted.TLabel").grid(row=0, column=1, sticky="w", padx=(12, 0), pady=(0, 6))
        ttk.Combobox(selection, textvariable=self.screen, values=SCREEN_NAMES,
                     state="readonly", width=20).grid(row=1, column=0, sticky="ew")
        ttk.Combobox(selection, textvariable=self.rotation, values=(0, 90, 180, 270),
                     state="readonly", width=5).grid(row=1, column=1, sticky="ew", padx=(12, 0))
        ttk.Label(selection, text="Preview limit / FPS", style="Muted.TLabel").grid(
            row=2, column=0, sticky="w", pady=self.ui_pixels((12, 0)))
        ttk.Combobox(selection, textvariable=self.preview_hz, values=(15, 25, 30, 60),
                     state="readonly", width=5).grid(
                         row=2, column=1, sticky="ew", padx=self.ui_pixels((12, 0)),
                         pady=self.ui_pixels((12, 0)))

    # self: Inspector controller grouping related controls without changing their state.
    # parent: Tab content owning the section.
    # title: Short English label for the group.
    # description: Optional explanatory text beneath the section label.
    # Raises tk.TclError if Tk cannot construct the section.
    def add_section(self, parent, title, description=None):
        section = ttk.Labelframe(parent, text=title, style="Card.TLabelframe", padding=self.ui_pixels(14))
        section.pack(fill="x", pady=self.ui_pixels((0, 16)))
        if description:
            ttk.Label(section, text=description, style="Muted.TLabel", wraplength=self.ui_pixels(310)).pack(fill="x", pady=self.ui_pixels((0, 12)))
        return section

    # self: Preview controller owning runtime appearance preferences.
    # appearance: Face tab owning the named sliders and expression buttons.
    # Raises tk.TclError if Tk cannot construct the face controls.
    def build_face_controls(self, appearance):
        symbols = self.add_section(appearance, "Emotion symbol",
            "One symbol at a time. None allows automatic caress hearts; Sleep selects its own symbol.")
        ttk.Combobox(symbols, textvariable=self.personality_cue,
                     values=tuple(cue.name for cue in PersonalityCue), state="readonly").pack(fill="x")
        ttk.Label(symbols, text="Device alerts here are synthetic previews.",
                  style="Muted.TLabel", wraplength=self.ui_pixels(310)).pack(fill="x", pady=(10, 0))
        # Targets belong to the controls; smoothing belongs to the animation state.
        presets = self.add_section(appearance, "Expressions")
        phase_row = ttk.Frame(presets)
        phase_row.pack(fill="x", pady=(0, 10))
        for column, phase in enumerate(("sleep", "waking", "ready")):
            phase_row.columnconfigure(column, weight=1, uniform="phase")
            ttk.Button(phase_row, text=phase.title(), style="Preset.TButton",
                       command=lambda value=phase: self.preset(value)).grid(
                           row=0, column=column, sticky="ew", padx=(0 if column == 0 else 4, 0))
        expression_grid = ttk.Frame(presets)
        expression_grid.pack(fill="x")
        for index, name in enumerate(self.renderer.rig["expression_presets"]):
            column = index % 2
            expression_grid.columnconfigure(column, weight=1, uniform="expression")
            ttk.Button(expression_grid, text=name.replace("_", " ").title(), style="Preset.TButton",
                       command=lambda value=name: self.preset(value)).grid(
                           row=index // 2, column=column, sticky="ew", pady=3,
                           padx=(0 if column == 0 else 6, 0))
        animation = self.add_section(appearance, "Animation")
        if self.jelly_study:
            self.add_toggle(animation, "Face follows jelly body", self.face_follows_body)
        self.add_toggle(animation, "Breathing and blinking", self.playing)
        self.add_toggle(animation, "Authored demo cycle", self.demo_enabled)
        channels = self.add_section(appearance, "Fine tuning", "Manual edits release the selected expression preset.")
        self.values = []
        for channel in self.renderer.rig["channels"]:
            value = tk.DoubleVar(value=channel["default"])
            value.trace_add("write", self.clear_face_preset)
            self.values.append(value)
            self.add_slider(channels, channel["name"].replace("_", " ").title(),
                            channel["minimum"], channel["maximum"], value)

    # self: Inspector controller retaining independent contour targets.
    # contour: Outline tab owning the independent wave controls.
    # Raises tk.TclError if Tk cannot construct the shape controls.
    def build_contour_controls(self, contour):
        # A new target nudges the wave; it never starts the clock over.
        motion = self.add_section(contour, "Cloud motion", "Targets blend without restarting the wave. Paused edits apply on resume.")
        self.add_toggle(motion, "Animate cloud shape", self.contour_playing)
        shape = self.add_section(contour, "Shape tuning")
        self.contour_values = {}
        self.contour_readouts = {}
        defaults = contour_defaults(self.renderer.rig)
        for control in self.renderer.rig["contour"]["controls"]:
            name = control["name"]
            value = tk.DoubleVar(value=defaults[name])
            self.contour_values[name]=value
            self.contour_readouts[name]=self.add_slider(shape,control["label"],control["minimum"],control["maximum"],value,readout=True)
        ttk.Label(shape, text="Values: current > target. Speed in cycles/s.\nFullness changes the resting shape.",
                  style="Muted.TLabel", wraplength=self.ui_pixels(310)).pack(fill="x", pady=(12, 0))
        self.contour_info = ttk.Label(contour, text="", style="Muted.TLabel")
        self.contour_info.pack(anchor="w",pady=4)
        ttk.Button(contour, text="Restore shape defaults", command=self.reset_contour).pack(fill="x", pady=10)

    # self: Preview controller owning shared geometry/response overlay switches.
    # overlay: Debug tab owning point selection, labels and placement.
    # Raises tk.TclError when widgets cannot be constructed.
    def build_debug_controls(self, overlay):
        geometry = self.add_section(overlay, "Geometry overlays",
            "Desktop inspection only. Disable rig points to recognize caresses.")
        for text,variable in (("Points, links and curve handles",self.debug),
                              ("Stable point names + leader lines",self.names),
                              ("Current contour / mint",self.outline),
                              ("Original contour / dashed amber",self.rest_outline),
                              ("Resting root + inertia vector",self.motion_overlay),
                              ("320 x 320 safe square",self.guide)):
            self.add_toggle(geometry,text,variable)
        selection = self.add_section(overlay, "Point inspector")
        ttk.Label(selection, text="Label scope", style="Muted.TLabel").pack(anchor="w")
        scope = ttk.Combobox(selection,textvariable=self.label_scope,values=LABEL_SCOPES,state="readonly")
        scope.pack(fill="x",pady=(8,2))
        scope.bind("<<ComboboxSelected>>",self.reset_label_page)
        pages = ttk.Frame(selection)
        pages.pack(fill="x",pady=2)
        ttk.Button(pages,text="<",width=3,style="Compact.TButton",command=lambda:self.change_label_page(-1)).pack(side="left")
        self.page_info = ttk.Label(pages,text="")
        self.page_info.pack(side="left",padx=8)
        ttk.Button(pages,text=">",width=3,style="Compact.TButton",command=lambda:self.change_label_page(1)).pack(side="right")
        self.entity_choices = {
            f"{entity['label']} [{name}]": name
            for name, entity in self.renderer.rig["entities"].items()
        }
        self.entity_picker = ttk.Combobox(
            selection, values=list(self.entity_choices), state="readonly"
        )
        self.entity_picker.pack(fill="x",pady=(6,2))
        self.entity_picker.current(0)
        self.entity_picker.bind("<<ComboboxSelected>>",self.select_entity)
        self.point_picker = ttk.Combobox(selection,values=[point["name"] for point in self.renderer.rig["points"]],state="readonly")
        self.point_picker.pack(fill="x",pady=(2,6))
        self.point_picker.bind("<<ComboboxSelected>>",self.select_point)
        placement = self.add_section(overlay, "Character placement", "Logical display coordinates; the native canvas remains 466 x 466.")
        self.transform = []
        for key,minimum,maximum in (("x",80,386),("y",100,360),("scale",60,180)):
            value = tk.DoubleVar(value=self.renderer.rig['default_transform'][key])
            self.transform.append(value)
            self.add_slider(placement, key.title(), minimum, maximum, value)

    # self: Preview controller whose source targets stay separate from artistic model state.
    # stimuli: Responsive, scrollable tab owning synthetic source controls and model readouts.
    # Raises tk.TclError when widgets cannot be constructed.
    def build_stimulus_controls(self, stimuli):
        motion = self.add_section(stimuli, "Motion / synthetic",
            "50 Hz host snapshot. Board axes in m/s2; Z includes 9.81 m/s2 gravity.")
        health_choices = tuple(health.value for health in SampleHealth)
        self.motion_health = tk.StringVar(value=SampleHealth.CURRENT.value)
        ttk.Combobox(motion, textvariable=self.motion_health, values=health_choices, state="readonly").pack(fill="x", pady=6)
        self.acceleration = {}
        self.acceleration_readouts = {}
        for axis in Axis:
            variable = tk.DoubleVar(value=0)
            self.acceleration[axis] = variable
            self.acceleration_readouts[axis] = self.add_slider(motion, f"Acceleration {axis.value.upper()}", -20, 20, variable, readout=True)
        self.motion_sensitivity = tk.DoubleVar(value=self.response_config["motion"]["sensitivity"])
        self.sensitivity_readout = self.add_slider(motion, "Sensitivity", 0, 4, self.motion_sensitivity, readout=True)
        pulse_row = ttk.Frame(motion)
        pulse_row.pack(fill="x", pady=6)
        self.pulse_axis = tk.StringVar(value=Axis.X.value)
        ttk.Combobox(pulse_row, textvariable=self.pulse_axis, values=tuple(axis.value for axis in Axis), state="readonly", width=4).pack(side="left")
        ttk.Button(pulse_row, text="Pulse +8 m/s2", command=self.pulse_motion).pack(side="right", fill="x", expand=True, padx=(8, 0))
        self.shaking = tk.BooleanVar(value=False)
        self.add_toggle(motion, "Continuous shake / all axes", self.shaking)
        power = self.add_section(stimuli, "Battery / synthetic")
        self.power_health = tk.StringVar(value=SampleHealth.CURRENT.value)
        ttk.Combobox(power, textvariable=self.power_health, values=health_choices, state="readonly").pack(fill="x", pady=6)
        self.charge_percent = tk.DoubleVar(value=100)
        self.charge_readout = self.add_slider(power, "Charge level", 0, 100, self.charge_percent, readout=True)
        self.charging = tk.BooleanVar(value=False)
        self.add_toggle(power, "Charging", self.charging)
        self.battery_present = tk.BooleanVar(value=True)
        self.add_toggle(power, "Battery present", self.battery_present)
        ttk.Label(power, text="Missing battery reports UNAVAILABLE.", style="Muted.TLabel").pack(anchor="w", pady=(6, 0))
        audio = self.add_section(stimuli, "Audio / synthetic", "Normalized PCM means; these controls are host inputs.")
        self.microphone_health = tk.StringVar(value=SampleHealth.CURRENT.value)
        ttk.Label(audio, text="Microphone health", style="Muted.TLabel").pack(anchor="w", pady=(0, 6))
        ttk.Combobox(audio, textvariable=self.microphone_health, values=health_choices, state="readonly").pack(fill="x")
        self.microphone_levels = (tk.DoubleVar(value=0), tk.DoubleVar(value=0))
        for channel, variable in zip(("Left", "Right"), self.microphone_levels):
            self.add_slider(audio, f"Microphone {channel}", 0, 1, variable)
        self.playback_health = tk.StringVar(value=SampleHealth.CURRENT.value)
        ttk.Label(audio, text="Playback health", style="Muted.TLabel").pack(anchor="w", pady=(14, 6))
        ttk.Combobox(audio, textvariable=self.playback_health, values=health_choices, state="readonly").pack(fill="x")
        self.playback_active = tk.BooleanVar(value=False)
        self.add_toggle(audio, "Playback active", self.playback_active)
        self.playback_level = tk.DoubleVar(value=.18)
        self.add_slider(audio, "Playback PCM mean", 0, 1, self.playback_level)
        cues = self.add_section(stimuli, "Local sound previews", "These buttons use the PC speaker, never Cody's speaker.")
        self.add_toggle(cues, "PC fake audio driver / synthetic", self.audio_enabled)
        for cue, label in (("audio_ready", "Preview audio-ready cue"), ("eyes_open", "Preview eyes-open strings")):
            ttk.Button(cues, text=label, command=lambda selected=cue: self.preview_cue(selected)).pack(fill="x", pady=3)
        ttk.Button(stimuli, text="Interaction / cancel idle scene", command=self.preview_interaction).pack(fill="x", pady=3)
        self.response_info = ttk.Label(stimuli, text="", style="Muted.TLabel", wraplength=self.ui_pixels(330))
        self.response_info.pack(fill="x", pady=8)
        ttk.Button(stimuli, text="Reset response model", command=self.reset_response).pack(fill="x")

    # self: Preview controller; this button plays a local authored WAV, never the device speaker.
    # cue: Stable identity of one of the two generated original boot cues.
    # Admission/playback failures remain visible through the fake driver.
    def preview_cue(self, cue):
        path = ROOT / "assets/cody/runtime/generated" / f"cody-{cue}.wav"
        self.audio.set_enabled(self.audio_enabled.get())
        self.audio.preview_file(path)

    # self: Host controller; only a successfully presented frame may request scene audio.
    # screen: Semantic identity accepted by Tk, sharing the displayed hit geometry.
    # personality_only: Admit only the actual visible manga cue on CCP/debug presentations.
    # Raises ValueError for an invalid authored scene clock or identity.
    def present_audio(self, screen, *, personality_only=False):
        self.audio.set_enabled(self.audio_enabled.get())
        painter = self.renderer.personality_painter
        frame = painter.last_frame if painter is not None else None
        cue = (frame.cue if frame is not None and frame.opacity > 0 and
               frame.status == PersonalityPresentationStatus.VISIBLE else PersonalityCue.NONE)
        if cue != PersonalityCue.NONE and cue != self.last_audio_cue:
            self.audio.personality(cue.value)
        self.last_audio_cue = cue
        if personality_only:
            return
        if (screen == Screen.RIG and self.playing.get() and
                self.response_state.idle_scene == IdleSceneId.WHISTLE and
                self.response_state.idle_scene_weight >= .8 and
                self.response_state.idle_scene_generation != self.last_whistle_generation):
            self.audio.whistle()
            self.last_whistle_generation = self.response_state.idle_scene_generation
        if screen != self.last_audio_screen:
            self.last_audio_glyphs = 0
            if screen == Screen.BOOT_WAKING:
                self.preview_cue("eyes_open")
            elif screen == Screen.BOOT_READY:
                self.preview_cue("audio_ready")
        if screen == Screen.GREETING:
            count = self.renderer.scene_painter.visible_characters("greeting", self.scene_elapsed_ms)
            self.audio.typewriter(max(0, count - self.last_audio_glyphs))
            self.last_audio_glyphs = count
        self.last_audio_screen = screen

    # self: Preview controller publishing explicit activity through the synthetic snapshot owner.
    def preview_interaction(self):
        self.stimulus_source.interact(int(time.perf_counter() * 1_000_000))

    # self: Preview controller supplying the chosen board axis to its synthetic source.
    # Raises ValueError for an unsupported selected axis, or tk.TclError on a lost control.
    def pulse_motion(self):
        self.stimulus_source.pulse(self.pulse_axis.get(), int(time.perf_counter() * 1_000_000))

    # self: Preview controller receiving an explicit debug-only reset of model state.
    def reset_response(self):
        self.response_state = self.renderer.reset_model(self.response_config, randbits(32) | 1)
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
        if screen not in ANIMATED_SCREENS:
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
        status = "RUNNING" if screen in TIMED_SCREENS else "PAUSED / boot artwork"
        self.response_info.configure(text=f"{status} / SYNTHETIC\n{motion_text}\n{power_text}\n"
            f"Microphone: {state.microphone_status.health.value}\nPlayback: {state.playback_status.health.value}\n"
            f"MODEL attention: {state.attention:.3f} / speaking: {state.speaking:.3f}\n"
            f"MODEL idle: {state.idle_seconds:.1f}s / scene: {state.idle_scene.name}\n"
            f"MODEL scene weight: {state.idle_scene_weight:.3f} / whistle audio: PC FAKE DRIVER\n"
            f"MODEL annoyance: {state.annoyance:.3f} / eye closure: {state.protective_eye_closure:.3f}\n"
            f"MODEL stress: {state.stress:.3f}\nMODEL fatigue: {fatigue_text}{body_text}")

    # self: Inspector controller creating an independent setting row.
    # parent owns the shared checkbox row.
    # text describes the independent setting in English.
    # variable is the existing BooleanVar that owns the setting.
    # Raises tk.TclError if Tk cannot construct the toggle.
    def add_toggle(self, parent, text, variable):
        ttk.Checkbutton(parent, text=text, variable=variable).pack(fill="x", pady=5)

    # self: Inspector controller creating a target slider with a stable value readout.
    # parent owns the shared label/slider row.
    # label identifies the authored control.
    # minimum is the lower bound accepted by the underlying contract.
    # maximum is the upper bound accepted by the underlying contract.
    # variable owns the target value independently from rendered state.
    # readout requests a right-aligned label for current/target values.
    # Raises tk.TclError if Tk cannot construct or observe the slider.
    def add_slider(self, parent, label, minimum, maximum, variable, readout=False):
        header = ttk.Frame(parent)
        header.pack(fill="x",pady=(14,6))
        ttk.Label(header,text=label).pack(side="left")
        output = ttk.Label(header, text=f"{variable.get():.2f}", style="Value.TLabel")
        output.pack(side="right", padx=(8, 0))
        if not readout:
            # Model readouts are updated by the frame owner; ordinary sliders show their targets.
            variable.trace_add("write", lambda *_: self.update_slider_value(variable, output))
        ttk.Scale(parent, from_=minimum, to=maximum, variable=variable, length=self.ui_pixels(280)).pack(fill="x", pady=(0, 4))
        return output

    # self: Inspector controller updating an ordinary authored target label.
    # variable: Existing DoubleVar owning the slider target.
    # output: Value label retained by the same widget tree.
    # Raises tk.TclError if the target cannot be read or the label has been destroyed.
    def update_slider_value(self, variable, output):
        output.configure(text=f"{variable.get():.2f}")

    # self: Desktop controller arranging preview and inspector within the workspace.
    # width is the current outer-window width used only to choose panel arrangement.
    # Raises tk.TclError if Tk cannot update the grid.
    def arrange_panels(self, width):
        stack_fits = self.available_ui_height >= self.ui_pixels(720)
        compact_wide = not stack_fits and width >= self.ui_pixels(760)
        mode = "wide" if width >= self.ui_pixels(1000) or compact_wide else "stacked"
        if mode==self.layout_mode:
            return
        self.layout_mode = mode
        # Stacking needs enough height for both the character and a usable scroll viewport.
        self.root.minsize(
            min(self.ui_pixels(700), self.available_ui_width),
            min(self.ui_pixels(560 if mode == "wide" else 720), self.available_ui_height),
        )
        self.workspace.columnconfigure(0, weight=1, minsize=0)
        self.workspace.columnconfigure(1, weight=0, minsize=0)
        if mode=="wide":
            self.workspace.rowconfigure(0, weight=1, minsize=0)
            self.workspace.rowconfigure(1, weight=0, minsize=0)
            self.controls_host.grid(row=0, column=1, sticky="nsew", padx=self.ui_pixels((20, 0)), pady=0)
        else:
            self.workspace.rowconfigure(0, weight=1, minsize=self.ui_pixels(160))
            self.workspace.rowconfigure(1, weight=2, minsize=self.ui_pixels(300))
            self.controls_host.grid(row=1, column=0, sticky="nsew", padx=0, pady=self.ui_pixels((16, 0)))

    # self: Desktop controller whose root width determines the panel arrangement.
    # event is a root Configure notification; child notifications do not change panel layout.
    # Raises tk.TclError if Tk cannot resize the status label or grid.
    def resize_window(self, event):
        if event.widget is not self.root:
            return
        self.arrange_panels(event.width)
        self.timing.configure(wraplength=max(self.ui_pixels(160),event.width-self.ui_pixels(40)))

    # event supplies the allocated preview dimensions, coalesced until the next normal frame.
    def resize_preview(self, event):
        self.pending_canvas_size = (max(1,event.width),max(1,event.height))

    # self: Desktop controller routing wheel input to the tab under the pointer.
    # event supplies a wheel delta; only descendants of the controls pane are scrolled.
    # Raises tk.TclError if Tk cannot locate the hovered widget or update the viewport.
    def scroll_controls(self, event):
        widget = self.root.winfo_containing(event.x_root, event.y_root)
        if isinstance(widget, (ttk.Scale, ttk.Combobox)):
            return
        for tab in self.inspector_tabs:
            if tab.contains(widget) and event.delta:
                tab.scroll(event.delta)
                return "break"

    # self: Host controller; stop callbacks, optional CCP and playback before destroying Tk.
    # Raises RuntimeError or OSError if CCP worker/socket ownership cannot be retired.
    def close(self):
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        if self.ccp_panel is not None:
            self.ccp_panel.close()
        self.audio.close()
        self.root.destroy()

    # self: Preview controller retaining the semantic selected expression identity.
    # name: Validated boot or expression preset applied as smooth channel targets.
    # Raises KeyError when the requested preset is not defined in the YAML catalog.
    def preset(self, name):
        values = self.face_presets[name]
        self.screen.set(Screen.RIG.value)
        for target,value in zip(self.values,values):
            target.set(value)
        self.face_preset_name = name

    # self: Preview controller whose manual face edits end the named expression.
    # arguments: Tk trace arguments; edits do not infer a new preset from numeric values.
    def clear_face_preset(self, *arguments):
        self.face_preset_name = None

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
        active_names = set()
        names = self.overlay_names()
        if not self.names.get() or self.display_screen != Screen.RIG or self.scene is None or self.layout.height<self.label_row_height+24:
            self.remove_unused_labels(active_names)
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
                active_names.add(name)
                if name not in self.label_items:
                    self.label_items[name] = (
                        self.canvas.create_line(0, 0, 0, 0, tags=tags),
                        self.canvas.create_oval(0, 0, 0, 0, tags=tags),
                        self.canvas.create_text(0, 0, tags=tags),
                    )
                leader, marker, label = self.label_items[name]
                self.canvas.coords(leader, label_x, row, elbow, row, x, y)
                self.canvas.itemconfigure(leader, fill=color(colors["link"]), width=metrics["link_width"])
                self.canvas.coords(marker, x - marker_radius, y - marker_radius,
                                   x + marker_radius, y + marker_radius)
                self.canvas.itemconfigure(marker, outline=ink, width=metrics["outline_width"])
                self.canvas.coords(label, label_x, row)
                self.canvas.itemconfigure(label, text=self.fit_label(name, width),
                                          anchor="e" if side == "left" else "w",
                                          fill=ink, font=self.label_font)
        self.remove_unused_labels(active_names)

    # active_names: Stable authored point names still shown in this label page.
    # Raises tk.TclError if Tk has already destroyed the canvas.
    def remove_unused_labels(self, active_names):
        for name in tuple(self.label_items):
            if name not in active_names:
                for item in self.label_items.pop(name):
                    self.canvas.delete(item)


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

    # self: Preview controller using the same displayed geometry for touch and eye hits.
    # event: Desktop coordinates projected through the last displayed layout and rotation.
    # Audio failures are retained by the optional fake driver without stopping touch handling.
    def touch(self, event):
        if self.ccp_panel is not None and self.ccp_panel.follow.get():
            self.ccp_panel.press(self.layout.to_logical(event.x, event.y, self.display_rotation))
            return
        self.preview_interaction()
        self.body_drag_origin = None
        self.petting.cancel()
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
        self.audio.set_enabled(self.audio_enabled.get())
        self.audio.touch()
        if self.display_screen == Screen.BOOT_READY and 73<=x<393 and 73<=y<393:
            self.screen.set(Screen.RIG.value)
            return
        if self.display_screen not in ANIMATED_SCREENS:
            return
        content_visible = (self.display_screen != Screen.GREETING or
            self.renderer.scene_painter.presentation("greeting", self.scene_elapsed_ms)[2])
        if not content_visible:
            return
        now_us = int(time.perf_counter() * 1_000_000)
        previous_opacity = self.navigation_visibility.opacity(now_us)
        self.navigation_visibility.interact(now_us)
        if self.renderer.debug_controls_enabled:
            control = next((control for control in self.renderer.navigation["controls"]
                if control["visible_by_default"] and control_contains(control, x, y)), None)
            if control is not None:
                if previous_opacity != 255:
                    return  # Reveal a hidden/fading toolbar without activating its button.
                if control["id"] == "back":
                    self.back()
                elif control["id"] == "overlay":
                    self.debug.set(not self.debug.get())
                    self.choose_point(None)
                elif control["id"] == "screenshot":
                    self.info.configure(text="Firmware camera writes BMP files to SD; host preview does not emulate storage.")
                else:
                    self.playing.set(not self.playing.get())
                return
        # Debug leaders never override the device's fixed navigation touch targets.
        if self.scene is not None:
            eye = self.renderer.hit_eye(self.scene, (x, y), self.response_config["touch"]["eye_hit_radius"])
            if eye != EyeId.NONE:
                self.stimulus_source.tap_eye(eye, int(time.perf_counter() * 1_000_000))
            if self.playing.get() and self.renderer.hit_body(self.scene, (x, y)):
                self.body_drag_origin = (x, y)
                self.body_drag_point = (x, y)
                self.body_drag_rotation = self.display_rotation
                self.body_drag_started_us = int(time.perf_counter() * 1_000_000)
                self.stimulus_source.publish_body_touch(True, self.body_drag_origin, (x, y),
                    self.body_drag_rotation, self.body_drag_started_us, self.body_drag_started_us)
                self.observe_petting((x, y), True, self.body_drag_started_us)
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

    # self: Preview controller retaining the gesture's original logical basis.
    # event: Host pointer movement, interpreted using the last displayed layout.
    def drag_body(self, event):
        if self.ccp_panel is not None and self.ccp_panel.follow.get():
            return
        self.navigation_visibility.interact(int(time.perf_counter() * 1_000_000))
        if self.body_drag_origin is None:
            return
        point = self.layout.to_logical(event.x, event.y, self.body_drag_rotation)
        if point is None:
            self.petting.cancel()
            return
        self.body_drag_point = point
        self.stimulus_source.publish_body_touch(True, self.body_drag_origin, point,
            self.body_drag_rotation, self.body_drag_started_us, int(time.perf_counter() * 1_000_000))
        self.observe_petting(point, False, int(time.perf_counter() * 1_000_000))

    # self: Synthetic preview owner; release lets the shared springs settle.
    # event: Actual host pointer release, including release outside the preview.
    def release_body(self, event):
        if self.ccp_panel is not None and self.ccp_panel.follow.get():
            self.ccp_panel.release(self.layout.to_logical(event.x, event.y, self.display_rotation))
            return
        self.navigation_visibility.interact(int(time.perf_counter() * 1_000_000))
        self.petting.cancel()
        if self.body_drag_origin is None:
            return
        point = self.layout.to_logical(event.x, event.y, self.body_drag_rotation)
        # Outside the preview there is no logical hit. The host-only release
        # preserves the last known gesture position rather than clipping it.
        if point is None:
            point = self.body_drag_point
        self.stimulus_source.publish_body_touch(False, self.body_drag_origin, point,
            self.body_drag_rotation, self.body_drag_started_us, int(time.perf_counter() * 1_000_000))
        self.body_drag_origin = None

    # self: Synthetic pointer controller retaining manual symbol selection.
    # point: Current logical desktop pointer coordinate.
    # pressed_edge: First pointer report of the contact.
    # sampled_us: Original monotonic desktop event timestamp.
    # Raises ValueError for rejected pointer time or displayed geometry.
    # Raises RuntimeError for the actual native caress or mood error in ordinary Studio.
    def observe_petting(self, point, pressed_edge, sampled_us):
        if (self.scene is None or self.display_screen != Screen.RIG or not self.playing.get() or
                self.debug.get() or self.personality_sleep or self.body_drag_rotation != self.display_rotation):
            self.petting.cancel()
            return
        # An explicitly selected authoring study has no displayed C++ frame. Never
        # feed its independent geometry to the ordinary native caress classifier.
        if self.native_behavior is not None and (self.renderer.offline_study or
                self.renderer.jelly_study_enabled or '_native_scene' not in self.scene):
            self.petting.cancel()
            return
        if not 0 <= point[0] < SIZE or not 0 <= point[1] < SIZE:
            self.petting.cancel()
            return
        zone = self.petting.observe(self.scene, point, True, pressed_edge, sampled_us)
        if zone != PettingZone.NONE:
            self.advance_happiness_clock(sampled_us)
            event = PersonalityEvent[self.personality_rules["caress"]["event"].upper()]
            if self.native_behavior is not None:
                self.happiness_state = self.native_behavior.apply_event(event)
            else:
                self.happiness_state = apply_event(self.personality_rules, self.happiness_state, event)
            # Match firmware arbitration: normal symbols cannot drop an accepted stroke.
            self.affection_remaining_us = self.petting.config["affection_us"]
            self.affection_owns_channel = True

    # self: Synthetic mood owner with an independent awake clock.
    # sampled_us: Monotonic host time, including time during animation pause.
    # Raises ValueError for a backwards clock or invalid artistic state.
    # Raises RuntimeError for actual native mood failures in ordinary Studio.
    def advance_happiness_clock(self, sampled_us):
        if type(sampled_us) is not int or sampled_us < self.happiness_sampled_us:
            raise ValueError(f"Happiness acquisition clock rejected: {sampled_us!r}")
        elapsed_us = sampled_us - self.happiness_sampled_us
        if self.native_behavior is not None:
            # The sleeping route rebases awake time; animation pause remains awake.
            if self.screen.get() == Screen.BOOT_SLEEP:
                elapsed_us = 0
            self.happiness_state = self.native_behavior.advance(elapsed_us)
        else:
            self.happiness_state = advance_happiness(self.personality_rules, self.happiness_state, elapsed_us)
        self.happiness_sampled_us = sampled_us

    # self: Ordinary native mood snapshot or the explicitly selected offline study state.
    # Raises ValueError for an invalid offline artistic state.
    def happiness_sadness(self):
        if self.native_behavior is not None:
            return self.happiness_state.sadness
        return sadness(self.personality_rules, self.happiness_state)

    # self: Pointer reaction owner, preserving explicitly selected Affection.
    # preserve_fade: Keep the current visible heart for a smooth semantic Sleep handoff.
    def cancel_transient_affection(self, preserve_fade=False):
        self.petting.cancel()
        self.affection_remaining_us = 0
        self.eye_reaction_remaining_us = 0
        painter = self.renderer.personality_painter
        if (not preserve_fade and self.eye_reaction != PersonalityCue.NONE and painter is not None and
                painter.channel.cue == self.eye_reaction):
            painter.disable(self.elapsed_ms)
        self.eye_reaction = PersonalityCue.NONE
        if not preserve_fade:
            if self.affection_owns_channel and painter is not None and painter.channel.cue == PersonalityCue.AFFECTION:
                painter.disable(self.elapsed_ms)
            self.affection_owns_channel = False

    # self: Synthetic preview owner selecting urgent alerts or a finishing interaction.
    def effective_personality_cue(self):
        selected = PersonalityCue[self.personality_cue.get()]
        desired, _ = self.renderer.select_personality(selected, self.personality_sleep,
            self.affection_remaining_us > 0, self.eye_reaction)
        return desired

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
        self.personality_sleep = screen == Screen.RIG and self.face_preset_name in ("sleep", "sleeping")
        if screen == Screen.GREETING:
            face_targets = self.renderer.scene_painter.face("greeting")
            targets = contour_defaults(self.renderer.rig)
            if self.previous_animation_screen != screen:
                self.face_state = list(face_targets)
        elif self.demo_enabled.get() and screen == Screen.RIG:
            phase_id, face_targets, targets = demo_targets(self.runtime_settings, self.renderer.rig, self.elapsed_ms)
            phase = next(step for step in self.runtime_settings["demo"]["steps"] if step["id"] == phase_id)
            self.personality_sleep = phase["face"] in ("sleep", "sleeping")
        self.face_state = self.renderer.advance_face(self.face_state, face_targets, active_ms)
        self.previous_animation_screen = screen
        values = self.face_state
        if self.playing.get() and screen not in ANIMATED_SCREENS:
            values = self.renderer.animate_face(self.face_state, self.elapsed_ms)
        # The single response composer adds effects before the contour transition.
        return values, targets

    # self: Presentation owner retaining Tk image storage across unchanged viewports.
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
        # Reuse Tk's image storage while the viewport size stays unchanged.
        new_photo = (self.photo is None or self.photo.width() != layout.image_size or
                     self.photo.height() != layout.image_size)
        if new_photo:
            self.photo = ImageTk.PhotoImage(display_image)
            self.canvas.itemconfigure(self.image_item, image=self.photo)
        else:
            self.photo.paste(display_image)
        if layout != self.layout:
            self.label_text_cache.clear()
            self.canvas.coords(self.image_item, layout.left, layout.top)

        self.layout = layout
        self.display_rotation = rotation
        self.display_screen = screen
        if self.names.get() or self.label_items:
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
        if self.contour_playing.get() and screen in ANIMATED_SCREENS:
            status = "RUNNING"
        phase = self.contour_state["phase"]
        blend_seconds = self.renderer.rig["contour"]["transition_seconds"]
        self.contour_info.configure(
            text=f"{status} / phase {phase:.2f} rad\nBlend: {blend_seconds:.2f} s"
        )

    # self: Inspector owner showing coordinates from the drawn frame, not a pending pose.
    # Raises ValueError for unsupported rotation, or tk.TclError on a lost inspector.
    def update_point_inspector(self):
        if self.selected is None or self.scene is None:
            self.info.configure(
                text="Enable Rig and select a point or its name.\n"
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

    # self: Preview owner comparing displayed state without advancing it.
    # screen: Current typed route; boot images depend only on viewport and rotation.
    # rotation: Cardinal orientation shared by the displayed image and touch.
    # layout: Complete desktop viewport including label margins.
    # values: Composed facial state; comparison does not alter the simulation.
    # transform: Composed position and scale in logical pixels.
    # Raises tk.TclError if a presentation control has become unavailable.
    # Raises native runtime errors if the shared boot animation cannot initialize or select its frame.
    def presentation_key(self, screen, rotation, layout, values, transform):
        common = (screen, rotation, layout)
        if screen == Screen.BOOT_VIBEWARE:
            if self.renderer.branding is None:
                self.renderer.branding = VibeWareBoot(self.renderer.root, cody_companion=True)
            return common + (self.renderer.branding.frame_index(self.elapsed_ms),)
        if screen not in ANIMATED_SCREENS:
            return common
        # Subpixel comparison only: leave the model's full-precision values alone.
        contour = tuple((name, round(value, 4)) for name, value in self.contour_state.items())
        overlay = None
        if self.motion_overlay.get() and self.response_frame is not None:
            frame = self.response_frame
            overlay = (frame.resting_root, frame.current_root)
        return common + (
            tuple(round(value, 4) for value in values),
            tuple(round(value, 3) for value in transform),
            contour, self.debug.get(), self.selected, self.guide.get(),
            self.outline.get(), self.rest_outline.get(), self.names.get(),
            self.label_scope.get(), self.label_page.get(), self.entity_name, overlay,
            self.face_follows_body.get(), self.renderer.navigation_opacity, self.playing.get(),
            self.effective_personality_cue(),
            self.personality_sleep,
            round(self.elapsed_ms, 1) if screen == Screen.RIG and
            (self.effective_personality_cue() != PersonalityCue.NONE or self.personality_sleep or
             self.renderer.personality_painter is not None and self.renderer.personality_painter.active) else None,
            self.renderer.scene_painter.visible_characters("greeting", self.scene_elapsed_ms)
            if screen == Screen.GREETING else None,
        )

    # self: Preview controller advancing sources, model, geometry and presentation once.
    # Optional PC audio errors are retained without stopping visual/touch updates.
    # Raises ValueError or tk.TclError; tick exposes the original error and stops.
    def render_tick(self):
        started = time.perf_counter()
        if self.ccp_panel is not None:
            if self.ccp_panel.follow.get():
                self.render_ccp_tick(started)
                return
            self.ccp_panel.refresh()
        self.advance_happiness_clock(int(started * 1_000_000))
        self.navigation_visibility.update(int(started * 1_000_000))
        self.renderer.navigation_opacity = self.navigation_visibility.opacity(int(started * 1_000_000))
        self.renderer.navigation_paused = not self.playing.get()
        interval_seconds = started - self.last_tick
        self.last_tick = started
        screen = self.screen.get()
        rotation = self.rotation.get()
        show_names = self.names.get() and screen == Screen.RIG
        layout = PreviewLayout.fit(*self.pending_canvas_size, show_names)
        active_ms = interval_seconds * 1000 if screen in TIMED_SCREENS else 0
        if screen == Screen.GREETING:
            if self.previous_animation_screen != screen:
                self.scene_elapsed_ms = 0.0
            elif self.playing.get():
                self.scene_elapsed_ms += active_ms
        if self.playing.get():
            self.elapsed_ms += active_ms

        values, targets = self.advance_animation(screen, active_ms)
        petting_context = (screen, rotation)
        selected_cue = PersonalityCue[self.personality_cue.get()]
        if selected_cue != self.petting_selected_cue:
            if selected_cue in (PersonalityCue.NONE, PersonalityCue.AFFECTION):
                self.affection_remaining_us = 0
                self.affection_owns_channel = False
            else:
                _, release_affection = self.renderer.select_personality(
                    selected_cue, self.personality_sleep, self.affection_remaining_us > 0)
                if release_affection:
                    self.affection_remaining_us = 0
        self.petting_selected_cue = selected_cue
        if (self.petting_context != petting_context or screen != Screen.RIG or
                not self.playing.get() or self.personality_sleep):
            preserve_fade = (self.petting_context == petting_context and screen == Screen.RIG and
                             self.playing.get() and self.personality_sleep)
            self.cancel_transient_affection(preserve_fade)
        else:
            self.affection_remaining_us = max(0, self.affection_remaining_us - int(active_ms * 1000))
            self.eye_reaction_remaining_us = max(0, self.eye_reaction_remaining_us - int(active_ms * 1000))
            if self.eye_reaction_remaining_us == 0:
                self.eye_reaction = PersonalityCue.NONE
        self.petting_context = petting_context
        desired, release_affection = self.renderer.select_personality(selected_cue, self.personality_sleep,
            self.affection_remaining_us > 0, self.eye_reaction)
        if release_affection:
            self.affection_remaining_us = 0
        if self.eye_reaction != PersonalityCue.NONE and desired != self.eye_reaction:
            self.eye_reaction = PersonalityCue.NONE
            self.eye_reaction_remaining_us = 0
        transform = [value.get() for value in self.transform]
        if screen == Screen.GREETING:
            cloud = self.renderer.scene_painter.definition("greeting")["cloud"]
            transform = [cloud[key] for key in ("x", "y", "scale")]
        inputs = self.observe_stimuli(int(started * 1_000_000), rotation)
        inputs = replace(inputs, idle_scene_allowed=(screen == Screen.RIG and self.playing.get() and
            not self.debug.get() and self.face_preset_name in (None, "calm", "happy", "drowsy", "curious") and
            not self.personality_sleep and selected_cue == PersonalityCue.NONE and
            self.affection_remaining_us == 0 and self.eye_reaction_remaining_us == 0))
        self.response_frame = None
        if screen in ANIMATED_SCREENS:
            if self.display_screen not in ANIMATED_SCREENS:
                self.response_state = self.renderer.rebase_model(self.response_state)
            self.response_config["motion"]["sensitivity"] = self.motion_sensitivity.get()
            validate_response_config(self.response_config)
            previous_response = self.response_state
            self.response_state = self.renderer.advance_model(self.response_config, self.response_state, inputs,
                                                   active_ms, int(started * 1_000_000), self.playing.get())
            if (screen == Screen.RIG and not self.debug.get() and self.playing.get() and
                    not self.personality_sleep and self.response_state.consumed_eye_tap_at_us >
                    previous_response.consumed_eye_tap_at_us):
                stressed = (self.face_preset_name == "stressed" or previous_response.stress >=
                            self.response_config["idle"]["activity_stress_threshold"])
                reaction = PersonalityCue.FRUSTRATION if randbelow(4) == 0 else PersonalityCue.FEAR
                if stressed:
                    reaction = PersonalityCue.FRUSTRATION if reaction == PersonalityCue.FEAR else PersonalityCue.FEAR
                if reaction == self.last_eye_reaction:
                    reaction = PersonalityCue.FRUSTRATION if reaction == PersonalityCue.FEAR else PersonalityCue.FEAR
                self.eye_reaction = self.last_eye_reaction = reaction
                self.eye_reaction_remaining_us = 4_000_000
                _, release_affection = self.renderer.select_personality(selected_cue, False,
                    self.affection_remaining_us > 0, reaction)
                if release_affection:
                    self.affection_remaining_us = 0
                desired, _ = self.renderer.select_personality(selected_cue, False, False, reaction)
                if desired != reaction:
                    self.eye_reaction = PersonalityCue.NONE
                    self.eye_reaction_remaining_us = 0
            self.response_frame = self.renderer.compose_model(self.response_state, values, targets,
                                                   transform, rotation, self.playing.get())
            values = self.response_frame.face
            targets = self.response_frame.contour
            transform = self.response_frame.transform
            if self.contour_playing.get():
                self.contour_state = self.renderer.advance_shape(self.contour_state, targets, active_ms)
        presentation_key = self.presentation_key(screen, rotation, layout, values, transform)
        changed = presentation_key != self.last_presentation_key
        if changed:
            self.renderer.face_follows_body = self.face_follows_body.get()
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
                scene_elapsed_ms=self.scene_elapsed_ms,
                personality_cue=self.effective_personality_cue(),
                personality_elapsed_ms=self.elapsed_ms,
                personality_nominal_scale=self.renderer.scene_painter.definition("greeting")["cloud"]["scale"]
                if screen == Screen.GREETING else self.transform[2].get(),
                personality_sleep=self.personality_sleep,
                personality_paused=not self.playing.get(),
            )
            self.present_frame(image, layout, screen, rotation)
            if self.last_presentation_key is None:
                print("[cody-preview] First frame presented; animation callback active.", flush=True)
            self.present_audio(screen)
            self.completed_preview_frames += 1
            # Commit only after Tk accepted the image and its matching hit targets.
            self.last_presentation_key = presentation_key
        self.refresh_inspector_readouts(inputs, targets, screen, started)

        finished = time.perf_counter()
        duration_ms = (finished - started) * 1000
        target_hz = self.preview_hz.get()
        personality_status = "HIDDEN"
        if self.renderer.personality_painter is not None:
            frame = self.renderer.personality_painter.last_frame
            personality_status = f"{frame.status.name} / {frame.cue.name} / alpha {frame.opacity}"
        if started >= self.next_status_at:
            self.next_status_at = started + .2
            elapsed = finished - self.status_sampled_at
            observed_fps = self.completed_preview_frames / elapsed if elapsed > 0 else 0
            self.completed_preview_frames = 0
            self.status_sampled_at = finished
            self.timing.configure(
                text=f"SYNTHETIC / symbol {personality_status} / "
                     f"happiness {self.happiness_state.happiness} / sadness {self.happiness_sadness()}\n"
                     f"Host presented {observed_fps:.1f} fps / tick {duration_ms:.1f} ms / interval {interval_seconds * 1000:.1f} ms / "
                     f"target {target_hz} Hz / frame {'UPDATED' if changed else 'UNCHANGED'} / ESP32 UNMEASURED\n"
                     f"PC fake audio: {repr(self.audio.last_error) if self.audio.last_error is not None else 'READY'}"
                     + (f"\nLINK ERROR: {self.last_link_error!r}" if self.last_link_error is not None else "")
            )
        # Boot artwork has no animation. Keep controls responsive without 60 full images/s.
        interval_ms = 1000 / target_hz if screen in TIMED_SCREENS else 100
        delay_ms = max(1, math.ceil(interval_ms - duration_ms))
        self.after_id = self.root.after(delay_ms, self.tick)

    # self: Studio owner presenting its isolated authenticated CCP state.
    # started: Current host clock; freezes the independent rig clock while CCP owns the view.
    # Raises ValueError or tk.TclError for rejected content or failed presentation.
    def render_ccp_tick(self, started):
        self.last_tick = started
        self.happiness_sampled_us = int(started * 1_000_000)
        self.body_drag_origin = None
        self.petting.cancel()
        self.remove_unused_labels(set())
        rotation = self.rotation.get()
        layout = PreviewLayout.fit(*self.pending_canvas_size, False)
        image, self.scene, key = self.ccp_panel.render(rotation)
        key += (layout,)
        if key != self.last_presentation_key:
            # Match the common preview's circular mask and cardinal presentation.
            mask = Image.new("L", (SIZE, SIZE), 0)
            ImageDraw.Draw(mask).ellipse((0, 0, SIZE - 1, SIZE - 1), fill=255)
            image.putalpha(mask)
            self.present_frame(orient(image, rotation), layout, Screen.GREETING, rotation)
            self.last_presentation_key = key
            # Optional SFX follow the exact native cue only after its frame was presented.
            self.present_audio(Screen.GREETING, personality_only=True)
        self.timing.configure(text="SIMULATED CCP1 / loopback only / real authenticated packets / ESP32 UNMEASURED")
        self.after_id = self.root.after(100, self.tick)


# argv: Explicit Cody argument list from the common Windows launcher.
# Raises ValueError for an invalid authored rig or evaluated scene.
# Raises OSError for a failed capture write and tk.TclError for unavailable window resources.
# Raises AttributeError or RuntimeError if required Windows DPI setup cannot be established.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skin", default="assets/cody/cody.yaml")
    parser.add_argument('--screen',choices=SCREEN_NAMES,default='rig')
    parser.add_argument('--rotation',type=int,choices=(0,90,180,270),default=0)
    parser.add_argument('--capture',type=Path,
                        help = 'Output PNG; relative names use artifacts/simulator, explicit paths must stay under artifacts')
    parser.add_argument('--safe-guide',action='store_true')
    parser.add_argument('--ccp', action='store_true',
                        help='Opt in to the isolated SIMULATED CCP1 loopback endpoint and visible pairing panel')
    parser.add_argument('--cloud-material-study', action='store_true',
                        help='Capture a host-only soft cloud material study; not firmware rendering parity')
    parser.add_argument('--cloud-png-mesh-study', action='store_true',
                        help='Capture the painted PNG/triangle appearance study; GIF output shows synthetic lobe motion')
    parser.add_argument('--cloud-jelly-study', action='store_true',
                        help='Pearlescent scanline appearance study: interactive preview, PNG or synthetic GIF')
    parser.add_argument('--mesh-overlay', action='store_true',
                        help='Overlay the actual triangle mesh in a PNG mesh study capture')
    parser.add_argument('--at-ms',type=int,default=1000)
    args = parser.parse_args(argv)
    if args.ccp and (args.capture is not None or args.screen != Screen.RIG or
                    args.cloud_material_study or args.cloud_png_mesh_study or args.cloud_jelly_study):
        parser.error('--ccp requires interactive --screen rig without a rendering study')
    if args.at_ms<0:
        parser.error('--at-ms must be non-negative')
    if args.cloud_material_study and (args.capture is None or args.screen != Screen.RIG):
        parser.error('--cloud-material-study requires --capture and --screen rig')
    if args.cloud_png_mesh_study and (args.capture is None or args.screen != Screen.RIG):
        parser.error('--cloud-png-mesh-study requires --capture and --screen rig')
    if args.cloud_jelly_study and args.screen not in ANIMATED_SCREENS:
        parser.error('--cloud-jelly-study requires an animated screen: rig or greeting')
    if sum((args.cloud_material_study, args.cloud_png_mesh_study, args.cloud_jelly_study)) > 1:
        parser.error('Select only one cloud study')
    if args.mesh_overlay and not args.cloud_png_mesh_study:
        parser.error('--mesh-overlay requires --cloud-png-mesh-study')
    sprite_study = args.cloud_png_mesh_study or args.cloud_jelly_study
    if args.capture is not None and sprite_study and args.capture.suffix.lower() not in ('.png', '.gif'):
        parser.error('Sprite studies capture PNG stills or a GIF synthetic motion loop')
    if args.capture is not None and args.capture.suffix.lower() == '.gif':
        if args.screen != Screen.RIG:
            parser.error('The fixed GIF motion timeline requires --screen rig; use PNG or interactive greeting')
        if not sprite_study:
            parser.error('GIF motion capture requires --cloud-png-mesh-study or --cloud-jelly-study')
        if args.safe_guide or '--at-ms' in (sys.argv[1:] if argv is None else argv):
            parser.error('GIF motion uses its complete fixed timeline; --safe-guide and --at-ms are not supported')
    if args.capture is not None:
        try:
            args.capture = resolve_runtime_output(args.capture)
        except (OSError, RuntimeError, ValueError) as error:
            parser.error(str(error))
    renderer = CodyRenderer(skin_path=args.skin, jelly_study=args.cloud_jelly_study,
        offline_study=args.cloud_material_study or args.cloud_png_mesh_study or args.cloud_jelly_study)
    if args.capture:
        destination = args.capture
        if sprite_study and destination.suffix.lower() == '.gif':
            renderer.capture_sprite_motion(destination, args.rotation, args.mesh_overlay)
            print(destination)
            return
        face = renderer.scene_painter.face("greeting") if args.screen == Screen.GREETING else renderer.rig['boot_channels']['ready']
        values = face if args.screen not in ANIMATED_SCREENS or args.screen == Screen.GREETING else renderer.animate_face(face,args.at_ms)
        contour = (renderer.advance_shape(make_contour_state(renderer.rig),contour_defaults(renderer.rig),args.at_ms)
                   if args.screen in ANIMATED_SCREENS else None)
        # Captures show the same clean artwork as the device's initial view.
        # Interactive inspection still exposes its separate debug controls.
        placement = renderer.scene_painter.definition("greeting")["cloud"] if args.screen == Screen.GREETING else renderer.rig['default_transform']
        image,_ = renderer.render(args.screen,values,tuple(placement[key] for key in ('x','y','scale')),
                                debug=False,guide=args.safe_guide,rotation=args.rotation,contour=contour,
                                material_study=args.cloud_material_study,
                                png_mesh_study=args.cloud_png_mesh_study, mesh_overlay=args.mesh_overlay,
                                scene_elapsed_ms=args.at_ms, personality_elapsed_ms=args.at_ms)
        destination.parent.mkdir(parents=True,exist_ok=True)
        renderer.capture_sheet(image,f"{args.screen} / {args.rotation} deg / HOST PREVIEW").save(destination)
        print(destination)
        if renderer.png_mesh is not None:
            study = renderer.png_mesh
            print(f'HOST ONLY: {len(study.vertices)} vertices, {len(study.triangles)} triangles; '
                  f'texture={study.texture.size}, source crop={study.source_crop}; ESP32 unmeasured')
        if renderer.jelly_sprite is not None:
            study = renderer.jelly_sprite
            print(f'HOST ONLY: {len(study.rows)} scanline supports, no triangles; '
                  f'texture={study.texture.size}, source crop={study.source_crop}; ESP32 unmeasured')
        return
    prepare_display()
    root = tk.Tk()
    root.withdraw()
    configure_tk_scaling(root)
    CodyWindow(root,renderer,args)
    root.deiconify()
    root.mainloop()


if __name__=='__main__':
    main()
