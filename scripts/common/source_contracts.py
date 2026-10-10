"""Explicit source families for implementation contracts after responsibility splits."""
from pathlib import Path

_SOURCE_PARTS = {
    "LUNASHELL_power.cpp": ("LUNASHELL_standby_resources.cpp", "LUNASHELL_standby_resources.hpp"),
    "CODY_onboarding.cpp": ("CODY_codex_event_presentation.hpp",),
    "LUNASHELL_hardware.cpp": (
        "LUNASHELL_snapshot_aging.cpp",
        "LUNASHELL_snapshot_names.cpp",
        "LUNASHELL_snapshot_reports.cpp",
        "LUNASHELL_rtc_calendar.cpp",
        "LUNASHELL_rtc_calendar.hpp",
        "LUNASHELL_snapshot_internal.hpp",
    ),
    "LUNASHELL_time.cpp": ("LUNASHELL_clock_policy.cpp",),
    "LUNASHELL_codex_link.cpp": ("LUNASHELL_codex_transaction.cpp", "../../include/LUNASHELL_codex_transaction.hpp"),
    "LUNASHELL_app.cpp": (
        "LUNASHELL_app_logs.cpp",
        "../pairing/LUNASHELL_app_pairing.cpp",
    ),
    "LUNASHELL_provisioning.hpp": ("LUNASHELL_provisioning_state.hpp",),
    "esp32_s3_touch_amoled_1_75.c": ("bsp_display_dma.c", "bsp_display_dma.h"),
}

# path: Principal source file whose explicitly registered implementation parts are required.
# Raises OSError or UnicodeError when any required source cannot be read.
def read_component_source(path: Path) -> str:
    parts = (path,) + tuple(path.parent / name for name in _SOURCE_PARTS.get(path.name, ()))
    return "\n".join(part.read_text(encoding="utf-8") for part in parts)
