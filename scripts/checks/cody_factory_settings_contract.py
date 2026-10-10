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
    # MSVC searches quoted includes through the including source's directories
    # before /I paths; keep this fixture outside cody_native's minimal nvs.h.
    sources = ("scripts/tests/factory_settings/factory_settings_contract.cpp",)
    command = compile_command(compiler, includes, sources, executable,
                              definitions=("CONFIG_ESP_TASK_WDT_TIMEOUT_S=12",), cxx_standard=20)
    run_stage(command, output, "factory-settings-compile")
    run_stage([str(executable)], output, "factory-settings")
