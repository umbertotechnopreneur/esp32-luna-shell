"""Build the Windows fake output driver linked to unchanged Cody audio modules."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage


# No parameters: loads an installed MSVC toolchain into this generator process only.
# Raises FileNotFoundError or RuntimeError for missing installation or setup failure.
def prepare_host_compiler():
    try:
        return resolve_compiler(None)
    except FileNotFoundError:
        locator = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        installation = subprocess.run([str(locator), "-latest", "-prerelease", "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            capture_output=True, text=True, check=True).stdout.strip()
        if not installation:
            raise FileNotFoundError("An installed Windows C++ compiler is required; nothing is installed automatically.")
        setup = Path(installation) / "Common7/Tools/VsDevCmd.bat"
        # Only the fixed installed setup script enters cmd; environment values are never logged.
        command = f'cmd.exe /d /s /c ""{setup}" -no_logo -arch=x64 -host_arch=x64 >nul && set"'
        # A literal Windows command line preserves cmd's outer quotes; list2cmdline
        # would escape the batch path for a different argument parser.
        result = subprocess.run(command,
            capture_output=True, text=True, check=True)
        for line in result.stdout.splitlines():
            name, separator, value = line.partition("=")
            if separator and name and not name.startswith("="):
                os.environ[name] = value
        return resolve_compiler(None)


# argv: Optional literal host compiler selection forwarded by the device CLI.
# Raises original compiler, installation or output errors; no playback is requested.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    args = parser.parse_args(argv)
    compiler = resolve_compiler(args.compiler) if args.compiler else prepare_host_compiler()
    output = ROOT / "artifacts/build-cody-simulator-audio"
    output.mkdir(parents=True, exist_ok=True)
    library = output / "cody_fake_audio.dll"
    includes = ("scripts/tests/cody_native", "components/CODY_companion/include",
        "components/CODY2D/include", "components/LUNASHELL_platform/include",
        "assets/cody/personality/generated", "assets/cody/personality-icons/audio",
        "assets/cody/rig/generated")
    sources = ("simulator/native/CODY_fake_audio.cpp",
        "components/CODY_companion/src/scenes/CODY_typewriter_audio.cpp",
        "components/CODY_companion/src/runtime/CODY_personality_audio.cpp",
        "assets/cody/personality-icons/audio/personality_audio.cpp")
    command = compile_command(compiler, includes, sources, library)
    if Path(compiler).stem.lower() == "cl":
        command += ["/LD", "/link", f"/IMPLIB:{output / 'cody_fake_audio.lib'}"]
    else:
        command += ["-shared"]
    run_stage(command, output, "compile")
    print(f"Windows fake audio driver compiled from real Cody modules: {library}")


if __name__ == "__main__":
    main()
