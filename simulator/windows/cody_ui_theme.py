"""Cody Studio colors and ttk styling; independent of firmware and scene rendering."""

from __future__ import annotations

import tkinter as tk
from tkinter import font as tkfont, ttk


PALETTE = {
    "background": "#0c121d",
    "canvas": "#0a1019",
    "panel": "#121c2a",
    "card": "#192536",
    "field": "#0f1927",
    "border": "#2b3b50",
    "text": "#edf4fc",
    "muted": "#99adc5",
    "disabled": "#61748c",
    "accent": "#60d4c5",
    "accent_hover": "#8ae5d9",
    "accent_dark": "#244e50",
    "button_hover": "#24364a",
}

FONTS = {
    "body": ("Segoe UI", 11),
    "small": ("Segoe UI", 10),
    "title": ("Segoe UI", 22, "bold"),
    "subtitle": ("Segoe UI", 11),
    "section": ("Segoe UI", 10, "bold"),
    "value": ("Segoe UI", 10, "bold"),
    "badge": ("Segoe UI", 9, "bold"),
    "status": ("Segoe UI", 9),
    "mono": ("Consolas", 10),
}


# root: Tk interpreter that owns the image throughout the window lifetime.
# selected: Whether the switch represents an enabled setting.
# hovered: Whether the pointer is over the switch.
# disabled: Whether interaction with the switch is disabled.
# Raises tk.TclError if the interpreter cannot allocate or paint the image.
def _switch_image(root, *, selected, hovered=False, disabled=False):
    # The transparent right edge separates the switch from its text label.
    image = tk.PhotoImage(master=root, width=42, height=20)
    track_color = PALETTE["accent"] if selected else PALETTE["border"]
    knob_color = PALETTE["background"] if selected else PALETTE["text"]
    if hovered:
        track_color = PALETTE["accent_hover"] if selected else PALETTE["disabled"]
    if disabled:
        track_color = PALETTE["accent_dark"] if selected else PALETTE["field"]
        knob_color = PALETTE["muted"]

    # Transparent corners let the same control sit on a panel or a card.
    for y in range(20):
        for x in range(34):
            closest_center = min(24, max(9, x))
            if (x - closest_center) ** 2 + (y - 9.5) ** 2 <= 81:
                image.put(track_color, (x, y))
    knob_center = 24 if selected else 9
    for y in range(20):
        for x in range(34):
            if (x - knob_center) ** 2 + (y - 9.5) ** 2 <= 36:
                image.put(knob_color, (x, y))
    return image


# root: Tk interpreter that owns the scale thumb image.
# color: Fill color of the circular thumb.
# Raises tk.TclError if the interpreter cannot allocate or paint the image.
def _scale_image(root, color):
    image = tk.PhotoImage(master=root, width=16, height=16)
    for y in range(16):
        for x in range(16):
            if (x - 7.5) ** 2 + (y - 7.5) ** 2 <= 49:
                image.put(color, (x, y))
    return image


# root: Tk interpreter that owns the scale track image.
# color: Fill color of the two-pixel track.
# Raises tk.TclError if the interpreter cannot allocate or paint the image.
def _scale_track_image(root, color):
    image = tk.PhotoImage(master=root, width=2, height=16)
    image.put(color, to=(0, 7, 2, 9))
    return image


