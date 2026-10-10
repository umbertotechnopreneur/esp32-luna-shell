"""Host-only compiler discovery shared by Cody driver generators."""
import os
from pathlib import Path
import subprocess
from scripts.common.cody_native_checks import resolve_compiler


# No parameters: loads an installed MSVC toolchain into this generator process only.
# Raises OSError or subprocess.CalledProcessError for missing installation or setup failure.
def prepare_host_compiler():
    try:
        return resolve_compiler(None)
    except FileNotFoundError:
        program_files = os.environ.get("ProgramFiles(x86)")
        if not program_files:
            raise FileNotFoundError("Windows compiler discovery requires ProgramFiles(x86).")
        locator = Path(program_files) / "Microsoft Visual Studio/Installer/vswhere.exe"
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
