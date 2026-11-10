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
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.common.cody_host_compiler import prepare_host_compiler, simulator_library_flags
from runtime_paths import simulator_library_path

SOURCES = (
    "cody-device/simulator/native/CODY_host_graphics.cpp",
    "cody-device/simulator/native/CODY_host_personality_behavior.cpp",
    "cody-device/simulator/native/CODY_host_settings.cpp",
    "cody-device/simulator/native/CODY_host_ccp.cpp",
    "cody-device/simulator/native/CODY_host_event_presentation.cpp",
    "cody-device/simulator/native/CODY_host_standby.cpp",
    "cody-device/simulator/native/CODY_host_snapshot.cpp",
    "cody-device/simulator/native/CODY_host_boot_branding.cpp",
    "cody-device/simulator/native/CODY_host_muse.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_muse_frame.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_pixel_avatar.cpp",
    "cody-device/components/CODY_domain/src/CODY_state_receive.cpp",
    "cody-device/components/LUNASHELL_platform/src/network/LUNASHELL_codex_events.cpp",
    "cody-device/components/LUNASHELL_platform/src/network/LUNASHELL_codex_transaction.cpp",
    "cody-device/components/LUNASHELL_platform/src/power/LUNASHELL_standby_resources.cpp",
    "cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_rtc_calendar.cpp",
    "cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_aging.cpp",
    "cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_names.cpp",
    "cody-device/components/LUNASHELL_platform/src/runtime/LUNASHELL_clock_policy.cpp",
    "cody-device/components/LUNASHELL_platform/src/http/LUNASHELL_http_json.cpp",
    "cody-device/components/CODY2D/src/CODY2D.cpp",
    "cody-device/components/CODY2D/src/CODY2D_paths.cpp",
    "cody-device/components/CODY2D/src/CODY2D_controls.cpp",
    "cody-device/components/CODY2D/src/CODY2D_text.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_rig.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_expression_catalog.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_response.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_character_direction.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_happiness.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_petting.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_connection_personality.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_dirty_plan.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_personality.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_jelly.cpp",
    "cody-device/components/CODY_companion/src/diagnostics/CODY_debug_views.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_scene.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_technical_panels.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_home_dashboard.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_home_identity.cpp",
    "cody-device/assets/cody/home-dashboard-fonts/generated/home_dashboard_fonts.cpp",
    "cody-device/assets/cody/personality-icons/generated/personality_icons.cpp",
    "cody-device/assets/cody/appearance/jelly-scanline/generated/cody_jelly_data.cpp",
    "cody-device/assets/cody/scenes/generated/scene_font.cpp",
    "cody-device/assets/cody/scenes/generated/scene_body_font.cpp",
)
MUSE_DIR = "cody-device/components/CODY_companion/third_party/muse"
MUSE_SOURCE = f"{MUSE_DIR}/muse_pixel.c"
INCLUDES = (
    "cody-device/simulator/native/platform",
    "cody-device/components/CODY2D/include",
    "cody-device/components/CODY_companion/include",
    "cody-device/components/CODY_domain/include",
    MUSE_DIR,
    "cody-device/components/CODY_companion/src/scenes",
    "cody-device/components/LUNASHELL_boot/include",
    "cody-device/components/LUNASHELL_platform/include",
    "cody-device/components/LUNASHELL_platform/src/snapshot",
    "cody-device/components/LUNASHELL_platform/src/power",
    "cody-device/assets/cody/rig/generated",
    "cody-device/assets/cody/scenes/generated",
    "cody-device/assets/cody/home-dashboard-fonts/generated",
    "cody-device/assets/cody/personality/generated",
    "cody-device/assets/cody/personality-icons/generated",
    "cody-device/assets/cody/appearance/jelly-scanline/generated",
    "cody-device/assets/branding/VibeWare/generated",
)
DEFINITIONS = ("CODY_ENABLE_PEARL_JELLY=1", "CODY_HOST_SIMULATOR=1")


