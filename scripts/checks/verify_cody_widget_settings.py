"""Build and run the shared widget contracts using an installed .NET SDK only."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "cody-desktop/widgets/codex-status/Tests/OpenAIUsage.Tests.csproj"


# argv: Options forwarded by scripts/device.ps1 check -Check cody-widget-settings.
# Raises OSError or subprocess errors for missing tools, failed compilation or failed contracts.
def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    dotnet = shutil.which("dotnet")
    if dotnet is None:
        raise RuntimeError("An installed .NET 10 SDK is required; this check installs nothing.")
    output = ROOT / "artifacts/checks/cody-widget-settings"
    output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    for variable, directory in {
        "DOTNET_CLI_HOME": "dotnet-home",
        "NUGET_PACKAGES": "nuget-packages",
        "NUGET_HTTP_CACHE_PATH": "nuget-http",
        "XDG_CACHE_HOME": "cache",
        "TMPDIR": "tmp",
        "TMP": "tmp",
        "TEMP": "tmp",
    }.items():
        destination = output / directory
        destination.mkdir(exist_ok=True)
        environment[variable] = str(destination)
    environment.update({
        "DOTNET_CLI_TELEMETRY_OPTOUT": "1",
        "DOTNET_SKIP_FIRST_TIME_EXPERIENCE": "1",
        "DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE": "1",
        "DOTNET_NOLOGO": "1",
        "MSBUILDDISABLENODEREUSE": "1",
    })
    # The shared project has no external package dependencies. Missing SDK packs
    # must fail locally instead of fetching or installing a toolchain.
    config = output / "offline-nuget.config"
    config.write_text("<configuration><packageSources><clear /></packageSources></configuration>\n", encoding="utf-8")
    build_root = output / "dotnet"
    subprocess.run([
        dotnet, "build", str(PROJECT), "--configuration", "Debug", "--artifacts-path", str(build_root),
        "--configfile", str(config), "--disable-build-servers", "-m:4", "--verbosity", "minimal",
        "-p:RuntimeIdentifiers=", "-p:RuntimeIdentifier=", "-p:NuGetAudit=false",
    ], cwd=ROOT, env=environment, check=True, timeout=300)
    executable = build_root / "bin/OpenAIUsage.Tests/debug/OpenAIUsage.Tests.dll"
    if not executable.is_file():
        raise RuntimeError("The widget contract build did not produce its expected executable.")
    subprocess.run([dotnet, str(executable)], cwd=ROOT, env=environment, check=True, timeout=60)
    print("Shared widget settings and account contracts passed; native Windows/GTK presentation is unverified.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Widget contract check failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
