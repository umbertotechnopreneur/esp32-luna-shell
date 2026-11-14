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
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.common.cody_host_compiler import prepare_host_compiler, simulator_library_flags
from runtime_paths import simulator_library_path

PET_ASSETS = (
    ("cody_pet_boot_ding", "cody-device/assets/cody/audio/pet/generated/boot-ding.pcm16le"),
    ("cody_pet_birth_rise", "cody-device/assets/cody/audio/pet/generated/birth-rise.pcm16le"),
    ("cody_pet_birth_greeting", "cody-device/assets/cody/audio/pet/generated/birth-greeting.pcm16le"),
    ("cody_pet_tap", "cody-device/assets/cody/audio/pet/generated/tap.pcm16le"),
    ("cody_pet_jump", "cody-device/assets/cody/audio/pet/generated/jump.pcm16le"),
    ("cody_pet_surprise", "cody-device/assets/cody/audio/pet/generated/surprise.pcm16le"),
    ("cody_first_boot_music", "cody-device/assets/cody/audio/onboarding/generated/boot-melody.pcm16le"),
)


# output: Artifact-owned assembler source defining the firmware's binary symbols.
# Raises OSError or ValueError when a source asset is missing or invalid.
def write_pet_assembler(output):
    # COFF requires distinct section flags; ELF uses the native read-only section.
    lines = ['.section .rdata,"dr"' if sys.platform == "win32" else '.section .rodata,"a",@progbits']
    for symbol, relative in PET_ASSETS:
        asset = (ROOT / relative).resolve(strict=True)
        size = asset.stat().st_size
        if size == 0 or size % 2 or (size != 147200 if symbol == "cody_first_boot_music" else size > 64000):
            raise ValueError(f"Invalid firmware PCM asset: {asset} ({size} bytes)")
        path = asset.as_posix()
        if '"' in path or '\n' in path or '\r' in path:
            raise ValueError(f"Cannot represent PCM asset as an assembler path: {asset}")
        start, end = f"_binary_{symbol}_start", f"_binary_{symbol}_end"
        lines.extend((".balign 4", f".global {start}", f"{start}:", f'.incbin "{path}"',
                      f".global {end}", f"{end}:"))
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


# requested: Optional explicit native host compiler, never an ESP-IDF compiler.
# Raises FileNotFoundError or ValueError when no compatible host target is available.
def select_pet_compiler(requested):
    if requested:
        compiler = resolve_compiler(requested)
    else:
        # Clang/GNU support the device source's asm labels; the general host
        # selector prefers MSVC when both are present, so try compatible tools first.
        compiler = None
        for candidate in ("clang++", "g++"):
            try:
                compiler = resolve_compiler(candidate)
                break
            except (FileNotFoundError, ValueError):
                continue
        if compiler is None:
            try:
                compiler = prepare_host_compiler()
            except FileNotFoundError as error:
                raise FileNotFoundError("Cody Studio native pet audio requires an installed "
                                        "host clang++ or g++ compiler; MSVC cl alone is unsupported.") from error
            if Path(compiler).stem.lower() == "cl":
                # Visual Studio setup may have added an installed LLVM compiler.
                for candidate in ("clang++", "g++"):
                    try:
                        compiler = resolve_compiler(candidate)
                        break
                    except (FileNotFoundError, ValueError):
                        continue
    if Path(compiler).stem.lower() in ("cl", "clang-cl"):
        raise RuntimeError("Cody Studio native pet audio requires a host clang++ or g++ compiler; "
                           "MSVC cl cannot compile the firmware's GNU asm symbol declarations. "
                           "Install a compatible compiler or select one explicitly.")
    return compiler


# argv: Optional literal host compiler selection forwarded by the device CLI.
# Raises original compiler, installation or output errors; no playback is requested.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--native-pet", action="store_true",
                        help="Link the unchanged firmware pet selector and original PCM assets (metadata only).")
    args = parser.parse_args(argv)
    library = simulator_library_path(ROOT, "audio")
    compiler = (select_pet_compiler(args.compiler) if args.native_pet else
                resolve_compiler(args.compiler) if args.compiler else prepare_host_compiler())
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
    definitions = ()
    if args.native_pet:
        # Each run keeps its own input record; do not overwrite a prior build's assembly.
        pet_assembly = output / f"cody_host_pet_assets-{uuid.uuid4().hex}.S"
        write_pet_assembler(pet_assembly)
        sources += ("cody-device/components/CODY_companion/src/runtime/CODY_pet_audio.cpp",
                    "cody-device/components/LUNASHELL_platform/src/runtime/LUNASHELL_clock_policy.cpp",
                    pet_assembly)
        definitions = ("CODY_HOST_NATIVE_PET=1",)
    command = compile_command(compiler, includes, sources, library, definitions)
    command += simulator_library_flags(compiler, library)
    # Preserve earlier compile logs instead of replacing the previous run's evidence.
    run_stage(command, output, f"compile-{uuid.uuid4().hex}")
    print(f"Cody Studio audio driver compiled from real Cody modules: {library}")


if __name__ == "__main__":
    main()
