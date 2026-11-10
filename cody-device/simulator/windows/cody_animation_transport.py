"""Send explicitly selected Studio animations through the paired CCP broker."""

from __future__ import annotations

from pathlib import Path
import sys
import threading
import tkinter
from typing import Callable


class AnimationTransport:
    # root: Active Tk root that owns UI callbacks.
    def __init__(self, root) -> None:
        self._root = root
        self._lock = threading.Lock()

    # scene_id: Stable firmware animation identifier.
    # action: Explicit select/play/pause/restart/normal command.
    # callback: Receives success and a human-readable result on the Tk thread.
    def request(self, scene_id: str, action: str, callback: Callable[[bool, str], None]) -> None:
        def worker() -> None:
            with self._lock:
                try:
                    plugin = Path(__file__).resolve().parents[3] / "cody-desktop" / "plugins" / "cody-codex-plugin" / "mcp"
                    if str(plugin) not in sys.path:
                        sys.path.insert(0, str(plugin))
                    import ccp_runtime
                    secret = ccp_runtime.ensure_runtime()
                    result = ccp_runtime.exchange("animation/request", {"scene": scene_id, "op": action}, secret, timeout=18.0)
                    if not isinstance(result, dict) or result.get("status") not in {"applied", "rejected", "unknown"}:
                        raise ValueError("Invalid animation response from CCP broker")
                    status = result.get("status")
                    success = status == "applied"
                    if success:
                        message = f"Cody {result['device_id']}: {action} applied"
                    elif status == "unknown":
                        message = "No authenticated response from Cody; outcome is unknown"
                    else:
                        message = f"Cody rejected {action}: {result.get('error', 'unknown error')}"
                except (ImportError, OSError, RuntimeError, ValueError, KeyError) as error:
                    success = False
                    message = str(error)
                    if message == "Unknown CCP local operation":
                        message = "The active CCP broker needs the updated animation support; restart the broker after updating"
            try:
                self._root.after(0, callback, success, message)
            except (RuntimeError, tkinter.TclError):
                # A closed Studio window has no UI to receive this transient result.
                return

        try:
            threading.Thread(target=worker, name="Cody-animation-request", daemon=True).start()
        except RuntimeError as error:
            callback(False, str(error))
