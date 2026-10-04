"""Scrollable inspector surfaces; independent of Cody rendering and device state."""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk


class InspectorTab(ttk.Frame):
    """Each tab retains its own scroll position and only its content sets the range."""

    # self: New inspector surface retaining its canvas and embedded content.
    # parent: Notebook owning this tab's widget lifetime.
    # background: Theme color used outside the embedded ttk content.
    # pixel_scale: Startup monitor scale relative to 96-DPI UI spacing.
    # Raises tk.TclError if Tk cannot construct or bind the widgets.
    def __init__(self, parent, background, pixel_scale):
        super().__init__(parent, style="Panel.TFrame")
        self.pixel_scale = pixel_scale
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)
        self.viewport = tk.Canvas(
            self, width=self.pixels(376), height=self.pixels(320), background=background,
            highlightthickness=0, borderwidth=0, yscrollincrement=self.pixels(24),
        )
        self.viewport.grid(row=0, column=0, sticky="nsew")
        self.scrollbar = ttk.Scrollbar(
            self, orient="vertical", command=self.viewport.yview,
        )
        self.scrollbar.grid(row=0, column=1, sticky="ns", padx=(self.pixels(2), 0))
        self.viewport.configure(yscrollcommand=self.scrollbar.set)
        self.content = ttk.Frame(self.viewport, style="Panel.TFrame", padding=(self.pixels(16), self.pixels(18)))
        self.content_item = self.viewport.create_window(
            0, 0, window=self.content, anchor="nw",
        )
        self.viewport.bind("<Configure>", self.resize_viewport)
        self.content.bind("<Configure>", self.resize_content)

    # self: Tab retaining the startup monitor's UI pixel scale.
    # value: Pixel spacing specified at 96 DPI, independent of the logical character.
    def pixels(self, value):
        return round(value * self.pixel_scale)

    # self: Tab whose embedded content follows its visible width.
    # event: Canvas configure event supplying allocated dimensions.
    # Raises tk.TclError if the embedded window no longer exists.
    def resize_viewport(self, event):
        self.viewport.itemconfigure(self.content_item, width=max(1, event.width))
        self.update_scroll_region()

    # self: Tab owning the independent content scroll range.
    # event: Content configure notification after layout or a control update.
    # Raises tk.TclError if Tk cannot query the embedded window.
    def resize_content(self, event):
        self.update_scroll_region()

    # self: Tab retaining a range at least as tall as its visible viewport.
    # Raises tk.TclError if the canvas has been destroyed.
    def update_scroll_region(self):
        bounds = self.viewport.bbox(self.content_item)
        if bounds is not None:
            self.viewport.configure(scrollregion=(
                0, 0, bounds[2], max(bounds[3], self.viewport.winfo_height()),
            ))

    # self: Tab considered for scrolling under the pointer.
    # widget: Deepest Tk widget under the pointer, or None outside the window.
    def contains(self, widget):
        while widget is not None:
            if widget is self:
                return True
            widget = getattr(widget, "master", None)
        return False

    # self: Tab whose content is explicitly scrolled by the desktop wheel.
    # delta: Signed Windows wheel delta, including small precision-wheel movements.
    # Raises tk.TclError if the viewport no longer exists.
    def scroll(self, delta):
        if delta:
            steps = max(1, abs(delta) // 120)
            self.viewport.yview_scroll(-steps if delta > 0 else steps, "units")
