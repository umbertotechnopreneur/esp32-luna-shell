# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Build the PC graphics driver from Cody's production C++ sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.common.cody_host_compiler import prepare_host_compiler

OUTPUT = ROOT / "artifacts/build-cody-simulator-graphics"
LIBRARY = OUTPUT / "cody_host_graphics.dll"
MANIFEST = OUTPUT / "manifest.json"
SOURCES = (
    "simulator/native/CODY_host_graphics.cpp",
    "simulator/native/CODY_host_personality_behavior.cpp",
    "simulator/native/CODY_host_settings.cpp",
    "simulator/native/CODY_host_ccp.cpp",
    "simulator/native/CODY_host_event_presentation.cpp",
    "simulator/native/CODY_host_standby.cpp",
    "simulator/native/CODY_host_snapshot.cpp",
    "simulator/native/CODY_host_boot_branding.cpp",
    "components/LUNASHELL_platform/src/network/LUNASHELL_codex_events.cpp",
    "components/LUNASHELL_platform/src/network/LUNASHELL_codex_transaction.cpp",
    "components/LUNASHELL_platform/src/power/LUNASHELL_standby_resources.cpp",
    "components/LUNASHELL_platform/src/snapshot/LUNASHELL_rtc_calendar.cpp",
    "components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_aging.cpp",
    "components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_names.cpp",
    "components/LUNASHELL_platform/src/runtime/LUNASHELL_clock_policy.cpp",
    "components/LUNASHELL_platform/src/http/LUNASHELL_http_json.cpp",
    "components/CODY2D/src/CODY2D.cpp",
    "components/CODY2D/src/CODY2D_paths.cpp",
    "components/CODY2D/src/CODY2D_controls.cpp",
    "components/CODY2D/src/CODY2D_text.cpp",
    "components/CODY_companion/src/model/CODY_rig.cpp",
    "components/CODY_companion/src/model/CODY_expression_catalog.cpp",
    "components/CODY_companion/src/model/CODY_response.cpp",
    "components/CODY_companion/src/model/CODY_character_direction.cpp",
    "components/CODY_companion/src/model/CODY_happiness.cpp",
    "components/CODY_companion/src/model/CODY_petting.cpp",
    "components/CODY_companion/src/model/CODY_connection_personality.cpp",
    "components/CODY_companion/src/rendering/CODY_dirty_plan.cpp",
    "components/CODY_companion/src/rendering/CODY_personality.cpp",
    "components/CODY_companion/src/rendering/CODY_jelly.cpp",
    "components/CODY_companion/src/diagnostics/CODY_debug_views.cpp",
    "components/CODY_companion/src/scenes/CODY_scene.cpp",
    "components/CODY_companion/src/scenes/CODY_technical_panels.cpp",
    "assets/cody/personality-icons/generated/personality_icons.cpp",
    "assets/cody/appearance/jelly-scanline/generated/cody_jelly_data.cpp",
    "assets/cody/scenes/generated/scene_font.cpp",
    "assets/cody/scenes/generated/scene_body_font.cpp",
)
INCLUDES = (
    "simulator/native/platform",
    "components/CODY2D/include",
    "components/CODY_companion/include",
    "components/CODY_companion/src/scenes",
    "components/LUNASHELL_boot/include",
    "components/LUNASHELL_platform/include",
    "components/LUNASHELL_platform/src/snapshot",
    "components/LUNASHELL_platform/src/power",
    "assets/cody/rig/generated",
    "assets/cody/scenes/generated",
    "assets/cody/personality/generated",
    "assets/cody/personality-icons/generated",
    "assets/cody/appearance/jelly-scanline/generated",
    "assets/branding/VibeWare/generated",
)
DEFINITIONS = ("CODY_ENABLE_PEARL_JELLY=1", "CODY_HOST_SIMULATOR=1")


# path: Exact source or library file whose byte identity is recorded.
# Raises OSError for unreadable inputs; absence is never replaced with a digest.
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# No parameters: gathers compiled sources, production headers and authored catalogs.
# Raises OSError for unreadable source trees or missing required inputs.
def source_provenance():
    paths = {ROOT / source for source in SOURCES}
    for directory in INCLUDES:
        paths.update(path for path in (ROOT / directory).rglob("*")
                     if path.is_file() and path.suffix in (".h", ".hpp"))
    paths.update(ROOT / name for name in (
        "assets/cody/cody.yaml", "assets/cody/scenes/scenes.json",
        "assets/cody/navigation.json", "assets/cody/personality.yaml",
        "assets/cody/appearance/jelly-scanline/skins.json",
        "scripts/generators/generate_cody_simulator_graphics.py",
        "simulator/native/CODY_host_snapshot.hpp",
        "components/LUNASHELL_boot/src/ui/VIBEWARE_boot_animation.c",
        "assets/branding/VibeWare/generated/vibeware_boot_data.c",
        "scripts/common/cody_native_checks.py",
        "scripts/common/cody_host_compiler.py",
    ))
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(paths)}


# argv: Optional existing compiler selection and explicit cache rebuild request.
# Raises original compiler, file, timeout or build errors; no simulator is executed.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)
    if os.name != "nt":
        raise RuntimeError("Cody Studio's Windows driver requires a Windows host toolchain.")
    compiler = resolve_compiler(args.compiler) if args.compiler else prepare_host_compiler()
    inputs = source_provenance()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    command = compile_command(compiler, INCLUDES, SOURCES, LIBRARY, DEFINITIONS, cxx_standard=20)
    if Path(compiler).stem.lower() == "cl":
        command += ["/LD", "/link", f"/IMPLIB:{OUTPUT / 'cody_host_graphics.lib'}"]
    else:
        command += ["-shared"]
    # A cached library is usable only when both its sources and its binary agree.
    if not args.force and MANIFEST.is_file() and LIBRARY.is_file():
        previous = json.loads(MANIFEST.read_text(encoding="utf-8"))
        if (previous.get("schema") == "cody.host-graphics.v1" and
                previous.get("inputs") == inputs and previous.get("command") == command and
                previous.get("library_sha256") == digest(LIBRARY)):
            print(f"Cody C++ graphics driver already current: {LIBRARY}")
            return
    # Retire provenance before rebuilding so a failed compiler cannot expose old output.
    MANIFEST.unlink(missing_ok=True)
    run_stage(command, OUTPUT, "compile")
    manifest = {"schema": "cody.host-graphics.v1", "inputs": inputs,
                "command": command, "library_sha256": digest(LIBRARY)}
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Cody C++ graphics driver compiled: {LIBRARY}")


if __name__ == "__main__":
    main()
