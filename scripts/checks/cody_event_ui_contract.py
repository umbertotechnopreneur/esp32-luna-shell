"""Compile real native event presentation without linking task, socket or device owners."""

from pathlib import Path

from scripts.common.cody_native_checks import compile_command, run_stage


# compiler: Existing compiler resolved by the explicitly requested parent suite.
# output: Parent artifact directory; all compilation and result files stay beneath it.
# Raises original compiler/runtime failures; this fixture never represents physical touch evidence.
def check_event_ui(compiler: str, output: Path) -> None:
    executable = output / "event-ui-contract.exe"
    includes = ("scripts/tests/cody_event_ui_stubs", "scripts/tests/cody_native",
                "components/CODY2D/include", "components/CODY_companion/include",
                "components/LUNASHELL_platform/include", "assets/cody/rig/generated",
                "assets/cody/scenes/generated", "assets/cody/appearance/jelly-scanline/generated",
                "assets/cody/audio/codex-actions/generated", "assets/cody/personality/generated")
    sources = ("scripts/tests/cody_native/event_ui_contract.cpp",
               "scripts/tests/cody_native/event_ui_owner_boundary.cpp",
               "components/CODY2D/src/CODY2D.cpp", "components/CODY2D/src/CODY2D_paths.cpp",
               "components/CODY2D/src/CODY2D_text.cpp", "components/CODY2D/src/CODY2D_controls.cpp",
               "components/CODY_companion/src/model/CODY_rig.cpp",
               "components/CODY_companion/src/scenes/CODY_scene.cpp",
               "assets/cody/scenes/generated/scene_font.cpp",
               "assets/cody/scenes/generated/scene_body_font.cpp")
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
