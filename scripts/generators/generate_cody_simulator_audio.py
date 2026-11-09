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

"""Build the desktop output driver linked to unchanged Cody audio modules."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.common.cody_host_compiler import prepare_host_compiler, simulator_library_flags
from runtime_paths import simulator_library_path


# argv: Optional literal host compiler selection forwarded by the device CLI.
# Raises original compiler, installation or output errors; no playback is requested.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    args = parser.parse_args(argv)
    library = simulator_library_path(ROOT, "audio")
    compiler = resolve_compiler(args.compiler) if args.compiler else prepare_host_compiler()
    output = library.parent
    output.mkdir(parents=True, exist_ok=True)
    includes = ("scripts/tests/cody_native", "cody-device/components/CODY_companion/include",
        "cody-device/components/CODY2D/include", "cody-device/components/LUNASHELL_platform/include",
        "cody-device/assets/cody/personality/generated", "cody-device/assets/cody/personality-icons/audio",
        "cody-device/assets/cody/rig/generated")
    sources = ("cody-device/simulator/native/CODY_fake_audio.cpp",
        "cody-device/components/CODY_companion/src/scenes/CODY_typewriter_audio.cpp",
        "cody-device/components/CODY_companion/src/runtime/CODY_personality_audio.cpp",
        "cody-device/assets/cody/personality-icons/audio/personality_audio.cpp")
    command = compile_command(compiler, includes, sources, library)
    command += simulator_library_flags(compiler, library)
    run_stage(command, output, "compile")
    print(f"Cody Studio audio driver compiled from real Cody modules: {library}")


if __name__ == "__main__":
    main()
