"""Focused host regression for the production SD observation freshness policy."""
from pathlib import Path

from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.common.cody_host_compiler import prepare_host_compiler

ROOT = Path(__file__).resolve().parents[2]


# requested_compiler: Optional existing host compiler; no installation is performed.
# Raises original compiler, process, filesystem or native contract failures.
def check_storage_observation(requested_compiler: str | None = None) -> None:
    compiler = resolve_compiler(requested_compiler) if requested_compiler else prepare_host_compiler()
    output = ROOT / "artifacts/checks/cody-storage"
    output.mkdir(parents=True, exist_ok=True)
    executable = output / "storage-observation-contract.exe"
    includes = (
        "cody-device/simulator/native/platform",
        "cody-device/components/LUNASHELL_platform/include",
        "cody-device/components/CODY_domain/include",
    )
    sources = (
        "scripts/tests/cody_native/storage_observation_contract.cpp",
        "cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_aging.cpp",
    )
    run_stage(compile_command(compiler, includes, sources, executable, cxx_standard=20), output, "compile")
    run_stage([str(executable)], output, "storage-observation")
    print(f"Cody storage observation check passed: {output}")
