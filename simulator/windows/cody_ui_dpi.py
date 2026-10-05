"""Explicit Windows PMv2 setup and Tk point scaling for the local simulator."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import sys
import tkinter as tk


ERROR_ACCESS_DENIED = 5
DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ctypes.c_void_p(-4)
MONITOR_DEFAULTTONEAREST = 2


class _MonitorInfo(ctypes.Structure):
    _fields_ = (
        ("cbSize", wintypes.DWORD),
        ("rcMonitor", wintypes.RECT),
        ("rcWork", wintypes.RECT),
        ("dwFlags", wintypes.DWORD),
    )


# message: One explicit startup decision or native API failure for the host log.
# Raises OSError if the simulator's diagnostic stream cannot be written.
def _report(message):
    print(f"[cody-ui-dpi] {message}", file=sys.stderr)


# Raises OSError if Windows cannot load user32.
# Raises AttributeError if Windows does not expose the required display APIs.
def _windows_user32():
    try:
        user32 = ctypes.WinDLL("user32", use_last_error=True)
        user32.SetProcessDpiAwarenessContext.argtypes = (ctypes.c_void_p,)
        user32.SetProcessDpiAwarenessContext.restype = wintypes.BOOL
        user32.GetThreadDpiAwarenessContext.argtypes = ()
        user32.GetThreadDpiAwarenessContext.restype = ctypes.c_void_p
        user32.AreDpiAwarenessContextsEqual.argtypes = (ctypes.c_void_p, ctypes.c_void_p)
        user32.AreDpiAwarenessContextsEqual.restype = wintypes.BOOL
        user32.GetDpiForWindow.argtypes = (wintypes.HWND,)
        user32.GetDpiForWindow.restype = wintypes.UINT
        user32.MonitorFromWindow.argtypes = (wintypes.HWND, wintypes.DWORD)
        user32.MonitorFromWindow.restype = wintypes.HANDLE
        user32.GetMonitorInfoW.argtypes = (wintypes.HANDLE, ctypes.POINTER(_MonitorInfo))
        user32.GetMonitorInfoW.restype = wintypes.BOOL
        return user32
    except (OSError, AttributeError) as error:
        _report(f"Required Windows display API is unavailable: {error}")
        raise


# user32: Library whose DPI context functions have explicit ctypes signatures.
# Raises RuntimeError if the UI thread does not have the required PMv2 context.
def _require_pmv2_thread(user32):
    context = user32.GetThreadDpiAwarenessContext()
    if not context:
        message = "GetThreadDpiAwarenessContext returned no valid context."
        _report(message)
        raise RuntimeError(message)
    matches = user32.AreDpiAwarenessContextsEqual(
        ctypes.c_void_p(context), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2,
    )
    if not matches:
        message = (
            "The current UI thread is not Per Monitor v2 aware; "
            "Cody Studio requires PMv2 before creating Tk."
        )
        _report(message)
        raise RuntimeError(message)


# Raises OSError for a rejected Windows DPI operation or unavailable native API.
# Raises AttributeError if Windows does not expose the required PMv2 APIs.
# Raises RuntimeError if the existing UI-thread DPI context is incompatible.
def prepare_display():
    """Establish PMv2 before Tk or any other DPI-dependent window is created."""
    if sys.platform != "win32":
        _report(f"Native {sys.platform} display handling; no Windows DPI override.")
        return

    user32 = _windows_user32()
    ctypes.set_last_error(0)
    configured = user32.SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2,
    )
    if configured:
        _require_pmv2_thread(user32)
        _report("Windows Per Monitor v2 awareness enabled before Tk creation.")
        return

    error = ctypes.get_last_error()
    if error == ERROR_ACCESS_DENIED:
        # A manifest or earlier caller can already own the process setting.
        # Accept it only when this thread will actually create a PMv2 window.
        _require_pmv2_thread(user32)
        _report("Windows DPI policy was already set; existing PMv2 context verified.")
        return

    if error:
        failure = ctypes.WinError(error)
        _report(f"SetProcessDpiAwarenessContext failed: {failure}")
        raise failure
    message = "SetProcessDpiAwarenessContext failed without a Windows error code."
    _report(message)
    raise RuntimeError(message)


# root: Newly created Tk root, before point-sized fonts and controls are constructed.
# Raises tk.TclError if Tk cannot realize the window or apply its point scaling.
# Raises OSError or AttributeError if required Windows DPI APIs are unavailable.
# Raises RuntimeError if the UI thread or realized window has no valid DPI state.
def configure_tk_scaling(root):
    """Set Tk pixels per typographic point from this window's actual startup DPI."""
    if sys.platform != "win32":
        return None

    user32 = _windows_user32()
    _require_pmv2_thread(user32)
    root.update_idletasks()
    window_id = root.winfo_id()
    dpi = user32.GetDpiForWindow(wintypes.HWND(window_id))
    if not dpi:
        message = f"GetDpiForWindow rejected the realized Tk window {window_id}."
        _report(message)
        raise RuntimeError(message)

    pixels_per_point = dpi / 72.0
    # Tk scaling changes point-based fonts, not authored scene coordinates or
    # canvas pixel bounds. Monitor changes need a separate Tk-aware update path.
    root.tk.call("tk", "scaling", pixels_per_point)
    _report(f"Tk startup DPI {dpi}; scaling {pixels_per_point:.4f} pixels per point.")
    return int(dpi)


# root: Realized Tk root whose nearest monitor owns the startup window.
# Raises tk.TclError if Tk cannot query its native window or desktop dimensions.
# Raises OSError or AttributeError if a required Windows monitor query fails.
# Raises RuntimeError if Windows supplies no monitor or invalid work-area bounds.
def window_work_area(root):
    """Return physical desktop bounds available to the window, excluding taskbars."""
    if sys.platform != "win32":
        return (0, 0, root.winfo_screenwidth(), root.winfo_screenheight())

    user32 = _windows_user32()
    _require_pmv2_thread(user32)
    window_id = root.winfo_id()
    monitor = user32.MonitorFromWindow(
        wintypes.HWND(window_id), MONITOR_DEFAULTTONEAREST,
    )
    if not monitor:
        message = f"MonitorFromWindow returned no monitor for Tk window {window_id}."
        _report(message)
        raise RuntimeError(message)

    info = _MonitorInfo()
    info.cbSize = ctypes.sizeof(_MonitorInfo)
    ctypes.set_last_error(0)
    if not user32.GetMonitorInfoW(monitor, ctypes.byref(info)):
        error = ctypes.get_last_error()
        if error:
            failure = ctypes.WinError(error)
            _report(f"GetMonitorInfoW failed: {failure}")
            raise failure
        message = "GetMonitorInfoW failed without a Windows error code."
        _report(message)
        raise RuntimeError(message)

    area = (info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom)
    if area[2] <= area[0] or area[3] <= area[1]:
        message = f"GetMonitorInfoW supplied invalid work-area bounds: {area}."
        _report(message)
        raise RuntimeError(message)
    _report(f"Startup monitor work area {area}, in physical desktop pixels.")
    return area
