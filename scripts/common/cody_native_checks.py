"""Shared host compiler/execution owner for portable Cody checks."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


# requested: Existing host Clang/GCC/MSVC driver supplied as a literal executable path.
# Raises FileNotFoundError when no supported host compiler is available; nothing is installed.
def resolve_compiler(requested: str | None) -> str:
    candidates = (requested,) if requested else ("clang++", "g++", "cl")
    for candidate in candidates:
        compiler = shutil.which(candidate)
        if compiler:
            return compiler
    raise FileNotFoundError("Native Cody checks require an existing clang++, g++ or cl host compiler.")


# compiler: Explicit host driver, not an ESP-IDF workflow entrypoint.
# include_dirs: Literal include directories relative to the repository root.
# sources: Actual portable source files relative to the repository root.
# executable: Artifact-owned executable; intermediate objects stay alongside it.
# definitions: Explicit compile-time features for this portable host target.
# Raises ValueError for an executable outside the repository artifacts directory.
def compile_command(compiler, include_dirs, sources, executable, definitions=()):
    executable = Path(executable).resolve()
    if not executable.is_relative_to(ROOT / "artifacts"):
        raise ValueError("Native compiler output must remain under artifacts/")
    if Path(compiler).stem.lower() == "cl":
        command = [compiler, "/nologo", "/std:c++17", "/O2", "/W4", "/EHsc", "/MD"]
        command += [f"/D{definition}" for definition in definitions]
        command += [f"/I{ROOT / directory}" for directory in include_dirs]
        command += [str(ROOT / source) for source in sources]
        command += [f"/Fo{executable.parent}/", f"/Fe{executable}"]
    else:
        command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra"]
        command += [f"-D{definition}" for definition in definitions]
        command += [f"-I{ROOT / directory}" for directory in include_dirs]
        command += [str(ROOT / source) for source in sources]
        command += ["-o", str(executable)]
    return command


# command: Literal compiler/executable argument vector; never interpreted by a shell.
# output: Artifact directory retaining original stdout/stderr and a report on failure.
# name: Stable stage name for its owned output files.
# Raises OSError, subprocess.TimeoutExpired or RuntimeError for launch, timeout or stage failure.
def run_stage(command: list[str], output: Path, name: str) -> subprocess.CompletedProcess:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=240)
    (output / f"{name}.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (output / f"{name}.stderr.txt").write_text(result.stderr, encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(f"Native {name} failed ({result.returncode}); see {output / (name + '.stderr.txt')}")
    return result


