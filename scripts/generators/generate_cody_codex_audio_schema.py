"""Export the installed public Codex protocol for explicit audio compatibility review."""

from pathlib import Path
import argparse
import shutil
import subprocess


# argv: Optional explicitly selected native CLI and experimental-schema export flag.
# Raises FileNotFoundError if the installed Codex executable is unavailable.
# Raises subprocess.CalledProcessError for the original schema-generation failure.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codex-executable")
    parser.add_argument("--experimental", action="store_true")
    arguments = parser.parse_args(argv)
    executable = shutil.which(arguments.codex_executable or "codex")
    if executable is None:
        raise FileNotFoundError("Installed Codex executable is unavailable")
    output = Path(__file__).resolve().parents[2] / "artifacts/cody-codex-audio/schema"
    output.mkdir(parents=True, exist_ok=True)
    command = [executable, "app-server", "generate-json-schema", "--out", str(output)]
    if arguments.experimental:
        command.append("--experimental")
    subprocess.run(command, check=True)
    print(f"Installed Codex protocol exported to {output}")


if __name__ == "__main__":
    main()
