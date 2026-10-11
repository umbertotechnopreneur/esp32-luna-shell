"""Export the installed Codex protocol for the version-specific audio bridge."""

from pathlib import Path
import shutil
import subprocess


# No parameters: exports protocol descriptions, without starting a chat or model.
# Raises FileNotFoundError if the installed Codex executable is unavailable.
# Raises subprocess.CalledProcessError for the original schema-generation failure.
def main():
    executable = shutil.which("codex")
    if executable is None:
        raise FileNotFoundError("Installed Codex executable is unavailable")
    output = Path(__file__).resolve().parents[2] / "artifacts/cody-codex-audio/schema"
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([executable, "app-server", "generate-json-schema", "--out", str(output)], check=True)
    print(f"Installed Codex protocol exported to {output}")


if __name__ == "__main__":
    main()
