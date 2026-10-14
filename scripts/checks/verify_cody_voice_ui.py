"""Exercise Cody recording gestures, countdowns and ribbon with the production native raster."""
from pathlib import Path
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage


# path: Artifact-owned status path; each replacement contains complete JSON.
# report: Current raster/gesture harness status, including its actual failing stage.
# Exceptions: Filesystem and serialization errors propagate unchanged.
def write_report(path, report):
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


# No parameters: the public device CLI forwards compiler/output options.
# Exceptions: Original compiler, fixture, interruption and filesystem errors propagate.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Existing host C++ compiler; nothing is installed")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/checks/cody-voice-ui")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "artifacts"):
        raise ValueError("Cody voice UI output must remain under artifacts/")
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "report.json"
    report = {"result": "running", "stage": "compiler-discovery", "failed_stage": None,
              "scope": "production ribbon/countdown/gesture headers and native glyphs; SIMULATED gain and state captions, no PCM or physical UI proof"}
    write_report(report_path, report)
    try:
        compiler = resolve_compiler(args.compiler)
        report["compiler"] = compiler
        report["stage"] = "compile"
        write_report(report_path, report)
        executable = output / "cody-voice-ui.exe"
        command = compile_command(compiler,
            ["scripts/tests/cody_native", "components/CODY2D/include",
             "components/CODY_companion/include", "assets/cody/scenes/generated",
             "assets/cody/personality/generated", "assets/cody/personality-icons/generated"],
            ["scripts/tests/cody_native/voice_ui_contract.cpp", "components/CODY2D/src/CODY2D.cpp",
             "components/CODY2D/src/CODY2D_text.cpp", "assets/cody/scenes/generated/scene_body_font.cpp",
             "assets/cody/scenes/generated/scene_font.cpp"], executable, cxx_standard=20)
        run_stage(command, output, "compile")
        report["stage"] = "raster-and-gestures"
        write_report(report_path, report)
        run_stage([str(executable), str(output)], output, "raster-and-gestures")
        report["stage"] = "native-preview-encoding"
        write_report(report_path, report)
        from PIL import Image
        # Pillow only encodes actual native RGB565-derived pixels; it draws no artwork.
        frames = []
        for index in range(90):
            with Image.open(output / f"voice-{index:02d}.ppm") as native:
                frames.append(native.convert("RGB"))
        frames[45].save(output / "voice-recording-simulated.png")
        frames[0].save(output / "voice-recording-simulated.gif", save_all=True,
                       append_images=frames[1:], duration=33, loop=0)
        report["previews"] = ["voice-recording-simulated.png", "voice-recording-simulated.gif"]
        phases = ("preparing", "start-3", "start-2", "start-1", "send-3", "send-2", "send-1")
        for phase in phases:
            # The native fixture draws both the countdown and its SIMULATED
            # label. Encoding preserves those pixels without a host UI substitute.
            preview_name = f"voice-{phase}-simulated.png"
            with Image.open(output / f"voice-{phase}.ppm") as native:
                native.convert("RGB").save(output / preview_name)
            report["previews"].append(preview_name)
        report["countdown_and_preparing_preview_count"] = len(phases)
        report["stage"] = "complete"
        report["result"] = "passed"
        write_report(report_path, report)
    except BaseException as error:
        report["result"] = "failed"
        report["failed_stage"] = report["stage"]
        report["error"] = f"{type(error).__name__}: {error}"
        try:
            write_report(report_path, report)
        except Exception as report_error:
            print(f"Could not save failed Cody voice UI report: {report_error}", file=sys.stderr)
        raise
    print(f"Production Cody voice UI host checks passed; {report_path}")


if __name__ == "__main__":
    main()
