"""Explicit public audio queue configuration; no endpoint or API credentials."""

from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import tempfile

from ccp_windows import runtime_directory


@dataclass(frozen=True)
class NativeAudioConfig:
    executable: str = ""
    source: str = "unconfigured"


# executable: Explicit absolute installed native Codex path, never command arguments.
# Raises ValueError or OSError for invalid or missing executable selection.
def validate_executable(executable: str) -> str:
    if (not isinstance(executable, str) or not 1 <= len(executable) <= 1024 or
            any(ord(character) < 32 for character in executable)):
        raise ValueError("Select an absolute installed native Codex executable")
    path = Path(executable)
    if not path.is_absolute() or path.name.lower() not in ("codex", "codex.exe"):
        raise ValueError("Select an absolute installed native Codex executable")
    if not path.is_file():
        raise FileNotFoundError("Configured Codex executable is unavailable; run codex-audio setup again")
    return str(path)


# pairs: JSON object members, preserving duplicate fields until validation.
# Raises ValueError rather than selecting one of two conflicting values.
def unique_fields(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Codex audio configuration contains duplicate fields")
        result[key] = value
    return result


# directory: Optional isolated storage owner for fixtures; production uses local user storage.
# environment: Optional explicit environment; executable overrides are intentional.
# Raises original storage/validation errors; old endpoint settings require explicit removal.
def load_config(directory: Path | None = None, environment=None) -> NativeAudioConfig:
    environment = os.environ if environment is None else environment
    if "CODY_CODEX_AUDIO_ENDPOINT" in environment or "CODY_CODEX_AUDIO_TOKEN_FILE" in environment:
        raise ValueError("Remove the obsolete Cody audio endpoint environment settings before queue setup")
    if "CODY_CODEX_AUDIO_EXECUTABLE" in environment:
        return NativeAudioConfig(validate_executable(environment["CODY_CODEX_AUDIO_EXECUTABLE"]), "environment")
    path = (runtime_directory() if directory is None else directory) / "audio-config.json"
    try:
        with path.open("rb") as source:
            payload = source.read(4097)
    except FileNotFoundError:
        return NativeAudioConfig()
    if len(payload) > 4096:
        raise ValueError("Codex audio configuration exceeds its storage bound")
    try:
        value = json.loads(payload.decode("utf-8"), object_pairs_hook=unique_fields)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("Codex audio configuration is not valid UTF-8 JSON") from error
    if (not isinstance(value, dict) or set(value) != {"version", "route", "executable"} or
            type(value["version"]) is not int or value["version"] != 2 or value["route"] != "public_audio_queue"):
        raise ValueError("Codex audio configuration schema is invalid; review existing settings before queue setup")
    return NativeAudioConfig(validate_executable(value["executable"]), "installation")


# configuration: Explicit native executable after the installer verifies the public queue.
# directory: Optional isolated storage owner for fixtures.
# Raises original persistence errors; invalid previous settings are never silently replaced.
def save_config(configuration: NativeAudioConfig, directory: Path | None = None) -> Path:
    owner = runtime_directory() if directory is None else directory
    load_config(owner, environment={})
    value = {"version": 2, "route": "public_audio_queue", "executable": validate_executable(configuration.executable)}
    payload = json.dumps(value, ensure_ascii=True).encode("utf-8")
    if len(payload) > 4096:
        raise ValueError("Codex audio configuration exceeds its storage bound")
    owner.mkdir(parents=True, exist_ok=True)
    staging = owner / "artifacts" / "audio-setup"
    staging.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=staging, delete=False) as output:
            temporary = Path(output.name)
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        destination = owner / "audio-config.json"
        os.replace(temporary, destination)
    except BaseException as error:
        if temporary is not None:
            try:
                temporary.unlink(missing_ok=True)
            except OSError as cleanup_error:
                error.add_note(f"Codex audio setup staging cleanup failed: {cleanup_error}")
        raise
    return destination