# path: Exact source or library file whose byte identity is recorded.
# Raises OSError for unreadable inputs; absence is never replaced with a digest.
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# No parameters: gathers compiled sources, production headers and authored catalogs.
# Raises OSError for unreadable source trees or missing required inputs.
def source_provenance():
    paths = {ROOT / source for source in (*SOURCES, MUSE_SOURCE)}
    for directory in INCLUDES:
        paths.update(path for path in (ROOT / directory).rglob("*")
                     if path.is_file() and path.suffix in (".h", ".hpp"))
    paths.update(ROOT / name for name in (
        "cody-device/assets/cody/cody.yaml", "cody-device/assets/cody/scenes/scenes.json",
        "cody-device/assets/cody/navigation.json", "cody-device/assets/cody/personality.yaml",
        "cody-device/assets/cody/appearance/jelly-scanline/skins.json",
        "scripts/generators/generate_cody_simulator_graphics.py",
        "cody-device/simulator/native/CODY_host_snapshot.hpp",
        "cody-device/components/LUNASHELL_boot/src/ui/VIBEWARE_boot_animation.c",
        "cody-device/assets/branding/VibeWare/generated/vibeware_boot_data.c",
        "scripts/common/cody_native_checks.py",
        "scripts/common/cody_host_compiler.py",
        "cody-device/simulator/windows/runtime_paths.py",
    ))
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(paths)}


# compiler: Existing native host driver.
# output: Artifact-owned temporary object to link into the C++ library.
def muse_compile_command(compiler, output):
    source = str(ROOT / MUSE_SOURCE)
    include = str(ROOT / MUSE_DIR)
    if Path(compiler).stem.lower() == "cl":
        return [compiler, "/nologo", "/TC", "/std:c11", "/O2", "/MD", "/c",
                f"/I{include}", source, f"/Fo{output}"]
    return [compiler, "-x", "c", "-std=c11", "-O2", "-fPIC", "-c",
            f"-I{include}", source, "-o", str(output)]


# argv: Optional existing compiler selection and explicit cache rebuild request.
# Raises original compiler, file, timeout or build errors; no simulator is executed.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)
    library = simulator_library_path(ROOT, "graphics")
    output = library.parent
    manifest_path = output / "manifest.json"
    compiler = resolve_compiler(args.compiler) if args.compiler else prepare_host_compiler()
    inputs = source_provenance()
    output.mkdir(parents=True, exist_ok=True)
    muse_object = output / "muse_pixel.obj"
    muse_command = muse_compile_command(compiler, muse_object)
    command = compile_command(compiler, INCLUDES, (*SOURCES, muse_object), library, DEFINITIONS, cxx_standard=20)
    command += simulator_library_flags(compiler, library)
    commands = [muse_command, command]
    # A cached library is usable only when both its sources and its binary agree.
    if not args.force and manifest_path.is_file() and library.is_file():
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        if (previous.get("schema") == "cody.host-graphics.v1" and
                previous.get("inputs") == inputs and previous.get("command") == commands and
                previous.get("library_sha256") == digest(library)):
            print(f"Cody C++ graphics driver already current: {library}")
            return
    # Retire provenance before rebuilding so a failed compiler cannot expose old output.
    manifest_path.unlink(missing_ok=True)
    try:
        result = subprocess.run(muse_command, cwd=ROOT, capture_output=True, text=True, timeout=240)
        if result.returncode != 0:
            raise RuntimeError(f"Muse C compilation failed ({result.returncode}):\n{result.stdout}{result.stderr}")
        run_stage(command, output, "compile")
    finally:
        muse_object.unlink(missing_ok=True)
    manifest = {"schema": "cody.host-graphics.v1", "inputs": inputs,
                "command": commands, "library_sha256": digest(library)}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Cody C++ graphics driver compiled: {library}")


if __name__ == "__main__":
    main()
