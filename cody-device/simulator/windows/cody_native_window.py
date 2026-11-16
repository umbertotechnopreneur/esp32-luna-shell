"""Host window for retired native Cody frames and explicit simulated input.

The native device runtime owns every display pixel. This window only transports
completed frames and input; it never constructs a device screen in Python.
"""

import time
import subprocess
import sys
import tkinter as tk

from PIL import Image, ImageTk

from cody_native_capture import capture_completed_frame
from cody_native_display import NativeDisplayError
from cody_native_snapshot_editor import SnapshotEditor
from cody_native_runtime import ROOT


# No parameters: create an isolated controlled session inside the running product.
# The public launcher holds the command lock; internal child orchestration uses
# its already-selected implementation and never builds or switches renderer.
# Raises OSError if the native Studio child cannot be started.
def open_controlled_studio():
    subprocess.Popen([sys.executable, '-B', str(ROOT / 'cody-device/simulator/windows/cody_simulator.py'),
                      '--studio-case', 'P04'], cwd=ROOT)


# device: Started native device owner with frame(), touch() and boot() methods.
# studio_scenario: Optional versioned controlled scenario prepared by the shared runner.
# Exception: Preserves native rendering/input failures; no alternate renderer is used.
def run_native_window(device, studio_scenario=None):
    if studio_scenario is not None:
        from cody_studio_window import StudioWindow
        root = tk.Tk()
        StudioWindow(root, device, studio_scenario)
        root.mainloop()
        return
    root = tk.Tk()
    root.title("Cody Studio — SIMULATED")
    menu = tk.Menu(root)
    simulator_menu = tk.Menu(menu, tearoff=False)
    simulator_menu.add_command(label="Open controlled P04 Studio…", command=open_controlled_studio)
    simulator_menu.add_command(label="Edit simulated snapshot…", command=lambda: SnapshotEditor(root, device))
    menu.add_cascade(label="Simulator", menu=simulator_menu)
    display_menu = tk.Menu(menu, tearoff=False)
    display_mode = tk.StringVar(value="fit")
    display_menu.add_radiobutton(label="Keep proportions", variable=display_mode, value="fit")
    display_menu.add_radiobutton(label="Fill window (stretch)", variable=display_mode, value="stretch")
    menu.add_cascade(label="Display", menu=display_menu)
    root.configure(menu=menu)
    canvas = tk.Canvas(root, width=490, height=490, highlightthickness=0, background="black")
    canvas.pack(fill="both", expand=True)
    state = {"photo": None, "item": None, "layout": None, "pressed": False,
             "last_touch": None, "revision": None, "deadline": time.monotonic() + 15.0}

    # event: Tk pointer position on the host canvas.
    # Returns a device coordinate only when the input falls inside the native frame.
    def point(event):
        layout = state["layout"]
        if layout is None:
            return None
        left, top, width, height = layout
        if not (left <= event.x < left + width and top <= event.y < top + height):
            return None
        # Match nearest-neighbour sampling at host-pixel centres on each axis.
        return (((2 * (event.x - left) + 1) * 466) // (2 * width),
                ((2 * (event.y - top) + 1) * 466) // (2 * height))

    # event: Host pointer press or movement; only admitted coordinates reach Cody.
    def pressed(event):
        position = point(event)
        if position is None:
            return
        device.touch(True, *position)
        state["pressed"] = True
        state["last_touch"] = position

    # event: Host pointer release, paired with the last admitted native contact.
    def released(event):
        if not state["pressed"]:
            return
        position = point(event) or state["last_touch"]
        device.touch(False, *position)
        state["pressed"] = False
        state["last_touch"] = None

    # No parameters: present the retired native frame without interpolation blur.
    def present():
        try:
            frame, revision = device.frame()
        except NativeDisplayError as error:
            if error.error != 0x103 or time.monotonic() >= state["deadline"]:
                root.destroy()
                raise
        else:
            viewport_width, viewport_height = max(canvas.winfo_width(), 1), max(canvas.winfo_height(), 1)
            if display_mode.get() == "stretch":
                width, height = viewport_width, viewport_height
            else:
                width = height = min(viewport_width, viewport_height)
            layout = ((viewport_width - width) // 2, (viewport_height - height) // 2, width, height)
            if revision != state["revision"] or layout != state["layout"]:
                state["photo"] = ImageTk.PhotoImage(frame.resize((width, height), Image.Resampling.NEAREST))
                if state["item"] is None:
                    state["item"] = canvas.create_image(layout[0], layout[1], anchor="nw", image=state["photo"])
                else:
                    canvas.coords(state["item"], layout[0], layout[1])
                    canvas.itemconfigure(state["item"], image=state["photo"])
                state["layout"], state["revision"] = layout, revision
        root.after(33, present)

    canvas.bind("<ButtonPress-1>", pressed)
    canvas.bind("<B1-Motion>", pressed)
    canvas.bind("<ButtonRelease-1>", released)
    root.bind("<b>", lambda event: device.boot(False))
    root.bind("<B>", lambda event: device.boot(True))
    # The capture is intentionally unlabelled: a frame counter is not a route ID.
    root.bind("<F12>", lambda event: print(f"SIMULATED native frame: {capture_completed_frame(device)[0]}", flush=True))
    root.after(0, present)
    root.mainloop()
