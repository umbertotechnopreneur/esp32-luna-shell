"""Read-only Codex/local status for the existing companion control owner."""

import hashlib
from typing import Any

from ccp_activity import ACTIVITY_LEASE_SECONDS
from ccp_presence import AppState


# activity: Existing native hook owner; no additional journal reads or synthetic events.
# presence: Existing native MCP transport lease owner, independent of widget lifetime.
# pc_id: Retained PC identity used when the native client has no followed chat.
# connected: None for service status; explicit authenticated pairing/liveness for a device.
def current_source(activity: Any, presence: Any, pc_id: str, connected: bool | None = None) -> dict[str, Any]:
    if connected is False:
        return {"provider": "local", "identity": "-", "state": "unavailable", "age_ms": None,
                "label": "Cody", "detail": "Device not connected"}
    native = presence.snapshot()
    if native.state != AppState.OPEN:
        return {"provider": "local", "identity": "-", "state": "idle", "age_ms": None,
                "label": "Cody", "detail": "Local companion"}
    observation = activity.source_observation()
    phase = observation["phase"]
    states = {"working": "working", "editing": "working", "running": "working", "paused": "attention",
              "unavailable": "unavailable", "interrupted": "attention"}
    state = states.get(phase, "idle")
    age_ms = observation["age_ms"] if observation["identity"] is not None else native.age_ms
    if observation["identity"] is not None and (age_ms is None or age_ms >= ACTIVITY_LEASE_SECONDS * 1000):
        state = "unavailable"
    identity = observation["identity"] or hashlib.sha256(("codex:" + pc_id).encode("ascii")).hexdigest()[:32]
    return {"provider": "codex", "identity": identity, "state": state, "age_ms": age_ms,
            "label": "Codex", "detail": "No active chat" if phase == "none" else ""}