# root: Simulator Tk root; no device, renderer or DPI state is accessed.
# Raises tk.TclError if the installed Tk cannot provide the required clam theme.
def install_theme(root):
    """Install the session theme and return its host-interface color palette."""
    if hasattr(root, "_cody_studio_palette"):
        return root._cody_studio_palette

    style = ttk.Style(root)
    style.theme_use("clam")
    root.configure(background=PALETTE["background"])

    for name in ("TkDefaultFont", "TkTextFont", "TkMenuFont", "TkHeadingFont"):
        tkfont.nametofont(name, root=root).configure(family="Segoe UI", size=11)

    style.configure(
        ".", background=PALETTE["panel"], foreground=PALETTE["text"],
        font=FONTS["body"], bordercolor=PALETTE["border"],
        lightcolor=PALETTE["border"], darkcolor=PALETTE["border"],
        focuscolor=PALETTE["accent"],
    )
    style.configure("TFrame", background=PALETTE["panel"])
    style.configure("Studio.TFrame", background=PALETTE["background"])
    style.configure("Panel.TFrame", background=PALETTE["panel"])
    style.configure("Card.TFrame", background=PALETTE["card"])
    style.configure("Toolbar.TFrame", background=PALETTE["panel"])
    style.configure(
        "Card.TLabelframe", background=PALETTE["panel"], padding=14,
        bordercolor=PALETTE["border"], borderwidth=1, relief="solid",
    )
    style.configure(
        "Card.TLabelframe.Label", background=PALETTE["panel"],
        foreground=PALETTE["accent"], font=FONTS["section"],
    )
    style.configure("TLabel", background=PALETTE["panel"], foreground=PALETTE["text"])
    style.configure("Title.TLabel", font=FONTS["title"], background=PALETTE["background"])
    style.configure("Subtitle.TLabel", font=FONTS["subtitle"], foreground=PALETTE["muted"])
    style.configure("Section.TLabel", font=FONTS["section"], foreground=PALETTE["accent"])
    style.configure("Muted.TLabel", font=FONTS["small"], foreground=PALETTE["muted"])
    style.configure("Value.TLabel", font=FONTS["value"], foreground=PALETTE["accent"])
    style.configure(
        "Badge.TLabel", font=FONTS["badge"], foreground=PALETTE["accent"],
        background=PALETTE["accent_dark"], padding=(10, 5),
    )
    style.configure(
        "Status.TLabel", font=FONTS["status"], foreground=PALETTE["muted"],
        background=PALETTE["background"], padding=(0, 4),
    )
    style.configure("Card.TLabel", background=PALETTE["card"])
    style.configure("Studio.TLabel", background=PALETTE["background"])
    style.configure("Toolbar.TLabel", background=PALETTE["panel"])

    style.configure(
        "TButton", background=PALETTE["card"], foreground=PALETTE["text"],
        borderwidth=1, relief="flat", padding=(12, 8), focusthickness=1,
        focuscolor=PALETTE["accent"],
    )
    style.map(
        "TButton",
        background=[("disabled", PALETTE["panel"]), ("pressed", PALETTE["accent_dark"]),
                    ("active", PALETTE["button_hover"])],
        foreground=[("disabled", PALETTE["disabled"])],
        bordercolor=[("focus", PALETTE["accent"]), ("active", PALETTE["disabled"])],
    )
    style.configure("Compact.TButton", font=FONTS["small"], padding=(10, 6))
    style.configure("Preset.TButton", font=FONTS["small"], padding=(10, 6))
    style.configure(
        "Primary.TButton", background=PALETTE["accent"], foreground=PALETTE["background"],
        font=FONTS["value"], bordercolor=PALETTE["accent"],
    )
    style.map(
        "Primary.TButton",
        background=[("disabled", PALETTE["accent_dark"]),
                    ("pressed", PALETTE["accent"]), ("active", PALETTE["accent_hover"])],
        foreground=[("disabled", PALETTE["muted"])],
    )

    style.configure(
        "TCombobox", fieldbackground=PALETTE["field"], background=PALETTE["card"],
        foreground=PALETTE["text"], arrowcolor=PALETTE["muted"],
        selectbackground=PALETTE["accent_dark"], selectforeground=PALETTE["text"],
        padding=(10, 7), borderwidth=1, arrowsize=14, relief="flat",
    )
    style.map(
        "TCombobox",
        fieldbackground=[("disabled", PALETTE["panel"]), ("readonly", PALETTE["field"])],
        foreground=[("disabled", PALETTE["disabled"]), ("readonly", PALETTE["text"])],
        background=[("active", PALETTE["button_hover"])],
        bordercolor=[("focus", PALETTE["accent"])],
    )
    # The popdown is a classic Tk listbox, so ttk style settings do not reach it.
    root.option_add("*TCombobox*Listbox.background", PALETTE["field"])
    root.option_add("*TCombobox*Listbox.foreground", PALETTE["text"])
    root.option_add("*TCombobox*Listbox.selectBackground", PALETTE["accent_dark"])
    root.option_add("*TCombobox*Listbox.selectForeground", PALETTE["text"])
    root.option_add("*TCombobox*Listbox.font", FONTS["body"])
    root.option_add("*TCombobox*Listbox.relief", "flat")
    root.option_add("*TCombobox*Listbox.borderWidth", 0)
    style.configure("TEntry", fieldbackground=PALETTE["field"], padding=(8, 6), borderwidth=1)
    style.map("TEntry", bordercolor=[("focus", PALETTE["accent"])])

    style.configure("TNotebook", background=PALETTE["panel"], borderwidth=0, tabmargins=(0, 6, 0, 12))
    style.configure(
        "TNotebook.Tab", background=PALETTE["panel"], foreground=PALETTE["muted"],
        padding=(16, 10), font=FONTS["value"], borderwidth=0,
    )
    style.map(
        "TNotebook.Tab",
        background=[("selected", PALETTE["card"]), ("active", PALETTE["button_hover"])],
        foreground=[("selected", PALETTE["accent"]), ("active", PALETTE["text"])],
        lightcolor=[("selected", PALETTE["card"])],
        darkcolor=[("selected", PALETTE["card"])],
    )

    switch_images = {
        "off": _switch_image(root, selected=False),
        "on": _switch_image(root, selected=True),
        "off_hover": _switch_image(root, selected=False, hovered=True),
        "on_hover": _switch_image(root, selected=True, hovered=True),
        "off_disabled": _switch_image(root, selected=False, disabled=True),
        "on_disabled": _switch_image(root, selected=True, disabled=True),
    }
    style.element_create(
        "Cody.Switch", "image", switch_images["off"],
        ("disabled", "selected", switch_images["on_disabled"]),
        ("disabled", switch_images["off_disabled"]),
        ("active", "selected", switch_images["on_hover"]),
        ("selected", switch_images["on"]),
        ("active", switch_images["off_hover"]),
        sticky="w",
    )
    style.layout("TCheckbutton", [
        ("Checkbutton.padding", {"sticky": "nswe", "children": [
            ("Cody.Switch", {"side": "left", "sticky": "w"}),
            ("Checkbutton.focus", {"side": "left", "sticky": "w", "children": [
                ("Checkbutton.label", {"sticky": "nswe"}),
            ]}),
        ]}),
    ])
    style.configure("TCheckbutton", background=PALETTE["panel"], padding=(0, 7), indicatorrelief="flat")
    style.map(
        "TCheckbutton", background=[("active", PALETTE["panel"])],
        foreground=[("disabled", PALETTE["disabled"])],
    )
    style.configure("Toolbar.TCheckbutton", background=PALETTE["background"], padding=(0, 7))
    style.map("Toolbar.TCheckbutton", background=[("active", PALETTE["background"])])

    scale_images = {
        "normal": _scale_image(root, PALETTE["accent"]),
        "active": _scale_image(root, PALETTE["accent_hover"]),
        "disabled": _scale_image(root, PALETTE["disabled"]),
    }
    scale_track = _scale_track_image(root, PALETTE["border"])
    style.element_create("Cody.Scale.trough", "image", scale_track, border=(0, 7, 0, 7), sticky="we")
    # Tk's scale behavior locates elements by their trough/slider suffixes.
    style.element_create(
        "Cody.Scale.slider", "image", scale_images["normal"],
        ("disabled", scale_images["disabled"]),
        ("active", scale_images["active"]),
    )
    style.layout("Horizontal.TScale", [
        ("Cody.Scale.trough", {"sticky": "we", "children": [
            ("Cody.Scale.slider", {"side": "left", "sticky": ""}),
        ]}),
    ])
    style.configure(
        "Horizontal.TScale", background=PALETTE["panel"], troughcolor=PALETTE["border"],
        borderwidth=0, troughborderwidth=0, sliderrelief="flat",
    )

    for orientation, thumb_sticky in (("Vertical", "nswe"), ("Horizontal", "nswe")):
        style_name = f"{orientation}.TScrollbar"
        style.layout(style_name, [
            (f"{orientation}.Scrollbar.trough", {"sticky": "nswe", "children": [
                (f"{orientation}.Scrollbar.thumb", {"expand": "1", "sticky": thumb_sticky}),
            ]}),
        ])
        style.configure(
            style_name, background=PALETTE["border"], troughcolor=PALETTE["panel"],
            borderwidth=0, arrowsize=9, width=9, relief="flat", gripcount=0,
        )
        style.map(style_name, background=[("active", PALETTE["disabled"]), ("pressed", PALETTE["muted"])])
    style.configure("TSeparator", background=PALETTE["border"])

    # PhotoImage wrappers must survive for as long as Tcl style elements use them.
    root._cody_studio_images = (*switch_images.values(), *scale_images.values(), scale_track)
    root._cody_studio_palette = PALETTE.copy()
    return root._cody_studio_palette
