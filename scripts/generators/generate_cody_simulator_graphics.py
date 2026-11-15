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

"""Build the PC graphics driver from Cody's production C++ sources."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.common.cody_host_compiler import prepare_host_compiler, simulator_library_flags
from runtime_paths import simulator_library_path
from scripts.generators.generate_cody_simulator_audio import PET_ASSETS, select_pet_compiler, write_pet_assembler
from scripts.generators.cody_host_device_assets import IMAGES, write_device_image_assembler

SOURCES = (
    "cody-device/simulator/native/CODY_host_graphics.cpp",
    "cody-device/simulator/native/CODY_host_display.cpp",
    "cody-device/simulator/native/CODY_host_bsp_display.cpp",
    "cody-device/simulator/native/CODY_host_qrcode.cpp",
    "cody-device/simulator/native/CODY_host_pairing.cpp",
    "cody-device/simulator/native/CODY_host_wifi.cpp",
    "cody-device/simulator/native/CODY_host_codex_services.cpp",
    "cody-device/simulator/native/CODY_host_optional_services.cpp",
    "cody-device/simulator/native/CODY_host_input.cpp",
    "cody-device/simulator/native/CODY_host_power.cpp",
    "cody-device/simulator/native/CODY_host_orientation_unavailable.cpp",
    "cody-device/simulator/native/CODY_host_time_owner.cpp",
    "cody-device/simulator/native/CODY_host_time_compat.cpp",
    "cody-device/simulator/native/CODY_host_time_io.cpp",
    "cody-device/simulator/native/CODY_host_personality_behavior.cpp",
    "cody-device/simulator/native/CODY_host_nvs.cpp",
    "cody-device/simulator/native/CODY_host_settings_bootstrap.cpp",
    "cody-device/simulator/native/CODY_host_liveness.cpp",
    "cody-device/components/LUNASHELL_platform/src/runtime/LUNASHELL_settings.cpp",
    "cody-device/components/LUNASHELL_platform/src/display/LUNASHELL_display_settings.cpp",
    "cody-device/simulator/native/CODY_host_ccp.cpp",
    "cody-device/simulator/native/CODY_host_event_presentation.cpp",
    "cody-device/simulator/native/CODY_host_standby.cpp",
    "cody-device/simulator/native/CODY_host_snapshot.cpp",
    "cody-device/simulator/native/CODY_host_onboarding_snapshot.cpp",
    "cody-device/simulator/native/CODY_host_studio_domain.cpp",
    "cody-device/simulator/native/CODY_host_boot_branding.cpp",
    "cody-device/simulator/native/CODY_host_muse.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_muse_frame.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_pixel_avatar.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_pixel_demo.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_guide_presenter.cpp",
    "cody-device/components/CODY_companion/src/runtime/CODY_settings.cpp",
    "cody-device/components/CODY_companion/src/runtime/CODY_settings_service.cpp",
    "cody-device/components/CODY_domain/src/CODY_state_receive.cpp",
    "cody-device/components/CODY_domain/src/CODY_state_json.cpp",
    "cody-device/components/CODY_domain/src/CODY_state_store.cpp",
    "cody-device/components/LUNASHELL_platform/src/network/LUNASHELL_codex_events.cpp",
    "cody-device/components/LUNASHELL_platform/src/network/LUNASHELL_codex_transaction.cpp",
    "cody-device/components/LUNASHELL_platform/src/network/LUNASHELL_pairing_service.cpp",
    "cody-device/components/LUNASHELL_platform/src/power/LUNASHELL_standby_resources.cpp",
    "cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_rtc_calendar.cpp",
    "cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_aging.cpp",
    "cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_names.cpp",
    "cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_reports.cpp",
    "cody-device/components/LUNASHELL_platform/src/runtime/LUNASHELL_clock_policy.cpp",
    "cody-device/components/LUNASHELL_platform/src/http/LUNASHELL_http_json.cpp",
    "cody-device/components/CODY2D/src/CODY2D.cpp",
    "cody-device/components/CODY2D/src/CODY2D_paths.cpp",
    "cody-device/components/CODY2D/src/CODY2D_controls.cpp",
    "cody-device/components/CODY2D/src/CODY2D_text.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_rig.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_expression_catalog.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_response.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_character_direction.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_happiness.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_petting.cpp",
    "cody-device/components/CODY_companion/src/model/CODY_connection_personality.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_dirty_plan.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_personality.cpp",
    "cody-device/components/CODY_companion/src/rendering/CODY_jelly.cpp",
    "cody-device/components/CODY_companion/src/diagnostics/CODY_debug_views.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_scene.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_technical_panels.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_home_dashboard.cpp",
    "cody-device/components/CODY_companion/src/scenes/CODY_home_identity.cpp",
    "cody-device/assets/cody/home-dashboard-fonts/generated/home_dashboard_fonts.cpp",
    "cody-device/assets/cody/personality-icons/generated/personality_icons.cpp",
    "cody-device/assets/cody/appearance/jelly-scanline/generated/cody_jelly_data.cpp",
    "cody-device/assets/cody/scenes/generated/scene_font.cpp",
    "cody-device/assets/cody/scenes/generated/scene_body_font.cpp",
)
MUSE_DIR = "cody-device/components/CODY_companion/third_party/muse"
MUSE_SOURCE = f"{MUSE_DIR}/muse_pixel.c"
QR_DIR = "cody-device/managed_components/espressif__qrcode"
QR_SOURCE = f"{QR_DIR}/qrcodegen.c"
INCLUDES = (
    "cody-device/simulator/native/platform",
    "cody-device/components/CODY2D/include",
    "cody-device/components/CODY_companion/include",
    "cody-device/components/CODY_domain/include",
    MUSE_DIR,
    "cody-device/components/CODY_companion/src/scenes",
    "cody-device/components/CODY_companion/src/runtime",
    "cody-device/components/LUNASHELL_boot/include",
    "cody-device/components/LUNASHELL_logger/include",
    "cody-device/components/LUNASHELL_platform/include",
    "cody-device/components/LUNASHELL_platform/src/snapshot",
    "cody-device/components/LUNASHELL_platform/src/power",
    "cody-device/assets/cody/rig/generated",
    "cody-device/assets/cody/scenes/generated",
    "cody-device/assets/cody/home-dashboard-fonts/generated",
    "cody-device/assets/cody/personality/generated",
    "cody-device/assets/cody/personality-icons/generated",
    "cody-device/assets/cody/appearance/jelly-scanline/generated",
    "cody-device/assets/branding/VibeWare/generated",
    QR_DIR,
    f"{QR_DIR}/include",
)
DEVICE_ONLY = frozenset((
    "cody-device/simulator/native/CODY_host_display.cpp",
    "cody-device/simulator/native/CODY_host_bsp_display.cpp",
    "cody-device/simulator/native/CODY_host_qrcode.cpp",
    "cody-device/simulator/native/CODY_host_pairing.cpp",
    "cody-device/simulator/native/CODY_host_wifi.cpp",
    "cody-device/simulator/native/CODY_host_codex_services.cpp",
    "cody-device/simulator/native/CODY_host_optional_services.cpp",
    "cody-device/simulator/native/CODY_host_input.cpp",
    "cody-device/simulator/native/CODY_host_power.cpp",
    "cody-device/simulator/native/CODY_host_orientation_unavailable.cpp",
    "cody-device/simulator/native/CODY_host_time_owner.cpp",
    "cody-device/simulator/native/CODY_host_time_compat.cpp",
    "cody-device/simulator/native/CODY_host_time_io.cpp",
    "cody-device/simulator/native/CODY_host_nvs.cpp",
    "cody-device/simulator/native/CODY_host_settings_bootstrap.cpp",
    "cody-device/simulator/native/CODY_host_liveness.cpp",
    "cody-device/simulator/native/CODY_host_onboarding_snapshot.cpp",
    "cody-device/simulator/native/CODY_host_studio_domain.cpp",
    "cody-device/components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_reports.cpp",
    "cody-device/components/LUNASHELL_platform/src/runtime/LUNASHELL_settings.cpp",
    "cody-device/components/LUNASHELL_platform/src/display/LUNASHELL_display_settings.cpp",
    "cody-device/components/LUNASHELL_platform/src/network/LUNASHELL_pairing_service.cpp",
    "cody-device/components/CODY_companion/src/runtime/CODY_settings.cpp",
    "cody-device/components/CODY_companion/src/runtime/CODY_settings_service.cpp",
))
BASE_DEFINITIONS = ("CODY_ENABLE_PEARL_JELLY=1", "CODY_HOST_SIMULATOR=1")
DEVICE_DEFINITIONS = ("CODY_HOST_DEVICE_RUNTIME=1",
    "CODY_DEVICE_UI_ONLY=1", "CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION=1",
    "CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY=1", "CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH=1",
    "CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS=1",
    "CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX=1",
    "CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD=1", "CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT=1",
    "CONFIG_LUNASHELL_BUILD_ENABLE_WIFI=1", "CONFIG_LUNASHELL_BUILD_ENABLE_PROVISIONING=1",
    "CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH=1", "CODY_HOST_REAL_AUDIO=1",
    "CONFIG_LWIP_SNTP_MAX_SERVERS=3",
    "CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO=1", "CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311=1",
    "CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE=1")
DEVICE_AUDIO_SOURCES = (
    "cody-device/components/LUNASHELL_platform/src/audio/LUNASHELL_audio.cpp",
    "cody-device/components/CODY_companion/src/runtime/CODY_pet_audio.cpp",
    "cody-device/simulator/native/CODY_host_audio_codec.cpp",
    "cody-device/simulator/native/CODY_host_audio_winmm.cpp",
    "cody-device/simulator/native/CODY_host_audio_alsa.cpp",
    "cody-device/simulator/native/CODY_host_audio_afe.cpp",
    "cody-device/simulator/native/CODY_host_audio_runtime.cpp",
)
DEVICE_UI_SOURCES = tuple("cody-device/components/CODY_companion/" + source for source in (
    "src/runtime/CODY_device_ui.cpp", "src/runtime/CODY_pet_service.cpp",
    "src/runtime/CODY_device_model.cpp", "src/runtime/CODY_device_effects.cpp",
    "src/model/CODY_battery_alerts.cpp", "src/scenes/CODY_onboarding.cpp",
    "src/scenes/CODY_device_view.cpp", "src/scenes/CODY_codex_panels.cpp",
    "src/scenes/CODY_codex_browser.cpp", "src/scenes/CODY_system_info.cpp",
    "src/scenes/CODY_audio_view.cpp", "src/scenes/CODY_prices_view.cpp",
    "src/scenes/CODY_text_entry.cpp", "src/scenes/CODY_screensaver.cpp",
    "src/scenes/CODY_reset_countdown.cpp", "src/scenes/CODY_reset_countdown_screen.cpp",
    "src/model/CODY_settings_view.cpp", "src/scenes/CODY_wifi_list_presenter.cpp",
    "src/runtime/CODY_codex_audio.cpp", "src/runtime/CODY_notifications.cpp",
    "src/runtime/CODY_whatsapp_audio.cpp", "src/runtime/CODY_whatsapp_service.cpp",
    "src/scenes/CODY_account_dashboard.cpp", "src/scenes/CODY_token_history.cpp",
    "src/diagnostics/CODY_status_overlay.cpp",
)) + ("cody-device/assets/cody/audio/codex-actions/generated/codex_audio_assets.cpp",)


# path: Exact source or library file whose byte identity is recorded.
# Raises OSError for unreadable inputs; absence is never replaced with a digest.
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# No parameters: gathers compiled sources, production headers and authored catalogs.
# Raises OSError for unreadable source trees or missing required inputs.
def source_provenance(device_runtime=False):
    paths = {ROOT / source for source in (*SOURCES, MUSE_SOURCE, QR_SOURCE, f"{QR_DIR}/qrcodegen.h")}
    paths.add(ROOT / "cody-device/simulator/native/CODY_host_settings.cpp")
    if device_runtime:
        paths.update(ROOT / source for source in DEVICE_AUDIO_SOURCES)
        paths.update(ROOT / source for source in DEVICE_UI_SOURCES)
        paths.update(ROOT / path for path in (
            "cody-device/simulator/native/CODY_host_device_bootstrap.hpp",
            "cody-device/simulator/native/CODY_host_audio_codec.hpp",
            "cody-device/simulator/native/CODY_host_studio_statistics.hpp",
            "cody-device/simulator/native/CODY_host_time_compat.hpp",
            "cody-device/simulator/native/CODY_host_bsp_display.hpp",
            "cody-device/components/LUNASHELL_platform/src/runtime/LUNASHELL_time.cpp",
        ))
        paths.update(ROOT / path for _, path in PET_ASSETS)
        paths.update(ROOT / path for _, path, _ in IMAGES)
        paths.add(ROOT / "scripts/generators/cody_host_device_assets.py")
        paths.add(ROOT / "scripts/generators/generate_cody_simulator_audio.py")
        paths.update(path for path in (ROOT / "cody-device/simulator/native/audio_platform").rglob("*")
                     if path.is_file() and path.suffix in (".h", ".hpp"))
    for directory in INCLUDES:
        paths.update(path for path in (ROOT / directory).rglob("*")
                     if path.is_file() and path.suffix in (".h", ".hpp"))
    paths.update(ROOT / name for name in (
        "cody-device/assets/cody/palette/theme.json",
        "cody-device/assets/cody/cody.yaml", "cody-device/assets/cody/scenes/scenes.json",
        "cody-device/assets/cody/navigation.json", "cody-device/assets/cody/personality.yaml",
        "cody-device/assets/cody/appearance/jelly-scanline/skins.json",
        "scripts/generators/generate_cody_simulator_graphics.py",
        "cody-device/simulator/native/CODY_host_snapshot.hpp",
        "cody-device/simulator/native/CODY_host_onboarding_snapshot.hpp",
        "cody-device/simulator/native/CODY_host_pairing.hpp",
        "cody-device/simulator/native/CODY_host_wifi.hpp",
        "cody-device/simulator/native/CODY_host_display.hpp",
        "cody-device/components/LUNASHELL_boot/src/ui/VIBEWARE_boot_animation.c",
        "cody-device/assets/branding/VibeWare/generated/vibeware_boot_data.c",
        "scripts/common/cody_native_checks.py",
        "scripts/common/cody_host_compiler.py",
        "cody-device/simulator/windows/runtime_paths.py",
    ))
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(paths)}


# compiler: Existing native host driver.
# output: Artifact-owned temporary object to link into the C++ library.
def muse_compile_command(compiler, output):
    source = str(ROOT / MUSE_SOURCE)
    include = str(ROOT / MUSE_DIR)
    if Path(compiler).stem.lower() == "cl":
        return [compiler, "/nologo", "/TC", "/std:c11", "/O2", "/MD", "/c",
                f"/I{include}", source, f"/Fo{output}"]
    return [compiler, "-x", "c", "-std=c11", "-O2", *(["-fPIC"] if sys.platform != "win32" else []), "-c",
            f"-I{include}", source, "-o", str(output)]


# compiler: Existing host C compiler driver.
# output: Artifact-owned QR object; links Espressif's unmodified C encoder.
# Exception: Propagates compiler availability or execution failures to the caller.
def qr_compile_command(compiler, output):
    source = str(ROOT / QR_SOURCE)
    include = str(ROOT / QR_DIR)
    if Path(compiler).stem.lower() == "cl":
        return [compiler, "/nologo", "/TC", "/std:c11", "/O2", "/MD", "/c",
                f"/I{include}", source, f"/Fo{output}"]
    return [compiler, "-x", "c", "-std=c11", "-O2", *(["-fPIC"] if sys.platform != "win32" else []), "-c",
            f"-I{include}", source, "-o", str(output)]


# argv: Optional existing compiler selection and explicit cache rebuild request.
# Raises original compiler, file, timeout or build errors; no simulator is executed.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--device-runtime", action="store_true",
                        help="Build the isolated, experimental native device-owner variant")
    args = parser.parse_args(argv)
    library = simulator_library_path(ROOT, "graphics-device" if args.device_runtime else "graphics")
    output = library.parent
    manifest_path = output / "manifest.json"
    compiler = select_pet_compiler(args.compiler) if args.device_runtime else (
        resolve_compiler(args.compiler) if args.compiler else prepare_host_compiler())
    inputs = source_provenance(args.device_runtime)
    output.mkdir(parents=True, exist_ok=True)
    muse_object = output / "muse_pixel.obj"
    qr_object = output / "qrcodegen.obj"
    muse_command = muse_compile_command(compiler, muse_object)
    qr_command = qr_compile_command(compiler, qr_object)
    legacy_only = frozenset((
        "cody-device/simulator/native/CODY_host_graphics.cpp",
        "cody-device/simulator/native/CODY_host_personality_behavior.cpp",
        "cody-device/simulator/native/CODY_host_event_presentation.cpp",
        "cody-device/components/CODY_companion/src/model/CODY_expression_catalog.cpp",
        "cody-device/components/CODY_companion/src/rendering/CODY_jelly.cpp",
    ))
    sources = tuple(source for source in SOURCES if source not in legacy_only) if args.device_runtime else tuple(source for source in SOURCES if source not in DEVICE_ONLY) + (
        "cody-device/simulator/native/CODY_host_settings.cpp",)
    # Stable symbolic names make the cache key independent of temporary inputs.
    pet_template = output / "{pet-assembly}.S" if args.device_runtime else None
    image_template = output / "{image-assembly}.S" if args.device_runtime else None
    pet_object_template = output / "{pet-object}.obj" if args.device_runtime else None
    image_object_template = output / "{image-object}.obj" if args.device_runtime else None
    objects = (muse_object, qr_object, pet_object_template, image_object_template) if args.device_runtime else (muse_object,)
    definitions = (("CODY_ENABLE_PEARL_JELLY=0", "CODY_HOST_SIMULATOR=1") + DEVICE_DEFINITIONS
                   if args.device_runtime else BASE_DEFINITIONS)
    includes = (("cody-device/simulator/native/audio_platform",) + INCLUDES +
                ("cody-device/assets/cody/audio/codex-actions/generated",)) if args.device_runtime else INCLUDES
    template_command = compile_command(compiler, includes, (*sources, *(DEVICE_AUDIO_SOURCES if args.device_runtime else ()),
                                              *(DEVICE_UI_SOURCES if args.device_runtime else ()),
                                              *objects), library, definitions, cxx_standard=20)
    if args.device_runtime and sys.platform == "win32":
        template_command[1:1] = ["-include", str(ROOT / "cody-device/simulator/native/platform/cody_host_posix_time.h")]
    template_command += simulator_library_flags(compiler, library)
    if args.device_runtime:
        template_command += (["-lwinmm", "-Wl,/errorlimit:0"] if sys.platform == "win32" else ["-ldl"])
    pet_asm_command = [compiler, "-x", "assembler", "-c", str(pet_template), "-o", str(pet_object_template)]
    image_asm_command = [compiler, "-x", "assembler", "-c", str(image_template), "-o", str(image_object_template)]
    commands = [muse_command, qr_command, pet_asm_command, image_asm_command, template_command] if args.device_runtime else [muse_command, template_command]
    # A cached library is usable only when both its sources and its binary agree.
    if not args.force and manifest_path.is_file() and library.is_file():
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        if (previous.get("schema") == "cody.host-graphics.v1" and
                previous.get("inputs") == inputs and previous.get("command") == commands and
                previous.get("library_sha256") == digest(library)):
            print(f"Cody C++ graphics driver already current: {library}")
            return
    # Keep the previous manifest. Its source and library hashes make the loader
    # reject a stale or partially rewritten driver after a failed compilation.
    result = subprocess.run(muse_command, cwd=ROOT, capture_output=True, text=True, timeout=240)
    if result.returncode != 0:
        raise RuntimeError(f"Muse C compilation failed ({result.returncode}):\n{result.stdout}{result.stderr}")
    command = list(template_command)
    if args.device_runtime:
        pet_assembly = output / f"cody_host_pet_{uuid.uuid4().hex}.S"
        image_assembly = output / f"cody_host_images_{uuid.uuid4().hex}.S"
        pet_object = pet_assembly.with_suffix(".obj")
        image_object = image_assembly.with_suffix(".obj")
        command = [part.replace(str(pet_object_template), str(pet_object)).replace(str(image_object_template), str(image_object))
                   for part in command]
        write_pet_assembler(pet_assembly)
        write_device_image_assembler(image_assembly)
        result = subprocess.run(qr_command, cwd=ROOT, capture_output=True, text=True, timeout=240)
        if result.returncode != 0:
            raise RuntimeError(f"QR C compilation failed ({result.returncode}):\n{result.stdout}{result.stderr}")
        run_stage([compiler, "-x", "assembler", "-c", str(pet_assembly), "-o", str(pet_object)], output, "pet-assembly")
        run_stage([compiler, "-x", "assembler", "-c", str(image_assembly), "-o", str(image_object)], output, "image-assembly")
    run_stage(command, output, "compile")
    manifest = {"schema": "cody.host-graphics.v1", "inputs": inputs,
                "command": commands, "library_sha256": digest(library)}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Cody C++ graphics driver compiled: {library}")


if __name__ == "__main__":
    main()
