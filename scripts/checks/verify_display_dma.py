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

"""Run actual standalone BSP frame functions against a deterministic host DMA transport."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import resolve_compiler, run_stage


# source: Actual production C source, never a copied implementation in the test.
# signature: Unique function definition prefix including its return type.
# Raises ValueError when the function is missing or its body is unbalanced.
def function_text(source: str, signature: str) -> str:
    # A prior declaration is not executable code; skip signatures ending in a semicolon.
    match = re.search(re.escape(signature) + r"[^;{]*\{", source)
    if not match:
        raise ValueError(f"Missing function definition: {signature}")
    start = match.start()
    body = match.end() - 1
    depth = 0
    for index in range(body, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise ValueError(f"Unbalanced function: {signature}")


# source: Production header/source containing a named anonymous struct typedef.
# name: Exact typedef identity extracted with its original fields.
# Raises ValueError when the requested declaration is absent.
def struct_text(source: str, name: str) -> str:
    match = re.search(r"typedef struct \{[^{}]*\}\s*" + re.escape(name) + r";", source)
    if not match:
        raise ValueError(f"Missing typedef: {name}")
    return match.group()


# output: Artifact-owned directory for extracted source and SDK boundary headers.
# Raises OSError or ValueError for unavailable sources or changed production boundaries.
def prepare_source(output: Path) -> dict:
    bsp_path = ROOT / "components/esp32_s3_touch_amoled_1_75/esp32_s3_touch_amoled_1_75.c"
    bsp = bsp_path.read_text(encoding="utf-8")
    header = (bsp_path.parent / "include/bsp/display.h").read_text(encoding="utf-8")
    declarations = struct_text(bsp, "bsp_display_frame_t") + "\nstatic bsp_display_frame_t display_frame;\n"
    declarations += "typedef uint32_t bsp_display_rgb565_pair_t;\n"
    signatures = (
        "static bool IRAM_ATTR bsp_display_color_transfer_done(",
        "esp_err_t bsp_display_frame_begin(",
        "esp_err_t bsp_display_frame_check_deadline(",
        "static void bsp_display_frame_quarantine(",
        "static esp_err_t bsp_display_frame_retire_transfer(",
        "esp_err_t bsp_display_frame_buffer(",
        "esp_err_t bsp_display_frame_submit_rgb565(",
        "esp_err_t bsp_display_frame_end(",
        "static inline void bsp_display_copy_rgb565_to_panel_order(",
        "static esp_err_t bsp_display_write_rgb565_internal(",
        "esp_err_t bsp_display_write_rgb565(",
    )
    extracted = declarations + "\n\n".join(function_text(bsp, signature) for signature in signatures)
    (output / "driver_under_test.inc").write_text(extracted + "\n", encoding="utf-8")
    stub = output / "stubs/bsp"
    stub.mkdir(parents=True, exist_ok=True)
    constants = "\n".join(line for line in header.splitlines()
                          if re.match(r"#define BSP_LCD_(H_RES|V_RES|BITS_PER_PIXEL)\b", line))
    order = re.search(r"typedef enum \{[^{}]*\}\s*bsp_display_rgb565_byte_order_t;", header)
    if not order:
        raise ValueError("Missing production RGB565 representation enum")
    (stub / "display.h").write_text(
        "#pragma once\n#include <stdint.h>\n#include \"esp_err.h\"\n" + constants + "\n" +
        order.group() + "\n" + struct_text(header, "bsp_display_frame_result_t") + "\n", encoding="utf-8")
    # Error values belong to the host SDK boundary; driver control flow remains unchanged.
    (stub.parent / "esp_err.h").write_text(
        "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n" +
        "\n".join(f"#define {name} {value}" for name, value in (
            ("ESP_ERR_NO_MEM", 0x101), ("ESP_ERR_INVALID_ARG", 0x102),
            ("ESP_ERR_INVALID_STATE", 0x103), ("ESP_ERR_INVALID_SIZE", 0x104),
            ("ESP_ERR_NOT_SUPPORTED", 0x106), ("ESP_ERR_TIMEOUT", 0x107))) + "\n", encoding="utf-8")
    dma = (bsp_path.parent / "bsp_display_dma.c").read_text(encoding="utf-8")
    (output / "dma_owner_under_test.inc").write_text(dma, encoding="utf-8")
    for name in ("sdkconfig.h", "esp_attr.h", "esp_heap_caps.h", "esp_memory_utils.h", "esp_log.h"):
        (stub.parent / name).write_text("#pragma once\n", encoding="utf-8")
    platform = (ROOT / "components/LUNASHELL_platform/src/display/LUNASHELL_display_runtime.cpp").read_text(encoding="utf-8")
    end = function_text(platform, 'extern "C" esp_err_t LUNASHELL_display_frame_end(')
    if end.index("bsp_display_frame_end(") >= end.index("LUNASHELL_liveness_watch_end("):
        raise ValueError("Display watchdog ends before the final DMA retires")
    cody = (ROOT / "components/CODY_companion/src/runtime/CODY_companion.cpp").read_text(encoding="utf-8")
    draw = function_text(cody, "DrawResult draw_companion_frame(")
    loop = draw[draw.index("for (uint32_t row = 0;"):]
    if loop.index("LUNASHELL_display_frame_buffer(") >= loop.index("const CODY2D::Surface strip{"):
        raise ValueError("Renderer does not reborrow an idle slot before rasterizing")
    if "LUNASHELL_display_frame_submit_rgb565(" not in loop:
        raise ValueError("Renderer no longer uses frame DMA submission")
    if loop.index("frame.finish(") >= loop.index("*s_resources.previous_rig ="):
        raise ValueError("Reference geometry is committed before frame retirement")
    return {"production_source_sha256": hashlib.sha256(bsp.encode()).hexdigest(),
            "production_allocator_sha256": hashlib.sha256(dma.encode()).hexdigest(),
            "executed_functions": list(signatures),
            "integration": ["watchdog spans DMA drain", "idle slot reborrow before raster",
                            "reference geometry committed after frame retirement"]}


# argv: Literal options supplied through device.ps1 check -Check display-dma.
# Raises original compiler/assertion/filesystem errors after preserving the report.
def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Existing MSVC, Clang or GCC driver; nothing is installed")
    args = parser.parse_args(argv)
    output = ROOT / "artifacts/checks/display-dma"
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "failed", "scope": "actual BSP control flow with host transport; physical behavior unverified"}
    try:
        report.update(prepare_source(output))
        compiler = resolve_compiler(args.compiler)
        executable = output / "display-dma-contract.exe"
        includes = (output, output / "stubs", ROOT / "components/esp32_s3_touch_amoled_1_75")
        source = ROOT / "scripts/tests/display_dma/frame_contract.c"
        if Path(compiler).stem.lower() in ("cl", "cl.exe"):
            command = [compiler, "/nologo", "/TC", "/std:c11", "/O2", "/W4"]
            command += [f"/I{directory}" for directory in includes]
            command += [str(source), f"/Fo{output}/", f"/Fe{executable}"]
        else:
            command = [compiler, "-x", "c", "-std=c11", "-O2", "-Wall", "-Wextra"]
            command += [f"-I{directory}" for directory in includes]
            command += [str(source), "-o", str(executable)]
        report["compiler"] = compiler
        run_stage(command, output, "compile")
        executed = run_stage([str(executable)], output, "contract")
        report["contracts"] = executed.stdout.splitlines()
        report["status"] = "passed"
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Display DMA contracts passed. Report: {output / 'report.json'}")


if __name__ == "__main__":
    main()
