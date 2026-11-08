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

"""Compile real native event presentation without linking task, socket or device owners."""

from pathlib import Path

from scripts.common.cody_native_checks import compile_command, run_stage


# compiler: Existing compiler resolved by the explicitly requested parent suite.
# output: Parent artifact directory; all compilation and result files stay beneath it.
# Raises original compiler/runtime failures; this fixture never represents physical touch evidence.
def check_event_ui(compiler: str, output: Path) -> None:
    executable = output / "event-ui-contract.exe"
    includes = ("scripts/tests/cody_event_ui_stubs", "scripts/tests/cody_native",
                "cody-device/components/CODY2D/include", "cody-device/components/CODY_companion/include",
                "cody-device/components/CODY_companion/src/scenes",
                "cody-device/components/LUNASHELL_platform/include", "cody-device/assets/cody/rig/generated",
                "cody-device/assets/cody/scenes/generated", "cody-device/assets/cody/appearance/jelly-scanline/generated",
                "cody-device/assets/cody/audio/codex-actions/generated", "cody-device/assets/cody/personality/generated")
    includes += ("cody-device/assets/cody/home-dashboard-fonts/generated",)
    sources = ("scripts/tests/cody_native/event_ui_contract.cpp",
               "scripts/tests/cody_native/event_ui_owner_boundary.cpp",
               "scripts/tests/shared_event_presentation.cpp",
               "cody-device/components/LUNASHELL_platform/src/network/LUNASHELL_codex_events.cpp",
               "cody-device/components/CODY2D/src/CODY2D.cpp", "cody-device/components/CODY2D/src/CODY2D_paths.cpp",
               "cody-device/components/CODY2D/src/CODY2D_text.cpp", "cody-device/components/CODY2D/src/CODY2D_controls.cpp",
               "cody-device/components/CODY_companion/src/model/CODY_rig.cpp",
               "cody-device/components/CODY_companion/src/scenes/CODY_scene.cpp",
               "cody-device/components/CODY_companion/src/scenes/CODY_technical_panels.cpp",
               "cody-device/components/CODY_companion/src/scenes/CODY_home_dashboard.cpp",
               "cody-device/components/CODY_companion/src/scenes/CODY_home_identity.cpp",
               "cody-device/assets/cody/home-dashboard-fonts/generated/home_dashboard_fonts.cpp",
               "cody-device/assets/cody/scenes/generated/scene_font.cpp",
               "cody-device/assets/cody/scenes/generated/scene_body_font.cpp")
    command = compile_command(compiler, includes, sources, executable,
                              definitions=("CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX=1",
                                           "CODY_ENABLE_PEARL_JELLY=0"), cxx_standard=20)
    # Every excluded owner aborts if actually called. This also keeps MSVC's
    # symbol resolution strict before its unused-function removal takes place.
    if Path(compiler).stem.lower() == "cl":
        command.insert(1, "/Gy")
        command.extend(("/link", "/OPT:REF", "/INCREMENTAL:NO"))
    else:
        command[1:1] = ["-ffunction-sections", "-fdata-sections"]
        command.append("-Wl,--gc-sections")
    run_stage(command, output, "event-ui-compile")
    run_stage([str(executable)], output, "event-ui")
