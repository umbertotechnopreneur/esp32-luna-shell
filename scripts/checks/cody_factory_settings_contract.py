"""Compile the real settings owner with isolated NVS and executor test doubles."""

from pathlib import Path

from scripts.common.cody_native_checks import compile_command, run_stage


# compiler: Existing host compiler admitted by the parent cody-runtime suite.
# output: Parent artifact directory for the executable and captured evidence.
# Raises original compile/runtime failures; this does not access or reset hardware.
def check_factory_settings(compiler: str, output: Path) -> None:
    executable = output / "factory-settings-contract.exe"
    includes = ("scripts/tests/factory_settings_stubs", "scripts/tests/cody_native",
                "components/LUNASHELL_platform/include")
    sources = ("scripts/tests/cody_native/factory_settings_contract.cpp",)
    command = compile_command(compiler, includes, sources, executable,
                              definitions=("CONFIG_ESP_TASK_WDT_TIMEOUT_S=12",), cxx_standard=20)
    run_stage(command, output, "factory-settings-compile")
    run_stage([str(executable)], output, "factory-settings")
