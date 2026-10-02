"""Local Cody rig preview in the existing Windows simulator toolchain. No network/device I/O."""

from __future__ import annotations

import argparse
import math
import sys
import time
import tkinter as tk
from pathlib import Path
from tkinter import ttk

from PIL import Image, ImageDraw, ImageFont, ImageTk

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from cody_rig_contract import animate, evaluate, load_contract  # noqa: E402

SCREEN_NAMES = ("rig", "boot-sleep", "boot-waking", "boot-ready")
BOOT_NAMES = {"boot-sleep": "cody-sleep-blue.png", "boot-waking": "cody-waking-blue.png", "boot-ready": "cody-ready-blue.png"}
SIZE = 466


# value is a native RGB565 word, expanded without inventing extra indexed colors.
def rgb565(value):
    return ((value >> 11 & 31) * 255 // 31, (value >> 5 & 63) * 255 // 63, (value & 31) * 255 // 31)


# x and y are physical preview pixels; rotation is clockwise, matching the logical canvas.
# Raises ValueError for a non-cardinal rotation.
def logical_touch(x, y, rotation):
    if rotation == 0: return x, y
    if rotation == 90: return y, SIZE - 1 - x
    if rotation == 180: return SIZE - 1 - x, SIZE - 1 - y
    if rotation == 270: return SIZE - 1 - y, x
    raise ValueError(f"Unsupported rotation: {rotation}")


# image is the logical canvas; rotation is a clockwise cardinal orientation.
# Raises ValueError for a non-cardinal rotation.
def orient(image, rotation):
    modes = {90: Image.Transpose.ROTATE_270, 180: Image.Transpose.ROTATE_180, 270: Image.Transpose.ROTATE_90}
    if rotation == 0: return image
    if rotation not in modes: raise ValueError(f"Unsupported rotation: {rotation}")
    return image.transpose(modes[rotation])


class CodyRenderer:
    """Authoring tables are immutable for this preview session; evaluated scenes are transient."""

    # root identifies the active Cody checkout; no external assets are resolved.
    def __init__(self, root=ROOT):
        self.rig, self.palette = load_contract(root)
        self.rgb_palette = [rgb565(int(entry["rgb565"], 16)) for entry in self.palette["entries"]]
        self.flat_palette = [channel for color in self.rgb_palette for channel in color]
        self.boot = {}
        for screen, name in BOOT_NAMES.items():
            with Image.open(root / "assets/cody/boot" / name) as source:
                # Exact source artwork is previewed; firmware's indexed RLE conversion is separate.
                self.boot[screen] = source.convert("RGB").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        self.font = ImageFont.load_default()

    # color is the initial palette slot of a fresh full logical canvas.
    def indexed(self, color=0):
        image = Image.new("P", (SIZE, SIZE), color)
        image.putpalette(self.flat_palette)
        return image

    # image receives a gradient polygon; vertices are the already evaluated curve samples.
    # slots identifies the ordered inclusive palette ramp.
    def fill(self, image, vertices, slots):
        first, last = slots
        if first == last:
            ImageDraw.Draw(image).polygon(vertices, fill=first)
            return
        mask = Image.new("L", (SIZE, SIZE))
        ImageDraw.Draw(mask).polygon(vertices, fill=255)
        top = min(y for _, y in vertices)
        bottom = max(y for _, y in vertices)
        ramp = self.indexed()
        draw = ImageDraw.Draw(ramp)
        for row in range(max(0, math.floor(top)), min(SIZE, math.ceil(bottom))):
            # This is edge coverage clipping; channel inputs themselves are never clamped.
            ratio = max(0, min(1, (row + .5 - top) / (bottom - top))) if bottom > top else 0
            slot = first + math.floor(ratio * (last - first) + .5)
            draw.line((0, row, SIZE - 1, row), fill=slot)
        image.paste(ramp, (0, 0), mask)

    # scene is evaluated once; debug and selected affect only its visual overlay.
    # guide shows the canonical safe square without changing geometry or hit testing.
    def draw_scene(self, scene, debug=True, selected=None, guide=False):
        image = self.indexed()
        for path in scene["paths"]:
            if not path["visible"]: continue
            definition = path["definition"]
            if definition["stroke"]:
                width = max(1, round(definition["stroke"] * scene["scale"]))
                draw = ImageDraw.Draw(image)
                draw.line(path["vertices"], fill=definition["paint"][0], width=width, joint="curve")
                radius = width / 2
                for x, y in (path["vertices"][0], path["vertices"][-1]):
                    draw.ellipse((x-radius, y-radius, x+radius, y+radius), fill=definition["paint"][0])
            else:
                self.fill(image, path["vertices"], definition["paint"])
        draw = ImageDraw.Draw(image)
        colors = self.rig["style"]["debug"]
        if debug:
            for point in self.rig["points"]:
                if point["role"] == "anchor" and point["parent"] is not None:
                    draw.line([scene["world"][point["parent"]], scene["world"][point["name"]]], fill=colors["link"], width=1)
            for curve in self.rig["curves"]:
                controls = [scene["world"][name] for name in curve["points"]]
                draw.line(controls[:2], fill=colors["handle"], width=1)
                draw.line(controls[2:], fill=colors["handle"], width=1)
            for point in self.rig["points"]:
                x, y = scene["world"][point["name"]]
                radius = 4 if point["name"] == selected else 1 if point["role"] == "handle" else 2
                slot = colors["selected"] if point["name"] == selected else colors[point["role"]]
                draw.ellipse((x-radius, y-radius, x+radius, y+radius), fill=slot)
        # Navigation uses the same exact logical circle for rendering and input.
        back = self.rig["style"]["back"]
        first, last = back["fill"]
        for row in range(203, 263):
            dy = row + .5 - 233
            half = math.sqrt(900 - dy*dy)
            slot = first + int((dy+30) * (last-first) / 60)
            for center in (40,426):
                draw.line((math.ceil(center-half), row, math.floor(center+half), row), fill=slot)
        draw.line([(44,222),(33,233),(44,244)], fill=back["ink"], width=3)
        nodes=[(416,242),(426,222),(436,242)]
        draw.line(nodes+[nodes[0]],fill=back['ink'],width=1)
        for x,y in nodes:
            draw.ellipse((x-2,y-2,x+2,y+2),fill=colors['selected'] if debug else back['ink'])
        if guide:
            draw.rectangle((73,73,392,392), outline=colors["selected"])
        return image

    # screen selects rig or an existing boot artwork; values and transform control the rig.
    # debug, selected and guide only affect the debug overlay; rotation transforms the whole result.
    def render(self, screen, values, transform, debug=True, selected=None, guide=False, rotation=0):
        if screen not in SCREEN_NAMES: raise ValueError(f"Unknown Cody screen: {screen}")
        scene = evaluate(self.rig, values, transform)
        image = self.draw_scene(scene, debug, selected, guide) if screen == "rig" else self.boot[screen].copy()
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
    # root is the Tk owner; renderer owns immutable tables; args selects the initial preview.
    def __init__(self, root, renderer, args):
        self.root, self.renderer = root, renderer
        self.root.title(f"{renderer.rig['character']['name']} / Rig PoC / SYNTHETIC")
        self.screen = tk.StringVar(value=args.screen)
        self.rotation = tk.IntVar(value=args.rotation)
        self.debug = tk.BooleanVar(value=True)
        self.guide = tk.BooleanVar(value=args.safe_guide)
        self.playing = tk.BooleanVar(value=True)
        self.elapsed_ms = 0.0
        self.last_tick = time.perf_counter()
        self.selected = None
        self.scene = None
        self.after_id = None
        self.canvas = tk.Canvas(root, width=SIZE, height=SIZE, highlightthickness=0, bg="black")
        self.canvas.grid(row=0,column=0,sticky="n")
        self.canvas.bind("<Button-1>", self.touch)
        self.image_item = self.canvas.create_image(0,0,anchor="nw")
        controls = ttk.Frame(root,padding=12)
        controls.grid(row=0,column=1,sticky="n")
        font = renderer.rig["style"]["host_font"]
        ttk.Label(controls,text="CODY / LOCAL RIG",font=(font["family"],12,"bold")).pack(anchor="w")
        ttk.Label(controls,text="SYNTHETIC INPUT / no sensor or PC link").pack(anchor="w")
        ttk.Combobox(controls,textvariable=self.screen,values=SCREEN_NAMES,state="readonly",width=27).pack(fill="x",pady=6)
        ttk.Combobox(controls,textvariable=self.rotation,values=(0,90,180,270),state="readonly").pack(fill="x")
        ttk.Checkbutton(controls,text="Points, links and curve handles",variable=self.debug).pack(anchor="w")
        ttk.Checkbutton(controls,text="320 x 320 safe square",variable=self.guide).pack(anchor="w")
        ttk.Checkbutton(controls,text="Animate breath and blink",variable=self.playing).pack(anchor="w")
        phase_row=ttk.Frame(controls); phase_row.pack(fill="x",pady=6)
        for phase in ("sleep","waking","ready"):
            ttk.Button(phase_row,text=phase.title(),command=lambda value=phase:self.preset(value)).pack(side="left")
        self.values=[]
        for channel in renderer.rig["channels"]:
            value=tk.DoubleVar(value=channel["default"])
            self.values.append(value)
            ttk.Label(controls,text=channel["name"]).pack(anchor="w")
            ttk.Scale(controls,from_=channel["minimum"],to=channel["maximum"],variable=value,length=280).pack(fill="x")
        self.transform=[]
        for key,minimum,maximum in (("x",80,386),("y",100,360),("scale",60,180)):
            value=tk.DoubleVar(value=renderer.rig['default_transform'][key]); self.transform.append(value)
            ttk.Label(controls,text=key).pack(anchor="w")
            ttk.Scale(controls,from_=minimum,to=maximum,variable=value).pack(fill="x")
        labels=[f"{entity['label']} [{name}]" for name,entity in renderer.rig['entities'].items()]
        self.entity_picker=ttk.Combobox(controls,values=labels,state="readonly")
        self.entity_picker.pack(fill="x",pady=6)
        self.entity_picker.bind("<<ComboboxSelected>>",self.select_entity)
        self.info=ttk.Label(controls,text="Select a point to inspect its stable name.",width=46,font=(font["family"],font["size"]))
        self.info.pack(anchor="w",pady=6)
        self.timing=ttk.Label(root,text="SYNTHETIC / host timings only")
        self.timing.grid(row=1,column=0,columnspan=2,sticky="w",padx=8,pady=8)
        self.root.bind("<Escape>",self.back)
        self.root.protocol("WM_DELETE_WINDOW",self.close)
        self.tick()

    # No parameters: cancel the scheduled callback before destroying the interactive preview.
    def close(self):
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.root.destroy()

    # phase selects independent channel values for the rig; it never simulates device boot success.
    def preset(self,phase):
        self.screen.set("rig")
        for target,value in zip(self.values,self.renderer.rig['boot_channels'][phase]): target.set(value)

    # event identifies the selected entity from the YAML catalog.
    def select_entity(self,event):
        index=self.entity_picker.current()
        if index>=0:
            self.selected=list(self.renderer.rig['entities'].values())[index]['anchor']

    # event is optional keyboard input; Back has the same one-step route as the device PoC.
    def back(self,event=None):
        if self.screen.get()=="rig": self.screen.set("boot-ready")

    # event supplies physical window coordinates, inverse-mapped once to logical pixels.
    def touch(self,event):
        x,y=logical_touch(event.x,event.y,self.rotation.get())
        if (x-233)**2+(y-233)**2>233**2: return
        if self.screen.get()=="boot-ready" and 73<=x<393 and 73<=y<393:
            self.screen.set("rig"); return
        if self.screen.get()!="rig": return
        if (x-40)**2+(y-233)**2<=900:
            self.back(); return
        if (x-426)**2+(y-233)**2<=900:
            self.debug.set(not self.debug.get()); self.selected=None; return
        if not self.debug.get(): return
        if self.scene is None: return
        nearest=min(self.scene['world'],key=lambda name:(x-self.scene['world'][name][0])**2+(y-self.scene['world'][name][1])**2)
        px,py=self.scene['world'][nearest]
        self.selected=nearest if (x-px)**2+(y-py)**2<=144 else None

    # No parameters: visible terminal errors stop the preview rather than retaining a false healthy state.
    def tick(self):
        try:
            self.render_tick()
        except Exception as error:
            self.playing.set(False)
            self.timing.configure(text=f"PREVIEW STOPPED / {type(error).__name__}: {error}")
            self.info.configure(text="Rendering failed. See console for the original error.")
            self.root.report_callback_exception(type(error), error, error.__traceback__)

    # No parameters: elapsed-time animation skips overdue frames and keeps Tk input responsive.
    def render_tick(self):
        started=time.perf_counter()
        interval=started-self.last_tick
        self.last_tick=started
        if self.playing.get() and self.screen.get()=="rig": self.elapsed_ms+=interval*1000
        base=[value.get() for value in self.values]
        values=animate(base,self.elapsed_ms) if self.playing.get() else base
        image,self.scene=self.renderer.render(self.screen.get(),values,[v.get() for v in self.transform],self.debug.get(),self.selected,self.guide.get(),self.rotation.get())
        self.photo=ImageTk.PhotoImage(image)
        self.canvas.itemconfigure(self.image_item,image=self.photo)
        if self.selected:
            local=self.scene['local'][self.selected]; world=self.scene['world'][self.selected]
            self.info.configure(text=f"{self.selected}\nlocal  {local[0]:+.4f}, {local[1]:+.4f}\ncanvas {world[0]:.1f}, {world[1]:.1f}")
        else: self.info.configure(text="Select a point or an entity.\nWhite: anchors / blue: knots\nAmber: cubic handles")
        duration=(time.perf_counter()-started)*1000
        self.timing.configure(text=f"SYNTHETIC / host frame {duration:.1f} ms / interval {interval*1000:.1f} ms / target 60 Hz / ESP32: UNMEASURED")
        self.after_id = self.root.after(max(1,math.ceil(1000/60-duration)),self.tick)


# argv is the explicit Cody argument list from the common Windows launcher.
def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--screen',choices=SCREEN_NAMES,default='rig')
    parser.add_argument('--rotation',type=int,choices=(0,90,180,270),default=0)
    parser.add_argument('--capture',type=Path)
    parser.add_argument('--safe-guide',action='store_true')
    parser.add_argument('--at-ms',type=int,default=1000)
    args=parser.parse_args(argv)
    if args.at_ms<0: parser.error('--at-ms must be non-negative')
    renderer=CodyRenderer()
    if args.capture:
        destination=args.capture.resolve()
        artifacts=(ROOT/'artifacts').resolve()
        if not destination.is_relative_to(artifacts): parser.error('Cody captures must be under this checkout/artifacts')
        values=animate(renderer.rig['boot_channels']['ready'],args.at_ms)
        image,_=renderer.render(args.screen,values,tuple(renderer.rig['default_transform'][key] for key in ('x','y','scale')),guide=args.safe_guide,rotation=args.rotation)
        destination.parent.mkdir(parents=True,exist_ok=True)
        renderer.capture_sheet(image,f"{args.screen} / {args.rotation} deg / HOST PREVIEW").save(destination)
        print(destination)
        return
    root=tk.Tk()
    CodyWindow(root,renderer,args)
    root.mainloop()


if __name__=='__main__':
    main()
